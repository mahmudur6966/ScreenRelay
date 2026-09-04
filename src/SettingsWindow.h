#pragma once
//
// SettingsWindow.h
//
// The app's main window. Two parts:
//
//  1) A "Media Library" panel: add video files once, they show up as a
//     named pool. Any pool entry can be assigned to any monitor - the same
//     video can drive two monitors at once, or each monitor can get its
//     own file.
//
//  2) One card per detected monitor: enable/disable it as an output, pick
//     a library video for it, optionally force a custom
//     width/height/refresh-rate/rotation, mute its audio, and see its live
//     playback status. "Start / Apply" (re)starts the live outputs
//     immediately via OutputManager; "Save as Default" writes config.ini so
//     the layout reloads automatically next launch.
//
// Monitor list is dynamic: WM_DISPLAYCHANGE (fired on hot-plug/unplug or a
// resolution change) triggers a debounced re-scan that rebuilds the cards
// without disturbing any outputs already running.
//
// Closing this window (the X button) just hides it - the app keeps running
// with a tray icon so outputs stay up; use the tray menu's "Exit" to
// actually quit.
//
// Needed so <windows.h>/<winuser.h> declare GetDpiForWindow, WM_DPICHANGED,
// and DPI_AWARENESS_CONTEXT_* (Windows 10 per-monitor DPI v2 APIs).
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#ifndef WINVER
#define WINVER 0x0A00
#endif

#include <windows.h>
#include <vector>
#include <string>
#include <utility>

#include "ConfigLoader.h"
#include "MediaLibrary.h"
#include "MonitorManager.h"
#include "OutputManager.h"
#include "LicenseManager.h"

class SettingsWindow
{
public:
    explicit SettingsWindow(OutputManager& outputManager);
    ~SettingsWindow();

    // Creates the window (hidden) and its tray icon. Call Show() to display it.
    bool Create(HINSTANCE hInstance);

    void Show();
    void Hide();

    // If autoStart is true, immediately applies whatever was loaded from
    // config.ini (used for the /autostart command-line option).
    void LoadInitialConfig(const std::wstring& configPath, bool autoStart);

    // Runs the trial/license gate (shows the trial or expiry prompt, and
    // the activation form if the person chooses to enter a key). Call once
    // after Create() and before LoadInitialConfig()/Show(). Returns false
    // if the person chose Exit at an expired-trial gate - the caller
    // should shut down without ever loading a config or showing the window.
    // When autoStart is true and the trial is still active (not expired),
    // this skips the reminder entirely so an unattended/kiosk boot isn't
    // blocked waiting for a click - an expired trial still gates even then.
    bool CheckLicenseAtStartup(bool autoStart);

    HWND GetHwnd() const { return m_hwnd; }

private:
    struct RowControls
    {
        int monitorIndex = 0;
        HWND checkEnable = nullptr;
        HWND labelInfo = nullptr;
        HWND comboVideo = nullptr;
        HWND buttonAdd = nullptr;
        HWND editWidth = nullptr;
        HWND editHeight = nullptr;
        HWND editHz = nullptr;
        HWND comboRotation = nullptr;
        HWND checkLoop = nullptr;
        HWND checkMute = nullptr;
        HWND labelStatus = nullptr;
        HWND cardFrame = nullptr;
    };

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    void BuildLayout();
    void DestroyRows();
    void ApplyFontToChildren();
    int Scale(int px) const;
    void CreateThemeResources();
    void DestroyThemeResources();
    void DrawOwnerButton(LPDRAWITEMSTRUCT dis) const;

    void RefreshLibraryListBox();
    void RefreshAllVideoCombos();
    int  SelectedLibraryIdForRow(const RowControls& row) const;
    void SetRowVideoSelection(const RowControls& row, int libraryId);
    void GenerateAndAttachThumbnail(int libraryId);
    void RegenerateAllThumbnails();
    void DrawLibraryListItem(LPDRAWITEMSTRUCT dis) const;

    void OnAddVideo();
    void OnRemoveSelectedVideo();
    void OnStartApply();
    void OnStopAll();
    void OnSaveDefault();
    void OnLoadConfig();
    void OnRefreshMonitors();
    void OnAbout();
    void OnStatusPollTick();
    void OnToggleStartWithWindows();
    void OnEnterLicenseKey();
    void UpdateTitleForLicenseState();

    std::vector<OutputConfig> CollectConfigsFromRows(bool onlyEnabled) const;
    void ShowTrayMenu();
    void SetStatus(const std::wstring& text);
    static const wchar_t* StatusText(PlaybackStatus status);

    OutputManager& m_outputManager;
    MediaLibrary m_library;
    LicenseManager m_license;

    HINSTANCE m_hInstance = nullptr;
    HWND m_hwnd = nullptr;
    HWND m_statusBar = nullptr;
    HWND m_listLibrary = nullptr;
    HWND m_btnAddVideo = nullptr;
    HWND m_btnRemoveVideo = nullptr;
    HWND m_btnStart = nullptr; // owner-drawn accent button
    HFONT m_uiFont = nullptr;
    HFONT m_uiFontBold = nullptr;
    HBRUSH m_windowBrush = nullptr;  // app background
    HBRUSH m_cardBrush = nullptr;    // card fill
    HPEN   m_cardBorderPen = nullptr;
    HBRUSH m_accentBrush = nullptr;
    HBRUSH m_accentBrushPressed = nullptr;

    std::vector<RowControls> m_rows;
    std::vector<MonitorInfo> m_monitors;
    std::vector<OutputConfig> m_loadedConfigs; // used to prefill rows on (re)build, matched by monitorIndex
    std::vector<std::pair<std::wstring, OutputConfig>> m_configsByDevice; // same, matched by device name (survives hot-plug reordering)
    std::wstring m_configPath = L"config.ini";
    bool m_trayIconAdded = false;
    UINT m_dpi = 96;

    static constexpr UINT_PTR kDisplayChangeTimerId = 100;
    static constexpr UINT_PTR kStatusPollTimerId = 101;
};
