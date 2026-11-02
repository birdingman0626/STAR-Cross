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
import struct
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
            # Seeds are --genomeSAindexNbases (4) bases long and must fit in the read; seeds read past
            # its end came from stale buffer bytes and made the coverage diagnostics vary run to run.
            for row in scores:
                assert float(row[5]) <= (int(row[4]) - 4 + 1) / int(row[4]) + 1e-6, row
            assert graph_scores(run(args.star_exe, kind + "-map-repeat", mapping)) == scores
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

    # (-)strand transcripts are read from the reverse-complement genome strand. Upstream read it
    # before it was filled: the sequences were spacer bytes, printed through an out-of-range lookup.
    def read_fasta(path):
        records, name = {}, None
        for line in path.read_text().splitlines():
            if line.startswith(">"):
                name = line[1:]
                records[name] = ""
            else:
                records[name] += line
        return records
    revcomp = lambda s: s[::-1].translate(str.maketrans("ACGT", "TGCA"))
    minus_gtf = root / "minus.gtf"
    minus_gtf.write_text('chr1\ttest\texon\t101\t900\t.\t+\t.\tgene_id "g1"; transcript_id "t1";\n'
                         'chr1\ttest\texon\t8001\t8500\t.\t-\t.\tgene_id "g3"; transcript_id "t3";\n'
                         'chr1\ttest\texon\t9001\t9300\t.\t-\t.\tgene_id "g3"; transcript_id "t3";\n')
    expected = {"t1": sequence[100:900], "t3": revcomp(sequence[8000:8500] + sequence[9000:9300])}
    code = {"A": 0, "C": 1, "G": 2, "T": 3}
    for kind in ("Transcriptome", "SuperTranscriptome"):
        genome = root / (kind + "-minus")
        genome.mkdir()
        run(args.star_exe, kind + "-minus-index",
            ["--runMode", "genomeGenerate", "--genomeFastaFiles", str(fasta), "--sjdbGTFfile", str(minus_gtf),
             "--sjdbOverhang", "49", "--genomeSAindexNbases", "4", "--genomeChrBinNbits", "10",
             "--genomeDir", str(genome), "--genomeType", kind])
        assert read_fasta(genome / "transcriptSequences.fasta") == expected, kind
        assert sorted(read_fasta(genome / "superTranscriptSequences.fasta").values()) == sorted(expected.values()), kind
        genome_bytes = (genome / "Genome").read_bytes()
        for name, seq in expected.items():
            assert bytes(code[b] for b in seq) in genome_bytes, (kind, name)
        checks.append(kind + " generation includes (-)strand transcript sequences in FASTA and genome")

    # GX/GN without Solo annotate genes with --soloStrand (it was left uninitialized, so genes
    # flipped run to run), and Aligned.toTranscriptome.out.bam rB blocks are in transcript
    # coordinates like POS (they were offset by genome chromosome starts indexed by transcript).
    def aux_tags(record):
        name_len, n_cigar, seq_len = record[8], struct.unpack("<H", record[12:14])[0], struct.unpack("<i", record[16:20])[0]
        name = record[32:32 + name_len - 1].decode()
        o = 32 + name_len + 4 * n_cigar + (seq_len + 1) // 2 + seq_len
        tags = {}
        while o < len(record):
            tag, kind = record[o:o + 2].decode(), chr(record[o + 2])
            o += 3
            if kind == "Z":
                end = record.index(b"\0", o)
                tags[tag], o = record[o:end].decode(), end + 1
            elif kind == "B":
                assert chr(record[o]) == "i", tag
                count = struct.unpack("<i", record[o + 1:o + 5])[0]
                tags[tag] = list(struct.unpack(f"<{count}i", record[o + 5:o + 5 + 4 * count]))
                o += 5 + 4 * count
            else:
                size = {"c": 1, "C": 1, "s": 2, "S": 2, "i": 4, "I": 4}[kind]
                tags[tag] = int.from_bytes(record[o:o + size], "little", signed=kind.islower())
                o += size
        return name, struct.unpack("<i", record[4:8])[0], tags

    annot = root / "annot.fastq"
    annot_reads = {"sense": sequence[5050:5150], "antisense": revcomp(sequence[5200:5300]),
                   "spliced": sequence[850:900] + sequence[1500:1550]}
    annot.write_text("".join(f"@{n}\n{s}\n+\n{'I'*len(s)}\n" for n, s in annot_reads.items()))
    expected_genes = {"Forward": {"sense": "g2", "antisense": "-", "spliced": "g1"},
                      "Reverse": {"sense": "-", "antisense": "g2", "spliced": "-"},
                      "Unstranded": {"sense": "g2", "antisense": "g2", "spliced": "g1"}}
    for strand, genes in expected_genes.items():
        options = ["--genomeDir", str(root / "Full"), "--readFilesIn", str(annot), "--outSAMtype", "BAM", "Unsorted",
                   "--outSAMattributes", "NH", "HI", "GX", "GN", "rB", "--soloStrand", strand,
                   "--quantMode", "TranscriptomeSAM"]
        out = run(args.star_exe, "gxgn-" + strand, options)
        records = [aux_tags(r) for r in bam_records(out / "Aligned.out.bam")]
        assert {n: (t["GX"], t["GN"]) for n, _, t in records} == {n: (g, g) for n, g in genes.items()}, (strand, records)
        assert bam_records(run(args.star_exe, "gxgn-" + strand + "-repeat", options) / "Aligned.out.bam") == \
            bam_records(out / "Aligned.out.bam"), strand
        for n, pos, t in records:  # one chromosome starting at 0: genomic rB blocks match POS
            assert t["rB"][2] == pos + 1, (n, pos, t["rB"])
        transcriptomic = {n: (pos, t["rB"]) for n, pos, t in
                          (aux_tags(r) for r in bam_records(out / "Aligned.toTranscriptome.out.bam"))}
        assert transcriptomic == {"sense": (50, [1, 100, 51, 150]), "antisense": (200, [1, 100, 201, 300]),
                                  "spliced": (750, [1, 100, 751, 850])}, (strand, transcriptomic)  # junction is contiguous in t1
    checks.append("GX/GN without Solo follow --soloStrand; toTranscriptome rB in transcript coordinates")

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
