#define Role GetEnv("SIDESCREEN_ROLE")
#define ReleaseVersion GetEnv("SIDESCREEN_VERSION")
#define SourceDir GetEnv("SIDESCREEN_STAGE")
#define OutputPath GetEnv("SIDESCREEN_DIST")
[Setup]
AppId=SideScreen.{#Role}
AppName=SideScreen {#Role}
AppVersion={#ReleaseVersion}
VersionInfoVersion=0.12.0.0
AppPublisher=EEkebin
AppPublisherURL=https://github.com/EEkebin/SideScreen
AppSupportURL=https://github.com/EEkebin/SideScreen/issues
DefaultDirName={localappdata}\Programs\SideScreen\{#Role}
DefaultGroupName=SideScreen
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.22000
OutputDir={#OutputPath}
OutputBaseFilename=SideScreen-{#ReleaseVersion}-{#Role}-Setup-win-x64
SetupIconFile=..\assets\branding\SideScreen.ico
UninstallDisplayIcon={app}\{#Role}.exe
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
DisableProgramGroupPage=yes
AppMutex=Local\SideScreen.{#Role}
CloseApplications=no
RestartApplications=no
InfoBeforeFile={#SourceDir}\README.txt
[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
[Tasks]
Name: desktopicon; Description: "Create a desktop shortcut"; GroupDescription: "Shortcuts:"
[Icons]
Name: "{group}\SideScreen {#Role}"; Filename: "{app}\{#Role}.exe"; WorkingDir: "{app}"
Name: "{autodesktop}\SideScreen {#Role}"; Filename: "{app}\{#Role}.exe"; WorkingDir: "{app}"; Tasks: desktopicon
