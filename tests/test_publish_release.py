import hashlib
import json
import os
from pathlib import Path
import runpy
import tempfile
import unittest
from unittest.mock import patch

SCRIPT = Path(__file__).resolve().parents[1]/'scripts/publish_release.py'

class ReleaseArtifacts(unittest.TestCase):
    def exercise(self, channel=True, missing=False, corrupt=False, android=False):
        with tempfile.TemporaryDirectory() as temporary:
            previous = Path.cwd()
            try:
                os.chdir(temporary)
                dist = Path('dist'); dist.mkdir()
                version = '2026.11.03-pre.1.1'
                (dist/'release-metadata.json').write_text(json.dumps(dict(
                    version=version, prerelease=channel, explanation='Room to grow.',
                    tag='v'+version, title='v'+version if channel else 'Room to Grow')))
                names = [f'ExtraPane-{version}-win-x64.zip', f'ExtraPane-{version}-Setup-win-x64.exe',
                         f'ExtraPane-{version}-linux-x86_64.AppImage', f'ExtraPane-{version}-linux-dependencies.tar.gz', f'ExtraPane-{version}-android.apk']
                sha = hashlib.sha256(b'artifact').hexdigest()
                for name in names:
                    if not (missing and name.endswith('.apk' if android else '.AppImage')): (dist/name).write_bytes(b'artifact')
                (dist/'SHA256SUMS.txt').write_text(''.join(f'{sha}  {name}\n' for name in names[:2]))
                (dist/(names[2]+'.sha256')).write_text(('0'*64 if corrupt and not android else sha)+'  '+names[2]+'\n')
                (dist/(names[4]+'.sha256')).write_text(('0'*64 if corrupt and android else sha)+'  '+names[4]+'\n')
                with patch.dict(os.environ, {'GITHUB_SHA':'abc123'}), patch('subprocess.run') as publish:
                    if missing or corrupt:
                        with self.assertRaises((AssertionError, RuntimeError)): runpy.run_path(str(SCRIPT))
                        publish.assert_not_called()
                    else:
                        runpy.run_path(str(SCRIPT))
                        command=publish.call_args.args[0]
                        self.assertIn(str(dist/names[2]),command)
                        self.assertIn(str(dist/names[3]),command)
                        self.assertIn(str(dist/names[4]),command)
                        self.assertIn('--prerelease' if channel else '--latest',command)
                        self.assertEqual(len((dist/'SHA256SUMS.txt').read_text().splitlines()),5)
            finally: os.chdir(previous)
    def test_prerelease_contains_linux(self): self.exercise()
    def test_stable_contains_linux(self): self.exercise(channel=False)
    def test_missing_linux_blocks_publication(self): self.exercise(missing=True)
    def test_corrupt_linux_blocks_publication(self): self.exercise(corrupt=True)
    def test_missing_android_blocks_publication(self): self.exercise(missing=True,android=True)
    def test_corrupt_android_blocks_publication(self): self.exercise(corrupt=True,android=True)

if __name__ == '__main__': unittest.main()
