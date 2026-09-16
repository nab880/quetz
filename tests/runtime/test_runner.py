import os
from pathlib import Path
import subprocess
import tempfile
import unittest


RUNNER = Path(__file__).resolve().parents[2] / 'runtime/quetz-run'


class GenericRunner(unittest.TestCase):
    def run_simulator(self, status=0, sentinel='PASS', args=()):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            deck = root / 'deck.py'
            deck.touch()
            binary = root / 'bin'
            binary.mkdir()
            for name, text in {
                'timeout': '#!/bin/sh\nshift\nexec "$@"\n',
                'sst': '#!/bin/sh\nprintf "TESTFINISH[0] (%s)\\n" "$TEST_SENTINEL"\nexit "$TEST_STATUS"\n'
            }.items():
                path = binary / name
                path.write_text(text)
                path.chmod(0o755)
            out = root / 'output'
            result = subprocess.run(['bash', str(RUNNER), '--no-docker', '--deck', str(deck),
                '--out', str(out), '--quiet', *args], capture_output=True, text=True,
                env=os.environ | {'PATH': str(binary) + ':/usr/bin:/bin',
                                  'TEST_STATUS': str(status), 'TEST_SENTINEL': sentinel})
            artifact = out / 'result.txt'
            return result, artifact.read_text() if artifact.exists() else None

    def test_success(self):
        result, artifact = self.run_simulator()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(artifact.startswith('PASS:'))

    def test_guest_failure(self):
        result, artifact = self.run_simulator(sentinel='FAIL')
        self.assertEqual(result.returncode, 2, result.stderr)
        self.assertTrue(artifact.startswith('FAIL:'))

    def test_nonzero_simulator_rejects_pass_text(self):
        result, artifact = self.run_simulator(status=1)
        self.assertEqual(result.returncode, 1)
        self.assertTrue(artifact.startswith('ERROR:'))

    def test_timeout_rejects_pass_text(self):
        result, artifact = self.run_simulator(status=124)
        self.assertEqual(result.returncode, 1)
        self.assertTrue(artifact.startswith('ERROR:'))

    def test_missing_sentinel(self):
        result, artifact = self.run_simulator(sentinel='unknown')
        self.assertEqual(result.returncode, 1)
        self.assertTrue(artifact.startswith('ERROR:'))

    def test_rejects_unbounded_timeout(self):
        result, artifact = self.run_simulator(args=['--timeout', '0'])
        self.assertEqual(result.returncode, 1)
        self.assertIsNone(artifact)

    def test_multiple_environment_assignments(self):
        result, artifact = self.run_simulator(args=['--env', 'TEST_SENTINEL=PASS', '--env', 'EXAMPLE=a b'])
        self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == '__main__':
    unittest.main()
