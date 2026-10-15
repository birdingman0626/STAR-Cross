import gzip
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
from qualification import bam_signature, measured_run, paired_assessment, background_environment_pass
from qualification import calibration_profile_matches, performance_acceptance, machine_identity, execution_environment


class QualificationTests(unittest.TestCase):
    def test_busy_admission_preflight_never_launches_star(self):
        import benchmark_cpu_subset as benchmark
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            fixture = root/"fixture"
            fixture.mkdir()
            outputs = []
            for name in ("R1.fastq", "R2.fastq"):
                (fixture/name).write_bytes(b"fixture")
                outputs.append(dict(name=name, sha256=hashlib.sha256(b"fixture").hexdigest()))
            (fixture/"manifest.json").write_text(json.dumps(dict(status="COMPLETE", pairs=1, sampling="test", outputs=outputs)))
            (root/"whitelists").mkdir()
            (root/"whitelists/3M-february-2018.txt").write_text("barcode")
            genome = root/"genome_cynomolgus"
            genome.mkdir()
            for name in ("Genome", "SA", "SAindex", "genomeParameters.txt", "Macaca_fascicularis_6.0.115.cellranger_filtered.gtf"):
                (genome/name).write_text("reference")
            argv = ["benchmark", "--fixture", str(fixture), "--data-dir", str(root), "--output-dir", str(root/"out"),
                    "--binary", f"a={sys.executable}", "--binary", f"b={sys.executable}", "--rounds", "5", "--full-contract"]
            busy = dict(background_cpu_samples=10, background_cpu_scope_verified=True,
                        background_cpu_mean_percent=20, background_cpu_p95_percent=30)
            with patch.object(sys, "argv", argv), patch.object(sys, "platform", "win32"), \
                    patch.object(benchmark, "machine_identity", return_value={"test": "host"}), \
                    patch.object(benchmark, "measured_run", return_value=(0, busy)) as measured:
                with self.assertRaises(SystemExit) as error:
                    benchmark.main()
                self.assertEqual(error.exception.code, 2)
                self.assertEqual(measured.call_count, 1)
                self.assertIn("time.sleep(10)", measured.call_args.args[0][-1])
            receipt = json.loads((root/"out/result.json").read_text())
            self.assertEqual(receipt["status"], "UNVERIFIED_ENVIRONMENT_PREFLIGHT")
            self.assertEqual(receipt["runs"], [])

    def test_background_gate(self):
        quiet = dict(warmup=False, background_cpu_samples=3,
                     background_cpu_scope_verified=True,
                     background_cpu_mean_percent=2, background_cpu_p95_percent=5)
        self.assertTrue(background_environment_pass([quiet], 5))
        for runs in ([], [dict(warmup=False)], [dict(quiet, background_cpu_p95_percent=6)],
                     [dict(quiet, background_cpu_mean_percent=float("nan"))], [dict(quiet, warmup=True)],
                     [dict(quiet, background_cpu_mean_percent=-1)],
                     [dict(quiet, background_cpu_scope_verified=False)]):
            self.assertFalse(background_environment_pass(runs, 5))
    def test_pair_gates(self):
        def runs(n, ratio=1, same=True, measured=True):
            return [dict(label=label, round=i, warmup=False, wall_seconds=100*(ratio if label == "b" else 1),
                         binary_sha256="a" if same or label == "a" else "b",
                         **({"peak_rss_bytes": 100000000} if measured else {}))
                    for i in range(n) for label in ("a", "b")]
        self.assertEqual(paired_assessment(runs(5), ["a", "b"])["status"], "CALIBRATION_PASS")
        for values in (runs(4), runs(5, 1.03), runs(5, measured=False)):
            self.assertEqual(paired_assessment(values, ["a", "b"])["status"], "INCONCLUSIVE")
        noisy = runs(5)
        for i, ratio in enumerate((0.9, 1.1, 0.9, 1.1, 1.0)):
            noisy[i*2+1]["wall_seconds"] *= ratio
        self.assertEqual(paired_assessment(noisy, ["a", "b"])["status"], "INCONCLUSIVE")
        for i, ratio in enumerate((1, 1, 1, 2, 0.5)):
            noisy[i*2+1]["wall_seconds"] = 100*ratio
        self.assertEqual(paired_assessment(noisy, ["a", "b"])["status"], "INCONCLUSIVE")
        self.assertEqual(paired_assessment(runs(10, 1.1, False), ["a", "b"])["status"], "NONREGRESSION_NOT_ESTABLISHED")
        values = runs(10, 0.9, False)
        self.assertTrue(paired_assessment(values, ["a", "b"])["requires_independent_AA_calibration"])
        values[0]["wall_seconds"] = 0
        with self.assertRaises(ValueError):
            paired_assessment(values, ["a", "b"])

    def test_performance_requires_science_and_calibration_identity(self):
        profile = dict(legacy=False, threads=8, pairs=1000, machine_identity=machine_identity(),
                       affinity_mask=3,
                       execution_environment=execution_environment(),
                       runner_sha256="runner", count_comparator_sha256="comparator", helper_sha256={"helper": "sha"})
        self.assertTrue(calibration_profile_matches(profile, dict(profile)))
        for key in profile:
            changed = dict(profile)
            changed[key] = None
            self.assertFalse(calibration_profile_matches(changed, profile))
            del changed[key]
            self.assertFalse(calibration_profile_matches(changed, profile))
        self.assertEqual(performance_acceptance("DIFFERENCES_REQUIRE_REVIEW", "TIME_NONREGRESSION_PASS", True, True),
                         "UNVERIFIED_SCIENTIFIC_CONTRACT")
        self.assertEqual(performance_acceptance("PASSED_DECLARED_RAW_ARTIFACTS", "TIME_NONREGRESSION_PASS", True, True),
                         "NONREGRESSION_PASS")
        self.assertNotEqual(performance_acceptance("PASSED_DECLARED_RAW_ARTIFACTS", "TIME_NONREGRESSION_PASS", True),
                            "NONREGRESSION_PASS")
        self.assertNotEqual(performance_acceptance("PASSED_DECLARED_RAW_ARTIFACTS", "NONREGRESSION_NOT_ESTABLISHED", True, True),
                            "NONREGRESSION_PASS")

    def test_bam_order_multiplicity_and_truncation(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp)/"a.bam"
            header = b"BAM\1"+struct.pack("<ii", 0, 0)
            records = [bytes([value])*32 for value in (1, 2, 1)]
            def write(values):
                with gzip.open(path, "wb") as out:
                    out.write(header+b"".join(struct.pack("<i", len(record))+record for record in values))
            write(records)
            first = bam_signature(path)
            write(list(reversed(records)))
            self.assertEqual(first, bam_signature(path))
            write(records[:2])
            self.assertNotEqual(first, bam_signature(path))
            with gzip.open(path, "wb") as out:
                out.write(header+b"\x20\x00")
            with self.assertRaises(ValueError):
                bam_signature(path)

    @unittest.skipUnless(sys.platform == "win32", "Windows process memory API")
    def test_windows_affinity_and_phase_observations(self):
        import ctypes
        from ctypes import wintypes
        kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        kernel.GetCurrentProcess.restype = wintypes.HANDLE
        kernel.GetProcessAffinityMask.argtypes = [wintypes.HANDLE, ctypes.POINTER(ctypes.c_size_t), ctypes.POINTER(ctypes.c_size_t)]
        allowed, system = ctypes.c_size_t(), ctypes.c_size_t()
        self.assertTrue(kernel.GetProcessAffinityMask(kernel.GetCurrentProcess(), ctypes.byref(allowed), ctypes.byref(system)))
        mask = allowed.value & -allowed.value
        with tempfile.TemporaryDirectory() as temp:
            with (Path(temp)/"console.log").open("w") as out:
                code, memory = measured_run([sys.executable, "-c",
                    "import time; print('..... started mapping', flush=True); time.sleep(0.4); print('..... finished mapping', flush=True)"],
                    out, temp, affinity_mask=mask)
            self.assertEqual(code, 0)
            self.assertEqual(memory["requested_affinity_mask"], mask)
            self.assertEqual([x["phase"] for x in memory["phase_observations"]], ["started mapping", "finished mapping"])

    @unittest.skipUnless(sys.platform == "win32", "Windows process memory API")
    def test_short_lived_windows_peak(self):
        with tempfile.TemporaryDirectory() as temp:
            with (Path(temp)/"console.log").open("w") as out:
                code, memory = measured_run([sys.executable, "-c", "x=bytearray(32*1024*1024)"], out, temp)
            self.assertEqual(code, 0)
            self.assertGreater(memory["peak_rss_bytes"], 32*1024*1024)
            self.assertGreaterEqual(memory["process_cpu_seconds"], 0)
            self.assertTrue(memory["cpu_samples"])
            self.assertGreaterEqual(memory["io_read_bytes"], 0)
