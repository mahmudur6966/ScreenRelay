#pragma once
//
// LicenseDialog.h
//
// Small UI pieces for the trial/license flow. Deliberately built the same
// way as the rest of the app - plain Win32, no .rc dialog resources:
//  - ShowTrialPrompt uses TaskDialogIndirect (a built-in Windows API) for
//    the "you're on trial" / "trial expired" prompt, since its custom
//    buttons cover everything that dialog needs without hand-building a
//    window for it.
//  - ShowActivationForm is a small hand-built "fake modal" window (owner
//    disabled + a local message loop) since entering a Name and a License
//    Key needs real text fields, which TaskDialog can't do.
//
#include <windows.h>
#include <string>

namespace LicenseDialog
{
    enum class TrialChoice { EnterLicense, ContinueTrial, Exit };

    // Shown at startup (and can be re-shown from the Help menu). If
    // expired is true, "Continue Trial" isn't offered - there's nothing
    // left to continue - only "Enter License Key" or "Exit".
    TrialChoice ShowTrialPrompt(HWND owner, int daysRemaining, bool expired, const wchar_t* appName);

    // A small Name + License Key entry form. Returns true if the user
    // filled both fields and clicked Activate - the caller still needs to
    // validate/store it via LicenseManager::Activate, and on failure can
    // show ShowInvalidKeyMessage and call this again.
    bool ShowActivationForm(HWND owner, HINSTANCE hInstance, std::wstring& outName, std::wstring& outKey, const wchar_t* appName);

    void ShowInvalidKeyMessage(HWND owner, const wchar_t* appName);
}
