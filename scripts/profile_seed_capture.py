"""Execute P0 from a frozen full-output command; diagnostic timing is not a speedup claim."""
import argparse
import json
import os
from pathlib import Path
import time
from benchmark_cpu_subset import sha256
from qualification import measured_run,bam_signature
from test_cpu_upstream import scientific_final_fields
from summarize_seed_capture import summarize


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('normal','diagnostic','reference-run','output'):
        p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--modulus',type=int,default=64)
    p.add_argument('--fixture',type=Path,help='Optional synchronized held-out FASTQ segment with COMPLETE manifest')
    args=p.parse_args()
    if args.modulus<1:p.error('positive sample modulus required')
    reference=args.reference_run.resolve()
    command=json.loads((reference/'command.json').read_text())
    if command[-2]!='--outFileNamePrefix':raise ValueError('invalid reference command')
    common=command[1:-2]
    if '--twopassMode' in common and common[common.index('--twopassMode')+1]!='None':
        raise ValueError('changing-index capture is unsupported')
    expected=json.loads((reference/'input_signatures.json').read_text())
    if args.fixture:
        fixture=args.fixture.resolve();manifest=json.loads((fixture/'manifest.json').read_text())
        if manifest.get('status')!='COMPLETE':raise ValueError('fixture is incomplete')
        i=common.index('--readFilesIn')+1
        old={Path(path).resolve() for path in common[i:i+2]}
        parents={path.parent for path in old}
        expected={path:digest for path,digest in expected.items() if Path(path).resolve() not in old
                  and not (Path(path).name=='manifest.json' and Path(path).resolve().parent in parents)}
        common[i:i+2]=[str(fixture/'R2.fastq'),str(fixture/'R1.fastq')]
        for item in manifest['outputs']:
            path=fixture/item['name'];expected[str(path)]=item['sha256']
        expected[str(fixture/'manifest.json')]=sha256(fixture/'manifest.json')
    if not expected or any(sha256(path)!=digest for path,digest in expected.items()):
        raise ValueError('reference inputs missing or stale')
    signatures={str(b.resolve()):sha256(b) for b in (args.normal,args.diagnostic)}
    output=args.output.resolve();output.mkdir(parents=True,exist_ok=False)
    capture=output/'capture';capture.mkdir()
    (output/'input_signatures.json').write_text(json.dumps(expected,indent=2))
    receipt=dict(status='RUNNING',runs=[],binaries=signatures,modulus=args.modulus,
                 timing_scope='instrumentation overhead screening only; includes first index dump and no cold-cache control')
    def save():(output/'result.json').write_text(json.dumps(receipt,indent=2))
    save()
    previous={key:os.environ.get(key) for key in ('STAR_SEED_TRACE_DIR','STAR_SEED_TRACE_MODULUS')}
    try:
        for label,binary in (('normal',args.normal),('diagnostic',args.diagnostic)):
            run=output/label;run.mkdir()
            if label=='diagnostic':
                os.environ['STAR_SEED_TRACE_DIR']=str(capture)
                os.environ['STAR_SEED_TRACE_MODULUS']=str(args.modulus)
            else:
                for key in previous:os.environ.pop(key,None)
            cmd=[str(binary.resolve()),*common,'--outFileNamePrefix',str(run)+os.sep]
            (run/'command.json').write_text(json.dumps(cmd,indent=2))
            start=time.perf_counter()
            with (run/'console.log').open('w') as console:
                code,memory=measured_run(cmd,console,run)
            receipt['runs'].append(dict(label=label,seconds=time.perf_counter()-start,exit_code=code,**memory));save()
            if code or 'ALL DONE!' not in (run/'Log.out').read_text():raise ValueError('producer failed')
        left,right=output/'normal',output/'diagnostic'
        def files(folder):return {str(p.relative_to(folder)):sha256(p) for p in (folder/'Solo.out').rglob('*') if p.is_file()}
        for feature in ('Gene','GeneFull_Ex50pAS','Velocyto'):
            names=('spliced.mtx','unspliced.mtx','ambiguous.mtx') if feature=='Velocyto' else ('matrix.mtx',)
            for name in (*names,'features.tsv','barcodes.tsv'):
                if not (left/'Solo.out'/feature/'raw'/name).is_file():raise ValueError('required scientific output missing')
        receipt['comparison']=dict(bam=bam_signature(left/'Aligned.out.bam')==bam_signature(right/'Aligned.out.bam'),
            sj=sorted((left/'SJ.out.tab').read_text().splitlines())==sorted((right/'SJ.out.tab').read_text().splitlines()),
            solo=files(left)==files(right),scientific_log=scientific_final_fields(left/'Log.final.out')==scientific_final_fields(right/'Log.final.out'))
        if not files(left) or not all(receipt['comparison'].values()):raise ValueError('scientific mismatch')
        receipt['diagnostics']=summarize(capture)
        if not receipt['diagnostics']['eligible_for_screening']:raise ValueError('capture sample overflow or incomplete position coverage')
        if any(sha256(path)!=digest for path,digest in {**expected,**signatures}.items()):raise ValueError('execution provenance changed')
        receipt['capture_signatures']={p.name:sha256(p) for p in capture.iterdir() if p.is_file()}
        receipt['status']='PASS_P0_SCOPED_DIAGNOSTICS';save()
        print(json.dumps(receipt,indent=2))
    except BaseException:
        receipt['status']='FAILED_OR_INTERRUPTED';save();raise
    finally:
        for key,value in previous.items():
            if value is None:os.environ.pop(key,None)
            else:os.environ[key]=value


if __name__=='__main__':main()
