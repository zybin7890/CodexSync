#ifndef PayloadDirectory
  #error PayloadDirectory is required
#endif
#ifndef OutputDirectory
  #error OutputDirectory is required
#endif
#ifndef AppVersion
  #define AppVersion "0.2.0"
#endif
[Setup]
AppId={{776260E0-50BA-480A-AAC0-4BA383382A61}
AppName=CodexSync
AppVersion={#AppVersion}
AppPublisher=CodexSync contributors
AppPublisherURL=https://github.com/zybin7890/CodexSync
DefaultDirName={localappdata}\Programs\CodexSync
DefaultGroupName=CodexSync
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.22000
OutputDir={#OutputDirectory}
OutputBaseFilename=CodexSync-v{#AppVersion}-windows-x64-setup
SetupIconFile=..\ui\codexsync.ico
UninstallDisplayIcon={app}\CodexSync.exe
LicenseFile=..\LICENSE
Compression=lzma2/fast
SolidCompression=yes
WizardStyle=modern
CloseApplications=no
RestartApplications=no
DisableProgramGroupPage=yes
UninstallDisplayName=CodexSync 0.2 (installed edition)
[Tasks]
Name: desktopicon; Description: "Create desktop shortcut (installed edition)"; Flags: unchecked
[Files]
Source: "{#PayloadDirectory}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs; Excludes: "portable.flag,data\*"
[Icons]
Name: "{group}\CodexSync"; Filename: "{app}\CodexSync.exe"; WorkingDir: "{app}"
Name: "{userdesktop}\CodexSync (installed)"; Filename: "{app}\CodexSync.exe"; WorkingDir: "{app}"; Tasks: desktopicon
[Run]
Filename: "{app}\CodexSync.exe"; Description: "Launch CodexSync"; Flags: nowait postinstall skipifsilent
[Code]
function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  if FileExists(ExpandConstant('{app}\portable.flag')) then
    Result := 'This is a portable edition folder. Choose a separate installation folder.';
end;

// User data, encryption keys and authorization are deliberately never removed.
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var Command: String;
begin
  if CurUninstallStep = usUninstall then
    if RegQueryStringValue(HKCU, 'Software\Microsoft\Windows\CurrentVersion\Run', 'CodexSync', Command) then
      if Pos('"' + ExpandConstant('{app}\CodexSync.exe') + '"', Command) = 1 then
        RegDeleteValue(HKCU, 'Software\Microsoft\Windows\CurrentVersion\Run', 'CodexSync');
end;
