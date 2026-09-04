#include "LicenseDialog.h"

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#include <commctrl.h>
#include <string>

#pragma comment(lib, "comctl32.lib")

namespace
{
    constexpr int ID_ACTIVATE = 1;
    constexpr int ID_CANCEL = 2;
    constexpr int ID_EDIT_NAME = 10;
    constexpr int ID_EDIT_KEY = 11;

    constexpr int TDB_ENTER_LICENSE = 100;
    constexpr int TDB_CONTINUE_TRIAL = 101;
    constexpr int TDB_EXIT = 102;

    HFONT CreateUiFont(UINT dpi)
    {
        LOGFONTW lf{};
        lf.lfHeight = -MulDiv(9, static_cast<int>(dpi), 72);
        lf.lfWeight = FW_NORMAL;
        lf.lfQuality = CLEARTYPE_QUALITY;
        wcscpy_s(lf.lfFaceName, L"Segoe UI");
        return CreateFontIndirectW(&lf);
    }

    void CenterOverOwner(HWND hwnd, HWND owner)
    {
        RECT ownerRc{}, selfRc{};
        HWND ref = (owner && IsWindow(owner)) ? owner : GetDesktopWindow();
        GetWindowRect(ref, &ownerRc);
        GetWindowRect(hwnd, &selfRc);
        int w = selfRc.right - selfRc.left;
        int h = selfRc.bottom - selfRc.top;
        int x = ownerRc.left + ((ownerRc.right - ownerRc.left) - w) / 2;
        int y = ownerRc.top + ((ownerRc.bottom - ownerRc.top) - h) / 2;
        SetWindowPos(hwnd, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    }

    // ---- Activation form (Name + Key) ----

    struct ActivationFormState
    {
        HWND editName = nullptr;
        HWND editKey = nullptr;
        std::wstring outName;
        std::wstring outKey;
        bool activated = false;
    };

    LRESULT CALLBACK ActivationWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        auto* state = reinterpret_cast<ActivationFormState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        switch (msg)
        {
            case WM_COMMAND:
            {
                int id = LOWORD(wParam);
                if (!state)
                    break;
                if (id == ID_ACTIVATE)
                {
                    wchar_t nameBuf[256] = {}, keyBuf[128] = {};
                    GetWindowTextW(state->editName, nameBuf, 256);
                    GetWindowTextW(state->editKey, keyBuf, 128);
                    state->outName = nameBuf;
                    state->outKey = keyBuf;
                    state->activated = true;
                    DestroyWindow(hwnd);
                }
                else if (id == ID_CANCEL)
                {
                    DestroyWindow(hwnd);
                }
                return 0;
            }
            case WM_CLOSE:
                DestroyWindow(hwnd);
                return 0;
            case WM_DESTROY:
                // Do NOT PostQuitMessage here - this window runs its own
                // local message loop (see ShowActivationForm), not the
                // application's main loop; posting WM_QUIT would leak out
                // and terminate the whole app once the outer loop sees it.
                return 0;
            default:
                return DefWindowProcW(hwnd, msg, wParam, lParam);
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

namespace LicenseDialog
{

TrialChoice ShowTrialPrompt(HWND owner, int daysRemaining, bool expired, const wchar_t* appName)
{
    TASKDIALOG_BUTTON buttons[3];
    int buttonCount = 0;
    buttons[buttonCount++] = { TDB_ENTER_LICENSE, L"Enter License Key" };
    if (!expired)
        buttons[buttonCount++] = { TDB_CONTINUE_TRIAL, L"Continue Trial" };
    buttons[buttonCount++] = { TDB_EXIT, L"Exit" };

    std::wstring mainInstruction = expired
        ? L"Your 30-day trial has ended"
        : (L"Trial mode \u2014 " + std::to_wstring(daysRemaining) + L" day" + (daysRemaining == 1 ? L"" : L"s") + L" remaining");

    std::wstring content = expired
        ? L"All features were available during your trial. Enter a license key to keep using it, or exit."
        : L"All features are unlocked during your trial. You can enter a license key at any time from the Help menu.";

    TASKDIALOGCONFIG config{};
    config.cbSize = sizeof(config);
    config.hwndParent = (owner && IsWindow(owner)) ? owner : nullptr;
    config.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_POSITION_RELATIVE_TO_WINDOW;
    config.pszWindowTitle = appName;
    config.pszMainInstruction = mainInstruction.c_str();
    config.pszContent = content.c_str();
    config.pButtons = buttons;
    config.cButtons = static_cast<UINT>(buttonCount);
    config.nDefaultButton = expired ? TDB_ENTER_LICENSE : TDB_CONTINUE_TRIAL;
    config.pszMainIcon = expired ? TD_WARNING_ICON : TD_INFORMATION_ICON;

    int clickedId = 0;
    HRESULT hr = TaskDialogIndirect(&config, &clickedId, nullptr, nullptr);

    if (FAILED(hr))
        return expired ? TrialChoice::Exit : TrialChoice::ContinueTrial; // conservative fallback if the API itself fails

    if (clickedId == TDB_ENTER_LICENSE) return TrialChoice::EnterLicense;
    if (clickedId == TDB_CONTINUE_TRIAL) return TrialChoice::ContinueTrial;
    if (clickedId == TDB_EXIT) return TrialChoice::Exit;

    // User dismissed via Esc/X (IDCANCEL) - be lenient during an active
    // trial (don't punish an accidental dismiss), but treat it the same as
    // Exit once expired, since there's no valid "continue" state.
    return expired ? TrialChoice::Exit : TrialChoice::ContinueTrial;
}

bool ShowActivationForm(HWND owner, HINSTANCE hInstance, std::wstring& outName, std::wstring& outKey, const wchar_t* appName)
{
    static const wchar_t* kClassName = L"ScreenRelayActivationFormClass";
    static bool registered = false;
    if (!registered)
    {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = ActivationWndProc;
        wc.hInstance = hInstance;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        wc.lpszClassName = kClassName;
        RegisterClassExW(&wc);
        registered = true;
    }

    HWND dpiRef = (owner && IsWindow(owner)) ? owner : GetDesktopWindow();
    UINT dpi = GetDpiForWindow(dpiRef);
    auto S = [dpi](int px) { return MulDiv(px, static_cast<int>(dpi), 96); };

    DWORD style = WS_CAPTION | WS_SYSMENU | WS_POPUP;
    RECT wr{ 0, 0, S(380), S(190) };
    AdjustWindowRectEx(&wr, style, FALSE, 0);

    ActivationFormState state;

    HWND hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME, kClassName,
        (std::wstring(appName) + L" - Enter License Key").c_str(),
        style, CW_USEDEFAULT, CW_USEDEFAULT, wr.right - wr.left, wr.bottom - wr.top,
        (owner && IsWindow(owner)) ? owner : nullptr, nullptr, hInstance, nullptr);
    if (!hwnd)
        return false;

    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&state));

    HFONT font = CreateUiFont(dpi);

    auto makeLabel = [&](const wchar_t* text, int x, int y, int w, int h)
    {
        HWND h2 = CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE, S(x), S(y), S(w), S(h), hwnd, nullptr, hInstance, nullptr);
        SendMessageW(h2, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        return h2;
    };

    makeLabel(L"Name / Company:", 16, 14, 300, 18);
    state.editName = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
        S(16), S(34), S(340), S(24), hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_EDIT_NAME)), hInstance, nullptr);
    SendMessageW(state.editName, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);

    makeLabel(L"License Key:", 16, 68, 300, 18);
    state.editKey = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
        S(16), S(88), S(340), S(24), hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_EDIT_KEY)), hInstance, nullptr);
    SendMessageW(state.editKey, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);

    HWND btnActivate = CreateWindowExW(0, L"BUTTON", L"Activate", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
        S(180), S(132), S(85), S(28), hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_ACTIVATE)), hInstance, nullptr);
    HWND btnCancel = CreateWindowExW(0, L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE,
        S(271), S(132), S(85), S(28), hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_CANCEL)), hInstance, nullptr);
    SendMessageW(btnActivate, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    SendMessageW(btnCancel, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);

    CenterOverOwner(hwnd, owner);
    if (owner && IsWindow(owner)) EnableWindow(owner, FALSE);
    ShowWindow(hwnd, SW_SHOW);
    SetFocus(state.editName);

    MSG msg;
    while (IsWindow(hwnd) && GetMessageW(&msg, nullptr, 0, 0) > 0)
    {
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE)
        {
            DestroyWindow(hwnd);
            continue;
        }
        if (!IsDialogMessageW(hwnd, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    if (owner && IsWindow(owner))
    {
        EnableWindow(owner, TRUE);
        SetForegroundWindow(owner);
    }
    DeleteObject(font);

    if (state.activated)
    {
        outName = state.outName;
        outKey = state.outKey;
    }
    return state.activated;
}

void ShowInvalidKeyMessage(HWND owner, const wchar_t* appName)
{
    MessageBoxW(owner,
        L"That name/license key combination isn't valid. Please check both fields and try again.",
        (std::wstring(appName) + L" - Invalid License Key").c_str(),
        MB_OK | MB_ICONWARNING);
}

} // namespace LicenseDialog
