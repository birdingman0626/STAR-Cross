"""Shared fail-closed qualification helpers; standard library only."""
import ctypes
from contextlib import closing
from ctypes import wintypes
import gzip
import hashlib
import math
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
    process.wait()
    memory = {}
    if sys.platform == "win32":
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
        memory = {"peak_rss_bytes": counters.PeakWorkingSetSize,
                  "peak_commit_bytes": counters.PeakPagefileUsage,
                  "memory_source": "Windows GetProcessMemoryInfo, STAR process only"}
    return process.returncode, memory


def bam_signature(path):
    """Order-independent exact record multiset, disk-backed with bounded SQL cache.

    Header reference order is checked separately. No lossy aggregate/count hash.
    Temporary database is only comparator scratch, never a scientific result.
    """
    digest = hashlib.sha256()
    with tempfile.TemporaryDirectory(prefix="star-bam-compare-") as scratch:
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
    calibrated = identical and enough and calibration_deviation <= 0.02
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
