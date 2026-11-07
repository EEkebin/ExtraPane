#define Role GetEnv("EXTRAPANE_ROLE")
#define ReleaseVersion GetEnv("EXTRAPANE_VERSION")
#define SourceDir GetEnv("EXTRAPANE_STAGE")
#define OutputPath GetEnv("EXTRAPANE_DIST")
#define HasDriver FileExists(SourceDir + "\ExtraPane.DriverSetup.exe")
#define HasCamera FileExists(SourceDir + "\ExtraPane.CameraSetup.exe")
#define HasUsb FileExists(SourceDir + "\ExtraPane.UsbSetup.exe")
#define HasUsbHost FileExists(SourceDir + "\ExtraPane.UsbHostSetup.exe")
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
DefaultDirName={autopf}\ExtraPane
UsePreviousAppDir=no
DisableDirPage=yes
DefaultGroupName=ExtraPane
UsePreviousGroup=no
PrivilegesRequired=admin
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
#if HasUsb
Source: "{#SourceDir}\ExtraPane.UsbSetup.exe"; Flags: dontcopy
#endif
#if HasUsbHost
Source: "{#SourceDir}\ExtraPane.UsbHostSetup.exe"; Flags: dontcopy
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
Filename: "{app}\{#Role}.exe"; WorkingDir: "{app}"; Description: "Launch ExtraPane"; Flags: postinstall nowait skipifsilent runasoriginaluser

[Code]
var
  RemoveUserData: Boolean;
  RoleChoice, DriverConsent: TInputOptionWizardPage;
  DriverInstalled: Boolean;
  CameraInstalled, RemoveCamera: Boolean;
  MicrophoneInstalled: Boolean;
  UsbHostInstalled: Boolean;
  ComponentRestart: Boolean;
  UsbHostChoice: TNewCheckBox;

#include "shutdown.iss"

function InitializeSetup: Boolean;
begin
  Result := StopRunningExtraPane;
  if not Result then
    SuppressibleMsgBox('ExtraPane could not finish shutting down. Close ExtraPane and stop its driver, then run Setup again.', mbError, MB_OK, IDOK);
end;

procedure InitializeWizard;
var UsbHostDescription: TNewStaticText;
begin
  RoleChoice := CreateInputOptionPage(wpWelcome, 'How will you use this PC?',
    'All choices install the same ExtraPane app.',
    'Client only needs no virtual-display driver. Server + Client can also host displays and requires that driver. USB source sharing is a separate option below when its component is bundled.', True, False);
  RoleChoice.Add('Client only');
  RoleChoice.Add('Server + Client (requires administrator permission to install the driver)');
  RoleChoice.SelectedValueIndex := 0;
  if ExpandConstant('{param:SERVER|}') = '1' then RoleChoice.SelectedValueIndex := 1
  else if (ExpandConstant('{param:SERVER|}') = '') and not WizardSilent and
    (GetPreviousData('ServerInstall', '0') = '1') then RoleChoice.SelectedValueIndex := 1;
#if HasUsbHost
  { Independent of the receiving role: storage is shared FROM this PC. }
  RoleChoice.CheckListBox.Height := RoleChoice.CheckListBox.Height - ScaleY(70);
  UsbHostChoice := TNewCheckBox.Create(RoleChoice);
  UsbHostChoice.Parent := RoleChoice.Surface;
  UsbHostChoice.SetBounds(RoleChoice.CheckListBox.Left,
    RoleChoice.CheckListBox.Top + RoleChoice.CheckListBox.Height + ScaleY(6),
    RoleChoice.CheckListBox.Width, ScaleY(22));
  UsbHostChoice.Caption := 'Enable USB storage sharing from this PC (administrator permission)';
  UsbHostChoice.Checked := ExpandConstant('{param:USBHOST|0}') = '1';
  UsbHostDescription := TNewStaticText.Create(RoleChoice);
  UsbHostDescription.Parent := RoleChoice.Surface;
  UsbHostDescription.SetBounds(UsbHostChoice.Left,
    UsbHostChoice.Top + UsbHostChoice.Height + ScaleY(2),
    UsbHostChoice.Width, ScaleY(40));
  UsbHostDescription.AutoSize := False;
  UsbHostDescription.WordWrap := True;
  UsbHostDescription.Caption := 'Installs USB source support and signed drivers. Receiving USB devices uses the separate shared USB/IP component. No storage device is selected or shared during setup.';
#endif
  DriverConsent := CreateInputOptionPage(RoleChoice.ID, 'Required server driver',
    'Server setup includes a machine-wide virtual-display driver.',
    'The driver is development signed, not Microsoft release signed. Setup adds its public code-signing certificate to this PC''s Trusted Root and Trusted Publishers stores. The driver is managed by the ExtraPane uninstaller. Secure Boot, Memory Integrity and test-signing mode are not changed. Windows policy can still reject driver loading.', False, False);
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
  else Result := Result + 'Use: Client only (no virtual-display driver)';
#if HasCamera
  if RoleChoice.SelectedValueIndex = 1 then Result := Result + NewLine + 'Includes the machine-wide camera receiving component.';
#endif
#if HasUsb
  if RoleChoice.SelectedValueIndex = 1 then Result := Result + NewLine + 'Includes the shared USB/IP microphone receiving component. Existing compatible installations are preserved.';
#endif
#if HasUsbHost
  if UsbHostChoice.Checked then
    Result := Result + NewLine + 'USB source sharing: install the bundled native backend and signed driver package with administrator approval. Existing compatible shared drivers are preserved. No USB device is bound, no public server service or firewall rule is added, and the monitor driver is not started by setup.'
  else
    Result := Result + NewLine + 'USB source sharing: not requested; existing source components are left unchanged.';
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
  if CompareText(ExpandConstant('{app}'), ExpandConstant('{autopf}\ExtraPane')) <> 0 then begin
    Result := 'ExtraPane must be installed in Program Files\ExtraPane.'; Exit;
  end;
#if !HasUsbHost
  if ExpandConstant('{param:USBHOST|0}') = '1' then begin
    Result := 'USB source sharing was requested, but this installer does not contain its component.';
    Exit;
  end;
#endif
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
      Result := 'The required server driver was not installed (exit ' + IntToStr(Code) + '). See Program Files\ExtraPane\Components\Display\driver-setup.log. Retry or go Back and choose Client only.'
    else DriverInstalled := True;
#else
    Result := 'This setup does not contain the required driver installer. Server setup cannot continue.';
#endif
  end;
#if HasCamera
  if (Result = '') and (RoleChoice.SelectedValueIndex = 1) and not CameraInstalled then begin
    ExtractTemporaryFile('ExtraPane.CameraSetup.exe');
    if not ShellExec('runas', ExpandConstant('{tmp}\ExtraPane.CameraSetup.exe'),
      '--install', '', SW_HIDE, ewWaitUntilTerminated, Code) then
      Result := 'Camera component setup could not start or administrator permission was canceled.'
    else if (Code <> 0) and (Code <> 3010) then
      Result := 'The camera component could not be installed (exit ' + IntToStr(Code) + '). Existing foreign registrations are not overwritten. Retry or choose Client only.'
    else begin CameraInstalled := True; if Code = 3010 then NeedsRestart := True; end;
  end;
#endif
#if HasUsb
  if (Result = '') and (RoleChoice.SelectedValueIndex = 1) and not MicrophoneInstalled then begin
    ExtractTemporaryFile('ExtraPane.UsbSetup.exe');
    if not ShellExec('runas', ExpandConstant('{tmp}\ExtraPane.UsbSetup.exe'), '--install', '', SW_HIDE, ewWaitUntilTerminated, Code) then
      Result := 'Microphone setup could not start or administrator permission was canceled.'
    else if (Code <> 0) and (Code <> 3010) then
      Result := 'Microphone setup failed (exit ' + IntToStr(Code) + '). Existing USB/IP installations are preserved; an incompatible installation needs separate resolution.'
    else begin
      MicrophoneInstalled := True;
      if Code = 3010 then NeedsRestart := True;
    end;
  end;
#endif
#if HasUsbHost
  if (Result = '') and UsbHostChoice.Checked and not UsbHostInstalled then begin
    ExtractTemporaryFile('ExtraPane.UsbHostSetup.exe');
    if not ShellExec('runas', ExpandConstant('{tmp}\ExtraPane.UsbHostSetup.exe'),
      '--install', '', SW_HIDE, ewWaitUntilTerminated, Code) then
      Result := 'USB source setup could not start or administrator permission was canceled. Retry or clear the USB storage sharing option.'
    else if (Code <> 0) and (Code <> 3010) then
      Result := 'USB source setup failed (exit ' + IntToStr(Code) + '). Existing shared drivers are not replaced or removed. Eject and recover any pending USB transfers before retrying; incompatible components need separate resolution.'
    else begin UsbHostInstalled := True; if Code = 3010 then NeedsRestart := True; end;
  end;
#endif
end;

procedure CurStepChanged(CurStep: TSetupStep);
var LegacyLocation, LegacyName, LegacyKey, ExpectedLocation: String;
begin
  if CurStep <> ssDone then Exit;
  LegacyKey := 'Software\Microsoft\Windows\CurrentVersion\Uninstall\SideScreen_is1';
  ExpectedLocation := ExpandConstant('{localappdata}\Programs\ExtraPane');
  if RegQueryStringValue(HKCU, LegacyKey, 'InstallLocation', LegacyLocation) and
     RegQueryStringValue(HKCU, LegacyKey, 'DisplayName', LegacyName) and
     (CompareText(RemoveBackslashUnlessRoot(LegacyLocation), ExpectedLocation) = 0) and
     ((CompareText(LegacyName, 'ExtraPane') = 0) or (CompareText(LegacyName, 'SideScreen') = 0)) then begin
    { Old user-owned executable is never executed elevated. Preserve files and
      settings for rollback, retire only the positively identified duplicate entry. }
    if RegDeleteKeyIncludingSubkeys(HKCU, LegacyKey) then
      SaveStringToFile(ExpandConstant('{app}\migration.log'),
        'Legacy per-user application retained for rollback: ' + ExpectedLocation + #13#10, True)
    else Log('Could not retire the legacy per-user uninstall entry; files/settings were preserved.');
  end;
end;

function CredDelete(Target: String; CredType, Flags: LongWord): Boolean;
  external 'CredDeleteW@advapi32.dll stdcall uninstallonly';

function InitializeUninstall: Boolean;
begin
  { One uninstall removes ExtraPane-owned components and current-user settings.
    Inno Setup supplies the normal confirmation dialog; no per-component toggles. }
  RemoveUserData := True;
  RemoveCamera := True;
  Result := StopRunningExtraPane;
  if not Result then
    SuppressibleMsgBox('ExtraPane could not finish shutting down. Close it and stop its server before uninstalling.', mbError, MB_OK, IDOK);
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
  Component: Integer;
  Helper, ComponentFolder: String;
begin
  if CurUninstallStep = usUninstall then begin
    { USB first: a busy source or unrecovered device stops removal while every
      recovery helper is still present. Independently owned USB drivers remain. }
    for Component := 0 to 1 do begin
      if Component = 0 then begin Helper := 'ExtraPane.UsbHostSetup.exe'; ComponentFolder := 'UsbHost'; end
      else begin Helper := 'ExtraPane.UsbSetup.exe'; ComponentFolder := 'Microphone'; end;
      if FileExists(ExpandConstant('{app}\') + Helper) then begin
        if not Exec(ExpandConstant('{app}\') + Helper, '--remove', '', SW_HIDE, ewWaitUntilTerminated, CameraCode) then
          RaiseException('Component cleanup could not start: ' + Helper + '. ExtraPane files were retained.');
        if CameraCode = 3010 then ComponentRestart := True;
        if (CameraCode <> 0) and (CameraCode <> 3010) then
          RaiseException('Component cleanup failed: ' + Helper + ' (exit ' + IntToStr(CameraCode) + '). Finish USB eject/recovery and retry. ExtraPane files were retained.');
      end else if DirExists(ExpandConstant('{app}\Components\') + ComponentFolder) then
        RaiseException('Missing cleanup helper for ' + ComponentFolder + '. Repair ExtraPane before uninstalling.');
    end;
  end;
  if (CurUninstallStep = usUninstall) and DirExists(ExpandConstant('{app}\Components\Display')) and
     not FileExists(ExpandConstant('{app}\Components\Display\unins000.exe')) then
    RaiseException('The display component uninstaller is missing. Repair ExtraPane before uninstalling.');
  if (CurUninstallStep = usUninstall) and FileExists(ExpandConstant('{app}\Components\Display\unins000.exe')) then begin
    if not Exec(ExpandConstant('{app}\Components\Display\unins000.exe'),
      '/VERYSILENT /SUPPRESSMSGBOXES /NORESTART', '', SW_HIDE, ewWaitUntilTerminated, CameraCode) then
      RaiseException('The display component uninstaller could not start. ExtraPane files were retained.');
    if CameraCode <> 0 then
      RaiseException('Display component removal failed (exit ' + IntToStr(CameraCode) + '). ExtraPane files were retained for retry.');
  end;
  if (CurUninstallStep = usUninstall) and RemoveCamera then begin
    if not FileExists(ExpandConstant('{app}\ExtraPane.CameraSetup.exe')) then
      RaiseException('Camera setup helper is missing. The shared camera component was not removed.');
    if not ShellExec('runas', ExpandConstant('{app}\ExtraPane.CameraSetup.exe'), '--remove', '', SW_HIDE, ewWaitUntilTerminated, CameraCode) then
      RaiseException('Camera removal could not start or administrator permission was canceled.');
    if (CameraCode <> 0) and (CameraCode <> 3010) then
      RaiseException('Camera component removal failed (exit ' + IntToStr(CameraCode) + ').');
    if CameraCode = 3010 then ComponentRestart := True;
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
    DeleteSettingsKey('Software\ExtraPane\ExtraPane');
    Log('Removed ExtraPane pairing, server address and saved app/display settings.');
  end;
end;

function UninstallNeedRestart: Boolean;
begin
  Result := ComponentRestart;
end;
