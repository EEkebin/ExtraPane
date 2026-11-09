"""Preseed the unchanged Gradle wrapper cache using verified public downloads."""
import hashlib
import os
from pathlib import Path
from verified_download import fetch_verified

ROOT = Path(__file__).resolve().parent.parent


def main():
    properties = dict(line.split('=', 1) for line in (ROOT/'apps/android/gradle/wrapper/gradle-wrapper.properties').read_text().splitlines() if '=' in line)
    url = properties['distributionUrl'].replace('\\:', ':')
    if not url.endswith('/gradle-8.13-bin.zip'):
        raise RuntimeError('Review the Gradle API fallback when updating the wrapper version')
    # Gradle PathAssembler hashes the distribution URI to lowercase base36 MD5.
    number = int.from_bytes(hashlib.md5(url.encode(), usedforsecurity=False).digest(), 'big')
    key = ''
    while number:
        number, digit = divmod(number, 36)
        key = '0123456789abcdefghijklmnopqrstuvwxyz'[digit] + key
    home = Path(os.environ.get('GRADLE_USER_HOME', str(Path.home()/'.gradle')))
    destination = home/'wrapper/dists/gradle-8.13-bin'/key/'gradle-8.13-bin.zip'
    fetch_verified(['https://downloads.gradle.org/distributions/gradle-8.13-bin.zip',
                    'https://api.github.com/repos/gradle/gradle-distributions/releases/assets/232373824'],
                   destination, properties['distributionSha256Sum'])
    print('Verified Gradle 8.13 wrapper distribution cached')


if __name__ == '__main__':
    main()
