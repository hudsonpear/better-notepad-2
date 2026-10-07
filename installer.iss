; Better Notepad (native Qt build) installer. Build with:
;   "C:\Users\PC05\AppData\Local\Programs\Inno Setup 6\ISCC.exe" installer.iss
; Output: installer-output\BetterNotepad-Setup-<version>.exe
#define AppName "Better Notepad"
#define AppVersion "2.0.0"
#define AppExe "BetterNotepad.exe"

[Setup]
AppId={{B3E1F5A2-6C41-4D7E-9A58-2F0C8E1D7B64}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher=Hudson Pear (pyrus)
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
; Per-user install by default (no admin prompt); the user can still pick "all users" in the dialog.
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
SetupIconFile=src-cpp\app.ico
UninstallDisplayIcon={app}\{#AppExe}
UninstallDisplayName={#AppName}
OutputDir=installer-output
OutputBaseFilename=BetterNotepad-Setup-{#AppVersion}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ChangesAssociations=yes
CloseApplications=yes
RestartApplications=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop shortcut"; Flags: unchecked
Name: "assoc"; Description: "Add {#AppName} to the ""Open with"" list of text, config and script files (.txt .md .log .ini .json .cfg .js .html .htm .css .vbs .reg .xml .sh .ps1 .bat). It is NOT set as the default app, so double-clicking files (including .bat) behaves exactly as before."; GroupDescription: "File types:"

[Files]
Source: "dist-native\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
; Bundled skins go to the user skins folder the app reads. Never overwrite a skin the user edited, and keep them on uninstall.
Source: "skins\*.json"; DestDir: "{localappdata}\com.pyrus.better-notepad\skins"; Flags: onlyifdoesntexist uninsneveruninstall
; Visual C++ runtime, only installed when missing (same installer the Qt build shipped with).
Source: "redist\vc_redist.x64.exe"; DestDir: "{tmp}"; Flags: deleteafterinstall; Check: VCRedistNeeded

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\{#AppExe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; Tasks: desktopicon

[Run]
Filename: "{tmp}\vc_redist.x64.exe"; Parameters: "/install /quiet /norestart"; StatusMsg: "Installing Visual C++ runtime..."; Flags: waituntilterminated; Check: VCRedistNeeded
Filename: "{app}\{#AppExe}"; Description: "Launch {#AppName}"; Flags: nowait postinstall skipifsilent

[Registry]
; "Alternate" registration only: OpenWithProgids puts us in the "Open with" list without touching
; the default handler of any extension (the Tauri build used the same rank).
Root: HKA; Subkey: "Software\Classes\BetterNotepad.File"; ValueType: string; ValueName: ""; ValueData: "{#AppName} document"; Flags: uninsdeletekey; Tasks: assoc
Root: HKA; Subkey: "Software\Classes\BetterNotepad.File\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: """{app}\{#AppExe}"",0"; Tasks: assoc
Root: HKA; Subkey: "Software\Classes\BetterNotepad.File\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#AppExe}"" ""%1"""; Tasks: assoc
Root: HKA; Subkey: "Software\Classes\Applications\{#AppExe}"; ValueType: string; ValueName: "FriendlyAppName"; ValueData: "{#AppName}"; Flags: uninsdeletekey; Tasks: assoc
Root: HKA; Subkey: "Software\Classes\Applications\{#AppExe}\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#AppExe}"" ""%1"""; Tasks: assoc
Root: HKA; Subkey: "Software\Classes\.txt\OpenWithProgids"; ValueType: string; ValueName: "BetterNotepad.File"; ValueData: ""; Flags: uninsdeletevalue; Tasks: assoc
Root: HKA; Subkey: "Software\Classes\Applications\BetterNotepad.exe\SupportedTypes"; ValueType: string; ValueName: ".txt"; ValueData: ""; Tasks: assoc
Root: HKA; Subkey: "Software\Classes\.md\OpenWithProgids"; ValueType: string; ValueName: "BetterNotepad.File"; ValueData: ""; Flags: uninsdeletevalue; Tasks: assoc
Root: HKA; Subkey: "Software\Classes\Applications\BetterNotepad.exe\SupportedTypes"; ValueType: string; ValueName: ".md"; ValueData: ""; Tasks: assoc
Root: HKA; Subkey: "Software\Classes\.log\OpenWithProgids"; ValueType: string; ValueName: "BetterNotepad.File"; ValueData: ""; Flags: uninsdeletevalue; Tasks: assoc
Root: HKA; Subkey: "Software\Classes\Applications\BetterNotepad.exe\SupportedTypes"; ValueType: string; ValueName: ".log"; ValueData: ""; Tasks: assoc
Root: HKA; Subkey: "Software\Classes\.ini\OpenWithProgids"; ValueType: string; ValueName: "BetterNotepad.File"; ValueData: ""; Flags: uninsdeletevalue; Tasks: assoc
Root: HKA; Subkey: "Software\Classes\Applications\BetterNotepad.exe\SupportedTypes"; ValueType: string; ValueName: ".ini"; ValueData: ""; Tasks: assoc
Root: HKA; Subkey: "Software\Classes\.json\OpenWithProgids"; ValueType: string; ValueName: "BetterNotepad.File"; ValueData: ""; Flags: uninsdeletevalue; Tasks: assoc
Root: HKA; Subkey: "Software\Classes\Applications\BetterNotepad.exe\SupportedTypes"; ValueType: string; ValueName: ".json"; ValueData: ""; Tasks: assoc
Root: HKA; Subkey: "Software\Classes\.cfg\OpenWithProgids"; ValueType: string; ValueName: "BetterNotepad.File"; ValueData: ""; Flags: uninsdeletevalue; Tasks: assoc
Root: HKA; Subkey: "Software\Classes\Applications\BetterNotepad.exe\SupportedTypes"; ValueType: string; ValueName: ".cfg"; ValueData: ""; Tasks: assoc
Root: HKA; Subkey: "Software\Classes\.js\OpenWithProgids"; ValueType: string; ValueName: "BetterNotepad.File"; ValueData: ""; Flags: uninsdeletevalue; Tasks: assoc
Root: HKA; Subkey: "Software\Classes\Applications\BetterNotepad.exe\SupportedTypes"; ValueType: string; ValueName: ".js"; ValueData: ""; Tasks: assoc
Root: HKA; Subkey: "Software\Classes\.html\OpenWithProgids"; ValueType: string; ValueName: "BetterNotepad.File"; ValueData: ""; Flags: uninsdeletevalue; Tasks: assoc
Root: HKA; Subkey: "Software\Classes\Applications\BetterNotepad.exe\SupportedTypes"; ValueType: string; ValueName: ".html"; ValueData: ""; Tasks: assoc
Root: HKA; Subkey: "Software\Classes\.htm\OpenWithProgids"; ValueType: string; ValueName: "BetterNotepad.File"; ValueData: ""; Flags: uninsdeletevalue; Tasks: assoc
Root: HKA; Subkey: "Software\Classes\Applications\BetterNotepad.exe\SupportedTypes"; ValueType: string; ValueName: ".htm"; ValueData: ""; Tasks: assoc
Root: HKA; Subkey: "Software\Classes\.css\OpenWithProgids"; ValueType: string; ValueName: "BetterNotepad.File"; ValueData: ""; Flags: uninsdeletevalue; Tasks: assoc
Root: HKA; Subkey: "Software\Classes\Applications\BetterNotepad.exe\SupportedTypes"; ValueType: string; ValueName: ".css"; ValueData: ""; Tasks: assoc
Root: HKA; Subkey: "Software\Classes\.vbs\OpenWithProgids"; ValueType: string; ValueName: "BetterNotepad.File"; ValueData: ""; Flags: uninsdeletevalue; Tasks: assoc
Root: HKA; Subkey: "Software\Classes\Applications\BetterNotepad.exe\SupportedTypes"; ValueType: string; ValueName: ".vbs"; ValueData: ""; Tasks: assoc
Root: HKA; Subkey: "Software\Classes\.reg\OpenWithProgids"; ValueType: string; ValueName: "BetterNotepad.File"; ValueData: ""; Flags: uninsdeletevalue; Tasks: assoc
Root: HKA; Subkey: "Software\Classes\Applications\BetterNotepad.exe\SupportedTypes"; ValueType: string; ValueName: ".reg"; ValueData: ""; Tasks: assoc
Root: HKA; Subkey: "Software\Classes\.xml\OpenWithProgids"; ValueType: string; ValueName: "BetterNotepad.File"; ValueData: ""; Flags: uninsdeletevalue; Tasks: assoc
Root: HKA; Subkey: "Software\Classes\Applications\BetterNotepad.exe\SupportedTypes"; ValueType: string; ValueName: ".xml"; ValueData: ""; Tasks: assoc
Root: HKA; Subkey: "Software\Classes\.sh\OpenWithProgids"; ValueType: string; ValueName: "BetterNotepad.File"; ValueData: ""; Flags: uninsdeletevalue; Tasks: assoc
Root: HKA; Subkey: "Software\Classes\Applications\BetterNotepad.exe\SupportedTypes"; ValueType: string; ValueName: ".sh"; ValueData: ""; Tasks: assoc
Root: HKA; Subkey: "Software\Classes\.ps1\OpenWithProgids"; ValueType: string; ValueName: "BetterNotepad.File"; ValueData: ""; Flags: uninsdeletevalue; Tasks: assoc
Root: HKA; Subkey: "Software\Classes\Applications\BetterNotepad.exe\SupportedTypes"; ValueType: string; ValueName: ".ps1"; ValueData: ""; Tasks: assoc
Root: HKA; Subkey: "Software\Classes\.bat\OpenWithProgids"; ValueType: string; ValueName: "BetterNotepad.File"; ValueData: ""; Flags: uninsdeletevalue; Tasks: assoc
Root: HKA; Subkey: "Software\Classes\Applications\BetterNotepad.exe\SupportedTypes"; ValueType: string; ValueName: ".bat"; ValueData: ""; Tasks: assoc
[Code]
function VCRedistNeeded: Boolean;
var
  Installed: Cardinal;
begin
  // Visual C++ 2015-2022 x64 runtime registers itself here (machine-wide).
  Result := not (RegQueryDWordValue(HKLM64, 'SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64',
                                    'Installed', Installed) and (Installed = 1));
end;