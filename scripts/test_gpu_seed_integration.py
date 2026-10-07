#!/usr/bin/env python3
"""Generate a real STAR index and validate the isolated resident seed-search experiment."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import random
import subprocess
import struct
import tempfile

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--star",required=True)
    parser.add_argument("--benchmark",required=True)
    parser.add_argument("--capture-star",help="Optional diagnostic build for actual post-clipping capture/replay")
    args=parser.parse_args()
    root=Path(tempfile.mkdtemp(prefix="star-seed-fixture-"));print(f"Evidence: {root}",flush=True)
    rng=random.Random(816)
    genome="".join(rng.choices("ACGT",k=20000))
    genome=genome[:1000]+"ACGT"*100+"N"*8+genome[1408:]
    fasta=root/"reference.fa";fasta.write_text(">chr1\n"+genome+"\n")
    gtf=root/"reference.gtf"
    gtf.write_text('chr1\ttest\texon\t101\t400\t.\t+\t.\tgene_id "g"; transcript_id "t";\n'
                   'chr1\ttest\texon\t701\t1000\t.\t+\t.\tgene_id "g"; transcript_id "t";\n')
    index=root/"index";index.mkdir()
    command=[str(Path(args.star).resolve()),"--runMode","genomeGenerate","--runThreadN","2",
        "--genomeDir",str(index),"--genomeFastaFiles",str(fasta),"--sjdbGTFfile",str(gtf),
        "--sjdbOverhang","49","--genomeSAindexNbases","4","--genomeChrBinNbits","10",
        "--outFileNamePrefix",str(root)+os.sep]
    generated=subprocess.run(command,capture_output=True,text=True)
    (root/"index-command.json").write_text(json.dumps(command));(root/"index-console.log").write_text(generated.stdout+generated.stderr)
    assert generated.returncode==0,generated.stderr
    fragments=[genome[p:p+80] for p in rng.sample(range(100,19800),1000)]
    fragments += [genome[350:400]+genome[700:750],genome[1000:1080],"N"+genome[200:280]+"N",
                  genome[1360:1440],genome[300:310],"T"*70]
    fragments += [s.translate(str.maketrans("ACGT","TGCA"))[::-1] for s in fragments[:50]]
    fastq=root/"reads.fastq"
    fastq.write_text("".join(f"@r{i}\n{s}\n+\n{'I'*len(s)}\n" for i,s in enumerate(fragments)))
    binary=str(Path(args.benchmark).resolve());runs=[]
    for batch in (1,7,256,65536):
        command=[binary,str(index),str(fastq),str(len(fragments)),str(batch),"3","2"]
        process=subprocess.run(command,capture_output=True,text=True)
        (root/f"batch-{batch}.log").write_text(process.stdout+process.stderr)
        assert process.returncode==0,process.stderr
        receipt=json.loads(process.stdout);assert receipt["status"]=="PASS" and receipt["queries"]>0
        runs.append(receipt)
    for label,batch,env in [("zero-batch",0,None),("oversized-batch",1048577,None),
                            ("no-device",256,{**os.environ,"CUDA_VISIBLE_DEVICES":"-1"})]:
        process=subprocess.run([binary,str(index),str(fastq),"10",str(batch),"1"],capture_output=True,text=True,env=env)
        (root/f"{label}.log").write_text(process.stdout+process.stderr)
        assert process.returncode!=0 and '"status":"PASS"' not in process.stdout,label
    result={"status":"PASS","runs":runs,"negative_checks":["zero batch","over-capacity batch","no CUDA device"],
        "star_sha256":hashlib.sha256(Path(args.star).read_bytes()).hexdigest(),
        "benchmark_sha256":hashlib.sha256(Path(args.benchmark).read_bytes()).hexdigest()}
    for label,payload in (
            ('unknown-capture-format',b'WRONG001'),
            ('truncated-capture',b'STARSD01'+bytes(3)),
            ('invalid-capture-bounds',b'STARSD01'+struct.pack('<Q8Q4Q',0,0,4,0,4,2,1,0,1,0,0,0,0)+bytes(4))):
        capture=root/label;capture.mkdir();(capture/'queries-0.bin').write_bytes(payload)
        process=subprocess.run([binary,str(index),str(capture),'10','7','1'],capture_output=True,text=True)
        (root/f'{label}.log').write_text(process.stdout+process.stderr)
        assert process.returncode!=0 and '"status":"PASS"' not in process.stdout,label
        result['negative_checks'].append(label)
    if args.capture_star:
        capture=root/'production-capture';capture.mkdir()
        outputs=[]
        for label,star,env in (('normal',args.star,os.environ),('capture',args.capture_star,{**os.environ,'STAR_SEED_TRACE_DIR':str(capture)})):
            out=root/label;out.mkdir()
            command=[str(Path(star).resolve()),'--genomeDir',str(index),'--readFilesIn',str(fastq),
                     '--runThreadN','2','--outSAMtype','BAM','Unsorted','--outSAMunmapped','Within',
                     '--outFileNamePrefix',str(out)+os.sep]
            process=subprocess.run(command,capture_output=True,text=True,env=env)
            (out/'console.log').write_text(process.stdout+process.stderr)
            assert process.returncode==0,process.stderr
            outputs.append(out)
        from test_cpu_upstream import bam_records,bam_scientific_header
        assert bam_records(outputs[0]/'Aligned.out.bam')==bam_records(outputs[1]/'Aligned.out.bam')
        assert bam_scientific_header(outputs[0]/'Aligned.out.bam')==bam_scientific_header(outputs[1]/'Aligned.out.bam')
        assert (outputs[0]/'SJ.out.tab').read_bytes()==(outputs[1]/'SJ.out.tab').read_bytes()
        for batch in (1,7,256):
            process=subprocess.run([binary,str(capture),str(capture),'40000',str(batch),'3','2'],capture_output=True,text=True)
            (root/f'capture-batch-{batch}.log').write_text(process.stdout+process.stderr)
            assert process.returncode==0,process.stderr
            result.setdefault('production_capture_runs',[]).append(json.loads(process.stdout))
        result['capture_instrumentation_preserves_bam_and_sj']=True
        result['capture_star_sha256']=hashlib.sha256(Path(args.capture_star).read_bytes()).hexdigest()
    (root/"result.json").write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2))

if __name__=="__main__":main()
