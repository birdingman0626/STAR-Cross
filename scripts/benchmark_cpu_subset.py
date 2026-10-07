#!/usr/bin/env python3
"""Source-bound real-prefix regression with optional paired runtime measurements.

One run per binary is screening, not a replicated performance claim. No source data
is copied into reports and no production result is overwritten.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import time
import sys
import random
from qualification import measured_run, bam_signature, paired_assessment, scientific_final_fields
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
    parser.add_argument("--rounds", type=int, default=1)
    parser.add_argument("--warmups", type=int, default=0)
    parser.add_argument("--order-seed", type=int, default=1729)
    parser.add_argument("--full-contract", action="store_true", help="Also compare BAM/header, scientific logs and all Solo matrices/axes; floats exact by default")
    parser.add_argument("--calibration-receipt", type=Path, help="Successful independent A/A result.json; required before accepting paired performance")
    parser.add_argument("--legacy", action="store_true")
    parser.add_argument("--gpu-sjdb-remap", action="append", default=[], help="label=off|auto|required, explicit experimental backend per binary")
    parser.add_argument("--reference-run", type=Path, help="Reuse a successful run with identical arguments, avoiding another full index load")
    args = parser.parse_args()
    if args.threads < 1:
        parser.error("Thread count must be positive")
    if args.rounds < 1 or args.warmups < 0:
        parser.error("Invalid repetitions")
    if args.rounds > 1 and (not args.full_contract or len(args.binary) != 2 or args.reference_run):
        parser.error("Paired runs require exactly two binaries, full contract and no reused run")
    if args.full_contract and args.reference_run:
        parser.error("Full-contract reuse is not qualified; rerun both binaries")
    labels = []
    for specification in args.binary:
        label, binary = specification.split("=", 1)
        if not label or any(c not in "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_" for c in label):
            parser.error("Unsafe binary label")
        if label in labels or not Path(binary).is_absolute() or not Path(binary).is_file():
            parser.error("Duplicate label or missing/nonabsolute binary")
        labels.append(label)
    gpu_modes = {}
    for specification in args.gpu_sjdb_remap:
        label, mode = specification.split("=", 1)
        if label not in labels or label in gpu_modes or mode not in ("off", "auto", "required"):
            parser.error("Invalid or duplicate GPU backend assignment")
        gpu_modes[label] = mode
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
    helper_signatures = {}
    for name in ("qualification.py", "test_cpu_upstream.py"):
        payload = Path(__file__).with_name(name).read_bytes()
        (output/name).write_bytes(payload)
        helper_signatures[name] = hashlib.sha256(payload).hexdigest()
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
              "helper_sha256": helper_signatures,
              "legacy": args.legacy, "threads": args.threads, "runs": [], "comparisons": [],
              "limitations": ["one run per binary; load/cache order confounded", "prefix not cell-representative",
                              "BAM semantic equivalence not checked by this script", "not full-library regression"]}
    if args.full_contract:
        result["limitations"].remove("BAM semantic equivalence not checked by this script")
        result["contract"] = "raw integer counts/axes/SJ; exact BAM multiset/header, all Solo matrices/axes, filtered/EM and scientific final fields"
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
    if args.calibration_receipt:
        calibration = read_json(args.calibration_receipt)
        calibration_labels = list(dict.fromkeys(run["label"] for run in calibration.get("runs", [])))
        if len(calibration_labels) != 2 or calibration.get("status") != "PASSED_DECLARED_RAW_ARTIFACTS":
            reject("Calibration receipt incomplete")
        if paired_assessment(calibration["runs"], calibration_labels).get("status") != "CALIBRATION_PASS":
            reject("Calibration receipt did not pass")
        if calibration.get("pairs") != manifest["pairs"] or calibration.get("threads") != args.threads:
            reject("Calibration workload differs")
        for prior in calibration["runs"]:
            prior_dir = args.calibration_receipt.parent/prior["run_directory"]
            if (prior.get("binary_sha256") != sha256(args.binary[0].split("=", 1)[1])
                    or prior.get("requested_gpu_mode", "off") != gpu_modes.get(labels[0], "off")
                    or read_json(prior_dir/"input_signatures.json") != signatures):
                reject("Calibration input/binary/backend differs")
            for name in ("full_contract_signature", "full_artifact_hashes"):
                if not prior.get(name+"_sha256") or sha256(prior_dir/(name+".json")) != prior[name+"_sha256"]:
                    reject("Calibration full-contract provenance missing or changed")
            for name, digest in read_json(prior_dir/"full_artifact_hashes.json").items():
                if sha256(prior_dir/name) != digest:
                    reject("Calibration scientific artifact changed")
        result["calibration_receipt_sha256"] = sha256(args.calibration_receipt)
        result["performance_acceptance"] = "UNVERIFIED_ENVIRONMENT"
    save()
    schedule = []
    rng = random.Random(args.order_seed)
    reverse_pair = False
    for round_number in range(-args.warmups, args.rounds):
        order = list(args.binary)
        if round_number % 2 == 0:
            reverse_pair = rng.choice((False, True))
        if args.rounds > 1 and reverse_pair != bool(round_number % 2):
            order.reverse()
        schedule.extend((round_number, specification) for specification in order)
    full_baseline = None
    for round_number, specification in schedule:
        label, binary = specification.split("=", 1)
        if not label or any(c not in "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_" for c in label):
            raise ValueError("Unsafe binary label")
        run = output/(label if args.rounds == 1 and args.warmups == 0 else f"round{round_number}_{label}")
        run.mkdir()
        (run/"input_signatures.json").write_text(json.dumps(signatures, indent=2))
        binary_digest = sha256(binary)
        backend = ["--gpuSjdbRemap", gpu_modes[label]] if label in gpu_modes else []
        command = [binary, *common, *backend, "--outFileNamePrefix", str(run)+"/"]
        (run/"command.json").write_text(json.dumps(command))
        print(f"Starting {label}", flush=True)
        start = time.monotonic()
        with (run/"console.log").open("w") as console:
            invocation = command if sys.platform == "win32" else ["/usr/bin/time", "-v", "-o", str(run/"time.txt"), *command]
            exit_code, memory = measured_run(invocation, console, run)
        item = {"label": label, "round": round_number, "warmup": round_number < 0,
                "run_directory": run.name, "binary_sha256": binary_digest, "exit_code": exit_code, **memory,
                "requested_gpu_mode": gpu_modes.get(label, "off"),
                "monotonic_elapsed_seconds": time.monotonic()-start}
        result["runs"].append(item)
        save()
        if exit_code:
            result["status"] = "FAILED_EXECUTION"
            save()
            raise RuntimeError(f"{label} failed: inspect {run}/console.log")
        if sys.platform == "win32":
            item["wall_seconds"] = item["monotonic_elapsed_seconds"]
            item["wall_clock_source"] = "Windows monotonic whole-process elapsed"
            item["peak_rss_status"] = "MEASURED"
            (run/"time.txt").write_text(f"Windows whole-process wall seconds: {item['wall_seconds']}\nPeak RSS bytes: {item['peak_rss_bytes']}\n")
        elapsed = [line.rsplit(": ", 1)[-1].strip() for line in read_text(run/"time.txt").splitlines()
                   if "Elapsed (wall clock)" in line]
        if sys.platform != "win32" and len(elapsed) != 1:
            reject("GNU time wall-clock evidence missing")
        try:
            if sys.platform != "win32":
                item["wall_clock_source"] = "GNU time elapsed wall clock"
                item["wall_seconds"] = sum(float(part)*60**index
                                       for index, part in enumerate(reversed(elapsed[0].split(":"))))
        except ValueError:
            reject("Invalid GNU time wall-clock evidence")
        if sys.platform != "win32":
            rss = [line.rsplit(":", 1)[-1].strip() for line in read_text(run/"time.txt").splitlines()
                   if "Maximum resident set size" in line]
            if len(rss) != 1 or not rss[0].isdigit() or int(rss[0]) <= 0:
                reject("Peak memory evidence missing")
            item["peak_rss_bytes"] = int(rss[0])*1024
            item["memory_source"] = "GNU time maximum resident set size"
        if sha256(binary) != binary_digest or any(
                (path.stat().st_size, path.stat().st_mtime_ns) != input_stats[str(path)] for path in inputs):
            reject("Binary or input changed during execution")
        final_log = read_text(run/"Log.final.out")
        item["gpu_backend_events"] = [line for line in read_text(run/"Log.out").splitlines()
                                      if line.startswith("GPU_SJDB_REMAP ")] if backend else []
        if gpu_modes.get(label) == "required" and (not item["gpu_backend_events"] or any(
                not line.startswith("GPU_SJDB_REMAP status=0 ") for line in item["gpu_backend_events"])):
            reject("Required GPU backend did not complete; CPU output cannot qualify GPU execution")
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
        if args.full_contract:
            from decimal import Decimal, InvalidOperation
            try:
                full = {"bam": bam_signature(run/"Aligned.out.bam"),
                        "scientific_log": scientific_final_fields(run/"Log.final.out"), "solo": {}}
            except (ValueError, OSError, AssertionError, EOFError) as error:
                reject(f"BAM/log contract invalid: {error}")
            for path in sorted((run/"Solo.out").rglob("*")):
                if path.is_file() and path.suffix in (".mtx", ".tsv"):
                    name = path.relative_to(run).as_posix()
                    if path.suffix == ".mtx":
                        with path.open() as stream:
                            header = stream.readline().strip()
                            if header not in ("%%MatrixMarket matrix coordinate integer general",
                                              "%%MatrixMarket matrix coordinate real general"):
                                reject(f"Unsupported matrix contract: {name}")
                            rows = [line.split() for line in stream if line.strip() and not line.startswith("%")]
                        shape = tuple(map(int, rows.pop(0)))
                        if len(shape) != 3 or shape[2] != len(rows):
                            reject(f"Invalid matrix dimensions: {name}")
                        coordinates = {}
                        for row, column, value in rows:
                            key = (int(row), int(column))
                            try:
                                value = Decimal(value)
                            except InvalidOperation:
                                reject(f"Invalid numeric matrix entry: {name}")
                            if not value.is_finite() or value < 0 or not (1 <= key[0] <= shape[0] and 1 <= key[1] <= shape[1]):
                                reject(f"Invalid matrix entry: {name}")
                            if " integer " in header and value != value.to_integral_value():
                                reject(f"Noninteger value in integer matrix: {name}")
                            if key in coordinates:
                                reject(f"Duplicate coordinate in full-contract matrix: {name}")
                            coordinates[key] = value
                        axes = tuple(len(read_text(path.parent/name).splitlines()) for name in ("features.tsv", "barcodes.tsv"))
                        if shape[:2] != axes:
                            reject(f"Full-contract matrix axes differ: {name}")
                        full["solo"][name] = (shape[:2], sorted(coordinates.items()))
                    else:
                        full["solo"][name] = sha256(path)
            for feature in ("Gene", "GeneFull_Ex50pAS"):
                if f"Solo.out/{feature}/filtered/matrix.mtx" not in full["solo"]:
                    reject(f"Filtered output missing for {feature}")
                if f"Solo.out/{feature}/raw/UniqueAndMult-EM.mtx" not in full["solo"]:
                    reject(f"EM output missing for {feature}")
            if full_baseline is None:
                full_baseline = full
            elif full != full_baseline:
                reject("Full scientific contract differs; performance acceptance blocked")
            item["full_scientific_contract"] = "PASS"
            signature_file = run/"full_contract_signature.json"
            signature_file.write_text(json.dumps(full, default=str))
            item["full_contract_signature_sha256"] = sha256(signature_file)
            full_hashes = {path.relative_to(run).as_posix(): sha256(path) for path in run.rglob("*")
                           if path.is_file() and (path.suffix in (".bam", ".mtx", ".tsv") or path.name in ("SJ.out.tab", "Log.final.out"))}
            (run/"full_artifact_hashes.json").write_text(json.dumps(full_hashes, indent=2))
            item["full_artifact_hashes_sha256"] = sha256(run/"full_artifact_hashes.json")
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
    if args.rounds > 1:
        result["paired_assessment"] = paired_assessment(result["runs"], labels, args.order_seed)
        result["performance_acceptance"] = "UNVERIFIED_ENVIRONMENT_AND_CALIBRATION"
        if args.calibration_receipt:
            result["performance_acceptance"] = "UNVERIFIED_ENVIRONMENT"
        result["limitations"] = ["prefix not cell-representative", "not full-library regression",
                                "performance requires idle-machine and independent calibration evidence",
                                "float matrices compared exactly; no post-hoc tolerance"]
        save()
    print(json.dumps(result, indent=2))
    if result["status"] != "PASSED_DECLARED_RAW_ARTIFACTS":
        raise SystemExit(2)


if __name__ == "__main__":
    main()
