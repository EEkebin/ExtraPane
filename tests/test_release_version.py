from pathlib import Path
import sys
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from release_version import metadata, parse

class CalendarVersions(unittest.TestCase):
    def test_prerelease_and_retry(self):
        first = metadata('2026-11-02', 'pre', 14, 1)
        retry = metadata('2026-11-02', 'pre', 14, 2)
        self.assertEqual(first['tag'], 'v2026.11.02-pre.14.1')
        self.assertEqual(first['title'], first['tag'])
        self.assertTrue(first['prerelease'])
        self.assertNotEqual(first['tag'], retry['tag'])

    def test_named_stable_and_revision(self):
        release = metadata('2026-11-02', 'stable', 1, 1, title='The Elbow Room Update',
                           explanation='Elbow room means space to move: another screen for your desktop.')
        self.assertEqual(release['tag'], 'v2026.11.02')
        self.assertEqual(release['title'], 'The Elbow Room Update')
        self.assertFalse(release['prerelease'])
        self.assertEqual(parse('2026.11.02.2'), '2026.11.2.2')
        self.assertEqual(parse('2026.02.03-pre.5.1'), '2026.2.3.0')

    def test_reject_invalid_dates_and_versions(self):
        for value in ['2026.02.29', '2026.13.01', '0.13.0', '2026.1.2',
                      '2026.11.02.1', '2026.11.02.65536', '2026.11.02-pre.3.0',
                      '2026.11.02\nBAD=value']:
            with self.subTest(value=value), self.assertRaises(ValueError): parse(value)
        self.assertEqual(parse('2028.02.29'), '2028.2.29.0')

    def test_stable_needs_name_and_explanation(self):
        for title, explanation in [('', ''), ('The Update', ''), ('', 'Explanation'), ('Bad\nName', 'Explanation')]:
            with self.assertRaises(ValueError):
                metadata('2026-11-02', 'stable', 1, 1, title=title, explanation=explanation)

if __name__ == '__main__': unittest.main()
