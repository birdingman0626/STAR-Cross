"""Language-pair orchestration tests; real scientific parity remains a CI gate."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

import qualify_language_pair


class LanguagePairTests(unittest.TestCase):
    def exercise(self, initial="17", *, fail_science=False, change_source=False):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            build = root / "build"
            build.mkdir()
            cache = build / "CMakeCache.txt"
            cache.write_text(f"STAR_CXX_STANDARD:STRING={initial}\n")
            binary = build / "STAR"
            binary.write_bytes(f"binary-{initial}".encode())
            commands = []

            def run(command, **kwargs):
                self.assertTrue(kwargs["check"])
                commands.append(command)
                standard = next((item.split("=")[-1] for item in command
                                 if item.startswith("-DSTAR_CXX_STANDARD=")), None)
                if standard:
                    cache.write_text(f"STAR_CXX_STANDARD:STRING={standard}\n")
                if command[:2] == ["cmake", "--build"]:
                    binary.write_bytes(cache.read_text().encode())
                if fail_science and any("test_cpu_upstream.py" in item for item in command):
                    raise subprocess.CalledProcessError(1, command)

            signatures = [b"", b"source/CMakeLists.txt\0"] if change_source else [b"", b""]
            argv = ["qualify_language_pair.py", "--build-dir", str(build),
                    "--initial-standard", initial, "--parallel", "2",
                    "--build-target", "STAR", "star_tests", "star_seed_properties",
                    "--ctest-regex", "BAM|^seed_search_properties$",
                    "--evidence-root", str(root / "evidence")]
            initial_signature = root / "initial-source-signature.json"
            initial_signature.write_text("{}")
            argv += ["--initial-source-signature", str(initial_signature)]
            with patch.object(sys, "argv", argv), \
                    patch.object(subprocess, "run", side_effect=run), \
                    patch.object(subprocess, "check_output", side_effect=signatures):
                if fail_science:
                    with self.assertRaises(subprocess.CalledProcessError):
                        qualify_language_pair.main()
                elif change_source:
                    with self.assertRaisesRegex(RuntimeError, "Source changed"):
                        qualify_language_pair.main()
                else:
                    qualify_language_pair.main()
            evidence = next((root / "evidence").iterdir())
            result = json.loads((evidence / "result.json").read_text())
            self.assertEqual(result["status"], "UNVERIFIED" if fail_science or change_source else "PASS")
            self.assertTrue((evidence / "source-signature.json").is_file())
            self.assertIn("STAR_CXX_STANDARD:STRING=20", cache.read_text())
            saved = next(build.glob("*-reference"))
            self.assertEqual(saved.read_bytes(), f"binary-{initial}".encode())
            return commands, result

    def test_emulated_pair_only_builds_cxx20_once(self):
        commands, result = self.exercise()
        builds = [command for command in commands if command[:2] == ["cmake", "--build"]]
        self.assertEqual(len(builds), 1)
        self.assertEqual(builds[0][-4:], ["--target", "STAR", "star_tests", "star_seed_properties"])
        test = next(command for command in commands if command[0] == "ctest")
        self.assertIn("--no-tests=error", test)
        self.assertEqual(test[-2:], ["-R", "BAM|^seed_search_properties$"])
        science = next(command for command in commands if any("test_cpu_upstream.py" in item for item in command))
        self.assertIn("--ref-exe", science)
        self.assertEqual(science[-2:], ["--sa-sparse", "3"])
        self.assertEqual(result["final_standard"], "20")

    def test_native_pair_restores_cxx20(self):
        commands, _ = self.exercise("20")
        configurations = [item for command in commands for item in command if item.startswith("-DSTAR_CXX_STANDARD=")]
        self.assertEqual(configurations, ["-DSTAR_CXX_STANDARD=17", "-DSTAR_CXX_STANDARD=20"])

    def test_scientific_failure_is_fatal_and_unverified(self):
        self.exercise(fail_science=True)
        self.exercise("20", fail_science=True)

    def test_source_drift_is_fatal_and_unverified(self):
        self.exercise(change_source=True)

    def test_initial_build_source_drift_rejects_before_rebuild(self):
        with tempfile.TemporaryDirectory() as temp:
            build = Path(temp)
            (build / "CMakeCache.txt").write_text("STAR_CXX_STANDARD:STRING=17\n")
            initial = build / "initial.json"
            initial.write_text('{"source/changed.cpp": "old-hash"}')
            argv = ["qualify_language_pair.py", "--build-dir", str(build),
                    "--initial-standard", "17", "--initial-source-signature", str(initial)]
            with patch.object(sys, "argv", argv), \
                    patch.object(subprocess, "check_output", return_value=b""), \
                    patch.object(subprocess, "run") as run:
                with self.assertRaisesRegex(RuntimeError, "initial build began"):
                    qualify_language_pair.main()
                run.assert_not_called()


if __name__ == "__main__":
    unittest.main()
