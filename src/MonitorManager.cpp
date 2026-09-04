#include "MonitorManager.h"
#include <algorithm>

namespace
{
    struct EnumContext
    {
        std::vector<MonitorInfo>* monitors;
    };
}

std::wstring MonitorManager::QueryFriendlyName(const std::wstring& deviceName)
{
    // deviceName looks like L"\\.\DISPLAY1". EnumDisplayDevicesW with that
    // name as the adapter and index 0 returns the attached monitor's
    // DeviceString, which is a human-readable model name when the driver
    // exposes one (falls back to "Generic PnP Monitor" otherwise - still
    // friendlier than a raw device path).
    DISPLAY_DEVICEW dd{};
    dd.cb = sizeof(dd);
    if (EnumDisplayDevicesW(deviceName.c_str(), 0, &dd, 0) && dd.DeviceString[0] != L'\0')
        return dd.DeviceString;
    return deviceName;
}

BOOL CALLBACK MonitorManager::MonitorEnumProc(HMONITOR hMonitor, HDC /*hdc*/, LPRECT lprcMonitor, LPARAM dwData)
{
    auto* ctx = reinterpret_cast<EnumContext*>(dwData);

    MONITORINFOEXW mi{};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(hMonitor, &mi))
        return TRUE; // keep enumerating

    MonitorInfo info{};
    info.deviceName   = mi.szDevice;
    info.friendlyName = QueryFriendlyName(mi.szDevice);
    info.rect         = *lprcMonitor;
    info.isPrimary    = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;
    info.index        = static_cast<int>(ctx->monitors->size()) + 1;

    ctx->monitors->push_back(info);
    return TRUE;
}

std::vector<MonitorInfo> MonitorManager::EnumerateMonitors()
{
    std::vector<MonitorInfo> monitors;
    EnumContext ctx{ &monitors };
    EnumDisplayMonitors(nullptr, nullptr, MonitorEnumProc, reinterpret_cast<LPARAM>(&ctx));

    // Put the primary monitor first, keep relative order otherwise stable,
    // then renumber 1..N. This tends to match what Display Settings calls
    // "Monitor 1" but is not guaranteed - always let the user confirm with /list.
    std::stable_sort(monitors.begin(), monitors.end(), [](const MonitorInfo& a, const MonitorInfo& b)
    {
        if (a.isPrimary != b.isPrimary)
            return a.isPrimary; // primary first
        return false;
    });

    for (size_t i = 0; i < monitors.size(); ++i)
        monitors[i].index = static_cast<int>(i) + 1;

    return monitors;
}

bool MonitorManager::SetMonitorMode(const std::wstring& deviceName,
                                     int width,
                                     int height,
                                     int refreshHz,
                                     MonitorRotation rotation)
{
    DEVMODEW dm{};
    dm.dmSize = sizeof(dm);

    // Start from the monitor's current settings so unspecified fields are preserved.
    if (!EnumDisplaySettingsW(deviceName.c_str(), ENUM_CURRENT_SETTINGS, &dm))
        return false;

    DWORD currentOrientation = dm.dmDisplayOrientation;
    DWORD targetOrientation  = (rotation == MonitorRotation::Unchanged)
                                   ? currentOrientation
                                   : static_cast<DWORD>(rotation);

    // If the orientation is changing between "landscape-like" (0/180) and
    // "portrait-like" (90/270), Windows expects dmPelsWidth/Height to already
    // describe the panel in its *new* orientation. We swap automatically only
    // when the caller did NOT explicitly give both width and height, to avoid
    // silently overriding an intentional value. If you find your chosen
    // driver behaves the other way around, swap Width/Height in config.ini.
    int targetWidth  = width  > 0 ? width  : static_cast<int>(dm.dmPelsWidth);
    int targetHeight = height > 0 ? height : static_cast<int>(dm.dmPelsHeight);

    bool wasPortrait   = (currentOrientation == static_cast<DWORD>(MonitorRotation::Deg90) ||
                           currentOrientation == static_cast<DWORD>(MonitorRotation::Deg270));
    bool willBePortrait = (targetOrientation == static_cast<DWORD>(MonitorRotation::Deg90) ||
                            targetOrientation == static_cast<DWORD>(MonitorRotation::Deg270));

    if (width <= 0 && height <= 0 && wasPortrait != willBePortrait)
    {
        // Neither dimension was explicitly requested: auto-swap to match the new orientation.
        std::swap(targetWidth, targetHeight);
    }

    dm.dmPelsWidth          = static_cast<DWORD>(targetWidth);
    dm.dmPelsHeight         = static_cast<DWORD>(targetHeight);
    dm.dmDisplayOrientation = targetOrientation;
    dm.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_DISPLAYORIENTATION;

    if (refreshHz > 0)
    {
        dm.dmDisplayFrequency = static_cast<DWORD>(refreshHz);
        dm.dmFields |= DM_DISPLAYFREQUENCY;
    }

    LONG result = ChangeDisplaySettingsExW(deviceName.c_str(), &dm, nullptr,
                                            CDS_UPDATEREGISTRY | CDS_NORESET, nullptr);
    if (result != DISP_CHANGE_SUCCESSFUL)
        return false;

    // Apply the queued change across all displays now.
    result = ChangeDisplaySettingsExW(nullptr, nullptr, nullptr, 0, nullptr);
    return result == DISP_CHANGE_SUCCESSFUL;
}

bool MonitorManager::RestoreMonitorMode(const std::wstring& deviceName)
{
    // Passing nullptr as devmode restores the registry (default) settings
    // for that device.
    LONG result = ChangeDisplaySettingsExW(deviceName.c_str(), nullptr, nullptr, 0, nullptr);
    return result == DISP_CHANGE_SUCCESSFUL;
}
