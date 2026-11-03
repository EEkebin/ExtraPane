"""Draft Linux AppImage packaging; never builds, launches, tests or publishes the app.

Run only after a separately authorized Linux build. Packaging tools are supplied
locally and hash-pinned by the caller, never fetched from mutable latest URLs.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import tempfile

from release_version import current

ROOT = Path(__file__).resolve().parent.parent
TOOLS = {'linuxdeploy', 'linuxdeploy-plugin-qt', 'linuxdeploy-plugin-gstreamer.sh'}
REQUIRED_PLUGINS = ('app', 'coreelements', 'playback', 'typefindfunctions',
                    'videoparsersbad', 'opengl', 'qml6', 'videoconvertscale', 'videorate', 'pipewire', 'openh264')
DECODER_PLUGINS = ('va', 'vaapi', 'nvcodec', 'v4l2codecs')


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tools-lock', type=Path, required=True)
    parser.add_argument('--licenses-dir', type=Path, required=True,
                        help='Reviewed notices/source-offer materials for the actual bundled dependency set')
    parser.add_argument('--qmake', type=Path, required=True, help='qmake from the same Qt 6 used to build')
    parser.add_argument('--plugins-dir', type=Path, required=True, help='GStreamer 1.24+ plugin directory')
    parser.add_argument('--helpers-dir', type=Path, required=True, help='Directory containing gst-plugin-scanner')
    args = parser.parse_args()
    if platform.system() != 'Linux' or platform.machine() != 'x86_64':
        raise SystemExit('Package on Linux x86_64; Windows/WSL is not launched by this script.')
    build = ROOT/'out/build/linux-release'
    if not (build/'platform/linux/ExtraPane').is_file():
        raise SystemExit('Build the Linux target first; packaging does not run a build.')
    version = current()
    output = ROOT/'out/dist-linux'/f'ExtraPane-{version}-linux-x86_64.AppImage'
    if output.exists():
        raise SystemExit('Refusing to overwrite an existing AppImage. Use a new release version.')
    lock = json.loads(args.tools_lock.read_text())
    if lock.get('schema') != 1 or set(lock.get('tools', {})) != TOOLS:
        raise SystemExit('Invalid tool lock; use packaging/linux/tools.example.json as the schema.')
    for name, tool in lock['tools'].items():
        path = Path(tool['path'])
        if not path.is_absolute() or not path.is_file() or not re.fullmatch('[a-fA-F0-9]{64}', tool['sha256']):
            raise SystemExit(f'Supply the reviewed tool path and SHA256 for {name}.')
        if digest(path).lower() != tool['sha256'].lower():
            raise SystemExit(f'Tool hash mismatch: {name}')
    if not args.licenses_dir.is_dir() or not any(args.licenses_dir.iterdir()):
        raise SystemExit('Dependency notices must be supplied before creating a distributable.')
    if not args.qmake.is_file() or not (args.helpers_dir/'gst-plugin-scanner').is_file():
        raise SystemExit('Missing Qt qmake or GStreamer plugin scanner.')
    plugins = []
    for name in REQUIRED_PLUGINS:
        path = args.plugins_dir/f'libgst{name}.so'
        if not path.is_file():
            raise SystemExit(f'Missing required plugin: {path.name}')
        plugins.append(path)
    decoders = [args.plugins_dir/f'libgst{name}.so' for name in DECODER_PLUGINS
                if (args.plugins_dir/f'libgst{name}.so').is_file()]
    # The required OpenH264 plugin includes a software encoder and decoder.
    plugins += decoders
    output.parent.mkdir(parents=True, exist_ok=True)
    (ROOT/'out').mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='linux-package-', dir=ROOT/'out') as temporary:
        stage = Path(temporary)
        tools = stage/'tools'; tools.mkdir()
        for name, tool in lock['tools'].items():
            shutil.copy2(tool['path'], tools/name)
            (tools/name).chmod(0o755)
        appdir = stage/'ExtraPane.AppDir'
        subprocess.run(['cmake', '--install', str(build), '--prefix', str(appdir/'usr')], check=True)
        bundled_plugins = stage/'plugins'; bundled_plugins.mkdir()
        for plugin in plugins:
            shutil.copy2(plugin, bundled_plugins/plugin.name)
        helpers = stage/'helpers'; helpers.mkdir()
        shutil.copy2(args.helpers_dir/'gst-plugin-scanner', helpers/'gst-plugin-scanner')
        notices = appdir/'usr/share/licenses/extrapane'
        shutil.copytree(args.licenses_dir, notices)
        # Only ship Qt plugins used by this app. A developer's KDE installation
        # may contain unrelated image importers with optional/missing dependencies.
        qt_plugins = Path(subprocess.check_output(
            [str(args.qmake), '-query', 'QT_INSTALL_PLUGINS'], text=True).strip())
        selected_qt = stage/'qt-plugins'; selected_qt.mkdir()
        for category in ('platforms', 'xcbglintegrations',
                         'wayland-decoration-client', 'wayland-graphics-integration-client',
                         'wayland-shell-integration', 'tls', 'networkinformation'):
            if (qt_plugins/category).is_dir():
                shutil.copytree(qt_plugins/category, selected_qt/category)
        (selected_qt/'imageformats').mkdir()
        (selected_qt/'platforminputcontexts').mkdir()
        # Avoid optional virtual-keyboard/platform integrations from the build PC.
        # Only the Qt modules imported by the app and its Fusion controls are used.
        qt_qml = Path(subprocess.check_output([str(args.qmake), '-query', 'QT_INSTALL_QML'], text=True).strip())
        selected_qml = stage/'qt-qml'; selected_qml.mkdir()
        for module in ('QtCore', 'QtQml', 'QtQuick'):
            target = selected_qml/module; target.mkdir()
            for file in (qt_qml/module).iterdir():
                if file.is_file(): shutil.copy2(file, target/file.name)
        for module in ('QtQml/Models', 'QtQml/WorkerScript', 'QtQuick/Window', 'QtQuick/Layouts',
                       'QtQuick/Controls', 'QtQuick/Templates', 'QtQuick/Effects', 'QtQuick/Shapes'):
            if (qt_qml/module).is_dir(): shutil.copytree(qt_qml/module, selected_qml/module)
        qml_scan = stage/'qml-scan'; qml_scan.mkdir()
        for file in (ROOT/'apps/linux').glob('*.qml'):
            # These two modules are registered by our executable and GStreamer's
            # native plugin at runtime; they have no deployable QML directory.
            lines = file.read_text().splitlines()
            (qml_scan/file.name).write_text('\n'.join(line for line in lines if not
                line.startswith(('import ExtraPane.Native', 'import org.freedesktop.gstreamer.')))+'\n')
        # The generated proxy preserves every qmake query except the plugin path.
        qmake_proxy = tools/'qmake-selected'
        qmake_proxy.write_text('#!/usr/bin/env python3\nimport subprocess,sys\n'
            f'real={str(args.qmake.resolve())!r}\npaths={{"QT_INSTALL_PLUGINS":{str(selected_qt)!r},"QT_INSTALL_QML":{str(selected_qml)!r}}}\n'
            'result=subprocess.run([real,*sys.argv[1:]],text=True,capture_output=True)\n'
            'text=result.stdout\n'
            'if len(sys.argv)==3 and sys.argv[1]=="-query" and sys.argv[2] in paths: text=paths[sys.argv[2]]+"\\n"\n'
            'elif sys.argv[1:]==["-query"]: text="\\n".join(line.split(":",1)[0]+":"+paths[line.split(":",1)[0]] if line.split(":",1)[0] in paths else line for line in text.splitlines())+"\\n"\n'
            'sys.stdout.write(text);sys.stderr.write(result.stderr);sys.exit(result.returncode)\n')
        qmake_proxy.chmod(0o755)
        # This records the build inputs; it is not a runtime validation claim.
        record = {'version': version, 'tools': lock['tools'],
                  'plugins': {p.name: digest(p) for p in plugins}, 'validation': 'not performed by packaging'}
        (appdir/'usr/share/doc/extrapane/package-inputs.json').write_text(json.dumps(record, indent=2)+'\n')
        env = dict(os.environ)
        env.update(PATH=str(tools)+os.pathsep+env.get('PATH', ''), ARCH='x86_64',
                   QMAKE=str(qmake_proxy), QML_SOURCES_PATHS=str(qml_scan), NO_STRIP='1',
                   EXTRA_PLATFORM_PLUGINS=';'.join(p.name for p in (Path(subprocess.check_output([str(args.qmake), '-query', 'QT_INSTALL_PLUGINS'], text=True).strip())/'platforms').glob('*wayland*.so')),
                   GSTREAMER_PLUGINS_DIR=str(bundled_plugins), GSTREAMER_HELPERS_DIR=str(helpers),
                   APPIMAGE_EXTRACT_AND_RUN='1', OUTPUT=str(stage/output.name), VERSION=version)
        subprocess.run([str(tools/'linuxdeploy'), '--appdir', str(appdir),
                        '--plugin', 'gstreamer', '--plugin', 'qt'],
                       cwd=stage, env=env, check=True)
        # New Qt versions use a plugin name older deployment tools do not
        # recognize. Copy the selected Wayland integrations and deploy their deps.
        for category in ('wayland-graphics-integration-client',
                         'wayland-decoration-client', 'wayland-shell-integration'):
            if (selected_qt/category).is_dir():
                shutil.copytree(selected_qt/category, appdir/'usr/plugins'/category, dirs_exist_ok=True)
        # Own the launch environment: plugin-generated hooks may fail under
        # set -e when an optional PTP helper is absent. Resolve the executable
        # directly, so Qt does not derive its prefix from an AppRun symlink.
        launcher = appdir/'AppRun'
        launcher.unlink(missing_ok=True)
        launcher.write_text('''#!/usr/bin/env bash
set -e
APPDIR="$(cd -- "$(dirname -- "$0")" && pwd)"
export APPDIR
export QT_PLUGIN_PATH="$APPDIR/usr/plugins"
export QML_IMPORT_PATH="$APPDIR/usr/qml"
export QML2_IMPORT_PATH="$APPDIR/usr/qml"
export GST_PLUGIN_SYSTEM_PATH_1_0="$APPDIR/usr/lib/gstreamer-1.0"
export GST_PLUGIN_PATH_1_0="$GST_PLUGIN_SYSTEM_PATH_1_0"
export GST_PLUGIN_SCANNER_1_0="$APPDIR/usr/lib/gstreamer1.0/gstreamer-1.0/gst-plugin-scanner"
export GST_REGISTRY_REUSE_PLUGIN_SCANNER=no
exec "$APPDIR/usr/bin/ExtraPane" "$@"
''')
        launcher.chmod(0o755)
        subprocess.run([str(tools/'linuxdeploy'), '--appdir', str(appdir),
                        '--deploy-deps-only', str(appdir/'usr/plugins'),
                        '--output', 'appimage'], cwd=stage, env=env, check=True)
        built = stage/output.name
        if not built.is_file():
            raise SystemExit('linuxdeploy did not produce the expected AppImage.')
        # No overwrite even if another packaging process produced this version meanwhile.
        with output.open('xb') as destination, built.open('rb') as source:
            shutil.copyfileobj(source, destination)
        output.chmod(0o755)
        output.with_suffix('.AppImage.sha256').write_text(f'{digest(output)}  {output.name}\n')
    print(f'Packaged {output}. Not launched or published; runtime validation remains required.')


if __name__ == '__main__':
    main()
