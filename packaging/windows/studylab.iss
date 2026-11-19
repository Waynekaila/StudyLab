; Installateur Windows de StudyLab (Inno Setup 6).
; Compilation (voir .github/workflows/build-windows.yml) :
;   iscc /DAppVersion=0.1.0 /DSourceDir=<dossier préparé par cmake --install + windeployqt> /O<sortie> studylab.iss

#ifndef AppVersion
  #define AppVersion "0.1.0"
#endif
#ifndef SourceDir
  #define SourceDir "..\..\build\stage"
#endif

[Setup]
AppId={{E409F671-0C1A-40D9-AD15-BD6A28ACD3AC}
AppName=StudyLab
AppVersion={#AppVersion}
AppVerName=StudyLab {#AppVersion}
AppPublisher=StudyLab
AppPublisherURL=https://github.com/Waynekaila/StudyLab
AppSupportURL=https://github.com/Waynekaila/StudyLab/issues
VersionInfoVersion={#AppVersion}
DefaultDirName={autopf}\StudyLab
DefaultGroupName=StudyLab
DisableProgramGroupPage=yes
; Installation pour l'utilisateur courant sans droits administrateur ; « pour tous » proposé dans l'assistant
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog commandline
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
OutputBaseFilename=StudyLab-{#AppVersion}-windows-x64-setup
SetupIconFile=studylab.ico
UninstallDisplayIcon={app}\studylab.exe
UninstallDisplayName=StudyLab
LicenseFile=..\..\LICENSE
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes
RestartApplications=no

[Languages]
Name: "french"; MessagesFile: "compiler:Languages\French.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\StudyLab"; Filename: "{app}\studylab.exe"
Name: "{autodesktop}\StudyLab"; Filename: "{app}\studylab.exe"; Tasks: desktopicon

[Registry]
; Liens « studylab://open?… » des exports Markdown (Obsidian…) : ouverts par StudyLab
Root: HKA; Subkey: "Software\Classes\studylab"; ValueType: string; ValueName: ""; ValueData: "URL:StudyLab"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\studylab"; ValueType: string; ValueName: "URL Protocol"; ValueData: ""
Root: HKA; Subkey: "Software\Classes\studylab\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\studylab.exe,0"
Root: HKA; Subkey: "Software\Classes\studylab\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\studylab.exe"" ""%1"""

[Run]
Filename: "{app}\studylab.exe"; Description: "{cm:LaunchProgram,StudyLab}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
; Les données (bibliothèque, base, sauvegardes) restent dans %LOCALAPPDATA%\StudyLab : rien n'est supprimé ici.
