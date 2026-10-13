"""Watchdog tests use synthetic children, not alignment data."""
import os
from pathlib import Path
import sys
import tempfile
import time
import unittest

from fixture_process import run_fixture


class FixtureProcessTests(unittest.TestCase):
    def test_output_and_returncode(self):
        with tempfile.TemporaryDirectory() as directory:
            log = Path(directory) / "console.log"
            result = run_fixture([sys.executable, "-c", "print('evidence'); raise SystemExit(7)"], log)
            self.assertEqual(result.returncode, 7)
            self.assertEqual(result.stdout.strip(), "evidence")

    def test_timeout_retains_output(self):
        with tempfile.TemporaryDirectory() as directory:
            log = Path(directory) / "console.log"
            with self.assertRaisesRegex(RuntimeError, "Fixture exceeded"):
                run_fixture([sys.executable, "-c", "import time; print('started', flush=True); time.sleep(60)"],
                            log, timeout=1)
            self.assertEqual(log.read_text().strip(), "started")

    @unittest.skipIf(os.name == "nt", "Unix read producer process groups")
    def test_timeout_kills_producer(self):
        with tempfile.TemporaryDirectory() as directory:
            log = Path(directory) / "console.log"
            marker = Path(directory) / "orphan-ran"
            child = "import time; from pathlib import Path; time.sleep(2); Path(%r).touch()" % str(marker)
            parent = "import subprocess, sys, time; subprocess.Popen([sys.executable, '-c', %r]); time.sleep(60)" % child
            with self.assertRaises(RuntimeError):
                run_fixture([sys.executable, "-c", parent], log, timeout=1)
            time.sleep(2)
            self.assertFalse(marker.exists(), "Timed-out read producer survived")


if __name__ == "__main__":
    unittest.main()
