"""Compile the real Pascal comparator into an inert check executable; no installation.

The fixture exits from InitializeSetup before any wizard/install operation and
writes only its check report in this checkout's out directory. It never reads the
installed ExtraPane record, runs a component helper or changes drivers/settings.
"""
from pathlib import Path
import argparse
import os
import subprocess

ROOT = Path(__file__).resolve().parent.parent

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', type=Path, default=ROOT/'out/installer-tools/inno/ISCC.exe')
    args = parser.parse_args()
    folder = ROOT/'out/installer-maintenance-version-check'
    folder.mkdir(parents=True, exist_ok=True)
    result = folder/'result.txt'
    # Every check uses the production Pascal implementation, not a Python copy.
    pairs = [
        ('2026.11.08-pre.10', '2026.11.08-pre.9', True),
        ('2026.11.08-pre.9', '2026.11.08-pre.10', False),
        ('2026.11.08-pre.84.2', '2026.11.08-pre.84.1', True),
        ('2026.11.08-pre.84.1', '2026.11.08-pre.84.2', False),
        ('2026.11.08-pre.84.1', '2026.11.08-pre.84.1', False),
        ('2026.11.08', '2026.11.08-pre.999.1', True),
        ('2026.11.08-pre.999.1', '2026.11.08', False),
        ('2026.11.08.2', '2026.11.08', True),
        ('2026.11.08.2-pre.0', '2026.11.08', True),
        ('2026.11.09-pre.0', '2026.11.08.2', True),
        ('2026.11.08-pre.0', '2026.11.08-pre.0', False),
        ('2026.11.08-pre.0.1', '2026.11.08-pre.0', True),
        ('2026.11.08-pre.10', 'unknown', False),
        ('2026.11.08-pre.', '2026.11.07', False),
        ('2026.11.08-pre.1.0', '2026.11.07', False),
        ('2026.11.08.1', '2026.11.07', False),
        ('2026.02.30', '2026.02.01', False),
        ('2028.02.29', '2028.02.28', True),
        ('2026.11.08-pre.9999999999999', '2026.11.07', False),
    ]
    checks = '\n'.join(f"  if NewerRelease('{a}', '{b}') <> {str(want)} then RaiseException('Version case {i}');" for i, (a,b,want) in enumerate(pairs))
    source = f"""[Setup]
AppName=ExtraPane isolated version check
AppVersion=1
DefaultDirName={{tmp}}/unused-extra-pane-fixture
PrivilegesRequired=lowest
CreateAppDir=no
Uninstallable=no
OutputDir={folder}
OutputBaseFilename=version-check
[Code]
#include "{ROOT / 'packaging/maintenance-version.iss'}"
function InitializeSetup: Boolean;
begin
{checks}
  SaveStringToFile('{result}', 'passed {len(pairs)} production Pascal version cases', False);
  Result := False;
end;
"""
    fixture = folder/'check.iss'
    fixture.write_text(source, encoding='utf-8')
    if result.exists(): result.unlink()
    subprocess.run([str(args.compiler), str(fixture)], check=True, capture_output=True, text=True)
    run = subprocess.run([str(folder/'version-check.exe'), '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART'], timeout=20)
    assert run.returncode == 1 and result.exists(), 'Fixture did not exit cleanly before installation'
    print(result.read_text())

if __name__ == '__main__': main()
