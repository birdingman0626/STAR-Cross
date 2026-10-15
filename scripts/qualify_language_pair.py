#!/usr/bin/env python3
"""Compare same-source C++20 and C++17 with the same CMake cache/toolchain.

Normally starts in a tested C++20 build and restores C++20 before returning.
An already tested C++17 baseline can instead transition once to C++20, avoiding
a third emulated build. In both modes the final build is C++20.
Builds are sequential: never share a CMake cache between concurrent processes.
"""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import hashlib


def source_signature(source):
    names = subprocess.check_output(["git", "ls-files", "--cached", "--others", "--exclude-standard", "-z"], cwd=source.parent).decode().split("\0")
    return {name: hashlib.sha256((source.parent/name).read_bytes()).hexdigest()
            for name in sorted(set(names)) if name.startswith(("source/", "test/")) and (source.parent/name).is_file()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", required=True, type=Path)
    parser.add_argument("--initial-standard", choices=("17", "20"), default="20")
    parser.add_argument("--parallel", type=int, default=4)
    parser.add_argument("--build-target", nargs="+", help="Targets required by the selected tests")
    parser.add_argument("--ctest-regex", help="Select the already qualified platform's test subset")
    parser.add_argument("--evidence-root", type=Path, help="Persistent parent for unique evidence directories")
    parser.add_argument("--initial-source-signature", type=Path, help="Signature captured before the initial build")
    args = parser.parse_args()
    build = args.build_dir.resolve()
    cache = (build/"CMakeCache.txt").read_text()
    if f"STAR_CXX_STANDARD:STRING={args.initial_standard}" not in cache:
        raise ValueError(f"Language pair must start from a C++{args.initial_standard} build")
    if args.parallel < 1:
        raise ValueError("Parallel build count must be positive")
    source = Path(__file__).resolve().parents[1]/"source"
    source_before = source_signature(source)
    if args.initial_source_signature and json.loads(args.initial_source_signature.read_text()) != source_before:
        raise RuntimeError("Source changed since initial build began; result unverified")
    if args.evidence_root:
        args.evidence_root.mkdir(parents=True, exist_ok=True)
    evidence = Path(tempfile.mkdtemp(prefix="star-language-pair-", dir=args.evidence_root))
    # A container creates mkdtemp directories as root-only 0700. This directory
    # holds public source hashes/commands, not private reads or credentials;
    # permit the host artifact uploader to traverse it without changing owners.
    evidence.chmod(0o755)
    (evidence/"source-signature.json").write_text(json.dumps(source_before, indent=2))
    (evidence/"result.json").write_text(json.dumps({"status": "UNVERIFIED", "initial_standard": args.initial_standard}, indent=2))
    binary = build/("STAR.exe" if (build/"STAR.exe").exists() else "STAR")
    # Same directory preserves runtime-DLL search without copying arbitrary DLLs.
    saved = binary.with_name(evidence.name+f"-cxx{args.initial_standard}-reference"+binary.suffix)
    if saved.exists():
        raise ValueError("Previous reference exists; investigate before reusing build")
    shutil.copy2(binary, saved)
    reference_sha256 = hashlib.sha256(saved.read_bytes()).hexdigest()
    commands = []
    def run(command):
        commands.append(command)
        (evidence/"commands.json").write_text(json.dumps(commands, indent=2))
        subprocess.run(command, check=True)
    def build_command():
        command = ["cmake", "--build", str(build), "--parallel", str(args.parallel)]
        if args.build_target:
            command += ["--target", *args.build_target]
        return command
    comparison_standard = "20" if args.initial_standard == "17" else "17"
    try:
        run(["cmake", "-S", str(source), "-B", str(build), f"-DSTAR_CXX_STANDARD={comparison_standard}"])
        run(build_command())
        test_command = ["ctest", "--test-dir", str(build), "--no-tests=error", "--output-on-failure"]
        if args.ctest_regex:
            test_command += ["-R", args.ctest_regex]
        run(test_command)
        import sys
        run([sys.executable, str(source.parent/"scripts/test_cpu_upstream.py"),
             "--star-exe", str(binary), "--ref-exe", str(saved), "--sa-sparse", "3"])
    finally:
        if args.initial_standard == "20":
            run(["cmake", "-S", str(source), "-B", str(build), "-DSTAR_CXX_STANDARD=20"])
            run(build_command())
        # Preserve reference/evidence for review; no overwrite on reruns.
    if source_signature(source) != source_before:
        raise RuntimeError("Source changed during language comparison; result unverified")
    if hashlib.sha256(saved.read_bytes()).hexdigest() != reference_sha256:
        raise RuntimeError("Saved baseline changed during language comparison; result unverified")
    (evidence/"result.json").write_text(json.dumps({
        "status": "PASS", "initial_standard": args.initial_standard,
        "final_standard": "20", "reference_sha256": reference_sha256,
        "final_sha256": hashlib.sha256(binary.read_bytes()).hexdigest(),
        "source_signature_sha256": hashlib.sha256(json.dumps(source_before, sort_keys=True).encode()).hexdigest(),
    }, indent=2))
    print(f"Same-source language pair passed. Evidence: {evidence}; reference: {saved}")


if __name__ == "__main__":
    main()
