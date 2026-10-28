"""Self-tests for scripts/local_ci.py (no network, no PyYAML needed)."""
import subprocess
import shutil
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import local_ci as lc  # noqa: E402

WORKFLOW = {
    "env": {"TOP": "t"},
    "jobs": {
        "build": {
            "runs-on": "${{ matrix.os }}",
            "env": {"SHA": "abc", "COPY": "${{ env.SHA }}-x"},
            "strategy": {"matrix": {"os": ["ubuntu-24.04", "ubuntu-24.04-arm"], "compiler": ["gcc", "clang"],
                                    "exclude": [{"os": "ubuntu-24.04-arm", "compiler": "clang"}]}},
            "steps": [
                {"uses": "actions/checkout@v7"},
                {"name": "Only gcc", "if": "matrix.compiler == 'gcc' && matrix.os != 'ubuntu-24.04-arm'", "run": "echo gcc"},
                {"name": "Build", "env": {"OPT": "a \"b\" $c ${{ github.workspace }}/x"}, "run": "echo ${{ matrix.compiler }} > out.txt"},
                {"name": "On failure", "if": "${{ failure() }}", "run": "exit 1"},
                {"name": "Baseline", "uses": "actions/checkout@v7", "with": {"ref": "${{ env.SHA }}", "path": "baseline"}},
                {"uses": "actions/upload-artifact@v7"},
            ],
        },
        "reusable": {"uses": "./.github/workflows/other.yml"},
    },
}


class ExpressionTests(unittest.TestCase):
    def test_operators_and_contexts(self):
        ctx = {"matrix": {"a": "x", "p": {"arch": "arm"}}}
        self.assertTrue(lc.evaluate("matrix.a == 'x' && matrix.p.arch == 'arm'", ctx))
        self.assertTrue(lc.evaluate("matrix.a != 'y' || failure()", ctx))
        self.assertFalse(lc.evaluate("!(matrix.a == 'x')", ctx))
        self.assertEqual(lc.evaluate("matrix.missing", ctx), "")
        self.assertEqual(lc.evaluate("'it''s'", ctx), "it's")
        self.assertTrue(lc.evaluate("always()", ctx))

    def test_rejects_code(self):
        for bad in ("__import__('os')", "matrix.a; x", "fromJSON('1')"):
            with self.assertRaises(ValueError):
                lc.evaluate(bad, {})

    def test_substitute(self):
        self.assertEqual(lc.substitute("cc=${{ matrix.c }}!", {"matrix": {"c": "gcc"}}), "cc=gcc!")


class WorkflowTests(unittest.TestCase):
    def setUp(self):
        self.lanes = {l["id"]: l for l in lc.lanes_from_workflow(WORKFLOW)}

    def test_matrix_expansion_exclude_and_runs_on(self):
        self.assertEqual(sorted(self.lanes), ["build-ubuntu-24.04-arm-gcc", "build-ubuntu-24.04-clang", "build-ubuntu-24.04-gcc"])
        self.assertEqual(self.lanes["build-ubuntu-24.04-arm-gcc"]["runs_on"], "ubuntu-24.04-arm")
        self.assertEqual(self.lanes["build-ubuntu-24.04-gcc"]["env"], {"TOP": "t", "SHA": "abc", "COPY": "abc-x"})

    def test_include_only_matrix(self):
        self.assertEqual(lc.expand_matrix({"matrix": {"include": [{"os": "a"}, {"os": "b"}]}}), [{"os": "a"}, {"os": "b"}])
        self.assertEqual(lc.expand_matrix(None), [{}])

    def test_plan_conditions_actions_and_overrides(self):
        lane = self.lanes["build-ubuntu-24.04-clang"]
        plan = {p[0]: p for p in lc.plan_steps(lane, {}, "https://example.invalid/r.git")}
        self.assertEqual(plan["Only gcc"][1], "skip")
        self.assertEqual(plan["On failure"][1], "skip")
        self.assertEqual(plan["Build"][2], "echo clang > out.txt")
        self.assertIn("fetch -q --depth 1 https://example.invalid/r.git abc", plan["Baseline"][2])
        self.assertEqual(plan["actions/upload-artifact@v7"][1], "skip")
        host = {"step_overrides": {"build/Build": "echo replaced", "Only gcc": None}}
        plan = {p[0]: p for p in lc.plan_steps(self.lanes["build-ubuntu-24.04-gcc"], host, "u")}
        self.assertEqual(plan["Build"][2], "echo replaced")
        self.assertIn("override", plan["Build"][4])
        self.assertEqual(plan["Only gcc"][1], "skip")

    @unittest.skipUnless(shutil.which("bash") and sys.platform != "win32", "needs a POSIX bash")
    def test_generated_driver_runs(self):
        lane = self.lanes["build-ubuntu-24.04-gcc"]
        lane = dict(lane, steps=[s for s in lane["steps"] if s.get("name") != "Baseline"])
        files = lc.bash_bundle_files(lane, {"env": {"HOSTVAR": "$HOME"}}, lc.plan_steps(lane, {}, "u"))
        with tempfile.TemporaryDirectory() as d:
            for rel, text in files.items():
                p = Path(d, rel)
                p.parent.mkdir(parents=True, exist_ok=True)
                p.write_text(text)
            Path(d, ".local-ci/steps/03.sh").write_text(files[".local-ci/steps/03.sh"] + 'printf "%s" "$OPT" > opt.txt\n')
            out = subprocess.run(["bash", str(Path(d, ".local-ci/run.sh"))], capture_output=True, text=True)
            self.assertEqual(out.returncode, 0, out.stdout + out.stderr)
            self.assertEqual(Path(d, "out.txt").read_text().strip(), "gcc")
            opt = Path(d, "opt.txt").read_text()
            self.assertTrue(opt.startswith('a "b" $c ') and opt.endswith("/x"), opt)
            self.assertEqual(Path(opt[len('a "b" $c '):-2]).resolve(), Path(d).resolve())
            self.assertIn("job passed", out.stdout)


if __name__ == "__main__":
    unittest.main()
