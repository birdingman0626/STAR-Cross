#!/usr/bin/env python3
"""Transcript3p accuracy on ambiguous alternative-polyadenylation isoforms with known truth.

A simulated genome carries four genes with a short and a long isoform (the long
one extends the last exon 3' end by EXTENSION bp, so most reads from either are
compatible with both) and four single-isoform genes that teach STAR the read
3'-distance distribution. 10x v3-style reads are placed at a 3'-biased distance
from each molecule's transcript end. Two cell clusters use different, gene-
specific short-isoform fractions. The test compares Transcript3p's estimated
short fraction per gene and cluster with the simulated truth.
"""
import argparse
import json
import random
import subprocess
import sys
import tempfile
from pathlib import Path

EXTENSION = 400            # long isoform = short isoform + this many 3' bases
EXON1, INTRON, EXON2 = 400, 800, 1800
SPACER = 1500
TRUTH = {  # short-isoform fraction per ambiguous gene, cluster 1 / cluster 2
    "A1": (0.8, 0.3), "A2": (0.5, 0.9), "A3": (0.2, 0.6), "A4": (0.65, 0.35)}
SINGLE = ["S1", "S2", "S3", "S4"]
CELLS_PER_CLUSTER, UMIS_AMBIGUOUS, UMIS_SINGLE = 25, 40, 60
MEAN_DISTANCE, READ_LEN = 250, 90
TOLERANCE = 0.10


def revcomp(s):
    return s[::-1].translate(str.maketrans("ACGT", "TGCA"))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--star-exe", required=True)
    parser.add_argument("--seed", type=int, default=7)
    parser.add_argument("--threads", type=int, default=2)
    args = parser.parse_args()
    star = str(Path(args.star_exe).resolve(strict=True))
    rng = random.Random(args.seed)
    root = Path(tempfile.mkdtemp(prefix="star-transcript3p-"))
    print(f"Evidence retained at: {root}", flush=True)

    # Genome and annotation (all genes on + strand).
    genes = list(TRUTH) + SINGLE
    span = EXON1 + INTRON + EXON2 + EXTENSION
    chrom = []
    gtf = []
    transcripts = {}  # transcript id -> spliced sequence
    for gene in genes:
        chrom.append("".join(rng.choices("ACGT", k=SPACER)))
        start = sum(map(len, chrom))  # 0-based gene start
        body = "".join(rng.choices("ACGT", k=span))
        chrom.append(body)
        e1 = (start + 1, start + EXON1)
        e2_start = start + EXON1 + INTRON + 1
        isoforms = {f"{gene}_short": e2_start + EXON2 - 1, f"{gene}_long": e2_start + EXON2 + EXTENSION - 1} \
            if gene in TRUTH else {f"{gene}_only": e2_start + EXON2 - 1}
        for tid, e2_end in isoforms.items():
            for a, b in (e1, (e2_start, e2_end)):
                gtf.append(f'chr1\tsim\texon\t{a}\t{b}\t.\t+\t.\tgene_id "{gene}"; transcript_id "{tid}"; '
                           f'gene_name "{gene}"; gene_biotype "protein_coding";\n')
            seq = body[0:EXON1] + body[EXON1 + INTRON:e2_end - start]
            transcripts[tid] = seq
    chrom.append("".join(rng.choices("ACGT", k=SPACER)))
    (root / "ref.fa").write_text(">chr1\n" + "".join(chrom) + "\n")
    (root / "genes.gtf").write_text("".join(gtf))
    genome = root / "genome"
    genome.mkdir()

    def run(label, options):
        out = root / label
        out.mkdir(exist_ok=True)
        command = [star, "--runThreadN", str(args.threads), *options, "--outFileNamePrefix", str(out) + "/"]
        proc = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                              errors="replace", timeout=900)
        (out / "console.log").write_text(proc.stdout)
        assert proc.returncode == 0, (label, proc.stdout[-3000:])
        return out

    run("index", ["--runMode", "genomeGenerate", "--genomeDir", str(genome), "--genomeFastaFiles",
                  str(root / "ref.fa"), "--sjdbGTFfile", str(root / "genes.gtf"), "--sjdbOverhang", "89",
                  "--genomeSAindexNbases", "8"])

    # Cells, molecules and reads.
    barcodes = sorted({"".join(rng.choices("ACGT", k=16)) for _ in range(CELLS_PER_CLUSTER * 2 + 10)})
    cells = barcodes[:CELLS_PER_CLUSTER * 2]
    cluster_of = {cb: 1 + (i >= CELLS_PER_CLUSTER) for i, cb in enumerate(cells)}
    (root / "whitelist.txt").write_text("".join(cb + "\n" for cb in barcodes))
    (root / "clusters.tsv").write_text("".join(f"{cb} {cl}\n" for cb, cl in cluster_of.items()))
    r1, r2, n = [], [], 0
    used = set()

    def read_from(tid, cb):
        nonlocal n
        seq = transcripts[tid]
        while True:  # 3'-biased distance of the read's 3' end from the transcript end
            d = int(rng.expovariate(1.0 / MEAN_DISTANCE))
            if d + READ_LEN <= len(seq) - EXON1:  # stay inside the last exon
                break
        cdna = seq[len(seq) - d - READ_LEN:len(seq) - d]
        while True:
            umi = "".join(rng.choices("ACGT", k=12))
            if (cb, umi) not in used:
                used.add((cb, umi))
                break
        r1.append(f"@r{n}\n{cb}{umi}\n+\n{'I' * 28}\n")
        r2.append(f"@r{n}\n{cdna}\n+\n{'I' * READ_LEN}\n")
        n += 1

    for cb, cluster in cluster_of.items():
        for gene, fractions in TRUTH.items():
            for _ in range(UMIS_AMBIGUOUS):
                read_from(f"{gene}_short" if rng.random() < fractions[cluster - 1] else f"{gene}_long", cb)
        for gene in SINGLE:
            for _ in range(UMIS_SINGLE):
                read_from(f"{gene}_only", cb)
    (root / "R1.fastq").write_text("".join(r1))
    (root / "R2.fastq").write_text("".join(r2))

    out = run("map", ["--genomeDir", str(genome), "--readFilesIn", str(root / "R2.fastq"), str(root / "R1.fastq"),
                      "--soloType", "CB_UMI_Simple", "--soloCBwhitelist", str(root / "whitelist.txt"),
                      "--soloCBlen", "16", "--soloUMIstart", "17", "--soloUMIlen", "12",
                      "--soloFeatures", "Transcript3p", "--soloClusterCBfile", str(root / "clusters.tsv"),
                      "--soloCellFilter", "None", "--outSAMtype", "None"])
    solo = out / "Solo.out" / "Transcript3p"
    features = [line.split("\t")[0] for line in (solo / "features.tsv").read_text().splitlines()]
    rows = [line.split() for line in (solo / "matrix.mtx").read_text().splitlines()
            if line and not line.startswith("%")]
    assert int(rows[0][0]) == len(features), "matrix rows must match features.tsv"
    estimate = {(features[int(r) - 1], int(c)): float(v) for r, c, v in rows[1:]}  # rows[0]: dimensions

    results, failures = [], []
    for gene, fractions in TRUTH.items():
        for cluster in (1, 2):
            short = estimate.get((f"{gene}_short", cluster), 0.0)
            long_ = estimate.get((f"{gene}_long", cluster), 0.0)
            measured = short / (short + long_) if short + long_ > 0 else float("nan")
            error = measured - fractions[cluster - 1]
            results.append({"gene": gene, "cluster": cluster, "truth": fractions[cluster - 1],
                            "estimate": round(measured, 4), "error": round(error, 4)})
            if not abs(error) <= TOLERANCE:
                failures.append(f"{gene} cluster {cluster}: estimate {measured:.3f} vs truth {fractions[cluster - 1]}")
    (root / "results.json").write_text(json.dumps(results, indent=1))
    print(json.dumps(results, indent=1))
    worst = max(abs(r["error"]) for r in results if r["error"] == r["error"])
    print(f"max |error| = {worst:.4f} (tolerance {TOLERANCE})")
    if failures:
        print("FAIL: " + "; ".join(failures))
        return 1
    print("PASS: Transcript3p recovers ambiguous isoform fractions within tolerance")
    return 0


if __name__ == "__main__":
    sys.exit(main())
