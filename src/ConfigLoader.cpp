#include "ConfigLoader.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cwctype>

namespace
{
    std::wstring Trim(const std::wstring& s)
    {
        size_t start = s.find_first_not_of(L" \t\r\n");
        size_t end   = s.find_last_not_of(L" \t\r\n");
        if (start == std::wstring::npos)
            return L"";
        return s.substr(start, end - start + 1);
    }

    int ExtractTrailingNumber(const std::wstring& s)
    {
        std::wstring digits;
        for (auto it = s.rbegin(); it != s.rend() && iswdigit(*it); ++it)
            digits.insert(digits.begin(), *it);
        if (digits.empty())
            return 0;
        return _wtoi(digits.c_str());
    }

    // Splits "id|path|displayName" (displayName optional). Windows paths do
    // not legally contain '|', so this is a safe, dependency-free encoding
    // for a single INI value.
    bool ParseLibraryValue(const std::wstring& value, LibraryItem& outItem)
    {
        size_t p1 = value.find(L'|');
        if (p1 == std::wstring::npos)
            return false;
        size_t p2 = value.find(L'|', p1 + 1);

        outItem.id = _wtoi(value.substr(0, p1).c_str());
        if (outItem.id <= 0)
            return false;

        if (p2 == std::wstring::npos)
        {
            outItem.filePath = value.substr(p1 + 1);
        }
        else
        {
            outItem.filePath = value.substr(p1 + 1, p2 - p1 - 1);
            outItem.displayName = value.substr(p2 + 1);
        }

        if (outItem.displayName.empty())
        {
            size_t slash = outItem.filePath.find_last_of(L"\\/");
            outItem.displayName = (slash == std::wstring::npos) ? outItem.filePath : outItem.filePath.substr(slash + 1);
        }
        return !outItem.filePath.empty();
    }
}

bool ConfigLoader::Load(const std::wstring& path, std::vector<OutputConfig>& outConfigs, MediaLibrary& outLibrary)
{
    std::wifstream file(path);
    if (!file.is_open())
        return false;

    outLibrary.Clear();

    enum class Section { None, Library, Monitor };
    Section section = Section::None;

    OutputConfig current;
    bool haveSection = false;
    bool legacyFilePending = false;
    std::wstring legacyFile;
    std::vector<LibraryItem> libraryItems;

    auto flush = [&]()
    {
        if (!haveSection || current.sectionName.empty())
            return;

        // Legacy v1 file: this section had "File=" but no LibraryId - import
        // the path into the library now so the rest of the app only ever
        // deals with LibraryId.
        if (legacyFilePending && !legacyFile.empty())
            current.libraryId = outLibrary.Add(legacyFile);

        outConfigs.push_back(current);
    };

    std::wstring line;
    while (std::getline(file, line))
    {
        std::wstring trimmed = Trim(line);
        if (trimmed.empty() || trimmed[0] == L';' || trimmed[0] == L'#')
            continue;

        if (trimmed.front() == L'[' && trimmed.back() == L']')
        {
            if (section == Section::Monitor)
                flush();

            std::wstring name = trimmed.substr(1, trimmed.size() - 2);
            if (_wcsicmp(name.c_str(), L"Library") == 0)
            {
                section = Section::Library;
                haveSection = false;
            }
            else
            {
                section = Section::Monitor;
                current = OutputConfig{};
                current.sectionName  = name;
                current.monitorIndex = ExtractTrailingNumber(name);
                current.enabled      = true; // presence of the section = this output was active
                legacyFilePending = false;
                legacyFile.clear();
                haveSection = true;
            }
            continue;
        }

        size_t eq = trimmed.find(L'=');
        if (eq == std::wstring::npos)
            continue;

        std::wstring key   = Trim(trimmed.substr(0, eq));
        std::wstring value = Trim(trimmed.substr(eq + 1));

        if (section == Section::Library)
        {
            LibraryItem item;
            if (ParseLibraryValue(value, item))
                libraryItems.push_back(item);
            continue;
        }

        if (section != Section::Monitor || !haveSection)
            continue;

        if (_wcsicmp(key.c_str(), L"LibraryId") == 0)
            current.libraryId = _wtoi(value.c_str());
        else if (_wcsicmp(key.c_str(), L"File") == 0)
        {
            // Legacy key. Only used if this section has no LibraryId.
            legacyFilePending = true;
            legacyFile = value;
        }
        else if (_wcsicmp(key.c_str(), L"Width") == 0)
            current.width = _wtoi(value.c_str());
        else if (_wcsicmp(key.c_str(), L"Height") == 0)
            current.height = _wtoi(value.c_str());
        else if (_wcsicmp(key.c_str(), L"RefreshHz") == 0)
            current.refreshHz = _wtoi(value.c_str());
        else if (_wcsicmp(key.c_str(), L"Rotation") == 0)
            current.rotationDeg = _wtoi(value.c_str());
        else if (_wcsicmp(key.c_str(), L"Loop") == 0)
            current.loop = (_wtoi(value.c_str()) != 0);
        else if (_wcsicmp(key.c_str(), L"Mute") == 0)
            current.mute = (_wtoi(value.c_str()) != 0);
    }

    if (section == Section::Monitor)
        flush();

    // Load the saved library (v2 files) after the loop so LoadEntries can
    // preserve the saved ids; for v1 files libraryItems is empty and the
    // per-section legacy imports above (outLibrary.Add) already populated it
    // with freshly allocated ids.
    if (!libraryItems.empty())
        outLibrary.LoadEntries(libraryItems);

    // Resolve filePath for every config now that the library is complete.
    for (auto& cfg : outConfigs)
    {
        if (const LibraryItem* item = outLibrary.Find(cfg.libraryId))
            cfg.filePath = item->filePath;
    }

    return !outConfigs.empty();
}

bool ConfigLoader::Save(const std::wstring& path, const std::vector<OutputConfig>& configs, const MediaLibrary& library)
{
    std::wofstream file(path, std::ios::trunc);
    if (!file.is_open())
        return false;

    file << L"; ScreenRelay config - generated from Settings window\r\n";
    file << L"; Format v2: videos live in [Library] and monitors reference them by LibraryId.\r\n\r\n";

    file << L"[Library]\r\n";
    int itemNum = 1;
    for (const auto& item : library.Items())
    {
        file << L"Item" << itemNum++ << L"=" << item.id << L"|" << item.filePath << L"|" << item.displayName << L"\r\n";
    }
    file << L"\r\n";

    for (const auto& cfg : configs)
    {
        std::wstring section = cfg.sectionName.empty()
            ? (L"Monitor" + std::to_wstring(cfg.monitorIndex))
            : cfg.sectionName;

        file << L"[" << section << L"]\r\n";
        file << L"LibraryId=" << cfg.libraryId << L"\r\n";
        file << L"Width=" << cfg.width << L"\r\n";
        file << L"Height=" << cfg.height << L"\r\n";
        file << L"RefreshHz=" << cfg.refreshHz << L"\r\n";
        file << L"Rotation=" << cfg.rotationDeg << L"\r\n";
        file << L"Loop=" << (cfg.loop ? 1 : 0) << L"\r\n";
        file << L"Mute=" << (cfg.mute ? 1 : 0) << L"\r\n\r\n";
    }

    return true;
}
