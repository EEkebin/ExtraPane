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

