#!/usr/bin/env python3
"""EmptyDrops_CR sensitivity and specificity on a simulated matrix with known truth.

Droplets are simulated from a fixed seed:
  - large cells (above the knee) from four cell types with distinct marker genes;
  - small real cells below the knee, drawn from the same cell-type profiles;
  - empty droplets drawn from the ambient profile (a mixture of all types);
  - a negative control of ambient-composition droplets with cell-like totals.
STAR's standalone `soloCellFiltering` must call every large cell, rescue most
small real cells (recall >= 0.9), and call few ambient-composition droplets
(<= 5 of 100: a 1% FDR over ~250 calls allows ~2.5; 0-2 observed over 8 seeds).
These are statistical acceptance bounds on a simulation, not an exact-output
oracle. With weak markers (2x) and 100-200 UMIs recall falls to 0, so the bound
is informative rather than trivially met.
"""
import argparse
import json
import random
import subprocess
import sys
import tempfile
from collections import Counter
from pathlib import Path

GENES, TYPES, MARKERS = 400, 4, 40
N_LARGE, N_SMALL, N_AMBIENT_LIKE, N_EMPTY = 150, 100, 100, 6000


def profiles(rng):
    base = [rng.uniform(0.5, 1.5) for _ in range(GENES)]
    types = []
    for t in range(TYPES):
        weights = base[:]
        for g in range(t * MARKERS, (t + 1) * MARKERS):  # disjoint marker blocks
            weights[g] *= 25
        total = sum(weights)
        types.append([w / total for w in weights])
    ambient = [sum(p[g] for p in types) / TYPES for g in range(GENES)]
    return types, ambient


def droplet(rng, profile, umis):
    return Counter(rng.choices(range(GENES), weights=profile, k=umis))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--star-exe", required=True)
    parser.add_argument("--seed", type=int, default=20261023)
    args = parser.parse_args()
    star = str(Path(args.star_exe).resolve(strict=True))
    rng = random.Random(args.seed)
    root = Path(tempfile.mkdtemp(prefix="star-emptydrops-"))
    print(f"Evidence retained at: {root}", flush=True)
    types, ambient = profiles(rng)

    droplets, truth = [], []
    for i in range(N_LARGE):
        droplets.append(droplet(rng, types[i % TYPES], rng.randint(5000, 20000))); truth.append("large")
    for i in range(N_SMALL):
        droplets.append(droplet(rng, types[i % TYPES], rng.randint(200, 800))); truth.append("small")
    for _ in range(N_AMBIENT_LIKE):
        droplets.append(droplet(rng, ambient, rng.randint(200, 800))); truth.append("ambient_like")
    for _ in range(N_EMPTY):
        droplets.append(droplet(rng, ambient, rng.randint(1, 80))); truth.append("empty")
    order = list(range(len(droplets)))
    rng.shuffle(order)  # barcode order must not leak the truth
    barcodes = [f"BC{n:06d}" for n in range(len(droplets))]
    label = {barcodes[column]: truth[index] for column, index in enumerate(order)}

    raw = root / "raw"
    raw.mkdir()
    entries = [(gene + 1, column + 1, count) for column, index in enumerate(order)
               for gene, count in sorted(droplets[index].items())]
    (raw / "matrix.mtx").write_text("%%MatrixMarket matrix coordinate integer general\n"
                                    f"{GENES} {len(droplets)} {len(entries)}\n"
                                    + "".join(f"{g} {c} {n}\n" for g, c, n in entries))
    (raw / "features.tsv").write_text("".join(f"G{g}\tGene{g}\tGene Expression\n" for g in range(GENES)))
    (raw / "barcodes.tsv").write_text("".join(f"{b}\n" for b in barcodes))

    # Scaled CellRanger parameters: knee from ~200 expected cells; ambient pool from
    # droplet ranks 1000-5000 (all empties); up to 2000 candidates of >= 100 UMIs.
    params = ["EmptyDrops_CR", "200", "0.99", "10", "1000", "5000", "100", "0.01", "2000", "0.01", "10000"]
    filtered = root / "filtered"
    result = subprocess.run([star, "--runMode", "soloCellFiltering", str(raw) + "/", str(filtered) + "/",
                             "--soloCellFilter", *params, "--outFileNamePrefix", str(root) + "/"],
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=600)
    (root / "console.log").write_text(result.stdout)
    assert result.returncode == 0, result.stdout[-3000:]
    called = (filtered / "barcodes.tsv").read_text().split()
    counts = Counter(label[b] for b in called)
    metrics = {
        "large_recall": counts["large"] / N_LARGE,
        "small_recall": counts["small"] / N_SMALL,
        "ambient_like_called": counts["ambient_like"],
        "empty_called": counts["empty"],
        "called_total": len(called),
    }
    (root / "metrics.json").write_text(json.dumps(metrics, indent=1))
    print(json.dumps(metrics, indent=1))
    failures = []
    if metrics["large_recall"] < 1.0:
        failures.append("not every above-knee cell was called")
    if metrics["small_recall"] < 0.9:
        failures.append(f"small-cell rescue recall {metrics['small_recall']:.2f} < 0.90")
    if metrics["ambient_like_called"] > 5:
        failures.append(f"{metrics['ambient_like_called']} ambient-composition droplets called (> 5 of 100)")
    if metrics["empty_called"] > 0:
        failures.append(f"{metrics['empty_called']} low-count empty droplets called")
    if failures:
        print("FAIL: " + "; ".join(failures))
        return 1
    print("PASS: EmptyDrops_CR rescues below-knee cells and rejects ambient-composition droplets")
    return 0


if __name__ == "__main__":
    sys.exit(main())
