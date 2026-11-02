#define Role GetEnv("SIDESCREEN_ROLE")
#define ReleaseVersion GetEnv("SIDESCREEN_VERSION")
#define SourceDir GetEnv("SIDESCREEN_STAGE")
#define OutputPath GetEnv("SIDESCREEN_DIST")
[Setup]
AppId=SideScreen
AppName=SideScreen
AppVersion={#ReleaseVersion}
VersionInfoVersion={#GetEnv("SIDESCREEN_NUMERIC_VERSION")}
VersionInfoProductTextVersion={#ReleaseVersion}
AppPublisher=EEkebin
AppPublisherURL=https://github.com/EEkebin/SideScreen
AppSupportURL=https://github.com/EEkebin/SideScreen/issues
DefaultDirName={localappdata}\Programs\SideScreen
DefaultGroupName=SideScreen
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.22000
OutputDir={#OutputPath}
OutputBaseFilename=SideScreen-{#ReleaseVersion}-Setup-win-x64
SetupIconFile=..\assets\branding\SideScreen.ico
UninstallDisplayIcon={app}\{#Role}.exe
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
DisableProgramGroupPage=yes
AppMutex=Local\SideScreen.App
Uninstallable=yes
CreateUninstallRegKey=yes
CloseApplications=no
RestartApplications=no
InfoBeforeFile={#SourceDir}\README.txt
[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
[Tasks]
Name: desktopicon; Description: "Create a desktop shortcut"; GroupDescription: "Shortcuts:"
[Icons]
Name: "{group}\SideScreen"; Filename: "{app}\{#Role}.exe"; WorkingDir: "{app}"
Name: "{autodesktop}\SideScreen"; Filename: "{app}\{#Role}.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#Role}.exe"; WorkingDir: "{app}"; Description: "Launch SideScreen"; Flags: postinstall nowait skipifsilent

[Code]
var
  RemoveUserData: Boolean;

function StopRunningSideScreen: Boolean;
var
  Window, LastWindow: HWND;
  Attempt: Integer;
begin
  LastWindow := 0;
  for Attempt := 0 to 399 do begin
    Window := FindWindowByClassName('SideScreen.Tray');
    if (Window = 0) and
       not CheckForMutexes('Local\SideScreen.App,Local\SideScreen.DriverRuntime') then begin
      Result := True;
      Exit;
    end;
    if (Window <> 0) and (Window <> LastWindow) then begin
      { The existing tray Exit command stops both roles and the elevated helper.
        This also works with older builds whose ordinary Close hides to tray. }
      Log('Requesting SideScreen shutdown before changing installed files.');
      PostMessage(Window, $0111, 3, 0);
      LastWindow := Window;
    end;
    Sleep(100);
  end;
  Log('SideScreen shutdown did not complete; installed files will not be replaced.');
  Result := False;
end;

function InitializeSetup: Boolean;
begin
  Result := StopRunningSideScreen;
  if not Result then
    SuppressibleMsgBox('SideScreen could not finish shutting down. Close SideScreen and stop its driver, then run Setup again.', mbError, MB_OK, IDOK);
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  Result := '';
  if not StopRunningSideScreen then
    Result := 'SideScreen is still running. Close it and stop its driver before continuing.';
end;

function CredDelete(Target: String; CredType, Flags: LongWord): Boolean;
  external 'CredDeleteW@advapi32.dll stdcall uninstallonly';

function InitializeUninstall: Boolean;
var
  Dialog: TSetupForm;
  Choice: TNewCheckBox;
  Description: TNewStaticText;
  ConfirmButton, CancelButton: TNewButton;
begin
  RemoveUserData := ExpandConstant('{param:REMOVEUSERDATA|0}') = '1';
  Result := True;
  if UninstallSilent then begin
    Result := StopRunningSideScreen;
    Exit;
  end;
#if Ver < EncodeVer(6, 6, 0)
  Dialog := CreateCustomForm;
  Dialog.ClientWidth := ScaleX(460);
  Dialog.ClientHeight := ScaleY(180);
#else
  Dialog := CreateCustomForm(460, 180, True, True);
#endif
  try
    Dialog.Caption := 'Uninstall SideScreen';
    Description := TNewStaticText.Create(Dialog);
    Description.Parent := Dialog;
    Description.SetBounds(ScaleX(20), ScaleY(20), ScaleX(420), ScaleY(55));
    Description.AutoSize := False;
    Description.WordWrap := True;
    Description.Caption := 'Remove SideScreen from this PC. Your separately installed display driver will stay installed.';
    Choice := TNewCheckBox.Create(Dialog);
    Choice.Parent := Dialog;
    Choice.SetBounds(ScaleX(20), ScaleY(80), ScaleX(420), ScaleY(25));
    Choice.Caption := 'Remove saved settings on this PC';
    Choice.Checked := RemoveUserData;
    ConfirmButton := TNewButton.Create(Dialog);
    ConfirmButton.Parent := Dialog;
    ConfirmButton.SetBounds(ScaleX(230), ScaleY(130), ScaleX(100), ScaleY(28));
    ConfirmButton.Caption := 'Uninstall';
    ConfirmButton.ModalResult := mrOk;
    ConfirmButton.Default := True;
    CancelButton := TNewButton.Create(Dialog);
    CancelButton.Parent := Dialog;
    CancelButton.SetBounds(ScaleX(340), ScaleY(130), ScaleX(100), ScaleY(28));
    CancelButton.Caption := 'Cancel';
    CancelButton.ModalResult := mrCancel;
    CancelButton.Cancel := True;
    Result := Dialog.ShowModal = mrOk;
    RemoveUserData := Result and Choice.Checked;
  finally
    Dialog.Free;
  end;
  if Result then begin
    Result := StopRunningSideScreen;
    if not Result then
      SuppressibleMsgBox('SideScreen could not finish shutting down. Close it and stop its driver before uninstalling.', mbError, MB_OK, IDOK);
  end;
end;

procedure DeleteSettingsKey(Key: String);
begin
  if RegKeyExists(HKCU, Key) then
    if not RegDeleteKeyIncludingSubkeys(HKCU, Key) then
      RaiseException('Unable to remove SideScreen settings: ' + Key);
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  ErrorCode: LongWord;
begin
  { Run after the app mutex check, before files are removed. Cancel never resets data. }
  if (CurUninstallStep = usUninstall) and RemoveUserData then begin
    { This is the single shared pairing used by both UI roles. }
    if not CredDelete('DM/peers/dm-lan', 1, 0) then begin
      ErrorCode := DLLGetLastError;
      if ErrorCode <> 1168 then
        RaiseException('Unable to remove SideScreen pairing: ' + SysErrorMessage(ErrorCode));
    end;
    DeleteSettingsKey('Software\DM\Host');
    DeleteSettingsKey('Software\DM\Receiver');
    DeleteSettingsKey('Software\DM\Display');
    Log('Removed SideScreen pairing, server address and saved app/display settings.');
  end;
end;
