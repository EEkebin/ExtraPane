"""Restore hash-pinned Microsoft SDK/WDK NuGet build tools into ignored out/.

Does not install a system SDK, driver, certificate, or package manager.
"""
from concurrent.futures import ThreadPoolExecutor
from hashlib import sha256
from pathlib import Path
from urllib.request import urlretrieve
from zipfile import ZipFile

VERSION = '10.0.28000.2526'
PACKAGES = {
    'microsoft.windows.sdk.cpp': 'be1b419491607eae6f7c57844ebab39face9643c51e2af1d9176a3ba0d0b23fc',
    'microsoft.windows.sdk.cpp.x64': 'a9cae2a8c5da7f5dc5838ae6a76d06d0d2e2fdc3d8cfc69ca6c184e4b9193a00',
    'microsoft.windows.wdk.x64': '63c939fb5a79295bf40e941db592681272219b04edff095fe2f3d123e5579a90',
}
ROOT = Path(__file__).resolve().parent.parent / 'out' / 'driver-tools'


def restore(item):
    name, expected = item
    archive = ROOT / (name + '.nupkg')
    if not archive.exists():
        urlretrieve(f'https://api.nuget.org/v3-flatcontainer/{name}/{VERSION}/{name}.{VERSION}.nupkg', archive)
    if sha256(archive.read_bytes()).hexdigest() != expected:
        raise RuntimeError(f'{name}: SHA256 mismatch; refusing to extract')
    destination = ROOT / (name + '.' + VERSION)
    marker = destination / '.dm-verified-sha256'
    if not marker.exists() or marker.read_text() != expected:
        with ZipFile(archive) as package:
            for entry in package.infolist():
                path = (destination / entry.filename).resolve()
                if not path.is_relative_to(destination.resolve()):
                    raise RuntimeError('Package contains a path outside its destination')
            package.extractall(destination)
        marker.write_text(expected)
    print(f'{name} {VERSION} verified', flush=True)


if __name__ == '__main__':
    ROOT.mkdir(parents=True, exist_ok=True)
    with ThreadPoolExecutor(max_workers=3) as pool:
        list(pool.map(restore, PACKAGES.items()))
