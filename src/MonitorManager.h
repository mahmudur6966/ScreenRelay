#pragma once
//
// MonitorManager.h
//
// Enumerates physical monitors attached to the desktop and allows changing
// a monitor's resolution, refresh rate and rotation using the native
// Win32 display APIs (EnumDisplayMonitors / ChangeDisplaySettingsEx).
//
// No third-party dependencies - everything here is plain Win32 (user32 / gdi32).
//
#include <windows.h>
#include <vector>
#include <string>

struct MonitorInfo
{
    std::wstring deviceName;   // e.g. L"\\\\.\\DISPLAY1" - used by EnumDisplaySettingsEx / ChangeDisplaySettingsEx
    std::wstring friendlyName; // e.g. L"Dell U2723QE" when Windows can report it, else falls back to deviceName
    RECT         rect;         // Monitor bounds in virtual-desktop coordinates
    int          index;        // 1-based order as returned by EnumDisplayMonitors (matches "Monitor 1/2/3..." in Windows Display Settings, best effort)
    bool         isPrimary;
};

// Orientation values match DEVMODE.dmDisplayOrientation:
//   0 = DMDO_DEFAULT (0 deg), 1 = DMDO_90, 2 = DMDO_180, 3 = DMDO_270
enum class MonitorRotation : DWORD
{
    Deg0   = 0,
    Deg90  = 1,
    Deg180 = 2,
    Deg270 = 3,
    Unchanged = 0xFFFFFFFF
};

class MonitorManager
{
public:
    // Returns all monitors currently attached, ordered the same way Windows
    // enumerates them (this is a best-effort match to the numbers shown in
    // "Display settings" - it is not guaranteed by the OS to be identical,
    // so always let the user verify with the /list command line option).
    static std::vector<MonitorInfo> EnumerateMonitors();

    // Change resolution / refresh rate / rotation for a single monitor.
    // Pass 0 for width/height/refreshHz to leave that field unchanged.
    // Pass MonitorRotation::Unchanged to leave rotation unchanged.
    // Returns true if the OS accepted the change (DISP_CHANGE_SUCCESSFUL).
    static bool SetMonitorMode(const std::wstring& deviceName,
                                int width,
                                int height,
                                int refreshHz,
                                MonitorRotation rotation);

    // Restores the monitor to the mode Windows considers its "registry"
    // (persisted / default) setting. Call this on shutdown if you changed
    // a mode, so the user's desktop isn't left altered after the app exits.
    static bool RestoreMonitorMode(const std::wstring& deviceName);

private:
    static BOOL CALLBACK MonitorEnumProc(HMONITOR hMonitor, HDC hdc, LPRECT lprcMonitor, LPARAM dwData);
    static std::wstring QueryFriendlyName(const std::wstring& deviceName);
};
