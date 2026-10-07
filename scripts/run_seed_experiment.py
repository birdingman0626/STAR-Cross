#!/usr/bin/env python3
"""Content-bound isolated seed-search benchmark; not a full STAR acceleration claim."""
import argparse
import json
from pathlib import Path
import subprocess
import time
from benchmark_cpu_subset import sha256

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ("benchmark","genome","fastq","output"):
        parser.add_argument("--"+name,required=True,type=Path)
    parser.add_argument("--reads",type=int,default=100000)
    parser.add_argument("--batch",type=int,default=65536)
    parser.add_argument("--repeats",type=int,default=3)
    parser.add_argument("--cpu-threads",type=int,default=8)
    args=parser.parse_args()
    if min(args.reads,args.batch,args.repeats,args.cpu_threads)<1:parser.error("positive run parameters required")
    binary=args.benchmark.resolve();genome=args.genome.resolve();fastq=args.fastq.resolve()
    capture=fastq.is_dir()
    query_files=sorted(fastq.glob('queries-*.bin')) if capture else [fastq]
    inputs=[binary,*query_files,*(genome/name for name in (("Genome","SA","genomeParameters.txt") if capture else ("Genome","SA","SAindex","genomeParameters.txt")))]
    if capture and not query_files:parser.error("empty capture directory")
    if not all(p.is_file() for p in inputs):parser.error("missing binary/input")
    args.output.mkdir(parents=True,exist_ok=False)
    stamps={str(p):(p.stat().st_size,p.stat().st_mtime_ns) for p in inputs}
    signatures={str(p):sha256(p) for p in inputs}
    if any((p.stat().st_size,p.stat().st_mtime_ns)!=stamps[str(p)] for p in inputs):
        raise ValueError("input changed during fingerprinting")
    command=[str(binary),str(genome),str(fastq),str(args.reads),str(args.batch),str(args.repeats),str(args.cpu_threads)]
    (args.output/"command.json").write_text(json.dumps(command,indent=2))
    (args.output/"input_signatures.json").write_text(json.dumps(signatures,indent=2))
    (args.output/"runner_snapshot.py").write_bytes(Path(__file__).read_bytes())
    source=Path(__file__).resolve().parents[1]
    for name in ("source/gpuSeedSearch.cu","source/gpuSeedSearch.h","source/SuffixArrayFuns.cpp","test/benchmark_seed_search.cpp"):
        (args.output/Path(name).name).write_bytes((source/name).read_bytes())
    start=time.monotonic();process=subprocess.run(command,capture_output=True,text=True)
    (args.output/"console.log").write_text(process.stdout+process.stderr)
    result={"status":"FAILED_EXECUTION","exit_code":process.returncode,"whole_process_seconds":time.monotonic()-start,
        "scope":("bounded post-clipping production extension replay; not full read mapping" if capture else
                 "isolated untrimmed ACGT-piece extension search; not full read mapping"), "peak_rss_status":"UNMEASURED"}
    if process.returncode==0:
        result["search"]=json.loads(process.stdout)
        unchanged=sha256(binary)==signatures[str(binary)] and all(
            (p.stat().st_size,p.stat().st_mtime_ns)==stamps[str(p)] for p in inputs)
        result["status"]="PASS_SCOPED_FIELDS" if unchanged and result["search"]["status"]=="PASS" else "FAILED_VALIDATION"
    (args.output/"result.json").write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2))
    if result["status"]!="PASS_SCOPED_FIELDS":raise SystemExit(1)

if __name__=="__main__":main()
