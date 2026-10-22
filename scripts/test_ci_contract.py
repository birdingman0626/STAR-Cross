"""Regression checks for the repository's deliberately simple release gate layout."""
from pathlib import Path
import json
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


def cxx20_only_contract(cmake, makefile, presets, build, s390x):
    """C++20 is the only language level: no knob may select or test another."""
    if "set(CMAKE_CXX_STANDARD 20)" not in cmake or 'NOT STAR_CXX_STANDARD STREQUAL "20"' not in cmake:
        raise ValueError("CMake must fix C++20 and reject other requested standards")
    if "CXX_STANDARD" in makefile:
        raise ValueError("Makefile must not offer a language-level override")
    for preset in json.loads(presets)["configurePresets"]:
        if "STAR_CXX_STANDARD" in preset.get("cacheVariables", {}):
            raise ValueError(f"Preset {preset['name']} selects a language level")
    for text, where in ((build, "build.yml"), (s390x, "build_validate_s390x.sh")):
        if re.search(r"STAR_CXX_STANDARD|matrix\.standard|standard: \[|qualify_language_pair", text):
            raise ValueError(f"{where} still configures or compares language levels")


class CIContractTests(unittest.TestCase):
    def test_cxx20_only(self):
        root = Path(__file__).resolve().parents[1]
        files = [(root/name).read_text(encoding="utf-8") for name in
                 ("source/CMakeLists.txt", "source/Makefile", "source/CMakePresets.json",
                  ".github/workflows/build.yml", "extras/tests/scripts/build_validate_s390x.sh")]
        cxx20_only_contract(*files)
        cmake, makefile, presets, build, s390x = files
        for broken in ((cmake.replace("set(CMAKE_CXX_STANDARD 20)", "set(CMAKE_CXX_STANDARD 17)"), makefile, presets, build, s390x),
                       (cmake, makefile + "\nCXX_STANDARD ?= 17\n", presets, build, s390x),
                       (cmake, makefile, presets.replace('"STAR_USE_AVX2": "OFF"', '"STAR_USE_AVX2": "OFF", "STAR_CXX_STANDARD": "17"'), build, s390x),
                       (cmake, makefile, presets, build.replace("compiler: [gcc, clang]", "compiler: [gcc, clang]\n        standard: [17, 20]"), s390x),
                       (cmake, makefile, presets, build, s390x + "\ncmake -B build -DSTAR_CXX_STANDARD=17\n")):
            with self.assertRaises(ValueError):
                cxx20_only_contract(*broken)

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
