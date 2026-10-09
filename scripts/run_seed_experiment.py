#!/usr/bin/env python3
"""Content-bound isolated seed-search benchmark; not a full STAR acceleration claim."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import time
import zipfile
from benchmark_cpu_subset import sha256


def verify_build_receipt(path,binary):
    """Validate local source snapshot contents and their recorded binary binding."""
    path=path.resolve();receipt=json.loads(path.read_text());archive=path.with_suffix('.source.zip')
    bindings=[digest for name,digest in receipt.get('binaries',{}).items() if Path(name).resolve()==binary]
    if bindings!=[sha256(binary)]:raise ValueError('build receipt binary binding mismatch')
    sources=receipt.get('source_files')
    if not isinstance(sources,dict) or not sources:raise ValueError('missing build receipt source inventory')
    content_signature=hashlib.sha256(json.dumps(sources,sort_keys=True).encode()).hexdigest()
    if content_signature!=receipt.get('source_tree_sha256'):raise ValueError('build receipt source content signature mismatch')
    if not archive.is_file() or sha256(archive)!=receipt.get('source_archive_sha256'):
        raise ValueError('build receipt source archive hash mismatch')
    with zipfile.ZipFile(archive) as saved:
        names=saved.namelist();expected={name for name,digest in sources.items() if digest is not None}
        if len(names)!=len(set(names)) or set(names)!=expected:raise ValueError('build receipt archive inventory mismatch')
        for name in names:
            digest=hashlib.sha256()
            with saved.open(name) as member:
                for block in iter(lambda:member.read(1024*1024),b''):digest.update(block)
            if digest.hexdigest()!=sources[name]:raise ValueError('build receipt archived source content mismatch')
    return [path,archive],dict(status='VERIFIED_RECEIPT_CONTENT_AND_BINARY_BINDING',
        source_tree_sha256=content_signature,source_archive_sha256=receipt['source_archive_sha256'],
        limitation='local source/binary receipt; does not independently prove reproducible compilation')


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ("benchmark","genome","fastq","output"):
        parser.add_argument("--"+name,required=True,type=Path)
    parser.add_argument("--reads",type=int,default=100000)
    parser.add_argument("--batch",type=int,default=65536)
    parser.add_argument("--repeats",type=int,default=3)
    parser.add_argument("--cpu-threads",type=int,default=8)
    parser.add_argument("--executor",choices=("cpu","cuda","pwl","pla","hint-sweep"),default="cpu",
                        help="CPU replay is independent of CUDA; cuda requires a CUDA-enabled benchmark")
    parser.add_argument('--hint-k',type=int,choices=(14,18,21),default=18)
    parser.add_argument('--hint-error',type=int,choices=(16,64,256),default=64,
                        help='sampled PLA slope-envelope target; unused by fixed-bin PWL')
    parser.add_argument('--build-receipt',type=Path,help='Optional local capture_qualification.py receipt with its adjacent .source.zip')
    args=parser.parse_args()
    if min(args.reads,args.batch,args.repeats,args.cpu_threads)<1:parser.error("positive run parameters required")
    binary=args.benchmark.resolve();genome=args.genome.resolve();fastq=args.fastq.resolve()
    capture=fastq.is_dir()
    query_files=sorted(fastq.glob('queries-*.bin')) if capture else [fastq]
    capture_files=sorted(p for p in fastq.iterdir() if p.is_file()) if capture else []
    inputs=[binary,*(capture_files if capture else query_files),*(genome/name for name in (("Genome","SA","genomeParameters.txt") if capture else ("Genome","SA","SAindex","genomeParameters.txt")))]
    inputs=list(dict.fromkeys(inputs))
    if capture and not query_files:parser.error("empty capture directory")
    if not all(p.is_file() for p in inputs):parser.error("missing binary/input")
    producer_source=dict(status='UNVERIFIED',limitation='current source copies are not attested build provenance')
    receipt_files=[]
    if args.build_receipt:
        receipt_files,producer_source=verify_build_receipt(args.build_receipt,binary)
        inputs.extend(receipt_files)
    capture_contract={"abi_status":"NOT_APPLICABLE"}
    if capture:
        capture_contract={"abi_status":"UNVERIFIED_LEGACY_NATIVE_ABI"}
        metadata=fastq/"capture_metadata.json"
        if metadata.exists():
            capture_contract=json.loads(metadata.read_text())
            expected={"format":1,"native_uint_bytes":8,"query_bytes":64,"match_bytes":32,"byte_order":sys.byteorder}
            if any(capture_contract.get(key)!=value for key,value in expected.items()):
                raise ValueError("capture native ABI mismatch")
            capture_contract["abi_status"]="VERIFIED_NATIVE_ABI"
    args.output.mkdir(parents=True,exist_ok=False)
    stamps={str(p):(p.stat().st_size,p.stat().st_mtime_ns) for p in inputs}
    signatures={str(p):sha256(p) for p in inputs}
    if any((p.stat().st_size,p.stat().st_mtime_ns)!=stamps[str(p)] for p in inputs):
        raise ValueError("input changed during fingerprinting")
    if capture:
        index_dumps=[fastq/name for name in ("Genome","SA","genomeParameters.txt")]
        if any(p.exists() for p in index_dumps):
            if not all(p.is_file() for p in index_dumps):
                raise ValueError("incomplete captured effective index")
            if any(signatures[str(p)]!=signatures[str(genome/p.name)] for p in index_dumps):
                raise ValueError("replay index differs from captured effective index")
            capture_contract["index_binding_status"]="VERIFIED_CAPTURED_EFFECTIVE_INDEX"
        else:
            capture_contract["index_binding_status"]="UNVERIFIED_LEGACY_EXTERNAL_INDEX"
    command=[str(binary),str(genome),str(fastq),str(args.reads),str(args.batch),str(args.repeats),str(args.cpu_threads),args.executor]
    if args.executor in ('pwl','pla','hint-sweep'):command.extend((str(args.hint_k),str(args.hint_error)))
    (args.output/"command.json").write_text(json.dumps(command,indent=2))
    (args.output/"input_signatures.json").write_text(json.dumps(signatures,indent=2))
    (args.output/"runner_snapshot.py").write_bytes(Path(__file__).read_bytes())
    source=Path(__file__).resolve().parents[1]
    for name in ("source/gpuSeedSearch.cu","source/gpuSeedSearch.h","source/SuffixArrayFuns.cpp","source/SuffixArrayFuns.h","test/benchmark_seed_search.cpp","test/SeedRankHint.h"):
        (args.output/Path(name).name).write_bytes((source/name).read_bytes())
    (args.output/'source_snapshot_status.json').write_text(json.dumps(dict(status='CURRENT_SOURCE_ONLY_NOT_ATTESTED_BUILD_PROVENANCE'),indent=2))
    if receipt_files:
        shutil.copy2(receipt_files[0],args.output/'build_receipt.json')
        shutil.copy2(receipt_files[1],args.output/'build_receipt.source.zip')
    start=time.monotonic();process=subprocess.run(command,capture_output=True,text=True)
    (args.output/"console.log").write_text(process.stdout+process.stderr)
    result={"status":"FAILED_EXECUTION","exit_code":process.returncode,"whole_process_seconds":time.monotonic()-start,
        "scope":("bounded post-clipping production extension replay; length/lower/upper/multiplicity only" if capture else
                 "isolated untrimmed ACGT-piece extension search; length/lower/upper/multiplicity only"),
        "emitted_seed_verification":"NOT_PERFORMED; requires full caller-chain integration",
        "executor":args.executor,"capture_contract":capture_contract,"peak_rss_status":"UNVERIFIED",
        "producer_source_status":producer_source['status'],"producer_source":producer_source}
    post_signatures={str(p):sha256(p) if p.is_file() else None for p in inputs}
    (args.output/"post_input_signatures.json").write_text(json.dumps(post_signatures,indent=2))
    inventory_unchanged=not capture or capture_files==sorted(p for p in fastq.iterdir() if p.is_file())
    unchanged=inventory_unchanged and post_signatures==signatures and all(
        p.is_file() and (p.stat().st_size,p.stat().st_mtime_ns)==stamps[str(p)] for p in inputs)
    result["provenance_status"]="VERIFIED_UNCHANGED" if unchanged else "FAILED_CHANGED_INPUT_OR_PRODUCER"
    if process.returncode==0:
        try:
            result["search"]=json.loads(process.stdout)
            result["peak_rss_status"]=result["search"].get("memory",{}).get("status","UNVERIFIED")
            result["status"]="PASS_SCOPED_FIELDS" if unchanged and result["search"].get("status")=="PASS" else "FAILED_VALIDATION"
        except (ValueError,AttributeError):
            result["status"]="FAILED_INVALID_OUTPUT"
    (args.output/"result.json").write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2))
    if result["status"]!="PASS_SCOPED_FIELDS":raise SystemExit(1)

if __name__=="__main__":main()
