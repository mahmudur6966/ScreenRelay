#include "SettingsWindow.h"
#include "ThumbnailGenerator.h"
#include "LicenseDialog.h"
#include <windowsx.h>
#include <commdlg.h>
#include <shellapi.h>
#include <commctrl.h>
#include <sstream>
#include <algorithm>

#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "advapi32.lib")

namespace
{
    const wchar_t* kClassName = L"ScreenRelaySettingsWindowClass";
    const wchar_t* kCardClassName = L"ScreenRelayCardClass";
    const UINT WM_APP_TRAYMSG = WM_APP + 1;

    constexpr int ID_BTN_START       = 900;
    constexpr int ID_BTN_STOP        = 901;
    constexpr int ID_BTN_SAVE        = 902;
    constexpr int ID_BTN_REFRESH     = 903;
    constexpr int ID_BTN_ADD_VIDEO   = 904;
    constexpr int ID_BTN_REMOVE_VIDEO = 905;
    constexpr int ID_LIST_LIBRARY    = 906;
    constexpr int ID_STATUS_BAR      = 907;

    constexpr int ID_TRAY_SHOW = 910;
    constexpr int ID_TRAY_STOP = 911;
    constexpr int ID_TRAY_EXIT = 912;

    constexpr int ID_MENU_LOAD    = 920;
    constexpr int ID_MENU_SAVE    = 921;
    constexpr int ID_MENU_EXIT    = 922;
    constexpr int ID_MENU_START   = 923;
    constexpr int ID_MENU_STOP    = 924;
    constexpr int ID_MENU_REFRESH = 925;
    constexpr int ID_MENU_ABOUT   = 926;
    constexpr int ID_MENU_STARTUP = 927;
    constexpr int ID_MENU_ENTER_LICENSE = 928;

    constexpr int ROW_ID_BASE = 1000;
    constexpr int ROW_ID_STRIDE = 11;
    // per-row offsets within ROW_ID_STRIDE:
    constexpr int OFF_LABEL = 0, OFF_ENABLE = 1, OFF_VIDEO = 2, OFF_ADD = 3,
                  OFF_WIDTH = 4, OFF_HEIGHT = 5, OFF_HZ = 6, OFF_ROTATION = 7,
                  OFF_LOOP = 8, OFF_MUTE = 9, OFF_STATUS = 10;

    // ---- Palette (light, modern, high-contrast) ----
    const COLORREF kColWindowBg   = RGB(240, 242, 245);
    const COLORREF kColCardBg     = RGB(255, 255, 255);
    const COLORREF kColCardBorder = RGB(222, 225, 230);
    const COLORREF kColCardTitle  = RGB(35, 38, 45);
    const COLORREF kColAccent     = RGB(0, 103, 199);
    const COLORREF kColAccentDark = RGB(0, 82, 160);
    const COLORREF kColAccentText = RGB(255, 255, 255);
    const COLORREF kColStatusIdle = RGB(120, 124, 132);
    const COLORREF kColStatusLive = RGB(18, 138, 66);
    const COLORREF kColStatusErr  = RGB(198, 40, 40);
    const COLORREF kColStatusOpen = RGB(184, 122, 0);

    // Base (96-DPI) layout constants. SettingsWindow::Scale() converts these
    // to the window's actual DPI at build time.
    constexpr int BASE_CLIENT_WIDTH = 970;

    constexpr int LIB_GROUP_Y = 10;
    constexpr int LIB_GROUP_H = 210;
    constexpr int LIB_LIST_X = 20;
    constexpr int LIB_LIST_Y = 34;
    constexpr int LIB_LIST_W = 660;
    constexpr int LIB_LIST_H = 176;
    constexpr int LIB_BTN_X = 700;

    // Owner-drawn library list row: [thumbnail] [filename], fixed height.
    constexpr int LIB_ITEM_PAD = 6;
    constexpr int LIB_THUMB_W = 64;
    constexpr int LIB_THUMB_H = 36; // 16:9
    constexpr int LIB_ITEM_HEIGHT = LIB_THUMB_H + LIB_ITEM_PAD * 2; // 48
    constexpr int LIB_BTN_W = 250;

    constexpr int CARD_X = 10;
    constexpr int CARD_W = BASE_CLIENT_WIDTH - 20;
    constexpr int CARD_H = 54;
    constexpr int CARD_GAP = 10;
    constexpr int CARD_INNER_Y = 24;

    constexpr int COL_CHECK    = 20;
    constexpr int COL_LABEL    = 46;
    constexpr int COL_VIDEO    = 220;
    constexpr int COL_ADD      = 465;
    constexpr int COL_WIDTH    = 535;
    constexpr int COL_HEIGHT   = 590;
    constexpr int COL_HZ       = 645;
    constexpr int COL_ROTATION = 695;
    constexpr int COL_LOOP     = 785;
    constexpr int COL_MUTE     = 828;
    constexpr int COL_STATUS   = 875;

    const int kRotationValues[] = { -1, 0, 90, 180, 270 };
    const wchar_t* kRotationLabels[] = { L"No change", L"0", L"90", L"180", L"270" };

    const wchar_t* kRunKeyPath = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    const wchar_t* kRunValueName = L"ScreenRelay";

    int GetEditInt(HWND hwnd)
    {
        wchar_t buf[32] = {};
        GetWindowTextW(hwnd, buf, 32);
        if (buf[0] == L'\0')
            return 0;
        return _wtoi(buf);
    }

    void SetEditInt(HWND hwnd, int value)
    {
        SetWindowTextW(hwnd, std::to_wstring(value).c_str());
    }

    bool IsChecked(HWND hwnd)
    {
        return SendMessageW(hwnd, BM_GETCHECK, 0, 0) == BST_CHECKED;
    }

    void SetChecked(HWND hwnd, bool checked)
    {
        SendMessageW(hwnd, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);
    }

    BOOL CALLBACK DestroyChildProc(HWND hwnd, LPARAM)
    {
        DestroyWindow(hwnd);
        return TRUE;
    }

    BOOL CALLBACK SetFontProc(HWND hwnd, LPARAM lParam)
    {
        SendMessageW(hwnd, WM_SETFONT, static_cast<WPARAM>(lParam), TRUE);
        return TRUE;
    }

    // A fixed 8px corner radius reads fine across the DPI range this app
    // targets without needing per-instance scaling; kept as a tiny function
    // so CardWndProc doesn't need direct access to SettingsWindow::Scale().
    int Scale_CardRadius() { return 8; }

    // ---- "Card" panel: a plain custom-painted rounded panel used as a
    // visual container behind each group of controls, instead of the
    // dated-looking native BS_GROUPBOX frame. All content it draws (fill,
    // border, bold title) reads from window properties set at creation, so
    // this stays a free function - no SettingsWindow state needed here.
    LRESULT CALLBACK CardWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        switch (msg)
        {
            case WM_NCHITTEST:
            {
                // Make the card click-through, the same way the native
                // BS_GROUPBOX frame it replaced always was. Without this,
                // the card is a fully opaque window and - being created
                // before the controls that visually sit on it - can end up
                // above an earlier sibling in z-order and silently eat
                // clicks meant for it. This is the single most important
                // difference from a real GROUPBOX and the most likely
                // cause of "some buttons don't respond" symptoms.
                LRESULT hit = DefWindowProcW(hwnd, msg, wParam, lParam);
                return (hit == HTCLIENT) ? HTTRANSPARENT : hit;
            }

            case WM_ERASEBKGND:
                return 1; // WM_PAINT paints the whole client area itself

            case WM_PAINT:
            {
                PAINTSTRUCT ps;
                HDC hdc = BeginPaint(hwnd, &ps);

                RECT rc;
                GetClientRect(hwnd, &rc);

                auto fill = reinterpret_cast<HBRUSH>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
                HBRUSH fillBrush = fill ? fill : reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH));
                HPEN borderPen = CreatePen(PS_SOLID, 1, kColCardBorder);

                HGDIOBJ oldBrush = SelectObject(hdc, fillBrush);
                HGDIOBJ oldPen = SelectObject(hdc, borderPen);
                int radius = Scale_CardRadius();
                RoundRect(hdc, rc.left, rc.top, rc.right - 1, rc.bottom - 1, radius, radius);
                SelectObject(hdc, oldBrush);
                SelectObject(hdc, oldPen);
                DeleteObject(borderPen);

                wchar_t title[256] = {};
                GetWindowTextW(hwnd, title, 256);
                if (title[0] != L'\0')
                {
                    HFONT font = reinterpret_cast<HFONT>(SendMessageW(hwnd, WM_GETFONT, 0, 0));
                    HGDIOBJ oldFont = font ? SelectObject(hdc, font) : nullptr;
                    SetBkMode(hdc, TRANSPARENT);
                    SetTextColor(hdc, kColCardTitle);
                    RECT textRc{ rc.left + 14, rc.top + 6, rc.right - 10, rc.top + 26 };
                    DrawTextW(hdc, title, -1, &textRc, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_NOPREFIX);
                    if (oldFont) SelectObject(hdc, oldFont);
                }

                EndPaint(hwnd, &ps);
                return 0;
            }
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    ATOM RegisterCardClassOnce(HINSTANCE hInstance)
    {
        static ATOM atom = 0;
        if (atom != 0)
            return atom;

        WNDCLASSEXW wc{};
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = CardWndProc;
        wc.hInstance     = hInstance;
        wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr; // painted entirely in WM_PAINT
        wc.lpszClassName = kCardClassName;
        atom = RegisterClassExW(&wc);
        return atom;
    }

    bool IsStartupRegistered()
    {
        HKEY key;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKeyPath, 0, KEY_READ, &key) != ERROR_SUCCESS)
            return false;
        LONG result = RegQueryValueExW(key, kRunValueName, nullptr, nullptr, nullptr, nullptr);
        RegCloseKey(key);
        return result == ERROR_SUCCESS;
    }

    void SetStartupRegistered(bool enabled)
    {
        HKEY key;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKeyPath, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS)
            return;

        if (enabled)
        {
            wchar_t exePath[MAX_PATH] = {};
            GetModuleFileNameW(nullptr, exePath, MAX_PATH);
            std::wstring command = L"\"" + std::wstring(exePath) + L"\" /autostart";
            RegSetValueExW(key, kRunValueName, 0, REG_SZ,
                reinterpret_cast<const BYTE*>(command.c_str()),
                static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
        }
        else
        {
            RegDeleteValueW(key, kRunValueName);
        }
        RegCloseKey(key);
    }
}

SettingsWindow::SettingsWindow(OutputManager& outputManager)
    : m_outputManager(outputManager)
{
}

SettingsWindow::~SettingsWindow()
{
    if (m_trayIconAdded && m_hwnd)
    {
        NOTIFYICONDATAW nid{};
        nid.cbSize = sizeof(nid);
        nid.hWnd = m_hwnd;
        nid.uID = 1;
        Shell_NotifyIconW(NIM_DELETE, &nid);
    }
    if (m_hwnd)
        DestroyWindow(m_hwnd);

    DestroyThemeResources();
    if (m_uiFont) DeleteObject(m_uiFont);
    if (m_uiFontBold) DeleteObject(m_uiFontBold);
}

int SettingsWindow::Scale(int px) const
{
    return MulDiv(px, static_cast<int>(m_dpi), 96);
}

void SettingsWindow::CreateThemeResources()
{
    DestroyThemeResources();
    m_windowBrush = CreateSolidBrush(kColWindowBg);
    m_cardBrush = CreateSolidBrush(kColCardBg);
    m_cardBorderPen = CreatePen(PS_SOLID, 1, kColCardBorder);
    m_accentBrush = CreateSolidBrush(kColAccent);
    m_accentBrushPressed = CreateSolidBrush(kColAccentDark);
}

void SettingsWindow::DestroyThemeResources()
{
    if (m_windowBrush) { DeleteObject(m_windowBrush); m_windowBrush = nullptr; }
    if (m_cardBrush) { DeleteObject(m_cardBrush); m_cardBrush = nullptr; }
    if (m_cardBorderPen) { DeleteObject(m_cardBorderPen); m_cardBorderPen = nullptr; }
    if (m_accentBrush) { DeleteObject(m_accentBrush); m_accentBrush = nullptr; }
    if (m_accentBrushPressed) { DeleteObject(m_accentBrushPressed); m_accentBrushPressed = nullptr; }
}

bool SettingsWindow::Create(HINSTANCE hInstance)
{
    m_hInstance = hInstance;

    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_BAR_CLASSES | ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);

    CreateThemeResources();
    RegisterCardClassOnce(hInstance);

    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = SettingsWindow::WndProc;
    wc.hInstance     = hInstance;
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = m_windowBrush;
    wc.lpszClassName = kClassName;
    wc.hIcon         = LoadIconW(nullptr, IDI_APPLICATION);
    RegisterClassExW(&wc);

    // Fixed-size main window: the layout below is computed absolutely, so a
    // resizable frame would just let the user scramble it. WS_CAPTION |
    // WS_SYSMENU | WS_MINIMIZEBOX gives a normal-looking title bar (move,
    // minimize, close, system menu) without a resize border or maximize box.
    DWORD style = WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN;

    RECT wr{ 0, 0, BASE_CLIENT_WIDTH, 400 };
    AdjustWindowRectEx(&wr, style, TRUE, 0);

    m_hwnd = CreateWindowExW(
        0, kClassName, L"ScreenRelay - Settings",
        style,
        CW_USEDEFAULT, CW_USEDEFAULT, wr.right - wr.left, wr.bottom - wr.top,
        nullptr, nullptr, hInstance, this);

    if (!m_hwnd)
        return false;

    m_dpi = GetDpiForWindow(m_hwnd);

    LOGFONTW lf{};
    lf.lfHeight = -MulDiv(9, static_cast<int>(m_dpi), 72);
    lf.lfWeight = FW_NORMAL;
    lf.lfQuality = CLEARTYPE_QUALITY;
    wcscpy_s(lf.lfFaceName, L"Segoe UI");
    m_uiFont = CreateFontIndirectW(&lf);

    LOGFONTW lfBold = lf;
    lfBold.lfWeight = FW_SEMIBOLD;
    m_uiFontBold = CreateFontIndirectW(&lfBold);

    // Menu bar: quick access to the same actions as the bottom buttons,
    // plus loading a different config file, launch-at-sign-in, and About.
    HMENU hFileMenu = CreatePopupMenu();
    AppendMenuW(hFileMenu, MF_STRING, ID_MENU_LOAD, L"&Load Config...");
    AppendMenuW(hFileMenu, MF_STRING, ID_MENU_SAVE, L"&Save as Default\tCtrl+S");
    AppendMenuW(hFileMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hFileMenu, MF_STRING | (IsStartupRegistered() ? MF_CHECKED : MF_UNCHECKED),
                ID_MENU_STARTUP, L"Start with &Windows");
    AppendMenuW(hFileMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hFileMenu, MF_STRING, ID_MENU_EXIT, L"E&xit");

    HMENU hOutputsMenu = CreatePopupMenu();
    AppendMenuW(hOutputsMenu, MF_STRING, ID_MENU_START, L"&Start / Apply\tF5");
    AppendMenuW(hOutputsMenu, MF_STRING, ID_MENU_STOP, L"S&top All");
    AppendMenuW(hOutputsMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hOutputsMenu, MF_STRING, ID_MENU_REFRESH, L"&Refresh Monitors");

    HMENU hHelpMenu = CreatePopupMenu();
    AppendMenuW(hHelpMenu, MF_STRING, ID_MENU_ENTER_LICENSE, L"&Enter License Key...");
    AppendMenuW(hHelpMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hHelpMenu, MF_STRING, ID_MENU_ABOUT, L"&About");

    HMENU hMenuBar = CreateMenu();
    AppendMenuW(hMenuBar, MF_POPUP, reinterpret_cast<UINT_PTR>(hFileMenu), L"&File");
    AppendMenuW(hMenuBar, MF_POPUP, reinterpret_cast<UINT_PTR>(hOutputsMenu), L"&Outputs");
    AppendMenuW(hMenuBar, MF_POPUP, reinterpret_cast<UINT_PTR>(hHelpMenu), L"&Help");
    SetMenu(m_hwnd, hMenuBar);

    m_statusBar = nullptr; // created inside BuildLayout(), which runs next

    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = m_hwnd;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_APP_TRAYMSG;
    nid.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wcscpy_s(nid.szTip, L"ScreenRelay");
    m_trayIconAdded = Shell_NotifyIconW(NIM_ADD, &nid) != FALSE;

    BuildLayout();

    // Poll each active output's playback status a few times a second so the
    // per-monitor status text/color in each card stays live.
    SetTimer(m_hwnd, kStatusPollTimerId, 500, nullptr);

    return true;
}

void SettingsWindow::Show()
{
    ShowWindow(m_hwnd, SW_SHOW);
    SetForegroundWindow(m_hwnd);
}

void SettingsWindow::Hide()
{
    ShowWindow(m_hwnd, SW_HIDE);
}

void SettingsWindow::LoadInitialConfig(const std::wstring& configPath, bool autoStart)
{
    m_configPath = configPath;
    m_loadedConfigs.clear();
    m_library.Clear();
    ConfigLoader::Load(configPath, m_loadedConfigs, m_library);
    BuildLayout();
    RegenerateAllThumbnails(); // thumbnails aren't persisted in config.ini - rebuild them for whatever loaded

    if (autoStart && !m_loadedConfigs.empty())
        OnStartApply();
}

void SettingsWindow::DestroyRows()
{
    EnumChildWindows(m_hwnd, DestroyChildProc, 0);
    m_rows.clear();
    m_listLibrary = nullptr;
    m_btnAddVideo = nullptr;
    m_btnRemoveVideo = nullptr;
    m_btnStart = nullptr;
    // Status bar is a child of m_hwnd too - recreate it at the end of BuildLayout.
    m_statusBar = nullptr;
}

void SettingsWindow::BuildLayout()
{
    DestroyRows();
    m_monitors = MonitorManager::EnumerateMonitors();

    auto makeStatic = [&](const wchar_t* text, int x, int y, int w, int h, int id = 0, DWORD extraStyle = 0)
    {
        HWND h2 = CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE | extraStyle,
            Scale(x), Scale(y), Scale(w), Scale(h), m_hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), m_hInstance, nullptr);
        if (m_uiFont) SendMessageW(h2, WM_SETFONT, reinterpret_cast<WPARAM>(m_uiFont), TRUE);
        return h2;
    };

    auto makeCard = [&](const wchar_t* title, int x, int y, int w, int h)
    {
        HWND card = CreateWindowExW(0, kCardClassName, title, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
            Scale(x), Scale(y), Scale(w), Scale(h), m_hwnd, nullptr, m_hInstance, nullptr);
        SetWindowLongPtrW(card, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(m_cardBrush));
        if (m_uiFontBold) SendMessageW(card, WM_SETFONT, reinterpret_cast<WPARAM>(m_uiFontBold), TRUE);
        return card;
    };

    // ---- Media Library panel ----
    makeCard(L"Media Library", CARD_X, LIB_GROUP_Y, CARD_W, LIB_GROUP_H);

    m_listLibrary = CreateWindowExW(0, L"LISTBOX", L"",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | LBS_OWNERDRAWFIXED | WS_BORDER,
        Scale(LIB_LIST_X), Scale(LIB_LIST_Y), Scale(LIB_LIST_W), Scale(LIB_LIST_H),
        m_hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_LIST_LIBRARY)), m_hInstance, nullptr);
    if (m_uiFont) SendMessageW(m_listLibrary, WM_SETFONT, reinterpret_cast<WPARAM>(m_uiFont), TRUE);

    m_btnAddVideo = CreateWindowExW(0, L"BUTTON", L"Add Video(s)...", WS_CHILD | WS_VISIBLE,
        Scale(LIB_BTN_X), Scale(LIB_LIST_Y), Scale(LIB_BTN_W), Scale(28),
        m_hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_BTN_ADD_VIDEO)), m_hInstance, nullptr);
    m_btnRemoveVideo = CreateWindowExW(0, L"BUTTON", L"Remove Selected", WS_CHILD | WS_VISIBLE,
        Scale(LIB_BTN_X), Scale(LIB_LIST_Y + 34), Scale(LIB_BTN_W), Scale(28),
        m_hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_BTN_REMOVE_VIDEO)), m_hInstance, nullptr);
    if (m_uiFont)
    {
        SendMessageW(m_btnAddVideo, WM_SETFONT, reinterpret_cast<WPARAM>(m_uiFont), TRUE);
        SendMessageW(m_btnRemoveVideo, WM_SETFONT, reinterpret_cast<WPARAM>(m_uiFont), TRUE);
    }
    RefreshLibraryListBox();

    // Column headers for the monitor cards
    int headerY = LIB_GROUP_Y + LIB_GROUP_H + 10;
    makeStatic(L"On", CARD_X + COL_CHECK, headerY, 30, 16);
    makeStatic(L"Monitor", CARD_X + COL_LABEL, headerY, 100, 16);
    makeStatic(L"Video source", CARD_X + COL_VIDEO, headerY, 100, 16);
    makeStatic(L"W", CARD_X + COL_WIDTH, headerY, 40, 16);
    makeStatic(L"H", CARD_X + COL_HEIGHT, headerY, 40, 16);
    makeStatic(L"Hz", CARD_X + COL_HZ, headerY, 40, 16);
    makeStatic(L"Rotation", CARD_X + COL_ROTATION, headerY, 80, 16);
    makeStatic(L"Loop", CARD_X + COL_LOOP, headerY, 38, 16);
    makeStatic(L"Mute", CARD_X + COL_MUTE, headerY, 42, 16);
    makeStatic(L"Status", CARD_X + COL_STATUS, headerY, 70, 16);

    int rowsTop = headerY + 20;

    // Prefer matching a monitor to its previous configuration by device
    // name (stable across hot-plug reordering); fall back to the numeric
    // monitor index (what gets persisted to config.ini).
    auto findMatch = [&](int monitorIndex, const std::wstring& deviceName) -> const OutputConfig*
    {
        for (const auto& pr : m_configsByDevice)
            if (pr.first == deviceName)
                return &pr.second;
        for (const auto& c : m_loadedConfigs)
            if (c.monitorIndex == monitorIndex)
                return &c;
        return nullptr;
    };

    m_rows.clear();
    for (size_t i = 0; i < m_monitors.size(); ++i)
    {
        const auto& mon = m_monitors[i];
        int cardY = rowsTop + static_cast<int>(i) * (CARD_H + CARD_GAP);
        int y = cardY + CARD_INNER_Y;
        int idBase = ROW_ID_BASE + static_cast<int>(i) * ROW_ID_STRIDE;

        const OutputConfig* match = findMatch(mon.index, mon.deviceName);

        RowControls row;
        row.monitorIndex = mon.index;

        std::wstringstream title;
        title << L"Monitor " << mon.index << (mon.isPrimary ? L"  (Primary)" : L"") << L"  -  " << mon.friendlyName;
        row.cardFrame = makeCard(title.str().c_str(), CARD_X, cardY, CARD_W, CARD_H);

        row.checkEnable = CreateWindowExW(0, L"BUTTON", L"", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            Scale(CARD_X + COL_CHECK), Scale(y), Scale(20), Scale(20), m_hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(idBase + OFF_ENABLE)), m_hInstance, nullptr);
        SetChecked(row.checkEnable, match && match->enabled);

        std::wstringstream info;
        info << (mon.rect.right - mon.rect.left) << L"x" << (mon.rect.bottom - mon.rect.top);
        row.labelInfo = makeStatic(info.str().c_str(), CARD_X + COL_LABEL, y + 2, 170, 32, idBase + OFF_LABEL);

        row.comboVideo = CreateWindowExW(0, L"COMBOBOX", L"",
            WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
            Scale(CARD_X + COL_VIDEO), Scale(y), Scale(240), Scale(200), m_hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(idBase + OFF_VIDEO)), m_hInstance, nullptr);
        if (m_uiFont) SendMessageW(row.comboVideo, WM_SETFONT, reinterpret_cast<WPARAM>(m_uiFont), TRUE);

        row.buttonAdd = CreateWindowExW(0, L"BUTTON", L"Add...", WS_CHILD | WS_VISIBLE,
            Scale(CARD_X + COL_ADD), Scale(y), Scale(60), Scale(22), m_hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(idBase + OFF_ADD)), m_hInstance, nullptr);
        if (m_uiFont) SendMessageW(row.buttonAdd, WM_SETFONT, reinterpret_cast<WPARAM>(m_uiFont), TRUE);

        row.editWidth = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_NUMBER,
            Scale(CARD_X + COL_WIDTH), Scale(y), Scale(48), Scale(22), m_hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(idBase + OFF_WIDTH)), m_hInstance, nullptr);
        SetEditInt(row.editWidth, match ? match->width : 0);
        if (m_uiFont) SendMessageW(row.editWidth, WM_SETFONT, reinterpret_cast<WPARAM>(m_uiFont), TRUE);

        row.editHeight = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_NUMBER,
            Scale(CARD_X + COL_HEIGHT), Scale(y), Scale(48), Scale(22), m_hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(idBase + OFF_HEIGHT)), m_hInstance, nullptr);
        SetEditInt(row.editHeight, match ? match->height : 0);
        if (m_uiFont) SendMessageW(row.editHeight, WM_SETFONT, reinterpret_cast<WPARAM>(m_uiFont), TRUE);

        row.editHz = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_NUMBER,
            Scale(CARD_X + COL_HZ), Scale(y), Scale(42), Scale(22), m_hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(idBase + OFF_HZ)), m_hInstance, nullptr);
        SetEditInt(row.editHz, match ? match->refreshHz : 0);
        if (m_uiFont) SendMessageW(row.editHz, WM_SETFONT, reinterpret_cast<WPARAM>(m_uiFont), TRUE);

        row.comboRotation = CreateWindowExW(0, L"COMBOBOX", L"",
            WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
            Scale(CARD_X + COL_ROTATION), Scale(y), Scale(85), Scale(100), m_hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(idBase + OFF_ROTATION)), m_hInstance, nullptr);
        if (m_uiFont) SendMessageW(row.comboRotation, WM_SETFONT, reinterpret_cast<WPARAM>(m_uiFont), TRUE);
        int selectIndex = 0;
        for (int r = 0; r < static_cast<int>(std::size(kRotationValues)); ++r)
        {
            SendMessageW(row.comboRotation, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(kRotationLabels[r]));
            if (match && match->rotationDeg == kRotationValues[r])
                selectIndex = r;
        }
        SendMessageW(row.comboRotation, CB_SETCURSEL, selectIndex, 0);

        row.checkLoop = CreateWindowExW(0, L"BUTTON", L"", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            Scale(CARD_X + COL_LOOP), Scale(y), Scale(20), Scale(20), m_hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(idBase + OFF_LOOP)), m_hInstance, nullptr);
        SetChecked(row.checkLoop, match ? match->loop : true);

        row.checkMute = CreateWindowExW(0, L"BUTTON", L"", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            Scale(CARD_X + COL_MUTE), Scale(y), Scale(20), Scale(20), m_hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(idBase + OFF_MUTE)), m_hInstance, nullptr);
        SetChecked(row.checkMute, match && match->mute);

        row.labelStatus = makeStatic(L"Idle", CARD_X + COL_STATUS, y + 2, 68, 20, idBase + OFF_STATUS);

        m_rows.push_back(row);

        // Fill and select the video combo now that the row exists.
        RefreshAllVideoCombos();
        SetRowVideoSelection(row, match ? match->libraryId : 0);
    }

    int buttonsY = rowsTop + static_cast<int>(m_monitors.size()) * (CARD_H + CARD_GAP) + 12;

    m_btnStart = CreateWindowExW(0, L"BUTTON", L"Start / Apply", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        Scale(CARD_X), Scale(buttonsY), Scale(140), Scale(34), m_hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_BTN_START)), m_hInstance, nullptr);
    HWND btnStop = CreateWindowExW(0, L"BUTTON", L"Stop All", WS_CHILD | WS_VISIBLE,
        Scale(CARD_X + 150), Scale(buttonsY), Scale(100), Scale(34), m_hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_BTN_STOP)), m_hInstance, nullptr);
    HWND btnSave = CreateWindowExW(0, L"BUTTON", L"Save as Default", WS_CHILD | WS_VISIBLE,
        Scale(CARD_X + 260), Scale(buttonsY), Scale(150), Scale(34), m_hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_BTN_SAVE)), m_hInstance, nullptr);
    HWND btnRefresh = CreateWindowExW(0, L"BUTTON", L"Refresh Monitors", WS_CHILD | WS_VISIBLE,
        Scale(CARD_X + 420), Scale(buttonsY), Scale(160), Scale(34), m_hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_BTN_REFRESH)), m_hInstance, nullptr);
    if (m_uiFont)
    {
        SendMessageW(btnStop, WM_SETFONT, reinterpret_cast<WPARAM>(m_uiFont), TRUE);
        SendMessageW(btnSave, WM_SETFONT, reinterpret_cast<WPARAM>(m_uiFont), TRUE);
        SendMessageW(btnRefresh, WM_SETFONT, reinterpret_cast<WPARAM>(m_uiFont), TRUE);
    }
    if (m_uiFontBold) SendMessageW(m_btnStart, WM_SETFONT, reinterpret_cast<WPARAM>(m_uiFontBold), TRUE);

    int clientBottom = buttonsY + 34 + 40; // button height + generous clearance so the status bar (added below) can't overlap and swallow clicks

    // Recreate the status bar last so it's on top of the z-order.
    m_statusBar = CreateWindowExW(0, STATUSCLASSNAME, nullptr,
        WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
        0, 0, 0, 0, m_hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_STATUS_BAR)), m_hInstance, nullptr);

    // Resize the window to fit exactly what we just laid out, then let the
    // status bar auto-position itself at the new bottom edge.
    RECT wr{ 0, 0, Scale(BASE_CLIENT_WIDTH), Scale(clientBottom) };
    DWORD style = static_cast<DWORD>(GetWindowLongPtrW(m_hwnd, GWL_STYLE));
    AdjustWindowRectEx(&wr, style, TRUE, 0);
    SetWindowPos(m_hwnd, nullptr, 0, 0, wr.right - wr.left, wr.bottom - wr.top,
                 SWP_NOMOVE | SWP_NOZORDER);
    SendMessageW(m_statusBar, WM_SIZE, 0, 0);

    ApplyFontToChildren(); // safety net for any control created above without an explicit WM_SETFONT
    if (m_uiFontBold) SendMessageW(m_btnStart, WM_SETFONT, reinterpret_cast<WPARAM>(m_uiFontBold), TRUE); // re-assert after the blanket pass above

    SetStatus(L"Ready. Add videos to the library, assign one per monitor, then Start / Apply.");
}

void SettingsWindow::DrawOwnerButton(LPDRAWITEMSTRUCT dis) const
{
    if (!dis || dis->CtlID != static_cast<UINT>(ID_BTN_START))
        return;

    bool pressed = (dis->itemState & ODS_SELECTED) != 0;
    HBRUSH fill = pressed ? m_accentBrushPressed : m_accentBrush;
    if (!fill) fill = reinterpret_cast<HBRUSH>(GetStockObject(GRAY_BRUSH));

    HDC hdc = dis->hDC;
    RECT rc = dis->rcItem;

    HPEN pen = CreatePen(PS_SOLID, 1, kColAccentDark);
    HGDIOBJ oldBrush = SelectObject(hdc, fill);
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, 6, 6);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(pen);

    wchar_t text[128] = {};
    GetWindowTextW(dis->hwndItem, text, 128);

    HFONT font = reinterpret_cast<HFONT>(SendMessageW(dis->hwndItem, WM_GETFONT, 0, 0));
    HGDIOBJ oldFont = font ? SelectObject(hdc, font) : nullptr;
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, kColAccentText);
    DrawTextW(hdc, text, -1, &rc, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
    if (oldFont) SelectObject(hdc, oldFont);

    if (dis->itemState & ODS_FOCUS)
    {
        RECT focusRc = rc;
        InflateRect(&focusRc, -4, -4);
        DrawFocusRect(hdc, &focusRc);
    }
}

void SettingsWindow::DrawLibraryListItem(LPDRAWITEMSTRUCT dis) const
{
    if (!dis)
        return;

    HDC hdc = dis->hDC;
    RECT rc = dis->rcItem;

    bool selected = (dis->itemState & ODS_SELECTED) != 0;
    HBRUSH bg = CreateSolidBrush(selected ? RGB(219, 234, 254) : kColCardBg);
    FillRect(hdc, &rc, bg);
    DeleteObject(bg);

    // itemID is -1 for the "draw an empty list" call some list box actions
    // trigger; nothing more to do in that case.
    if (static_cast<int>(dis->itemID) < 0)
        return;

    int libId = static_cast<int>(dis->itemData);
    const LibraryItem* item = m_library.Find(libId);
    if (!item)
        return;

    int pad = Scale(LIB_ITEM_PAD);
    int thumbW = Scale(LIB_THUMB_W);
    int thumbH = Scale(LIB_THUMB_H);
    RECT thumbRc{ rc.left + pad, rc.top + pad, rc.left + pad + thumbW, rc.top + pad + thumbH };
    // Clamp in case the row got taller/shorter than the thumbnail (shouldn't
    // happen since WM_MEASUREITEM sets a fixed height matching this, but
    // stay defensive against DPI-rounding edge cases).
    if (thumbRc.bottom > rc.bottom - pad) thumbRc.bottom = rc.bottom - pad;

    if (item->thumbnail)
    {
        HDC memDC = CreateCompatibleDC(hdc);
        HGDIOBJ oldBmp = SelectObject(memDC, item->thumbnail);
        BITMAP bm{};
        GetObjectW(item->thumbnail, sizeof(bm), &bm);
        StretchBlt(hdc, thumbRc.left, thumbRc.top, thumbRc.right - thumbRc.left, thumbRc.bottom - thumbRc.top,
                   memDC, 0, 0, bm.bmWidth, bm.bmHeight, SRCCOPY);
        SelectObject(memDC, oldBmp);
        DeleteDC(memDC);
    }
    else
    {
        // Placeholder while a thumbnail hasn't been generated yet (or
        // generation failed for this file) - a plain play-glyph tile.
        HBRUSH placeholder = CreateSolidBrush(RGB(60, 60, 66));
        FillRect(hdc, &thumbRc, placeholder);
        DeleteObject(placeholder);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(200, 200, 205));
        DrawTextW(hdc, L"\u25B6", -1, &thumbRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    RECT textRc{ thumbRc.right + pad, rc.top, rc.right - pad, rc.bottom };
    HFONT font = m_uiFont;
    HGDIOBJ oldFont = font ? SelectObject(hdc, font) : nullptr;
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, kColCardTitle);
    DrawTextW(hdc, item->displayName.c_str(), -1, &textRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    if (oldFont) SelectObject(hdc, oldFont);

    if (dis->itemState & ODS_FOCUS)
        DrawFocusRect(hdc, &rc);
}

void SettingsWindow::GenerateAndAttachThumbnail(int libraryId)
{
    const LibraryItem* item = m_library.Find(libraryId);
    if (!item || item->thumbnail)
        return; // already has one, or id doesn't exist

    HBITMAP bmp = GenerateVideoThumbnail(item->filePath, LIB_THUMB_W, LIB_THUMB_H);
    if (bmp)
        m_library.SetThumbnail(libraryId, bmp);

    if (m_listLibrary)
        InvalidateRect(m_listLibrary, nullptr, TRUE);
}

void SettingsWindow::RegenerateAllThumbnails()
{
    // Thumbnails aren't persisted in config.ini, so a freshly loaded
    // library starts with none - rebuild them all. Each file is decoded
    // synchronously, so this can take a moment for a large library; a
    // wait cursor keeps that from looking like the app hung.
    if (m_library.Empty())
        return;

    HCURSOR oldCursor = SetCursor(LoadCursorW(nullptr, IDC_WAIT));
    for (const auto& item : m_library.Items())
        GenerateAndAttachThumbnail(item.id);
    SetCursor(oldCursor);
}

void SettingsWindow::RefreshLibraryListBox()
{
    if (!m_listLibrary)
        return;
    SendMessageW(m_listLibrary, LB_RESETCONTENT, 0, 0);
    for (const auto& item : m_library.Items())
    {
        int idx = static_cast<int>(SendMessageW(m_listLibrary, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item.displayName.c_str())));
        if (idx >= 0)
            SendMessageW(m_listLibrary, LB_SETITEMDATA, idx, static_cast<LPARAM>(item.id));
    }
}

int SettingsWindow::SelectedLibraryIdForRow(const RowControls& row) const
{
    int sel = static_cast<int>(SendMessageW(row.comboVideo, CB_GETCURSEL, 0, 0));
    if (sel <= 0)
        return 0; // index 0 is always "(No video)"
    return static_cast<int>(SendMessageW(row.comboVideo, CB_GETITEMDATA, sel, 0));
}

void SettingsWindow::SetRowVideoSelection(const RowControls& row, int libraryId)
{
    int count = static_cast<int>(SendMessageW(row.comboVideo, CB_GETCOUNT, 0, 0));
    for (int i = 0; i < count; ++i)
    {
        if (libraryId != 0 && static_cast<int>(SendMessageW(row.comboVideo, CB_GETITEMDATA, i, 0)) == libraryId)
        {
            SendMessageW(row.comboVideo, CB_SETCURSEL, i, 0);
            return;
        }
    }
    SendMessageW(row.comboVideo, CB_SETCURSEL, 0, 0); // "(No video)"
}

void SettingsWindow::RefreshAllVideoCombos()
{
    for (auto& row : m_rows)
    {
        int previouslySelected = SelectedLibraryIdForRow(row);

        SendMessageW(row.comboVideo, CB_RESETCONTENT, 0, 0);
        int idx0 = static_cast<int>(SendMessageW(row.comboVideo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"(No video)")));
        SendMessageW(row.comboVideo, CB_SETITEMDATA, idx0, 0);

        for (const auto& item : m_library.Items())
        {
            int idx = static_cast<int>(SendMessageW(row.comboVideo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item.displayName.c_str())));
            SendMessageW(row.comboVideo, CB_SETITEMDATA, idx, static_cast<LPARAM>(item.id));
        }

        SetRowVideoSelection(row, previouslySelected);
    }
}

void SettingsWindow::SetStatus(const std::wstring& text)
{
    if (m_statusBar)
        SendMessageW(m_statusBar, SB_SETTEXTW, 0, reinterpret_cast<LPARAM>(text.c_str()));
}

const wchar_t* SettingsWindow::StatusText(PlaybackStatus status)
{
    switch (status)
    {
        case PlaybackStatus::Idle:    return L"Idle";
        case PlaybackStatus::Opening: return L"Opening...";
        case PlaybackStatus::Playing: return L"\u25CF Live";
        case PlaybackStatus::Error:   return L"\u26A0 Error";
    }
    return L"Idle";
}

std::vector<OutputConfig> SettingsWindow::CollectConfigsFromRows(bool onlyEnabled) const
{
    std::vector<OutputConfig> result;

    for (const auto& row : m_rows)
    {
        bool enabled = IsChecked(row.checkEnable);
        if (onlyEnabled && !enabled)
            continue;

        OutputConfig cfg;
        cfg.monitorIndex = row.monitorIndex;
        cfg.sectionName = L"Monitor" + std::to_wstring(row.monitorIndex);
        cfg.enabled = enabled;

        cfg.libraryId = SelectedLibraryIdForRow(row);
        if (const LibraryItem* item = m_library.Find(cfg.libraryId))
            cfg.filePath = item->filePath;

        cfg.width = GetEditInt(row.editWidth);
        cfg.height = GetEditInt(row.editHeight);
        cfg.refreshHz = GetEditInt(row.editHz);

        int sel = static_cast<int>(SendMessageW(row.comboRotation, CB_GETCURSEL, 0, 0));
        cfg.rotationDeg = (sel >= 0 && sel < static_cast<int>(std::size(kRotationValues))) ? kRotationValues[sel] : -1;

        cfg.loop = IsChecked(row.checkLoop);
        cfg.mute = IsChecked(row.checkMute);

        result.push_back(cfg);
    }
    return result;
}

void SettingsWindow::OnAddVideo()
{
    // Multi-select so several videos can be added to the library in one go.
    wchar_t fileBuf[8192] = {};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = m_hwnd;
    ofn.lpstrFilter = L"Video Files\0*.mp4;*.mov;*.avi;*.mkv;*.wmv;*.m4v;*.mpg;*.mpeg\0All Files\0*.*\0\0";
    ofn.lpstrFile = fileBuf;
    ofn.nMaxFile = static_cast<DWORD>(std::size(fileBuf));
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_ALLOWMULTISELECT | OFN_EXPLORER;
    ofn.lpstrTitle = L"Add video(s) to the library";

    if (!GetOpenFileNameW(&ofn))
        return;

    // With OFN_ALLOWMULTISELECT, fileBuf holds: "<folder>\0<file1>\0<file2>\0...\0\0"
    // when multiple files are chosen, or just "<fullpath>\0\0" for a single one.
    std::wstring folder = fileBuf;
    const wchar_t* p = fileBuf + folder.size() + 1;
    int added = 0;
    std::vector<int> newIds;

    if (*p == L'\0')
    {
        // Single selection: fileBuf is already the full path.
        newIds.push_back(m_library.Add(folder));
        added = 1;
    }
    else
    {
        while (*p != L'\0')
        {
            std::wstring name = p;
            std::wstring full = folder + L"\\" + name;
            newIds.push_back(m_library.Add(full));
            ++added;
            p += name.size() + 1;
        }
    }

    RefreshLibraryListBox();
    RefreshAllVideoCombos();
    SetStatus(L"Generating thumbnails for " + std::to_wstring(added) + L" video(s)...");
    if (m_statusBar) UpdateWindow(m_statusBar); // force the status text to paint before the blocking work below
    for (int id : newIds)
        GenerateAndAttachThumbnail(id);
    SetStatus(L"Added " + std::to_wstring(added) + L" video(s) to the library.");
}

void SettingsWindow::OnRemoveSelectedVideo()
{
    if (!m_listLibrary)
        return;
    int sel = static_cast<int>(SendMessageW(m_listLibrary, LB_GETCURSEL, 0, 0));
    if (sel == LB_ERR)
    {
        SetStatus(L"Select a video in the library list first.");
        return;
    }
    int id = static_cast<int>(SendMessageW(m_listLibrary, LB_GETITEMDATA, sel, 0));
    m_library.Remove(id);
    RefreshLibraryListBox();
    RefreshAllVideoCombos(); // any row that had this video assigned falls back to "(No video)"
    SetStatus(L"Removed from the library.");
}

void SettingsWindow::OnStartApply()
{
    std::vector<OutputConfig> configs = CollectConfigsFromRows(true);
    if (configs.empty())
    {
        SetStatus(L"Nothing to start: check at least one monitor and assign it a video.");
        return;
    }

    std::wstring warnings;
    bool started = m_outputManager.Apply(configs, m_monitors, warnings);

    std::wstringstream status;
    status << (started ? L"Outputs started/updated for " : L"No outputs started for ")
           << configs.size() << L" configured monitor(s).";
    if (!warnings.empty())
        status << L"  Warnings: " << warnings;
    SetStatus(status.str());
}

void SettingsWindow::OnStopAll()
{
    m_outputManager.StopAll();
    SetStatus(L"All outputs stopped.");
}

void SettingsWindow::OnSaveDefault()
{
    std::vector<OutputConfig> configs = CollectConfigsFromRows(true);
    if (ConfigLoader::Save(m_configPath, configs, m_library))
        SetStatus(L"Saved " + std::to_wstring(configs.size()) + L" output(s) to " + m_configPath +
                   L". They'll load automatically next time.");
    else
        SetStatus(L"Failed to save " + m_configPath + L" - check the app has write access to its folder.");
}

void SettingsWindow::OnLoadConfig()
{
    wchar_t fileBuf[MAX_PATH] = {};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = m_hwnd;
    ofn.lpstrFilter = L"Config Files\0*.ini\0All Files\0*.*\0\0";
    ofn.lpstrFile = fileBuf;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    ofn.lpstrTitle = L"Load a config file";

    if (!GetOpenFileNameW(&ofn))
        return;

    LoadInitialConfig(fileBuf, false);
    SetStatus(L"Loaded " + std::wstring(fileBuf));
}

void SettingsWindow::OnAbout()
{
    std::wstring licenseLine = m_license.IsLicensed()
        ? (L"Licensed to: " + m_license.LicensedName())
        : (m_license.IsTrialExpired()
            ? L"Trial expired - Help > Enter License Key to activate."
            : (L"Trial mode - " + std::to_wstring(m_license.TrialDaysRemaining()) + L" day(s) remaining."));

    MessageBoxW(m_hwnd,
        (L"ScreenRelay\r\n\r\n"
         L"Plays looped video to one or more physical monitors, independent of "
         L"the monitor running the app itself. Built on Win32 + Windows Media "
         L"Foundation (MFPlay) - no third-party runtime required.\r\n\r\n" + licenseLine).c_str(),
        L"About ScreenRelay", MB_OK | MB_ICONINFORMATION);
}

void SettingsWindow::UpdateTitleForLicenseState()
{
    std::wstring title = L"ScreenRelay - Settings";
    if (m_license.IsLicensed())
        title += L"  (Licensed to " + m_license.LicensedName() + L")";
    else if (m_license.IsTrialExpired())
        title += L"  (Trial expired)";
    else
        title += L"  (Trial - " + std::to_wstring(m_license.TrialDaysRemaining()) + L" day(s) left)";
    SetWindowTextW(m_hwnd, title.c_str());
}

bool SettingsWindow::CheckLicenseAtStartup(bool autoStart)
{
    if (m_license.IsLicensed())
    {
        UpdateTitleForLicenseState();
        return true;
    }

    bool expired = m_license.IsTrialExpired();

    // Don't interrupt a silent/unattended autostart boot with a reminder
    // if the trial is still active - nothing urgent to say. An expired
    // trial still gates even on autostart, since silently running past it
    // forever would defeat the point of having a trial at all.
    if (autoStart && !expired)
    {
        UpdateTitleForLicenseState();
        return true;
    }

    for (;;)
    {
        LicenseDialog::TrialChoice choice = LicenseDialog::ShowTrialPrompt(m_hwnd, m_license.TrialDaysRemaining(), expired, L"ScreenRelay");

        if (choice == LicenseDialog::TrialChoice::Exit)
            return false;

        if (choice == LicenseDialog::TrialChoice::ContinueTrial)
            break;

        // EnterLicense
        std::wstring name, key;
        if (LicenseDialog::ShowActivationForm(m_hwnd, m_hInstance, name, key, L"ScreenRelay"))
        {
            if (m_license.Activate(name, key))
                break;
            LicenseDialog::ShowInvalidKeyMessage(m_hwnd, L"ScreenRelay");
        }
        // Cancelled the form, or an invalid key: loop back to the prompt.
        expired = m_license.IsTrialExpired();
    }

    UpdateTitleForLicenseState();
    return true;
}

void SettingsWindow::OnEnterLicenseKey()
{
    if (m_license.IsLicensed())
    {
        MessageBoxW(m_hwnd,
            (L"Already licensed to: " + m_license.LicensedName()).c_str(),
            L"ScreenRelay", MB_OK | MB_ICONINFORMATION);
        return;
    }

    std::wstring name, key;
    if (LicenseDialog::ShowActivationForm(m_hwnd, m_hInstance, name, key, L"ScreenRelay"))
    {
        if (m_license.Activate(name, key))
        {
            UpdateTitleForLicenseState();
            SetStatus(L"Licensed to " + m_license.LicensedName() + L". Thank you!");
        }
        else
        {
            LicenseDialog::ShowInvalidKeyMessage(m_hwnd, L"ScreenRelay");
        }
    }
}

void SettingsWindow::OnToggleStartWithWindows()
{
    bool nowEnabled = !IsStartupRegistered();
    SetStartupRegistered(nowEnabled);

    HMENU fileMenu = GetSubMenu(GetMenu(m_hwnd), 0);
    if (fileMenu)
        CheckMenuItem(fileMenu, ID_MENU_STARTUP, nowEnabled ? MF_CHECKED : MF_UNCHECKED);

    SetStatus(nowEnabled
        ? L"ScreenRelay will now start automatically with Windows (silently, in the tray)."
        : L"Removed from Windows startup.");
}

void SettingsWindow::OnStatusPollTick()
{
    for (const auto& row : m_rows)
    {
        PlaybackStatus status = m_outputManager.GetStatus(row.monitorIndex);
        SetWindowTextW(row.labelStatus, StatusText(status));

        COLORREF color = kColStatusIdle;
        if (status == PlaybackStatus::Playing) color = kColStatusLive;
        else if (status == PlaybackStatus::Error) color = kColStatusErr;
        else if (status == PlaybackStatus::Opening) color = kColStatusOpen;

        // Stash the color on the control for WM_CTLCOLORSTATIC to pick up.
        SetWindowLongPtrW(row.labelStatus, GWLP_USERDATA, static_cast<LONG_PTR>(color));
        InvalidateRect(row.labelStatus, nullptr, TRUE);

        if (status == PlaybackStatus::Error)
        {
            std::wstring err = m_outputManager.GetLastError(row.monitorIndex);
            if (!err.empty())
            {
                std::wstringstream tip;
                tip << L"Monitor " << row.monitorIndex << L": " << err;
                SetStatus(tip.str());
            }
        }
    }
}

void SettingsWindow::ApplyFontToChildren()
{
    if (!m_uiFont || !m_hwnd)
        return;
    EnumChildWindows(m_hwnd, SetFontProc, reinterpret_cast<LPARAM>(m_uiFont));
}

void SettingsWindow::ShowTrayMenu()
{
    POINT pt;
    GetCursorPos(&pt);

    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, ID_TRAY_SHOW, L"Show Settings");
    AppendMenuW(menu, MF_STRING, ID_TRAY_STOP, L"Stop All Outputs");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ID_TRAY_EXIT, L"Exit");

    SetForegroundWindow(m_hwnd); // required so the menu closes properly on click-away
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, m_hwnd, nullptr);
    DestroyMenu(menu);
}

LRESULT CALLBACK SettingsWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    SettingsWindow* self = nullptr;

    if (msg == WM_NCCREATE)
    {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = reinterpret_cast<SettingsWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    else
    {
        self = reinterpret_cast<SettingsWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (self)
        return self->HandleMessage(hwnd, msg, wParam, lParam);

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT SettingsWindow::HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
        case WM_DRAWITEM:
        {
            auto* dis = reinterpret_cast<LPDRAWITEMSTRUCT>(lParam);
            if (dis && dis->CtlType == ODT_BUTTON)
            {
                DrawOwnerButton(dis);
                return TRUE;
            }
            if (dis && dis->CtlType == ODT_LISTBOX && dis->CtlID == static_cast<UINT>(ID_LIST_LIBRARY))
            {
                DrawLibraryListItem(dis);
                return TRUE;
            }
            return DefWindowProcW(hwnd, msg, wParam, lParam);
        }

        case WM_MEASUREITEM:
        {
            auto* mis = reinterpret_cast<LPMEASUREITEMSTRUCT>(lParam);
            if (mis && mis->CtlID == static_cast<UINT>(ID_LIST_LIBRARY))
            {
                mis->itemHeight = static_cast<UINT>(Scale(LIB_ITEM_HEIGHT));
                return TRUE;
            }
            return DefWindowProcW(hwnd, msg, wParam, lParam);
        }

        case WM_CTLCOLORSTATIC:
        {
            // Button-class checkboxes (BS_AUTOCHECKBOX) route through this
            // same message, not WM_CTLCOLORBTN - only true push buttons do.
            HWND ctrl = reinterpret_cast<HWND>(lParam);
            HDC hdc = reinterpret_cast<HDC>(wParam);

            bool onCard = false;
            for (const auto& row : m_rows)
            {
                if (ctrl == row.labelStatus)
                {
                    COLORREF color = static_cast<COLORREF>(GetWindowLongPtrW(ctrl, GWLP_USERDATA));
                    if (color == 0) color = kColStatusIdle;
                    SetTextColor(hdc, color);
                    SetBkMode(hdc, TRANSPARENT);
                    return reinterpret_cast<LRESULT>(m_cardBrush ? m_cardBrush : GetSysColorBrush(COLOR_WINDOW));
                }
                if (ctrl == row.checkEnable || ctrl == row.labelInfo || ctrl == row.checkLoop || ctrl == row.checkMute)
                {
                    onCard = true;
                    break;
                }
            }

            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, kColCardTitle);
            HBRUSH bg = onCard ? m_cardBrush : m_windowBrush;
            return reinterpret_cast<LRESULT>(bg ? bg : GetSysColorBrush(COLOR_BTNFACE));
        }

        case WM_COMMAND:
        {
            int id = LOWORD(wParam);
            int code = HIWORD(wParam);

            if (id == ID_BTN_START || id == ID_MENU_START) { OnStartApply(); return 0; }
            if (id == ID_BTN_STOP  || id == ID_MENU_STOP)  { OnStopAll(); return 0; }
            if (id == ID_BTN_SAVE  || id == ID_MENU_SAVE)  { OnSaveDefault(); return 0; }
            if (id == ID_BTN_REFRESH || id == ID_MENU_REFRESH) { OnRefreshMonitors(); return 0; }
            if (id == ID_BTN_ADD_VIDEO) { OnAddVideo(); return 0; }
            if (id == ID_BTN_REMOVE_VIDEO) { OnRemoveSelectedVideo(); return 0; }
            if (id == ID_MENU_LOAD) { OnLoadConfig(); return 0; }
            if (id == ID_MENU_ABOUT) { OnAbout(); return 0; }
            if (id == ID_MENU_STARTUP) { OnToggleStartWithWindows(); return 0; }
            if (id == ID_MENU_ENTER_LICENSE) { OnEnterLicenseKey(); return 0; }
            if (id == ID_MENU_EXIT)
            {
                m_outputManager.StopAll();
                DestroyWindow(hwnd);
                return 0;
            }
            if (id == ID_LIST_LIBRARY && code == LBN_DBLCLK)
            {
                // Double-clicking a library entry is a quick way to check
                // it's still on disk / remind yourself of the full path.
                int sel = static_cast<int>(SendMessageW(m_listLibrary, LB_GETCURSEL, 0, 0));
                if (sel != LB_ERR)
                {
                    int libId = static_cast<int>(SendMessageW(m_listLibrary, LB_GETITEMDATA, sel, 0));
                    if (const LibraryItem* item = m_library.Find(libId))
                        SetStatus(item->filePath);
                }
                return 0;
            }

            if (id == ID_TRAY_SHOW) { Show(); return 0; }
            if (id == ID_TRAY_STOP) { OnStopAll(); return 0; }
            if (id == ID_TRAY_EXIT)
            {
                m_outputManager.StopAll();
                DestroyWindow(hwnd);
                return 0;
            }

            if (id >= ROW_ID_BASE && code == BN_CLICKED)
            {
                int rowIndex = (id - ROW_ID_BASE) / ROW_ID_STRIDE;
                int offset   = (id - ROW_ID_BASE) % ROW_ID_STRIDE;
                if (offset == OFF_ADD && rowIndex >= 0 && rowIndex < static_cast<int>(m_rows.size()))
                {
                    // "Add..." on a row: add the file to the library AND
                    // assign it to this row in one step.
                    wchar_t fileBuf[MAX_PATH] = {};
                    OPENFILENAMEW ofn{};
                    ofn.lStructSize = sizeof(ofn);
                    ofn.hwndOwner = m_hwnd;
                    ofn.lpstrFilter = L"Video Files\0*.mp4;*.mov;*.avi;*.mkv;*.wmv;*.m4v;*.mpg;*.mpeg\0All Files\0*.*\0\0";
                    ofn.lpstrFile = fileBuf;
                    ofn.nMaxFile = MAX_PATH;
                    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
                    ofn.lpstrTitle = L"Select a video file";

                    if (GetOpenFileNameW(&ofn))
                    {
                        int libId = m_library.Add(fileBuf);
                        RefreshLibraryListBox();
                        RefreshAllVideoCombos();
                        SetRowVideoSelection(m_rows[rowIndex], libId);
                        SetChecked(m_rows[rowIndex].checkEnable, true);
                        GenerateAndAttachThumbnail(libId);
                    }
                    return 0;
                }
            }
            return 0;
        }

        case WM_TIMER:
            if (wParam == kStatusPollTimerId)
            {
                OnStatusPollTick();
                return 0;
            }
            if (wParam == kDisplayChangeTimerId)
            {
                KillTimer(hwnd, kDisplayChangeTimerId);
                OnRefreshMonitors();
                return 0;
            }
            return DefWindowProcW(hwnd, msg, wParam, lParam);

        case WM_DISPLAYCHANGE:
        {
            // Fired repeatedly during a hot-plug / resolution-change
            // transition - debounce so we only rebuild once things settle.
            SetTimer(hwnd, kDisplayChangeTimerId, 800, nullptr);
            return 0;
        }

        case WM_DPICHANGED:
        {
            m_dpi = HIWORD(wParam);
            auto* suggested = reinterpret_cast<RECT*>(lParam);
            SetWindowPos(hwnd, nullptr, suggested->left, suggested->top,
                         suggested->right - suggested->left, suggested->bottom - suggested->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);

            if (m_uiFont) DeleteObject(m_uiFont);
            if (m_uiFontBold) DeleteObject(m_uiFontBold);
            LOGFONTW lf{};
            lf.lfHeight = -MulDiv(9, static_cast<int>(m_dpi), 72);
            lf.lfWeight = FW_NORMAL;
            lf.lfQuality = CLEARTYPE_QUALITY;
            wcscpy_s(lf.lfFaceName, L"Segoe UI");
            m_uiFont = CreateFontIndirectW(&lf);
            LOGFONTW lfBold = lf;
            lfBold.lfWeight = FW_SEMIBOLD;
            m_uiFontBold = CreateFontIndirectW(&lfBold);

            BuildLayout();
            return 0;
        }

        case WM_SIZE:
            if (m_statusBar)
                SendMessageW(m_statusBar, WM_SIZE, wParam, lParam);
            return 0;

        case WM_APP_TRAYMSG:
        {
            UINT mouseMsg = static_cast<UINT>(lParam);
            if (mouseMsg == WM_RBUTTONUP || mouseMsg == WM_CONTEXTMENU)
                ShowTrayMenu();
            else if (mouseMsg == WM_LBUTTONDBLCLK)
                Show();
            return 0;
        }

        case WM_CLOSE:
            // Hide instead of destroy: outputs (and the tray icon) keep
            // running. Use the tray menu's "Exit" to actually quit.
            Hide();
            return 0;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;

        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

void SettingsWindow::OnRefreshMonitors()
{
    // Preserve whatever the user has currently typed/checked (including
    // disabled rows) before re-enumerating and rebuilding, keyed both by
    // device name (survives hot-plug reordering) and by index (fallback).
    std::vector<MonitorInfo> oldMonitors = m_monitors;
    std::vector<OutputConfig> configs = CollectConfigsFromRows(false);

    m_loadedConfigs = configs;
    m_configsByDevice.clear();
    for (const auto& cfg : configs)
        for (const auto& mon : oldMonitors)
            if (mon.index == cfg.monitorIndex)
            {
                m_configsByDevice.emplace_back(mon.deviceName, cfg);
                break;
            }

    BuildLayout();
    SetStatus(L"Monitor list refreshed.");
}
