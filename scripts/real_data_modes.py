#!/usr/bin/env python3
"""Real-data identity check for mapping modes outside the 10x Solo profile.

Each profile runs every binary on the same real sample. Outputs are compared
with the first label's, using the full-library method:
- the BAM header without @PG/@CO;
- the decompressed BAM records (coordinate-sorted, so the order is fixed);
- SJ.out.tab and ReadsPerGene.out.tab, byte for byte;
- Chimeric.out.junction as a set of lines, since thread order varies;
- the scientific Log.final.out fields.

Profiles:
  twopass   paired bulk RNA-seq, --twopassMode Basic, GeneCounts
  chimeric  paired bulk RNA-seq, STAR-Fusion style chimeric settings, Junctions
  long      STARlong on long reads (ONT/PacBio), single end

usage: real_data_modes.py --profile twopass --genome-dir G --reads R1 [R2] --output-dir OUT
                          --threads 10 label=/abs/STAR label2=/abs/STAR ...
Results go to OUT/result.json, which a rerun resumes. Large outputs are
deleted once hashed; Log.out and Log.final.out are kept.
"""
import argparse
import hashlib
import json
import os
import platform
import shutil
import struct
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from test_cpu_upstream import scientific_final_fields  # noqa: E402

PROFILES = {
    "twopass": ["--twopassMode", "Basic", "--quantMode", "GeneCounts",
                "--outSAMtype", "BAM", "SortedByCoordinate",
                "--outSAMattributes", "NH", "HI", "AS", "nM", "NM", "MD", "jM", "jI", "MC", "ch"],
    "chimeric": ["--quantMode", "GeneCounts", "--outSAMtype", "BAM", "SortedByCoordinate",
                 "--outSAMattributes", "NH", "HI", "AS", "nM", "NM", "MD", "jM", "jI", "MC", "ch",
                 "--chimSegmentMin", "12", "--chimJunctionOverhangMin", "8", "--chimOutType", "Junctions",
                 "--chimOutJunctionFormat", "0", "--alignSJDBoverhangMin", "10", "--alignMatesGapMax", "100000",
                 "--alignIntronMax", "100000", "--alignSJstitchMismatchNmax", "5", "-1", "5", "5",
                 "--chimMultimapScoreRange", "3", "--chimScoreJunctionNonGTAG", "-4", "--chimMultimapNmax", "20",
                 "--chimNonchimScoreDropMin", "10", "--peOverlapNbasesMin", "12", "--peOverlapMMp", "0.1",
                 "--alignInsertionFlush", "Right", "--alignSplicedMateMapLminOverLmate", "0",
                 "--alignSplicedMateMapLmin", "30"],
    "long": ["--outSAMtype", "BAM", "SortedByCoordinate", "--outSAMattributes", "NH", "HI", "AS", "nM", "NM",
             "--outFilterMultimapScoreRange", "20", "--outFilterScoreMinOverLread", "0",
             "--outFilterMatchNminOverLread", "0.66", "--outFilterMismatchNmax", "1000",
             "--winAnchorMultimapNmax", "200", "--seedSearchStartLmax", "12", "--seedPerReadNmax", "100000",
             "--seedPerWindowNmax", "100", "--alignTranscriptsPerReadNmax", "100000",
             "--alignTranscriptsPerWindowNmax", "10000"],
}
BAM = "Aligned.sortedByCoord.out.bam"


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as stream:
        for block in iter(lambda: stream.read(1 << 22), b""):
            digest.update(block)
    return digest.hexdigest()


def bam_parts(path):
    """Header without @PG/@CO, reference list, and sha256 of the decompressed record bytes."""
    z = subprocess.Popen(["zcat", str(path)], stdout=subprocess.PIPE, bufsize=1 << 22)
    stream = z.stdout

    def read(n):
        data = stream.read(n)
        if len(data) != n:
            raise ValueError("truncated BAM")
        return data
    if read(4) != b"BAM\1":
        raise ValueError("not a BAM file")
    text = read(struct.unpack("<i", read(4))[0]).decode()
    refs = []
    for _ in range(struct.unpack("<i", read(4))[0]):
        name = read(struct.unpack("<i", read(4))[0])
        refs.append([name.rstrip(b"\0").decode(), struct.unpack("<i", read(4))[0]])
    digest, size = hashlib.sha256(), 0
    for block in iter(lambda: stream.read(1 << 22), b""):
        digest.update(block)
        size += len(block)
    if z.wait() != 0:
        raise RuntimeError("zcat failed")
    kept = [line for line in text.splitlines() if not line.startswith(("@PG", "@CO"))]
    return {"header_sha256": hashlib.sha256("\n".join(kept).encode()).hexdigest(), "header_lines": len(kept),
            "refs_sha256": hashlib.sha256(json.dumps(refs).encode()).hexdigest(),
            "record_bytes": size, "records_sha256": digest.hexdigest()}


def line_set_sha256(path):
    return hashlib.sha256("\n".join(sorted(Path(path).read_text().splitlines())).encode()).hexdigest()


def signature(run_dir):
    sig = {"bam": bam_parts(run_dir / BAM), "final": scientific_final_fields(run_dir / "Log.final.out")}
    for name in ("SJ.out.tab", "ReadsPerGene.out.tab"):
        if (run_dir / name).exists():
            sig[name] = sha256(run_dir / name)
    if (run_dir / "Chimeric.out.junction").exists():
        sig["Chimeric.out.junction"] = line_set_sha256(run_dir / "Chimeric.out.junction")
        sig["chimeric_junction_lines"] = sum(1 for _ in open(run_dir / "Chimeric.out.junction"))
    return sig


def differences(a, b):
    keys = sorted(set(a) | set(b))
    diff = {}
    for key in keys:
        if key == "bam":
            for part in ("header_sha256", "refs_sha256", "record_bytes", "records_sha256"):
                if a["bam"][part] != b["bam"][part]:
                    diff["bam." + part] = [a["bam"][part], b["bam"][part]]
        elif key == "final":
            fields = set(a["final"]) | set(b["final"])
            changed = {k: [a["final"].get(k), b["final"].get(k)] for k in fields if a["final"].get(k) != b["final"].get(k)}
            if changed:
                diff["final"] = changed
        elif a.get(key) != b.get(key):
            diff[key] = [a.get(key), b.get(key)]
    return diff


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--profile", choices=sorted(PROFILES), required=True)
    parser.add_argument("--genome-dir", type=Path, required=True)
    parser.add_argument("--reads", nargs="+", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--threads", type=int, default=10)
    parser.add_argument("--extra", nargs=argparse.REMAINDER, default=[], help="appended identically for all labels")
    parser.add_argument("binaries", nargs="+", help="label=/absolute/path; the first is the reference")
    args = parser.parse_args()
    specs = [b.split("=", 1) for b in args.binaries]
    if any(len(s) != 2 or not Path(s[1]).is_absolute() for s in specs) or len({s[0] for s in specs}) != len(specs):
        parser.error("binaries must be distinct label=/absolute/path")

    reads = [str(p) for p in args.reads]
    common = ["--runThreadN", str(args.threads), "--genomeDir", str(args.genome_dir), "--readFilesIn", *reads,
              *(["--readFilesCommand", "zcat"] if reads[0].endswith(".gz") else []), *PROFILES[args.profile],
              *args.extra]
    out = args.output_dir
    out.mkdir(parents=True, exist_ok=True)
    result_path = out / "result.json"
    result = json.loads(result_path.read_text()) if result_path.exists() else {
        "status": "RUNNING", "profile": args.profile, "host": platform.node(), "kernel": platform.release(),
        "common_arguments": common, "runner_sha256": sha256(__file__),
        "inputs": {p: {"bytes": os.path.getsize(p), "sha256": sha256(p)} for p in reads},
        "genome_sha256": {n: sha256(args.genome_dir / n) for n in ("Genome", "SA", "SAindex", "genomeParameters.txt")},
        "binaries": {}, "runs": {}}
    if result["common_arguments"] != common:
        raise SystemExit("result.json was written with different arguments; use a new --output-dir")

    def save():
        result_path.write_text(json.dumps(result, indent=1))
    save()

    work = out / "run"  # one prefix for every label, so command lines differ only in argv[0]
    for label, binary in specs:
        if result["runs"].get(label, {}).get("exit") == 0:
            continue
        result["binaries"][label] = {"path": binary, "sha256": sha256(binary), "version": subprocess.run(
            [binary, "--version"], capture_output=True, text=True).stdout.strip()}
        shutil.rmtree(work, ignore_errors=True)
        work.mkdir()
        command = [binary, *common, "--outFileNamePrefix", f"{work}/"]
        started = time.monotonic()
        with open(out / f"{label}.console.log", "w") as console:
            process = subprocess.Popen(command, stdout=console, stderr=subprocess.STDOUT)
            _, status, usage = os.wait4(process.pid, 0)
        run = {"exit": os.waitstatus_to_exitcode(status), "wall_seconds": time.monotonic() - started,
               "user_seconds": usage.ru_utime, "system_seconds": usage.ru_stime,
               "peak_rss_bytes": usage.ru_maxrss * 1024}
        dest = out / label
        shutil.rmtree(dest, ignore_errors=True)
        work.rename(dest)
        if run["exit"] == 0:
            run["signature"] = signature(dest)
        for path in dest.iterdir():  # keep receipts only
            if path.name not in ("Log.out", "Log.final.out", "Log.progress.out"):
                shutil.rmtree(path) if path.is_dir() else path.unlink()
        result["runs"][label] = run
        save()
        print(f"{label}: exit {run['exit']} {run['wall_seconds']:.0f}s", flush=True)

    runs = [result["runs"][label] for label, _ in specs]
    if any(r["exit"] for r in runs):
        result["status"] = "RUN_FAILED"
    else:
        reference = runs[0]["signature"]
        result["differences"] = {label: differences(reference, result["runs"][label]["signature"])
                                 for label, _ in specs[1:]}
        result["status"] = "IDENTICAL" if not any(result["differences"].values()) else "DIFFERENT"
    save()
    print(result["status"], json.dumps(result.get("differences", {}))[:2000])


if __name__ == "__main__":
    main()
