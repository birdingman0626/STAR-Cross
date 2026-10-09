import gzip
from pathlib import Path
import tempfile
import unittest
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from subset_fastq_pairs import subset


class PairedPrefix(unittest.TestCase):
    def test_prefix_preserves_headers_and_checks_pairs(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            for mate in (1, 2):
                with gzip.open(root / f"r{mate}.gz", "wb") as stream:
                    stream.write(f"@a/{mate} {mate}:N:0:INDEX\r\nACGT\r\n+\r\nIIII\r\n@b/{mate}\nAA\n+\nII\n".encode())
            result = subset(root/"r1.gz", root/"r2.gz", root/"out", 1)
            self.assertEqual(result["pairs"], 1)
            self.assertEqual(result["outputs"][0]["read_lengths"], [4])
            self.assertIn(b"1:N:0:INDEX\r\n", (root/"out/R1.fastq").read_bytes())
            self.assertNotIn(b"@b", (root/"out/R1.fastq").read_bytes())
            with self.assertRaises(FileExistsError):
                subset(root/"r1.gz", root/"r2.gz", root/"out", 1)
            with self.assertRaises(ValueError):
                subset(root/"r1.gz", root/"r2.gz", root/"short", 3)
            with gzip.open(root/"r2.gz", "wb") as stream:
                stream.write(b"@other/2\nACGT\n+\nIIII\n")
            with self.assertRaises(ValueError):
                subset(root/"r1.gz", root/"r2.gz", root/"mismatch", 1)
            self.assertFalse((root/"mismatch/manifest.json").exists())

    def test_bad_quality_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            for mate in (1, 2):
                with gzip.open(root/f"r{mate}.gz", "wb") as stream:
                    stream.write(b"@a\nACGT\n+\nII\n")
            with self.assertRaises(ValueError):
                subset(root/"r1.gz", root/"r2.gz", root/"out", 1)

    def test_offset_selects_segment_and_validates_skipped_pairs(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp)
            for mate in (1,2):
                (root/f'r{mate}').write_bytes(f'@a/{mate}\nAC\n+\nII\n@b/{mate}\nGT\n+\nII\n'.encode())
            result=subset(root/'r1',root/'r2',root/'out',1,1)
            self.assertEqual(result['first_source_pair'],2)
            self.assertEqual((root/'out/R1.fastq').read_bytes(),b'@b/1\nGT\n+\nII\n')
            with self.assertRaises(ValueError):subset(root/'r1',root/'r2',root/'short',1,2)
            (root/'r2').write_bytes(b'@different/2\nAC\n+\nII\n@b/2\nGT\n+\nII\n')
            with self.assertRaises(ValueError):subset(root/'r1',root/'r2',root/'mismatch',1,1)


if __name__ == "__main__":
    unittest.main()
