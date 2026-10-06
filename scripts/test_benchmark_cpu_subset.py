"""Regression tests for real-subset benchmark gates; Linux/WSL only."""
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
import unittest


class BenchmarkGates(unittest.TestCase):
    def test_comparison_and_negative_gates(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            genome = root/"genome_cynomolgus"
            genome.mkdir()
            for name in ("Genome", "SA", "SAindex", "genomeParameters.txt",
                         "Macaca_fascicularis_6.0.115.cellranger_filtered.gtf"):
                (genome/name).write_text("reference")
            (root/"whitelists").mkdir()
            (root/"whitelists/3M-february-2018.txt").write_text("barcode")
            fixture = root/"fixture"
            fixture.mkdir()
            outputs = []
            for mate in (1, 2):
                path = fixture/f"R{mate}.fastq"
                path.write_bytes(b"fixture\n")
                outputs.append({"name": path.name, "sha256": hashlib.sha256(path.read_bytes()).hexdigest()})
            (fixture/"manifest.json").write_text(json.dumps({"status": "COMPLETE", "pairs": 1,
                "sampling": "first_N_synchronized_pairs", "outputs": outputs}))
            binary = root/"STAR"
            binary.write_text('''#!/usr/bin/env python3
import pathlib, sys
out = pathlib.Path(sys.argv[sys.argv.index("--outFileNamePrefix")+1])
(out/"Log.final.out").write_text("Number of input reads | 1\\n")
(out/"SJ.out.tab").write_text("fixture\\n")
for feature in ("Gene", "GeneFull_Ex50pAS", "Velocyto"):
    raw = out/"Solo.out"/feature/"raw"
    raw.mkdir(parents=True)
    matrices = ("spliced.mtx", "unspliced.mtx", "ambiguous.mtx") if feature == "Velocyto" else ("matrix.mtx",)
    for name in (*matrices, "features.tsv", "barcodes.tsv"):
        if out.name == "missing" and name == "ambiguous.mtx": continue
        if name.endswith(".mtx"):
            if out.name == "malformed":
                (raw/name).write_text("malformed")
                continue
            value=2 if out.name == "different" and name == "spliced.mtx" else 1
            rows=f"1 1 {value}\\n2 2 1\\n"
            if out.name == "reordered": rows=f"2 2 1\\n1 1 {value}\\n"
            (raw/name).write_text("%%MatrixMarket matrix coordinate integer general\\n2 2 2\\n"+rows)
        else:
            (raw/name).write_text("axis1\\naxis2\\n")
            if out.name == "wrong-axis": (raw/name).write_text("axis1\\n")
''')
            binary.chmod(0o755)
            script = Path(__file__).with_name("benchmark_cpu_subset.py")
            for label, status, code in [("same", "PASSED_DECLARED_RAW_ARTIFACTS", 0),
                                         ("reordered", "PASSED_DECLARED_RAW_ARTIFACTS", 0),
                                         ("different", "DIFFERENCES_REQUIRE_REVIEW", 2),
                                         ("malformed", "FAILED_VALIDATION", 1),
                                         ("wrong-axis", "FAILED_VALIDATION", 1),
                                         ("missing", "FAILED_VALIDATION", 1)]:
                output = root/label
                result = subprocess.run(["python3", str(script), "--fixture", str(fixture), "--data-dir", str(root),
                                         "--output-dir", str(output), "--binary", f"baseline={binary}",
                                         "--binary", f"{label}={binary}"], capture_output=True, text=True)
                self.assertEqual(result.returncode, code, result.stderr)
                record = json.loads((output/"result.json").read_text())
                self.assertEqual(record["status"], status)
                self.assertTrue((output/"runner_snapshot.py").is_file())
            reused = root/"reuse"
            result = subprocess.run(["python3", str(script), "--fixture", str(fixture), "--data-dir", str(root),
                                     "--output-dir", str(reused), "--reference-run", str(root/"same/baseline"),
                                     "--binary", f"same={binary}"], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            receipt = root/"same/baseline/input_signatures.json"
            original_receipt = receipt.read_text()
            receipt.unlink()
            result = subprocess.run(["python3", str(script), "--fixture", str(fixture), "--data-dir", str(root),
                                     "--output-dir", str(root/"unverified"), "--reference-run", str(root/"same/baseline"),
                                     "--binary", f"same={binary}"], capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(json.loads((root/"unverified/result.json").read_text())["status"], "FAILED_VALIDATION")
            receipt.write_text(original_receipt)
            binary_text = binary.read_text()
            binary.write_text(binary_text+"\n# changed binary\n")
            result = subprocess.run(["python3", str(script), "--fixture", str(fixture), "--data-dir", str(root),
                                     "--output-dir", str(root/"changed-binary"), "--reference-run", str(root/"same/baseline"),
                                     "--binary", f"same={binary}"], capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(json.loads((root/"changed-binary/result.json").read_text())["status"], "FAILED_VALIDATION")
            binary.write_text(binary_text)
            (genome/"SA").write_text("changed reference")
            result = subprocess.run(["python3", str(script), "--fixture", str(fixture), "--data-dir", str(root),
                                     "--output-dir", str(root/"changed-input"), "--reference-run", str(root/"same/baseline"),
                                     "--binary", f"same={binary}"], capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(json.loads((root/"changed-input/result.json").read_text())["status"], "FAILED_VALIDATION")
            (genome/"SA").write_text("reference")
            (root/"same/baseline/Solo.out/Gene/raw/matrix.mtx").write_text("stale")
            result = subprocess.run(["python3", str(script), "--fixture", str(fixture), "--data-dir", str(root),
                                     "--output-dir", str(root/"stale"), "--reference-run", str(root/"same/baseline"),
                                     "--binary", f"same={binary}"], capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(json.loads((root/"stale/result.json").read_text())["status"], "FAILED_VALIDATION")
            (fixture/"R1.fastq").write_bytes(b"corrupt")
            result = subprocess.run(["python3", str(script), "--fixture", str(fixture), "--data-dir", str(root),
                                     "--output-dir", str(root/"corrupt"), "--binary", f"a={binary}",
                                     "--binary", f"b={binary}"], capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertFalse((root/"corrupt").exists())


if __name__ == "__main__":
    unittest.main()
