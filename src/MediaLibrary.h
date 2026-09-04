#pragma once
//
// MediaLibrary.h
//
// A small in-memory pool of video files the user has added ("Media
// Library"). Monitors reference a video entry by stable integer id instead
// of embedding a raw path, so the *same* video can be assigned to several
// monitors at once (played independently/looped on each), and a monitor's
// assignment survives library edits (rename-on-disk aside).
//
// Persistence lives in ConfigLoader; MediaLibrary is just the runtime pool
// - id allocation, de-duplication by path, and (since each entry owns a
// GDI thumbnail bitmap) cleanup of those bitmaps on remove/clear/destroy.
//
#include <windows.h>
#include <string>
#include <vector>

struct LibraryItem
{
    int          id = 0;      // stable for the lifetime of the app / config file
    std::wstring filePath;    // full path on disk
    std::wstring displayName; // shown in the UI (defaults to the file name)
    HBITMAP      thumbnail = nullptr; // owned by this entry; freed by MediaLibrary
};

class MediaLibrary
{
public:
    ~MediaLibrary();

    // Adds filePath if not already present (compared case-insensitively);
    // returns the id of the (possibly pre-existing) entry. Empty path -> 0.
    int Add(const std::wstring& filePath);

    // Removes an entry by id (freeing its thumbnail bitmap, if any). Any
    // monitor row still referencing this id should fall back to "(no
    // video)" - SettingsWindow handles that.
    bool Remove(int id);

    const LibraryItem* Find(int id) const;
    const LibraryItem* FindByPath(const std::wstring& filePath) const;

    // Attaches a thumbnail bitmap to an entry, replacing (and freeing) any
    // previous one. MediaLibrary takes ownership of bmp - do not delete it
    // after calling this. Returns false (and does not take ownership,
    // caller must clean up bmp itself) if id doesn't exist.
    bool SetThumbnail(int id, HBITMAP bmp);

    const std::vector<LibraryItem>& Items() const { return m_items; }
    bool Empty() const { return m_items.empty(); }

    // Used by ConfigLoader when restoring a saved library section - takes
    // entries as-is (including their saved ids) and continues id allocation
    // after the highest id seen. Thumbnails are not persisted, so loaded
    // entries start with thumbnail == nullptr; SettingsWindow regenerates
    // them after a load.
    void LoadEntries(const std::vector<LibraryItem>& items);

    void Clear();

private:
    static std::wstring ExtractFileName(const std::wstring& path);

    std::vector<LibraryItem> m_items;
    int m_nextId = 1;
};
