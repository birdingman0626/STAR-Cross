#!/usr/bin/env python3
"""Stream a synchronized FASTQ segment. Engineering fixture, not a biological random sample."""
import argparse
import gzip
import hashlib
import json
from pathlib import Path
import time


def read_record(stream):
    lines = tuple(stream.readline() for _ in range(4))
    if not any(lines):
        return None
    if not all(lines) or not lines[0].startswith(b"@") or not lines[2].startswith(b"+"):
        raise ValueError("Truncated or invalid four-line FASTQ record")
    seq, qual = lines[1].rstrip(b"\r\n"), lines[3].rstrip(b"\r\n")
    if not seq or len(seq) != len(qual):
        raise ValueError("FASTQ sequence/quality length mismatch")
    return lines


def read_id(header):
    name = header.split()[0]
    return name[:-2] if name.endswith((b"/1", b"/2")) else name


def open_fastq(path):
    with Path(path).open("rb") as stream:
        compressed = stream.read(2) == b"\x1f\x8b"
    return gzip.open(path, "rb") if compressed else Path(path).open("rb")


def subset(r1, r2, output, pairs, skip_pairs=0):
    if pairs <= 0 or skip_pairs < 0:
        raise ValueError("pairs must be positive and skip_pairs nonnegative")
    sources = [Path(r1), Path(r2)]
    before = [path.stat() for path in sources]
    output = Path(output)
    output.mkdir(parents=True, exist_ok=False)
    paths = [output / "R1.fastq.partial", output / "R2.fastq.partial"]
    hashes = [hashlib.sha256(), hashlib.sha256()]
    lengths = [set(), set()]
    start = time.monotonic()
    with open_fastq(sources[0]) as input1, open_fastq(sources[1]) as input2, \
            paths[0].open("xb") as out1, paths[1].open("xb") as out2:
        for index in range(skip_pairs+pairs):
            records = [read_record(input1), read_record(input2)]
            if any(record is None for record in records):
                raise ValueError(f"Fewer than {pairs} complete pairs (stopped at {index})")
            if read_id(records[0][0]) != read_id(records[1][0]):
                raise ValueError(f"R1/R2 identifiers do not match at pair {index+1}")
            if index < skip_pairs:
                continue
            for mate, stream in enumerate((out1, out2)):
                data = b"".join(records[mate])
                stream.write(data)
                hashes[mate].update(data)
                lengths[mate].add(len(records[mate][1].rstrip(b"\r\n")))
            if (index+1-skip_pairs) % 100000 == 0:
                print(f"Validated and copied {index+1-skip_pairs:,} pairs", flush=True)
    after = [path.stat() for path in sources]
    if any((a.st_size, a.st_mtime_ns) != (b.st_size, b.st_mtime_ns) for a, b in zip(before, after)):
        raise ValueError("Source changed during extraction; partial fixture is not valid")
    for mate, path in enumerate(paths, 1):
        path.rename(output / f"R{mate}.fastq")
    manifest = {
        "status": "COMPLETE", "sampling": "contiguous_synchronized_segment" if skip_pairs else "first_N_synchronized_pairs",
        "pairs": pairs, "skip_pairs": skip_pairs, "first_source_pair": skip_pairs+1,
        "limitations": ["not random or cell-representative", "source gzip CRC and full-file checksum not verified"],
        "elapsed_seconds": time.monotonic()-start,
        "sources": [{"path": str(path), "bytes": stat.st_size, "mtime_ns": stat.st_mtime_ns}
                    for path, stat in zip(sources, before)],
        "outputs": [{"name": f"R{mate+1}.fastq", "sha256": digest.hexdigest(), "read_lengths": sorted(lengths[mate])}
                    for mate, digest in enumerate(hashes)],
    }
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    return manifest


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--r1", required=True)
    parser.add_argument("--r2", required=True)
    parser.add_argument("--output-dir", required=True)
    parser.add_argument("--pairs", type=int, default=1000000)
    parser.add_argument('--skip-pairs', type=int, default=0)
    args = parser.parse_args()
    print(json.dumps(subset(args.r1, args.r2, args.output_dir, args.pairs,args.skip_pairs), indent=2))
