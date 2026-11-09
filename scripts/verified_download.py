"""Download public build inputs with a required SHA256 and narrow URL fallback."""
import hashlib
from pathlib import Path
import shutil
import urllib.error
import urllib.request


def fetch_verified(urls, destination, sha256):
    destination = Path(destination)
    if destination.is_file():
        with destination.open('rb') as stream:
            if hashlib.file_digest(stream, 'sha256').hexdigest() == sha256:
                return
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = destination.with_name(destination.name + '.download')
    failure = None
    try:
        for url in urls:
            headers = {'User-Agent': 'ExtraPane-build'}
            if url.startswith('https://api.github.com/repos/'):
                headers['Accept'] = 'application/octet-stream'
            try:
                with urllib.request.urlopen(urllib.request.Request(url, headers=headers), timeout=60) as response, temporary.open('wb') as output:
                    shutil.copyfileobj(response, output)
            except (urllib.error.URLError, TimeoutError, ConnectionError) as error:
                failure = error
                continue
            with temporary.open('rb') as stream:
                actual = hashlib.file_digest(stream, 'sha256').hexdigest()
            if actual != sha256:
                raise RuntimeError('Downloaded build input failed SHA256 verification: ' + destination.name)
            temporary.replace(destination)
            return
        raise RuntimeError('Could not download verified build input: ' + destination.name) from failure
    finally:
        temporary.unlink(missing_ok=True)
