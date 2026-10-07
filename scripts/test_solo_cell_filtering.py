#!/usr/bin/env python3
"""Standalone Solo filtering regression with hand-computed matrix oracles."""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile


def check_cell_filtering(star_exe, root):
    root = Path(root)
    root.mkdir(parents=True, exist_ok=True)
    checks = []
    features = "g1\tGene1\tGene Expression\ng2\tGene2\tGene Expression\n"
    cases = (
        ("single", ["CELL1"], [(1, 1, 3)], 10, ["CELL1"], [(1, 1, 3)]),
        # Sparse columns and shuffled entries exercise sorting and the last cell.
        ("sparse", ["CELL1", "EMPTY", "CELL3"],
         [(2, 3, 7), (1, 1, 4), (1, 3, 3)], 10,
         ["CELL1", "CELL3"], [(1, 1, 4), (1, 2, 3), (2, 2, 7)]),
        # Preserve the existing TopCells cutoff/tie policy, not exact cardinality.
        ("cutoff", ["CELL1", "CELL2", "CELL3"],
         [(1, 3, 1), (1, 1, 10), (1, 2, 4)], 1,
         ["CELL1", "CELL2"], [(1, 1, 10), (1, 2, 4)]),
        ("rounding", ["CELL1", "CELL2"], [(1, 1, 1.6), (2, 2, 0.4)], 10,
         ["CELL1"], [(1, 1, 2)]),
        ("unsigned-knee", ["CELL1", "CELL2"], [(1, 1, 4294967295), (1, 2, 3000000000)], 10,
         ["CELL1"], [(1, 1, 4294967295)]),
    )
    for name, barcodes, entries, top, expected_barcodes, expected_entries in cases:
        case = root / name
        raw, filtered = case / "raw", case / "filtered"
        raw.mkdir(parents=True)
        (raw / "features.tsv").write_text(features)
        (raw / "barcodes.tsv").write_text("\n".join(barcodes) + "\n")
        field = "integer" if name == "single" else "real"
        matrix = f"%%MatrixMarket matrix coordinate {field} general\n% test\n"
        matrix += f"2 {len(barcodes)} {len(entries)}\n"
        matrix += "".join(f"{g} {c} {n}\n" for g, c, n in entries)
        (raw / "matrix.mtx").write_text(matrix)
        filter_parameters = (["CellRanger2.2", "1", ".99", "1"] if name == "unsigned-knee"
                             else ["TopCells", str(top)])
        result = subprocess.run(
            [str(Path(star_exe).resolve()), "--runMode", "soloCellFiltering",
             str(raw.resolve()), str(filtered.resolve()) + "/",
             "--soloCellFilter", *filter_parameters,
             "--outFileNamePrefix", str(case.resolve()) + "/"],
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        (case / "process.log").write_text(result.stdout)
        assert result.returncode == 0, (name, result.returncode, result.stdout)
        assert (filtered / "features.tsv").read_text() == features, name
        assert (filtered / "barcodes.tsv").read_text().splitlines() == expected_barcodes, name
        lines = [line for line in (filtered / "matrix.mtx").read_text().splitlines()
                 if line and not line.startswith("%")]
        assert tuple(map(int, lines[0].split())) == (2, len(expected_barcodes), len(expected_entries)), name
        actual = sorted(tuple(map(int, line.split())) for line in lines[1:])
        assert actual == sorted(expected_entries), (name, actual, expected_entries)
        checks.append(f"standalone filtering {name}: exact hand-computed counts and axes")
    return checks


def check_invalid_matrices(star_exe, root):
    root = Path(root)
    valid = "%%MatrixMarket matrix coordinate real general\n2 1 1\n1 1 3\n"
    cases = {
        "truncated": valid.rsplit("3", 1)[0],
        "missing-header": "2 1 1\n1 1 3\n",
        "bad-dimensions": valid.replace("2 1 1", "0 1 1"),
        "row-zero": valid.replace("1 1 3", "0 1 3"),
        "row-outside": valid.replace("1 1 3", "3 1 3"),
        "column-outside": valid.replace("1 1 3", "1 2 3"),
        "negative": valid.replace("1 1 3", "1 1 -1"),
        "nan": valid.replace("1 1 3", "1 1 nan"),
        "inf": valid.replace("1 1 3", "1 1 inf"),
        "count-overflow": valid.replace("1 1 3", "1 1 4294967296"),
        "cell-overflow": "%%MatrixMarket matrix coordinate real general\n2 1 2\n1 1 4294967295\n2 1 1\n",
        "duplicate": "%%MatrixMarket matrix coordinate real general\n2 1 2\n1 1 1\n1 1 2\n",
        "extra-entry": valid + "2 1 2\n",
        "barcodes-short": valid.replace("2 1 1", "2 2 1"),
        "features-short": valid.replace("2 1 1", "3 1 1"),
        "huge-entry-claim": valid.replace("2 1 1", "2 1 1000000000"),
        "huge-axis-claim": valid.replace("2 1 1", "2 4294967295 1"),
        "barcodes-long": valid,
        "features-long": valid.replace("2 1 1", "1 1 1"),
    }
    checks = []
    for name, matrix in cases.items():
        case = root / name
        raw = case / "raw"
        raw.mkdir(parents=True)
        (raw / "matrix.mtx").write_text(matrix)
        (raw / "features.tsv").write_text("g1\tGene1\ng2\tGene2\n")
        (raw / "barcodes.tsv").write_text("CELL1\nCELL2\n" if name == "barcodes-long" else "CELL1\n")
        result = subprocess.run(
            [str(Path(star_exe).resolve()), "--runMode", "soloCellFiltering",
             str(raw.resolve()), str((case / "filtered").resolve()) + "/",
             "--soloCellFilter", "TopCells", "10",
             "--outFileNamePrefix", str(case.resolve()) + "/"],
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=20)
        (case / "process.log").write_text(result.stdout)
        assert result.returncode == 102 and "invalid count matrix" in result.stdout, (name, result.returncode, result.stdout)
        assert not (case / "filtered/matrix.mtx").exists(), name
        checks.append(f"standalone invalid matrix {name}: explicit input rejection")
    return checks


def check_emptydrops(star_exe, root, include_invalid=True):
    root = Path(root)
    raw = root / "raw"
    raw.mkdir(parents=True)
    barcodes = [f"CELL{i}" for i in range(10)]
    entries = [(1, 1, 1000)] + [(g, c, g) for c in range(2, 11) for g in range(1, 7)]
    (raw / "features.tsv").write_text("".join(f"g{i}\tG{i}\n" for i in range(1, 7)))
    (raw / "barcodes.tsv").write_text("\n".join(barcodes) + "\n")
    (raw / "matrix.mtx").write_text("%%MatrixMarket matrix coordinate integer general\n"
                                    + f"6 10 {len(entries)}\n"
                                    + "".join(f"{g} {c} {n}\n" for g, c, n in entries))
    checks = []
    for label, minimum, limit in (("bounded", 1, 100), ("no-candidates", 500, 100), ("huge-cap", 1, 4294967295)):
        matrices = []
        for repeat in range(2):
            case = root / f"{label}-{repeat}"
            case.mkdir()
            filtered = case / "filtered"
            result = subprocess.run([str(Path(star_exe).resolve()), "--runMode", "soloCellFiltering",
                str(raw.resolve()), str(filtered.resolve()) + "/", "--soloCellFilter", "EmptyDrops_CR",
                "1", "0.99", "10", "3", "10", str(minimum), "0", str(limit), "0.01", "20",
                "--outFileNamePrefix", str(case.resolve()) + "/"],
                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=30)
            (case / "process.log").write_text(result.stdout)
            assert result.returncode == 0, (label, result.returncode, result.stdout)
            log = (case / "Log.out").read_text()
            if label == "no-candidates":
                assert "no candidate cells" in log
            else:
                assert "number of candidate cells=9" in log and "finished simulations" in log
            selected = (filtered / "barcodes.tsv").read_text().splitlines()
            assert selected and selected[0] == "CELL0" and len(set(selected)) == len(selected)
            if label == "no-candidates":
                assert selected == ["CELL0"]
            lines = [line for line in (filtered / "matrix.mtx").read_text().splitlines()
                     if line and not line.startswith("%")]
            actual = sorted(tuple(map(int, line.split())) for line in lines[1:])
            expected = sorted((g, selected.index(barcodes[c-1])+1, n)
                              for g, c, n in entries if barcodes[c-1] in selected)
            assert actual == expected and tuple(map(int, lines[0].split())) == (6, len(selected), len(expected))
            matrices.append([(filtered / name).read_bytes() for name in ("matrix.mtx", "barcodes.tsv", "features.tsv")])
        assert matrices[0] == matrices[1], label
        checks.append(f"EmptyDrops {label}: repeated outputs deterministic, counts/axes unchanged")
    fallback_raw = root / "fallback-raw"
    fallback_raw.mkdir()
    (fallback_raw / "features.tsv").write_text("g1\tG1\ng2\tG2\n")
    (fallback_raw / "barcodes.tsv").write_text("\n".join(barcodes) + "\n")
    (fallback_raw / "matrix.mtx").write_text("%%MatrixMarket matrix coordinate integer general\n2 10 10\n"
        + "1 1 1000\n" + "".join(f"1 {c} 1\n" for c in range(2, 11)))
    case = root / "insufficient-sgt"
    case.mkdir()
    filtered = case / "filtered"
    result = subprocess.run([str(Path(star_exe).resolve()), "--runMode", "soloCellFiltering",
        str(fallback_raw.resolve()), str(filtered.resolve()) + "/", "--soloCellFilter", "EmptyDrops_CR",
        "1", ".99", "10", "3", "10", "1", "0", "100", ".01", "20",
        "--outFileNamePrefix", str(case.resolve()) + "/"],
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=30)
    (case / "process.log").write_text(result.stdout)
    assert result.returncode == 0, result.stdout
    assert "insufficient ambient frequency categories for SGT" in (case / "Log.out").read_text()
    assert (filtered / "barcodes.tsv").read_text().splitlines() == ["CELL0"]
    assert (filtered / "matrix.mtx").read_text().splitlines()[-1].split() == ["1", "1", "1000"]
    checks.append("EmptyDrops insufficient SGT: explicit knee-only fallback preserves the known cell")
    rescue_raw = root / "rescue-raw"
    rescue_raw.mkdir()
    (rescue_raw / "features.tsv").write_bytes((raw / "features.tsv").read_bytes())
    (rescue_raw / "barcodes.tsv").write_bytes((raw / "barcodes.tsv").read_bytes())
    # The known signal cell is below the knee cutoff but concentrated in a
    # low-ambient-probability gene; the other candidates follow the ambient mix.
    rescue_entries = [(1, 1, 1000), (1, 2, 60)] + [(g, c, g) for c in range(3, 11) for g in range(1, 7)]
    (rescue_raw / "matrix.mtx").write_text("%%MatrixMarket matrix coordinate integer general\n"
        + f"6 10 {len(rescue_entries)}\n" + "".join(f"{g} {c} {n}\n" for g, c, n in rescue_entries))
    for filtering, params, expected in (("knee", ["CellRanger2.2", "1", ".99", "10"], ["CELL0"]),
        ("rescue", ["EmptyDrops_CR", "1", ".99", "10", "3", "10", "1", "0", "100", ".01", "1000"], ["CELL0", "CELL1"])):
        case = root / filtering
        case.mkdir()
        filtered = case / "filtered"
        result = subprocess.run([str(Path(star_exe).resolve()), "--runMode", "soloCellFiltering",
            str(rescue_raw.resolve()), str(filtered.resolve()) + "/", "--soloCellFilter", *params,
            "--outFileNamePrefix", str(case.resolve()) + "/"],
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=30)
        (case / "process.log").write_text(result.stdout)
        assert result.returncode == 0, result.stdout
        assert (filtered / "barcodes.tsv").read_text().splitlines() == expected
        if filtering == "rescue":
            assert "number of additional non-ambient cells=1" in (case / "Log.out").read_text()
            rows = [line.split() for line in (filtered / "matrix.mtx").read_text().splitlines()
                    if line and not line.startswith("%")]
            assert rows == [["6", "2", "2"], ["1", "1", "1000"], ["1", "2", "60"]]
    checks.append("EmptyDrops positive rescue: detects the known below-knee signal cell, excludes ambient cells")
    if not include_invalid:
        return checks
    invalid = [("TopCells", "0"), ("TopCells", "-1"), ("TopCells", "4294967296"),
               ("TopCells", "junk"), ("TopCells", "1", "extra"),
               ("CellRanger2.2", "1e300", ".99", "10"),
               ("CellRanger2.2", "1", ".99", "0"),
               ("EmptyDrops_CR", "1", ".99", "10", "10", "3", "1", "0", "100", ".01", "20"),
               ("EmptyDrops_CR", "1", ".99", "10", "3", "10", "1", "0", "100", ".01", "0")]
    for index, parameters in enumerate(invalid):
        case = root / f"invalid-filter-{index}"
        case.mkdir()
        result = subprocess.run([str(Path(star_exe).resolve()), "--runMode", "soloCellFiltering",
            str(raw.resolve()), str((case / "filtered").resolve()) + "/", "--soloCellFilter", *parameters,
            "--outFileNamePrefix", str(case.resolve()) + "/"],
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=20)
        (case / "process.log").write_text(result.stdout)
        assert result.returncode == 102 and "fatal PARAMETERS error" in result.stdout, result.stdout
    checks.append("nine invalid cell-filter parameter sets fail explicitly before calculation")
    return checks


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--star-exe", required=True)
    parser.add_argument("--invalid-only", action="store_true")
    parser.add_argument("--emptydrops-only", action="store_true")
    parser.add_argument("--valid-only", action="store_true", help="Exclude fatal-input fixtures for targeted leak checks")
    args = parser.parse_args()
    root = Path(tempfile.mkdtemp(prefix="star-cell-filter-"))
    print(f"Evidence retained at: {root}", flush=True)
    checks = check_emptydrops(args.star_exe, root, not args.valid_only) if args.emptydrops_only else (
        check_invalid_matrices(args.star_exe, root) if args.invalid_only else check_cell_filtering(args.star_exe, root))
    print(json.dumps({"checks": checks}, indent=2))


if __name__ == "__main__":
    main()
