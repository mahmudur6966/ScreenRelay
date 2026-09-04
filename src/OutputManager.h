#pragma once
//
// OutputManager.h
//
// Owns the currently-running VideoWindow outputs and the set of monitors
// whose display mode this app has changed. Applying a new configuration
// (from the Settings window, or from config.ini at startup) stops whatever
// is currently running and starts fresh from the new settings - this keeps
// the logic simple and robust for a "settings button" workflow where the
// user may reconfigure things at any time while the app is running.
//
#include <windows.h>
#include <map>
#include <memory>
#include <vector>
#include <string>

#include "ConfigLoader.h"
#include "MonitorManager.h"
#include "VideoWindow.h"

class OutputManager
{
public:
    ~OutputManager();

    // Stops all current outputs and restores any changed monitor modes,
    // then applies the given configs (one enabled output per entry).
    // configs[i].filePath must already be resolved (SettingsWindow does
    // this by looking up libraryId in its MediaLibrary before calling).
    // On partial failure (e.g. one monitor no longer present), continues
    // with the rest and appends a description to warningsOut.
    bool Apply(const std::vector<OutputConfig>& configs,
               const std::vector<MonitorInfo>& monitors,
               std::wstring& warningsOut);

    // Stops all outputs and restores any monitor modes this app changed.
    void StopAll();

    bool HasActiveOutputs() const { return !m_windows.empty(); }

    // Live status for a given monitor index, for the Settings window to
    // poll and display (Idle if there is no output running on it).
    PlaybackStatus GetStatus(int monitorIndex) const;
    std::wstring GetLastError(int monitorIndex) const;

private:
    std::map<int, std::unique_ptr<VideoWindow>> m_windows; // key = monitor index
    std::vector<std::wstring> m_changedDevices;
};
