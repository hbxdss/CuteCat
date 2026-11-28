; CuteCat installer script (Inno Setup 6)
; Build: scripts\package.bat then
;   "%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe" scripts\installer.iss
; Keep this file pure ASCII (no Chinese .isl shipped with Inno Setup by default).

#define MyAppName "CuteCat"
#define MyAppVersion "1.0.1"
#define MyAppPublisher "hbxdss"
#define MyAppURL "https://github.com/hbxdss/CuteCat-One"
#define MyAppExeName "CuteCat.exe"

[Setup]
AppId={{8F0A5E2C-3B4D-4A6E-9C1F-7D2B5C8E4A31}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}/issues
AppUpdatesURL={#MyAppURL}/releases
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
OutputDir=..\out
OutputBaseFilename=CuteCat-Setup-{#MyAppVersion}
SetupIconFile=..\resources\icons\app.ico
UninstallDisplayIcon={app}\{#MyAppExeName}
LicenseFile=..\LICENSE
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
PrivilegesRequired=admin

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Additional shortcuts:"; Flags: checkedonce
Name: "autostart"; Description: "Start CuteCat when Windows starts"; GroupDescription: "Startup:"; Flags: unchecked

[Files]
Source: "..\dist\CuteCat\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\CuteCat"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\Uninstall CuteCat"; Filename: "{uninstallexe}"
Name: "{autodesktop}\CuteCat"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Registry]
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "CuteCat"; ValueData: """{app}\{#MyAppExeName}"""; Flags: uninsdeletevalue; Tasks: autostart

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "Launch CuteCat"; Flags: nowait postinstall skipifsilent

[UninstallRun]
Filename: "{cmd}"; Parameters: "/C taskkill /IM {#MyAppExeName} /F"; Flags: runhidden; RunOnceId: "KillCuteCat"