#!/usr/bin/env python3
"""Real STAR junction-remap backend and fail-closed integration; requires CUDA device."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import random
import subprocess
import tempfile
from test_cpu_upstream import bam_records


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cpu", required=True)
    parser.add_argument("--cuda", required=True)
    args=parser.parse_args()
    root=Path(tempfile.mkdtemp(prefix="star-gpu-sjdb-"))
    print(f"Evidence: {root}",flush=True)
    def run(label,binary,options,success=True,env=None):
        out=root/label;out.mkdir()
        command=[binary,"--runThreadN","2",*options,"--outFileNamePrefix",str(out)+"/"]
        (out/"command.json").write_text(json.dumps(command))
        process=subprocess.run(command,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,env=env)
        (out/"console.log").write_text(process.stdout)
        assert (process.returncode==0)==success,(label,process.stdout[-2500:])
        if not success:
            assert not (out/"Log.final.out").exists(),label
            assert "ALL DONE!" not in (out/"Log.out").read_text(),label
        return out
    rng=random.Random(892)
    sequence="".join(rng.choices("ACGT",k=20000))
    fasta=root/"reference.fa";fasta.write_text(">chr1\n"+sequence+"\n")
    gtf=root/"genes.gtf"
    exon=lambda start,end,gene: f'chr1\ttest\texon\t{start}\t{end}\t.\t+\t.\tgene_id "{gene}"; transcript_id "{gene}";\n'
    gtf.write_text(exon(101,400,"g1")+exon(701,1000,"g1"))
    novel=root/"novel.gtf";novel.write_text(gtf.read_text()+exon(1101,1200,"g2")+exon(1401,1500,"g2"))
    genome=root/"genome";genome.mkdir()
    run("index",args.cpu,["--runMode","genomeGenerate","--genomeDir",str(genome),
        "--genomeFastaFiles",str(fasta),"--sjdbGTFfile",str(gtf),"--sjdbOverhang","49",
        "--genomeSAindexNbases","4","--genomeChrBinNbits","10"])
    reads=root/"reads.fastq"
    fragments=[sequence[120:220],sequence[350:400]+sequence[700:750],sequence[1110:1210]]
    reads.write_text("".join(f"@r{i}\n{s}\n+\n{'I'*len(s)}\n" for i,s in enumerate(fragments)))
    common=["--genomeDir",str(genome),"--readFilesIn",str(reads),"--outSAMtype","BAM","Unsorted"]
    baseline=run("cpu",args.cpu,common+["--sjdbGTFfile",str(gtf)])
    gpu=run("cuda",args.cuda,common+["--sjdbGTFfile",str(gtf),"--gpuSjdbRemap","required"])
    assert "GPU_SJDB_REMAP status=0 " in (gpu/"Log.out").read_text()
    assert bam_records(baseline/"Aligned.out.bam")==bam_records(gpu/"Aligned.out.bam")
    assert (baseline/"SJ.out.tab").read_bytes()==(gpu/"SJ.out.tab").read_bytes()
    run("no-device",args.cuda,common+["--sjdbGTFfile",str(gtf),"--gpuSjdbRemap","required"],False,
        {**os.environ,"CUDA_VISIBLE_DEVICES":"-1"})
    fallback=run("no-device-auto",args.cuda,common+["--sjdbGTFfile",str(gtf),"--gpuSjdbRemap","auto"],
        env={**os.environ,"CUDA_VISIBLE_DEVICES":"-1"})
    assert "GPU_SJDB_REMAP status=1 " in (fallback/"Log.out").read_text()
    assert bam_records(baseline/"Aligned.out.bam")==bam_records(fallback/"Aligned.out.bam")
    run("cpu-required",args.cpu,common+["--sjdbGTFfile",str(gtf),"--gpuSjdbRemap","required"],False)
    run("new-junction-required",args.cuda,common+["--sjdbGTFfile",str(novel),"--gpuSjdbRemap","required"],False)
    cpu_novel=run("new-junction-cpu",args.cpu,common+["--sjdbGTFfile",str(novel)])
    auto_novel=run("new-junction-auto",args.cuda,common+["--sjdbGTFfile",str(novel),"--gpuSjdbRemap","auto"])
    assert "new suffixes or changed index geometry unsupported" in (auto_novel/"Log.out").read_text()
    assert bam_records(cpu_novel/"Aligned.out.bam")==bam_records(auto_novel/"Aligned.out.bam")
    result={"status":"PASS", "checks":["required CUDA executed and BAM/SJ match CPU",
        "missing device required rejects / auto falls back without scientific changes",
        "CPU-only required rejects", "new junction required rejects / auto matches CPU"],
        "cpu_sha256":hashlib.sha256(Path(args.cpu).read_bytes()).hexdigest(),
        "cuda_sha256":hashlib.sha256(Path(args.cuda).read_bytes()).hexdigest()}
    (root/"result.json").write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2))


if __name__=="__main__":
    main()
