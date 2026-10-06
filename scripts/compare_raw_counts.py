#!/usr/bin/env python3
"""Quantify sparse integer-count changes independently of coordinate line ordering."""
import argparse
import json
from pathlib import Path


def matrix(path):
    with Path(path).open() as stream:
        if stream.readline().strip() != "%%MatrixMarket matrix coordinate integer general":
            raise ValueError(f"Not an integer coordinate matrix: {path}")
        rows = (line for line in stream if line.strip() and not line.startswith("%"))
        shape = tuple(map(int, next(rows).split()))
        if len(shape) != 3:
            raise ValueError("Invalid matrix dimensions")
        if any(value < 0 for value in shape):
            raise ValueError("Negative matrix dimensions")
        counts, records = {}, 0
        for line in rows:
            row, column, value = map(int, line.split())
            if not (1 <= row <= shape[0] and 1 <= column <= shape[1]) or value < 0:
                raise ValueError("Invalid count coordinate or value")
            counts[row, column] = counts.get((row, column), 0) + value
            records += 1
        if records != shape[2]:
            raise ValueError("Declared nnz does not match matrix records")
        return shape[:2], counts


def compare(reference, candidate):
    result = {}
    for feature, files in [("Gene", ["matrix.mtx"]), ("GeneFull_Ex50pAS", ["matrix.mtx"]),
                           ("Velocyto", ["spliced.mtx", "unspliced.mtx", "ambiguous.mtx"])]:
        folder = Path("Solo.out")/feature/"raw"
        for name in ("features.tsv", "barcodes.tsv"):
            if (reference/folder/name).read_bytes() != (candidate/folder/name).read_bytes():
                raise ValueError(f"Axes differ: {feature}/{name}")
        for name in files:
            old_shape, old = matrix(reference/folder/name)
            new_shape, new = matrix(candidate/folder/name)
            if old_shape != new_shape:
                raise ValueError("Matrix shapes differ")
            changes = [(key, old.get(key, 0), new.get(key, 0)) for key in sorted(old.keys() | new.keys())
                       if old.get(key, 0) != new.get(key, 0)]
            result[f"{feature}/{name}"] = {"changed_coordinates": len(changes),
                "reference_sum": sum(old.values()), "candidate_sum": sum(new.values()),
                "absolute_count_delta": sum(abs(new-old) for _, old, new in changes),
                "examples": [{"gene_row": key[0], "barcode_column": key[1], "before": old, "after": new}
                             for key, old, new in changes[:5]]}
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("reference", type=Path)
    parser.add_argument("candidate", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    result = compare(args.reference, args.candidate)
    args.output.open("x").write(json.dumps(result, indent=2))
    print(json.dumps({name: {key: value for key, value in item.items() if key != "examples"}
                      for name, item in result.items()}, indent=2))
