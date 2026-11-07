#!/usr/bin/env python3
"""Linux paired A/B (or same-binary A/A) STARsolo benchmark on a frozen index.

Order alternates ABBA per round so drift and cache warm-up do not favour one
label. Each run must reproduce the first run's scientific artifacts exactly
(BAM record multiset/header, SJ.out.tab, every Solo.out file, scientific
Log.final.out fields); timing is only assessed once that contract holds. The
same paired_assessment statistics as the Windows harness decide the verdict.

Background load is the host's non-STAR busy CPU during each run, from
/proc/stat minus STAR's own rusage. It is evidence, not an idle guarantee
(hypervisor steal and frequency changes are recorded or out of scope).

Under WSL2, /proc/stat covers the Linux VM only, so Windows programs on the
same CPUs are invisible to it. The Windows host is then read too, through WSL
interop (WMI raw counter Win32_PerfRawData_PerfOS_Processor, locale
independent): host background = host busy CPU minus the VM's busy CPU, over the
host's CPU count. It also counts the VM's own virtualization overhead, so it
errs high. A WSL run whose host load cannot be read fails the environment gate.
"""
import argparse
import hashlib
import json
import os
import platform
import subprocess
import sys
import threading
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from qualification import bam_signature, paired_assessment, machine_identity, execution_environment  # noqa: E402
from test_cpu_upstream import scientific_final_fields  # noqa: E402


def sha256(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for block in iter(lambda: stream.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def cpu_jiffies():
    fields = Path("/proc/stat").read_text().splitlines()[0].split()[1:]
    values = list(map(int, fields))
    idle = values[3] + values[4]
    steal = values[7] if len(values) > 7 else 0
    return sum(values[:8]) - idle - steal, sum(values[:8]), steal


def is_wsl():
    return Path("/proc/sys/fs/binfmt_misc/WSLInterop").exists() or "microsoft" in platform.release().lower()


HOST_QUERY = ("$p = Get-CimInstance Win32_PerfRawData_PerfOS_Processor -Filter \"Name='_Total'\"; "
              "$n = (Get-CimInstance Win32_ComputerSystem).NumberOfLogicalProcessors; "
              "\"$($p.PercentProcessorTime) $($p.Timestamp_Sys100NS) $n\"")


def windows_host_cpu():
    """(idle 100 ns ticks averaged over CPUs, timestamp 100 ns, logical CPUs) of the Windows host, or None."""
    try:
        out = subprocess.run(["powershell.exe", "-NoProfile", "-NonInteractive", "-Command", HOST_QUERY],
                             capture_output=True, text=True, timeout=120).stdout.split()
        idle, stamp, cpus = map(int, out)
        return idle, stamp, cpus
    except (OSError, ValueError, subprocess.SubprocessError):
        return None


def host_background_percent(before, after, vm_busy_cpu_seconds):
    """Windows host busy CPU not spent by the WSL VM, as a percent of all host CPUs."""
    seconds = (after[1] - before[1]) / 1e7
    if seconds <= 0 or after[2] <= 0:
        return None
    busy_fraction = min(1.0, max(0.0, 1 - (after[0] - before[0]) / (after[1] - before[1])))
    other = max(0.0, busy_fraction * after[2] * seconds - vm_busy_cpu_seconds)
    return 100 * other / (after[2] * seconds)


def format_percent(value):
    return "unknown" if value is None else f"{value:.2f}%"


def environment_gate(runs, max_background, max_host_background=None):
    """Every measured run under both limits; with a host limit (WSL), unknown host load fails."""
    for run in runs:
        if run["background_cpu_mean_percent"] > max_background:
            return False
        if max_host_background is not None:
            host_bg = run.get("host_background_cpu_mean_percent")
            if host_bg is None or host_bg > max_host_background:
                return False
    return True


def measured_run(command, console, host=False):
    tick = os.sysconf("SC_CLK_TCK")
    host0 = windows_host_cpu() if host else None
    busy0, total0, steal0 = cpu_jiffies()
    started = time.monotonic()
    process = subprocess.Popen(command, stdout=console, stderr=subprocess.STDOUT)
    samples, stop = [], threading.Event()

    def sample():  # 1 s system busy fraction, for spike visibility only
        last = cpu_jiffies()
        while not stop.wait(1.0):
            now = cpu_jiffies()
            if now[1] > last[1]:
                samples.append((now[0] - last[0]) / (now[1] - last[1]))
            last = now
    sampler = threading.Thread(target=sample, daemon=True)
    sampler.start()
    _, status, usage = os.wait4(process.pid, 0)
    wall = time.monotonic() - started
    stop.set()
    sampler.join()
    busy1, total1, steal1 = cpu_jiffies()
    host1 = windows_host_cpu() if host else None
    cpus = os.cpu_count()
    star_cpu = usage.ru_utime + usage.ru_stime
    other = max(0.0, (busy1 - busy0) / tick - star_cpu)
    host_measure = {}
    if host:
        host_bg = host_background_percent(host0, host1, (busy1 - busy0) / tick) if host0 and host1 else None
        host_measure = {"host_background_cpu_mean_percent": host_bg, "host_logical_cpus": host1[2] if host1 else None}
    return os.waitstatus_to_exitcode(status), {**host_measure,
        "wall_seconds": wall, "peak_rss_bytes": usage.ru_maxrss * 1024,
        "user_seconds": usage.ru_utime, "system_seconds": usage.ru_stime,
        "major_faults": usage.ru_majflt, "voluntary_switches": usage.ru_nvcsw,
        "involuntary_switches": usage.ru_nivcsw,
        "background_cpu_mean_percent": 100 * other / (cpus * wall),
        "steal_percent": 100 * (steal1 - steal0) / max(1, total1 - total0),
        "system_busy_samples": len(samples),
    }


def artifacts(run_dir):
    signature = {"bam": bam_signature(run_dir / "Aligned.out.bam"),
                 "sj": sha256(run_dir / "SJ.out.tab"),
                 "final": scientific_final_fields(run_dir / "Log.final.out"),
                 "solo": {str(p.relative_to(run_dir)): sha256(p)
                          for p in sorted((run_dir / "Solo.out").rglob("*")) if p.is_file()}}
    return signature


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fixture", type=Path, required=True)
    parser.add_argument("--genome-dir", type=Path, required=True)
    parser.add_argument("--whitelist", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--binary", action="append", required=True, help="label=/absolute/path (one for A/A, two for A/B)")
    parser.add_argument("--rounds", type=int, default=5)
    parser.add_argument("--warmups", type=int, default=1)
    parser.add_argument("--threads", type=int, default=8)
    parser.add_argument("--max-background-cpu-percent", type=float, default=5.0)
    parser.add_argument("--max-host-background-cpu-percent", type=float, default=None,
                        help="WSL only: limit for Windows host load outside the VM (default: --max-background-cpu-percent)")
    parser.add_argument("--extra", nargs=argparse.REMAINDER, default=[], help="Extra STAR arguments, appended identically for all labels")
    args = parser.parse_args()
    specs = [b.split("=", 1) for b in args.binary]
    if not 1 <= len(specs) <= 2 or any(len(s) != 2 or not Path(s[1]).is_absolute() for s in specs):
        parser.error("Give one (A/A) or two (A/B) label=/absolute/path binaries")
    if len(specs) == 1:
        specs = [[specs[0][0] + "-a", specs[0][1]], [specs[0][0] + "-b", specs[0][1]]]
    labels = [label for label, _ in specs]
    if len(set(labels)) != 2:
        parser.error("Labels must differ")
    manifest = json.loads((args.fixture / "manifest.json").read_text())
    if manifest.get("status") != "COMPLETE" or any(
            sha256(args.fixture / item["name"]) != item["sha256"] for item in manifest["outputs"]):
        raise SystemExit("Fixture incomplete or checksum mismatch")
    args.output_dir.mkdir(parents=True, exist_ok=False)
    common = ["--runThreadN", str(args.threads), "--genomeDir", str(args.genome_dir),
              "--readFilesIn", str(args.fixture / "R2.fastq"), str(args.fixture / "R1.fastq"),
              "--soloType", "CB_UMI_Simple", "--soloCBwhitelist", str(args.whitelist),
              "--soloCBstart", "1", "--soloCBlen", "16", "--soloUMIstart", "17", "--soloUMIlen", "12",
              "--soloBarcodeReadLength", "0", "--clipAdapterType", "CellRanger4", "--soloFeatures", "Gene",
              "GeneFull_Ex50pAS", "Velocyto", "--soloMultiMappers", "EM", "--soloCellFilter", "EmptyDrops_CR",
              "--outSAMtype", "BAM", "Unsorted", "--outSAMattributes", "NH", "HI", "AS", "nM", "CR", "UR",
              *args.extra]
    host = is_wsl()
    host_limit = (args.max_background_cpu_percent if args.max_host_background_cpu_percent is None
                  else args.max_host_background_cpu_percent) if host else None
    genome_files = {name: sha256(args.genome_dir / name) for name in ("Genome", "SA", "SAindex", "genomeParameters.txt")}
    result = {"status": "RUNNING", "labels": labels, "pairs_in_fixture": manifest["pairs"],
              "fixture": str(args.fixture), "genome_sha256": genome_files,
              "binaries": {label: {"path": path, "sha256": sha256(path)} for label, path in specs},
              "machine_identity": machine_identity(), "kernel": platform.release(),
              "execution_environment": execution_environment(), "threads": args.threads,
              "max_background_cpu_percent": args.max_background_cpu_percent,
              "host_load_source": "windows-wmi-raw" if host else None,
              "max_host_background_cpu_percent": host_limit,
              "runner_sha256": sha256(__file__), "common_arguments": common, "runs": []}
    save = lambda: (args.output_dir / "result.json").write_text(json.dumps(result, indent=1))
    save()
    reference = None
    for number in range(-args.warmups, args.rounds):
        order = specs if number % 2 == 0 else specs[::-1]
        for label, path in order:
            run_dir = args.output_dir / f"round{number}_{label}"
            run_dir.mkdir()
            command = [path, *common, "--outFileNamePrefix", str(run_dir) + "/"]
            (run_dir / "command.json").write_text(json.dumps(command))
            with (run_dir / "console.log").open("w") as console:
                code, measure = measured_run(command, console, host)
            if code:
                result["status"] = "RUN_FAILED"
                result["error"] = f"{run_dir.name} exited {code}"
                save()
                raise SystemExit(result["error"])
            signature = artifacts(run_dir)
            if reference is None:
                reference = signature
                (args.output_dir / "reference_signature.json").write_text(
                    json.dumps(signature, indent=1, default=lambda raw: raw.hex()))  # BAM header bytes
            identical = signature == reference
            result["runs"].append(dict(label=label, round=number, warmup=number < 0,
                                       binary_sha256=result["binaries"][label]["sha256"],
                                       scientific_identical=identical, **measure))
            print(f"{run_dir.name}: {measure['wall_seconds']:.2f}s rss={measure['peak_rss_bytes']/2**30:.2f}GiB "
                  f"bg={measure['background_cpu_mean_percent']:.2f}%"
                  + (f" host_bg={format_percent(measure['host_background_cpu_mean_percent'])}" if host else "")
                  + f" identical={identical}", flush=True)
            if not identical:
                result["status"] = "SCIENTIFIC_MISMATCH"
                save()
                raise SystemExit(f"{run_dir.name} differs from the reference artifacts")
            # Keep receipts, drop bulky outputs once signed.
            for bulky in ("Aligned.out.bam",):
                (run_dir / bulky).unlink(missing_ok=True)
            save()
    measured = [run for run in result["runs"] if not run["warmup"]]
    environment_ok = environment_gate(measured, args.max_background_cpu_percent, host_limit)
    assessment = paired_assessment(result["runs"], labels)
    result.update(status="COMPLETE", scientific_contract="PASS", environment_gate_pass=environment_ok,
                  paired_assessment=assessment)
    save()
    print(json.dumps({"environment_gate_pass": environment_ok, **assessment}, indent=1))


if __name__ == "__main__":
    main()
