#pragma once
//
// LicenseManager.h
//
// A minimal offline trial/license system:
//  - First launch is recorded in the registry (HKCU) and the trial runs
//    for kTrialDays from that date. A "last seen" timestamp is also
//    tracked so turning the system clock backward doesn't extend the
//    trial - the effective "now" used for the countdown never moves
//    earlier than the latest time this app has actually observed.
//  - A license "activates" by storing a Name + Key pair (HKCU) once the
//    Key validates against the Name using a simple offline hash scheme
//    (see ComputeExpectedKey in the .cpp).
//
// Honesty note, worth reading before shipping this to real customers:
// like any purely client-side license check, this deters casual copying -
// it is NOT tamper-proof. Anyone willing to patch the binary or edit the
// registry can bypass it; no offline-only scheme can prevent that. Treat
// this as "keeps honest users honest," not real DRM. A production product
// that actually needs strong protection would use a server-side
// activation/licensing service instead.
//
// IMPORTANT: change kLicenseSecret in LicenseManager.cpp to your own
// private value before distributing this to anyone - it is the one piece
// that makes generated keys unpredictable, and it ships inside the .cpp
// file (and therefore, unless you keep your source private, inside the
// compiled .exe too).
//
#include <string>

class LicenseManager
{
public:
    static constexpr int kTrialDays = 30;

    LicenseManager();

    // True once a valid Name+Key pair is stored (see Activate()).
    bool IsLicensed() const { return m_licensed; }
    std::wstring LicensedName() const { return m_licensedName; }

    // Whole days left in the trial, clamped to [0, kTrialDays]. Always 0
    // once licensed (irrelevant at that point) or once the trial has run out.
    int TrialDaysRemaining() const;
    bool IsTrialExpired() const;

    // Validates key against name using the offline hash scheme and, if
    // valid, persists both to HKCU and updates IsLicensed()/LicensedName().
    // Returns false (and changes nothing) if the key doesn't match.
    bool Activate(const std::wstring& name, const std::wstring& key);

    // Removes a stored license. Not exposed in the UI; here for testing.
    void Deactivate();

    // Computes the expected key for a given name - the one place this
    // logic lives; Activate() calls it too, and it's what a companion
    // key-generation script (see /tools in the repo) reimplements to
    // produce real keys for customers.
    static std::wstring ComputeExpectedKey(const std::wstring& name);

private:
    void LoadFirstRunDate();
    void LoadStoredLicense();

    bool m_licensed = false;
    std::wstring m_licensedName;
    long long m_firstRunUnixSeconds = 0;
    long long m_effectiveNowUnixSeconds = 0;
};
