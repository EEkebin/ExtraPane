"""Bounded retry for DiskImages' transient resource-busy verification failure."""
import subprocess
import time


def verify_disk_image(path, run=subprocess.run, sleep=time.sleep):
    for attempt in range(5):
        result = run(['hdiutil', 'verify', str(path)], capture_output=True,
                     text=True, timeout=60)
        if result.returncode == 0:
            return
        message = (result.stdout or '') + (result.stderr or '')
        if 'resource temporarily unavailable' not in message.lower() or attempt == 4:
            print(message)
            raise subprocess.CalledProcessError(result.returncode, result.args,
                                                output=result.stdout, stderr=result.stderr)
        print(f'DiskImages is temporarily busy; retrying verification ({attempt + 1}/4).')
        sleep(2 ** attempt)
