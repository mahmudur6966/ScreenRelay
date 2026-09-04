; ScreenRelay installer script for Inno Setup
; (free, download from https://jrsoftware.org/isdl.php)
;
; How to build the installer:
;   1. Build the project in Visual Studio as Release|x64, so
;      bin\x64\Release\ScreenRelay.exe and config.ini exist
;      (the .vcxproj already copies config.ini next to the .exe).
;   2. Install Inno Setup.
;   3. Open this file in the Inno Setup Compiler (Start Menu > Inno Setup
;      Compiler > File > Open), or from a command prompt:
;        "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" installer\ScreenRelay.iss
;   4. Click Build > Compile (or just press F9). The finished installer
;      lands in installer\Output\ScreenRelay-Setup-<version>.exe.
;
; This script installs per-user (no admin rights / UAC prompt needed) into
; %LocalAppData%\Programs\ScreenRelay. That's deliberate: the
; app writes config.ini next to its own .exe when you click "Save as
; Default", and a standard user can't write into Program Files without
; running elevated every time. If you specifically want an all-users,
; Program Files install instead, see the commented alternative under
; [Setup] below - just be aware "Save as Default" will then need the app
; run as Administrator to succeed.

#define MyAppName "ScreenRelay"
#define MyAppVersion "1.0.0"
#define MyAppPublisher "Your Company"
#define MyAppExeName "ScreenRelay.exe"
#define SourceDir "..\bin\x64\Release"

[Setup]
; Generate your own GUID (Tools > Generate GUID in the Inno Setup IDE, or
; any online GUID generator) and use it here instead - AppId must stay the
; same across versions so upgrades install over the old copy rather than
; side-by-side.
AppId={{B6F1B9C2-8E2C-4B0E-9B2E-7E7B0E9B9B4A}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={localappdata}\Programs\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
; --- All-users / Program Files alternative (needs admin at install time,
;     and needs the app run elevated for "Save as Default" to work) ---
; DefaultDirName={autopf}\{#MyAppName}
; PrivilegesRequired=admin
OutputDir=Output
OutputBaseFilename={#MyAppName}-Setup-{#MyAppVersion}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayIcon={app}\{#MyAppExeName}
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop shortcut"; GroupDescription: "Additional shortcuts:"; Flags: unchecked

[Files]
Source: "{#SourceDir}\{#MyAppExeName}"; DestDir: "{app}"; Flags: ignoreversion
; onlyifdoesntexist: an upgrade/reinstall won't clobber a config.ini the
; user has already customized via "Save as Default".
Source: "{#SourceDir}\config.ini"; DestDir: "{app}"; Flags: ignoreversion onlyifdoesntexist

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\Uninstall {#MyAppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "Launch {#MyAppName} now"; Flags: nowait postinstall skipifsilent

; config.ini is left in place on uninstall (so a later reinstall keeps the
; user's monitor/video layout). Uncomment to wipe it on uninstall instead:
; [UninstallDelete]
; Type: files; Name: "{app}\config.ini"
