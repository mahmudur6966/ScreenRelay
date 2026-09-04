//
// main.cpp
//
// ScreenRelay
// ------------------------
// Shows a Settings window where you build a small "Media Library" of video
// files, then per detected monitor: enable it as an output, assign it one
// of the library videos (the same video can drive several monitors at
// once), and optionally set a custom resolution/refresh-rate/rotation.
// Click "Start / Apply" to go live immediately, "Save as Default" to write
// config.ini for next launch.
//
// The Settings window minimizes to a system tray icon when closed (X) so
// your fullscreen outputs keep running - right-click the tray icon to bring
// Settings back, stop all outputs, or exit for real.
//
// Command line:
//   ScreenRelay.exe                 -> opens Settings, loads config.ini into the rows (does not auto-start)
//   ScreenRelay.exe /autostart       -> loads config.ini AND immediately starts those outputs, Settings window stays hidden (tray only)
//   ScreenRelay.exe myconfig.ini     -> use a specific config file instead of config.ini
//   ScreenRelay.exe /list            -> lists detected monitors and exits (no window)
//

// Needed so <windows.h>/<winuser.h> declare the Windows 10 per-monitor-DPI
// types (DPI_AWARENESS_CONTEXT etc.) used below and by SettingsWindow.
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#ifndef WINVER
#define WINVER 0x0A00
#endif

#include <windows.h>
#include <mfapi.h>
#include <objbase.h>
#include <shellapi.h>
#include <vector>
#include <sstream>

#include "MonitorManager.h"
#include "OutputManager.h"
#include "SettingsWindow.h"

// Pulls in the ComCtl32 v6 manifest dependency at link time, so standard
// controls (buttons, checkboxes, combo boxes, the status bar) render with
// current Windows visual styles instead of the classic Windows 98 look.
// This is the single highest-leverage change for a "modern" look in a
// plain Win32 app with no UI framework.
#pragma comment(linker, \
    "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' " \
    "version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

namespace
{
    const wchar_t* kSingleInstanceMutexName = L"Local\\ScreenRelay_SingleInstance";

    void PrintMonitorList(const std::vector<MonitorInfo>& monitors)
    {
        std::wstringstream ss;
        ss << L"Detected monitors:\r\n\r\n";
        for (const auto& m : monitors)
        {
            ss << L"  Monitor " << m.index
               << (m.isPrimary ? L" (Primary)" : L"")
               << L"  " << m.friendlyName
               << L"  device=" << m.deviceName
               << L"  bounds=(" << m.rect.left << L"," << m.rect.top << L")-("
               << m.rect.right << L"," << m.rect.bottom << L")"
               << L"  size=" << (m.rect.right - m.rect.left) << L"x" << (m.rect.bottom - m.rect.top)
               << L"\r\n";
        }
        MessageBoxW(nullptr, ss.str().c_str(), L"ScreenRelay - Monitor List", MB_OK);
    }

    // Per-monitor DPI awareness so text and layout stay crisp when the
    // Settings window is dragged between monitors with different scaling
    // (common on a rig where the control monitor and the HDMI outputs run
    // at different resolutions). Falls back gracefully on older Windows.
    void EnableDpiAwareness()
    {
        HMODULE user32 = GetModuleHandleW(L"user32.dll");
        if (!user32)
            return;

        using SetCtxFn = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);
        auto setCtx = reinterpret_cast<SetCtxFn>(GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
        if (setCtx)
        {
            setCtx(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
            return;
        }

        // Windows 8.1-era fallback.
        using SetAwareFn = HRESULT(WINAPI*)(int);
        HMODULE shcore = LoadLibraryW(L"Shcore.dll");
        if (shcore)
        {
            auto setAware = reinterpret_cast<SetAwareFn>(GetProcAddress(shcore, "SetProcessDpiAwareness"));
            if (setAware)
                setAware(2 /* PROCESS_PER_MONITOR_DPI_AWARE */);
            FreeLibrary(shcore);
        }
    }
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE /*hPrevInstance*/,
                     LPWSTR /*lpCmdLine*/, int /*nCmdShow*/)
{
    EnableDpiAwareness();

    // --- Single instance guard ---
    // Two copies fighting over the same physical monitors would be
    // confusing (each restoring/overriding the other's display mode).
    HANDLE singleInstanceMutex = CreateMutexW(nullptr, TRUE, kSingleInstanceMutexName);
    bool alreadyRunning = singleInstanceMutex && GetLastError() == ERROR_ALREADY_EXISTS;

    // --- Parse command line ---
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);

    std::wstring configPath = L"config.ini";
    bool listOnly = false;
    bool autoStart = false;

    for (int i = 1; i < argc; ++i)
    {
        std::wstring arg = argv[i];
        if (arg == L"/list" || arg == L"--list")
            listOnly = true;
        else if (arg == L"/autostart" || arg == L"--autostart")
            autoStart = true;
        else
            configPath = arg;
    }
    if (argv) LocalFree(argv);

    if (alreadyRunning && !listOnly)
    {
        MessageBoxW(nullptr,
            L"ScreenRelay is already running - check the system tray.",
            L"ScreenRelay", MB_OK | MB_ICONINFORMATION);
        if (singleInstanceMutex) CloseHandle(singleInstanceMutex);
        return 0;
    }

    // --- Init COM + Media Foundation ---
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hr))
    {
        if (singleInstanceMutex) CloseHandle(singleInstanceMutex);
        return -1;
    }

    hr = MFStartup(MF_VERSION);
    if (FAILED(hr))
    {
        CoUninitialize();
        if (singleInstanceMutex) CloseHandle(singleInstanceMutex);
        return -1;
    }

    if (listOnly)
    {
        PrintMonitorList(MonitorManager::EnumerateMonitors());
        MFShutdown();
        CoUninitialize();
        if (singleInstanceMutex) CloseHandle(singleInstanceMutex);
        return 0;
    }

    OutputManager outputManager;
    SettingsWindow settings(outputManager);

    if (!settings.Create(hInstance))
    {
        MessageBoxW(nullptr, L"Failed to create the Settings window.", L"ScreenRelay", MB_OK | MB_ICONERROR);
        MFShutdown();
        CoUninitialize();
        if (singleInstanceMutex) CloseHandle(singleInstanceMutex);
        return -1;
    }

    // Trial/license gate - see SettingsWindow::CheckLicenseAtStartup for
    // exactly what this shows and when. Returns false only if the person
    // chose Exit at an expired-trial prompt, in which case we shut down
    // without ever loading a config or showing the window.
    if (!settings.CheckLicenseAtStartup(autoStart))
    {
        MFShutdown();
        CoUninitialize();
        if (singleInstanceMutex) CloseHandle(singleInstanceMutex);
        return 0;
    }

    settings.LoadInitialConfig(configPath, autoStart);

    // Show the Settings window unless we were told to just autostart from a
    // saved config - in that case stay in the tray so the outputs are
    // undisturbed (handy for launching this at Windows startup).
    if (!autoStart)
        settings.Show();

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    MFShutdown();
    CoUninitialize();
    if (singleInstanceMutex) CloseHandle(singleInstanceMutex);
    return static_cast<int>(msg.wParam);
}
