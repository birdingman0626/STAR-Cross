#!/usr/bin/env python3
"""Bounded pipeline qualification against identical synchronous arguments.

One run per mode is correctness screening, not a replicated speed claim.
The supplied command receipt must end in --outFileNamePrefix <directory>.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import time
from qualification import measured_run, bam_signature, scientific_final_fields


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--command',type=Path,required=True)
    parser.add_argument('--binary',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    command=json.loads(args.command.read_text())
    if command[-2]!='--outFileNamePrefix': parser.error('unsupported command receipt')
    args.output.mkdir(parents=True,exist_ok=False)
    binary=str(args.binary.resolve())
    binary_hash=hashlib.sha256(args.binary.read_bytes()).hexdigest()
    for key in ('STAR_CHUNK_PIPELINE','STAR_ASYNC_BAM','STAR_ASYNC_SAM'): os.environ.pop(key,None)
    runs=[];baseline=None
    for mode,settings in (('normal',{}),('input',{'STAR_CHUNK_PIPELINE':'1'}),
                          ('output',{'STAR_ASYNC_BAM':'1'}),
                          ('combined',{'STAR_CHUNK_PIPELINE':'1','STAR_ASYNC_BAM':'1'})):
        folder=(args.output/mode).resolve();folder.mkdir()
        argv=[binary,*command[1:-2],'--outFileNamePrefix',str(folder)+os.sep]
        for key in ('STAR_CHUNK_PIPELINE','STAR_ASYNC_BAM'): os.environ.pop(key,None)
        os.environ.update(settings)
        (folder/'command.json').write_text(json.dumps(argv))
        (folder/'pipeline_environment.json').write_text(json.dumps(settings))
        with (folder/'console.log').open('w') as console:
            start=time.perf_counter();code,memory=measured_run(argv,console,folder)
            elapsed=time.perf_counter()-start
        if code: raise RuntimeError(f'{mode} failed: {code}; inspect {folder}/console.log')
        # Compare scientific records, not thread-dependent BGZF block ordering.
        signatures={p.relative_to(folder).as_posix():bam_signature(p) for p in folder.rglob('*.bam')}
        # Reference names are bytes in the shared BAM parser; encode losslessly
        # for the receipt, identically for every profile.
        for signature in signatures.values():
            refs,lines=signature['header']
            signature['header']=([[name.hex(),length] for name,length in refs],lines)
        signatures['final_fields']=scientific_final_fields(folder/'Log.final.out')
        for p in folder.rglob('*'):
            if p.is_file() and (p.name=='SJ.out.tab' or p.suffix in ('.mtx','.tsv') or p.name=='Summary.csv'):
                signatures[p.relative_to(folder).as_posix()]=hashlib.sha256(p.read_bytes()).hexdigest()
        if baseline is None: baseline=signatures
        if signatures!=baseline: raise RuntimeError(f'{mode} scientific outputs differ')
        (folder/'scientific_signatures.json').write_text(json.dumps(signatures,indent=2))
        runs.append(dict(mode=mode,seconds=elapsed,environment=settings,**memory))
    if hashlib.sha256(args.binary.read_bytes()).hexdigest()!=binary_hash:
        raise RuntimeError('binary changed during qualification')
    receipt=dict(status='PASS',scope='same-binary pipeline correctness screening; no speed promotion',
                 binary_sha256=binary_hash,
                 source_command_sha256=hashlib.sha256(args.command.read_bytes()).hexdigest(),runs=runs)
    (args.output/'result.json').write_text(json.dumps(receipt,indent=2))
    print(json.dumps(receipt,indent=2))


if __name__=='__main__': main()
