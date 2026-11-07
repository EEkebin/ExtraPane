#define Role GetEnv("EXTRAPANE_ROLE")
#define ReleaseVersion GetEnv("EXTRAPANE_VERSION")
#define SourceDir GetEnv("EXTRAPANE_STAGE")
#define OutputPath GetEnv("EXTRAPANE_DIST")
#define HasDriver FileExists(SourceDir + "\ExtraPane.DriverSetup.exe")
#define HasCamera FileExists(SourceDir + "\ExtraPane.CameraSetup.exe")
[Setup]
; Stable identity recognizes installations made under the previous name.
AppId=SideScreen
AppName=ExtraPane
AppVersion={#ReleaseVersion}
VersionInfoVersion={#GetEnv("EXTRAPANE_NUMERIC_VERSION")}
VersionInfoProductTextVersion={#ReleaseVersion}
AppPublisher=EEkebin
AppPublisherURL=https://github.com/EEkebin/ExtraPane
AppSupportURL=https://github.com/EEkebin/ExtraPane/issues
DefaultDirName={localappdata}\Programs\ExtraPane
DefaultGroupName=ExtraPane
UsePreviousGroup=no
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.22000
OutputDir={#OutputPath}
OutputBaseFilename=ExtraPane-{#ReleaseVersion}-Setup-win-x64
SetupIconFile=..\assets\branding\ExtraPane.ico
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
Source: "{#SourceDir}\ExtraPane.DriverSetup.exe"; Flags: dontcopy
#endif
#if HasCamera
Source: "{#SourceDir}\ExtraPane.CameraSetup.exe"; Flags: dontcopy
#endif
; Remove only known legacy app files after graceful shutdown; preserve data.
[InstallDelete]
Type: files; Name: "{app}\SideScreen.exe"
Type: files; Name: "{app}\SideScreen.Service.exe"
Type: files; Name: "{app}\SideScreen.Update.exe"
Type: files; Name: "{app}\SideScreen.DriverSetup.exe"
Type: files; Name: "{app}\SideScreen.ico"
Type: files; Name: "{app}\SideScreen.png"
Type: files; Name: "{app}\SideScreen.pri"
Type: files; Name: "{app}\SideScreen.winmd"
Type: files; Name: "{autodesktop}\SideScreen.lnk"
Type: files; Name: "{userprograms}\SideScreen\SideScreen.lnk"
[Tasks]
Name: desktopicon; Description: "Create a desktop shortcut"; GroupDescription: "Shortcuts:"
[Icons]
Name: "{group}\ExtraPane"; Filename: "{app}\{#Role}.exe"; WorkingDir: "{app}"
Name: "{autodesktop}\ExtraPane"; Filename: "{app}\{#Role}.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#Role}.exe"; WorkingDir: "{app}"; Description: "Launch ExtraPane"; Flags: postinstall nowait skipifsilent

[Code]
var
  RemoveUserData: Boolean;
  RoleChoice, DriverConsent: TInputOptionWizardPage;
  DriverInstalled: Boolean;
  CameraInstalled, RemoveCamera: Boolean;

#include "shutdown.iss"

function InitializeSetup: Boolean;
begin
  Result := StopRunningExtraPane;
  if not Result then
    SuppressibleMsgBox('ExtraPane could not finish shutting down. Close ExtraPane and stop its driver, then run Setup again.', mbError, MB_OK, IDOK);
end;

procedure InitializeWizard;
begin
  RoleChoice := CreateInputOptionPage(wpWelcome, 'How will you use this PC?',
    'Both choices install the same ExtraPane app.',
    'Client only receives another PC''s display and needs no virtual-display driver. Server + Client can also host displays and requires the driver on this PC.', True, False);
  RoleChoice.Add('Client only');
  RoleChoice.Add('Server + Client (requires administrator permission to install the driver)');
#if HasCamera
  RoleChoice.Add('Camera host + Client (administrator permission; no virtual-display driver)');
#endif
  RoleChoice.SelectedValueIndex := 0;
  if ExpandConstant('{param:SERVER|}') = '1' then RoleChoice.SelectedValueIndex := 1
  else if (ExpandConstant('{param:SERVER|}') = '') and not WizardSilent and
    (GetPreviousData('ServerInstall', '0') = '1') then RoleChoice.SelectedValueIndex := 1;
#if HasCamera
  if (ExpandConstant('{param:CAMERA|}') = '1') and (ExpandConstant('{param:SERVER|}') <> '1') then RoleChoice.SelectedValueIndex := 2
  else if (ExpandConstant('{param:SERVER|}') = '') and (ExpandConstant('{param:CAMERA|}') = '') and not WizardSilent and
    (RoleChoice.SelectedValueIndex = 0) and (GetPreviousData('CameraInstall', '0') = '1') then RoleChoice.SelectedValueIndex := 2;
#endif
  DriverConsent := CreateInputOptionPage(RoleChoice.ID, 'Required server driver',
    'Server setup includes a machine-wide virtual-display driver.',
    'The driver is development signed, not Microsoft release signed. Setup adds its public code-signing certificate to this PC''s Trusted Root and Trusted Publishers stores. The driver has its own Windows uninstall entry. Secure Boot, Memory Integrity and test-signing mode are not changed. Windows policy can still reject driver loading.', False, False);
  DriverConsent.Add('I agree to install the driver and trust its development certificate on this PC.');
  DriverConsent.Values[0] := ExpandConstant('{param:ACCEPTDEVELOPMENTCERT|0}') = '1';
end;

procedure RegisterPreviousData(PreviousDataKey: Integer);
begin
  if RoleChoice.SelectedValueIndex = 1 then SetPreviousData(PreviousDataKey, 'ServerInstall', '1')
  else SetPreviousData(PreviousDataKey, 'ServerInstall', '0');
  if CameraInstalled then SetPreviousData(PreviousDataKey, 'CameraInstall', '1')
  else SetPreviousData(PreviousDataKey, 'CameraInstall', '0');
end;

function UpdateReadyMemo(Space, NewLine, MemoUserInfoInfo, MemoDirInfo, MemoTypeInfo, MemoComponentsInfo, MemoGroupInfo, MemoTasksInfo: String): String;
begin
  Result := MemoDirInfo + NewLine + NewLine;
  if RoleChoice.SelectedValueIndex = 1 then Result := Result + 'Use: Server + Client (virtual-display driver required)'
  else if RoleChoice.SelectedValueIndex = 2 then Result := Result + 'Use: Camera host + Client (machine-wide camera component, no virtual-display driver)'
  else Result := Result + 'Use: Client only (no driver installation)';
#if HasCamera
  if RoleChoice.SelectedValueIndex = 1 then Result := Result + NewLine + 'Includes the machine-wide camera receiving component.';
#endif
  Result := Result + NewLine + NewLine + MemoGroupInfo + NewLine + MemoTasksInfo;
end;

function ShouldSkipPage(PageID: Integer): Boolean;
begin
  Result := (PageID = DriverConsent.ID) and (RoleChoice.SelectedValueIndex <> 1);
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
  if not StopRunningExtraPane then begin
    Result := 'ExtraPane is still running. Close it and stop its driver before continuing.';
    Exit;
  end;
  if (RoleChoice.SelectedValueIndex = 1) and not DriverInstalled then begin
    if not DriverConsent.Values[0] then begin
      Result := 'Server setup requires development certificate consent. Silent installs need /SERVER=1 /ACCEPTDEVELOPMENTCERT=1.';
      Exit;
    end;
#if HasDriver
    ExtractTemporaryFile('ExtraPane.DriverSetup.exe');
    if not ShellExec('runas', ExpandConstant('{tmp}\ExtraPane.DriverSetup.exe'),
      '/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /ACCEPTDEVELOPMENTCERT=1', '', SW_HIDE, ewWaitUntilTerminated, Code) then
      Result := 'The driver installer could not start or administrator permission was canceled. Server setup cannot continue.'
    else if Code <> 0 then
      Result := 'The required server driver was not installed (exit ' + IntToStr(Code) + '). See Program Files\SideScreen Driver\driver-setup.log. Retry or go Back and choose Client only.'
    else DriverInstalled := True;
#else
    Result := 'This setup does not contain the required driver installer. Server setup cannot continue.';
#endif
  end;
#if HasCamera
  if (Result = '') and (RoleChoice.SelectedValueIndex <> 0) and not CameraInstalled then begin
    ExtractTemporaryFile('ExtraPane.CameraSetup.exe');
    if not ShellExec('runas', ExpandConstant('{tmp}\ExtraPane.CameraSetup.exe'),
      '--install', '', SW_HIDE, ewWaitUntilTerminated, Code) then
      Result := 'Camera component setup could not start or administrator permission was canceled.'
    else if Code <> 0 then
      Result := 'The camera component could not be installed (exit ' + IntToStr(Code) + '). Existing foreign registrations are not overwritten. Retry or choose Client only.'
    else CameraInstalled := True;
  end;
#endif
end;

function CredDelete(Target: String; CredType, Flags: LongWord): Boolean;
  external 'CredDeleteW@advapi32.dll stdcall uninstallonly';

function InitializeUninstall: Boolean;
var
  Dialog: TSetupForm;
  Choice, CameraChoice: TNewCheckBox;
  Description: TNewStaticText;
  ConfirmButton, CancelButton: TNewButton;
begin
  RemoveUserData := ExpandConstant('{param:REMOVEUSERDATA|0}') = '1';
  RemoveCamera := ExpandConstant('{param:REMOVECAMERA|0}') = '1';
  Result := True;
  if UninstallSilent then begin
    Result := StopRunningExtraPane;
    Exit;
  end;
#if Ver < EncodeVer(6, 6, 0)
  Dialog := CreateCustomForm;
  Dialog.ClientWidth := ScaleX(460);
  Dialog.ClientHeight := ScaleY(220);
#else
  Dialog := CreateCustomForm(460, 220, True, True);
#endif
  try
    Dialog.Caption := 'Uninstall ExtraPane';
    Description := TNewStaticText.Create(Dialog);
    Description.Parent := Dialog;
    Description.SetBounds(ScaleX(20), ScaleY(20), ScaleX(420), ScaleY(65));
    Description.AutoSize := False;
    Description.WordWrap := True;
    Description.Caption := 'Remove ExtraPane from this PC. The machine-wide driver stays installed. Remove it separately using the ExtraPane Virtual Display Driver entry in Windows Apps.';
    Choice := TNewCheckBox.Create(Dialog);
    Choice.Parent := Dialog;
    Choice.SetBounds(ScaleX(20), ScaleY(90), ScaleX(420), ScaleY(25));
    Choice.Caption := 'Remove saved settings on this PC';
    Choice.Checked := RemoveUserData;
    CameraChoice := TNewCheckBox.Create(Dialog);
    CameraChoice.Parent := Dialog;
    CameraChoice.SetBounds(ScaleX(20), ScaleY(120), ScaleX(420), ScaleY(35));
    CameraChoice.Caption := 'Remove the machine-wide camera component (affects all users)';
    CameraChoice.Checked := RemoveCamera;
    CameraChoice.Enabled := FileExists(ExpandConstant('{app}\ExtraPane.CameraSetup.exe'));
    ConfirmButton := TNewButton.Create(Dialog);
    ConfirmButton.Parent := Dialog;
    ConfirmButton.SetBounds(ScaleX(230), ScaleY(170), ScaleX(100), ScaleY(28));
    ConfirmButton.Caption := 'Uninstall';
    ConfirmButton.ModalResult := mrOk;
    ConfirmButton.Default := True;
    CancelButton := TNewButton.Create(Dialog);
    CancelButton.Parent := Dialog;
    CancelButton.SetBounds(ScaleX(340), ScaleY(170), ScaleX(100), ScaleY(28));
    CancelButton.Caption := 'Cancel';
    CancelButton.ModalResult := mrCancel;
    CancelButton.Cancel := True;
    Result := Dialog.ShowModal = mrOk;
    RemoveUserData := Result and Choice.Checked;
    RemoveCamera := Result and CameraChoice.Enabled and CameraChoice.Checked;
  finally
    Dialog.Free;
  end;
  if Result then begin
    Result := StopRunningExtraPane;
    if not Result then
      SuppressibleMsgBox('ExtraPane could not finish shutting down. Close it and stop its driver before uninstalling.', mbError, MB_OK, IDOK);
  end;
end;

procedure DeleteSettingsKey(Key: String);
begin
  if RegKeyExists(HKCU, Key) then
    if not RegDeleteKeyIncludingSubkeys(HKCU, Key) then
      RaiseException('Unable to remove ExtraPane settings: ' + Key);
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  ErrorCode: LongWord;
  CameraCode: Integer;
begin
  if (CurUninstallStep = usUninstall) and RemoveCamera then begin
    if not FileExists(ExpandConstant('{app}\ExtraPane.CameraSetup.exe')) then
      RaiseException('Camera setup helper is missing. The shared camera component was not removed.');
    if not ShellExec('runas', ExpandConstant('{app}\ExtraPane.CameraSetup.exe'), '--remove', '', SW_HIDE, ewWaitUntilTerminated, CameraCode) then
      RaiseException('Camera removal could not start or administrator permission was canceled.');
    if (CameraCode <> 0) and (CameraCode <> 3010) then
      RaiseException('Camera component removal failed (exit ' + IntToStr(CameraCode) + ').');
    if CameraCode = 3010 then
      SuppressibleMsgBox('The camera registration was removed. Restart Windows to finish deleting camera files currently loaded by Windows.', mbInformation, MB_OK, IDOK);
  end;
  { Run after the app mutex check, before files are removed. Cancel never resets data. }
  if (CurUninstallStep = usUninstall) and RemoveUserData then begin
    { This is the single shared pairing used by both UI roles. }
    if not CredDelete('DM/peers/dm-lan', 1, 0) then begin
      ErrorCode := DLLGetLastError;
      if ErrorCode <> 1168 then
        RaiseException('Unable to remove ExtraPane pairing: ' + SysErrorMessage(ErrorCode));
    end;
    DeleteSettingsKey('Software\DM\Host');
    DeleteSettingsKey('Software\DM\Receiver');
    DeleteSettingsKey('Software\DM\Display');
    Log('Removed ExtraPane pairing, server address and saved app/display settings.');
  end;
end;
