#include "ThumbnailGenerator.h"
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mfobjects.h>
#include <algorithm>

#pragma comment(lib, "mfreadwrite.lib")

namespace
{
    HBITMAP CreateTopDownDib32(int width, int height, void** outBits)
    {
        BITMAPINFO bmi{};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = width;
        bmi.bmiHeader.biHeight = -height; // negative = top-down, matches how we write rows below
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;
        return CreateDIBSection(nullptr, &bmi, DIB_RGB_COLORS, outBits, nullptr, 0);
    }
}

HBITMAP GenerateVideoThumbnail(const std::wstring& filePath, int thumbWidth, int thumbHeight)
{
    if (filePath.empty() || thumbWidth <= 0 || thumbHeight <= 0)
        return nullptr;

    // Enabling video processing on the reader lets it auto-insert a color
    // converter when the request below asks for a subtype (RGB32) that
    // doesn't match what the decoder natively outputs (almost always NV12
    // for H.264 etc). WITHOUT this attribute, SetCurrentMediaType(RGB32)
    // below fails for nearly every real-world video file - this was the
    // actual cause of thumbnails never appearing.
    IMFAttributes* readerAttributes = nullptr;
    MFCreateAttributes(&readerAttributes, 1);
    if (readerAttributes)
        readerAttributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);

    IMFSourceReader* reader = nullptr;
    HRESULT createHr = MFCreateSourceReaderFromURL(filePath.c_str(), readerAttributes, &reader);
    if (readerAttributes) readerAttributes->Release();
    if (FAILED(createHr) || !reader)
        return nullptr;

    // Only the first video stream, decoded straight to RGB32 - let Media
    // Foundation's own color converter handle whatever the source codec is.
    reader->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS), FALSE);
    reader->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), TRUE);

    IMFMediaType* outType = nullptr;
    HRESULT hr = MFCreateMediaType(&outType);
    if (SUCCEEDED(hr)) hr = outType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    if (SUCCEEDED(hr)) hr = outType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
    if (SUCCEEDED(hr)) hr = reader->SetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), nullptr, outType);
    if (outType) outType->Release();

    if (FAILED(hr))
    {
        reader->Release();
        return nullptr;
    }

    // Seek a little into the clip - many files open on a black or fading-in
    // first frame, so ~1 second in tends to give a more representative
    // thumbnail. Not fatal if the source doesn't support seeking.
    PROPVARIANT seekVar;
    PropVariantInit(&seekVar);
    seekVar.vt = VT_I8;
    seekVar.hVal.QuadPart = 10'000'000; // 1 second, in 100ns units
    reader->SetCurrentPosition(GUID_NULL, seekVar);
    PropVariantClear(&seekVar);

    // Skip empty/metadata-only reads until we actually get a video sample.
    IMFSample* sample = nullptr;
    for (int attempt = 0; attempt < 10 && !sample; ++attempt)
    {
        DWORD streamIndex = 0, flags = 0;
        LONGLONG timestamp = 0;
        hr = reader->ReadSample(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), 0,
                                 &streamIndex, &flags, &timestamp, &sample);
        if (FAILED(hr) || (flags & MF_SOURCE_READERF_ENDOFSTREAM))
            break;
    }

    if (!sample)
    {
        reader->Release();
        return nullptr;
    }

    UINT32 frameWidth = 0, frameHeight = 0;
    IMFMediaType* currentType = nullptr;
    if (SUCCEEDED(reader->GetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), &currentType)) && currentType)
    {
        MFGetAttributeSize(currentType, MF_MT_FRAME_SIZE, &frameWidth, &frameHeight);
        currentType->Release();
    }

    HBITMAP result = nullptr;

    if (frameWidth > 0 && frameHeight > 0)
    {
        IMFMediaBuffer* buffer = nullptr;
        if (SUCCEEDED(sample->ConvertToContiguousBuffer(&buffer)) && buffer)
        {
            // Prefer IMF2DBuffer for the real (possibly hardware-aligned)
            // row pitch; fall back to a linear lock assuming tightly-packed
            // RGB32 rows if the buffer doesn't support it.
            BYTE* scanline0 = nullptr;
            LONG pitch = 0;
            bool locked2D = false;

            IMF2DBuffer* buffer2D = nullptr;
            if (SUCCEEDED(buffer->QueryInterface(__uuidof(IMF2DBuffer), reinterpret_cast<void**>(&buffer2D))) && buffer2D)
            {
                if (SUCCEEDED(buffer2D->Lock2D(&scanline0, &pitch)))
                    locked2D = true;
            }

            BYTE* linearBits = nullptr;
            DWORD maxLen = 0, curLen = 0;
            if (!locked2D)
            {
                if (SUCCEEDED(buffer->Lock(&linearBits, &maxLen, &curLen)))
                {
                    scanline0 = linearBits;
                    pitch = static_cast<LONG>(frameWidth) * 4;
                }
            }

            if (scanline0)
            {
                bool bottomUp = pitch < 0;
                LONG absPitch = bottomUp ? -pitch : pitch;

                double scale = std::min(static_cast<double>(thumbWidth) / frameWidth,
                                         static_cast<double>(thumbHeight) / frameHeight);
                int destW = std::max(1, static_cast<int>(frameWidth * scale));
                int destH = std::max(1, static_cast<int>(frameHeight * scale));
                int offsetX = (thumbWidth - destW) / 2;
                int offsetY = (thumbHeight - destH) / 2;

                void* dibBits = nullptr;
                HBITMAP dib = CreateTopDownDib32(thumbWidth, thumbHeight, &dibBits);
                if (dib && dibBits)
                {
                    auto* dst = static_cast<UINT32*>(dibBits);
                    for (int i = 0; i < thumbWidth * thumbHeight; ++i)
                        dst[i] = 0xFF2B2B2E; // opaque dark letterbox background

                    for (int y = 0; y < destH; ++y)
                    {
                        UINT32 srcY = std::min<UINT32>(frameHeight - 1, static_cast<UINT32>(y / scale));
                        const BYTE* srcRow = bottomUp
                            ? scanline0 + static_cast<size_t>(frameHeight - 1 - srcY) * absPitch
                            : scanline0 + static_cast<size_t>(srcY) * absPitch;
                        UINT32* dstRow = dst + static_cast<size_t>(offsetY + y) * thumbWidth + offsetX;

                        for (int x = 0; x < destW; ++x)
                        {
                            UINT32 srcX = std::min<UINT32>(frameWidth - 1, static_cast<UINT32>(x / scale));
                            const BYTE* px = srcRow + static_cast<size_t>(srcX) * 4; // MFVideoFormat_RGB32 = B,G,R,X per pixel
                            dstRow[x] = 0xFF000000u | (static_cast<UINT32>(px[2]) << 16) | (static_cast<UINT32>(px[1]) << 8) | px[0];
                        }
                    }
                    result = dib;
                }
                else if (dib)
                {
                    DeleteObject(dib);
                }
            }

            if (locked2D) buffer2D->Unlock2D();
            else if (linearBits) buffer->Unlock();
            if (buffer2D) buffer2D->Release();
            buffer->Release();
        }
    }

    sample->Release();
    reader->Release();
    return result;
}
