{ Version ordering uses the release label, not Windows numeric metadata, which
  intentionally drops prerelease run/attempt numbers. }
type TCalendarParts = array[0..6] of Integer;

function NumberParts(S: String; var Parts: TCalendarParts; var Count: Integer): Boolean;
var Token: String; I, Dot: Integer;
begin
  Result := False; Count := 0;
  while S <> '' do begin
    if Count > 6 then Exit;
    Dot := Pos('.', S);
    if Dot = 0 then begin Token := S; S := ''; end
    else begin
      Token := Copy(S, 1, Dot - 1); Delete(S, 1, Dot);
      if S = '' then Exit;
    end;
    if (Token = '') or (Length(Token) > 9) then Exit;
    for I := 1 to Length(Token) do if (Token[I] < '0') or (Token[I] > '9') then Exit;
    Parts[Count] := StrToIntDef(Token, -1);
    if Parts[Count] < 0 then Exit;
    Count := Count + 1;
  end;
  Result := Count > 0;
end;

function CalendarParts(S: String; var Parts: TCalendarParts): Boolean;
var Base, Pre: String; P, Count, PreCount, I, Days: Integer; Extra: TCalendarParts;
begin
  Result := False;
  for I := 0 to 6 do begin Parts[I] := 0; Extra[I] := 0; end;
  P := Pos('-pre.', S); Base := S; Pre := '';
  if P > 0 then begin Base := Copy(S, 1, P - 1); Pre := Copy(S, P + 5, Length(S)); end;
  if not NumberParts(Base, Parts, Count) then Exit;
  if (Count < 3) or (Count > 4) then Exit;
  if (Parts[0] < 1000) or (Parts[0] > 9999) or (Parts[1] < 1) or (Parts[1] > 12) then Exit;
  Days := 31;
  if (Parts[1] = 4) or (Parts[1] = 6) or (Parts[1] = 9) or (Parts[1] = 11) then Days := 30;
  if Parts[1] = 2 then begin
    Days := 28;
    if ((Parts[0] mod 4) = 0) and (((Parts[0] mod 100) <> 0) or ((Parts[0] mod 400) = 0)) then Days := 29;
  end;
  if (Parts[2] < 1) or (Parts[2] > Days) then Exit;
  if (Count = 4) and ((Parts[3] < 2) or (Parts[3] > 65535)) then Exit;
  if P = 0 then Parts[4] := 1
  else begin
    if not NumberParts(Pre, Extra, PreCount) then Exit;
    if (PreCount < 1) or (PreCount > 2) then Exit;
    if (PreCount = 2) and (Extra[1] < 1) then Exit;
    Parts[5] := Extra[0]; Parts[6] := Extra[1];
  end;
  Result := True;
end;

function NewerRelease(const Candidate, Installed: String): Boolean;
var A, B: TCalendarParts; I: Integer;
begin
  Result := False;
  if not CalendarParts(Candidate, A) or not CalendarParts(Installed, B) then Exit;
  for I := 0 to 6 do begin
    if A[I] > B[I] then begin Result := True; Exit; end;
    if A[I] < B[I] then Exit;
  end;
end;
