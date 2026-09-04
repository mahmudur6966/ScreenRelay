# ScreenRelay

A Windows desktop app that plays looped video full-screen to one or more
physical monitors, independent of whichever monitor the app's own Settings
window is sitting on. Typical setup: one monitor running the app/desktop,
two more connected over HDMI purely as video outputs (digital signage,
lobby displays, showroom screens, etc).

Built on plain Win32 + Windows Media Foundation's high-level player
(MFPlay). No third-party codec pack, UI framework, or runtime is required —
only the Windows SDK.

---

## Features

- **Media Library** — add video files once; any monitor can be assigned
  any library entry. Assign the same video to two monitors to mirror it,
  or different videos to run independent loops.
- **Per-monitor output control** — enable/disable each detected monitor
  independently, and optionally force a custom resolution, refresh rate,
  and rotation (0/90/180/270°) for that monitor while the app runs; the
  original mode is restored when outputs stop.
- **Dynamic monitor detection** — the monitor list re-scans automatically
  on hot-plug/unplug (`WM_DISPLAYCHANGE`, debounced ~800ms). Assignments
  are matched to the physical device where possible, so replugging a
  monitor into the same port doesn't scramble its settings.
- **Live status per monitor** — Idle / Opening / ● Live / ⚠ Error, polled
  a few times a second.
- **Tight looping** — the player seeks back to the start ~200ms before a
  clip's natural end (instead of waiting for it to fully end), so in the
  common case MFPlay never hits a hard stop/restart. See
  [Looping behavior & limitations](#looping-behavior--limitations) below
  for what this does and doesn't guarantee.
- **config.ini persistence** — "Save as Default" writes the current
  library + per-monitor assignment to `config.ini`, which auto-loads (and
  optionally auto-starts) on the next launch — handy for running this at
  Windows startup on a dedicated playout box.
- Themed native controls (ComCtl32 v6), per-monitor DPI awareness, a
  system tray icon so outputs keep running when the Settings window is
  closed, and a single-instance guard.

---

## Requirements

- Windows 10 or later (uses `SetProcessDpiAwarenessContext` /
  `GetDpiForWindow`, with a graceful fallback to the older
  `SetProcessDpiAwareness` API on Windows 8.1; below that, DPI awareness
  is simply skipped).
- Visual Studio 2022/2026 with the "Desktop development with C++"
  workload and the Windows 10/11 SDK.
- No external dependencies — just the Windows SDK libraries already
  listed in the `.vcxproj` (`mfplay.lib`, `mf.lib`, `mfplat.lib`,
  `mfreadwrite.lib`, `mfuuid.lib`,
  `shell32.lib`, `user32.lib`, `gdi32.lib`, `ole32.lib`, `comdlg32.lib`,
  `comctl32.lib`).

## Building

1. Open `ScreenRelay.sln` in Visual Studio.
2. If prompted to retarget the platform toolset, accept it (the project
   ships targeting `v143`; VS will offer to update it to whatever you
   have installed).
3. Build `x64` / `Debug` or `Release`.
4. The built `.exe` and a copy of `config.ini` land in
   `bin\x64\<Configuration>\`.

> **Note on this build:** this codebase was written and reviewed in an
> environment without access to an actual Windows/MSVC toolchain, so it
> has been checked carefully (brace/paren balance, MFPlay/Win32 API
> signatures cross-checked against known-good usage) but **not
> compiled**. If you hit build errors, they should be small and
> localized — happy to fix them.

## Running

```
ScreenRelay.exe                 Opens Settings, loads config.ini into the UI (does not auto-start)
ScreenRelay.exe /autostart       Loads config.ini AND immediately starts those outputs; Settings window stays hidden (tray only)
ScreenRelay.exe myconfig.ini     Use a specific config file instead of config.ini
ScreenRelay.exe /list            Lists detected monitors (index, name, bounds) and exits - no window
```

`/autostart` is intended for launching the app at Windows sign-in (e.g. via
a shortcut in the Startup folder or a Scheduled Task) so a playout box
comes back up on its own after a reboot, with the Settings window staying
tucked away in the tray.

### Typical workflow

1. Launch the app. It lists every detected monitor as a card.
2. In the **Media Library** panel, click **Add Video(s)...** and pick one
   or more files.
3. On each monitor's card, tick **On**, pick a video from the **Video
   source** dropdown, and set width/height/refresh/rotation if you want to
   force a specific mode (leave blank to keep the monitor's current mode).
4. Click **Start / Apply**. Each enabled, assigned monitor gets a
   borderless full-screen output window playing its assigned video, looped.
5. Click **Save as Default** to persist this layout to `config.ini` so it
   comes back automatically next launch (see `/autostart` above).
6. Closing the Settings window (the X button) just hides it to the tray —
   outputs keep running. Use the tray icon's right-click menu to bring
   Settings back, stop everything, or exit the app for real.

Press **Esc** on an individual output window to close just that one output
(useful while testing).

---

## config.ini format (v2)

```ini
[Library]
Item1=1|C:\Videos\file1.mp4|file1.mp4
Item2=2|C:\Videos\file2.mp4|file2.mp4

[Monitor2]
LibraryId=1
Width=1920
Height=1080
RefreshHz=60
Rotation=0
Loop=1

[Monitor3]
LibraryId=2
Width=1920
Height=1080
RefreshHz=60
Rotation=0
Loop=1
```

- **`[Library]`** — the video pool. Each line is
  `ItemN=<id>|<full path>|<display name>`. IDs are stable for the life of
  the file; monitors reference a video by this ID, not by embedding a path
  directly, so the same video can back multiple `[MonitorN]` sections just
  by repeating its `LibraryId`.
- **`[MonitorN]`** — one section per output. `N` must match the monitor
  number shown by `ScreenRelay.exe /list` (this numbering is a
  best-effort match to what Windows' Display Settings calls "Monitor
  1/2/3...", not a guarantee — always confirm with `/list` if in doubt).
  - `LibraryId` — which library entry to play here (`0` or omitted = none).
  - `Width` / `Height` — force a resolution; `0` = leave the monitor's
    current mode alone.
  - `RefreshHz` — force a refresh rate in Hz; `0` = leave alone.
  - `Rotation` — `0`, `90`, `180`, or `270`; `-1` = leave alone.
  - `Loop` — `1` to loop forever, `0` to play once and stop.
  - `Mute` — `1` to play this output's audio silently, `0` for normal audio.
- Editing by hand works, but using the Settings window's **Save as
  Default** is much less error-prone.

### Upgrading from the old (v1) format

Older config files that only had a `File=<path>` line directly under
`[MonitorN]` (no `[Library]` section at all) still load correctly: each
`File=` path is automatically imported into the library on load, and
`LibraryId` gets filled in. Opening such a file and clicking **Save as
Default** rewrites it in the new v2 format.

---

## Fixed in this pass

- **Mouse pointer disappearing app-wide after Start / Apply.** The old code
  called `ShowCursor(FALSE)` in each output window's `Create()` — that API
  hides the cursor for the *entire application* (a global per-thread
  counter with no matching restore), so it vanished over the Settings
  window too, and stacked worse with more monitors. Fixed by hiding the
  cursor only while it's over an output window's own client area
  (`WM_SETCURSOR`), which is how this is normally done in Win32.
- **Visual pass**: monitor rows and the library panel are now custom-drawn
  rounded "cards" (white fill, light border, bold title) on a soft gray
  background instead of classic `BS_GROUPBOX` frames, and **Start / Apply**
  is an accent-colored owner-drawn button so the primary action is obvious
  at a glance.
- **Per-monitor Mute** checkbox — useful for signage loops where you want
  video but not audio on some or all outputs.
- **Start with Windows** — `File` menu toggle that adds/removes a
  `HKCU\...\Run` entry so the app comes back after a reboot without manual
  Task Scheduler setup (writes `"<exe path>" /autostart`, no admin rights
  needed).
- **Card panels intercepting clicks.** The custom-drawn card panels behind
  each monitor row weren't click-through the way a native `GROUPBOX` frame
  is, so a card could end up above an earlier sibling control in z-order
  and silently swallow its clicks. Fixed via `WM_NCHITTEST` → `HTTRANSPARENT`.
- **Video thumbnails** in the Media Library list — a real frame is decoded
  from each file (via Media Foundation's Source Reader, ~1 second into the
  clip to skip black/fade-in opens) and shown next to the filename, so a
  library with several videos is easy to scan instead of just a list of
  filenames.
- **Thumbnails never actually appearing.** The Source Reader was created
  without `MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING`, so its request to
  decode straight to RGB32 silently failed for almost every real-world
  video file (most decoders natively output NV12, not RGB32, and without
  that attribute the reader won't auto-insert a color converter to bridge
  the two). Every thumbnail was quietly falling back to the placeholder
  tile. Fixed by enabling that attribute when creating the reader.
- **Renamed the app to ScreenRelay** — directory, solution/project name,
  window titles, tray tooltip, registry keys (startup entry, license
  storage), mutex name, and window class names all updated to match.
- **30-day trial + license system** — see [Licensing](#licensing) below.

## Licensing

ScreenRelay now includes a simple offline trial/license system:

- **First 30 days**: every feature is fully unlocked. A small reminder
  dialog shows on launch with the days remaining and an "Enter License
  Key" shortcut (skipped on a silent `/autostart` launch so it doesn't
  block an unattended boot — see below).
- **After 30 days**: the same dialog appears, but without a "Continue
  Trial" option — only "Enter License Key" or "Exit". This applies even on
  `/autostart`, since silently running past an expired trial forever would
  defeat the point of having one.
- **Activating**: Help menu → **Enter License Key...** (also reachable
  from the trial dialog itself) opens a small Name + License Key form.
- License state is stored per-Windows-user in
  `HKEY_CURRENT_USER\Software\ScreenRelay` (`Trial` and `License` subkeys) —
  no files, no server, no internet connection needed.

### How the key scheme works

A key is derived from the entered **Name** using FNV-1a 64-bit hashing of
`NORMALIZED_NAME|SECRET`, formatted as 16 hex digits in four dash-separated
groups (e.g. `A1B2-C3D4-E5F6-1234`). The exact algorithm is implemented
twice, kept in sync on purpose:

- `LicenseManager::ComputeExpectedKey` in `src/LicenseManager.cpp` — what
  the app itself checks a key against.
- `tools/generate_license_key.py` — a small script you run to actually
  issue a key for a customer: `python generate_license_key.py "Customer Name"`.

**Before shipping this to anyone**, change `kLicenseSecret` in
`src/LicenseManager.cpp` **and** `SECRET` in
`tools/generate_license_key.py` to the same new private value (they must
match each other, and must differ from the placeholder currently in both
files, since that placeholder has been shared in the conversation that
produced this code).

### Honest limitations

This is offline, client-side license checking — like any scheme of that
kind, it's a deterrent against casual copying, not tamper-proof DRM.
Anyone willing to patch the compiled binary or edit the registry directly
can bypass it; no purely offline check can prevent that, and I'd rather
say so plainly than oversell it. If you eventually need stronger
protection (e.g. selling broadly and expecting deliberate cracking
attempts), that means a server-side activation step, which is a
meaningfully bigger feature than what's here.

The trial timer also has a basic anti-rollback guard (it tracks the latest
time it's ever seen and won't let the countdown "go backward" if you turn
the system clock back), but a determined user could still reset the trial
by deleting `HKCU\Software\ScreenRelay` outright, reinstalling Windows, or
similar. Again — deterrent, not a vault.

## Building an installer

`installer/ScreenRelay.iss` is a ready-to-use
[Inno Setup](https://jrsoftware.org/isdl.php) script (free, no other
dependency needed since the project statically links the C++ runtime -
`RuntimeLibrary` is set to `MultiThreaded`/`MultiThreadedDebug`, so the
installed app needs nothing but Windows itself):

1. Build `Release|x64` first.
2. Install Inno Setup, open `installer\ScreenRelay.iss` in it
   (or run `ISCC.exe installer\ScreenRelay.iss` from a command
   prompt), and compile.
3. The installer lands in `installer\Output\ScreenRelay-Setup-<version>.exe`.

It installs per-user under `%LocalAppData%\Programs\ScreenRelay`
(no admin/UAC prompt) rather than Program Files, on purpose - the app
writes `config.ini` next to its own `.exe` when you click **Save as
Default**, and a standard user can't write into Program Files without
running elevated every time. The script has a commented-out alternative
for an all-users Program Files install if you specifically want that
instead (see the comments in the `.iss` file for the trade-off).

Generate your own AppId GUID for the script before shipping this to real
users (Inno Setup's IDE has Tools > Generate GUID) - AppId must stay
constant across versions so future installers upgrade in place instead of
installing side-by-side.

## Looping behavior & limitations

MFPlay (the high-level Media Foundation player this app uses) doesn't have
a first-class "loop" primitive. This app uses a poll timer to check
playback position and seeks back to frame 0 roughly 200ms before the
clip's natural end, so in the common case MFPlay's own "playback ended"
stop/restart sequence — which shows a brief black frame — never actually
happens. A fallback listens for the natural end-of-clip event too, in case
a very short clip or a stalled poll causes the preemptive seek to miss.

This is a practical mitigation, not a hardware-guaranteed zero-gap loop.
True frame-accurate gapless looping (the kind a dedicated playout/CG
engine like AirBox or PlayBox does) needs a custom Media Session topology
with a pre-rolled duplicate source, which is a materially larger project
than wrapping MFPlay. If frame-perfect looping turns out to matter for
your use case, that's the next architectural step — worth a separate
conversation if you want to go there.

---

## Project structure

```
ScreenRelay.sln
ScreenRelay.vcxproj(.filters/.user)
config.ini                     Example / default config (v2 format)
installer/
  ScreenRelay.iss               Inno Setup script - see "Building an installer"
tools/
  generate_license_key.py       Issues real license keys (see "Licensing")
src/
  main.cpp                     Entry point: DPI awareness, ComCtl32 v6 manifest,
                                single-instance guard, command-line parsing
  MonitorManager.h/.cpp        Enumerates monitors, changes resolution/refresh/rotation
  MediaLibrary.h/.cpp          The video pool (add/remove/find by id or path)
  ThumbnailGenerator.h/.cpp    Grabs a frame from a video file as an HBITMAP (Media
                                Foundation Source Reader)
  ConfigLoader.h/.cpp          Reads/writes config.ini (v2 format, with v1 migration)
  VideoWindow.h/.cpp           One borderless full-screen output window + MFPlay player
                                + preemptive loop-seek timer + status tracking
  OutputManager.h/.cpp         Owns all active VideoWindows and any monitor-mode changes,
                                exposes per-monitor live status
  LicenseManager.h/.cpp        Trial countdown + offline license key validation (HKCU-backed)
  LicenseDialog.h/.cpp         Trial/expiry prompt (TaskDialog) + license key entry form
  SettingsWindow.h/.cpp        The whole UI: media library panel, per-monitor cards,
                                menu bar, status bar, tray icon, hot-plug handling,
                                license gate on startup
```

## Command-line quick reference

| Command | Effect |
|---|---|
| `ScreenRelay.exe` | Open Settings, load config.ini, don't start |
| `ScreenRelay.exe /autostart` | Load config.ini and start immediately, stay in tray |
| `ScreenRelay.exe path\to\other.ini` | Use a specific config file |
| `ScreenRelay.exe /list` | Print detected monitors and exit |
