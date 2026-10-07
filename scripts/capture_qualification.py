#!/usr/bin/env python3
"""Capture local, immutable source/runtime receipts; never publish private paths."""
import argparse
import hashlib
import json
from pathlib import Path
import platform
import subprocess
import sys
import zipfile
import shutil


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--binary", type=Path, action="append", default=[])
    parser.add_argument("--build-dir", type=Path, action="append", default=[])
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    def command(*values):
        try:
            proc = subprocess.run(values, cwd=root, capture_output=True, text=True)
        except FileNotFoundError:
            return {"exit_code": None, "status": "UNAVAILABLE"}
        return {"exit_code": proc.returncode, "stdout": proc.stdout, "stderr": proc.stderr}
    def digest(path):
        value = hashlib.sha256()
        with path.open("rb") as stream:
            for block in iter(lambda: stream.read(1024*1024), b""):
                value.update(block)
        return value.hexdigest()
    files = command("git", "ls-files", "--cached", "--others", "--exclude-standard", "-z")
    if files["exit_code"]:
        raise RuntimeError("Cannot inventory source")
    sources = {name: digest(root/name) if (root/name).is_file() else None
               for name in sorted(set(files["stdout"].split("\0"))-{ "" })}
    source_digest = hashlib.sha256(json.dumps(sources, sort_keys=True).encode()).hexdigest()
    archive = args.output.with_suffix(".source.zip")
    with zipfile.ZipFile(archive, "x", compression=zipfile.ZIP_DEFLATED) as snapshot:
        for name, expected in sources.items():
            if expected is not None:
                if digest(root/name) != expected:
                    raise RuntimeError("Source changed while capturing")
                snapshot.write(root/name, name)
    manifest = {"source_tree_sha256": source_digest, "source_files": sources,
                "source_archive_sha256": digest(archive),
                "head": command("git", "rev-parse", "HEAD"), "status": command("git", "status", "--porcelain"),
                "os": platform.platform(), "python": sys.version, "cmake": command("cmake", "--version"),
                "gpu": command("nvidia-smi", "--query-gpu=name,driver_version,memory.used,memory.total,utilization.gpu", "--format=csv"),
                "binaries": {str(path.resolve()): digest(path) for path in args.binary},
                "restriction": "Local evidence only; absolute paths and source inventory are not release assets"}
    manifest["builds"] = {}
    for build in args.build_dir:
        records = {}
        for name in ("CMakeCache.txt", "build.ninja", "compile_commands.json", "CMakeFiles/rules.ninja"):
            path = build/name
            if path.is_file():
                target = args.output.parent/(args.output.stem+"-"+build.name+"-"+Path(name).name)
                if target.exists():
                    raise ValueError("Build receipt already exists")
                shutil.copy2(path, target)
                records[name] = {"sha256": digest(path), "snapshot": target.name}
        records["metadata"] = {path.name: json.loads(path.read_text()) for path in build.glob("STAR-build-info-*.json")}
        manifest["builds"][build.name] = records
    with args.output.open("x", encoding="utf-8") as stream:
        json.dump(manifest, stream, indent=2)
    print(f"Source tree hash: {source_digest}")


if __name__ == "__main__":
    main()
