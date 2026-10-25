#!/usr/bin/env python3
"""Malformed FASTQ robustness: every case must terminate without a crash, hang or
sanitizer report; inputs that readLoad documents as fatal must be rejected.

Run against an ASan/UBSan build for memory-safety evidence. Accepting an input is
recorded, not judged: this checks safety, not which inputs STAR should tolerate.
"""
import argparse
import json
import random
import subprocess
import sys
import tempfile
from pathlib import Path

REJECT, NO_CRASH = "reject", "no-crash"


def fastq(records):
    return "".join(f"@{name}\n{seq}\n+\n{qual}\n" for name, seq, qual in records)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--star-exe", required=True)
    parser.add_argument("--timeout", type=int, default=120, help="Per-run seconds; exceeding it is a hang")
    args = parser.parse_args()
    args.star_exe = str(Path(args.star_exe).resolve(strict=True))  # see test_cpu_upstream.py
    root = Path(tempfile.mkdtemp(prefix="star-malformed-"))
    print(f"Evidence retained at: {root}", flush=True)
    sanitizer_markers = ("AddressSanitizer", "LeakSanitizer", "runtime error:", "UndefinedBehaviorSanitizer")

    def run(label, options):
        out = root / label
        out.mkdir()
        cmd = [args.star_exe, "--runThreadN", "2", *options, "--outFileNamePrefix", str(out) + "/"]
        (out / "command.json").write_text(json.dumps(cmd))
        try:
            proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                                  errors="replace", timeout=args.timeout)
        except subprocess.TimeoutExpired:
            return None, ""
        (out / "console.log").write_text(proc.stdout)
        log = out / "Log.out"
        return proc.returncode, proc.stdout + (log.read_text(errors="replace") if log.is_file() else "")

    rng = random.Random(4242)
    sequence = "".join(rng.choices("ACGT", k=20000))
    (root / "reference.fa").write_text(">chr1\n" + sequence + "\n")
    genome = root / "genome"
    genome.mkdir()
    code, text = run("index", ["--runMode", "genomeGenerate", "--genomeDir", str(genome),
                               "--genomeFastaFiles", str(root / "reference.fa"),
                               "--genomeSAindexNbases", "4", "--genomeChrBinNbits", "10"])
    assert code == 0, ("index", code, text[-2000:])

    good = [(f"r{i}", sequence[p:p+80], "I" * 80) for i, p in enumerate(range(100, 4000, 211))]
    single = {
        "valid-control": (fastq(good), 0),
        "quality-shorter": (fastq(good[:3]) + "@bad\nACGTACGTAC\n+\nIIII\n", REJECT),
        "quality-longer": (fastq(good[:3]) + "@bad\nACGTAC\n+\nIIIIIIIIIIII\n", REJECT),
        "empty-sequence-line": (fastq(good[:3]) + "@bad\n\n+\n\n", REJECT),
        "read-longer-than-max": (fastq([("long", "A" * 5000, "I" * 5000)]), REJECT),
        "read-name-too-long": (fastq([("n" * 70000, "ACGT" * 20, "I" * 80)]), REJECT),
        "missing-plus-line": (fastq(good[:3]) + "@bad\nACGTACGT\nIIIIIIII\n" + fastq(good[3:6]), NO_CRASH),
        "truncated-final-record": (fastq(good) + "@cut\nACGTACGT\n", NO_CRASH),
        "truncated-mid-sequence": (fastq(good) + "@cut\nACG", NO_CRASH),
        "empty-file": ("", NO_CRASH),
        "header-without-at": (fastq(good[:2]).replace("@r1", "r1", 1), NO_CRASH),
        "crlf-line-endings": (fastq(good).replace("\n", "\r\n"), NO_CRASH),
        "nul-bytes-in-sequence": (fastq(good[:3]) + "@nul\nACGT\0\0ACGT\n+\nIIIIIIIIII\n", NO_CRASH),
        "iupac-and-lowercase": (fastq([("iu", "acgtRYKMSWNNacgt" * 5, "I" * 80)] + good[:3]), NO_CRASH),
        "binary-garbage": (bytes(rng.randrange(256) for _ in range(4096)).decode("latin-1"), NO_CRASH),
        "only-header": ("@lonely\n", NO_CRASH),
    }
    paired = {
        "mate-count-mismatch": (fastq(good), fastq(good[:-3]), NO_CRASH),
        "mate-names-differ": (fastq(good), fastq([(n + "x", s, q) for n, s, q in good]), NO_CRASH),
        "mate-quality-shorter": (fastq(good), fastq(good[:2]) + "@bad\nACGTACGT\n+\nII\n", REJECT),
    }

    failures, table = [], []

    def judge(label, expectation, code, text):
        crashed = code is None or code < 0 or code in (134, 139) or any(m in text for m in sanitizer_markers)
        outcome = "hang" if code is None else ("crash" if crashed else ("accepted" if code == 0 else "rejected"))
        table.append((label, expectation, code, outcome))
        if crashed:
            failures.append(f"{label}: {outcome} (exit {code})")
        elif expectation == REJECT and (code == 0 or "EXITING" not in text):
            failures.append(f"{label}: expected a fatal input rejection, got exit {code}")
        elif expectation == 0 and code != 0:
            failures.append(f"{label}: valid control failed (exit {code})")

    for label, (content, expectation) in single.items():
        path = root / f"{label}.fastq"
        path.write_bytes(content.encode("latin-1"))
        code, text = run(label, ["--genomeDir", str(genome), "--readFilesIn", str(path)])
        judge(label, expectation, code, text)
    for label, (mate1, mate2, expectation) in paired.items():
        paths = [root / f"{label}_{i}.fastq" for i in (1, 2)]
        for path, content in zip(paths, (mate1, mate2)):
            path.write_bytes(content.encode("latin-1"))
        code, text = run(label, ["--genomeDir", str(genome), "--readFilesIn", *map(str, paths)])
        judge(label, expectation, code, text)

    width = max(len(row[0]) for row in table)
    for label, expectation, code, outcome in table:
        print(f"{label:<{width}}  expect={expectation!s:<8} exit={code!s:<5} {outcome}")
    (root / "summary.json").write_text(json.dumps(table, indent=1))
    if failures:
        print("FAILURES:\n  " + "\n  ".join(failures))
        return 1
    print(f"PASS: {len(table)} malformed-input cases terminated safely")
    return 0


if __name__ == "__main__":
    sys.exit(main())
