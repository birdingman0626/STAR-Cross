#!/usr/bin/env python3
"""Compare same-source C++20 and C++17 with the same CMake cache/toolchain.

Run in a C++20 build after its tests; restores/rebuilds C++20 before returning.
Builds are sequential: never share a CMake cache between concurrent processes.
"""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import hashlib


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", required=True, type=Path)
    args = parser.parse_args()
    build = args.build_dir.resolve()
    cache = (build/"CMakeCache.txt").read_text()
    if "STAR_CXX_STANDARD:STRING=20" not in cache:
        raise ValueError("Language pair must start from a C++20 build")
    source = Path(__file__).resolve().parents[1]/"source"
    def source_signature():
        names = subprocess.check_output(["git", "ls-files", "--cached", "--others", "--exclude-standard", "-z"], cwd=source.parent).decode().split("\0")
        return {name: hashlib.sha256((source.parent/name).read_bytes()).hexdigest()
                for name in sorted(set(names)) if name.startswith(("source/", "test/")) and (source.parent/name).is_file()}
    source_before = source_signature()
    evidence = Path(tempfile.mkdtemp(prefix="star-language-pair-"))
    binary = build/("STAR.exe" if (build/"STAR.exe").exists() else "STAR")
    # Same directory preserves runtime-DLL search without copying arbitrary DLLs.
    saved = binary.with_name(evidence.name+"-cxx20-reference"+binary.suffix)
    if saved.exists():
        raise ValueError("Previous reference exists; investigate before reusing build")
    shutil.copy2(binary, saved)
    commands = []
    def run(command):
        commands.append(command)
        (evidence/"commands.json").write_text(json.dumps(commands, indent=2))
        subprocess.run(command, check=True)
    try:
        run(["cmake", "-S", str(source), "-B", str(build), "-DSTAR_CXX_STANDARD=17"])
        run(["cmake", "--build", str(build), "--parallel", "4"])
        run(["ctest", "--test-dir", str(build), "--no-tests=error", "--output-on-failure"])
        import sys
        run([sys.executable, str(source.parent/"scripts/test_cpu_upstream.py"),
             "--star-exe", str(binary), "--ref-exe", str(saved), "--sa-sparse", "3"])
    finally:
        run(["cmake", "-S", str(source), "-B", str(build), "-DSTAR_CXX_STANDARD=20"])
        run(["cmake", "--build", str(build), "--parallel", "4"])
        # Preserve reference/evidence for review; no overwrite on reruns.
    if source_signature() != source_before:
        raise RuntimeError("Source changed during language comparison; result unverified")
    (evidence/"source-signature.json").write_text(json.dumps(source_before, indent=2))
    print(f"Same-source language pair passed. Evidence: {evidence}; reference: {saved}")


if __name__ == "__main__":
    main()
