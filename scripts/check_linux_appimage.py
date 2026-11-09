"""Headless startup/cleanup check; does not claim GPU or LAN streaming coverage."""
import os
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import time

artifact = Path(sys.argv[1]).resolve()
environment = dict(os.environ, QT_QPA_PLATFORM='xcb',
                   LIBGL_ALWAYS_SOFTWARE='1', QT_FORCE_STDERR_LOGGING='1')
environment.pop('APPIMAGE_EXTRACT_AND_RUN', None)
log = artifact.parent/'appimage-startup.log'
with log.open('w') as output, tempfile.TemporaryDirectory(prefix='extrapane-portable-') as temporary:
    # FUSE is unavailable in the runner container. Extract explicitly so SIGTERM
    # reaches ExtraPane rather than the AppImage runtime's extraction supervisor.
    subprocess.run([str(artifact), '--appimage-extract'], cwd=temporary,
                   env=environment, stdout=subprocess.DEVNULL, stderr=output, check=True)
    launcher = Path(temporary)/'squashfs-root/AppRun'
    factory_check = subprocess.run([str(launcher), '--check-media-factories'],
                                   cwd='/tmp', env=environment, stdout=subprocess.PIPE,
                                   stderr=output, timeout=30, check=True)
    factories = json.loads(factory_check.stdout)
    if factories.get('schema') != 1 or factories.get('missing') != [] or factories.get('devicesTested') is not False:
        raise RuntimeError('Portable media factory check returned invalid or missing capabilities')
    docs = Path(temporary)/'squashfs-root/usr/share/doc/extrapane'
    for document in ('LINUX.md', 'LINUX-RESOURCES.md', 'PLATFORM-PARITY.md'):
        if not (docs/document).is_file():
            raise RuntimeError(f'Missing portable setup documentation: {document}')
    process = subprocess.Popen([str(launcher)], cwd='/tmp', env=environment,
                               stdout=output, stderr=subprocess.STDOUT)
    try:
        time.sleep(12)
        if process.poll() is not None:
            output.flush()
            raise RuntimeError(f'AppImage exited before startup check: {process.returncode}\n'+log.read_text())
        process.terminate()
        if process.wait(timeout=15) != 0:
            raise RuntimeError('AppImage did not shut down cleanly')
    finally:
        if process.poll() is None:
            process.kill()
            process.wait()
print('AppImage media factories, setup documentation, startup and graceful shutdown passed; no device or streaming test performed.')
