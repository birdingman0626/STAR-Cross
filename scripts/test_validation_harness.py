#!/usr/bin/env python3
"""Fail-closed regression tests for the historical Bash validation profiles (Linux/WSL)."""
from pathlib import Path
import os
import re
import shutil
import subprocess
import tempfile
import unittest

SCRIPTS = Path(__file__).resolve().parent


@unittest.skipUnless(os.name == "posix" and shutil.which("bash"), "Bash profiles require Linux/WSL paths")
class ValidationHarness(unittest.TestCase):
    def setUp(self):
        self.root = Path(tempfile.mkdtemp(prefix="star-harness-tests-"))
        self.addCleanup(shutil.rmtree, self.root)

    def invoke(self, script, *args):
        result = subprocess.run(["bash", str(SCRIPTS / script), *map(str, args)],
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        return result.returncode, result.stdout

    def test_genome_equivalence_rejects_self_comparison(self):
        binary = self.root / "STAR"
        binary.write_bytes(b"same binary")
        script = SCRIPTS.parent / "extras/tests/scripts/validate_genome_equivalence.sh"
        result = subprocess.run(["bash", str(script), str(binary), str(binary), str(self.root / "work")],
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 2)
        self.assertIn("identical", result.stderr)

    def test_release_requires_all_reference_and_candidate_files(self):
        original, candidate = self.root / "reference", self.root / "candidate"
        files = re.findall(r"^  (Solo\.out/[^\s]+)", (SCRIPTS / "release_compare.sh").read_text(), re.M)
        self.assertEqual(len(files), 21)
        self.assertNotEqual(self.invoke("release_compare.sh", original, candidate)[0], 0)
        for directory in (original, candidate):
            for file in files:
                path = directory / file
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("fixture\n")
        self.assertEqual(self.invoke("release_compare.sh", original, candidate)[0], 0)
        target = candidate / files[0]
        target.write_text("changed\n")
        self.assertNotEqual(self.invoke("release_compare.sh", original, candidate)[0], 0)
        target.unlink()
        self.assertNotEqual(self.invoke("release_compare.sh", original, candidate)[0], 0)
        (original / files[1]).unlink()
        self.assertNotEqual(self.invoke("release_compare.sh", original, candidate)[0], 0)

    def test_validate_requires_tests_inputs_references_and_uses_reference_binary(self):
        build = self.root / "build"
        build.mkdir()
        binary = build / "STAR"
        binary.write_text('''#!/usr/bin/env python3
import pathlib, sys
if "--version" in sys.argv:
    print("STAR fixture")
    raise SystemExit(0)
out = pathlib.Path(sys.argv[sys.argv.index("--outFileNamePrefix")+1])
(out / "Log.final.out").write_text("Number of input reads | 1\\n")
for feature in ("Gene", "GeneFull_Ex50pAS"):
    folder = out / "Solo.out" / feature / "raw"
    folder.mkdir(parents=True, exist_ok=True)
    for artifact in ("matrix.mtx", "features.tsv", "barcodes.tsv"):
        (folder / artifact).write_text("fixture\\n")
''')
        binary.chmod(0o755)
        data = self.root / "data"
        args = ["--star-exe", binary, "--data-dir", data]
        self.assertNotEqual(self.invoke("validate_build.sh", *args)[0], 0) # absent CTest
        tests = build / "test"
        tests.mkdir()
        (tests / "CTestTestfile.cmake").write_text("")
        self.assertNotEqual(self.invoke("validate_build.sh", *args)[0], 0) # zero discovered tests
        (tests / "CTestTestfile.cmake").write_text('add_test(fixture "/bin/true")\n')
        self.assertNotEqual(self.invoke("validate_build.sh", *args)[0], 0) # absent reads
        for name in ["fastq/R1_1M.fastq", "fastq/R2_1M.fastq", "genome_cynomolgus/Genome",
                     "genome_cynomolgus/Macaca_fascicularis_6.0.115.cellranger_filtered.gtf",
                     "whitelists/3M-february-2018.txt"]:
            path = data / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("fixture\n")
        self.assertNotEqual(self.invoke("validate_build.sh", *args)[0], 0) # absent references
        code, text = self.invoke("validate_build.sh", *args, "--ref-exe", binary)
        self.assertEqual(code, 0, text)
        retained = Path(re.search(r"Evidence retained at: (.+)", text)[1])
        self.addCleanup(shutil.rmtree, retained)
        self.assertTrue((retained / "reference/Solo.out/Gene/raw/matrix.mtx").exists())
        self.assertTrue((retained / "candidate/Solo.out/Gene/raw/matrix.mtx").exists())
        binary.write_text(binary.read_text().replace("reads | 1", "reads | 0"))
        code, text = self.invoke("validate_build.sh", *args, "--ref-exe", binary)
        self.assertNotEqual(code, 0, text)
        retained = Path(re.search(r"Evidence retained at: (.+)", text)[1])
        self.addCleanup(shutil.rmtree, retained)


if __name__ == "__main__":
    unittest.main()
