#!/usr/bin/env python3
"""Standalone BAM tools: --runMode inputAlignmentsFromBAM signal (bedGraph/wiggle)
and duplicate marking, plus signal output during mapping.

Builds a miniature two-chromosome genome and a coordinate-sorted paired-end BAM
with unique, multi-mapping and identical duplicate pairs, then runs every signal
variant and both duplicate-marking modes. With --ref-exe the same input BAM is
processed by a reference binary and every output must be identical (signal files
byte-for-byte, BAM after BGZF decompression). Failure gates require a clean
non-zero exit for a missing input and, on Linux, for outputs that cannot be
written (symlinked to /dev/full); success with a truncated output is a failure.
"""
import argparse
import gzip
import hashlib
import json
import os
import random
import subprocess
import sys
import tempfile
from pathlib import Path

SANITIZER = ("AddressSanitizer", "LeakSanitizer", "runtime error:")


def revcomp(seq):
    return seq[::-1].translate(str.maketrans("ACGT", "TGCA"))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--star-exe", required=True)
    parser.add_argument("--ref-exe", help="Reference binary whose outputs must be identical")
    args = parser.parse_args()
    star = str(Path(args.star_exe).resolve(strict=True))
    ref = str(Path(args.ref_exe).resolve(strict=True)) if args.ref_exe else None
    root = Path(tempfile.mkdtemp(prefix="star-bam-tools-"))
    print(f"Evidence retained at: {root}", flush=True)

    def run(label, exe, options, expect_ok=True):
        out = root / label
        out.mkdir(parents=True, exist_ok=True)
        command = [exe, "--outFileNamePrefix", str(out) + "/", *options]
        (out / "command.json").write_text(json.dumps(command))
        proc = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                              errors="replace", timeout=300)
        (out / "console.log").write_text(proc.stdout)
        assert not any(marker in proc.stdout for marker in SANITIZER), (label, proc.stdout[-3000:])
        if expect_ok:
            assert proc.returncode == 0, (label, proc.returncode, proc.stdout[-3000:])
        return out, proc

    rng = random.Random(2026)
    repeat = "".join(rng.choices("ACGT", k=300))  # present on both chromosomes -> NH=2
    chr1 = "".join(rng.choices("ACGT", k=9000)) + repeat + "".join(rng.choices("ACGT", k=9000))
    chr2 = "".join(rng.choices("ACGT", k=6000)) + repeat + "".join(rng.choices("ACGT", k=6000))
    (root / "ref.fa").write_text(f">chr1\n{chr1}\n>chr2\n{chr2}\n")
    genome = root / "genome"
    genome.mkdir()
    run("index", star, ["--runMode", "genomeGenerate", "--genomeDir", str(genome), "--genomeFastaFiles",
                        str(root / "ref.fa"), "--genomeSAindexNbases", "5", "--genomeChrBinNbits", "10"])

    mate1, mate2, n = [], [], 0
    def pair(seq, start, insert, minus):
        nonlocal n
        fragment = seq[start:start + insert]
        r1, r2 = fragment[:60], revcomp(fragment[-60:])
        if minus:
            r1, r2 = revcomp(fragment[-60:]), fragment[:60]
        mate1.append(f"@p{n}\n{r1}\n+\n{'I' * 60}\n")
        mate2.append(f"@p{n}\n{r2}\n+\n{'I' * 60}\n")
        n += 1
    for chrom in (chr1, chr2):
        for _ in range(120):
            pair(chrom, rng.randrange(0, len(chrom) - 400), rng.randrange(180, 320), rng.random() < 0.5)
        for _ in range(6):  # identical duplicates of one fragment per chromosome and strand
            pair(chrom, 1000, 250, False)
            pair(chrom, 2000, 220, True)
    for _ in range(8):  # multimappers inside the shared repeat
        pair(chr1, 9000 + rng.randrange(0, 40), 230, rng.random() < 0.5)
    (root / "R1.fq").write_text("".join(mate1))
    (root / "R2.fq").write_text("".join(mate2))

    mapped, _ = run("map", star, ["--genomeDir", str(genome), "--readFilesIn", str(root / "R1.fq"), str(root / "R2.fq"),
                                  "--outSAMtype", "BAM", "SortedByCoordinate", "--outSAMattributes", "NH", "HI", "AS", "nM",
                                  "--outWigType", "bedGraph", "--limitBAMsortRAM", "100000000"])
    bam = mapped / "Aligned.sortedByCoord.out.bam"
    assert bam.stat().st_size > 0 and list(mapped.glob("Signal*.bg")), "mapping did not produce BAM and signal"

    variants = []
    for wig_type in (["bedGraph"], ["wiggle"], ["bedGraph", "read1_5p"], ["bedGraph", "read2"]):
        for strand in ("Stranded", "Unstranded"):
            for norm in ("RPM", "None"):
                variants.append(("-".join(wig_type + [strand, norm]),
                                 ["--outWigType", *wig_type, "--outWigStrand", strand, "--outWigNorm", norm]))
    variants.append(("bedGraph-prefix-chr1", ["--outWigType", "bedGraph", "--outWigReferencesPrefix", "chr1"]))
    dedup = [("dedup-UniqueIdentical", ["--bamRemoveDuplicatesType", "UniqueIdentical"]),
             ("dedup-NotMulti", ["--bamRemoveDuplicatesType", "UniqueIdenticalNotMulti"]),
             ("dedup-mate2bases", ["--bamRemoveDuplicatesType", "UniqueIdentical", "--bamRemoveDuplicatesMate2basesN", "5"])]

    def outputs(directory):
        result = {}
        for path in sorted(directory.glob("Signal*")):
            result[path.name] = hashlib.sha256(path.read_bytes()).hexdigest()
        processed = directory / "Processed.out.bam"
        if processed.exists():
            result[processed.name] = hashlib.sha256(gzip.decompress(processed.read_bytes())).hexdigest()
        return result

    checks, signatures = [], {}
    for exe_label, exe in (("candidate", star), ("reference", ref)):
        if exe is None:
            continue
        for label, options in variants + dedup:
            out, _ = run(f"{exe_label}/{label}", exe, ["--runMode", "inputAlignmentsFromBAM",
                                                      "--inputBAMfile", str(bam), *options])
            signatures.setdefault(label, {})[exe_label] = outputs(out)
    for label, by_exe in signatures.items():
        files = by_exe["candidate"]
        assert files, f"{label}: no output"
        if label.startswith("dedup"):
            raw = gzip.decompress((root / "candidate" / label / "Processed.out.bam").read_bytes())
            assert len(raw) > 1000, f"{label}: empty BAM"
        else:
            sizes = [(root / "candidate" / label / name).stat().st_size for name in files]
            assert any(sizes), f"{label}: all signal files empty"
        if ref:
            assert by_exe["reference"] == files, (label, by_exe)
    checks.append(f"{len(variants)} signal variants and {len(dedup)} duplicate-marking modes produced output")
    if ref:
        checks.append("all outputs identical to the reference binary")
    dedup_raw = gzip.decompress((root / "candidate" / "dedup-UniqueIdentical" / "Processed.out.bam").read_bytes())
    assert dedup_raw != gzip.decompress(bam.read_bytes()), "duplicate marking changed nothing"
    checks.append("duplicate marking changes FLAGs relative to the input")

    # Failure gates: a missing input must fail cleanly, not crash.
    for label, options in (("missing-signal", ["--outWigType", "bedGraph"]),
                           ("missing-dedup", ["--bamRemoveDuplicatesType", "UniqueIdentical"])):
        out, proc = run(f"fail/{label}", star, ["--runMode", "inputAlignmentsFromBAM",
                                               "--inputBAMfile", str(root / "absent.bam"), *options], expect_ok=False)
        assert proc.returncode not in (0, -11, 139) and "EXITING" in proc.stdout, (label, proc.returncode, proc.stdout[-2000:])
    checks.append("missing input BAM: clean fatal error for signal and duplicate marking")
    if sys.platform.startswith("linux") and os.path.exists("/dev/full"):
        for label, options, target in (("full-dedup", ["--bamRemoveDuplicatesType", "UniqueIdentical"], "Processed.out.bam"),
                                       ("full-signal", ["--outWigType", "bedGraph", "--outWigStrand", "Unstranded"],
                                        "Signal.Unique.str1.out.bg")):
            out = root / "fail" / label
            out.mkdir(parents=True)
            (out / target).symlink_to("/dev/full")
            _, proc = run(f"fail/{label}", star, ["--runMode", "inputAlignmentsFromBAM", "--inputBAMfile", str(bam),
                                                 *options], expect_ok=False)
            assert proc.returncode != 0 and "EXITING" in proc.stdout, \
                f"{label}: output write failure was not reported (exit {proc.returncode})"
        checks.append("unwritable outputs (/dev/full) are reported as fatal errors")
    print(json.dumps({"checks": checks}, indent=1))
    print("PASS: standalone BAM tools")


if __name__ == "__main__":
    main()
