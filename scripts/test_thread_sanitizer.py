#!/usr/bin/env python3
"""Multi-threaded STAR workloads for ThreadSanitizer and thread-count invariance.

A simulated annotated genome and read set drive every threaded code path with
small I/O buffers, so hundreds of chunks are processed concurrently:
  - genomeGenerate (OpenMP suffix-array sort) and on-the-fly junction insertion;
  - paired-end mapping with unsorted + coordinate-sorted BAM, transcriptome BAM,
    gene counts, BySJout filtering, mate overlap merging, chimeric detection,
    unmapped-read output and signal tracks;
  - two-pass mapping with input-order SAM output;
  - the opt-in chunk pipeline and asynchronous BAM/SAM writers;
  - transcriptome BAM with --quantTranscriptomeBAMprimary ReadIndex, compared
    exactly (the default Random primary flag is compared with 0x100 masked);
  - STARsolo droplet (Gene/GeneFull/SJ/Velocyto, multimapper EM) and SmartSeq.

Every scenario is run with --runThreadN 1 and with --threads; outputs that must
not depend on the thread count are compared. When the STAR binary is built with
-DSTAR_TSAN=ON each run writes TSan reports into the evidence directory, and
any report fails the test. With an ordinary build the script is a pure
thread-count invariance test.
"""
import argparse
import gzip
import os
import random
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

COMPLEMENT = str.maketrans("ACGTN", "TGCAN")
SUPPRESSIONS = Path(__file__).resolve().parent / "tsan_suppressions.txt"


def find_archer():
    """LLVM's OpenMP TSan tool: Fedora/RHEL ship it in lib64, Debian/Ubuntu per LLVM version."""
    if os.environ.get("STAR_ARCHER_LIB"):
        return Path(os.environ["STAR_ARCHER_LIB"])
    candidates = [Path("/usr/lib64/libarcher.so"), *sorted(Path("/usr/lib").glob("llvm-*/lib/libarcher.so"), reverse=True)]
    return next((p for p in candidates if p.exists()), Path("/nonexistent/libarcher.so"))


ARCHER = find_archer()
TSAN_EXIT = 66  # TSan's default exit status after reporting races


def revcomp(s):
    return s.translate(COMPLEMENT)[::-1]


def mutate(rng, s, rate=0.01):
    out = list(s)
    for i in range(len(out)):
        if rng.random() < rate:
            out[i] = rng.choice("ACGT".replace(out[i], "") or "A")
    return "".join(out)


def simulate(root, rng, pairs, solo_reads):
    chroms = {"chr1": 600_000, "chr2": 400_000, "chr3": 300_000}
    seq = {c: "".join(rng.choices("ACGT", k=n)) for c, n in chroms.items()}
    repeat = "".join(rng.choices("ACGT", k=400))  # multimapper source, 3 copies
    for c, pos in (("chr1", 550_000), ("chr2", 350_000), ("chr3", 250_000)):
        seq[c] = seq[c][:pos] + repeat + seq[c][pos + len(repeat):]
    (root / "ref.fa").write_text("".join(f">{c}\n{s}\n" for c, s in seq.items()))

    gtf, transcripts = [], []
    for g in range(36):
        c = "chr1" if g < 20 else "chr2"
        start = 5_000 + (g % 20) * 25_000
        strand = "+" if g % 3 else "-"
        exons, p = [], start
        for _ in range(3):
            length = rng.randint(150, 320)
            exons.append((p, p + length - 1))
            p += length + rng.randint(300, 2_000)
        isoforms = {f"T{g}a": exons}
        if g % 4 == 0:
            isoforms[f"T{g}b"] = [exons[0], exons[2]]
        for tid, ex in isoforms.items():
            for a, b in ex:
                gtf.append(f'{c}\tsim\texon\t{a + 1}\t{b + 1}\t.\t{strand}\t.\tgene_id "G{g}"; '
                           f'transcript_id "{tid}"; gene_name "Gene{g}"; gene_biotype "protein_coding";\n')
            mrna = "".join(seq[c][a:b + 1] for a, b in ex)
            transcripts.append(mrna if strand == "+" else revcomp(mrna))
    (root / "genes.gtf").write_text("".join(gtf))

    def genomic(length):
        c = rng.choice(list(seq))
        p = rng.randrange(0, len(seq[c]) - length)
        return seq[c][p:p + length]

    r1, r2 = [], []
    for i in range(pairs):
        kind = rng.random()
        if kind < 0.60:   # spliced transcript fragment
            t = rng.choice(transcripts)
            flen = min(len(t), rng.randint(200, 400))
            p = rng.randrange(0, len(t) - flen + 1)
            frag = t[p:p + flen]
        elif kind < 0.85:  # intronic / intergenic
            frag = genomic(rng.randint(200, 400))
        elif kind < 0.90:  # inter-chromosomal chimera
            frag = genomic(rng.randint(100, 200)) + revcomp(genomic(rng.randint(100, 200)))
        elif kind < 0.95:  # repeat multimapper
            p = rng.randrange(0, len(repeat) - 220)
            frag = repeat[p:p + rng.randint(200, 220)]
        else:              # unmappable
            frag = "".join(rng.choices("ACGT", k=300))
        if rng.random() < 0.5:
            frag = revcomp(frag)
        a, b = mutate(rng, frag[:100]), mutate(rng, revcomp(frag[-100:]))
        r1.append(f"@p{i}\n{a}\n+\n{'I' * len(a)}\n")
        r2.append(f"@p{i}\n{b}\n+\n{'I' * len(b)}\n")
    (root / "R1.fq").write_text("".join(r1))
    (root / "R2.fq").write_text("".join(r2))

    # Droplet single-cell reads: read 1 = CB(16)+UMI(12), read 2 = 90 bp cDNA.
    barcodes = sorted({"".join(rng.choices("ACGT", k=16)) for _ in range(600)})
    cells = barcodes[:60]
    (root / "whitelist.txt").write_text("".join(b + "\n" for b in barcodes))
    s1, s2 = [], []
    for i in range(solo_reads):
        cb = rng.choice(cells) if rng.random() < 0.9 else rng.choice(barcodes)
        if rng.random() < 0.02:  # one-mismatch barcode
            j = rng.randrange(16)
            cb = cb[:j] + rng.choice("ACGT".replace(cb[j], "")) + cb[j + 1:]
        umi = "".join(rng.choices("ACGT", k=12))
        if rng.random() < 0.85:
            t = rng.choice(transcripts)
            p = max(0, len(t) - 90 - int(rng.expovariate(1 / 150)))
            cdna = t[p:p + 90]
        else:
            cdna = genomic(90)
        if len(cdna) < 90:
            cdna = genomic(90)
        s1.append(f"@s{i}\n{cb}{umi}\n+\n{'I' * 28}\n")
        s2.append(f"@s{i}\n{mutate(rng, cdna)}\n+\n{'I' * 90}\n")
    (root / "solo_R1.fq").write_text("".join(s1))
    (root / "solo_R2.fq").write_text("".join(s2))

    # SmartSeq: one single-end file per cell.
    manifest = []
    for c in range(6):
        reads = []
        for i in range(1500):
            t = rng.choice(transcripts)
            p = rng.randrange(0, len(t) - 100)
            reads.append(f"@c{c}r{i}\n{mutate(rng, t[p:p + 100])}\n+\n{'I' * 100}\n")
        path = root / f"smart{c}.fq"
        path.write_text("".join(reads))
        manifest.append(f"{path}\t-\tcell{c}\n")
    (root / "manifest.tsv").write_text("".join(manifest))


def bam_records(path):
    """Header text (without @PG/@CO) and the records, in file order."""
    with gzip.open(path, "rb") as stream:
        assert stream.read(4) == b"BAM\1"
        text = stream.read(struct.unpack("<i", stream.read(4))[0]).decode()
        for _ in range(struct.unpack("<i", stream.read(4))[0]):
            name_len = struct.unpack("<i", stream.read(4))[0]
            stream.read(name_len + 4)
        records = []
        while True:
            size = stream.read(4)
            if not size:
                break
            records.append(stream.read(struct.unpack("<i", size)[0]))
    header = [l for l in text.splitlines() if not l.startswith(("@PG", "@CO"))]
    return header, records


def compare(kind, a, b):
    """Return None when the thread-count-independent content matches."""
    if not a.exists() or not b.exists():
        return f"missing {a if not a.exists() else b}"
    if kind == "bytes":
        same = a.read_bytes() == b.read_bytes()
    elif kind == "lines":  # order-insensitive text, SAM headers without @PG/@CO
        strip = lambda p: sorted(l for l in p.read_text().splitlines() if not l.startswith(("@PG", "@CO")))
        same = strip(a) == strip(b)
    elif kind == "sam":    # ordered, headers without @PG/@CO
        strip = lambda p: [l for l in p.read_text().splitlines() if not l.startswith(("@PG", "@CO"))]
        same = strip(a) == strip(b)
    elif kind == "fastx":  # order-insensitive 4-line records
        group = lambda p: sorted(zip(*[iter(p.read_text().splitlines())] * 4))
        same = group(a) == group(b)
    elif kind == "bam":    # ordered records
        same = bam_records(a) == bam_records(b)
    elif kind == "bamset":  # order-insensitive records
        (ha, ra), (hb, rb) = bam_records(a), bam_records(b)
        same = ha == hb and sorted(ra) == sorted(rb)
    elif kind == "bamset-any-primary":
        # Transcriptome BAM: STAR picks the primary among a read's transcript
        # alignments with a per-thread RNG (ReadAlign_quantTranscriptome.cpp), so
        # FLAG 0x100 legitimately depends on which thread mapped the read.
        unflag = lambda r: r[:15] + bytes([r[15] & 0xFE]) + r[16:]
        (ha, ra), (hb, rb) = bam_records(a), bam_records(b)
        same = ha == hb and sorted(map(unflag, ra)) == sorted(map(unflag, rb))
    elif kind == "final":  # Log.final.out without timing lines
        keep = lambda p: [l for l in p.read_text().splitlines()
                          if not any(w in l for w in ("Started", "Finished", "speed"))]
        same = keep(a) == keep(b)
    else:
        raise ValueError(kind)
    return None if same else f"{a.name} differs ({kind})"


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--star-exe", required=True)
    parser.add_argument("--threads", type=int, default=8)
    parser.add_argument("--pairs", type=int, default=30_000)
    parser.add_argument("--solo-reads", type=int, default=30_000)
    parser.add_argument("--seed", type=int, default=20261023)
    parser.add_argument("--only", nargs="*", help="run only these scenarios")
    parser.add_argument("--allow-tsan-reports", action="store_true",
                        help="record ThreadSanitizer reports without failing")
    args = parser.parse_args()
    if args.threads < 2:
        parser.error("--threads must be at least 2")
    star = str(Path(args.star_exe).resolve(strict=True))
    root = Path(tempfile.mkdtemp(prefix="star-threads-"))
    reports = root / "tsan"
    reports.mkdir()
    print(f"Evidence retained at: {root}", flush=True)
    print(f"OpenMP TSan tool: {ARCHER if ARCHER.exists() else 'not found (OpenMP regions may report false races)'}", flush=True)
    simulate(root, random.Random(args.seed), args.pairs, args.solo_reads)
    failures = []

    def run(label, threads, options, env_extra=None, valid=True):
        out = root / f"{label}.t{threads}"
        out.mkdir(exist_ok=True)
        env = dict(os.environ)
        env["TSAN_OPTIONS"] = " ".join(filter(None, [f"suppressions={SUPPRESSIONS}", env.get("TSAN_OPTIONS", ""),
                                                     f"log_path={reports}/{label}.t{threads}",
                                                     f"exitcode={TSAN_EXIT} second_deadlock_stack=1"]))
        if ARCHER.exists():  # OpenMP happens-before edges for the uninstrumented libomp
            env.setdefault("OMP_TOOL_LIBRARIES", str(ARCHER))
        env.update(env_extra or {})
        command = [star, "--runThreadN", str(threads), *options, "--outFileNamePrefix", f"{out}/"]
        print(f"Running {label} with {threads} thread(s)", flush=True)
        proc = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                              errors="replace", env=env, timeout=3600)
        (out / "console.log").write_text(proc.stdout)
        # A run whose only problem is a recorded race exits with TSAN_EXIT; crashes
        # and sanitizer aborts keep their own status and always count as failures.
        logs = [p.read_text(errors="replace") for p in reports.glob(f"{label}.t{threads}.*")]
        raced = proc.returncode == TSAN_EXIT and logs and not any("ABORTING" in text for text in logs)
        if (proc.returncode == 0 or raced) != valid:
            failures.append(f"{label} t{threads}: exit {proc.returncode}: {proc.stdout[-1500:]}")
        return out

    def scenario(name, options, outputs, env_extra=None, genome=None):
        if args.only and name not in args.only:
            return
        g = ["--genomeDir", str(genome or root / "genome")]
        one = run(name, 1, g + options, env_extra)
        many = run(name, args.threads, g + options, env_extra)
        for rel, kind in outputs:
            problem = compare(kind, one / rel, many / rel)
            if problem:
                failures.append(f"{name}: {problem}")

    def index(name, gtf):
        out = root / name
        for threads in (1, args.threads):
            target = out if threads > 1 else root / f"{name}.t1"
            target.mkdir()
            options = ["--runMode", "genomeGenerate", "--genomeDir", str(target), "--genomeFastaFiles",
                       str(root / "ref.fa"), "--genomeSAindexNbases", "9", "--outTmpDir", str(target) + "_tmp"]
            if gtf:
                options += ["--sjdbGTFfile", str(root / "genes.gtf"), "--sjdbOverhang", "99"]
            run(f"{name}-build", threads, options)
        for f in ("Genome", "SA", "SAindex", "chrNameLength.txt", *(("sjdbList.out.tab", "exonGeTrInfo.tab")
                                                                     if gtf else ())):
            problem = compare("bytes", root / f"{name}.t1" / f, out / f)
            if problem:
                failures.append(f"{name}: {problem}")

    index("genome", gtf=True)
    pe = ["--readFilesIn", str(root / "R1.fq"), str(root / "R2.fq"), "--limitIObufferSize", "250000", "100000"]
    sorted_bam = "Aligned.sortedByCoord.out.bam"

    scenario("pe_full", pe + [
        "--outSAMtype", "BAM", "Unsorted", "SortedByCoordinate", "--outBAMsortingBinsN", "20",
        "--quantMode", "TranscriptomeSAM", "GeneCounts", "--outSAMunmapped", "Within",
        "--outSAMattributes", "NH", "HI", "AS", "nM", "NM", "MD", "jM", "jI", "XS", "MC", "ch",
        "--outFilterType", "BySJout", "--outSAMstrandField", "intronMotif", "--peOverlapNbasesMin", "10",
        "--outReadsUnmapped", "Fastx", "--outWigType", "bedGraph", "--chimSegmentMin", "15",
        "--chimMultimapNmax", "10", "--chimOutType", "Junctions"], [
        ("Aligned.out.bam", "bamset"), (sorted_bam, "bam"), ("Aligned.toTranscriptome.out.bam", "bamset-any-primary"),
        ("ReadsPerGene.out.tab", "bytes"), ("SJ.out.tab", "bytes"), ("Chimeric.out.junction", "lines"),
        ("Unmapped.out.mate1", "fastx"), ("Unmapped.out.mate2", "fastx"),
        ("Signal.Unique.str1.out.bg", "bytes"), ("Signal.UniqueMultiple.str2.out.bg", "bytes"),
        ("Log.final.out", "final")])

    scenario("pe_chim_within_bam", pe + [
        "--outSAMtype", "BAM", "SortedByCoordinate", "--chimSegmentMin", "15", "--chimOutType", "WithinBAM",
        "HardClip", "--outSAMattributes", "NH", "HI", "AS", "nM", "ch"], [
        (sorted_bam, "bam"), ("SJ.out.tab", "bytes"), ("Log.final.out", "final")])

    scenario("twopass_sam", pe + [
        "--twopassMode", "Basic", "--outSAMtype", "SAM", "--outSAMorder", "PairedKeepInputOrder"], [
        ("Aligned.out.sam", "sam"), ("SJ.out.tab", "bytes"), ("Log.final.out", "final")])

    scenario("async_bam", pe + [
        "--outSAMtype", "BAM", "Unsorted", "--quantMode", "TranscriptomeSAM"], [
        ("Aligned.out.bam", "bamset"), ("Aligned.toTranscriptome.out.bam", "bamset-any-primary"), ("SJ.out.tab", "bytes")],
        env_extra={"STAR_CHUNK_PIPELINE": "1", "STAR_ASYNC_BAM": "1"})

    # With ReadIndex the transcriptome primary flag no longer depends on the
    # mapping thread, so the file is compared exactly, FLAG 0x100 included.
    scenario("transcriptome_read_index", pe + [
        "--outSAMtype", "BAM", "Unsorted", "--quantMode", "TranscriptomeSAM",
        "--quantTranscriptomeBAMprimary", "ReadIndex"], [
        ("Aligned.out.bam", "bamset"), ("Aligned.toTranscriptome.out.bam", "bamset")])

    scenario("async_sam", pe + ["--outSAMtype", "SAM"], [
        ("Aligned.out.sam", "lines"), ("SJ.out.tab", "bytes")],
        env_extra={"STAR_CHUNK_PIPELINE": "1", "STAR_ASYNC_SAM": "1"})

    if not args.only or "sjdb_insert" in args.only:
        index("genome_plain", gtf=False)
    scenario("sjdb_insert", pe + [
        "--sjdbGTFfile", str(root / "genes.gtf"), "--sjdbOverhang", "99", "--outSAMtype", "BAM",
        "SortedByCoordinate"], [(sorted_bam, "bam"), ("SJ.out.tab", "bytes")], genome=root / "genome_plain")

    solo_features = ("Gene", "GeneFull", "SJ", "Velocyto")
    solo_outputs = [(sorted_bam, "bam"), ("SJ.out.tab", "bytes")]
    for f in solo_features:
        solo_outputs += [(f"Solo.out/{f}/raw/{n}", "bytes") for n in ("barcodes.tsv", "features.tsv")]
        solo_outputs += [(f"Solo.out/{f}/Features.stats", "bytes")]
    solo_outputs += [(f"Solo.out/{f}/raw/matrix.mtx", "bytes") for f in ("Gene", "GeneFull", "SJ")]
    solo_outputs += [("Solo.out/Velocyto/raw/spliced.mtx", "bytes"), ("Solo.out/Gene/raw/UniqueAndMult-EM.mtx", "bytes"),
                     ("Solo.out/Gene/filtered/matrix.mtx", "bytes"), ("Solo.out/Barcodes.stats", "bytes")]
    scenario("solo_droplet", [
        "--readFilesIn", str(root / "solo_R2.fq"), str(root / "solo_R1.fq"), "--limitIObufferSize", "250000",
        "100000", "--soloType", "CB_UMI_Simple", "--soloCBwhitelist", str(root / "whitelist.txt"),
        "--soloCBlen", "16", "--soloUMIstart", "17", "--soloUMIlen", "12", "--soloFeatures", *solo_features,
        "--soloMultiMappers", "Uniform", "EM", "--soloCellFilter", "CellRanger2.2", "60", "0.99", "10",
        "--outSAMtype", "BAM", "SortedByCoordinate",
        "--outSAMattributes", "NH", "HI", "nM", "AS", "CR", "UR", "CB", "UB", "GX", "GN", "sS", "sQ", "sM"],
        solo_outputs)

    scenario("solo_smartseq", [
        "--readFilesManifest", str(root / "manifest.tsv"), "--soloType", "SmartSeq", "--soloUMIdedup", "Exact",
        "--soloStrand", "Unstranded", "--soloFeatures", "Gene", "GeneFull", "--outSAMtype", "None",
        "--limitIObufferSize", "250000", "100000"], [
        ("Solo.out/Gene/raw/matrix.mtx", "bytes"), ("Solo.out/GeneFull/raw/matrix.mtx", "bytes"),
        ("Solo.out/Gene/raw/barcodes.tsv", "bytes")])

    found = sorted(p for p in reports.iterdir())
    summaries = {}
    for path in found:
        for line in path.read_text(errors="replace").splitlines():
            if line.startswith("SUMMARY: ThreadSanitizer"):
                summaries.setdefault(line, set()).add(path.name.split(".")[0])
    for line, where in sorted(summaries.items()):
        print(f"{line}   [{', '.join(sorted(where))}]")
    if found and not args.allow_tsan_reports:
        failures.append(f"{len(found)} ThreadSanitizer report file(s) in {reports}")
    if failures:
        print("FAIL:\n  " + "\n  ".join(failures))
        return 1
    print(f"PASS: thread-count-invariant outputs, {len(found)} ThreadSanitizer report file(s)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
