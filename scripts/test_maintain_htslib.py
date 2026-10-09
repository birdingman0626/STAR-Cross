"""Small negative controls for the locked vendor reconstruction boundary."""
import importlib.util
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

SCRIPT = Path(__file__).with_name("maintain_htslib.py")
spec = importlib.util.spec_from_file_location("maintain_htslib", SCRIPT)
vendor = importlib.util.module_from_spec(spec)
spec.loader.exec_module(vendor)


class VendorMaintenanceTests(unittest.TestCase):
    def test_path_escape_rejected(self):
        for name in ("", ".", "C:/config.h", "../config.h", "/config.h", "nested/../../config.h", "nested\\config.h"):
            with self.assertRaises(ValueError):
                vendor.safe_name(name)
        self.assertEqual(vendor.safe_name("htslib/sam.h"), "htslib/sam.h")

    def test_checkout_newlines_not_a_patch(self):
        self.assertEqual(vendor.canonical(b"a\r\nb\r\n"), b"a\nb\n")
        binary = b"\x00a\r\n"
        self.assertEqual(vendor.canonical(binary), binary)

    def test_wrong_archive_fails_before_tar_or_queue_creation(self):
        with tempfile.TemporaryDirectory() as scratch:
            archive = Path(scratch) / "not-the-release.tar"
            archive.write_bytes(b"not an official release")
            queue = Path(scratch) / "must-not-exist"
            result = subprocess.run([sys.executable, str(SCRIPT), "--archive", str(archive),
                                     "--queue", str(queue), "--record"], capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("archive does not match dependencies.lock.json", result.stderr)
            self.assertFalse(queue.exists())


if __name__ == "__main__":
    unittest.main()
