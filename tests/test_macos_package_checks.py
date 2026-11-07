import subprocess
import sys
import unittest
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from macos_package_checks import verify_disk_image


class DiskImageVerification(unittest.TestCase):
    def invoke(self, outcomes):
        self.calls = 0
        self.delays = []
        def run(command, **kwargs):
            code, error = outcomes[min(self.calls, len(outcomes) - 1)]
            self.calls += 1
            return subprocess.CompletedProcess(command, code, '', error)
        verify_disk_image('test.dmg', run=run, sleep=self.delays.append)

    def test_busy_then_success(self):
        self.invoke([(1, 'Resource temporarily unavailable'), (0, '')])
        self.assertEqual((self.calls, self.delays), (2, [1]))

    def test_corruption_is_not_retried(self):
        with self.assertRaises(subprocess.CalledProcessError):
            self.invoke([(1, 'checksum mismatch')])
        self.assertEqual((self.calls, self.delays), (1, []))

    def test_busy_failure_remains_failure(self):
        with self.assertRaises(subprocess.CalledProcessError):
            self.invoke([(1, 'Resource temporarily unavailable')])
        self.assertEqual((self.calls, self.delays), (5, [1, 2, 4, 8]))


if __name__ == '__main__':
    unittest.main()
