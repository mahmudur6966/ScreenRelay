#!/usr/bin/env python3
"""
generate_license_key.py

Generates a ScreenRelay license key for a given customer name, using the
exact same offline scheme as LicenseManager::ComputeExpectedKey in
src/LicenseManager.cpp:

    normalized = uppercase(name) with all whitespace removed
    combined   = normalized + "|" + SECRET
    hash       = FNV-1a 64-bit of combined (each UTF-16 code unit folded
                 into the hash as two bytes - low byte, then high byte -
                 matching the C++ loop over `wchar_t` exactly)
    key        = hash formatted as 16 uppercase hex digits, grouped in 4s
                 with dashes, e.g. "A1B2-C3D4-E5F6-1234"

IMPORTANT: SECRET below must match kLicenseSecret in src/LicenseManager.cpp
EXACTLY, and BOTH must be changed from the shared placeholder value before
you ship this to real customers - anyone who has seen this placeholder
(which has been shared in this conversation) can already generate valid
keys for any name against an unmodified build.

Usage:
    python generate_license_key.py "Customer Name or Company"
"""
import sys

# Must match kLicenseSecret in src/LicenseManager.cpp exactly.
SECRET = "CHANNELS-STUDIO-2026"

FNV_OFFSET_BASIS = 0xcbf29ce484222325
FNV_PRIME = 0x100000001b3
MASK64 = 0xFFFFFFFFFFFFFFFF


def normalize_name(name: str) -> str:
    return "".join(ch.upper() for ch in name if not ch.isspace())


def fnv1a64(s: str) -> int:
    """Matches LicenseManager.cpp's Fnv1a64: each character is folded into
    the hash as two bytes (low byte, then high byte), exactly like the C++
    loop over `wchar_t` (2 bytes on Windows, UTF-16)."""
    h = FNV_OFFSET_BASIS
    for ch in s:
        code = ord(ch) & 0xFFFF
        low = code & 0xFF
        high = (code >> 8) & 0xFF
        h ^= low
        h = (h * FNV_PRIME) & MASK64
        h ^= high
        h = (h * FNV_PRIME) & MASK64
    return h


def format_key(value: int) -> str:
    hex_str = f"{value:016X}"
    return "-".join(hex_str[i:i + 4] for i in range(0, 16, 4))


def generate_key(name: str) -> str:
    normalized = normalize_name(name)
    if not normalized:
        raise ValueError("Name cannot be empty (after removing whitespace).")
    combined = normalized + "|" + SECRET
    return format_key(fnv1a64(combined))


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print('Usage: python generate_license_key.py "Customer Name or Company"')
        sys.exit(1)

    customer_name = sys.argv[1]
    print(f"Name: {customer_name}")
    print(f"Key:  {generate_key(customer_name)}")
