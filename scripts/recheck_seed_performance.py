"""Repeat frozen P1/P2 screens with Windows background-CPU telemetry.

Hash shared inputs before/after the entire serial experiment, never during timing.
This is warm replay screening, not full-pipeline or isolated-core qualification.
"""
import argparse
import ctypes
from ctypes import wintypes
import json
import os
from pathlib import Path
import statistics
import subprocess
import sys
import time

from benchmark_cpu_subset import sha256
from run_seed_experiment import verify_build_receipt


def median_ratio(runs):
    return statistics.median(r['candidate_seconds']/r['cpu_seconds'] for r in runs)


def quiet_enough(cpu_samples,available_bytes):
    # 25.3 GiB index plus headroom; operational gates, not biological thresholds.
    return available_bytes>=40*1024**3 and statistics.median(cpu_samples)<=10


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--evidence',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--rounds',type=int,default=3)
    p.add_argument('--repeats',type=int,default=501)
    args=p.parse_args()
    if sys.platform!='win32' or min(args.rounds,args.repeats)<1:
        p.error('Windows and positive repetitions required')
    root=args.evidence.resolve();out=args.output.resolve();out.mkdir(parents=True,exist_ok=False)
    signatures={};commands={}
    for label in ('p1-replay-1-cpu-base','p1-replay-1-word','p2-discovery','p2-heldout'):
        commands[label]=json.loads((root/label/'command.json').read_text())
        for name,digest in json.loads((root/label/'input_signatures.json').read_text()).items():
            if name in signatures and signatures[name]!=digest:raise ValueError('conflicting frozen input')
            signatures[name]=digest
    _,producer=verify_build_receipt(root/'p2-source.json',Path(commands['p2-discovery'][0]))
    receipt=dict(status='RUNNING',runs=[],cpu_count=os.cpu_count(),repeats=args.repeats,
        p2_producer=producer,p1_producer='UNVERIFIED_SOURCE; original frozen binary hashes checked',
        scope='warm four-field replay only; no CPU affinity/frequency isolation or production speedup claim')
    def save():(out/'result.json').write_text(json.dumps(receipt,indent=2))
    save();(out/'input_signatures.json').write_text(json.dumps(signatures,indent=2))
    (out/'runner_snapshot.py').write_bytes(Path(__file__).read_bytes())
    def verify():
        for name,digest in signatures.items():
            if sha256(name)!=digest:raise ValueError('frozen input or binary changed: '+name)
    api=ctypes.WinDLL('kernel32',use_last_error=True)
    api.GetSystemTimes.argtypes=[ctypes.POINTER(wintypes.FILETIME)]*3
    api.GetProcessTimes.argtypes=[wintypes.HANDLE]+[ctypes.POINTER(wintypes.FILETIME)]*4
    class MemoryStatus(ctypes.Structure):
        _fields_=[('length',wintypes.DWORD),('load',wintypes.DWORD)]+[
            (name,ctypes.c_ulonglong) for name in
            ('total_phys','available_phys','total_page','available_page','total_virtual','available_virtual','extended')]
    api.GlobalMemoryStatusEx.argtypes=[ctypes.POINTER(MemoryStatus)]
    def available_memory():
        info=MemoryStatus();info.length=ctypes.sizeof(info)
        if not api.GlobalMemoryStatusEx(ctypes.byref(info)):raise ctypes.WinError(ctypes.get_last_error())
        return info.available_phys
    def ticks(item):return (item.dwHighDateTime<<32)|item.dwLowDateTime
    def system_times():
        idle,kernel,user=(wintypes.FILETIME() for _ in range(3))
        if not api.GetSystemTimes(ctypes.byref(idle),ctypes.byref(kernel),ctypes.byref(user)):
            raise ctypes.WinError(ctypes.get_last_error())
        return ticks(idle),ticks(kernel)+ticks(user)
    def times(process):
        idle,total=system_times()
        creation,exit_time,pk,pu=(wintypes.FILETIME() for _ in range(4))
        if not api.GetProcessTimes(int(process._handle),*(ctypes.byref(x) for x in (creation,exit_time,pk,pu))):
            raise ctypes.WinError(ctypes.get_last_error())
        return idle,total,ticks(pk)+ticks(pu)
    def preflight():
        # Let brief post-fingerprinting activity settle without weakening the gate.
        for attempt in range(3):
            samples=[];previous=system_times()
            for _ in range(10):
                time.sleep(.5);current=system_times()
                idle,total=(b-a for a,b in zip(previous,current));previous=current
                if total>0:samples.append(100*(total-idle)/total)
            memory=available_memory()
            receipt['preflight']=dict(cpu_percent=samples,available_physical_bytes=memory,
                gate='median total CPU <=10%, available physical RAM >=40GiB')
            receipt.setdefault('preflight_history',[]).append(receipt['preflight'])
            save()
            if samples and quiet_enough(samples,memory):return
        raise ValueError('machine not ready for low-interference replay; see preflight receipt')
    def execute(label,round_index):
        command=commands[label].copy();command[5]=str(args.repeats)
        name=f'{label}-r{round_index}';start=time.perf_counter();samples=[]
        (out/(name+'.command.json')).write_text(json.dumps(command,indent=2))
        with (out/(name+'.stdout.json')).open('w') as stdout,(out/(name+'.stderr.log')).open('w') as stderr:
            process=subprocess.Popen(command,stdout=stdout,stderr=stderr)
            previous=times(process)
            while process.poll() is None:
                time.sleep(.2);current=times(process)
                idle,total,own=(b-a for a,b in zip(previous,current));previous=current
                if total>0:
                    samples.append(dict(seconds=time.perf_counter()-start,
                        total_cpu_percent=100*(total-idle)/total,
                        background_cpu_percent=100*max(0,total-idle-own)/total))
            code=process.wait()
        (out/(name+'.load.json')).write_text(json.dumps(samples,indent=2))
        data=json.loads((out/(name+'.stdout.json')).read_text())
        if code or data.get('status')!='PASS' or data.get('replay_limit_omitted_records')!=0:
            raise ValueError('replay failed or omitted queries: '+name)
        summary=dict(label=label,round=round_index,queries=data['queries'],seconds=time.perf_counter()-start,
            background_cpu_median_percent=statistics.median(s['background_cpu_percent'] for s in samples) if samples else None,
            background_cpu_max_percent=max((s['background_cpu_percent'] for s in samples),default=None),
            telemetry_scope='whole-process 200ms samples; cannot certify every short kernel interval or core isolation',
            memory=data['memory'])
        if 'experiments' in data:
            summary['models']=[dict(model=e['model'],k=e['k'],error=e['error'],
                median_candidate_cpu_ratio=median_ratio(e['runs']),hint_fraction=e['hint_usable_fraction'],
                build_seconds=e['build_seconds']) for e in data['experiments']]
        else:summary['median_cpu_seconds']=statistics.median(r['cpu_seconds'] for r in data['runs'])
        receipt['runs'].append(summary);save();print(name+' PASS',flush=True)
    try:
        preflight();print('Checking frozen inputs before timing',flush=True);verify();preflight()
        for round_index in range(1,args.rounds+1):
            pair=['p1-replay-1-cpu-base','p1-replay-1-word']
            models=['p2-discovery','p2-heldout']
            if round_index%2==0:pair.reverse();models.reverse()
            for label in pair+models:execute(label,round_index)
        print('Checking frozen inputs after timing',flush=True);verify()
        receipt['status']='PASS_SCOPED_FIELDS_WITH_LOAD_RECORD';save()
    except BaseException:
        receipt['status']='FAILED_OR_INTERRUPTED';save();raise


if __name__=='__main__':main()
