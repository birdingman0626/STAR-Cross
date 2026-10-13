import gzip
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
from qualification import bam_signature, measured_run, paired_assessment, background_environment_pass
from qualification import calibration_profile_matches, performance_acceptance, machine_identity, execution_environment


class QualificationTests(unittest.TestCase):
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
        self.assertEqual(paired_assessment(runs(10, 1.1, False), ["a", "b"])["status"], "TIME_REGRESSION")
        values = runs(10, 0.9, False)
        self.assertTrue(paired_assessment(values, ["a", "b"])["requires_independent_AA_calibration"])
        values[0]["wall_seconds"] = 0
        with self.assertRaises(ValueError):
            paired_assessment(values, ["a", "b"])

    def test_performance_requires_science_and_calibration_identity(self):
        profile = dict(legacy=False, threads=8, pairs=1000, machine_identity=machine_identity(),
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
    def test_short_lived_windows_peak(self):
        with tempfile.TemporaryDirectory() as temp:
            with (Path(temp)/"console.log").open("w") as out:
                code, memory = measured_run([sys.executable, "-c", "x=bytearray(32*1024*1024)"], out, temp)
            self.assertEqual(code, 0)
            self.assertGreater(memory["peak_rss_bytes"], 32*1024*1024)
