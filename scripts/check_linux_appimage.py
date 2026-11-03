"""Headless startup/cleanup check; does not claim GPU or LAN streaming coverage."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time

artifact = Path(sys.argv[1]).resolve()
environment = dict(os.environ, QT_QPA_PLATFORM='xcb',
                   LIBGL_ALWAYS_SOFTWARE='1')
environment.pop('APPIMAGE_EXTRACT_AND_RUN', None)
log = artifact.parent/'appimage-startup.log'
with log.open('w') as output, tempfile.TemporaryDirectory(prefix='extrapane-portable-') as temporary:
    # FUSE is unavailable in the runner container. Extract explicitly so SIGTERM
    # reaches ExtraPane rather than the AppImage runtime's extraction supervisor.
    subprocess.run([str(artifact), '--appimage-extract'], cwd=temporary,
                   env=environment, stdout=subprocess.DEVNULL, stderr=output, check=True)
    launcher = Path(temporary)/'squashfs-root/AppRun'
    process = subprocess.Popen([str(launcher)], cwd='/tmp', env=environment,
                               stdout=output, stderr=subprocess.STDOUT)
    try:
        time.sleep(12)
        if process.poll() is not None:
            raise RuntimeError(f'AppImage exited before startup check: {process.returncode}')
        process.terminate()
        if process.wait(timeout=15) != 0:
            raise RuntimeError('AppImage did not shut down cleanly')
    finally:
        if process.poll() is None:
            process.kill()
            process.wait()
print('AppImage startup and graceful shutdown passed under Xvfb; no streaming test performed.')
