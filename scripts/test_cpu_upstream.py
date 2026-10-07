#!/usr/bin/env python3
"""Small real-STAR regression fixtures; no source datasets or third-party Python packages.

Usage: python3 scripts/test_cpu_upstream.py --star-exe /path/to/STAR [--ref-exe /path/to/STAR.before]
Evidence is retained in a unique temporary directory, not in canonical data outputs.
"""
import argparse
import gzip
import hashlib
import json
from pathlib import Path
import random
import struct
import subprocess
import tempfile


def bam_scientific_header(path):
    """Reference IDs/order and scientific header lines; PG invocation is provenance."""
    with gzip.open(path, 'rb') as stream:
        assert stream.read(4)==b'BAM\1'
        length=struct.unpack('<i',stream.read(4))[0]
        assert length>=0
        text=stream.read(length)
        assert len(text)==length
        count=struct.unpack('<i',stream.read(4))[0]
        assert count>=0
        refs=[]
        for _ in range(count):
            length=struct.unpack('<i',stream.read(4))[0]
            assert length>0
            name=stream.read(length)
            assert len(name)==length
            refs.append((name,struct.unpack('<i',stream.read(4))[0]))
        return refs,sorted(line for line in text.decode().splitlines() if line.startswith(('@SQ','@RG','@HD')))


def scientific_final_fields(path):
    """Exclude only known non-scientific job timestamps and mapping speed."""
    fields={}
    excluded={'Started job on','Started mapping on','Finished on','Mapping speed, Million of reads per hour'}
    for line in Path(path).read_text().splitlines():
        if '|' in line:
            key,value=(part.strip() for part in line.split('|',1))
            if key not in excluded:
                assert key not in fields
                fields[key]=value
    assert fields
    return fields


def bam_records(path):
    records = []
    # Stream BGZF members: gzip.decompress on concatenated members repeatedly
    # copies the remaining compressed suffix and is unsuitable for real libraries.
    with gzip.open(path, "rb") as stream:
        assert stream.read(4) == b"BAM\1"
        length = struct.unpack("<i", stream.read(4))[0]
        assert length >= 0 and len(stream.read(length)) == length
        refs = struct.unpack("<i", stream.read(4))[0]
        for _ in range(refs):
            length = struct.unpack("<i", stream.read(4))[0]
            assert length > 0 and len(stream.read(length+4)) == length+4
        while True:
            size_bytes = stream.read(4)
            if not size_bytes:
                break
            size = struct.unpack("<i", size_bytes)[0]
            assert size >= 32
            record = stream.read(size)
            assert len(record) == size
            records.append(record)
    return sorted(records)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--star-exe", required=True)
    parser.add_argument("--ref-exe")
    parser.add_argument("--threads", type=int, default=2)
    parser.add_argument("--shared-memory", action="store_true",
                        help="Unix-only: compare real STAR keep/remove/load modes with ordinary loading")
    parser.add_argument("--sa-sparse", type=int, default=1,
                        help="Miniature index suffix-array sparsity (exercise sparse seed competition)")
    args = parser.parse_args()
    if args.threads < 1:
        parser.error("Thread count must be positive")
    if args.sa_sparse < 1:
        parser.error("Suffix-array sparsity must be positive")
    root = Path(tempfile.mkdtemp(prefix="star-cpu-regression-"))
    print(f"Evidence retained at: {root}", flush=True)
    checks = []

    def run(label, options, binary=None, valid=True):
        out = root / label
        out.mkdir()
        cmd = [binary or args.star_exe, "--runThreadN", str(args.threads), *options,
               "--outFileNamePrefix", str(out) + "/"]
        proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        (out / "command.json").write_text(json.dumps(cmd))
        (out / "console.log").write_text(proc.stdout)
        assert (proc.returncode == 0) == valid, (label, proc.returncode, proc.stdout[-3000:])
        return out

    rng = random.Random(12345)
    sequence = "".join(rng.choices("ACGT", k=20000))
    fasta = root / "reference.fa"
    fasta.write_text(">chr1\n" + sequence + "\n")
    gtf = root / "genes.gtf"
    gtf.write_text('chr1\ttest\texon\t101\t400\t.\t+\t.\tgene_id "g1"; transcript_id "t1"; gene_name "G1"; gene_biotype "protein_coding";\n'
                   'chr1\ttest\texon\t701\t1000\t.\t+\t.\tgene_id "g1"; transcript_id "t1"; gene_name "G1"; gene_biotype "protein_coding";\n'
                   'chr1\ttest\texon\t2001\t2500\t.\t+\t.\tgene_id "g2"; transcript_id "t2"; gene_name "G2";\n')
    genome = root / "genome"
    genome.mkdir()
    run("index", ["--runMode", "genomeGenerate", "--genomeDir", str(genome),
                  "--genomeFastaFiles", str(fasta), "--sjdbGTFfile", str(gtf),
                  "--sjdbOverhang", "49", "--genomeSAindexNbases", "4", "--genomeChrBinNbits", "10",
                  "--genomeSAsparseD", str(args.sa_sparse)])
    common = ["--genomeDir", str(genome)]
    reads = root / "reads.fastq"
    fragments = [sequence[p:p+100] for p in range(100, 9000, 137)]
    fragments += [sequence[350:400] + sequence[700:750], "N" * 100]
    reads.write_text("".join(f"@r{i}\n{s}\n+\n{'I'*len(s)}\n" for i, s in enumerate(fragments)))
    mapped = run("mapped", common + ["--readFilesIn", str(reads), "--outSAMtype", "BAM", "Unsorted",
                                      "--outSAMunmapped", "Within"])
    assert len(bam_records(mapped / "Aligned.out.bam")) == len(fragments)
    checks.append("real miniature genome and mapped/spliced/unmapped BAM")
    if args.shared_memory:
        # The unique fixture index determines its IPC key. Removal is confined to
        # this index, and finally cleans a failed fixture without touching others.
        try:
            run("shared-load-exit", common + ["--genomeLoad", "LoadAndExit"])
            for mode in ("LoadAndKeep", "LoadAndRemove"):
                shared = run("shared-" + mode, common + ["--genomeLoad", mode,
                             "--readFilesIn", str(reads), "--outSAMtype", "BAM", "Unsorted",
                             "--outSAMunmapped", "Within"])
                assert bam_records(shared / "Aligned.out.bam") == bam_records(mapped / "Aligned.out.bam")
                assert scientific_final_fields(shared / "Log.final.out") == scientific_final_fields(mapped / "Log.final.out")
                assert (shared / "SJ.out.tab").read_bytes() == (mapped / "SJ.out.tab").read_bytes()
            run("shared-reload-exit", common + ["--genomeLoad", "LoadAndExit"])
            run("shared-remove", common + ["--genomeLoad", "Remove"])
            checks.append("shared-memory LoadAndExit/Keep/Remove and explicit Remove: exact BAM, junction and scientific log equivalence")
        finally:
            cleanup = subprocess.run([args.star_exe, *common, "--genomeLoad", "Remove",
                                      "--outFileNamePrefix", str(root / "shared-cleanup")],
                                     capture_output=True, text=True, timeout=30)
            (root / "shared-cleanup.log").write_text(cleanup.stdout + cleanup.stderr)
    if args.ref_exe:
        before = run("before", common + ["--readFilesIn", str(reads), "--outSAMtype", "BAM", "Unsorted",
                                          "--outSAMunmapped", "Within"], args.ref_exe)
        assert bam_records(before / "Aligned.out.bam") == bam_records(mapped / "Aligned.out.bam")
        assert (before / "SJ.out.tab").read_bytes() == (mapped / "SJ.out.tab").read_bytes()
        checks.append("before/after alignment-record and junction equivalence on miniature fixture")
    sorted_options = common + ["--readFilesIn", str(reads), "--outSAMtype", "BAM", "SortedByCoordinate",
                               "--limitBAMsortRAM", "100000000", "--quantMode", "TranscriptomeSAM"]
    sorted_output = run("sorted-transcriptome", sorted_options)
    for artifact in ("Aligned.sortedByCoord.out.bam", "Aligned.toTranscriptome.out.bam"):
        assert bam_records(sorted_output / artifact)
        if args.ref_exe:
            # One reference run serves both coordinate and transcriptome writers.
            if artifact == "Aligned.sortedByCoord.out.bam":
                sorted_before = run("sorted-before", sorted_options, args.ref_exe)
            assert bam_records(sorted_output / artifact) == bam_records(sorted_before / artifact)
    checks.append("coordinate-sorted and transcriptome BAM writers")

    # Force a deferred compressed write failure; opening a file is not proof
    # that BAM flushing/closing succeeded. Never touch a user's output file.
    if Path("/dev/full").exists():
        failed = root/"disk-full"
        failed.mkdir()
        (failed/"Aligned.out.bam").symlink_to("/dev/full")
        command = [args.star_exe, "--runThreadN", str(args.threads), *common,
                   "--readFilesIn", str(reads), "--outSAMtype", "BAM", "Unsorted",
                   "--outFileNamePrefix", str(failed)+"/"]
        process = subprocess.run(command, capture_output=True, text=True)
        (failed/"command.json").write_text(json.dumps(command))
        (failed/"console.log").write_text(process.stdout+process.stderr)
        assert process.returncode != 0, "Deferred BAM write failure was accepted"
        assert not (failed/"Log.final.out").exists()
        assert "ALL DONE!" not in (failed/"Log.out").read_text()
        checks.append("deferred BAM disk-full error blocks scientific completion")

    # FASTA uses a distinct input branch; C++20 removed unsafe char* extraction.
    fasta_reads = root / "reads.fasta"
    fasta_reads.write_text("".join(f">r{i} comment\n{s[:50]}\n{s[50:]}\n"
                                 for i, s in enumerate(fragments)))
    fasta_options = common + ["--readFilesIn", str(fasta_reads), "--outSAMtype", "BAM", "Unsorted",
                              "--outSAMunmapped", "Within"]
    fasta_output = run("fasta", fasta_options)
    assert len(bam_records(fasta_output / "Aligned.out.bam")) == len(fragments)
    if args.ref_exe:
        fasta_before = run("fasta-before", fasta_options, args.ref_exe)
        assert bam_records(fasta_output / "Aligned.out.bam") == bam_records(fasta_before / "Aligned.out.bam")
    oversized_fasta = root / "oversized.fasta"
    oversized_fasta.write_text(">" + "A" * 50000 + "\n" + sequence[100:200] + "\n")
    run("oversized-fasta", common + ["--readFilesIn", str(oversized_fasta), "--outSAMtype", "None"], valid=False)
    checks.append("multiline FASTA identity and oversized FASTA header rejection")

    # Distinct indexes on the two mates, CRLF, no comment and short comments.
    mates = [root / "mate1.fastq", root / "mate2.fastq"]
    for m, file in enumerate(mates, 1):
        file.write_bytes("".join(f"@pair{i}{extra}\r\n{'N'*100}\r\n+\r\n{'I'*100}\r\n"
                                for i, extra in enumerate([f" {m}:Y:0:INDEX{m} extra{m}", "", " 1:Y"])).encode())
    for mode in ["Normal", "BySJout"]:
        out = run("header-" + mode, common + ["--readFilesIn", *map(str, mates), "--outReadsUnmapped", "Fastx",
                                             "--outSAMtype", "None", "--outFilterType", mode])
        for m in (1, 2):
            text = (out / f"Unmapped.out.mate{m}").read_text()
            assert f"{m}:Y:0:INDEX{m} extra{m}" in text
            assert f"@pair0 {m-1}:Y:" in text
            assert "1:Y" in text
            assert len(text.splitlines()) == 12
    checks.append("per-mate FASTQ comments, no-comment/short-comment/CRLF, Normal and BySJout")
    oversized_header = root / "oversized-header.fastq"
    oversized_header.write_text("@long " + "A"*50000 + "\n" + "N"*100 + "\n+\n" + "I"*100 + "\n")
    run("oversized-header", common + ["--readFilesIn", str(oversized_header), "--outSAMtype", "None"], valid=False)
    checks.append("oversized preserved FASTQ header rejected before chunk copy")

    sam = root / "input.sam"
    sam.write_text("@HD\tVN:1.6\nread\t4\t*\t0\t0\t*\t*\t0\t0\t" + sequence[100:200] +
                   "\t" + "I"*100 + "\tXB:B:I,0,4294967295\tXZ:Z:index\n")
    opts = common + ["--readFilesType", "SAM", "SE", "--readFilesIn", str(sam), "--outSAMtype", "BAM", "Unsorted"]
    out = run("sam-B", opts)
    record = bam_records(out / "Aligned.out.bam")[0]
    assert b"XBBI\x02\x00\x00\x00\x00\x00\x00\x00\xff\xff\xff\xff" in record
    assert b"XZZindex\x00" in record
    kept = run("sam-filter", opts + ["--readFilesSAMattrKeep", "XZ"])
    assert b"XBBI" not in bam_records(kept / "Aligned.out.bam")[0]
    for label, tag in [("invalid-B", "XB:B:q,1"), ("oversized-aux", "XZ:Z:" + "A"*12000)]:
        invalid = root / (label + ".sam")
        invalid.write_text(sam.read_text().split("\tXB:")[0] + "\t" + tag + "\n")
        run(label, common + ["--readFilesType", "SAM", "SE", "--readFilesIn", str(invalid),
                             "--outSAMtype", "BAM", "Unsorted"], valid=False)
    checks.append("real SAM-input BAM-output B array, tag whitelist, malformed and oversized rejection")

    whitelist = root / "whitelist.txt"
    whitelist.write_text("ACGTACGT\n")
    cdna = root / "cdna.fastq"
    barcode = root / "barcode.fastq"
    cdna.write_text("@cell\n" + sequence[120:170] + "\n+\n" + "I"*50 + "\n")
    barcode.write_text("@cell\nACGTACGTAAAA\n+\nIIIIIIIIIIII\n")
    solo = common + ["--readFilesIn", str(cdna), str(barcode), "--soloType", "CB_UMI_Simple",
                     "--soloCBwhitelist", str(whitelist), "--soloCBlen", "8", "--soloUMIstart", "9",
                     "--soloUMIlen", "4", "--soloCellFilter", "None", "--outSAMtype", "None",
                     "--soloFeatures", "Gene", "GeneFull_Ex50pAS", "Velocyto"]
    default = run("solo-default", solo)
    biotype = run("solo-biotype", solo + ["--soloOutFormatFeaturesGeneField3", "+"])
    two = run("solo-two-column", solo + ["--soloOutFormatFeaturesGeneField3", "-"])
    default_features = (default / "Solo.out/Gene/raw/features.tsv").read_text()
    assert "Gene Expression" in default_features
    assert "protein_coding" in (biotype / "Solo.out/Gene/raw/features.tsv").read_text()
    assert "MissingGeneType" in (biotype / "Solo.out/Gene/raw/features.tsv").read_text()
    assert all(len(line.split("\t")) == 2 for line in (two / "Solo.out/Gene/raw/features.tsv").read_text().splitlines())
    checks.append("biotype opt-in, missing biotype sentinel, unchanged default and two-column features")
    velocity = default / "Solo.out/Velocyto/raw"
    assert all((velocity / name).is_file() for name in ("spliced.mtx", "unspliced.mtx", "ambiguous.mtx"))
    checks.append("native Velocity always emits all three required raw layers")
    if args.ref_exe:
        solo_before = run("solo-before", solo, args.ref_exe)
        for feature in ("Gene", "GeneFull_Ex50pAS", "Velocyto"):
            folder = default / "Solo.out" / feature / "raw"
            artifacts = list(folder.glob("*.mtx")) + list(folder.glob("*.tsv"))
            assert artifacts, feature
            for path in artifacts:
                relative = path.relative_to(default)
                assert path.read_bytes() == (solo_before / relative).read_bytes(), str(relative)
        checks.append("Gene/GeneFull/Velocity integer matrices and axes match reference on tiny fixture")
    result = {"checks": checks, "binary_sha256": hashlib.sha256(Path(args.star_exe).read_bytes()).hexdigest(),
              "threads": args.threads,
              "reference_sha256": hashlib.sha256(Path(args.ref_exe).read_bytes()).hexdigest() if args.ref_exe else None}
    (root / "result.json").write_text(json.dumps(result, indent=2))
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
