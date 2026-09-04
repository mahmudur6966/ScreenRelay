#pragma once
//
// ConfigLoader.h
//
// Minimal INI-style config file reader/writer. Deliberately hand-rolled (no
// third-party JSON/INI library) to keep the project dependency-free -
// only the Windows SDK is required to build.
//
// Config format v2 adds a [Library] section (the media pool) and monitor
// sections reference a video by LibraryId instead of embedding the path
// directly. Old (v1) config.ini files that only have "File=" under a
// [MonitorN] section still load correctly: each such File is imported into
// the library automatically and the resulting id is filled in, so opening
// and re-saving an old file upgrades it in place.
//
#include <string>
#include <map>
#include <vector>
#include "MediaLibrary.h"

struct OutputConfig
{
    std::wstring sectionName;   // e.g. L"Monitor2"
    int          monitorIndex = 0;   // parsed from the section name's trailing digits
    int          libraryId = 0;      // 0 = no video assigned; else id into the MediaLibrary
    std::wstring filePath;      // resolved at runtime from libraryId (or legacy File=) - what OutputManager actually plays
    int          width = 0;     // 0 = leave monitor resolution unchanged
    int          height = 0;
    int          refreshHz = 0; // 0 = leave unchanged
    int          rotationDeg = -1; // -1 = leave unchanged, else 0/90/180/270
    bool         loop = true;
    bool         mute = false;  // true = play this output silently

    // UI-only flag (not written to / read from the "Rotation="-style keys):
    // true if this monitor's row should start checked when the Settings
    // window loads this config. Load() sets this true for every section it
    // finds (presence in the file implies it was an active output).
    bool         enabled = false;
};

class ConfigLoader
{
public:
    // Loads config.ini-style file: a [Library] section (file pool) plus one
    // OutputConfig per [MonitorN] section. Legacy "File=" entries (no
    // [Library] section at all) are auto-imported into outLibrary.
    // Returns true if at least one monitor section was found.
    static bool Load(const std::wstring& path,
                      std::vector<OutputConfig>& outConfigs,
                      MediaLibrary& outLibrary);

    // Writes the library and one [MonitorN] section per entry (N =
    // config.monitorIndex), overwriting the file. Used by the Settings
    // window's "Save as Default" button so the next launch can auto-start
    // with the same layout.
    static bool Save(const std::wstring& path,
                      const std::vector<OutputConfig>& configs,
                      const MediaLibrary& library);
};
