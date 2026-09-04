#include "OutputManager.h"
#include <sstream>

namespace
{
    MonitorRotation ToRotation(int degrees)
    {
        switch (degrees)
        {
            case 0:   return MonitorRotation::Deg0;
            case 90:  return MonitorRotation::Deg90;
            case 180: return MonitorRotation::Deg180;
            case 270: return MonitorRotation::Deg270;
            default:  return MonitorRotation::Unchanged; // -1 or anything unexpected
        }
    }

    const MonitorInfo* FindMonitorByIndex(const std::vector<MonitorInfo>& monitors, int index)
    {
        for (const auto& m : monitors)
            if (m.index == index)
                return &m;
        return nullptr;
    }
}

OutputManager::~OutputManager()
{
    StopAll();
}

void OutputManager::StopAll()
{
    m_windows.clear(); // each VideoWindow's destructor stops playback + destroys its window

    for (const auto& device : m_changedDevices)
        MonitorManager::RestoreMonitorMode(device);
    m_changedDevices.clear();
}

bool OutputManager::Apply(const std::vector<OutputConfig>& configs,
                           const std::vector<MonitorInfo>& monitors,
                           std::wstring& warningsOut)
{
    StopAll();

    std::wstringstream warnings;
    bool anyStarted = false;

    for (const auto& cfg : configs)
    {
        const MonitorInfo* mon = FindMonitorByIndex(monitors, cfg.monitorIndex);
        if (!mon)
        {
            warnings << L"Monitor " << cfg.monitorIndex << L" is not currently connected - skipped.\r\n";
            continue;
        }
        if (cfg.filePath.empty())
        {
            warnings << L"Monitor " << cfg.monitorIndex << L" has no video assigned - skipped.\r\n";
            continue;
        }

        if (cfg.width > 0 || cfg.height > 0 || cfg.refreshHz > 0 || cfg.rotationDeg >= 0)
        {
            bool ok = MonitorManager::SetMonitorMode(
                mon->deviceName, cfg.width, cfg.height, cfg.refreshHz, ToRotation(cfg.rotationDeg));
            if (ok)
                m_changedDevices.push_back(mon->deviceName);
            else
                warnings << L"Monitor " << cfg.monitorIndex << L": could not apply the requested display mode, continuing with its current mode.\r\n";
        }

        // Re-fetch bounds in case the mode change moved/resized this monitor
        // within the virtual desktop layout.
        std::vector<MonitorInfo> refreshed = MonitorManager::EnumerateMonitors();
        const MonitorInfo* monAfter = FindMonitorByIndex(refreshed, cfg.monitorIndex);
        RECT targetRect = monAfter ? monAfter->rect : mon->rect;

        auto win = std::make_unique<VideoWindow>();
        if (!win->Create(targetRect, L"ScreenRelay - Monitor " + std::to_wstring(cfg.monitorIndex)))
        {
            warnings << L"Monitor " << cfg.monitorIndex << L": failed to create the output window.\r\n";
            continue;
        }
        win->SetMuted(cfg.mute);
        if (!win->PlayFile(cfg.filePath, cfg.loop))
        {
            warnings << L"Monitor " << cfg.monitorIndex << L": failed to start playback of \"" << cfg.filePath << L"\".\r\n";
        }

        m_windows[cfg.monitorIndex] = std::move(win);
        anyStarted = true;
    }

    warningsOut = warnings.str();
    return anyStarted;
}

PlaybackStatus OutputManager::GetStatus(int monitorIndex) const
{
    auto it = m_windows.find(monitorIndex);
    if (it == m_windows.end())
        return PlaybackStatus::Idle;
    return it->second->GetStatus();
}

std::wstring OutputManager::GetLastError(int monitorIndex) const
{
    auto it = m_windows.find(monitorIndex);
    if (it == m_windows.end())
        return L"";
    return it->second->GetLastError();
}
