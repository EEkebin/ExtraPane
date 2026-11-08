#define Role GetEnv("EXTRAPANE_ROLE")
#define ProductName "ExtraPane"
#define ReleaseVersion GetEnv("EXTRAPANE_VERSION")
#define SourceDir GetEnv("EXTRAPANE_STAGE")
#define OutputPath GetEnv("EXTRAPANE_DIST")
#define HasDriver FileExists(SourceDir + "\Components\Display\package\manifest.json") && FileExists(SourceDir + "\Components\Display\manage-driver.ps1")
#define HasCamera FileExists(SourceDir + "\ExtraPane.CameraSetup.exe")
#define HasUsb FileExists(SourceDir + "\ExtraPane.UsbSetup.exe")
#define HasUsbHost FileExists(SourceDir + "\ExtraPane.UsbHostSetup.exe")
#if !HasUsbHost
#error Mandatory USB source component is missing from the installer payload
#endif
[Setup]
; Stable identity recognizes installations made under the previous name.
AppId=SideScreen
AppName={#ProductName}
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
UninstallDisplayName={#ProductName}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
DisableProgramGroupPage=yes
AppMutex=Local\SideScreen.App
Uninstallable=not IsUninstallMaintenance
CreateUninstallRegKey=yes
CloseApplications=no
RestartApplications=no
InfoBeforeFile={#SourceDir}\README.txt
[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Excludes: "ExtraPane.DriverSetup.exe"; Flags: ignoreversion recursesubdirs createallsubdirs; Check: InstallApplication
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
Type: files; Name: "{app}\SideScreen.exe"; Check: InstallApplication
Type: files; Name: "{app}\SideScreen.Service.exe"; Check: InstallApplication
Type: files; Name: "{app}\SideScreen.Update.exe"; Check: InstallApplication
Type: files; Name: "{app}\SideScreen.DriverSetup.exe"; Check: InstallApplication
Type: files; Name: "{app}\SideScreen.ico"; Check: InstallApplication
Type: files; Name: "{app}\SideScreen.png"; Check: InstallApplication
Type: files; Name: "{app}\SideScreen.pri"; Check: InstallApplication
Type: files; Name: "{app}\SideScreen.winmd"; Check: InstallApplication
Type: files; Name: "{autodesktop}\SideScreen.lnk"; Check: InstallApplication
Type: files; Name: "{userprograms}\SideScreen\SideScreen.lnk"; Check: InstallApplication
[UninstallDelete]
; These managed journal/log rules previously belonged to the child display
; uninstaller. They run only after the main removal callback succeeds.
Type: filesandordirs; Name: "{app}\Components\Display\state"
Type: files; Name: "{app}\Components\Display\driver-setup.log"
; Helpers can create component directories outside the installer file manifest.
; Remove only empty directories after successful component cleanup.
Type: dirifempty; Name: "{app}\Components\Display"
Type: dirifempty; Name: "{app}\Components"
Type: dirifempty; Name: "{app}"
[Tasks]
Name: desktopicon; Description: "Create a desktop shortcut"; GroupDescription: "Shortcuts:"
[Icons]
Name: "{group}\ExtraPane"; Filename: "{app}\{#Role}.exe"; WorkingDir: "{app}"; Check: InstallApplication
Name: "{autodesktop}\ExtraPane"; Filename: "{app}\{#Role}.exe"; WorkingDir: "{app}"; Tasks: desktopicon; Check: InstallApplication

[Run]
Filename: "{app}\{#Role}.exe"; WorkingDir: "{app}"; Description: "Launch ExtraPane"; Flags: postinstall nowait skipifsilent runasoriginaluser; Check: CanLaunchApplication

[Code]
#include "maintenance-version.iss"
var
  RemoveUserData: Boolean;
  MaintenanceChoice: TInputOptionWizardPage;
  ExistingInstall, UpdateAvailable, MaintenanceRemoved: Boolean;
  InstalledVersion, InstalledLocation, InstalledUninstaller: String;
  MaintenanceMode: String;
  MaintenanceStatusFile, UninstallStatusFile: String;
  MaintenancePendingBefore, MaintenancePendingAfter: String;
  MaintenanceNeedsRestart: Boolean;
  RoleChoice: TInputOptionWizardPage;
  DriverConsent: TOutputMsgWizardPage;
  DriverConsentAccepted: Boolean;
  StandardNextLeft, StandardNextWidth, StandardBackLeft: Integer;
  DriverInstalled, DriverInstallFailed: Boolean;
  CameraInstalled, RemoveCamera: Boolean;
  MicrophoneInstalled: Boolean;
  UsbHostInstalled: Boolean;
  ComponentRestart: Boolean;
  ComponentError: String;

#include "shutdown.iss"

const AppUninstallKey = 'Software\Microsoft\Windows\CurrentVersion\Uninstall\SideScreen_is1';

function IsUninstallMaintenance: Boolean;
begin Result := ExistingInstall and (MaintenanceMode = 'uninstall'); end;

function InstallApplication: Boolean;
begin Result := not IsUninstallMaintenance; end;

function DetectInstalled: Boolean;
var Name, Command: String;
begin
  Result := RegKeyExists(HKLM64, AppUninstallKey);
  if not Result then Exit;
  RegQueryStringValue(HKLM64, AppUninstallKey, 'DisplayVersion', InstalledVersion);
  if not RegQueryStringValue(HKLM64, AppUninstallKey, 'InstallLocation', InstalledLocation) then Exit;
  InstalledLocation := RemoveBackslashUnlessRoot(InstalledLocation);
  { Never execute a registry-selected arbitrary/per-user program elevated. }
  if (CompareText(InstalledLocation, ExpandConstant('{autopf}\ExtraPane')) <> 0) and
     (CompareText(InstalledLocation, ExpandConstant('{autopf}\SideScreen')) <> 0) then Exit;
  if not RegQueryStringValue(HKLM64, AppUninstallKey, 'DisplayName', Name) then Exit;
  if (Pos('ExtraPane', Name) <> 1) and (Pos('SideScreen', Name) <> 1) then Exit;
  if not RegQueryStringValue(HKLM64, AppUninstallKey, 'UninstallString', Command) then Exit;
  if CompareText(Command, '"' + InstalledLocation + '\unins000.exe"') <> 0 then Exit;
  if FileExists(InstalledLocation + '\unins000.exe') then InstalledUninstaller := InstalledLocation + '\unins000.exe';
end;

procedure MaintenanceChanged(Sender: TObject);
begin
  case MaintenanceChoice.SelectedValueIndex of
    0: MaintenanceMode := 'update';
    1: MaintenanceMode := 'restore';
    2: MaintenanceMode := 'uninstall';
  end;
end;

function InitializeSetup: Boolean;
var Requested: String;
begin
  ExistingInstall := DetectInstalled;
  UpdateAvailable := ExistingInstall and NewerRelease('{#ReleaseVersion}', InstalledVersion);
  Requested := LowerCase(ExpandConstant('{param:MAINTENANCE|}'));
  MaintenanceMode := Requested;
  if MaintenanceMode = '' then begin
    if UpdateAvailable or WizardSilent then MaintenanceMode := 'update'
    else MaintenanceMode := 'restore';
  end;
  Result := (MaintenanceMode = 'update') or (MaintenanceMode = 'restore') or (MaintenanceMode = 'uninstall');
  if not Result then begin SuppressibleMsgBox('Unknown maintenance action. Use update, restore or uninstall.', mbError, MB_OK, IDOK); Exit; end;
  if not ExistingInstall and (Requested <> '') and (Requested <> 'update') then begin
    Result := False; SuppressibleMsgBox('No installed ExtraPane was found for this maintenance action.', mbError, MB_OK, IDOK); Exit;
  end;
  if ExistingInstall and WizardSilent and (MaintenanceMode = 'update') and not UpdateAvailable then begin
    Result := False; SuppressibleMsgBox('Update requires an installer newer than the installed ExtraPane. Use /MAINTENANCE=restore only for an explicit clean reinstall.', mbError, MB_OK, IDOK); Exit;
  end;
  Result := StopRunningExtraPane;
  if not Result then
    SuppressibleMsgBox('ExtraPane could not finish shutting down. Close ExtraPane and stop its driver, then run Setup again.', mbError, MB_OK, IDOK);
end;

procedure InitializeWizard;
begin
  MaintenanceChoice := CreateInputOptionPage(wpWelcome, 'ExtraPane is already installed',
    'Installed: ' + InstalledVersion + '    Installer: {#ReleaseVersion}',
    'Update keeps your settings. Restore removes ExtraPane, its managed components and settings, then reinstalls this version. Uninstall removes them without reinstalling. Independently installed shared drivers are preserved.', True, False);
  MaintenanceChoice.Add('Update to {#ReleaseVersion}');
  MaintenanceChoice.Add('Restore (clean reinstall)');
  MaintenanceChoice.Add('Uninstall ExtraPane');
  MaintenanceChoice.CheckListBox.ItemEnabled[0] := UpdateAvailable;
  MaintenanceChoice.CheckListBox.ItemEnabled[1] := InstalledUninstaller <> '';
  MaintenanceChoice.CheckListBox.ItemEnabled[2] := InstalledUninstaller <> '';
  MaintenanceChoice.SelectedValueIndex := 0;
  if MaintenanceMode = 'restore' then MaintenanceChoice.SelectedValueIndex := 1;
  if MaintenanceMode = 'uninstall' then MaintenanceChoice.SelectedValueIndex := 2;
  MaintenanceChoice.CheckListBox.OnClickCheck := @MaintenanceChanged;
  RoleChoice := CreateInputOptionPage(MaintenanceChoice.ID, 'How will you use this PC?',
    'All choices install the same ExtraPane app.',
    'Client only needs no virtual-display driver. Server + Client can also host displays and requires that driver.', True, False);
  RoleChoice.Add('Client only');
  RoleChoice.Add('Server + Client (requires administrator permission to install the driver)');
  RoleChoice.SelectedValueIndex := 0;
  if ExpandConstant('{param:SERVER|}') = '1' then RoleChoice.SelectedValueIndex := 1
  else if (ExpandConstant('{param:SERVER|}') = '') and
    (GetPreviousData('ServerInstall', '0') = '1') then RoleChoice.SelectedValueIndex := 1;
  DriverConsent := CreateOutputMsgPage(RoleChoice.ID, 'Required server driver',
    'Server setup includes a machine-wide virtual-display driver.',
    'The driver is development signed, not Microsoft release signed. Setup adds its public code-signing certificate to this PC''s Trusted Root and Trusted Publishers stores. The driver is managed by the ExtraPane uninstaller. Secure Boot, Memory Integrity and test-signing mode are not changed. Windows policy can still reject driver loading.' + #13#10 + #13#10 + 'Select I agree - Next to install the driver and trust its development certificate. Otherwise, go Back and choose Client only.');
  StandardBackLeft := WizardForm.BackButton.Left;
  StandardNextLeft := WizardForm.NextButton.Left;
  StandardNextWidth := WizardForm.NextButton.Width;
  DriverConsentAccepted := ExpandConstant('{param:ACCEPTDEVELOPMENTCERT|0}') = '1';
end;

procedure RegisterPreviousData(PreviousDataKey: Integer);
begin
  if IsUninstallMaintenance then Exit;
  if RoleChoice.SelectedValueIndex = 1 then SetPreviousData(PreviousDataKey, 'ServerInstall', '1')
  else SetPreviousData(PreviousDataKey, 'ServerInstall', '0');
  if CameraInstalled then SetPreviousData(PreviousDataKey, 'CameraInstall', '1')
  else SetPreviousData(PreviousDataKey, 'CameraInstall', '0');
end;

function UpdateReadyMemo(Space, NewLine, MemoUserInfoInfo, MemoDirInfo, MemoTypeInfo, MemoComponentsInfo, MemoGroupInfo, MemoTasksInfo: String): String;
begin
  if IsUninstallMaintenance then begin Result := 'Remove ExtraPane, its managed components and current-user settings.'; Exit; end;
  Result := MemoDirInfo + NewLine + NewLine;
  if ExistingInstall and (MaintenanceMode = 'restore') then Result := 'Restore: completely remove the existing installation and settings, then reinstall.' + NewLine + NewLine + Result;
  if RoleChoice.SelectedValueIndex = 1 then Result := Result + 'Use: Server + Client (virtual-display driver required)'
  else Result := Result + 'Use: Client only (no virtual-display driver)';
#if HasCamera
  if RoleChoice.SelectedValueIndex = 1 then Result := Result + NewLine + 'Includes the machine-wide camera receiving component.';
#endif
#if HasUsb
  if RoleChoice.SelectedValueIndex = 1 then Result := Result + NewLine + 'Includes the microphone receiving component. Existing compatible installations are preserved.';
#endif

  Result := Result + NewLine + NewLine + MemoGroupInfo + NewLine + MemoTasksInfo;
end;

function ShouldSkipPage(PageID: Integer): Boolean;
begin
  Result := ((PageID = MaintenanceChoice.ID) and not ExistingInstall) or
    ((PageID = DriverConsent.ID) and (RoleChoice.SelectedValueIndex <> 1)) or
    (IsUninstallMaintenance and ((PageID = RoleChoice.ID) or (PageID = DriverConsent.ID) or (PageID = wpInfoBefore) or (PageID = wpSelectTasks)));
end;

function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;
  if (CurPageID = MaintenanceChoice.ID) and ExistingInstall then begin
    MaintenanceChanged(nil);
    if (MaintenanceMode = 'update') and not UpdateAvailable then begin Result := False; Exit; end;
    if (MaintenanceMode <> 'update') and (InstalledUninstaller = '') then begin Result := False; SuppressibleMsgBox('The registered uninstaller is missing or has an unexpected location. Update with a newer installer to repair it first.', mbError, MB_OK, IDOK); Exit; end;
  end;
  if (CurPageID = DriverConsent.ID) and not WizardSilent then
    DriverConsentAccepted := True;
end;

procedure CaptureComponentOutput(const S: String; const Error, FirstLine: Boolean);
begin
  Log(S);
  if Trim(S) <> '' then ComponentError := Copy(Trim(S), 1, 1024);
end;

function RunComponent(const Filename, Params, WorkingDir: String; var Code: Integer): Boolean;
begin
  Code := -1;
  ComponentError := '';
  Result := ExecAndLogOutput(Filename, Params, WorkingDir, SW_HIDE,
    ewWaitUntilTerminated, Code, @CaptureComponentOutput);
end;


function RemoveExistingInstallation(var NeedsRestart: Boolean): String;
var Code: Integer; Status: AnsiString;
begin
  Result := '';
  if not MaintenanceRemoved then begin
    if (InstalledUninstaller = '') or not FileExists(InstalledUninstaller) then begin
      Result := 'The installed uninstaller is missing or has an unexpected location. Update with a newer installer to repair cleanup first.'; Exit;
    end;
    { The previous uninstaller owns its file manifest and all guarded component
      cleanup. Do not bypass its checks or replace its ownership records. }
    MaintenancePendingBefore := '';
    RegQueryMultiStringValue(HKLM64, 'SYSTEM\CurrentControlSet\Control\Session Manager', 'PendingFileRenameOperations', MaintenancePendingBefore);
    MaintenanceStatusFile := ExpandConstant('{tmp}\maintenance-status.txt');
    if not SaveStringToFile(MaintenanceStatusFile, '', False) then begin Result := 'Cannot prepare the uninstall completion record.'; Exit; end;
    if not Exec(InstalledUninstaller, '/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /LOG /MAINTENANCESTATUS="' + MaintenanceStatusFile + '"',
      InstalledLocation, SW_HIDE, ewWaitUntilTerminated, Code) then begin
      Result := 'ExtraPane removal could not start. Nothing will be reinstalled.'; Exit;
    end;
    if (Code <> 0) or RegKeyExists(HKLM64, AppUninstallKey) then begin
      Result := 'ExtraPane removal did not complete (exit ' + IntToStr(Code) + '). Existing cleanup safeguards were preserved. Check the uninstall log in your Temp folder; no reinstall was started.'; Exit;
    end;
    MaintenanceRemoved := True;
  end;
  MaintenancePendingAfter := '';
  RegQueryMultiStringValue(HKLM64, 'SYSTEM\CurrentControlSet\Control\Session Manager',
    'PendingFileRenameOperations', MaintenancePendingAfter);
  Status := '';
  LoadStringFromFile(MaintenanceStatusFile, Status);
  { Older uninstallers do not report a helper's restart requirement. Their
    successful removal is real, but only a restart makes restoration safe. }
  MaintenanceNeedsRestart := (Status <> 'complete;restart=0') or
    (MaintenancePendingBefore <> MaintenancePendingAfter) or
    (Pos(LowerCase(InstalledLocation + '\'), LowerCase(MaintenancePendingAfter)) > 0);
  if MaintenanceNeedsRestart then begin
    ComponentRestart := True; NeedsRestart := True;
    if MaintenanceMode = 'restore' then begin
      if (Status <> 'complete;restart=0') and (Status <> 'complete;restart=1') then
        Result := 'ExtraPane was removed. Its older uninstaller cannot report whether a component needs a restart. Restart Windows, then run this installer to complete the clean installation.'
      else Result := 'ExtraPane was removed, but Windows must restart to release its components. Restart Windows, then run this installer to complete the clean installation.';
    end;
  end;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
var Code: Integer;
begin
  Result := '';
  if CompareText(ExpandConstant('{app}'), ExpandConstant('{autopf}\ExtraPane')) <> 0 then begin
    Result := 'ExtraPane must be installed in Program Files\ExtraPane.'; Exit;
  end;

  if not StopRunningExtraPane then begin
    Result := 'ExtraPane is still running. Close it and stop its driver before continuing.';
    Exit;
  end;
  if ExistingInstall and (MaintenanceMode = 'update') and not UpdateAvailable then begin Result := 'This installer is not newer than the installed version. Select Restore for a clean reinstall.'; Exit; end;
  if IsUninstallMaintenance then begin Result := RemoveExistingInstallation(NeedsRestart); Exit; end;
  if (RoleChoice.SelectedValueIndex = 1) and not DriverInstalled then begin
    if not DriverConsentAccepted then begin
      Result := 'Server setup requires development certificate consent. Silent installs need /SERVER=1 /ACCEPTDEVELOPMENTCERT=1.';
      Exit;
    end;
#if !HasDriver
    Result := 'This setup does not contain the required display-driver package. Server setup cannot continue.';
#endif
  end;
  if Result <> '' then Exit;
  if ExistingInstall and (MaintenanceMode = 'restore') then begin
    Result := RemoveExistingInstallation(NeedsRestart);
    if Result <> '' then Exit;
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
  if (Result = '') and not UsbHostInstalled then begin
    ExtractTemporaryFile('ExtraPane.UsbHostSetup.exe');
    if not RunComponent(ExpandConstant('{tmp}\ExtraPane.UsbHostSetup.exe'), '--install', '', Code) then
      Result := 'USB source setup could not start or administrator permission was canceled. Please retry setup.'
    else if (Code <> 0) and (Code <> 3010) then
      Result := 'USB source support could not be prepared (exit ' + IntToStr(Code) + '). ' + ComponentError + #13#10 + 'An exact compatible installed backend is reused without requiring a missing USB card. Existing bindings and recovery records are preserved; a changed or incomplete backend needs repair.'
    else begin UsbHostInstalled := True; if Code = 3010 then NeedsRestart := True; end;
  end;
#endif
end;

function RunDisplayManager(Action: String; var Code: Integer): Boolean;
begin
  Result := RunComponent(ExpandConstant('{sys}\WindowsPowerShell\v1.0\powershell.exe'),
    '-NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "' +
    ExpandConstant('{app}\Components\Display\manage-driver.ps1') + '" -Action ' + Action,
    ExpandConstant('{app}\Components\Display'), Code);
  Result := Result and (Code = 0);
  if not Result then Log('Display manager ' + Action + ' failed with code ' + IntToStr(Code));
end;

function CanLaunchApplication: Boolean;
begin
  Result := InstallApplication and not DriverInstallFailed;
end;

function GetCustomSetupExitCode: Integer;
begin
  Result := 0;
  if DriverInstallFailed then Result := 1;
end;

procedure CurPageChanged(CurPageID: Integer);
begin
  WizardForm.BackButton.Left := StandardBackLeft;
  WizardForm.NextButton.Width := StandardNextWidth;
  WizardForm.NextButton.Left := StandardNextLeft;
  if (CurPageID = wpReady) and IsUninstallMaintenance then begin
    WizardForm.NextButton.Caption := 'Uninstall';
  end else if CurPageID = DriverConsent.ID then begin
    WizardForm.NextButton.Width := ScaleX(140);
    WizardForm.NextButton.Left := StandardNextLeft + StandardNextWidth - WizardForm.NextButton.Width;
    WizardForm.BackButton.Left := StandardBackLeft - (WizardForm.NextButton.Width - StandardNextWidth);
    WizardForm.NextButton.Caption := 'I agree - Next >';
  end else if CurPageID = wpReady then
    WizardForm.NextButton.Caption := SetupMessage(msgButtonInstall)
  else if CurPageID = wpFinished then
    WizardForm.NextButton.Caption := SetupMessage(msgButtonFinish)
  else
    WizardForm.NextButton.Caption := SetupMessage(msgButtonNext);
  if (CurPageID = wpInstalling) and IsUninstallMaintenance then begin
    WizardForm.PageNameLabel.Caption := 'Uninstalling ExtraPane';
    WizardForm.PageDescriptionLabel.Caption := 'Removing ExtraPane and its managed components.';
  end;
  if (CurPageID = wpFinished) and IsUninstallMaintenance then begin
    WizardForm.FinishedHeadingLabel.Caption := 'ExtraPane removed';
    WizardForm.FinishedLabel.Caption := 'ExtraPane, its managed components and current-user settings have been removed. Independently installed shared components were preserved.';
    if ComponentRestart then WizardForm.FinishedLabel.Caption := WizardForm.FinishedLabel.Caption + #13#10#13#10 + 'Restart Windows to finish removing loaded files.';
  end;
  if (CurPageID = wpFinished) and DriverInstallFailed then begin
    WizardForm.FinishedHeadingLabel.Caption := 'Server driver installation failed';
    WizardForm.FinishedLabel.Caption := 'ExtraPane remains installed for repair or removal, but the server driver is not ready. Check Program Files\ExtraPane\Components\Display\driver-setup.log and run this installer again to repair it. No boot-security settings were changed.';
  end;
end;

procedure RetireManagedDriverEntry;
var Key, Location, Name, UninstallCommand, Expected: String;
begin
  Key := 'Software\Microsoft\Windows\CurrentVersion\Uninstall\SideScreen.VirtualDisplayDriver_is1';
  if not RegQueryStringValue(HKLM64, Key, 'InstallLocation', Location) then Exit;
  Location := RemoveBackslashUnlessRoot(Location);
  Expected := ExpandConstant('{app}\Components\Display');
  if (CompareText(Location, Expected) <> 0) and
     (CompareText(Location, ExpandConstant('{autopf}\SideScreen Driver')) <> 0) then Exit;
  if (CompareText(Location, Expected) <> 0) and not DriverInstalled then Exit;
  if not RegQueryStringValue(HKLM64, Key, 'DisplayName', Name) then Exit;
  if (Pos('ExtraPane Virtual Display Driver', Name) <> 1) and
     (Pos('SideScreen Virtual Display Driver', Name) <> 1) then Exit;
  if not RegQueryStringValue(HKLM64, Key, 'UninstallString', UninstallCommand) then Exit;
  if CompareText(UninstallCommand, '"' + Location + '\unins000.exe"') <> 0 then Exit;
  { Install has verified/migrated the ownership journal. Never execute the old
    uninstaller: it would remove the driver that the main app now owns. }
  if not RegDeleteKeyIncludingSubkeys(HKLM64, Key) then
    Log('Could not retire verified duplicate display-driver uninstall entry.');
end;

procedure CurStepChanged(CurStep: TSetupStep);
var LegacyLocation, LegacyName, LegacyKey, ExpectedLocation: String;
    Code: Integer;
begin
  if IsUninstallMaintenance then begin
    { Inno may create the default directory even with no selected files. Only
      remove it when empty; never recurse through user or foreign files. }
    if CurStep = ssDone then RemoveDir(ExpandConstant('{app}'));
    Exit;
  end;
  if (CurStep = ssPostInstall) and (RoleChoice.SelectedValueIndex = 1) then begin
    { Files and the main uninstall record exist before any driver changes. A
      failed driver install leaves this app available for repair/uninstall. }
    DriverInstalled := RunDisplayManager('Install', Code);
    DriverInstallFailed := not DriverInstalled;
    if DriverInstallFailed then
      SuppressibleMsgBox('The server driver could not be installed (exit ' + IntToStr(Code) + '). ' + ComponentError + #13#10 + 'ExtraPane is retained for repair or uninstall. See Program Files\ExtraPane\Components\Display\driver-setup.log.', mbError, MB_OK, IDOK);
  end;
  if (CurStep <> ssDone) or DriverInstallFailed then Exit;
  if DriverInstalled then RetireManagedDriverEntry
  else if DirExists(ExpandConstant('{app}\Components\Display\state')) and
          RunDisplayManager('ValidateOwnership', Code) then RetireManagedDriverEntry;
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
  UninstallStatusFile := ExpandConstant('{param:MAINTENANCESTATUS|}');
  Result := StopRunningExtraPane;
  if not Result then
    SuppressibleMsgBox('ExtraPane could not finish shutting down. Close it and stop its server before uninstalling.', mbError, MB_OK, IDOK);
end;

procedure StopUninstall(const Detail: String);
begin
  Log('ExtraPane uninstall stopped: ' + Detail);
  SuppressibleMsgBox('ExtraPane could not finish uninstalling.' + #13#10#13#10 +
    Detail + #13#10#13#10 +
    'Close any active ExtraPane USB transfer and retry. You can install the latest ExtraPane over this installation to repair its cleanup tools without reconnecting a missing card.',
    mbError, MB_OK, IDOK);
  { Abort is a documented silent exception from usUninstall: retain the app and
    its cleanup helpers without showing a Pascal runtime-error dialog. }
  Abort;
end;

procedure DeleteSettingsKey(Key: String);
begin
  if RegKeyExists(HKCU, Key) then
    if not RegDeleteKeyIncludingSubkeys(HKCU, Key) then
      StopUninstall('Unable to remove ExtraPane settings: ' + Key);
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  ErrorCode: LongWord;
  CameraCode: Integer;
  Component: Integer;
  Helper, ComponentFolder: String;
begin
  if (CurUninstallStep = usDone) and (UninstallStatusFile <> '') then begin
    if ComponentRestart then SaveStringToFile(UninstallStatusFile, 'complete;restart=1', False)
    else SaveStringToFile(UninstallStatusFile, 'complete;restart=0', False);
  end;
  if CurUninstallStep = usUninstall then begin
    { Reconcile safe local records and retire verified inactive absent-device
      ownership. The helper archives interrupted records; it does not pretend a
      clean eject or write to missing/replacement media. Active/foreign state blocks. }
    if FileExists(ExpandConstant('{app}\ExtraPane.UsbRecovery.exe')) then begin
      if not RunComponent(ExpandConstant('{app}\ExtraPane.UsbRecovery.exe'), '--prepare-uninstall', '', CameraCode) then
        StopUninstall('USB recovery check could not start. ExtraPane files were retained.');
      if CameraCode <> 0 then
        StopUninstall('USB cleanup could not establish safe inactive ownership: ' + ComponentError + #13#10 + 'ExtraPane files were retained.');
    end;
    { Verify source removal before deleting any component payload. }
    if FileExists(ExpandConstant('{app}\ExtraPane.UsbHostSetup.exe')) then begin
      if not RunComponent(ExpandConstant('{app}\ExtraPane.UsbHostSetup.exe'), '--check-removal', '', CameraCode) then
        StopUninstall('USB source removal check could not start. ExtraPane files were retained.');
      if CameraCode <> 0 then
        StopUninstall('USB source support is still in use or its ownership changed: ' + ComponentError);
    end;
    { USB first: validated inactive stale ownership was retired above. Unrelated
      shared drivers and foreign registrations are always preserved. }
    for Component := 0 to 1 do begin
      if Component = 0 then begin Helper := 'ExtraPane.UsbHostSetup.exe'; ComponentFolder := 'UsbHost'; end
      else begin Helper := 'ExtraPane.UsbSetup.exe'; ComponentFolder := 'Microphone'; end;
      if FileExists(ExpandConstant('{app}\') + Helper) then begin
        if not RunComponent(ExpandConstant('{app}\') + Helper, '--remove', '', CameraCode) then
          StopUninstall('Component cleanup could not start: ' + Helper + '. ExtraPane files were retained.');
        if CameraCode = 3010 then ComponentRestart := True;
        if (CameraCode <> 0) and (CameraCode <> 3010) then
          StopUninstall('Component cleanup failed: ' + Helper + ' (exit ' + IntToStr(CameraCode) + '). ' + ComponentError + #13#10 + 'ExtraPane files were retained for retry.');
      end else if DirExists(ExpandConstant('{app}\Components\') + ComponentFolder) then
        StopUninstall('Missing cleanup helper for ' + ComponentFolder + '. Repair ExtraPane before uninstalling.');
    end;
  end;
  if (CurUninstallStep = usUninstall) and DirExists(ExpandConstant('{app}\Components\Display\state')) then begin
    if not FileExists(ExpandConstant('{app}\Components\Display\manage-driver.ps1')) then
      StopUninstall('The display driver manager is missing. Repair ExtraPane before uninstalling.');
    if not RunDisplayManager('Remove', CameraCode) then
      StopUninstall('Display driver removal failed (exit ' + IntToStr(CameraCode) + '). ' + ComponentError + #13#10 + 'ExtraPane and its ownership records were retained for retry.');
  end;
  if (CurUninstallStep = usUninstall) and RemoveCamera and
     (FileExists(ExpandConstant('{app}\ExtraPane.CameraSetup.exe')) or
      DirExists(ExpandConstant('{app}\Components\Camera'))) then begin
    if not FileExists(ExpandConstant('{app}\ExtraPane.CameraSetup.exe')) then
      StopUninstall('Camera setup helper is missing. The shared camera component was not removed.');
    if not RunComponent(ExpandConstant('{app}\ExtraPane.CameraSetup.exe'), '--remove --quiet', '', CameraCode) then
      StopUninstall('Camera removal could not start or administrator permission was canceled.');
    if (CameraCode <> 0) and (CameraCode <> 3010) then
      StopUninstall('Camera component removal failed (exit ' + IntToStr(CameraCode) + '). ' + ComponentError);
    if CameraCode = 3010 then ComponentRestart := True;
    if CameraCode = 3010 then
      Log('Camera registration removed; loaded files will be deleted after the final requested restart.');
  end;
  { Run after the app mutex check, before files are removed. Cancel never resets data. }
  if (CurUninstallStep = usUninstall) and RemoveUserData then begin
    { This is the single shared pairing used by both UI roles. }
    if not CredDelete('DM/peers/dm-lan', 1, 0) then begin
      ErrorCode := DLLGetLastError;
      if ErrorCode <> 1168 then
        StopUninstall('Unable to remove ExtraPane pairing: ' + SysErrorMessage(ErrorCode));
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
