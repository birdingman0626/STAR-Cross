"""Shared fail-closed qualification helpers; standard library only."""
import ctypes
from contextlib import closing
from ctypes import wintypes
import gzip
import hashlib
import math
import os
import platform
from pathlib import Path
import random
import sqlite3
import statistics
import struct
import subprocess
import sys
import tempfile
from test_cpu_upstream import bam_scientific_header, scientific_final_fields


def measured_run(command, console, cwd):
    """Whole child process elapsed is timed by caller; retain Windows OS peaks.

    STAR's direct process only. No claim about descendants or shared-memory RSS.
    Keep the handle open after exit so short-lived processes are measurable.
    """
    process = subprocess.Popen(command, stdout=console, stderr=subprocess.STDOUT, cwd=cwd)
    memory = {}
    if sys.platform == "win32":
        kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        kernel.GetSystemTimes.argtypes = [ctypes.POINTER(wintypes.FILETIME)]*3
        kernel.GetProcessTimes.argtypes = [wintypes.HANDLE]+[ctypes.POINTER(wintypes.FILETIME)]*4
        def ticks(value):
            return (value.dwHighDateTime << 32) | value.dwLowDateTime
        def cpu_times():
            system = [wintypes.FILETIME() for _ in range(3)]
            own = [wintypes.FILETIME() for _ in range(4)]
            if not kernel.GetSystemTimes(*(ctypes.byref(item) for item in system)):
                raise ctypes.WinError(ctypes.get_last_error())
            if not kernel.GetProcessTimes(wintypes.HANDLE(int(process._handle)),
                                          *(ctypes.byref(item) for item in own)):
                raise ctypes.WinError(ctypes.get_last_error())
            return ticks(system[0]), ticks(system[1])+ticks(system[2]), ticks(own[2])+ticks(own[3])
        kernel.GetActiveProcessorGroupCount.restype = wintypes.WORD
        single_group = kernel.GetActiveProcessorGroupCount() == 1
        previous = cpu_times()
        background = []
        while True:
            try:
                process.wait(timeout=0.25)
            except subprocess.TimeoutExpired:
                pass
            current = cpu_times()
            idle, total, own = (right-left for left, right in zip(previous, current))
            if single_group and total > 0:
                background.append(100*max(0, total-idle-own)/total)
            previous = current
            if process.returncode is not None:
                break
        memory.update(background_cpu_samples=len(background),
                      background_cpu_mean_percent=statistics.mean(background) if background else None,
                      background_cpu_p95_percent=sorted(background)[math.ceil(len(background)*0.95)-1] if background else None,
                      background_cpu_source="Windows GetSystemTimes minus STAR GetProcessTimes; single processor group only",
                      background_cpu_scope_verified=single_group)
        class Counters(ctypes.Structure):
            _fields_ = [("cb", wintypes.DWORD), ("PageFaultCount", wintypes.DWORD)] + [
                (name, ctypes.c_size_t) for name in (
                    "PeakWorkingSetSize", "WorkingSetSize", "QuotaPeakPagedPoolUsage",
                    "QuotaPagedPoolUsage", "QuotaPeakNonPagedPoolUsage", "QuotaNonPagedPoolUsage",
                    "PagefileUsage", "PeakPagefileUsage", "PrivateUsage")]
        counters = Counters()
        counters.cb = ctypes.sizeof(counters)
        api = ctypes.WinDLL("psapi", use_last_error=True).GetProcessMemoryInfo
        api.argtypes = [wintypes.HANDLE, ctypes.c_void_p, wintypes.DWORD]
        api.restype = wintypes.BOOL
        if not api(wintypes.HANDLE(int(process._handle)), ctypes.byref(counters), counters.cb):
            raise ctypes.WinError(ctypes.get_last_error())
        memory.update({"peak_rss_bytes": counters.PeakWorkingSetSize,
                  "peak_commit_bytes": counters.PeakPagefileUsage,
                  "memory_source": "Windows GetProcessMemoryInfo, STAR process only"})
    else:
        process.wait()
    return process.returncode, memory


def bam_signature(path):
    """Order-independent exact record multiset, disk-backed with bounded SQL cache.

    Header reference order is checked separately. No lossy aggregate/count hash.
    Temporary database is only comparator scratch, never a scientific result.
    """
    digest = hashlib.sha256()
    # Keep the large record database on the BAM's data volume. SQLite's sorter
    # may additionally use OS temporary storage; allow space on both volumes.
    with tempfile.TemporaryDirectory(prefix="star-bam-compare-",dir=Path(path).parent) as scratch:
        with closing(sqlite3.connect(str(Path(scratch)/"records.sqlite"))) as db:
            db.execute("PRAGMA cache_size=-8192")
            db.execute("PRAGMA temp_store=FILE")
            db.execute("CREATE TABLE records (value BLOB NOT NULL)")
            with gzip.open(path, "rb") as stream:
                def read(size):
                    data = stream.read(size)
                    if len(data) != size:
                        raise ValueError("Truncated BAM")
                    return data
                def integer():
                    return struct.unpack("<i", read(4))[0]
                if read(4) != b"BAM\1":
                    raise ValueError("Invalid BAM magic")
                length = integer()
                if length < 0:
                    raise ValueError("Invalid BAM header")
                read(length)
                refs = integer()
                if refs < 0:
                    raise ValueError("Invalid BAM reference count")
                for _ in range(refs):
                    length = integer()
                    if length <= 0:
                        raise ValueError("Invalid BAM reference name")
                    read(length+4)
                count = 0
                batch = []
                while True:
                    size = stream.read(4)
                    if not size:
                        break
                    if len(size) != 4:
                        raise ValueError("Truncated BAM record size")
                    length = struct.unpack("<i", size)[0]
                    if not 32 <= length <= 16*1024*1024:
                        raise ValueError("Invalid BAM record size")
                    batch.append((read(length),))
                    count += 1
                    if len(batch) == 1000:
                        db.executemany("INSERT INTO records VALUES (?)", batch)
                        batch.clear()
                db.executemany("INSERT INTO records VALUES (?)", batch)
            for (record,) in db.execute("SELECT value FROM records ORDER BY value"):
                digest.update(struct.pack("<Q", len(record)))
                digest.update(record)
    return {"records": count, "sha256": digest.hexdigest(), "header": bam_scientific_header(path)}


def paired_assessment(runs, labels, seed=1729):
    """Paired median-ratio bootstrap; small/missing/noisy evidence is not a pass."""
    rounds = sorted({run["round"] for run in runs if not run["warmup"]})
    pairs = []
    memory_deltas = []
    for number in rounds:
        pair = [next(run for run in runs if run["round"] == number and run["label"] == label
                     and not run["warmup"]) for label in labels]
        if any(run["wall_seconds"] <= 0 or not math.isfinite(run["wall_seconds"]) for run in pair):
            raise ValueError("Invalid measured time")
        pairs.append(pair[1]["wall_seconds"]/pair[0]["wall_seconds"])
        if all(run.get("peak_rss_bytes", 0) > 0 for run in pair):
            memory_deltas.append(pair[1]["peak_rss_bytes"]-pair[0]["peak_rss_bytes"])
    rng = random.Random(seed)
    medians = sorted(statistics.median(rng.choices(pairs, k=len(pairs))) for _ in range(10000))
    lower, upper = medians[249], medians[9749]
    median = statistics.median(pairs)
    calibration_deviation = statistics.median(abs(ratio-1) for ratio in pairs)
    reference_peak = [run["peak_rss_bytes"] for run in runs if run["label"] == labels[0]
                      and not run["warmup"] and run.get("peak_rss_bytes", 0) > 0]
    memory_limit = max(64*1024*1024, statistics.median(reference_peak)*0.01) if reference_peak else None
    identical = (len({run["binary_sha256"] for run in runs}) == 1
                 and len({run.get("requested_gpu_mode", "off") for run in runs}) == 1)
    enough = len(pairs) >= (5 if identical else 10)
    # A median alone can hide a minority of extreme interruptions. Require the
    # predeclared interval too; do not loosen it after observing a noisy run.
    calibrated = identical and enough and calibration_deviation <= 0.02 and lower >= 0.98 and upper <= 1.02
    memory_ok = len(memory_deltas) == len(pairs) and statistics.median(memory_deltas) <= memory_limit
    return {"status": "CALIBRATION_PASS" if calibrated and memory_ok else
            "INCONCLUSIVE" if identical or not enough or not memory_ok else
            "TIME_NONREGRESSION_PASS" if upper <= 1.05 else "TIME_REGRESSION",
            "pairs": len(pairs), "median_time_ratio": median,
            "AA_median_absolute_relative_deviation": calibration_deviation if identical else None,
            "paired_bootstrap_95_percent": [lower, upper], "bootstrap_seed": seed,
            "memory_gate_pass": memory_ok, "memory_limit_bytes": memory_limit,
            "median_peak_rss_delta_bytes": statistics.median(memory_deltas) if memory_deltas else None,
            "requires_independent_AA_calibration": not identical,
            "speedup_supported_by_interval": not identical and enough and upper < 1 and memory_ok}


def background_environment_pass(runs, limit):
    measured = [run for run in runs if not run["warmup"]]
    return bool(measured) and all(run.get("background_cpu_samples", 0) > 0 and
                run.get("background_cpu_scope_verified", False) and
                all(run.get(key) is not None and math.isfinite(run[key]) and 0 <= run[key] <= limit
                    for key in ("background_cpu_mean_percent", "background_cpu_p95_percent"))
                for run in measured)


def machine_identity():
    """Local calibration identity; no raw hostname in the receipt."""
    return {"host_sha256": hashlib.sha256(platform.node().encode()).hexdigest(),
            "platform": platform.platform(), "processor": platform.processor(),
            "logical_cpus": os.cpu_count(), "python": sys.version}


def execution_environment():
    """Only performance-relevant switches; never capture credentials wholesale."""
    names = ("STAR_CHUNK_PIPELINE", "STAR_ASYNC_SAM", "STAR_ASYNC_BAM",
             "STAR_SEED_TRACE_DIR", "STAR_SEED_TRACE_MODULUS", "STAR_PROFILE_SA_SORT",
             "OMP_NUM_THREADS", "OMP_THREAD_LIMIT", "OMP_DYNAMIC", "OMP_SCHEDULE",
             "OMP_PROC_BIND", "OMP_PLACES", "OMP_WAIT_POLICY", "KMP_AFFINITY",
             "KMP_BLOCKTIME", "GOMP_CPU_AFFINITY")
    return {name: os.environ.get(name) for name in names}


def calibration_profile_matches(calibration, current):
    keys = ("legacy", "threads", "pairs", "machine_identity", "execution_environment", "runner_sha256",
            "count_comparator_sha256", "helper_sha256")
    return all(key in calibration and key in current and calibration[key] == current[key] for key in keys)


def performance_acceptance(scientific_status, assessment, environment_ok, calibration_ok=False):
    if scientific_status != "PASSED_DECLARED_RAW_ARTIFACTS":
        return "UNVERIFIED_SCIENTIFIC_CONTRACT"
    if not environment_ok:
        return "UNVERIFIED_ENVIRONMENT"
    if assessment == "CALIBRATION_PASS":
        return "CALIBRATION_PASS"
    if assessment == "TIME_NONREGRESSION_PASS" and calibration_ok:
        return "NONREGRESSION_PASS"
    return "UNVERIFIED_ASSESSMENT_OR_CALIBRATION"
