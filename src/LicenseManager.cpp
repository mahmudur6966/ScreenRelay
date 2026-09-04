#include "LicenseManager.h"

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#include <windows.h>
#include <algorithm>
#include <cwctype>
#include <ctime>

#pragma comment(lib, "advapi32.lib")

namespace
{
    const wchar_t* kRegPath = L"Software\\ScreenRelay";

    // CHANGE THIS before distributing real licenses. This string is the
    // only thing that makes ComputeExpectedKey's output unpredictable -
    // anyone who has it (and the algorithm below, which is not a secret
    // once this source file is) can generate valid keys for any name.
    const wchar_t* kLicenseSecret = L"A1B2-C3D4-E5F6-1234";

    std::wstring NormalizeName(const std::wstring& name)
    {
        std::wstring result;
        for (wchar_t c : name)
        {
            if (!iswspace(c))
                result += towupper(c);
        }
        return result;
    }

    std::wstring NormalizeKey(const std::wstring& key)
    {
        std::wstring result;
        for (wchar_t c : key)
        {
            if (c != L'-' && !iswspace(c))
                result += towupper(c);
        }
        return result;
    }

    std::wstring TrimWhitespace(const std::wstring& s)
    {
        size_t start = s.find_first_not_of(L" \t\r\n");
        size_t end = s.find_last_not_of(L" \t\r\n");
        if (start == std::wstring::npos)
            return L"";
        return s.substr(start, end - start + 1);
    }

    // FNV-1a 64-bit, folded to bytes explicitly so the result doesn't
    // depend on wchar_t's size on a given platform/compiler.
    unsigned long long Fnv1a64(const std::wstring& s)
    {
        unsigned long long hash = 14695981039346656037ULL;
        for (wchar_t c : s)
        {
            hash ^= static_cast<unsigned long long>(c & 0xFF);
            hash *= 1099511628211ULL;
            hash ^= static_cast<unsigned long long>((c >> 8) & 0xFF);
            hash *= 1099511628211ULL;
        }
        return hash;
    }

    std::wstring FormatKey(unsigned long long value)
    {
        wchar_t buf[20] = {};
        swprintf_s(buf, L"%016llX", value);
        std::wstring hex = buf;
        std::wstring formatted;
        for (size_t i = 0; i < hex.size(); ++i)
        {
            if (i > 0 && i % 4 == 0)
                formatted += L'-';
            formatted += hex[i];
        }
        return formatted; // e.g. "A1B2-C3D4-E5F6-1234"
    }
}

std::wstring LicenseManager::ComputeExpectedKey(const std::wstring& name)
{
    std::wstring normalized = NormalizeName(name);
    if (normalized.empty())
        return L"";
    std::wstring combined = normalized + L"|" + kLicenseSecret;
    return FormatKey(Fnv1a64(combined));
}

LicenseManager::LicenseManager()
{
    LoadFirstRunDate();
    LoadStoredLicense();
}

void LicenseManager::LoadFirstRunDate()
{
    long long now = static_cast<long long>(time(nullptr));
    m_firstRunUnixSeconds = now;
    m_effectiveNowUnixSeconds = now;

    HKEY hKey;
    std::wstring trialPath = std::wstring(kRegPath) + L"\\Trial";
    if (RegCreateKeyExW(HKEY_CURRENT_USER, trialPath.c_str(), 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &hKey, nullptr) != ERROR_SUCCESS)
        return; // fall back to "just started now" if the registry is unreachable

    DWORD64 firstRun = 0, lastSeen = 0;
    DWORD size = sizeof(DWORD64), type = 0;
    bool haveFirstRun = RegQueryValueExW(hKey, L"FirstRunUnix", nullptr, &type, reinterpret_cast<BYTE*>(&firstRun), &size) == ERROR_SUCCESS && type == REG_QWORD;
    size = sizeof(DWORD64);
    bool haveLastSeen = RegQueryValueExW(hKey, L"LastSeenUnix", nullptr, &type, reinterpret_cast<BYTE*>(&lastSeen), &size) == ERROR_SUCCESS && type == REG_QWORD;

    if (!haveFirstRun)
    {
        firstRun = static_cast<DWORD64>(now);
        RegSetValueExW(hKey, L"FirstRunUnix", 0, REG_QWORD, reinterpret_cast<const BYTE*>(&firstRun), sizeof(firstRun));
    }

    // Anti-rollback: never let the "now" used for elapsed-day math appear
    // earlier than the latest time we've already observed - defeats the
    // simplest bypass (turning the system clock back to reset the trial).
    long long effectiveNow = std::max<long long>(now, haveLastSeen ? static_cast<long long>(lastSeen) : now);
    DWORD64 toStore = static_cast<DWORD64>(effectiveNow);
    RegSetValueExW(hKey, L"LastSeenUnix", 0, REG_QWORD, reinterpret_cast<const BYTE*>(&toStore), sizeof(toStore));

    m_firstRunUnixSeconds = static_cast<long long>(firstRun);
    m_effectiveNowUnixSeconds = effectiveNow;

    RegCloseKey(hKey);
}

void LicenseManager::LoadStoredLicense()
{
    HKEY hKey;
    std::wstring licPath = std::wstring(kRegPath) + L"\\License";
    if (RegOpenKeyExW(HKEY_CURRENT_USER, licPath.c_str(), 0, KEY_READ, &hKey) != ERROR_SUCCESS)
        return;

    wchar_t nameBuf[256] = {}, keyBuf[128] = {};
    DWORD nameSize = sizeof(nameBuf), keySize = sizeof(keyBuf);
    bool haveName = RegQueryValueExW(hKey, L"Name", nullptr, nullptr, reinterpret_cast<BYTE*>(nameBuf), &nameSize) == ERROR_SUCCESS;
    bool haveKey = RegQueryValueExW(hKey, L"Key", nullptr, nullptr, reinterpret_cast<BYTE*>(keyBuf), &keySize) == ERROR_SUCCESS;
    RegCloseKey(hKey);

    if (haveName && haveKey)
    {
        std::wstring expected = ComputeExpectedKey(nameBuf);
        if (!expected.empty() && NormalizeKey(expected) == NormalizeKey(keyBuf))
        {
            m_licensed = true;
            m_licensedName = nameBuf;
        }
    }
}

int LicenseManager::TrialDaysRemaining() const
{
    if (m_licensed)
        return 0;
    long long elapsedSeconds = m_effectiveNowUnixSeconds - m_firstRunUnixSeconds;
    long long elapsedDays = elapsedSeconds / 86400;
    long long remaining = static_cast<long long>(kTrialDays) - elapsedDays;
    if (remaining < 0) remaining = 0;
    if (remaining > kTrialDays) remaining = kTrialDays;
    return static_cast<int>(remaining);
}

bool LicenseManager::IsTrialExpired() const
{
    return !m_licensed && TrialDaysRemaining() <= 0;
}

bool LicenseManager::Activate(const std::wstring& name, const std::wstring& key)
{
    std::wstring trimmedName = TrimWhitespace(name);
    if (trimmedName.empty() || key.empty())
        return false;

    std::wstring expected = ComputeExpectedKey(trimmedName);
    if (expected.empty() || NormalizeKey(expected) != NormalizeKey(key))
        return false;

    HKEY hKey;
    std::wstring licPath = std::wstring(kRegPath) + L"\\License";
    if (RegCreateKeyExW(HKEY_CURRENT_USER, licPath.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &hKey, nullptr) == ERROR_SUCCESS)
    {
        RegSetValueExW(hKey, L"Name", 0, REG_SZ, reinterpret_cast<const BYTE*>(trimmedName.c_str()),
                        static_cast<DWORD>((trimmedName.size() + 1) * sizeof(wchar_t)));
        RegSetValueExW(hKey, L"Key", 0, REG_SZ, reinterpret_cast<const BYTE*>(key.c_str()),
                        static_cast<DWORD>((key.size() + 1) * sizeof(wchar_t)));
        RegCloseKey(hKey);
    }

    m_licensed = true;
    m_licensedName = trimmedName;
    return true;
}

void LicenseManager::Deactivate()
{
    HKEY hKey;
    std::wstring licPath = std::wstring(kRegPath) + L"\\License";
    if (RegOpenKeyExW(HKEY_CURRENT_USER, licPath.c_str(), 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS)
    {
        RegDeleteValueW(hKey, L"Name");
        RegDeleteValueW(hKey, L"Key");
        RegCloseKey(hKey);
    }
    m_licensed = false;
    m_licensedName.clear();
}
