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
    for name in ("build-linux", "build-macos", "build-windows", "build-s390x", "validate-genome-index", "asan-sjdb"):
        job(build, name)
    bigendian = job(build, "build-s390x")
    if re.search(r"^    (if:|continue-on-error:)", bigendian, re.M):
        raise ValueError("Big-endian qualification cannot be optional")
    if "bash extras/tests/scripts/build_validate_s390x.sh" not in bigendian:
        raise ValueError("Big-endian validation harness missing")
    if "uses: ./.github/workflows/build.yml" not in job(release, "qualification"):
        raise ValueError("Release does not qualify its own SHA")
    publication = job(release, "release")
    if re.search(r"^    if:", publication, re.M):
        raise ValueError("Publication must retain default successful-needs gate")
    match = re.search(r"^    needs: \[([^\]]+)\]", publication, re.M)
    if not match or not {"qualification", "build"}.issubset(set(map(str.strip, match.group(1).split(",")))):
        raise ValueError("Publication can bypass required verification")
    if "pattern: STAR-*" not in publication:
        raise ValueError("Qualification artifacts not isolated from release assets")
    artifact = "name: qualification-STAR-linux-s390x"
    if artifact not in bigendian or artifact not in publication:
        raise ValueError("Validated big-endian artifact not connected to publication")


class CIContractTests(unittest.TestCase):
    def test_actual_and_bypass_negative_controls(self):
        root = Path(__file__).resolve().parents[1]/".github/workflows"
        build = (root/"build.yml").read_text()
        release = (root/"release.yml").read_text()
        release_contract(build, release)
        for broken in (release.replace("needs: [build, qualification]", "needs: [build]"),
                       release.replace("  release:\n", "  release:\n    if: always()\n"),
                       release.replace("uses: ./.github/workflows/build.yml", "uses: external/workflow@main"),
                       release.replace("name: qualification-STAR-linux-s390x", "name: unvalidated-s390x")):
            with self.assertRaises(ValueError):
                release_contract(build, broken)
        for broken in (build.replace("  build-s390x:\n", "  removed-s390x:\n"),
                       build.replace("  build-s390x:\n", "  build-s390x:\n    continue-on-error: true\n"),
                       build.replace("bash extras/tests/scripts/build_validate_s390x.sh", "true"),
                       build.replace("name: qualification-STAR-linux-s390x", "name: unvalidated-s390x")):
            with self.assertRaises(ValueError):
                release_contract(broken, release)


if __name__ == "__main__":
    unittest.main()
