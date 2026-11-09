#define Package GetEnv("EXTRAPANE_DRIVER_PACKAGE")
#define Version GetEnv("EXTRAPANE_VERSION")
[Setup]
; Stable identity preserves driver ownership and its existing uninstaller.
AppId=SideScreen.VirtualDisplayDriver
AppName=ExtraPane Virtual Display Driver
AppVersion={#Version}
VersionInfoVersion={#GetEnv("EXTRAPANE_NUMERIC_VERSION")}
VersionInfoProductTextVersion={#Version}
AppPublisher=EEkebin
AppPublisherURL=https://github.com/EEkebin/ExtraPane
DefaultDirName={autopf}\ExtraPane\Components\Display
UsePreviousAppDir=no
DisableDirPage=yes
DisableProgramGroupPage=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.22000
OutputDir={#GetEnv("EXTRAPANE_DRIVER_DIST")}
OutputBaseFilename=ExtraPane.DriverSetup
SetupIconFile=..\assets\branding\ExtraPane.ico
UninstallDisplayIcon={app}\ExtraPane.ico
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
CloseApplications=no
RestartApplications=no
[Registry]
; Internal component: removal is orchestrated by the main ExtraPane uninstaller.
Root: HKLM; Subkey: "Software\Microsoft\Windows\CurrentVersion\Uninstall\SideScreen.VirtualDisplayDriver_is1"; ValueType: dword; ValueName: "SystemComponent"; ValueData: "1"; Flags: uninsdeletevalue
[Files]
Source: "{#Package}\*"; DestDir: "{app}\package"; Flags: ignoreversion
Source: "manage-driver.ps1"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\assets\branding\ExtraPane.ico"; DestDir: "{app}"; Flags: ignoreversion
[UninstallDelete]
Type: filesandordirs; Name: "{app}\state"
Type: files; Name: "{app}\driver-setup.log"
[Code]
var
  Consent: TInputOptionWizardPage;
  DriverInstallFailed: Boolean;
#include "shutdown.iss"

function InitializeSetup: Boolean;
begin
  Result := True;
  if WizardSilent and (ExpandConstant('{param:ACCEPTDEVELOPMENTCERT|0}') <> '1') then begin
    Log('Silent driver setup requires /ACCEPTDEVELOPMENTCERT=1.');
    Result := False;
  end;
end;

procedure InitializeWizard;
begin
  Consent := CreateInputOptionPage(wpWelcome, 'Server display driver',
    'The driver is required to host an extended display.',
    'This development driver is not Microsoft release signed. Setup adds its public code-signing certificate to this PC''s Trusted Root and Trusted Publishers stores and installs the driver for all users. It does not change Secure Boot, Memory Integrity or test-signing mode. Windows policy may still prevent the driver from loading.', False, False);
  Consent.Add('I agree to install the driver and trust its development certificate on this PC.');
  Consent.Values[0] := ExpandConstant('{param:ACCEPTDEVELOPMENTCERT|0}') = '1';
end;

function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;
  if (CurPageID = Consent.ID) and not Consent.Values[0] then begin
    SuppressibleMsgBox('Certificate consent is required to install this development driver. Cancel to leave this PC unchanged.', mbInformation, MB_OK, IDOK);
    Result := False;
  end;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  Result := '';
  if CompareText(ExpandConstant('{app}'), ExpandConstant('{autopf}\ExtraPane\Components\Display')) <> 0 then
    Result := 'The driver manager must be installed in Program Files\ExtraPane\Components\Display.'
  else if not Consent.Values[0] then
    Result := 'Development certificate consent is required.'
  else if not StopRunningExtraPane then
    Result := 'ExtraPane could not stop its sessions. Close the app and retry.';
end;

function RunManager(Action: String): Boolean;
var Code: Integer;
begin
  Result := Exec(ExpandConstant('{sys}\WindowsPowerShell\v1.0\powershell.exe'),
    '-NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "' + ExpandConstant('{app}\manage-driver.ps1') + '" -Action ' + Action,
    ExpandConstant('{app}'), SW_HIDE, ewWaitUntilTerminated, Code);
  Result := Result and (Code = 0);
  if not Result then Log('Driver manager failed with code ' + IntToStr(Code));
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if (CurStep = ssPostInstall) and not RunManager('Install') then begin
    DriverInstallFailed := True;
    RaiseException('The server driver could not be installed. See Program Files\ExtraPane\Components\Display\driver-setup.log and Windows\INF\setupapi.dev.log. No boot-security settings were changed.');
  end;
end;

function GetCustomSetupExitCode: Integer;
begin
  Result := 0;
  if DriverInstallFailed then Result := 1;
end;

procedure CurPageChanged(CurPageID: Integer);
begin
  if (CurPageID = wpFinished) and DriverInstallFailed then begin
    WizardForm.FinishedHeadingLabel.Caption := 'Server driver installation failed';
    WizardForm.FinishedLabel.Caption := 'The driver is not ready for hosting. Check Program Files\ExtraPane\Components\Display\driver-setup.log, correct the reported problem, and run driver setup again. The driver manager remains installed for repair or removal.';
  end;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  { This runs after confirmation, before any files or registry entries are removed. }
  if CurUninstallStep = usUninstall then begin
    if not StopRunningExtraPane then RaiseException('Close ExtraPane sessions before removing the driver.');
    if not RunManager('Remove') then
      RaiseException('The driver could not be removed. The uninstaller and ownership records have been kept so you can retry. Check driver-setup.log. If Windows requested a restart, restart manually before retrying.');
  end;
end;
