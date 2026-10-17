; The Windows installer for the packaged game (Inno Setup 6).
;
;   python Tools\unreal.py --engine F:\UE_5.8 installer
;
; packs Builds\Windows (what "unreal.py package" makes) into Builds\Installer\Ephemera-Setup-<version>.exe
; with its data beside it in .bin files. The game installs for the current user only, with no
; administrator prompt, into a folder it may write to: this build keeps its settings, logs and
; crash reports beside the program (Ephemera\Saved), where Tools\winsync.py logs looks for them.

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#define Root SourcePath + "..\.."

[Setup]
AppId={{6C1E0B52-7C0B-4E0E-9B62-3E1F0A5D7E41}
AppName=Ephemera
AppVersion={#AppVersion}
AppVerName=Ephemera {#AppVersion}
AppPublisher=Ephemera (a non-commercial fan project)
DefaultDirName={autopf}\Ephemera
DefaultGroupName=Ephemera
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir={#Root}\Builds\Installer
OutputBaseFilename=Ephemera-Setup-{#AppVersion}
SetupIconFile={#Root}\Build\Windows\Application.ico
UninstallDisplayIcon={app}\Ephemera.exe
WizardStyle=modern
; The game's data is already compressed and is stored as it is; only the programs are squeezed.
Compression=lzma2/fast
SolidCompression=no
; One file while it fits. More than about 2 GB cannot live inside one .exe: past that the build
; tool passes /DSpan and the data goes into .bin files that must stay beside the setup program.
#ifdef Span
DiskSpanning=yes
DiskSliceSize=max
#endif

[Tasks]
Name: "desktopicon"; Description: "Put a shortcut on the desktop"; GroupDescription: "Shortcuts:"

[Files]
Source: "{#Root}\Builds\Windows\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion; Excludes: "\Ephemera\Saved\*,\Ephemera\Content\Paks\*,Manifest_*.txt"
Source: "{#Root}\Builds\Windows\Ephemera\Content\Paks\*"; DestDir: "{app}\Ephemera\Content\Paks"; Flags: recursesubdirs ignoreversion nocompression

[Icons]
Name: "{autoprograms}\Ephemera"; Filename: "{app}\Ephemera.exe"; WorkingDir: "{app}"
Name: "{autodesktop}\Ephemera"; Filename: "{app}\Ephemera.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\Ephemera.exe"; Description: "Play Ephemera now"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
Type: filesandordirs; Name: "{app}\Ephemera\Saved"
