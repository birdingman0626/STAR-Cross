"""Regression checks for the repository's deliberately simple release gate layout."""
from pathlib import Path
import re
import unittest


def release_contract(build, release):
    def job(text, name):
        found = re.search(rf"(?ms)^  {re.escape(name)}:\n(.*?)(?=^  [\w-]+:\n|\Z)", text)
        if not found:
            raise ValueError(f"Required job missing: {name}")
        return found.group(1)
    if not re.search(r"^  workflow_call:", build, re.M):
        raise ValueError("Qualification workflow not reusable")
    for name in ("build-linux", "build-macos", "build-windows", "validate-genome-index", "asan-sjdb"):
        job(build, name)
    if "uses: ./.github/workflows/build.yml" not in job(release, "qualification"):
        raise ValueError("Release does not qualify its own SHA")
    publication = job(release, "release")
    if re.search(r"^    if:", publication, re.M):
        raise ValueError("Publication must retain default successful-needs gate")
    match = re.search(r"^    needs: \[([^\]]+)\]", publication, re.M)
    if not match or not {"qualification", "build", "bigendian"}.issubset(set(map(str.strip, match.group(1).split(",")))):
        raise ValueError("Publication can bypass required verification")
    if "pattern: STAR-*" not in publication:
        raise ValueError("Qualification artifacts not isolated from release assets")


class CIContractTests(unittest.TestCase):
    def test_actual_and_bypass_negative_controls(self):
        root = Path(__file__).resolve().parents[1]/".github/workflows"
        build = (root/"build.yml").read_text()
        release = (root/"release.yml").read_text()
        release_contract(build, release)
        for broken in (release.replace("needs: [build, bigendian, qualification]", "needs: [build, bigendian]"),
                       release.replace("  release:\n", "  release:\n    if: always()\n"),
                       release.replace("uses: ./.github/workflows/build.yml", "uses: external/workflow@main")):
            with self.assertRaises(ValueError):
                release_contract(build, broken)


if __name__ == "__main__":
    unittest.main()
