"""Paired native full-pipeline measurements using frozen executables and inputs."""
import argparse
import atexit
import ctypes
from ctypes import wintypes
import json
from pathlib import Path
import subprocess
import sys
import time
import statistics

root = Path(__file__).resolve().parents[1]
sys.path.insert(0,str(root / 'scripts'))
from benchmark_cpu_subset import sha256
from test_cpu_upstream import bam_records, bam_scientific_header, scientific_final_fields

class Memory(ctypes.Structure):
    _fields_ = [('cb',wintypes.DWORD),('PageFaultCount',wintypes.DWORD),
                *[(n,ctypes.c_size_t) for n in ('PeakWorkingSetSize','WorkingSetSize',
                  'QuotaPeakPagedPoolUsage','QuotaPagedPoolUsage','QuotaPeakNonPagedPoolUsage',
                  'QuotaNonPagedPoolUsage','PagefileUsage','PeakPagefileUsage','PrivateUsage')]]

memory_api = ctypes.WinDLL('psapi',use_last_error=True).GetProcessMemoryInfo
memory_api.argtypes = [wintypes.HANDLE,ctypes.POINTER(Memory),wintypes.DWORD]
memory_api.restype = wintypes.BOOL
parser=argparse.ArgumentParser(description=__doc__)
for name in ('baseline','candidate','reference-run','output-dir'):
    parser.add_argument('--'+name,required=True,type=Path)
parser.add_argument('--rounds',type=int,default=3)
parser.add_argument('--verify-only',action='store_true')
parser.add_argument('--candidate-reference-run',type=Path,
                    help='Optional content-fingerprinted candidate command for effective-index reuse')
args=parser.parse_args()
if not __debug__:parser.error('scientific validation requires assertions; do not run with Python -O')
if sys.platform!='win32' or args.rounds<1: parser.error('Windows and positive rounds required')
verify_only=args.verify_only
folder=args.output_dir.resolve()
if not verify_only: folder.mkdir(parents=True,exist_ok=False)
binary=dict(baseline=args.baseline.resolve(),candidate=args.candidate.resolve())
original=args.reference_run.resolve()
saved_command=json.loads((original/'command.json').read_text())
if not isinstance(saved_command,list) or saved_command[-2]!='--outFileNamePrefix':
    raise ValueError('reference command must end with --outFileNamePrefix path')
common = saved_command[1:-2]
inputs = json.loads((original/'input_signatures.json').read_text())
candidate_common=common
if args.candidate_reference_run:
    candidate_reference=args.candidate_reference_run.resolve()
    candidate_command=json.loads((candidate_reference/'command.json').read_text())
    if candidate_command[-2]!='--outFileNamePrefix':raise ValueError('invalid candidate command')
    candidate_common=candidate_command[1:-2]
    for path,digest in json.loads((candidate_reference/'input_signatures.json').read_text()).items():
        if path in inputs and inputs[path]!=digest:raise ValueError('conflicting input fingerprint')
        inputs[path]=digest
if not verify_only:
    assert all(sha256(p)==digest for p,digest in inputs.items()), 'input fingerprint changed'
binary_hashes = {label:sha256(path) for label,path in binary.items()}
if not verify_only:
    (folder/'runner.py').write_bytes(Path(__file__).read_bytes())
    (folder/'input_signatures.json').write_text(json.dumps(inputs,indent=2))
receipt = dict(status='RUNNING', effective_arguments=common,
               candidate_effective_arguments=candidate_common,
               binary_hashes=binary_hashes,runs=[],comparisons=[],
               cache='OS cache not flushed; alternated order; no cold-cache claim',
               memory='Windows process peak working set and peak commit; sampled every 100 ms')
def save(): (folder/'result.json').write_text(json.dumps(receipt,indent=2))
def incomplete():
    if receipt['status']=='RUNNING':
        receipt['status']='FAILED_OR_INTERRUPTED';save()
atexit.register(incomplete)
if verify_only:
    receipt=json.loads((folder/'result.json').read_text())
    assert len(receipt['runs'])==2*args.rounds and all(r['exit_code']==0 for r in receipt['runs'])
    assert receipt['binary_hashes']==binary_hashes, 'verification binary changed'
    receipt['status']='RUNNING'
    receipt['comparisons']=[]
    receipt['verification_runner_sha256']=sha256(Path(__file__))
    (folder/'verification-runner.py').write_bytes(Path(__file__).read_bytes())
save()
for round_id in ([] if verify_only else range(args.rounds)):
    order = ('baseline','candidate') if round_id%2==0 else ('candidate','baseline')
    for label in order:
        out = folder/f'{label}-{round_id+1}'; out.mkdir()
        command=[str(binary[label]),*(common if label=='baseline' else candidate_common),'--outFileNamePrefix',str(out)+'/']
        (out/'command.json').write_text(json.dumps(command))
        start=time.perf_counter();peak_ws=peak_commit=0; samples=0
        with (out/'console.log').open('w') as log:
            proc=subprocess.Popen(command,stdout=log,stderr=subprocess.STDOUT,cwd=out)
            while True:
                info=Memory();info.cb=ctypes.sizeof(info)
                if memory_api(wintypes.HANDLE(int(proc._handle)),ctypes.byref(info),info.cb):
                    peak_ws=max(peak_ws,info.PeakWorkingSetSize)
                    peak_commit=max(peak_commit,info.PeakPagefileUsage);samples+=1
                if proc.poll() is not None: break
                time.sleep(.1)
        elapsed=time.perf_counter()-start
        item=dict(label=label,round=round_id+1,seconds=elapsed,exit_code=proc.returncode,
                  peak_working_set_bytes=peak_ws,peak_commit_bytes=peak_commit,memory_samples=samples)
        receipt['runs'].append(item);save();print(json.dumps(item),flush=True)
        if proc.returncode or not samples or sha256(binary[label])!=binary_hashes[label]:
            receipt['status']='FAILED_EXECUTION_OR_PROVENANCE';save();raise SystemExit(1)
        assert 'ALL DONE!' in (out/'Log.out').read_text()

# Compare after all timed runs; validation must not contend with measurements.
reference=folder/'baseline-1'
reference_header=bam_scientific_header(reference/'Aligned.out.bam')
reference_bam=bam_records(reference/'Aligned.out.bam')
reference_files={p.relative_to(reference):sha256(p) for p in (reference/'Solo.out').rglob('*') if p.is_file()}
assert reference_files and any('spliced.mtx' in str(p) for p in reference_files)
for item in receipt['runs']:
    out=folder/f"{item['label']}-{item['round']}"
    command=json.loads((out/'command.json').read_text())
    assert command==[str(binary[item['label']]),*(common if item['label']=='baseline' else candidate_common),
                     '--outFileNamePrefix',str(out)+'/'], 'recorded execution arguments differ'
    files={p.relative_to(out):sha256(p) for p in (out/'Solo.out').rglob('*') if p.is_file()}
    same_bam=bam_records(out/'Aligned.out.bam')==reference_bam
    same_sj=sorted((out/'SJ.out.tab').read_text().splitlines())==sorted((reference/'SJ.out.tab').read_text().splitlines())
    final=lambda p:scientific_final_fields(p/'Log.final.out')
    result=dict(run=out.name,bam_records=len(reference_bam),bam_records_equal=same_bam,
                bam_dictionary_and_scientific_headers_equal=bam_scientific_header(out/'Aligned.out.bam')==reference_header,
                solo_matrices_axes_and_feature_stats_byte_equal=files==reference_files,
                sj_equal=same_sj,scientific_final_fields_equal=final(out)==final(reference))
    receipt['comparisons'].append(result);save()
    assert all(result[k] for k in ('bam_records_equal','bam_dictionary_and_scientific_headers_equal','solo_matrices_axes_and_feature_stats_byte_equal','sj_equal','scientific_final_fields_equal')),result
baseline=[r['seconds'] for r in receipt['runs'] if r['label']=='baseline']
changed=[r['seconds'] for r in receipt['runs'] if r['label']=='candidate']
receipt['baseline_median_seconds']=statistics.median(baseline)
receipt['candidate_median_seconds']=statistics.median(changed)
receipt['median_reduction_fraction']=1-statistics.median(changed)/statistics.median(baseline)
assert all(sha256(p)==digest for p,digest in inputs.items()), 'input fingerprint changed after execution'
receipt['status']='PASS_SCIENTIFIC_COMPARISON';save();print(json.dumps(receipt,indent=2))
