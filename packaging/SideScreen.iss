#define Role GetEnv("SIDESCREEN_ROLE")
#define ReleaseVersion GetEnv("SIDESCREEN_VERSION")
#define SourceDir GetEnv("SIDESCREEN_STAGE")
#define OutputPath GetEnv("SIDESCREEN_DIST")
#define HasDriver FileExists(SourceDir + "\SideScreen.DriverSetup.exe")
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
#if HasDriver
Source: "{#SourceDir}\SideScreen.DriverSetup.exe"; Flags: dontcopy
#endif
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
  RoleChoice, DriverConsent: TInputOptionWizardPage;
  DriverInstalled: Boolean;

#include "shutdown.iss"

function InitializeSetup: Boolean;
begin
  Result := StopRunningSideScreen;
  if not Result then
    SuppressibleMsgBox('SideScreen could not finish shutting down. Close SideScreen and stop its driver, then run Setup again.', mbError, MB_OK, IDOK);
end;

procedure InitializeWizard;
begin
  RoleChoice := CreateInputOptionPage(wpWelcome, 'How will you use this PC?',
    'Both choices install the same SideScreen app.',
    'Client only receives another PC''s display and needs no virtual-display driver. Server + Client can also host displays and requires the driver on this PC.', True, False);
  RoleChoice.Add('Client only');
  RoleChoice.Add('Server + Client (requires administrator permission to install the driver)');
  if ExpandConstant('{param:SERVER|0}') = '1' then RoleChoice.SelectedValueIndex := 1
  else RoleChoice.SelectedValueIndex := 0;
  DriverConsent := CreateInputOptionPage(RoleChoice.ID, 'Required server driver',
    'Server setup includes a machine-wide virtual-display driver.',
    'The driver is development signed, not Microsoft release signed. Setup adds its public code-signing certificate to this PC''s Trusted Root and Trusted Publishers stores. The driver has its own Windows uninstall entry. Secure Boot, Memory Integrity and test-signing mode are not changed. Windows policy can still reject driver loading.', False, False);
  DriverConsent.Add('I agree to install the driver and trust its development certificate on this PC.');
  DriverConsent.Values[0] := ExpandConstant('{param:ACCEPTDEVELOPMENTCERT|0}') = '1';
end;

function ShouldSkipPage(PageID: Integer): Boolean;
begin
  Result := (PageID = DriverConsent.ID) and (RoleChoice.SelectedValueIndex = 0);
end;

function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;
  if (CurPageID = DriverConsent.ID) and not DriverConsent.Values[0] then begin
    SuppressibleMsgBox('The server requires this driver. Accept the certificate change, or go Back and choose Client only.', mbInformation, MB_OK, IDOK);
    Result := False;
  end;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
var Code: Integer;
begin
  Result := '';
  if not StopRunningSideScreen then begin
    Result := 'SideScreen is still running. Close it and stop its driver before continuing.';
    Exit;
  end;
  if (RoleChoice.SelectedValueIndex = 1) and not DriverInstalled then begin
    if not DriverConsent.Values[0] then begin
      Result := 'Server setup requires development certificate consent. Silent installs need /SERVER=1 /ACCEPTDEVELOPMENTCERT=1.';
      Exit;
    end;
#if HasDriver
    ExtractTemporaryFile('SideScreen.DriverSetup.exe');
    if not ShellExec('runas', ExpandConstant('{tmp}\SideScreen.DriverSetup.exe'),
      '/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /ACCEPTDEVELOPMENTCERT=1', '', SW_HIDE, ewWaitUntilTerminated, Code) then
      Result := 'The driver installer could not start or administrator permission was canceled. Server setup cannot continue.'
    else if Code <> 0 then
      Result := 'The required server driver was not installed (exit ' + IntToStr(Code) + '). See Program Files\SideScreen Driver\driver-setup.log. Retry or go Back and choose Client only.'
    else DriverInstalled := True;
#else
    Result := 'This setup does not contain the required driver installer. Server setup cannot continue.';
#endif
  end;
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
    Description.SetBounds(ScaleX(20), ScaleY(20), ScaleX(420), ScaleY(65));
    Description.AutoSize := False;
    Description.WordWrap := True;
    Description.Caption := 'Remove SideScreen from this PC. The machine-wide driver stays installed. Remove it separately using the SideScreen Virtual Display Driver entry in Windows Apps.';
    Choice := TNewCheckBox.Create(Dialog);
    Choice.Parent := Dialog;
    Choice.SetBounds(ScaleX(20), ScaleY(90), ScaleX(420), ScaleY(25));
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
