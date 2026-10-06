#!/usr/bin/env python3
"""Sequential, source-bound real-prefix STAR regression and initial runtime measurements.

One run per binary is screening, not a replicated performance claim. No source data
is copied into reports and no production result is overwritten.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import time
from compare_raw_counts import matrix


def sha256(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for block in iter(lambda: stream.read(1024*1024), b""):
            digest.update(block)
    return digest.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fixture", required=True)
    parser.add_argument("--data-dir", required=True)
    parser.add_argument("--output-dir", required=True)
    parser.add_argument("--binary", action="append", required=True, help="label=/absolute/path")
    parser.add_argument("--threads", type=int, default=8)
    parser.add_argument("--legacy", action="store_true")
    parser.add_argument("--reference-run", type=Path, help="Reuse a successful run with identical arguments, avoiding another full index load")
    args = parser.parse_args()
    if args.threads < 1:
        parser.error("Thread count must be positive")
    labels = []
    for specification in args.binary:
        label, binary = specification.split("=", 1)
        if not label or any(c not in "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_" for c in label):
            parser.error("Unsafe binary label")
        if label in labels or not Path(binary).is_absolute() or not Path(binary).is_file():
            parser.error("Duplicate label or missing/nonabsolute binary")
        labels.append(label)
    if len(args.binary) < 2 and args.reference_run is None:
        parser.error("At least two binaries are required for a comparison")
    fixture, data, output = (Path(path).resolve() for path in (args.fixture, args.data_dir, args.output_dir))
    if args.reference_run is not None:
        args.reference_run = args.reference_run.resolve()
    manifest = json.loads((fixture/"manifest.json").read_text())
    if manifest["status"] != "COMPLETE":
        raise ValueError("Fixture is not complete")
    for artifact in manifest["outputs"]:
        if sha256(fixture/artifact["name"]) != artifact["sha256"]:
            raise ValueError("Fixture checksum mismatch")
    # Hash actual reference contents, not only paths or timestamps. Old runs
    # without this receipt are deliberately not eligible for automatic reuse.
    inputs = [fixture/"manifest.json", *(fixture/item["name"] for item in manifest["outputs"]),
              data/"whitelists/3M-february-2018.txt"]
    genome = data/"genome_cynomolgus"
    for name in ("Genome", "SA", "SAindex", "genomeParameters.txt",
                 "Macaca_fascicularis_6.0.115.cellranger_filtered.gtf"):
        if not (genome/name).is_file():
            raise ValueError(f"Required reference input absent: {genome/name}")
    inputs += sorted(path for path in genome.rglob("*") if path.is_file())
    input_stats = {str(path): (path.stat().st_size, path.stat().st_mtime_ns) for path in inputs}
    signatures = {str(path): sha256(path) for path in inputs}
    if any((path.stat().st_size, path.stat().st_mtime_ns) != input_stats[str(path)] for path in inputs):
        raise ValueError("Input changed during fingerprinting")
    output.mkdir(parents=True, exist_ok=False)
    runner = Path(__file__).read_bytes()
    (output/"runner_snapshot.py").write_bytes(runner)
    comparator = Path(__file__).with_name("compare_raw_counts.py").read_bytes()
    (output/"compare_raw_counts.py").write_bytes(comparator)
    common = ["--runThreadN", str(args.threads), "--genomeDir", str(data/"genome_cynomolgus"),
              "--readFilesIn", str(fixture/"R2.fastq"), str(fixture/"R1.fastq"),
              "--sjdbGTFfile", str(data/"genome_cynomolgus/Macaca_fascicularis_6.0.115.cellranger_filtered.gtf"),
              "--soloType", "CB_UMI_Simple", "--soloCBwhitelist", str(data/"whitelists/3M-february-2018.txt"),
              "--soloCBstart", "1", "--soloCBlen", "16", "--soloUMIstart", "17", "--soloUMIlen", "12",
              "--soloBarcodeReadLength", "0", "--clipAdapterType", "CellRanger4", "--soloFeatures", "Gene",
              "GeneFull_Ex50pAS", "Velocyto", "--soloMultiMappers", "EM", "--soloCellFilter", "EmptyDrops_CR",
              "--outSAMtype", "BAM", "Unsorted", "--outSAMattributes", "NH", "HI", "AS", "nM", "CR", "UR"]
    if args.legacy:
        common += ["--legacy", "True"]
    result = {"status": "RUNNING", "pairs": manifest["pairs"], "sampling": manifest["sampling"],
              "runner_sha256": hashlib.sha256(runner).hexdigest(),
              "count_comparator_sha256": hashlib.sha256(comparator).hexdigest(),
              "legacy": args.legacy, "threads": args.threads, "runs": [], "comparisons": [],
              "limitations": ["one run per binary; load/cache order confounded", "prefix not cell-representative",
                              "BAM semantic equivalence not checked by this script", "not full-library regression"]}
    result_path = output/"result.json"

    def save():
        result_path.write_text(json.dumps(result, indent=2))

    def reject(message):
        result["status"] = "FAILED_VALIDATION"
        result["error"] = message
        save()
        raise RuntimeError(message)

    def read_text(path):
        try:
            return path.read_text()
        except OSError as error:
            reject(f"Required evidence unavailable: {path}: {error}")

    def read_json(path):
        try:
            return json.loads(read_text(path))
        except ValueError as error:
            reject(f"Invalid evidence JSON: {path}: {error}")

    save()
    baseline = None
    baseline_run = args.reference_run
    if args.reference_run is not None:
        receipt = args.reference_run/"input_signatures.json"
        if not receipt.is_file() or read_json(receipt) != signatures:
            reject("Reference input provenance is missing or stale; rerun rather than relabel old evidence")
        saved_command = read_json(args.reference_run/"command.json")
        if not isinstance(saved_command, list) or len(saved_command) < 3 or saved_command[1:-2] != common or saved_command[-2] != "--outFileNamePrefix":
            reject("Reference arguments differ from requested profile")
        final_log = read_text(args.reference_run/"Log.final.out")
        observed = [line.split("|")[1].strip() for line in final_log.splitlines() if "Number of input reads" in line]
        if observed != [str(manifest["pairs"])]:
            reject("Reference processed-read count differs from fixture")
        baseline = read_json(args.reference_run/"artifact_hashes.json")
        if not isinstance(baseline, dict) or not baseline or any(not (args.reference_run/name).is_file() or sha256(args.reference_run/name) != digest for name, digest in baseline.items()):
            reject("Reference artifact signatures are missing or stale")
        producer = read_json(args.reference_run.parent/"result.json")
        if not isinstance(producer, dict):
            reject("Invalid reference producer record")
        producer_runs = [run for run in producer.get("runs", []) if run.get("label") == args.reference_run.name]
        if producer.get("status") not in ("PASSED_DECLARED_RAW_ARTIFACTS", "DIFFERENCES_REQUIRE_REVIEW") or len(producer_runs) != 1 or not producer_runs[0].get("binary_sha256"):
            reject("Reference producer is incomplete or unverified")
        result["reused_reference_binary_sha256"] = producer_runs[0]["binary_sha256"]
        if sha256(saved_command[0]) != result["reused_reference_binary_sha256"]:
            reject("Reference binary changed since execution")
        save()
    for specification in args.binary:
        label, binary = specification.split("=", 1)
        if not label or any(c not in "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_" for c in label):
            raise ValueError("Unsafe binary label")
        run = output/label
        run.mkdir()
        (run/"input_signatures.json").write_text(json.dumps(signatures, indent=2))
        binary_digest = sha256(binary)
        command = [binary, *common, "--outFileNamePrefix", str(run)+"/"]
        (run/"command.json").write_text(json.dumps(command))
        print(f"Starting {label}", flush=True)
        start = time.monotonic()
        with (run/"console.log").open("w") as console:
            process = subprocess.run(["/usr/bin/time", "-v", "-o", str(run/"time.txt"), *command],
                                     stdout=console, stderr=subprocess.STDOUT, cwd=run)
        item = {"label": label, "binary_sha256": binary_digest, "exit_code": process.returncode,
                "monotonic_elapsed_seconds": time.monotonic()-start}
        result["runs"].append(item)
        save()
        if process.returncode:
            result["status"] = "FAILED_EXECUTION"
            save()
            raise RuntimeError(f"{label} failed: inspect {run}/console.log")
        elapsed = [line.rsplit(": ", 1)[-1].strip() for line in read_text(run/"time.txt").splitlines()
                   if "Elapsed (wall clock)" in line]
        if len(elapsed) != 1:
            reject("GNU time wall-clock evidence missing")
        try:
            item["wall_seconds"] = sum(float(part)*60**index
                                       for index, part in enumerate(reversed(elapsed[0].split(":"))))
        except ValueError:
            reject("Invalid GNU time wall-clock evidence")
        if sha256(binary) != binary_digest or any(
                (path.stat().st_size, path.stat().st_mtime_ns) != input_stats[str(path)] for path in inputs):
            reject("Binary or input changed during execution")
        final_log = read_text(run/"Log.final.out")
        observed = [line.split("|")[1].strip() for line in final_log.splitlines() if "Number of input reads" in line]
        if observed != [str(manifest["pairs"])]:
            reject(f"Unexpected input-read count: {observed}")
        required = [Path("SJ.out.tab")]
        for feature in ("Gene", "GeneFull_Ex50pAS", "Velocyto"):
            raw = run/"Solo.out"/feature/"raw"
            expected = ["spliced.mtx", "unspliced.mtx", "ambiguous.mtx"] if feature == "Velocyto" else ["matrix.mtx"]
            for name in [*expected, "features.tsv", "barcodes.tsv"]:
                if not (raw/name).is_file():
                    reject(f"Required raw artifact absent: {raw/name}")
                required.append((raw/name).relative_to(run))
        hashes = {str(path): sha256(run/path) for path in required}
        try:
            for path in required:
                if path.suffix == ".mtx":
                    shape, _ = matrix(run/path)
                    axes = tuple(len(read_text(run/path.parent/name).splitlines())
                                 for name in ("features.tsv", "barcodes.tsv"))
                    if shape != axes:
                        reject(f"Matrix dimensions differ from axes: {path}")
        except (ValueError, StopIteration) as error:
            reject(f"Invalid integer matrix: {error}")
        (run/"artifact_hashes.json").write_text(json.dumps(hashes, indent=2))
        if baseline is None:
            baseline = hashes
            baseline_run = run
        else:
            if hashes.keys() != baseline.keys():
                reject("Reference declared artifact set differs from this contract")
            differences = [name for name, digest in hashes.items() if baseline[name] != digest]
            serialization_only = [name for name in differences if name.endswith(".mtx")
                                  and matrix(baseline_run/name) == matrix(run/name)]
            differences = [name for name in differences if name not in serialization_only]
            result["comparisons"].append({"label": label, "exact_raw_integer_matrices_axes_and_sj": not differences,
                                          "differing_artifacts": differences,
                                          "serialization_only_artifacts": serialization_only})
            save()
        print(f"Completed {label}: {item['wall_seconds']:.2f}s", flush=True)
    result["status"] = "PASSED_DECLARED_RAW_ARTIFACTS" if all(
        item["exact_raw_integer_matrices_axes_and_sj"] for item in result["comparisons"]) else "DIFFERENCES_REQUIRE_REVIEW"
    save()
    print(json.dumps(result, indent=2))
    if result["status"] != "PASSED_DECLARED_RAW_ARTIFACTS":
        raise SystemExit(2)


if __name__ == "__main__":
    main()
