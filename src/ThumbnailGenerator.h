#pragma once
//
// ThumbnailGenerator.h
//
// Grabs a representative frame from a video file using Media Foundation's
// high-level Source Reader (a different, simpler MF API than the MFPlay
// player used for actual playback in VideoWindow - the Source Reader is
// meant exactly for "decode me a few frames" use cases like this one).
//
// NOMINMAX must be defined before <windows.h> is ever included in this
// project (it is, via the .vcxproj) - otherwise windows.h's min/max
// macros mangle the std::min<T>/std::max<T> calls in the .cpp.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <string>

// Decodes a frame partway into filePath and returns it as a top-down 32bpp
// DIB section HBITMAP exactly thumbWidth x thumbHeight in size (the source
// frame is scaled to fit and letterboxed on a dark background, so callers
// never need to handle mismatched aspect ratios). The caller owns the
// returned bitmap (DeleteObject it, or better, hand it to
// MediaLibrary::SetThumbnail which takes ownership).
//
// Returns nullptr on failure (unsupported codec, corrupt file, missing
// codec pack, etc.) - callers should fall back to a placeholder.
//
// This runs synchronously on the calling thread and typically takes tens
// to a few hundred milliseconds depending on the file's codec and
// container; call it off the UI thread for bulk imports if that ever
// becomes noticeable.
HBITMAP GenerateVideoThumbnail(const std::wstring& filePath, int thumbWidth, int thumbHeight);
