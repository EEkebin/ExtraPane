"""Format (or with --check, verify) tracked sources.

C/C++/Objective-C++ use clang-format with the repository .clang-format.
Java uses google-java-format (AOSP 4-space style) from a hash-pinned jar.
QML uses qmlformat from the pinned Qt SDK.

  python scripts/format.py              # rewrite files
  python scripts/format.py --check      # exit 1 and list files that need formatting
  python scripts/format.py --check FILE...  # only these files

Tools are looked up via CLANG_FORMAT, JAVA (or JAVA_HOME) and QMLFORMAT, then
PATH and the usual local locations.
"""
import argparse
import glob
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import sys
import urllib.request

ROOT = Path(__file__).resolve().parent.parent
CPP = ('.c', '.h', '.cpp', '.hpp', '.mm', '.m')
JAVA = ('.java',)
QML = ('.qml',)
GJF_VERSION = '1.28.0'
GJF_URL = f'https://github.com/google/google-java-format/releases/download/v{GJF_VERSION}/google-java-format-{GJF_VERSION}-all-deps.jar'
GJF_SHA256 = '32342e7c1b4600f80df3471da46aee8012d3e1445d5ea1be1fb71289b07cc735'


def tracked(suffixes, only):
    if only:
        return [Path(p) for p in only if Path(p).suffix in suffixes]
    out = subprocess.run(['git', 'ls-files', '-z'], cwd=ROOT, check=True, capture_output=True).stdout
    return [Path(p) for p in out.decode().split('\0') if p and Path(p).suffix in suffixes]


def find(env, names, extra=()):
    if os.environ.get(env):
        return os.environ[env]
    for name in names:
        found = shutil.which(name)
        if found:
            return found
    for pattern in extra:
        hits = sorted(glob.glob(pattern))
        if hits:
            return hits[-1]
    return None


def clang_format():
    return find('CLANG_FORMAT', ['clang-format'], [
        r'C:\Program Files\Microsoft Visual Studio\*\*\VC\Tools\Llvm\x64\bin\clang-format.exe'])


def java():
    if os.environ.get('JAVA'):
        return os.environ['JAVA']
    home = os.environ.get('JAVA_HOME')
    if home:
        return str(Path(home) / 'bin' / 'java')
    return find('JAVA', ['java'], [str(ROOT / 'out/android-tools/jdk/*/bin/java.exe')])


def gjf_jar():
    jar = ROOT / 'out/format-tools' / f'google-java-format-{GJF_VERSION}.jar'
    if not jar.is_file():
        jar.parent.mkdir(parents=True, exist_ok=True)
        with urllib.request.urlopen(GJF_URL, timeout=120) as response:
            data = response.read()
        if hashlib.sha256(data).hexdigest() != GJF_SHA256:
            raise SystemExit('google-java-format checksum mismatch; refusing to run it')
        jar.write_bytes(data)
    return jar


def qmlformat():
    return find('QMLFORMAT', ['qmlformat'], [str(ROOT / 'out/qt-sdk/*/*/bin/qmlformat.exe'),
                                              str(ROOT / 'out/qt-sdk/*/*/bin/qmlformat')])


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--check', action='store_true')
    parser.add_argument('--skip', action='append', default=[], choices=['cpp', 'java', 'qml'])
    parser.add_argument('files', nargs='*')
    args = parser.parse_args()
    dirty = []

    def run(files, command_for, label):
        for path in files:
            source = (ROOT / path).read_bytes()
            result = subprocess.run(command_for(ROOT / path), cwd=ROOT, capture_output=True)
            if result.returncode:
                raise SystemExit(f'{label} failed on {path}:\n{result.stderr.decode(errors="replace")}')
            # Some tools emit CRLF on Windows; the repository stores LF (.gitattributes).
            formatted = result.stdout.replace(b'\r\n', b'\n')
            if formatted != source:
                dirty.append(str(path))
                if not args.check:
                    (ROOT / path).write_bytes(formatted)

    if 'cpp' not in args.skip:
        tool = clang_format()
        if not tool:
            raise SystemExit('clang-format not found (set CLANG_FORMAT)')
        run(tracked(CPP, args.files), lambda p: [tool, '--style=file', str(p)], 'clang-format')
    if 'java' not in args.skip:
        files = tracked(JAVA, args.files)
        if files:
            runtime = java()
            if not runtime:
                raise SystemExit('java not found (set JAVA or JAVA_HOME)')
            jar = gjf_jar()
            run(files, lambda p: [runtime, '-jar', str(jar), '--aosp', str(p)], 'google-java-format')
    if 'qml' not in args.skip:
        files = tracked(QML, args.files)
        if files:
            tool = qmlformat()
            if not tool:
                raise SystemExit('qmlformat not found (set QMLFORMAT)')
            run(files, lambda p: [tool, str(p)], 'qmlformat')

    if args.check and dirty:
        print('These files need formatting (run python scripts/format.py):')
        for path in dirty:
            print('  ' + path)
        return 1
    print(f'{len(dirty)} file(s) {"need formatting" if args.check else "reformatted"}.')
    return 0


if __name__ == '__main__':
    sys.exit(main())
