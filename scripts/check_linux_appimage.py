"""Headless startup/cleanup check; does not claim GPU or LAN streaming coverage."""
import os
from pathlib import Path
import subprocess
import sys
import time

artifact = Path(sys.argv[1]).resolve()
environment = dict(os.environ, APPIMAGE_EXTRACT_AND_RUN='1', QT_QPA_PLATFORM='xcb',
                   LIBGL_ALWAYS_SOFTWARE='1')
log = artifact.parent/'appimage-startup.log'
with log.open('w') as output:
    process = subprocess.Popen([str(artifact)], cwd='/tmp', env=environment,
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
