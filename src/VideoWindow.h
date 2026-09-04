#pragma once
//
// VideoWindow.h
//
// A borderless, always-on-top window that is sized and positioned to
// exactly cover one physical monitor, and plays a local video file into it
// full-screen and looped - the same end result as vMix's "Fullscreen
// Output" to a monitor, but driven by this app instead.
//
// Video decode/render is done with Windows Media Foundation's high-level
// "MFPlay" player (IMFPMediaPlayer). This ships with Windows - no third
// party codec pack or video library is required for common formats
// (H.264/MP4, etc. - whatever codecs are registered on the system).
//
// Gapless-ish looping: MFPlay's own end-of-clip -> seek(0) -> Play() path
// (still used here as a fallback) involves a real stop/restart and shows a
// brief black frame. Instead, a short poll timer watches playback position
// and issues the seek-to-0 *before* the clip's natural end
// (kLoopLeadHns ahead), so in the common case MFPlay never actually reaches
// PLAYBACK_ENDED and the loop point is nearly seamless. This is a practical
// mitigation, not a hardware-guaranteed zero-gap loop: true frame-accurate
// gapless playback would need a custom Media Session topology with a
// pre-rolled duplicate source instead of MFPlay's high-level API.
//
#include <windows.h>
#include <mfapi.h>
#include <mfplay.h>
#include <mfobjects.h>
#include <string>
#include <functional>

enum class PlaybackStatus
{
    Idle,     // nothing loaded / not started
    Opening,  // media item requested, not yet playing
    Playing,
    Error
};

class VideoWindow : public IMFPMediaPlayerCallback
{
public:
    VideoWindow();
    ~VideoWindow();

    // Creates the borderless output window covering monitorRect (virtual
    // desktop coordinates) and initializes the media player bound to it.
    bool Create(const RECT& monitorRect, const std::wstring& windowTitle);

    // Starts (or replaces) playback of filePath. Playback loops
    // automatically when it reaches the end if loop == true.
    bool PlayFile(const std::wstring& filePath, bool loop);

    void Close();

    HWND GetHwnd() const { return m_hwnd; }
    PlaybackStatus GetStatus() const { return m_status; }
    std::wstring GetLastError() const { return m_lastError; }

    // Mutes/unmutes this output's audio. Safe to call any time after
    // Create() succeeds, including before a media item is loaded.
    void SetMuted(bool muted);

    // Optional: notified (on the UI thread) whenever GetStatus() would
    // change. Used by SettingsWindow to show live per-monitor state.
    void SetStatusChangedCallback(std::function<void(PlaybackStatus)> cb) { m_onStatusChanged = std::move(cb); }

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    // IMFPMediaPlayerCallback
    void STDMETHODCALLTYPE OnMediaPlayerEvent(MFP_EVENT_HEADER* pEventHeader) override;

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    static ATOM RegisterWindowClassOnce();

    void SetStatus(PlaybackStatus status);
    void StartLoopTimer();
    void StopLoopTimer();
    void OnLoopTimerTick();

    HWND             m_hwnd = nullptr;
    IMFPMediaPlayer*  m_pPlayer = nullptr;
    volatile LONG    m_refCount = 1;
    bool             m_loop = true;
    std::wstring     m_pendingFile;

    PlaybackStatus   m_status = PlaybackStatus::Idle;
    std::wstring     m_lastError;
    std::function<void(PlaybackStatus)> m_onStatusChanged;

    // Preemptive loop-seek state.
    bool             m_hasDuration = false;
    LONGLONG         m_durationHns = 0;   // clip duration, 100ns units
    bool             m_loopSeekQueued = false;
    UINT_PTR         m_loopTimerId = 0;

    static constexpr LONGLONG kLoopLeadHns = 2'000'000; // seek 200ms before natural end
    static constexpr UINT     kLoopTimerMs = 15;         // poll interval
};
