#include "MediaLibrary.h"
#include <algorithm>
#include <cwctype>

std::wstring MediaLibrary::ExtractFileName(const std::wstring& path)
{
    size_t pos = path.find_last_of(L"\\/");
    return (pos == std::wstring::npos) ? path : path.substr(pos + 1);
}

MediaLibrary::~MediaLibrary()
{
    Clear();
}

int MediaLibrary::Add(const std::wstring& filePath)
{
    if (filePath.empty())
        return 0;

    if (const LibraryItem* existing = FindByPath(filePath))
        return existing->id;

    LibraryItem item;
    item.id = m_nextId++;
    item.filePath = filePath;
    item.displayName = ExtractFileName(filePath);
    m_items.push_back(item);
    return item.id;
}

bool MediaLibrary::Remove(int id)
{
    auto it = std::find_if(m_items.begin(), m_items.end(),
        [id](const LibraryItem& i) { return i.id == id; });
    if (it == m_items.end())
        return false;
    if (it->thumbnail)
        DeleteObject(it->thumbnail);
    m_items.erase(it);
    return true;
}

const LibraryItem* MediaLibrary::Find(int id) const
{
    for (const auto& item : m_items)
        if (item.id == id)
            return &item;
    return nullptr;
}

const LibraryItem* MediaLibrary::FindByPath(const std::wstring& filePath) const
{
    for (const auto& item : m_items)
    {
        if (item.filePath.size() == filePath.size() &&
            _wcsicmp(item.filePath.c_str(), filePath.c_str()) == 0)
            return &item;
    }
    return nullptr;
}

bool MediaLibrary::SetThumbnail(int id, HBITMAP bmp)
{
    for (auto& item : m_items)
    {
        if (item.id == id)
        {
            if (item.thumbnail && item.thumbnail != bmp)
                DeleteObject(item.thumbnail);
            item.thumbnail = bmp;
            return true;
        }
    }
    return false;
}

void MediaLibrary::LoadEntries(const std::vector<LibraryItem>& items)
{
    Clear(); // frees any existing thumbnails before replacing the list
    m_items = items;
    int maxId = 0;
    for (const auto& item : m_items)
        maxId = std::max(maxId, item.id);
    m_nextId = maxId + 1;
}

void MediaLibrary::Clear()
{
    for (auto& item : m_items)
    {
        if (item.thumbnail)
            DeleteObject(item.thumbnail);
    }
    m_items.clear();
    m_nextId = 1;
}
