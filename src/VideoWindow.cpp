#include "VideoWindow.h"
#include <cassert>

#pragma comment(lib, "mfplay.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "mf.lib")

namespace
{
    const wchar_t* kWindowClassName = L"ScreenRelayVideoOutputWindowClass";
    constexpr UINT_PTR kLoopTimerId = 1;
}

VideoWindow::VideoWindow() = default;

VideoWindow::~VideoWindow()
{
    Close();
}

ATOM VideoWindow::RegisterWindowClassOnce()
{
    static ATOM atom = 0;
    if (atom != 0)
        return atom;

    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = VideoWindow::WndProc;
    wc.hInstance     = GetModuleHandleW(nullptr);
    wc.hCursor       = nullptr; // cursor is hidden explicitly in WM_SETCURSOR, scoped to this window only
    wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    wc.lpszClassName = kWindowClassName;

    atom = RegisterClassExW(&wc);
    return atom;
}

bool VideoWindow::Create(const RECT& monitorRect, const std::wstring& windowTitle)
{
    RegisterWindowClassOnce();

    // WS_POPUP with no caption/border, sized exactly to the monitor and
    // positioned at its virtual-desktop origin = a true full-screen,
    // border-less output window (the same visual result as vMix's
    // "Fullscreen Output").
    m_hwnd = CreateWindowExW(
        WS_EX_TOPMOST,
        kWindowClassName,
        windowTitle.c_str(),
        WS_POPUP | WS_VISIBLE,
        monitorRect.left, monitorRect.top,
        monitorRect.right - monitorRect.left,
        monitorRect.bottom - monitorRect.top,
        nullptr, nullptr, GetModuleHandleW(nullptr), this);

    if (!m_hwnd)
        return false;

    ShowWindow(m_hwnd, SW_SHOW);
    SetWindowPos(m_hwnd, HWND_TOPMOST, monitorRect.left, monitorRect.top,
                 monitorRect.right - monitorRect.left,
                 monitorRect.bottom - monitorRect.top,
                 SWP_SHOWWINDOW);
    // NOTE: cursor hiding is done per-window via WM_SETCURSOR below, not
    // via the global ShowCursor() counter - that API hides the pointer for
    // the whole application (including the Settings window) and has no
    // matching restore anywhere, which is why the pointer used to vanish
    // everywhere as soon as any output started.

    // Create the MFPlay player bound to this window. MFP_CREATION_OPTIONS
    // are 0 for default behavior. The player calls back into us for events
    // (media item created, playback ended, errors, etc.).
    HRESULT hr = MFPCreateMediaPlayer(
        nullptr,        // no URL yet - we open explicitly in PlayFile
        FALSE,          // start playback automatically -> we control it ourselves
        static_cast<MFP_CREATION_OPTIONS>(0),
        this,           // callback
        m_hwnd,         // video render target window
        &m_pPlayer);

    if (FAILED(hr) || !m_pPlayer)
    {
        SetStatus(PlaybackStatus::Error);
        m_lastError = L"Could not initialize the Media Foundation player.";
        return false;
    }

    return true;
}

void VideoWindow::SetMuted(bool muted)
{
    if (m_pPlayer)
        m_pPlayer->SetMute(muted ? TRUE : FALSE);
}

bool VideoWindow::PlayFile(const std::wstring& filePath, bool loop)
{
    if (!m_pPlayer)
        return false;

    StopLoopTimer();
    m_hasDuration = false;
    m_loopSeekQueued = false;
    m_loop = loop;
    m_pendingFile = filePath;

    SetStatus(PlaybackStatus::Opening);

    // Asynchronous: result arrives in OnMediaPlayerEvent as
    // MFP_EVENT_TYPE_MEDIAITEM_CREATED, where we call SetMediaItem + Play.
    HRESULT hr = m_pPlayer->CreateMediaItemFromURL(filePath.c_str(), FALSE, 0, nullptr);
    if (FAILED(hr))
    {
        SetStatus(PlaybackStatus::Error);
        m_lastError = L"Could not open the video file.";
    }
    return SUCCEEDED(hr);
}

void VideoWindow::Close()
{
    StopLoopTimer();
    if (m_pPlayer)
    {
        m_pPlayer->Shutdown();
        m_pPlayer->Release();
        m_pPlayer = nullptr;
    }
    if (m_hwnd)
    {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
}

void VideoWindow::SetStatus(PlaybackStatus status)
{
    if (m_status == status)
        return;
    m_status = status;
    if (m_onStatusChanged)
        m_onStatusChanged(status);
}

void VideoWindow::StartLoopTimer()
{
    if (m_loopTimerId != 0 || !m_hwnd)
        return;
    m_loopTimerId = SetTimer(m_hwnd, kLoopTimerId, kLoopTimerMs, nullptr);
}

void VideoWindow::StopLoopTimer()
{
    if (m_loopTimerId != 0 && m_hwnd)
    {
        KillTimer(m_hwnd, kLoopTimerId);
        m_loopTimerId = 0;
    }
}

void VideoWindow::OnLoopTimerTick()
{
    if (!m_pPlayer || !m_loop || !m_hasDuration || m_loopSeekQueued)
        return;

    PROPVARIANT pos;
    PropVariantInit(&pos);
    HRESULT hr = m_pPlayer->GetPosition(MFP_POSITIONTYPE_100NS, &pos);
    if (SUCCEEDED(hr) && pos.vt == VT_I8)
    {
        LONGLONG remaining = m_durationHns - pos.hVal.QuadPart;
        if (remaining <= kLoopLeadHns)
        {
            m_loopSeekQueued = true;

            PROPVARIANT zero;
            PropVariantInit(&zero);
            zero.vt = VT_I8;
            zero.hVal.QuadPart = 0;
            m_pPlayer->SetPosition(MFP_POSITIONTYPE_100NS, &zero);
            PropVariantClear(&zero);
        }
    }
    PropVariantClear(&pos);
}

// ---- IUnknown ----

STDMETHODIMP VideoWindow::QueryInterface(REFIID riid, void** ppv)
{
    if (!ppv) return E_POINTER;
    if (riid == IID_IUnknown || riid == __uuidof(IMFPMediaPlayerCallback))
    {
        *ppv = static_cast<IMFPMediaPlayerCallback*>(this);
        AddRef();
        return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) VideoWindow::AddRef()
{
    return InterlockedIncrement(&m_refCount);
}

STDMETHODIMP_(ULONG) VideoWindow::Release()
{
    // Lifetime of VideoWindow is owned by main.cpp (stack/vector), not by
    // COM ref-counting; we implement AddRef/Release only to satisfy the
    // IMFPMediaPlayerCallback contract, and never self-delete here.
    LONG count = InterlockedDecrement(&m_refCount);
    return static_cast<ULONG>(count);
}

// ---- IMFPMediaPlayerCallback ----

void STDMETHODCALLTYPE VideoWindow::OnMediaPlayerEvent(MFP_EVENT_HEADER* pEventHeader)
{
    if (!pEventHeader)
        return;

    if (FAILED(pEventHeader->hrEvent) &&
        pEventHeader->eEventType != MFP_EVENT_TYPE_MEDIAITEM_CREATED)
    {
        // MEDIAITEM_CREATED failures are handled below with a clearer
        // message; anything else failing mid-playback is a hard error.
        SetStatus(PlaybackStatus::Error);
        m_lastError = L"Playback error (hr=0x" + std::to_wstring(static_cast<unsigned long>(pEventHeader->hrEvent)) + L").";
    }

    switch (pEventHeader->eEventType)
    {
        case MFP_EVENT_TYPE_MEDIAITEM_CREATED:
        {
            auto* pEvent = MFP_GET_MEDIAITEM_CREATED_EVENT(pEventHeader);
            if (SUCCEEDED(pEventHeader->hrEvent) && pEvent->pMediaItem && m_pPlayer)
            {
                m_pPlayer->SetMediaItem(pEvent->pMediaItem);
            }
            else
            {
                SetStatus(PlaybackStatus::Error);
                m_lastError = L"Unsupported or unreadable video file.";
            }
            break;
        }

        case MFP_EVENT_TYPE_MEDIAITEM_SET:
        {
            if (SUCCEEDED(pEventHeader->hrEvent) && m_pPlayer)
            {
                // Cache duration so the loop timer can compute "time
                // remaining" without querying it every tick.
                PROPVARIANT duration;
                PropVariantInit(&duration);
                if (SUCCEEDED(m_pPlayer->GetDuration(MFP_POSITIONTYPE_100NS, &duration)) && duration.vt == VT_I8)
                {
                    m_durationHns = duration.hVal.QuadPart;
                    m_hasDuration = m_durationHns > 0;
                }
                PropVariantClear(&duration);

                m_loopSeekQueued = false;
                m_pPlayer->Play();
            }
            break;
        }

        case MFP_EVENT_TYPE_PLAY:
        {
            if (SUCCEEDED(pEventHeader->hrEvent))
            {
                SetStatus(PlaybackStatus::Playing);
                if (m_loop && m_hasDuration)
                    StartLoopTimer();
            }
            break;
        }

        case MFP_EVENT_TYPE_POSITION_SET:
        {
            // Our preemptive loop-seek (or any other seek) landed; clear the
            // guard so the next lap can be caught too, and keep playing.
            if (SUCCEEDED(pEventHeader->hrEvent))
            {
                m_loopSeekQueued = false;
                if (m_pPlayer)
                    m_pPlayer->Play();
            }
            break;
        }

        case MFP_EVENT_TYPE_PLAYBACK_ENDED:
        {
            // Fallback path: only reached if the preemptive seek above
            // didn't fire in time (e.g. a very short clip, or the poll
            // timer got starved). Still loops correctly, just with the
            // classic brief black-frame restart.
            if (m_loop && m_pPlayer)
            {
                PROPVARIANT var;
                PropVariantInit(&var);
                var.vt = VT_I8;
                var.hVal.QuadPart = 0;
                m_pPlayer->SetPosition(MFP_POSITIONTYPE_100NS, &var);
                PropVariantClear(&var);
                m_pPlayer->Play();
            }
            else
            {
                SetStatus(PlaybackStatus::Idle);
                StopLoopTimer();
            }
            break;
        }

        default:
            break;
    }
}

// ---- Win32 window procedure ----

LRESULT CALLBACK VideoWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    VideoWindow* self = nullptr;

    if (msg == WM_NCCREATE)
    {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = reinterpret_cast<VideoWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    else
    {
        self = reinterpret_cast<VideoWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (self)
        return self->HandleMessage(hwnd, msg, wParam, lParam);

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT VideoWindow::HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
        case WM_SIZE:
            // Let MFPlay know the render surface size changed so it can
            // rescale the video frame to fill the window.
            if (m_pPlayer)
            {
                RECT rc;
                GetClientRect(hwnd, &rc);
                m_pPlayer->UpdateVideo();
            }
            return 0;

        case WM_ERASEBKGND:
            return 1; // avoid white flash; MFPlay paints the video frame

        case WM_SETCURSOR:
            // Hide the pointer only while it's over this specific output
            // window's client area - leaves it alone everywhere else
            // (including the Settings window), unlike the old global
            // ShowCursor(FALSE) approach.
            if (LOWORD(lParam) == HTCLIENT)
            {
                SetCursor(nullptr);
                return TRUE;
            }
            return DefWindowProcW(hwnd, msg, wParam, lParam);

        case WM_TIMER:
            if (wParam == kLoopTimerId)
            {
                OnLoopTimerTick();
                return 0;
            }
            return DefWindowProcW(hwnd, msg, wParam, lParam);

        // Esc closes just this output window (handy while testing).
        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE)
                PostMessageW(hwnd, WM_CLOSE, 0, 0);
            return 0;

        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            return 0;

        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}
