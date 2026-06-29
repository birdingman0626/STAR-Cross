R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>STAR WebUI</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;background:#f4f5f7;color:#333}
header{background:#1a1a2e;color:#fff;padding:14px 24px}
header h1{font-size:1.2rem;font-weight:600}
header small{opacity:.65;font-size:.78rem;margin-left:8px}
.wrap{max-width:1100px;margin:0 auto;padding:20px}
.card{background:#fff;border-radius:6px;padding:20px;margin-bottom:18px;box-shadow:0 1px 3px rgba(0,0,0,.1)}
h2{font-size:.78rem;font-weight:700;text-transform:uppercase;letter-spacing:.06em;color:#666;margin-bottom:14px}
.row{display:flex;flex-wrap:wrap;gap:10px;margin-bottom:10px}
.field{display:flex;flex-direction:column;gap:3px;flex:1 1 180px}
.field label{font-size:.76rem;color:#666;font-weight:500}
.field input,.field select{padding:6px 8px;border:1px solid #d0d0d0;border-radius:4px;font-size:.88rem}
.field input:focus,.field select:focus{outline:none;border-color:#5c6bc0}
.checks{display:flex;flex-wrap:wrap;gap:10px;padding-top:4px}
.checks label{font-size:.83rem;display:flex;align-items:center;gap:4px;cursor:pointer}
.sep{border-top:1px solid #f0f0f0;margin:12px 0}
.sec-label{font-size:.72rem;font-weight:700;text-transform:uppercase;letter-spacing:.07em;color:#9c9c9c;margin-bottom:8px}
.btn-primary{padding:8px 20px;border:none;border-radius:4px;cursor:pointer;font-size:.9rem;font-weight:600;background:#5c6bc0;color:#fff}
.btn-primary:hover{background:#3f51b5}
.btn-sm{padding:3px 10px;font-size:.75rem;border-radius:4px;border:1px solid #c5cae9;background:#fff;color:#5c6bc0;cursor:pointer}
.btn-cancel{background:#e53935;color:#fff;font-size:.76rem;padding:3px 9px;border:none;border-radius:4px;cursor:pointer}
.btn-log{background:none;color:#5c6bc0;border:1px solid #c5cae9;font-size:.76rem;padding:3px 9px;border-radius:4px;cursor:pointer}
.err{color:#c62828;font-size:.82rem;margin-top:8px}
.note{font-size:.76rem;color:#aaa;margin-top:6px}
table{width:100%;border-collapse:collapse;font-size:.85rem}
th{text-align:left;padding:7px 10px;border-bottom:2px solid #eee;color:#666;font-size:.74rem;text-transform:uppercase;font-weight:700}
td{padding:7px 10px;border-bottom:1px solid #f0f0f0;vertical-align:middle}
tr:hover td{background:#fafbff}
.badge{display:inline-block;padding:2px 8px;border-radius:10px;font-size:.72rem;font-weight:700}
.s-running{background:#e3f2fd;color:#1565c0}
.s-succeeded{background:#e8f5e9;color:#2e7d32}
.s-failed{background:#ffebee;color:#c62828}
.s-queued{background:#fff3e0;color:#e65100}
.s-cancelled{background:#f0f0f0;color:#666}
.hrow{display:flex;justify-content:space-between;align-items:center;margin-bottom:14px}
.hrow h2{margin-bottom:0}
#log-pre{background:#1e1e1e;color:#d4d4d4;padding:14px;border-radius:4px;font-family:Consolas,monospace;font-size:.78rem;white-space:pre-wrap;max-height:420px;overflow-y:auto}
#log-box{display:none}
#cmd-box{background:#f8f8f8;border:1px solid #e0e0e0;border-radius:4px;padding:10px 12px;font-family:Consolas,monospace;font-size:.78rem;white-space:pre-wrap;max-height:200px;overflow-y:auto;color:#222;line-height:1.65}
.cmd-hdr{display:flex;justify-content:space-between;align-items:center;margin-bottom:4px}
.cmd-hdr span{font-size:.76rem;font-weight:500;color:#666}
.field[data-tip]{position:relative}
.field[data-tip]>label{cursor:help}
.field[data-tip]:hover::after{content:attr(data-tip);position:absolute;bottom:calc(100% + 6px);left:0;min-width:200px;max-width:290px;background:#1e2535;color:#dde3f0;padding:8px 11px;border-radius:5px;font-size:.75rem;font-weight:400;line-height:1.5;white-space:normal;pointer-events:none;z-index:200;box-shadow:0 3px 14px rgba(0,0,0,.35)}
</style>
</head>
<body>
<header>
  <h1>STAR WebUI<small id="ver"></small></h1>
</header>
<div class="wrap">

<div class="card">
  <h2>Submit Job</h2>
  <form id="jform">

    <!-- ── Run config ── -->
    <div class="row">
      <div class="field" style="flex:0 0 160px" data-tip="alignReads: map reads to a genome index. genomeGenerate: build a genome index (do this first). soloCellFiltering: re-filter cell barcodes from existing STARsolo output.">
        <label>Run Mode</label>
        <select id="runMode">
          <option value="alignReads">alignReads</option>
          <option value="genomeGenerate">genomeGenerate</option>
          <option value="soloCellFiltering">soloCellFiltering</option>
        </select>
      </div>
      <div class="field" data-tip="Path to the genome index directory. Built with --runMode genomeGenerate. Must contain SA, Genome, genomeParameters.txt and other index files.">
        <label>Genome Directory</label>
        <input id="genomeDir" type="text" placeholder="/path/to/genome/index">
        <div id="genomeDirStatus" style="font-size:.76rem;min-height:1.1em"></div>
      </div>
      <div class="field" style="flex:0 0 70px" data-tip="Number of parallel CPU threads. Set to the number of physical cores for best performance. More threads = faster but proportionally more RAM.">
        <label>Threads</label>
        <input id="threads" type="number" value="4" min="1">
      </div>
    </div>

    <!-- Genome generation params -->
    <div class="row" id="genGenRow" style="display:none">
      <div class="field" data-tip="Reference genome FASTA file(s). Required for genomeGenerate. For human or mouse download the primary assembly FASTA from Ensembl or NCBI. Space-separate multiple files (e.g. per-chromosome).">
        <label>Genome FASTA file(s) <span style="font-weight:400;color:#aaa">(required; space-separated for multiple chromosomes)</span></label>
        <input id="genomeFastaFiles" type="text" placeholder="/data/GRCh38.fa">
      </div>
      <div class="field" style="flex:0 0 155px" data-tip="Suffix array pre-index string length. Default 14 works for genomes >= 500 MB. For smaller genomes scale down: min(14, floor(log2(genomeSize)/2 - 1)). Example: 100 MB genome needs value 10.">
        <label>genomeSAindexNbases <span style="font-weight:400;color:#aaa">(4-14)</span></label>
        <input id="genomeSAindexNbases" type="number" value="14" min="4" max="14">
      </div>
      <div class="field" style="flex:0 0 110px" data-tip="Splice junction database overhang. Optimal value is readLength - 1. Default 100 works for most experiments. Set during genome generation; affects novel junction detection sensitivity.">
        <label>sjdbOverhang</label>
        <input id="sjdbOverhang" type="number" value="100" min="1">
      </div>
    </div>

)HTML"
R"HTML(
    <!-- ── Input reads ── -->
    <div class="row">
      <div class="field" data-tip="Read 1 FASTQ file(s). For 10x scRNA-seq: the short barcode+UMI read (26-28 bp). For standard RNA-seq: the forward read. Separate multiple lanes with spaces. Compression setting below must match.">
        <label>FASTQ Read 1 <span style="font-weight:400;color:#aaa">(space-separated for multiple lanes)</span></label>
        <input id="r1" type="text" placeholder="/data/R1.fastq.gz">
      </div>
      <div class="field" data-tip="Read 2 FASTQ file(s). For 10x scRNA-seq: the cDNA read (longer). For standard RNA-seq: the reverse read. Leave blank for single-end sequencing.">
        <label>FASTQ Read 2 <span style="font-weight:400;color:#aaa">(blank = single-end)</span></label>
        <input id="r2" type="text" placeholder="/data/R2.fastq.gz">
      </div>
      <div class="field" style="flex:0 0 210px" data-tip="Decompression command for compressed input reads. Must match the file extension: zcat for .gz (most common), bzcat for .bz2, zstdcat for .zst. Leave Uncompressed for plain FASTQ.">
        <label>File Compression</label>
        <select id="rfc">
          <option value="">Uncompressed</option>
          <option value="zcat" selected>gzip (.gz) - zcat</option>
          <option value="bzcat">bzip2 (.bz2) - bzcat</option>
          <option value="zstdcat">zstd (.zst) - zstdcat</option>
          <option value="custom">Custom...</option>
        </select>
      </div>
      <div class="field" id="rfcCustomWrap" style="display:none;flex:0 0 160px">
        <label>Custom command</label>
        <input id="rfcCustomVal" type="text" placeholder="pigz -dc">
      </div>
    </div>

    <!-- ── Output ── -->
    <div class="row">
      <div class="field" data-tip="Prefix for all output files. The directory must already exist. Trailing slash = directory (e.g. /out/ makes /out/Aligned.out.bam). Non-slash suffix adds a name tag (e.g. /out/s1_ makes /out/s1_Aligned.out.bam).">
        <label>Output Prefix <span style="font-weight:400;color:#aaa">(directory must exist)</span></label>
        <input id="outPrefix" type="text" placeholder="/data/output/sample_">
      </div>
      <div class="field" style="flex:0 0 230px" data-tip="Output alignment format. BAM SortedByCoordinate: required for IGV, featureCounts, HTSeq. BAM Unsorted: faster, use with --quantMode TranscriptomeSAM for RSEM. CRAM: smaller referenceless CRAM (no reference FASTA needed). None: skip alignment output for count-only runs.">
        <label>outSAMtype</label>
        <select id="samtype">
          <option value="BAM SortedByCoordinate">BAM SortedByCoordinate</option>
          <option value="BAM Unsorted">BAM Unsorted</option>
          <option value="CRAM SortedByCoordinate">CRAM SortedByCoordinate</option>
          <option value="CRAM Unsorted">CRAM Unsorted</option>
          <option value="SAM">SAM</option>
          <option value="None">None (count only)</option>
        </select>
      </div>
    </div>

    <!-- Alignment options (alignReads only) -->
    <div class="row" id="alignRow" style="display:none">
      <div class="field" style="flex:0 0 170px" data-tip="2-pass mapping: first pass discovers novel splice junctions, second pass maps with them included. Recommended for improved sensitivity and accuracy. Approximately doubles runtime.">
        <label>twopassMode</label>
        <select id="twopassMode">
          <option value="None">None (1-pass)</option>
          <option value="Basic">Basic (2-pass, recommended)</option>
        </select>
      </div>
      <div class="field" data-tip="TranscriptomeSAM: output a transcriptome-aligned BAM for RSEM or salmon. GeneCounts: output per-gene read count table (like featureCounts). Can enable both simultaneously.">
        <label>quantMode</label>
        <div class="checks">
          <label><input type="checkbox" class="qmode" value="TranscriptomeSAM"> TranscriptomeSAM</label>
          <label><input type="checkbox" class="qmode" value="GeneCounts"> GeneCounts</label>
        </div>
      </div>
      <div class="field" style="flex:0 0 175px" data-tip="intronMotif: adds XS:A strand tag required by Cufflinks, StringTie, and DEXSeq for unstranded libraries. Not needed for dUTP-based strand-specific protocols which use the flag bits instead.">
        <label>outSAMstrandField</label>
        <select id="outSAMstrandField">
          <option value="None">None</option>
          <option value="intronMotif">intronMotif (Cufflinks/StringTie)</option>
        </select>
      </div>
    </div>

    <!-- ── STARsolo ── -->
    <div class="sep"></div>
    <div class="sec-label">STARsolo</div>
    <div class="row">
      <div class="field" style="flex:0 0 190px" data-tip="CB_UMI_Simple: fixed CB+UMI structure (10x Chromium, DropSeq, SeqWell). CB_UMI_Complex: tiled or variable barcode layout (inDrops, custom). SmartSeq: plate-based full-length RNA-seq (no CB/UMI). None: plain alignReads without cell demultiplexing.">
        <label>soloType</label>
        <select id="soloType">
          <option value="">None (plain alignReads)</option>
          <option value="CB_UMI_Simple">CB_UMI_Simple (10x, DropSeq...)</option>
          <option value="CB_UMI_Complex">CB_UMI_Complex (inDrops...)</option>
          <option value="SmartSeq">SmartSeq</option>
        </select>
      </div>
      <div class="field" id="chemWrap" style="flex:0 0 240px;display:none" data-tip="Pre-configured CB+UMI lengths for common platforms. 10x v3/v4: CB16+UMI12 (28 bp R1). 10x v2: CB16+UMI10 (26 bp R1). DropSeq: CB12+UMI8. SeqWell: CB8+UMI8. Custom: enter lengths manually.">
        <label>Chemistry Preset</label>
        <select id="chemPreset">
          <option value="10xv3">10x Chromium v3 / v4 &nbsp;(CB16 UMI12)</option>
          <option value="10xv2">10x Chromium v2 &nbsp;(CB16 UMI10)</option>
          <option value="dropseq">DropSeq &nbsp;(CB12 UMI8)</option>
          <option value="seqwell">SeqWell s3 &nbsp;(CB8 UMI8)</option>
          <option value="custom">Custom...</option>
        </select>
      </div>
      <div class="field" id="whitelistWrap" style="display:none" data-tip="Path to list of valid cell barcodes. For 10x v3/v4: 3M-february-2018.txt.gz. For 10x v2: 737K-august-2016.txt. Download from the Cell Ranger reference data package. Use - to auto-detect (not recommended).">
        <label>CB Whitelist</label>
        <input id="whitelist" type="text" placeholder="/data/3M-february-2018.txt">
      </div>
      <div class="field" id="clipWrap" style="flex:0 0 170px;display:none" data-tip="CellRanger4: trims TSO and polyA sequences from cDNA reads. Required for 10x v3/v4 libraries to avoid miscounting reads at transcript ends. AdapterTrimST: for STARsolo-specific adapter trimming.">
        <label>clipAdapterType</label>
        <select id="clipAdapterType">
          <option value="">None</option>
          <option value="CellRanger4">CellRanger4 (10x v3/v4)</option>
          <option value="AdapterTrimST">AdapterTrimST</option>
        </select>
      </div>
      <div class="field" id="soloStrandWrap" style="flex:0 0 155px;display:none" data-tip="Library strand orientation relative to the mRNA. Forward: most 10x Chromium libraries. Reverse: some library kits. Unstranded: non-stranded protocols. Incorrect setting causes major expression miscounting.">
        <label>soloStrand</label>
        <select id="soloStrand">
          <option value="Forward">Forward (default)</option>
          <option value="Reverse">Reverse</option>
          <option value="Unstranded">Unstranded</option>
        </select>
      </div>
      <div class="field" id="soloUMIdedupWrap" style="flex:0 0 200px;display:none" data-tip="UMI deduplication strategy. 1MM_All: merge UMIs within 1 Hamming distance (default, most permissive). 1MM_Directional: directional error correction (recommended for high-depth data). Exact: no correction. NoDedup: count all reads.">
        <label>soloUMIdedup</label>
        <select id="soloUMIdedup">
          <option value="1MM_All">1MM_All (default)</option>
          <option value="1MM_Directional">1MM_Directional</option>
          <option value="Exact">Exact</option>
          <option value="NoDedup">NoDedup</option>
        </select>
      </div>
    </div>

    <!-- Custom chemistry fields -->
    <div class="row" id="chemCustom" style="display:none">
      <div class="field" style="flex:0 0 85px"><label>soloCBlen</label><input id="soloCBlen" type="number" value="16" min="1"></div>
      <div class="field" style="flex:0 0 85px"><label>soloUMIlen</label><input id="soloUMIlen" type="number" value="12" min="1"></div>
      <div class="field" style="flex:0 0 85px"><label>soloCBstart</label><input id="soloCBstart" type="number" value="1" min="1"></div>
      <div class="field" style="flex:0 0 85px"><label>soloUMIstart</label><input id="soloUMIstart" type="number" value="17" min="1"></div>
      <div class="field" style="flex:0 0 260px">
        <label>&nbsp;</label>
        <label class="checks" style="margin-top:2px"><input type="checkbox" id="disableStrictLen" checked><span>Disable strict barcode length check</span></label>
      </div>
    </div>
)HTML"
R"HTML(

    <!-- soloFeatures / multiMapper / cellFilter -->
    <div class="row" id="soloFeatRow" style="display:none">
      <div class="field" data-tip="Gene: counts reads overlapping exons (spliced RNA). GeneFull: counts over full gene body including introns (snRNA-seq). GeneFull_Ex50pAS: like GeneFull but requires >=50% read overlap with annotated sequence (reduces noise). SJ: splice junction counts. Velocyto: spliced/unspliced/ambiguous counts for RNA velocity.">
        <label>soloFeatures</label>
        <div class="checks">
          <label><input type="checkbox" class="sfeat" value="Gene" checked> Gene</label>
          <label><input type="checkbox" class="sfeat" value="GeneFull"> GeneFull</label>
          <label><input type="checkbox" class="sfeat" value="GeneFull_Ex50pAS"> GeneFull_Ex50pAS</label>
          <label><input type="checkbox" class="sfeat" value="SJ"> SJ</label>
          <label><input type="checkbox" class="sfeat" value="Velocyto"> Velocyto</label>
        </div>
      </div>
      <div class="field" style="flex:0 0 155px" data-tip="How to assign reads mapping to multiple genes. Uniform: distribute equally. EM: expectation-maximization, most accurate but slower. Rescue: simple rescue heuristic. PropUnique: proportional to unique counts.">
        <label>soloMultiMappers</label>
        <select id="soloMM">
          <option value="Uniform">Uniform</option>
          <option value="EM">EM</option>
          <option value="Rescue">Rescue</option>
          <option value="PropUnique">PropUnique</option>
        </select>
      </div>
      <div class="field" style="flex:0 0 210px" data-tip="Cell filtering method. EmptyDrops_CR: statistical model distinguishing real cells from empty droplets (recommended). CellRanger2 200: knee-point method, min 200 UMIs. TopCells 500: top 500 barcodes by count. None: no filtering, output all barcodes.">
        <label>soloCellFilter</label>
        <select id="cellFilterSel">
          <option value="EmptyDrops_CR">EmptyDrops_CR (recommended)</option>
          <option value="CellRanger2 200">CellRanger2 200</option>
          <option value="TopCells 500">TopCells 500</option>
          <option value="None">None (no filtering)</option>
          <option value="custom">Custom…</option>
        </select>
      </div>
      <div class="field" id="cfCustomWrap" style="display:none;flex:0 0 220px">
        <label>Custom soloCellFilter</label>
        <input id="cfCustomVal" type="text" placeholder="EmptyDrops_CR 10 45000 90 0.01 20000 0.01">
      </div>
    </div>

    <!-- GTF (optional) -->
    <div class="row">
      <div class="field" data-tip="Gene annotation in GTF/GFF format. Optional if already baked into the genome index. Required for --quantMode GeneCounts and for STARsolo gene-level output. Download from Ensembl or GENCODE.">
        <label>GTF file <span style="font-weight:400;color:#aaa">(optional, skip if already embedded in genome index)</span></label>
        <input id="gtf" type="text" placeholder="/data/genes.gtf">
      </div>
    </div>

    <!-- Advanced options -->
    <div class="sep"></div>
    <details>
      <summary style="cursor:pointer;font-size:.76rem;font-weight:700;text-transform:uppercase;letter-spacing:.07em;color:#9c9c9c;padding:4px 0;list-style:none">Advanced</summary>
      <div class="row" style="margin-top:10px">
        <div class="field" style="flex:0 0 175px" data-tip="Max number of loci a read may align to. Reads exceeding this are considered unmapped. Default 10. Set to 1 to keep only uniquely mapping reads. Increase for repetitive element analysis.">
          <label>outFilterMultimapNmax</label>
          <input id="multimapNmax" type="number" value="10" min="1">
        </div>
        <div class="field" style="flex:0 0 220px" data-tip="RAM limit for BAM coordinate sorting in bytes. 0 = auto-detect from available memory. For large genomes you may need to set this explicitly, e.g. 20000000000 for 20 GB. Increase if sorting fails with an out-of-memory error.">
          <label>limitBAMsortRAM (bytes, 0=auto)</label>
          <input id="limitBAMsortRAM" type="number" value="0" min="0">
        </div>
      </div>
      <div class="row" style="margin-top:4px">
        <div class="field" style="flex:1" data-tip="Extra STAR arguments appended verbatim to the command line (space-separated). Examples: --legacy (use upstream 2.7.11b algorithm variants), --chimSegmentMin 20 (enable chimeric output).">
          <label>Extra STAR arguments</label>
          <input id="extraArgs" type="text" placeholder="e.g. --legacy  or  --chimSegmentMin 20">
        </div>
      </div>
    </details>

    <!-- ── Command preview ── -->
    <div class="sep"></div>
    <div class="cmd-hdr">
      <span>Command Preview</span>
      <button type="button" class="btn-sm" onclick="copyCmd()">Copy</button>
    </div>
    <div id="cmd-box">STAR ...</div>

    <div id="ferr" class="err"></div>
    <div style="margin-top:14px;display:flex;align-items:center;gap:18px;flex-wrap:wrap">
      <button type="submit" class="btn-primary">Submit Job</button>
      <label style="font-size:.85rem;display:flex;align-items:center;gap:6px;cursor:pointer">
        <input type="checkbox" id="generateReport" checked>
        Generate HTML report on completion
      </label>
    </div>
    <p class="note">Output directory must exist before submitting.</p>
  </form>
</div>

<!-- Job list -->
<div class="card">
  <div class="hrow">
    <h2>Jobs</h2>
    <span style="font-size:.75rem;color:#aaa">auto-refresh 3s</span>
  </div>
  <table>
    <thead><tr>
      <th>ID</th><th>State</th><th>Mode</th>
      <th>Output</th><th>Submitted</th><th>Duration</th><th></th>
    </tr></thead>
    <tbody id="jtbody"></tbody>
  </table>
  <p id="nojobs" style="color:#aaa;font-size:.83rem;margin-top:10px">No jobs yet.</p>
</div>

<!-- Log viewer -->
<div class="card" id="log-box">
  <div class="hrow">
    <h2>Logs &mdash; Job <span id="log-jid"></span></h2>
    <button class="btn-sm" onclick="closeLogs()">Close</button>
  </div>
  <pre id="log-pre">(loading...)</pre>
</div>

</div>
<script>
const CHEM = {
  '10xv3':   [16, 12, 1, 17, 0],
  '10xv2':   [16, 10, 1, 17, 0],
  'dropseq': [12,  8, 1, 13, 0],
  'seqwell': [ 8,  8, 1,  9, 0],
};

let watchId = null;

fetch('/props').then(r=>r.json()).then(d=>{
  document.getElementById('ver').textContent = ' v'+d.version+' \u2022 '+d.platform;
  if (d.cpuCount > 0) document.getElementById('threads').value = d.cpuCount;
  if (d.outDir) {
    const base = (d.outDir.endsWith('/')||d.outDir.endsWith('\\')) ? d.outDir : d.outDir+'/';
    const now  = new Date();
    const ts   = now.getFullYear().toString()
               + String(now.getMonth()+1).padStart(2,'0')
               + String(now.getDate()).padStart(2,'0')
               + '-';
    document.getElementById('outPrefix').value = base + ts;
  }
  updateCmd();
});
)HTML"
R"HTML(

// ── Visibility wiring ──
function onSoloTypeChange() {
  const isSolo = document.getElementById('soloType').value !== '';
  ['chemWrap','whitelistWrap','clipWrap','soloStrandWrap','soloUMIdedupWrap','soloFeatRow'].forEach(id=>{
    document.getElementById(id).style.display = isSolo ? 'flex' : 'none';
  });
  if (!isSolo) document.getElementById('chemCustom').style.display = 'none';
  else onChemPresetChange();
  updateCmd();
}
function onChemPresetChange() {
  const isCustom = document.getElementById('chemPreset').value === 'custom';
  document.getElementById('chemCustom').style.display = isCustom ? 'flex' : 'none';
  updateCmd();
}
// ── Genome directory probe ──
let _probeTimer = null;
function scheduleProbe() {
  clearTimeout(_probeTimer);
  _probeTimer = setTimeout(probeGenomeDir, 650);
}
async function probeGenomeDir() {
  const dir = document.getElementById('genomeDir').value.trim();
  const el  = document.getElementById('genomeDirStatus');
  if (!dir) { el.innerHTML = ''; return; }
  el.innerHTML = '<span style="color:#9c9c9c;font-style:italic">checking...</span>';
  let d;
  try { d = await fetch('/genome/probe?dir='+encodeURIComponent(dir)).then(r=>r.json()); }
  catch(e) { el.innerHTML = ''; return; }
  if (d.type === 'star_index') {
    el.innerHTML = '<span style="color:#2e7d32">Valid STAR genome index</span>';
  } else if (d.type === 'cellranger_ref') {
    const sd = d.starDir || '', gf = d.gtfFile || '';
    el.innerHTML = '<span style="color:#e65100">CellRanger reference detected \u2014 '
      + '<button type="button" class="btn-sm" onclick="applyCRRef('+JSON.stringify(sd)+','+JSON.stringify(gf)+')">'
      + 'Use star/ index + genes/genes.gtf</button></span>';
  } else if (d.type === 'not_found') {
    el.innerHTML = '<span style="color:#c62828">Directory not found</span>';
  } else if (d.type === 'has_source') {
    const nf = (d.fastaFiles||[]).length, ng = (d.gtfFiles||[]).length;
    if (nf > 0 || ng > 0) {
      const hint = [];
      if (nf) hint.push(nf+' FASTA');
      if (ng) hint.push(ng+' GTF');
      el.innerHTML = '<span style="color:#1565c0">Found '+hint.join(', ')+' file(s) \u2014 '
        + '<button type="button" class="btn-sm" onclick="switchToGenomeGenerate('
        + JSON.stringify(d.fastaFiles||[])+','+JSON.stringify((d.gtfFiles||[])[0]||'')+')">Switch to genomeGenerate</button></span>';
    } else {
      el.innerHTML = '<span style="color:#9c9c9c">No STAR index found \u2014 use genomeGenerate to build one</span>';
    }
  } else {
    el.innerHTML = '<span style="color:#9c9c9c">No STAR index found \u2014 use genomeGenerate to build one</span>';
  }
}
function applyCRRef(starDir, gtfFile) {
  document.getElementById('genomeDir').value = starDir;
  if (gtfFile) document.getElementById('gtf').value = gtfFile;
  document.getElementById('genomeDirStatus').innerHTML =
    '<span style="color:#2e7d32">Applied: using star/ index</span>';
  updateCmd();
}
function switchToGenomeGenerate(fastas, gtf) {
  document.getElementById('runMode').value = 'genomeGenerate';
  onRunModeChange();
  if (fastas && fastas.length)
    document.getElementById('genomeFastaFiles').value = fastas.join(' ');
  if (gtf) document.getElementById('gtf').value = gtf;
  document.getElementById('genomeDirStatus').innerHTML =
    '<span style="color:#2e7d32">Switched to genomeGenerate \u2014 set Output Prefix to the index directory</span>';
  updateCmd();
}
document.getElementById('genomeDir').addEventListener('input', scheduleProbe);

function onRunModeChange() {
  const mode = document.getElementById('runMode').value;
  document.getElementById('genGenRow').style.display  = mode==='genomeGenerate' ? 'flex' : 'none';
  document.getElementById('alignRow').style.display   = mode==='alignReads'     ? 'flex' : 'none';
  updateCmd();
}
document.getElementById('runMode').addEventListener('change', onRunModeChange);
document.getElementById('soloType').addEventListener('change', onSoloTypeChange);
document.getElementById('chemPreset').addEventListener('change', onChemPresetChange);
document.getElementById('rfc').addEventListener('change', e=>{
  document.getElementById('rfcCustomWrap').style.display = e.target.value==='custom'?'flex':'none';
  updateCmd();
});
document.getElementById('cellFilterSel').addEventListener('change', e=>{
  document.getElementById('cfCustomWrap').style.display = e.target.value==='custom'?'flex':'none';
  updateCmd();
});
document.getElementById('jform').addEventListener('input', updateCmd);
document.getElementById('jform').addEventListener('change', updateCmd);

// ── Command builder ──
function qarg(s){ return /[ "\\]/.test(String(s)) ? '"'+String(s)+'"' : String(s); }

function getChemParams() {
  const chemKey = document.getElementById('chemPreset').value;
  const p = CHEM[chemKey];
  return p
    ? {soloCBlen:p[0],soloUMIlen:p[1],soloCBstart:p[2],soloUMIstart:p[3],soloBarcodeReadLength:p[4]}
    : {soloCBlen:    parseInt(document.getElementById('soloCBlen').value),
       soloUMIlen:   parseInt(document.getElementById('soloUMIlen').value),
       soloCBstart:  parseInt(document.getElementById('soloCBstart').value),
       soloUMIstart: parseInt(document.getElementById('soloUMIstart').value),
       soloBarcodeReadLength: document.getElementById('disableStrictLen').checked?0:1};
}
)HTML"
R"HTML(

function buildCmd() {
  const args = [];
  const flag = (k,...vs) => { args.push('--'+k); vs.forEach(v=>args.push(qarg(v))); };

  const runMode   = document.getElementById('runMode').value;
  const genomeDir = document.getElementById('genomeDir').value.trim();
  const outPrefix = document.getElementById('outPrefix').value.trim();
  const threads   = parseInt(document.getElementById('threads').value)||4;

  flag('runMode', runMode);
  if (genomeDir) flag('genomeDir', genomeDir);
  if (outPrefix) {
    const p = (outPrefix.endsWith('/')||outPrefix.endsWith('\\')) ? outPrefix : outPrefix+'/';
    flag('outFileNamePrefix', p);
  }
  flag('runThreadN', threads);

  if (runMode === 'genomeGenerate') {
    const fasta = document.getElementById('genomeFastaFiles').value.trim();
    if (fasta) { args.push('--genomeFastaFiles'); fasta.split(/\s+/).filter(Boolean).forEach(f=>args.push(qarg(f))); }
    const saIdx = parseInt(document.getElementById('genomeSAindexNbases').value)||14;
    if (saIdx !== 14) flag('genomeSAindexNbases', saIdx);
    const overhang = parseInt(document.getElementById('sjdbOverhang').value)||100;
    if (overhang !== 100) flag('sjdbOverhang', overhang);
  }

  const r1 = document.getElementById('r1').value.trim();
  const r2 = document.getElementById('r2').value.trim();
  if (r1) {
    const files = [...r1.split(/\s+/), ...(r2?r2.split(/\s+/):[])].filter(Boolean);
    args.push('--readFilesIn'); files.forEach(f=>args.push(qarg(f)));
  }

  const rfcSel = document.getElementById('rfc').value;
  const rfc = rfcSel==='custom' ? document.getElementById('rfcCustomVal').value.trim() : rfcSel;
  if (rfc) flag('readFilesCommand', rfc);

  const samtype = document.getElementById('samtype').value;
  args.push('--outSAMtype'); samtype.split(' ').forEach(t=>args.push(t));

  const soloType = document.getElementById('soloType').value;
  if (soloType) {
    flag('soloType', soloType);

    const cp = getChemParams();
    flag('soloCBlen',             cp.soloCBlen);
    flag('soloUMIlen',            cp.soloUMIlen);
    flag('soloCBstart',           cp.soloCBstart);
    flag('soloUMIstart',          cp.soloUMIstart);
    flag('soloBarcodeReadLength', cp.soloBarcodeReadLength);

    const wl = document.getElementById('whitelist').value.trim();
    if (wl) flag('soloCBwhitelist', wl);

    const clip = document.getElementById('clipAdapterType').value;
    if (clip) flag('clipAdapterType', clip);

    const soloStrand = document.getElementById('soloStrand').value;
    if (soloStrand !== 'Forward') flag('soloStrand', soloStrand);
    const soloUMIdedup = document.getElementById('soloUMIdedup').value;
    if (soloUMIdedup !== '1MM_All') flag('soloUMIdedup', soloUMIdedup);

    const feats = [...document.querySelectorAll('.sfeat:checked')].map(c=>c.value);
    if (feats.length){ args.push('--soloFeatures'); feats.forEach(f=>args.push(f)); }

    flag('soloMultiMappers', document.getElementById('soloMM').value);

    const cfSel = document.getElementById('cellFilterSel').value;
    const cf = cfSel==='custom' ? document.getElementById('cfCustomVal').value.trim() : cfSel;
    if (cf){ args.push('--soloCellFilter'); cf.split(/\s+/).filter(Boolean).forEach(t=>args.push(t)); }
  }

  if (runMode === 'alignReads') {
    const twopass = document.getElementById('twopassMode').value;
    if (twopass !== 'None') flag('twopassMode', twopass);
    const quants = [...document.querySelectorAll('.qmode:checked')].map(c=>c.value);
    if (quants.length) { args.push('--quantMode'); quants.forEach(q=>args.push(q)); }
    const samStrand = document.getElementById('outSAMstrandField').value;
    if (samStrand !== 'None') flag('outSAMstrandField', samStrand);
  }

  const multimapNmax = parseInt(document.getElementById('multimapNmax').value)||10;
  if (multimapNmax !== 10) flag('outFilterMultimapNmax', multimapNmax);
  const limitRAM = parseInt(document.getElementById('limitBAMsortRAM').value)||0;
  if (limitRAM > 0) flag('limitBAMsortRAM', limitRAM);

  const extraArgs = document.getElementById('extraArgs').value.trim();
  if (extraArgs) extraArgs.split(/\s+/).forEach(t => args.push(t));

  const gtf = document.getElementById('gtf').value.trim();
  if (gtf) flag('sjdbGTFfile', gtf);

  const lines = ['STAR'];
  for (let i=0; i<args.length; i++) {
    if (args[i].startsWith('--')) {
      let line = '  '+args[i];
      while (i+1 < args.length && !args[i+1].startsWith('--')) line += ' '+args[++i];
      lines.push(line);
    }
  }
  return lines.join(' \\\n');
}

function updateCmd() {
  document.getElementById('cmd-box').textContent = buildCmd();
}

function copyCmd() {
  const text = buildCmd().replace(/ \\\n\s+/g, ' ');
  navigator.clipboard.writeText(text).catch(()=>{
    const ta=document.createElement('textarea');
    ta.value=text; document.body.appendChild(ta); ta.select();
    document.execCommand('copy'); document.body.removeChild(ta);
  });
}

// ── Form submit ──
document.getElementById('jform').addEventListener('submit', async e=>{
  e.preventDefault();
  document.getElementById('ferr').textContent = '';

  const r1 = document.getElementById('r1').value.trim();
  const r2 = document.getElementById('r2').value.trim();
  const readFilesIn = r1 ? [...r1.split(/\s+/), ...(r2?r2.split(/\s+/):[])].filter(Boolean) : [];

  const rfcSel = document.getElementById('rfc').value;
  const readFilesCommand = rfcSel==='custom' ? document.getElementById('rfcCustomVal').value.trim() : rfcSel;

  const cfSel = document.getElementById('cellFilterSel').value;
  const soloCellFilter = cfSel==='custom' ? document.getElementById('cfCustomVal').value.trim() : cfSel;

  const soloType = document.getElementById('soloType').value;
  const chemParams = soloType ? getChemParams() : {};

  const feats = [...document.querySelectorAll('.sfeat:checked')].map(c=>c.value);
  const quantParts = [...document.querySelectorAll('.qmode:checked')].map(c=>c.value);
  const runMode2 = document.getElementById('runMode').value;

  const body = {
    runMode:                runMode2,
    genomeDir:              document.getElementById('genomeDir').value.trim(),
    outFileNamePrefix:      document.getElementById('outPrefix').value.trim(),
    runThreadN:             parseInt(document.getElementById('threads').value)||4,
    outSAMtype:             document.getElementById('samtype').value,
    readFilesCommand,
    soloType,
    soloCBwhitelist:        document.getElementById('whitelist').value.trim(),
    soloFeatures:           feats,
    soloMultiMappers:       document.getElementById('soloMM').value,
    soloCellFilter,
    clipAdapterType:        document.getElementById('clipAdapterType').value,
    soloStrand:             soloType ? document.getElementById('soloStrand').value : '',
    soloUMIdedup:           soloType ? document.getElementById('soloUMIdedup').value : '',
    sjdbGTFfile:            document.getElementById('gtf').value.trim(),
    twopassMode:            runMode2==='alignReads' ? document.getElementById('twopassMode').value : '',
    quantMode:              quantParts.join(' '),
    outSAMstrandField:      runMode2==='alignReads' ? document.getElementById('outSAMstrandField').value : '',
    outFilterMultimapNmax:  parseInt(document.getElementById('multimapNmax').value)||10,
    limitBAMsortRAM:        parseInt(document.getElementById('limitBAMsortRAM').value)||0,
    genomeFastaFiles:       runMode2==='genomeGenerate' ? document.getElementById('genomeFastaFiles').value.trim().split(/\s+/).filter(Boolean) : [],
    genomeSAindexNbases:    runMode2==='genomeGenerate' ? parseInt(document.getElementById('genomeSAindexNbases').value)||14 : 14,
    sjdbOverhang:           runMode2==='genomeGenerate' ? parseInt(document.getElementById('sjdbOverhang').value)||100 : 100,
    generateReport:         document.getElementById('generateReport').checked,
    extraArgs:              document.getElementById('extraArgs').value.trim(),
    ...chemParams,
  };
  if (readFilesIn.length) body.readFilesIn = readFilesIn;

  const res = await fetch('/jobs',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)});
  const data = await res.json();
  if (!res.ok){ document.getElementById('ferr').textContent = data.error||'Submission failed'; return; }
  refreshJobs();
});

// ── Jobs table ──
function badge(s){ return `<span class="badge s-${s}">${s}</span>`; }
function dur(start,end){
  if (!start) return '';
  const s=new Date(start), e=end?new Date(end):new Date();
  let sec=Math.max(0,Math.floor((e-s)/1000));
  if (sec<60) return sec+'s';
  if (sec<3600) return Math.floor(sec/60)+'m '+('0'+sec%60).slice(-2)+'s';
  return Math.floor(sec/3600)+'h '+Math.floor((sec%3600)/60)+'m';
}
async function refreshJobs(){
  const jobs = await fetch('/jobs').then(r=>r.json());
  document.getElementById('nojobs').style.display = jobs.length?'none':'block';
  document.getElementById('jtbody').innerHTML = jobs.slice().reverse().map(j=>`<tr>
    <td>${j.id}</td><td>${badge(j.state)}</td><td>${j.runMode}</td>
    <td style="max-width:260px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;font-size:.8rem" title="${j.outputPrefix}">${j.outputPrefix}</td>
    <td style="font-size:.8rem">${j.submitTime}</td>
    <td style="font-size:.8rem">${dur(j.startTime,j.state==='running'?null:j.endTime)}</td>
    <td style="white-space:nowrap">
      <button class="btn-log" onclick="viewLogs(${j.id})">Logs</button>
      ${j.reportPath?`<button class="btn-log" style="margin-left:4px" onclick="window.open('/jobs/${j.id}/report','_blank')">Report</button>`:''}
      ${j.state==='running'?`<button class="btn-cancel" style="margin-left:4px" onclick="cancelJob(${j.id})">Cancel</button>`:''}
    </td>
  </tr>`).join('');
  if (watchId!==null) { const j=jobs.find(x=>x.id===watchId); if(j) fetchLogs(watchId); }
}
async function viewLogs(id){
  watchId=id;
  document.getElementById('log-jid').textContent=id;
  document.getElementById('log-box').style.display='block';
  await fetchLogs(id);
  document.getElementById('log-box').scrollIntoView({behavior:'smooth'});
}
async function fetchLogs(id){
  const data=await fetch('/jobs/'+id+'/logs').then(r=>r.json());
  const el=document.getElementById('log-pre');
  el.textContent=data.logs||(data.error?'Error: '+data.error:'(no logs yet)');
  el.scrollTop=el.scrollHeight;
}
function closeLogs(){ watchId=null; document.getElementById('log-box').style.display='none'; }
async function cancelJob(id){
  await fetch('/jobs/'+id+'/cancel',{method:'POST'});
  refreshJobs();
}

// Init
onRunModeChange();
updateCmd();
refreshJobs();
setInterval(refreshJobs, 3000);
</script>
</body>
</html>
)HTML"
