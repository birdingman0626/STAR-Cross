#!/usr/bin/env python3
"""Execute C++17 ownership paths for super-transcriptomes and optional STARlong.

Synthetic, deterministic fixtures only. Receipts and failed commands are retained.
This is lifecycle/equality evidence, not biological or long-read accuracy validation.
"""
import argparse
import hashlib
import json
from pathlib import Path
import random
import subprocess
import tempfile

from test_cpu_upstream import bam_records, bam_scientific_header, scientific_final_fields


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--star-exe", required=True)
    parser.add_argument("--ref-exe")
    parser.add_argument("--long-exe")
    parser.add_argument("--ref-long-exe")
    args = parser.parse_args()
    if args.ref_long_exe and not args.long_exe:
        parser.error("--ref-long-exe requires --long-exe")
    root = Path(tempfile.mkdtemp(prefix="star-experimental-genomes-"))
    print(f"Evidence retained at: {root}", flush=True)
    checks = []

    def run(binary, label, options, valid=True):
        out = root / label
        out.mkdir()
        command = [binary, "--runThreadN", "1", *options,
                   "--outFileNamePrefix", str(out) + "/"]
        (out / "command.json").write_text(json.dumps(command))
        process = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        (out / "console.log").write_text(process.stdout)
        assert (process.returncode == 0) == valid, (label, process.returncode, process.stdout[-4000:])
        return out

    def graph_scores(out):
        return sorted(line.split("\t") for line in (out / "console.log").read_text().splitlines()
                      if line.startswith("@short"))

    def compare(candidate, reference):
        assert sorted(bam_records(candidate / "Aligned.out.bam")) == sorted(bam_records(reference / "Aligned.out.bam"))
        assert bam_scientific_header(candidate / "Aligned.out.bam") == bam_scientific_header(reference / "Aligned.out.bam")
        assert scientific_final_fields(candidate / "Log.final.out") == scientific_final_fields(reference / "Log.final.out")
        assert (candidate / "SJ.out.tab").read_bytes() == (reference / "SJ.out.tab").read_bytes()

    rng = random.Random(20261007)
    sequence = "".join(rng.choices("ACGT", k=20000))
    fasta = root / "reference.fa"
    fasta.write_text(">chr1\n" + sequence + "\n")
    gtf = root / "genes.gtf"
    gtf.write_text('chr1\ttest\texon\t101\t900\t.\t+\t.\tgene_id "g1"; transcript_id "t1";\n'
                   'chr1\ttest\texon\t1501\t2300\t.\t+\t.\tgene_id "g1"; transcript_id "t1";\n'
                   'chr1\ttest\texon\t5001\t6500\t.\t+\t.\tgene_id "g2"; transcript_id "t2";\n')
    short = root / "short.fastq"
    fragments = [sequence[150:250], sequence[850:900] + sequence[1500:1550], sequence[5050:5150]]
    short.write_text("".join(f"@short{i}\n{s}\n+\n{'I'*len(s)}\n" for i, s in enumerate(fragments)))
    index_options = ["--runMode", "genomeGenerate", "--genomeFastaFiles", str(fasta),
                     "--sjdbGTFfile", str(gtf), "--sjdbOverhang", "49",
                     "--genomeSAindexNbases", "4", "--genomeChrBinNbits", "10"]
    for kind in ("Full", "SuperTranscriptome"):
        genome = root / kind
        genome.mkdir()
        options = index_options + ["--genomeDir", str(genome), "--genomeType", kind]
        run(args.star_exe, kind + "-index", options)
        output = ["BAM", "Unsorted"] if kind == "Full" else ["None"]
        mapping = ["--genomeDir", str(genome), "--readFilesIn", str(short), "--outSAMtype", *output]
        candidate = run(args.star_exe, kind + "-map", mapping)
        if kind == "Full":
            records = bam_records(candidate / "Aligned.out.bam")
            assert len(records) == len(fragments), (kind, len(records))
            assert scientific_final_fields(candidate / "Log.final.out")["Uniquely mapped reads number"] == "3"
            checks.append("Full actual generation, exonic/spliced mapping and teardown")
        else:
            # Upstream graph output/statistics are unfinished. Validate real DP
            # execution via its diagnostic scores, never certify an empty BAM.
            scores = graph_scores(candidate)
            for read in ("@short0", "@short1", "@short2"):
                assert max(int(row[7]) for row in scores if row[0] == read) == 100
            checks.append("SuperTranscriptome generation, actual graph scores and teardown (diagnostics only)")
            for format_ in ("BAM", "CRAM"):
                rejected = run(args.star_exe, kind + "-reject-" + format_,
                               mapping[:-1] + [format_, "Unsorted"], valid=False)
                assert "SuperTranscriptome mapping does not implement BAM/CRAM output" in (rejected / "console.log").read_text()
            checks.append("SuperTranscriptome rejects unsupported BAM/CRAM instead of silently emitting empty output")
        if args.ref_exe:
            ref_genome = root / (kind + "-reference")
            ref_genome.mkdir()
            run(args.ref_exe, kind + "-ref-index", index_options + ["--genomeDir", str(ref_genome), "--genomeType", kind])
            for name in ("Genome", "SA", "SAindex", "chrName.txt", "chrStart.txt", "chrLength.txt"):
                assert (genome / name).read_bytes() == (ref_genome / name).read_bytes(), (kind, name)
            reference = run(args.ref_exe, kind + "-ref-map", ["--genomeDir", str(ref_genome), *mapping[2:]])
            if kind == "Full":
                compare(candidate, reference)
                checks.append("Full index, BAM/header, junctions and scientific log equal to reference")
            else:
                assert graph_scores(candidate) == graph_scores(reference)
                checks.append("SuperTranscriptome index and graph diagnostic scores equal to reference; alignment output NOT qualified")

    if args.long_exe:
        reads = root / "long.fastq"
        fragments = [sequence[100:900] + sequence[1500:2300], sequence[5050:6250]]
        reads.write_text("".join(f"@long{i}\n{s}\n+\n{'I'*len(s)}\n" for i, s in enumerate(fragments)))
        options = ["--genomeDir", str(root / "Full"), "--readFilesIn", str(reads),
                   "--outSAMtype", "BAM", "Unsorted"]
        candidate = run(args.long_exe, "long-map", options)
        assert len(bam_records(candidate / "Aligned.out.bam")) == 2
        assert scientific_final_fields(candidate / "Log.final.out")["Uniquely mapped reads number"] == "2"
        checks.append("STARlong actual 1200/1600-base exonic/spliced mapping and teardown")
        if args.ref_long_exe:
            compare(candidate, run(args.ref_long_exe, "long-ref-map", options))
            checks.append("STARlong BAM/header, junctions and scientific log equal to reference")
    result = {"checks": checks, "binary_sha256": {
        name: hashlib.sha256(Path(binary).read_bytes()).hexdigest()
        for name, binary in vars(args).items() if binary}}
    (root / "result.json").write_text(json.dumps(result, indent=2))
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
