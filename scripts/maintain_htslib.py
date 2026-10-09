#!/usr/bin/env python3
"""Record or verify the vendored HTSlib patch queue against the locked archive."""
import argparse
import difflib
import hashlib
import json
from pathlib import Path, PurePosixPath
import subprocess
import tarfile
import tempfile

ROOT = Path(__file__).resolve().parents[1]
VENDOR = ROOT / "source/htslib"
LOCAL_FILES = {"config.h", "config_vars.h", "version.h", "win32_compat.h"}


def digest(data):
    return hashlib.sha256(data).hexdigest()


def canonical(data):
    # Checkout newline conversion is not a local upstream patch.
    return data.replace(b"\r\n", b"\n") if b"\0" not in data else data


def safe_name(name):
    path = PurePosixPath(name)
    if not name or name == "." or path.is_absolute() or ".." in path.parts or "\\" in name or ":" in name:
        raise ValueError("unsafe patch inventory path")
    return name


def upstream_files(archive, version):
    prefix = f"htslib-{version}/"
    with tarfile.open(archive) as saved:
        result = {}
        for member in saved.getmembers():
            if member.isfile() and member.name.startswith(prefix):
                name = safe_name(member.name[len(prefix):])
                result[name] = canonical(saved.extractfile(member).read())
        return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--archive", type=Path, required=True)
    parser.add_argument("--queue", type=Path, default=ROOT / "source/cmake/htslib-patches")
    parser.add_argument("--record", action="store_true", help="Create a new queue; never replace one")
    args = parser.parse_args()
    lock = json.loads((ROOT / "dependencies.lock.json").read_text())
    dependency = next(item for item in lock["dependencies"] if item["name"] == "HTSlib")
    if digest(args.archive.read_bytes()) != dependency["sha256"]:
        raise ValueError("archive does not match dependencies.lock.json")
    upstream = upstream_files(args.archive, dependency["version"])
    if args.record:
        names = subprocess.check_output(["git", "ls-files", "source/htslib"], cwd=ROOT, text=True).splitlines()
        inventory, extras, changes = {}, {}, []
        for path in names:
            name = safe_name(path.removeprefix("source/htslib/"))
            data = canonical((VENDOR / name).read_bytes())
            if name not in upstream and name not in LOCAL_FILES and not name.startswith("win32_stubs/"):
                extras[name] = digest(data)
                continue
            inventory[name] = digest(data)
            original = upstream.get(name, b"")
            if original != data:
                changes.extend(difflib.unified_diff(original.decode("utf-8").splitlines(True),
                    data.decode("utf-8").splitlines(True),
                    fromfile=f"a/{name}" if name in upstream else "/dev/null", tofile=f"b/{name}"))
        patch = "".join(changes).encode()
        args.queue.mkdir(parents=True, exist_ok=False)
        (args.queue / "0001-star-cross.patch").write_bytes(patch)
        manifest = {"schema_version": 1, "version": dependency["version"],
            "archive_sha256": dependency["sha256"], "source": dependency["source"],
            "patch_sha256": digest(patch), "files": inventory, "retained_checkout_extras": extras,
            "normalization": "CRLF to LF for files without NUL bytes"}
        (args.queue / "manifest.json").write_text(json.dumps(manifest, indent=2, sort_keys=True)+"\n")
    manifest = json.loads((args.queue / "manifest.json").read_text())
    patch_path = (args.queue / "0001-star-cross.patch").resolve()
    if manifest["version"] != dependency["version"] or manifest["archive_sha256"] != dependency["sha256"]:
        raise ValueError("patch queue does not match dependency lock")
    if digest(patch_path.read_bytes()) != manifest["patch_sha256"]:
        raise ValueError("patch queue content hash changed")
    tracked = subprocess.check_output(["git", "ls-files", "source/htslib"], cwd=ROOT, text=True).splitlines()
    expected_inventory = set(manifest["files"]) | set(manifest.get("retained_checkout_extras", {}))
    if {name.removeprefix("source/htslib/") for name in tracked} != expected_inventory:
        raise ValueError("tracked vendor inventory changed; review the patch queue")
    for name, expected in manifest.get("retained_checkout_extras", {}).items():
        safe_name(name)
        if digest(canonical((VENDOR / name).read_bytes())) != expected:
            raise ValueError(f"unrecorded checkout-extra modification: {name}")
    scratch_parent = ROOT / "data/validation"
    scratch_parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="htslib-patch-check-", dir=scratch_parent) as scratch:
        target = Path(scratch) / "tree"
        target.mkdir()
        for name in manifest["files"]:
            safe_name(name)
            if name in upstream:
                path = target / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(upstream[name])
        subprocess.run(["git", "apply", "--no-index", "--whitespace=nowarn", "--directory",
                        target.relative_to(ROOT).as_posix(), str(patch_path)], cwd=ROOT, check=True)
        for name, expected in manifest["files"].items():
            if digest(canonical((target / name).read_bytes())) != expected:
                raise ValueError(f"reconstruction mismatch: {name}")
            if digest(canonical((VENDOR / name).read_bytes())) != expected:
                raise ValueError(f"unrecorded vendor modification: {name}")
    print(json.dumps({"status": "PASS", "version": manifest["version"],
                      "files": len(manifest["files"]), "patch_sha256": manifest["patch_sha256"]}))


if __name__ == "__main__":
    main()
