"""Sparse comparison validates malformed data and does not confuse order with counts."""
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).parent))
from compare_raw_counts import matrix


class MatrixValidation(unittest.TestCase):
    def test_order_duplicates_and_zero(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp)/"matrix.mtx"
            path.write_text("%%MatrixMarket matrix coordinate integer general\n% comment\n2 3 4\n"
                            "2 3 4\n1 1 2\n2 3 1\n1 2 0\n")
            shape, counts = matrix(path)
            self.assertEqual(shape, (2, 3))
            self.assertEqual(counts, {(2, 3): 5, (1, 1): 2, (1, 2): 0})

    def test_invalid_inputs_fail(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp)/"matrix.mtx"
            header = "%%MatrixMarket matrix coordinate integer general\n"
            for body in ("-1 3 0\n", "2 3 1\n", "2 3 1\n3 1 2\n",
                         "2 3 1\n1 1 -2\n", "2 3 0\n1 1 2\n", "2 3\n"):
                with self.subTest(body=body):
                    path.write_text(header+body)
                    with self.assertRaises(ValueError):
                        matrix(path)
            path.write_text("%%MatrixMarket matrix coordinate real general\n2 3 0\n")
            with self.assertRaises(ValueError):
                matrix(path)


if __name__ == "__main__":
    unittest.main()
