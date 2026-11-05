R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<meta name="color-scheme" content="light dark">
<title>STAR-Cross WebUI</title>
<style>
:root{
  --bg:#f5f6f8;--surface:#fff;--surface-2:#f0f2f5;--border:#dde1e7;--border-strong:#c3c9d2;
  --text:#1c222b;--muted:#596270;--accent:#0b6b68;--accent-hover:#08524f;--accent-soft:#e2f1f0;--on-accent:#fff;
  --ok:#1b7339;--ok-bg:#e5f3e9;--err:#b3261e;--err-bg:#fcebea;--warn:#8a4600;--warn-bg:#fdf0dc;
  --info:#1d5ba3;--info-bg:#e7effa;--neutral:#4a5260;--neutral-bg:#eceef2;
  --term-bg:#11161c;--term-text:#d3dae3;
  --shadow:0 1px 2px rgba(16,24,40,.06);
  --sans:system-ui,-apple-system,"Segoe UI",Roboto,"Helvetica Neue",sans-serif;
  --mono:ui-monospace,SFMono-Regular,"Cascadia Mono",Menlo,Consolas,monospace;
}
@media (prefers-color-scheme:dark){:root{
  --bg:#0e1115;--surface:#161a20;--surface-2:#1d222a;--border:#2a3039;--border-strong:#3a424e;
  --text:#e4e8ee;--muted:#9ba5b3;--accent:#52c2bb;--accent-hover:#76d3cd;--accent-soft:#14302f;--on-accent:#04201f;
  --ok:#7fd396;--ok-bg:#132a1b;--err:#ff8f86;--err-bg:#33171a;--warn:#f0b866;--warn-bg:#2f2312;
  --info:#8bb8f2;--info-bg:#152338;--neutral:#b4bcc8;--neutral-bg:#242a33;
  --term-bg:#0a0d11;--shadow:none;
}}
*,*::before,*::after{box-sizing:border-box}
*{margin:0}
[hidden]{display:none!important}
html{-webkit-text-size-adjust:100%}
body{font:14px/1.45 var(--sans);background:var(--bg);color:var(--text);-webkit-font-smoothing:antialiased}
code,pre{font-family:var(--mono)}
a{color:var(--accent)}
:focus-visible{outline:2px solid var(--accent);outline-offset:2px}
.skip{position:absolute;left:-999px;top:8px;background:var(--surface);color:var(--text);padding:8px 12px;border-radius:6px;z-index:10}
.skip:focus{left:8px}
.sr{position:absolute;width:1px;height:1px;overflow:hidden;clip:rect(0 0 0 0);white-space:nowrap}

/* header */
.top{display:flex;align-items:center;gap:12px;padding:12px 24px;background:var(--surface);border-bottom:1px solid var(--border)}
.brand{display:flex;align-items:baseline;gap:8px;font-size:1.05rem;font-weight:650;letter-spacing:-.01em}
.brand span{font-weight:500;color:var(--muted)}
.ver{font:12px var(--mono);color:var(--muted);background:var(--surface-2);border:1px solid var(--border);border-radius:999px;padding:2px 8px}
.conn{margin-left:auto;display:flex;align-items:center;gap:6px;font-size:.8rem;color:var(--muted)}
.conn::before{content:"";width:8px;height:8px;border-radius:50%;background:var(--ok)}
.conn.down{color:var(--err)}
.conn.down::before{background:var(--err)}

/* layout */
.layout{max-width:1240px;margin:0 auto;padding:20px 24px 40px;display:grid;grid-template-columns:minmax(0,1fr) 380px;gap:20px;align-items:start}
.span-all{grid-column:1/-1}
.panel{background:var(--surface);border:1px solid var(--border);border-radius:10px;box-shadow:var(--shadow);min-width:0}
.panel-head{display:flex;align-items:center;justify-content:space-between;gap:12px;padding:14px 18px;border-bottom:1px solid var(--border)}
.panel-head h2{font-size:.95rem;font-weight:650}
.panel-body{padding:16px 18px}
.side{position:sticky;top:16px;display:flex;flex-direction:column;gap:16px;min-width:0}
.banner{background:var(--warn-bg);color:var(--warn);border:1px solid currentColor;border-radius:8px;padding:10px 14px;font-size:.88rem}

/* form */
.section{border:0;padding:16px 18px;border-top:1px solid var(--border);min-width:0}
.panel-head+.section{border-top:0}
.section>legend{float:left;width:100%;padding:0;margin-bottom:12px;font-size:.78rem;font-weight:650;letter-spacing:.04em;text-transform:uppercase;color:var(--muted)}
.section>legend+*{clear:both}
.row{display:flex;flex-wrap:wrap;gap:12px 14px}
.row+.row{margin-top:12px}
.field{display:flex;flex-direction:column;gap:5px;flex:1 1 240px;min-width:0}
.field.xs{flex:0 1 96px}.field.s{flex:0 1 170px}.field.m{flex:0 1 250px}.field.l{flex:1 1 320px}
.field label,.field .lbl{font-size:.82rem;font-weight:550;color:var(--text)}
.field label code,.field .lbl code{font-size:.72rem;font-weight:400;color:var(--muted);margin-left:4px;white-space:nowrap}
.hint{font-weight:400;color:var(--muted)}
.lblrow{display:flex;align-items:center;gap:6px;min-height:24px}
input[type=text],input[type=number],select{width:100%;height:36px;padding:0 10px;font:inherit;font-size:.9rem;color:var(--text);background:var(--surface);border:1px solid var(--border-strong);border-radius:6px}
input[type=text],input[type=number]{font-family:var(--mono);font-size:.84rem}
input::placeholder{color:var(--muted);opacity:.75}
input:hover,select:hover{border-color:var(--muted)}
input[type=checkbox]{width:16px;height:16px;accent-color:var(--accent)}
.checks{display:flex;flex-wrap:wrap;gap:4px 14px;min-height:36px;align-items:center}
.checks label{display:flex;align-items:center;gap:6px;font-size:.86rem;font-weight:400;cursor:pointer;min-height:28px}
.help-btn{position:relative;width:24px;height:24px;margin:-4px 0;border:0;border-radius:50%;background:none;color:var(--muted);font:600 .7rem var(--sans);cursor:pointer;flex:none;opacity:.7}
.help-btn::before{content:"";position:absolute;inset:5px;border:1px solid currentColor;border-radius:50%}
.field:hover .help-btn,.help-btn:focus-visible{opacity:1}
.help-btn:hover{color:var(--accent);opacity:1}
.help-btn[aria-expanded="true"]{color:var(--on-accent);opacity:1}
.help-btn[aria-expanded="true"]::before{background:var(--accent);border-color:var(--accent);z-index:-1}
.lblrow{isolation:isolate}
.help{font-size:.8rem;line-height:1.5;color:var(--text);background:var(--accent-soft);border-radius:6px;padding:8px 10px}
.probe{font-size:.8rem;min-height:1.2em;display:flex;flex-wrap:wrap;align-items:center;gap:6px}
.t-ok{color:var(--ok)}.t-warn{color:var(--warn)}.t-info{color:var(--info)}.t-err{color:var(--err)}.t-muted{color:var(--muted)}
details.adv{border-top:1px solid var(--border)}
details.adv>summary{list-style:none;cursor:pointer;display:flex;align-items:center;gap:10px;padding:14px 18px;font-size:.78rem;font-weight:650;letter-spacing:.04em;text-transform:uppercase;color:var(--muted)}
details.adv>summary::-webkit-details-marker{display:none}
details.adv>summary::before{content:"";width:7px;height:7px;border-right:2px solid currentColor;border-bottom:2px solid currentColor;transform:rotate(-45deg)}
details.adv[open]>summary::before{transform:rotate(45deg)}
details.adv>summary:hover{color:var(--text)}
details.adv>.adv-body{padding:0 18px 16px}
@media (prefers-reduced-motion:no-preference){details.adv>summary::before{transition:transform .15s}}
)HTML"
R"HTML(
/* buttons */
.btn{display:inline-flex;align-items:center;justify-content:center;gap:6px;min-height:32px;padding:0 12px;font:500 .84rem var(--sans);color:var(--text);background:var(--surface);border:1px solid var(--border-strong);border-radius:6px;cursor:pointer;text-decoration:none;white-space:nowrap}
.btn:hover{background:var(--surface-2)}
.btn-primary{min-height:40px;padding:0 18px;font-weight:600;font-size:.92rem;color:var(--on-accent);background:var(--accent);border-color:var(--accent)}
.btn-primary:hover{background:var(--accent-hover);border-color:var(--accent-hover)}
.btn-primary:disabled{opacity:.6;cursor:progress}
.btn-danger{color:var(--err);border-color:currentColor}
.btn-danger:hover{background:var(--err-bg)}
.btn-sm{min-height:28px;padding:0 10px;font-size:.8rem}

/* command preview */
.cmd{margin:0;padding:12px 14px;background:var(--surface-2);border:1px solid var(--border);border-radius:8px;font-size:.78rem;line-height:1.7;white-space:pre-wrap;word-break:break-all;max-height:min(46vh,420px);overflow:auto;color:var(--text)}
.cmd .flag{color:var(--accent);font-weight:600}
.submit-row{display:flex;flex-direction:column;gap:10px;margin-top:14px}
.submit-row label{display:flex;align-items:center;gap:8px;font-size:.86rem;cursor:pointer}
.err{color:var(--err);font-size:.85rem}
.err:empty{display:none}
.note{font-size:.8rem;color:var(--muted)}

/* jobs */
.jobs-meta{font-size:.8rem;color:var(--muted)}
table{width:100%;border-collapse:collapse;font-size:.86rem}
th{text-align:left;font-size:.74rem;font-weight:600;letter-spacing:.03em;text-transform:uppercase;color:var(--muted);padding:10px 14px;border-bottom:1px solid var(--border);white-space:nowrap}
td{padding:10px 14px;border-bottom:1px solid var(--border);vertical-align:top}
tbody tr:last-child td{border-bottom:0}
tbody tr:hover td{background:var(--surface-2)}
.num{font-variant-numeric:tabular-nums;white-space:nowrap}
.path{font-family:var(--mono);font-size:.8rem;max-width:340px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.acts{display:flex;gap:6px;justify-content:flex-end}
.badge{display:inline-flex;align-items:center;gap:6px;padding:2px 9px 2px 8px;border-radius:999px;font-size:.76rem;font-weight:600;white-space:nowrap}
.badge::before{content:"";width:6px;height:6px;border-radius:50%;background:currentColor}
.s-queued{background:var(--warn-bg);color:var(--warn)}
.s-running{background:var(--info-bg);color:var(--info)}
.s-succeeded{background:var(--ok-bg);color:var(--ok)}
.s-failed{background:var(--err-bg);color:var(--err)}
.s-cancelled{background:var(--neutral-bg);color:var(--neutral)}
@media (prefers-reduced-motion:no-preference){.s-running::before{animation:pulse 1.4s ease-in-out infinite}}
@keyframes pulse{50%{opacity:.25}}
.why{margin-top:6px;font-size:.8rem;color:var(--err);max-width:280px}
.why summary{cursor:pointer;color:var(--muted);font-size:.76rem;margin-top:2px}
.why pre{margin-top:4px;padding:6px 8px;font-size:.72rem;white-space:pre-wrap;word-break:break-word;background:var(--surface-2);border-radius:4px;color:var(--text)}
.empty{padding:28px 18px;text-align:center;color:var(--muted);font-size:.88rem}

/* log */
.log{margin:0;padding:14px 16px;background:var(--term-bg);color:var(--term-text);font-size:.78rem;line-height:1.55;white-space:pre-wrap;word-break:break-word;max-height:460px;overflow:auto;border-radius:0 0 10px 10px}

@media (max-width:980px){
  .layout{grid-template-columns:minmax(0,1fr)}
  .side{position:static}
}
@media (max-width:640px){
  .top{padding:10px 16px;flex-wrap:wrap}
  .layout{padding:16px 12px 32px;gap:16px}
  .section,.panel-body,.panel-head,details.adv>summary{padding-left:14px;padding-right:14px}
  details.adv>.adv-body{padding:0 14px 14px}
  .field,.field.s,.field.m,.field.l{flex:1 1 100%}
  .field.xs{flex:1 1 calc(50% - 7px)}
  input[type=text],input[type=number],select{font-size:16px;height:40px}
  table,thead,tbody,tr,td{display:block}
  thead{position:absolute;width:1px;height:1px;overflow:hidden;clip:rect(0 0 0 0)}
  tbody tr{display:grid;grid-template-columns:auto minmax(0,1fr);gap:6px 12px;padding:12px 14px;border-bottom:1px solid var(--border)}
  tbody tr:last-child{border-bottom:0}
  td{padding:0;border:0;display:contents}
  td::before{content:attr(data-label);font-size:.72rem;text-transform:uppercase;letter-spacing:.03em;color:var(--muted);padding-top:2px}
  td.c-acts::before{content:none}
  td.c-acts .acts{grid-column:1/-1;justify-content:flex-start;margin-top:6px}
  tbody tr:hover td{background:none}
  .path{max-width:none;white-space:normal;word-break:break-all}
}
</style>
</head>
)HTML"
R"HTML(
<body>
<a class="skip" href="#jobs">Skip to jobs</a>
<header class="top">
  <h1 class="brand">STAR-Cross <span>WebUI</span></h1>
  <span id="ver" class="ver" hidden></span>
  <span id="connPill" class="conn">Connected</span>
</header>
<main class="layout">
<div id="conn" class="banner span-all" role="status" hidden></div>

<form id="jform" class="panel" aria-labelledby="form-title">
  <div class="panel-head"><h2 id="form-title">New job</h2></div>

  <fieldset class="section">
    <legend>Run</legend>
    <div class="row">
      <div class="field m" data-tip="alignReads: map reads to a genome index. genomeGenerate: build a genome index (do this first). soloCellFiltering: re-filter cell barcodes from existing STARsolo output.">
        <label for="runMode">Run mode <code>--runMode</code></label>
        <select id="runMode">
          <option value="alignReads">alignReads</option>
          <option value="genomeGenerate">genomeGenerate</option>
          <option value="soloCellFiltering">soloCellFiltering</option>
        </select>
      </div>
      <div class="field l" data-tip="Path to the genome index directory. Built with --runMode genomeGenerate. Must contain SA, Genome, genomeParameters.txt and other index files.">
        <label for="genomeDir">Genome directory <code>--genomeDir</code></label>
        <input id="genomeDir" type="text" placeholder="/path/to/genome/index" autocomplete="off" spellcheck="false">
        <div id="genomeDirStatus" class="probe" aria-live="polite"></div>
      </div>
      <div class="field xs" data-tip="Number of parallel CPU threads. Set to the number of physical cores for best performance. More threads = faster but proportionally more RAM.">
        <label for="threads">Threads</label>
        <input id="threads" type="number" value="4" min="1" inputmode="numeric">
      </div>
    </div>
    <div class="row" id="genGenRow" hidden>
      <div class="field l" data-tip="Reference genome FASTA file(s). Required for genomeGenerate. For human or mouse download the primary assembly FASTA from Ensembl or NCBI. Space-separate multiple files (e.g. per-chromosome).">
        <label for="genomeFastaFiles">Genome FASTA <span class="hint">(required; space-separated)</span></label>
        <input id="genomeFastaFiles" type="text" placeholder="/data/GRCh38.fa" autocomplete="off" spellcheck="false">
      </div>
      <div class="field s" data-tip="Suffix array pre-index string length. Default 14 works for genomes >= 500 MB. For smaller genomes scale down: min(14, floor(log2(genomeSize)/2 - 1)). Example: 100 MB genome needs value 10.">
        <label for="genomeSAindexNbases">SAindexNbases <span class="hint">(4-14)</span></label>
        <input id="genomeSAindexNbases" type="number" value="14" min="4" max="14" inputmode="numeric">
      </div>
      <div class="field s" data-tip="Splice junction database overhang. Optimal value is readLength - 1. Default 100 works for most experiments. Set during genome generation; affects novel junction detection sensitivity.">
        <label for="sjdbOverhang">sjdbOverhang</label>
        <input id="sjdbOverhang" type="number" value="100" min="1" inputmode="numeric">
      </div>
    </div>
  </fieldset>

  <fieldset class="section">
    <legend>Reads</legend>
    <div class="row">
      <div class="field l" data-tip="Read 1 FASTQ file(s). For 10x scRNA-seq: the short barcode+UMI read (26-28 bp). For standard RNA-seq: the forward read. Separate multiple lanes with spaces. Compression setting below must match.">
        <label for="r1">Read 1 <span class="hint">(space-separated lanes)</span></label>
        <input id="r1" type="text" placeholder="/data/R1.fastq.gz" autocomplete="off" spellcheck="false">
      </div>
      <div class="field l" data-tip="Read 2 FASTQ file(s). For 10x scRNA-seq: the cDNA read (longer). For standard RNA-seq: the reverse read. Leave blank for single-end sequencing.">
        <label for="r2">Read 2 <span class="hint">(blank = single-end)</span></label>
        <input id="r2" type="text" placeholder="/data/R2.fastq.gz" autocomplete="off" spellcheck="false">
      </div>
    </div>
    <div class="row">
      <div class="field m" data-tip="Decompression command for compressed input reads. Must match the file extension: zcat for .gz (most common), bzcat for .bz2, zstdcat for .zst. Leave Uncompressed for plain FASTQ.">
        <label for="rfc">Compression <code>--readFilesCommand</code></label>
        <select id="rfc">
          <option value="">Uncompressed</option>
          <option value="zcat" selected>gzip (.gz) - zcat</option>
          <option value="bzcat">bzip2 (.bz2) - bzcat</option>
          <option value="zstdcat">zstd (.zst) - zstdcat</option>
          <option value="custom">Custom&hellip;</option>
        </select>
      </div>
      <div class="field m" id="rfcCustomWrap" hidden>
        <label for="rfcCustomVal">Custom command</label>
        <input id="rfcCustomVal" type="text" placeholder="pigz -dc" autocomplete="off" spellcheck="false">
      </div>
    </div>
  </fieldset>

  <fieldset class="section">
    <legend>Output</legend>
    <div class="row">
      <div class="field l" data-tip="Prefix for all output files. The directory must already exist. Trailing slash = directory (e.g. /out/ makes /out/Aligned.out.bam). Non-slash suffix adds a name tag (e.g. /out/s1_ makes /out/s1_Aligned.out.bam).">
        <label for="outPrefix">Output prefix <span class="hint">(directory must exist)</span></label>
        <input id="outPrefix" type="text" placeholder="/data/output/sample_" autocomplete="off" spellcheck="false">
      </div>
      <div class="field m" data-tip="Output alignment format. BAM SortedByCoordinate: required for IGV, featureCounts, HTSeq. BAM Unsorted: faster, use with --quantMode TranscriptomeSAM for RSEM. CRAM: smaller referenceless CRAM (no reference FASTA needed). None: skip alignment output for count-only runs.">
        <label for="samtype">Format <code>--outSAMtype</code></label>
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
)HTML"
R"HTML(
    <div class="row" id="alignRow" hidden>
      <div class="field m" data-tip="2-pass mapping: first pass discovers novel splice junctions, second pass maps with them included. Recommended for improved sensitivity and accuracy. Approximately doubles runtime.">
        <label for="twopassMode">Two-pass <code>--twopassMode</code></label>
        <select id="twopassMode">
          <option value="None">None (1-pass)</option>
          <option value="Basic">Basic (2-pass, recommended)</option>
        </select>
      </div>
      <div class="field" data-tip="TranscriptomeSAM: output a transcriptome-aligned BAM for RSEM or salmon. GeneCounts: output per-gene read count table (like featureCounts). Can enable both simultaneously.">
        <span class="lbl" id="grp-qmode">Quantification <code>--quantMode</code></span>
        <div class="checks" role="group" aria-labelledby="grp-qmode">
          <label><input type="checkbox" class="qmode" value="TranscriptomeSAM"> TranscriptomeSAM</label>
          <label><input type="checkbox" class="qmode" value="GeneCounts"> GeneCounts</label>
        </div>
      </div>
      <div class="field m" data-tip="intronMotif: adds XS:A strand tag required by Cufflinks, StringTie, and DEXSeq for unstranded libraries. Not needed for dUTP-based strand-specific protocols which use the flag bits instead.">
        <label for="outSAMstrandField">Strand field <code>--outSAMstrandField</code></label>
        <select id="outSAMstrandField">
          <option value="None">None</option>
          <option value="intronMotif">intronMotif (Cufflinks/StringTie)</option>
        </select>
      </div>
    </div>
    <div class="row">
      <div class="field l" data-tip="Gene annotation in GTF/GFF format. Optional if already baked into the genome index. Required for --quantMode GeneCounts and for STARsolo gene-level output. Download from Ensembl or GENCODE.">
        <label for="gtf">Annotation GTF <code>--sjdbGTFfile</code> <span class="hint">(optional if in the index)</span></label>
        <input id="gtf" type="text" placeholder="/data/genes.gtf" autocomplete="off" spellcheck="false">
      </div>
    </div>
  </fieldset>

  <fieldset class="section">
    <legend>STARsolo</legend>
    <div class="row">
      <div class="field m" data-tip="CB_UMI_Simple: fixed CB+UMI structure (10x Chromium, DropSeq, SeqWell). CB_UMI_Complex: tiled or variable barcode layout (inDrops, custom). SmartSeq: plate-based full-length RNA-seq (no CB/UMI). None: plain alignReads without cell demultiplexing.">
        <label for="soloType">Solo type <code>--soloType</code></label>
        <select id="soloType">
          <option value="">None (plain alignReads)</option>
          <option value="CB_UMI_Simple">CB_UMI_Simple (10x, DropSeq&hellip;)</option>
          <option value="CB_UMI_Complex">CB_UMI_Complex (inDrops&hellip;)</option>
          <option value="SmartSeq">SmartSeq</option>
        </select>
      </div>
      <div class="field m" id="chemWrap" hidden data-tip="Pre-configured CB+UMI lengths for common platforms. 10x v3/v4: CB16+UMI12 (28 bp R1). 10x v2: CB16+UMI10 (26 bp R1). DropSeq: CB12+UMI8. SeqWell: CB8+UMI8. Custom: enter lengths manually.">
        <label for="chemPreset">Chemistry</label>
        <select id="chemPreset">
          <option value="10xv3">10x Chromium v3 / v4 (CB16 UMI12)</option>
          <option value="10xv2">10x Chromium v2 (CB16 UMI10)</option>
          <option value="dropseq">DropSeq (CB12 UMI8)</option>
          <option value="seqwell">SeqWell s3 (CB8 UMI8)</option>
          <option value="custom">Custom&hellip;</option>
        </select>
      </div>
      <div class="field l" id="whitelistWrap" hidden data-tip="Path to list of valid cell barcodes. For 10x v3/v4: 3M-february-2018.txt.gz. For 10x v2: 737K-august-2016.txt. Download from the Cell Ranger reference data package. Use - to auto-detect (not recommended).">
        <label for="whitelist">Barcode whitelist <code>--soloCBwhitelist</code></label>
        <input id="whitelist" type="text" placeholder="/data/3M-february-2018.txt" autocomplete="off" spellcheck="false">
      </div>
    </div>
    <div class="row" id="chemCustom" hidden>
      <div class="field xs"><label for="soloCBlen">CB length</label><input id="soloCBlen" type="number" value="16" min="1" inputmode="numeric"></div>
      <div class="field xs"><label for="soloUMIlen">UMI length</label><input id="soloUMIlen" type="number" value="12" min="1" inputmode="numeric"></div>
      <div class="field xs"><label for="soloCBstart">CB start</label><input id="soloCBstart" type="number" value="1" min="1" inputmode="numeric"></div>
      <div class="field xs"><label for="soloUMIstart">UMI start</label><input id="soloUMIstart" type="number" value="17" min="1" inputmode="numeric"></div>
      <div class="field m">
        <span class="lbl" aria-hidden="true">&nbsp;</span>
        <div class="checks"><label><input type="checkbox" id="disableStrictLen" checked> Disable strict barcode length check</label></div>
      </div>
    </div>
    <div class="row" id="soloOptRow" hidden>
      <div class="field m" id="clipWrap" data-tip="CellRanger4: trims TSO and polyA sequences from cDNA reads. Required for 10x v3/v4 libraries to avoid miscounting reads at transcript ends. AdapterTrimST: for STARsolo-specific adapter trimming.">
        <label for="clipAdapterType">Adapter clipping <code>--clipAdapterType</code></label>
        <select id="clipAdapterType">
          <option value="">None</option>
          <option value="CellRanger4">CellRanger4 (10x v3/v4)</option>
          <option value="AdapterTrimST">AdapterTrimST</option>
        </select>
      </div>
      <div class="field s" id="soloStrandWrap" data-tip="Library strand orientation relative to the mRNA. Forward: most 10x Chromium libraries. Reverse: some library kits. Unstranded: non-stranded protocols. Incorrect setting causes major expression miscounting.">
        <label for="soloStrand">Strand <code>--soloStrand</code></label>
        <select id="soloStrand">
          <option value="Forward">Forward (default)</option>
          <option value="Reverse">Reverse</option>
          <option value="Unstranded">Unstranded</option>
        </select>
      </div>
      <div class="field m" id="soloUMIdedupWrap" data-tip="UMI deduplication strategy. 1MM_All: merge UMIs within 1 Hamming distance (default, most permissive). 1MM_Directional: directional error correction (recommended for high-depth data). Exact: no correction. NoDedup: count all reads.">
        <label for="soloUMIdedup">UMI dedup <code>--soloUMIdedup</code></label>
        <select id="soloUMIdedup">
          <option value="1MM_All">1MM_All (default)</option>
          <option value="1MM_Directional">1MM_Directional</option>
          <option value="Exact">Exact</option>
          <option value="NoDedup">NoDedup</option>
        </select>
      </div>
    </div>
)HTML"
R"HTML(
    <div class="row" id="soloFeatRow" hidden>
      <div class="field l" data-tip="Gene: counts reads overlapping exons (spliced RNA). GeneFull: counts over full gene body including introns (snRNA-seq). GeneFull_Ex50pAS: like GeneFull but requires >=50% read overlap with annotated sequence (reduces noise). SJ: splice junction counts. Velocyto: spliced/unspliced/ambiguous counts for RNA velocity.">
        <span class="lbl" id="grp-sfeat">Features <code>--soloFeatures</code></span>
        <div class="checks" role="group" aria-labelledby="grp-sfeat">
          <label><input type="checkbox" class="sfeat" value="Gene" checked> Gene</label>
          <label><input type="checkbox" class="sfeat" value="GeneFull"> GeneFull</label>
          <label><input type="checkbox" class="sfeat" value="GeneFull_Ex50pAS"> GeneFull_Ex50pAS</label>
          <label><input type="checkbox" class="sfeat" value="SJ"> SJ</label>
          <label><input type="checkbox" class="sfeat" value="Velocyto"> Velocyto</label>
        </div>
      </div>
      <div class="field s" data-tip="How to assign reads mapping to multiple genes. Uniform: distribute equally. EM: expectation-maximization, most accurate but slower. Rescue: simple rescue heuristic. PropUnique: proportional to unique counts.">
        <label for="soloMM">Multimappers</label>
        <select id="soloMM">
          <option value="Uniform">Uniform</option>
          <option value="EM">EM</option>
          <option value="Rescue">Rescue</option>
          <option value="PropUnique">PropUnique</option>
        </select>
      </div>
      <div class="field m" data-tip="Cell filtering method. EmptyDrops_CR: statistical model distinguishing real cells from empty droplets (recommended). CellRanger2 200: knee-point method, min 200 UMIs. TopCells 500: top 500 barcodes by count. None: no filtering, output all barcodes.">
        <label for="cellFilterSel">Cell filter <code>--soloCellFilter</code></label>
        <select id="cellFilterSel">
          <option value="EmptyDrops_CR">EmptyDrops_CR (recommended)</option>
          <option value="CellRanger2 200">CellRanger2 200</option>
          <option value="TopCells 500">TopCells 500</option>
          <option value="None">None (no filtering)</option>
          <option value="custom">Custom&hellip;</option>
        </select>
      </div>
      <div class="field l" id="cfCustomWrap" hidden>
        <label for="cfCustomVal">Custom cell filter</label>
        <input id="cfCustomVal" type="text" placeholder="EmptyDrops_CR 10 45000 90 0.01 20000 0.01" autocomplete="off" spellcheck="false">
      </div>
    </div>
  </fieldset>

  <details class="adv">
    <summary>Advanced</summary>
    <div class="adv-body">
      <div class="row">
        <div class="field m" data-tip="Max number of loci a read may align to. Reads exceeding this are considered unmapped. Default 10. Set to 1 to keep only uniquely mapping reads. Increase for repetitive element analysis.">
          <label for="multimapNmax">Max loci <code>--outFilterMultimapNmax</code></label>
          <input id="multimapNmax" type="number" value="10" min="1" inputmode="numeric">
        </div>
        <div class="field m" data-tip="RAM limit for BAM coordinate sorting in bytes. 0 = auto-detect from available memory. For large genomes you may need to set this explicitly, e.g. 20000000000 for 20 GB. Increase if sorting fails with an out-of-memory error.">
          <label for="limitBAMsortRAM">Sort RAM, bytes <span class="hint">(0 = auto)</span></label>
          <input id="limitBAMsortRAM" type="number" value="0" min="0" inputmode="numeric">
        </div>
      </div>
      <div class="row">
        <div class="field" data-tip="Extra STAR arguments appended verbatim to the command line (space-separated). Examples: --legacy (use upstream 2.7.11b algorithm variants), --chimSegmentMin 20 (enable chimeric output).">
          <label for="extraArgs">Extra STAR arguments</label>
          <input id="extraArgs" type="text" placeholder="e.g. --legacy  or  --chimSegmentMin 20" autocomplete="off" spellcheck="false">
        </div>
      </div>
    </div>
  </details>
</form>

<aside class="side" aria-label="Command and submission">
  <section class="panel">
    <div class="panel-head">
      <h2 id="cmd-title">Command</h2>
      <button type="button" class="btn btn-sm" id="copyBtn">Copy</button>
    </div>
    <div class="panel-body">
      <pre id="cmd-box" class="cmd" aria-labelledby="cmd-title">STAR &hellip;</pre>
      <div class="submit-row">
        <div id="ferr" class="err" role="alert"></div>
        <button type="submit" form="jform" class="btn btn-primary" id="submitBtn">Submit job</button>
        <label><input type="checkbox" id="generateReport" checked> Write an HTML report when the job ends</label>
        <p class="note">The output directory must exist before you submit.</p>
      </div>
    </div>
  </section>
</aside>

<section class="panel span-all" id="jobs" aria-labelledby="jobs-title">
  <div class="panel-head">
    <h2 id="jobs-title">Jobs</h2>
    <span class="jobs-meta">Updates every 3 s</span>
  </div>
  <table>
    <thead><tr>
      <th scope="col">Job</th><th scope="col">Status</th><th scope="col">Mode</th>
      <th scope="col">Output</th><th scope="col">Submitted</th><th scope="col">Duration</th><th scope="col"><span class="sr">Actions</span></th>
    </tr></thead>
    <tbody id="jtbody"></tbody>
  </table>
  <p id="nojobs" class="empty">No jobs yet. Submitted jobs appear here with their logs and reports.</p>
</section>

<section class="panel span-all" id="log-box" aria-labelledby="log-title" hidden>
  <div class="panel-head">
    <h2 id="log-title">Log &middot; job <span id="log-jid"></span></h2>
    <button type="button" class="btn btn-sm" id="closeLogsBtn">Close</button>
  </div>
  <pre id="log-pre" class="log" tabindex="0" aria-label="Job log">(loading&hellip;)</pre>
</section>
</main>
)HTML"
R"HTML(
<script>
const $ = id => document.getElementById(id);
const CHEM = {
  '10xv3':   [16, 12, 1, 17, 0],
  '10xv2':   [16, 10, 1, 17, 0],
  'dropseq': [12,  8, 1, 13, 0],
  'seqwell': [ 8,  8, 1,  9, 0],
};

let watchId = null;

fetch('/props').then(r=>r.json()).then(d=>{
  const ver = $('ver');
  ver.textContent = String(d.version||'').replace(/^STAR-Cross\s+/, '') + (d.platform ? ' \u00b7 '+d.platform : '');
  ver.hidden = !ver.textContent;
  if (d.cpuCount > 0) $('threads').value = d.cpuCount;
  if (d.outDir) {
    const base = (d.outDir.endsWith('/')||d.outDir.endsWith('\\')) ? d.outDir : d.outDir+'/';
    const now  = new Date();
    const ts   = now.getFullYear().toString()
               + String(now.getMonth()+1).padStart(2,'0')
               + String(now.getDate()).padStart(2,'0')
               + '-';
    $('outPrefix').value = base + ts;
  }
  updateCmd();
}).catch(()=>setConnection(false));

// ── Visibility wiring ──
function onSoloTypeChange() {
  const isSolo = $('soloType').value !== '';
  ['chemWrap','whitelistWrap','soloOptRow','soloFeatRow'].forEach(id=>{ $(id).hidden = !isSolo; });
  if (!isSolo) $('chemCustom').hidden = true;
  else onChemPresetChange();
  updateCmd();
}
function onChemPresetChange() {
  $('chemCustom').hidden = $('chemPreset').value !== 'custom';
  updateCmd();
}
function onRunModeChange() {
  const mode = $('runMode').value;
  $('genGenRow').hidden = mode !== 'genomeGenerate';
  $('alignRow').hidden  = mode !== 'alignReads';
  updateCmd();
}

// ── Genome directory probe ──
let _probeTimer = null;
function scheduleProbe() {
  clearTimeout(_probeTimer);
  _probeTimer = setTimeout(probeGenomeDir, 650);
}
function setProbe(cls, text) {
  const span = document.createElement('span');
  span.className = cls; span.textContent = text;
  $('genomeDirStatus').replaceChildren(span);
  return span;
}
// Server-provided paths never become markup or inline script.
function setProbeWithButton(cls, text, label, onClick) {
  const span = setProbe(cls, text);
  const btn = document.createElement('button');
  btn.type = 'button'; btn.className = 'btn btn-sm'; btn.textContent = label;
  btn.addEventListener('click', onClick);
  span.after(btn);
}
async function probeGenomeDir() {
  const dir = $('genomeDir').value.trim();
  if (!dir) { $('genomeDirStatus').replaceChildren(); return; }
  setProbe('t-muted', 'Checking\u2026');
  let d;
  try { d = await fetch('/genome/probe?dir='+encodeURIComponent(dir)).then(r=>r.json()); }
  catch(e) { $('genomeDirStatus').replaceChildren(); return; }
  if (dir !== $('genomeDir').value.trim()) return; // superseded by newer input
  if (d.type === 'star_index') {
    setProbe('t-ok', '\u2713 Valid STAR genome index');
  } else if (d.type === 'cellranger_ref') {
    const sd = d.starDir || '', gf = d.gtfFile || '';
    setProbeWithButton('t-warn', 'CellRanger reference detected.', 'Use star/ index and genes.gtf', () => applyCRRef(sd, gf));
  } else if (d.type === 'not_found') {
    setProbe('t-err', 'Directory not found');
  } else if (d.type === 'has_source' && ((d.fastaFiles||[]).length || (d.gtfFiles||[]).length)) {
    const fastas = d.fastaFiles||[], gtfs = d.gtfFiles||[], hint = [];
    if (fastas.length) hint.push(fastas.length+' FASTA');
    if (gtfs.length) hint.push(gtfs.length+' GTF');
    setProbeWithButton('t-info', 'Found '+hint.join(', ')+' file(s), but no index.', 'Switch to genomeGenerate',
                       () => switchToGenomeGenerate(fastas, gtfs[0]||''));
  } else {
    setProbe('t-muted', 'No STAR index here. Build one with genomeGenerate.');
  }
}
function applyCRRef(starDir, gtfFile) {
  $('genomeDir').value = starDir;
  if (gtfFile) $('gtf').value = gtfFile;
  setProbe('t-ok', 'Using the star/ index');
  updateCmd();
}
function switchToGenomeGenerate(fastas, gtf) {
  $('runMode').value = 'genomeGenerate';
  onRunModeChange();
  if (fastas && fastas.length) $('genomeFastaFiles').value = fastas.join(' ');
  if (gtf) $('gtf').value = gtf;
  setProbe('t-ok', 'Switched to genomeGenerate. Set the output prefix to the new index directory.');
  updateCmd();
}
$('genomeDir').addEventListener('input', scheduleProbe);
$('runMode').addEventListener('change', onRunModeChange);
$('soloType').addEventListener('change', onSoloTypeChange);
$('chemPreset').addEventListener('change', onChemPresetChange);
$('rfc').addEventListener('change', e=>{ $('rfcCustomWrap').hidden = e.target.value !== 'custom'; updateCmd(); });
$('cellFilterSel').addEventListener('change', e=>{ $('cfCustomWrap').hidden = e.target.value !== 'custom'; updateCmd(); });
$('jform').addEventListener('input', updateCmd);
$('jform').addEventListener('change', updateCmd);

// ── Command builder ──
function qarg(s){ return /[ "\\]/.test(String(s)) ? '"'+String(s)+'"' : String(s); }

function getChemParams() {
  const p = CHEM[$('chemPreset').value];
  return p
    ? {soloCBlen:p[0],soloUMIlen:p[1],soloCBstart:p[2],soloUMIstart:p[3],soloBarcodeReadLength:p[4]}
    : {soloCBlen:    parseInt($('soloCBlen').value),
       soloUMIlen:   parseInt($('soloUMIlen').value),
       soloCBstart:  parseInt($('soloCBstart').value),
       soloUMIstart: parseInt($('soloUMIstart').value),
       soloBarcodeReadLength: $('disableStrictLen').checked?0:1};
}
)HTML"
R"HTML(
function buildCmd() {
  const args = [];
  const flag = (k,...vs) => { args.push('--'+k); vs.forEach(v=>args.push(qarg(v))); };

  const runMode   = $('runMode').value;
  const genomeDir = $('genomeDir').value.trim();
  const outPrefix = $('outPrefix').value.trim();
  const threads   = parseInt($('threads').value)||4;

  flag('runMode', runMode);
  if (genomeDir) flag('genomeDir', genomeDir);
  if (outPrefix) {
    const p = (outPrefix.endsWith('/')||outPrefix.endsWith('\\')) ? outPrefix : outPrefix+'/';
    flag('outFileNamePrefix', p);
  }
  flag('runThreadN', threads);

  if (runMode === 'genomeGenerate') {
    const fasta = $('genomeFastaFiles').value.trim();
    if (fasta) { args.push('--genomeFastaFiles'); fasta.split(/\s+/).filter(Boolean).forEach(f=>args.push(qarg(f))); }
    const saIdx = parseInt($('genomeSAindexNbases').value)||14;
    if (saIdx !== 14) flag('genomeSAindexNbases', saIdx);
    const overhang = parseInt($('sjdbOverhang').value)||100;
    if (overhang !== 100) flag('sjdbOverhang', overhang);
  }

  const r1 = $('r1').value.trim();
  const r2 = $('r2').value.trim();
  if (r1) {
    const files = [...r1.split(/\s+/), ...(r2?r2.split(/\s+/):[])].filter(Boolean);
    args.push('--readFilesIn'); files.forEach(f=>args.push(qarg(f)));
  }

  const rfcSel = $('rfc').value;
  const rfc = rfcSel==='custom' ? $('rfcCustomVal').value.trim() : rfcSel;
  if (rfc) flag('readFilesCommand', rfc);

  const samtype = $('samtype').value;
  args.push('--outSAMtype'); samtype.split(' ').forEach(t=>args.push(t));

  const soloType = $('soloType').value;
  if (soloType) {
    flag('soloType', soloType);

    const cp = getChemParams();
    flag('soloCBlen',             cp.soloCBlen);
    flag('soloUMIlen',            cp.soloUMIlen);
    flag('soloCBstart',           cp.soloCBstart);
    flag('soloUMIstart',          cp.soloUMIstart);
    flag('soloBarcodeReadLength', cp.soloBarcodeReadLength);

    const wl = $('whitelist').value.trim();
    if (wl) flag('soloCBwhitelist', wl);

    const clip = $('clipAdapterType').value;
    if (clip) flag('clipAdapterType', clip);

    const soloStrand = $('soloStrand').value;
    if (soloStrand !== 'Forward') flag('soloStrand', soloStrand);
    const soloUMIdedup = $('soloUMIdedup').value;
    if (soloUMIdedup !== '1MM_All') flag('soloUMIdedup', soloUMIdedup);

    const feats = [...document.querySelectorAll('.sfeat:checked')].map(c=>c.value);
    if (feats.length){ args.push('--soloFeatures'); feats.forEach(f=>args.push(f)); }

    flag('soloMultiMappers', $('soloMM').value);

    const cfSel = $('cellFilterSel').value;
    const cf = cfSel==='custom' ? $('cfCustomVal').value.trim() : cfSel;
    if (cf){ args.push('--soloCellFilter'); cf.split(/\s+/).filter(Boolean).forEach(t=>args.push(t)); }
  }

  if (runMode === 'alignReads') {
    const twopass = $('twopassMode').value;
    if (twopass !== 'None') flag('twopassMode', twopass);
    const quants = [...document.querySelectorAll('.qmode:checked')].map(c=>c.value);
    if (quants.length) { args.push('--quantMode'); quants.forEach(q=>args.push(q)); }
    const samStrand = $('outSAMstrandField').value;
    if (samStrand !== 'None') flag('outSAMstrandField', samStrand);
  }

  const multimapNmax = parseInt($('multimapNmax').value)||10;
  if (multimapNmax !== 10) flag('outFilterMultimapNmax', multimapNmax);
  const limitRAM = parseInt($('limitBAMsortRAM').value)||0;
  if (limitRAM > 0) flag('limitBAMsortRAM', limitRAM);

  const extraArgs = $('extraArgs').value.trim();
  if (extraArgs) extraArgs.split(/\s+/).forEach(t => args.push(t));

  const gtf = $('gtf').value.trim();
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

// Flags are highlighted with DOM nodes, so typed values never become markup.
function updateCmd() {
  const box = $('cmd-box');
  const parts = [];
  buildCmd().split('\n').forEach((line, i) => {
    if (i) parts.push('\n');
    const m = line.match(/^(\s*)(--\S+)(.*)$/);
    if (!m) { parts.push(line); return; }
    const f = document.createElement('span');
    f.className = 'flag'; f.textContent = m[2];
    parts.push(m[1], f, m[3]);
  });
  box.replaceChildren(...parts);
}

async function copyCmd() {
  const text = buildCmd().replace(/ \\\n\s+/g, ' ');
  const btn = $('copyBtn');
  try { await navigator.clipboard.writeText(text); }
  catch (e) {
    const ta=document.createElement('textarea');
    ta.value=text; document.body.appendChild(ta); ta.select();
    document.execCommand('copy'); document.body.removeChild(ta);
  }
  btn.textContent = 'Copied';
  setTimeout(()=>{ btn.textContent = 'Copy'; }, 1500);
}
$('copyBtn').addEventListener('click', copyCmd);
)HTML"
R"HTML(
// ── Field help: a toggle per field, readable by keyboard, touch and screen readers ──
document.querySelectorAll('.field[data-tip]').forEach((field, i)=>{
  const label = field.querySelector(':scope > label, :scope > .lbl');
  if (!label) return;
  const help = document.createElement('p');
  help.className = 'help'; help.id = 'help-'+i; help.hidden = true;
  help.textContent = field.dataset.tip;
  const btn = document.createElement('button');
  btn.type = 'button'; btn.className = 'help-btn'; btn.textContent = '?';
  btn.setAttribute('aria-expanded', 'false');
  btn.setAttribute('aria-controls', help.id);
  btn.setAttribute('aria-label', 'Help: '+label.textContent.trim());
  btn.addEventListener('click', ()=>{
    help.hidden = !help.hidden;
    btn.setAttribute('aria-expanded', String(!help.hidden));
  });
  const row = document.createElement('div');
  row.className = 'lblrow';
  label.replaceWith(row); row.append(label, btn);
  row.after(help);
  const ctl = field.querySelector('input:not([type=checkbox]),select');
  if (ctl) ctl.setAttribute('aria-describedby', help.id);
  field.removeAttribute('data-tip');
});
document.addEventListener('keydown', e=>{
  if (e.key !== 'Escape') return;
  const field = document.activeElement && document.activeElement.closest && document.activeElement.closest('.field');
  const btn = field && field.querySelector('.help-btn[aria-expanded="true"]');
  if (btn) btn.click();
});

// ── Form submit ──
$('jform').addEventListener('submit', async e=>{
  e.preventDefault();
  $('ferr').textContent = '';

  const r1 = $('r1').value.trim();
  const r2 = $('r2').value.trim();
  const readFilesIn = r1 ? [...r1.split(/\s+/), ...(r2?r2.split(/\s+/):[])].filter(Boolean) : [];

  const rfcSel = $('rfc').value;
  const readFilesCommand = rfcSel==='custom' ? $('rfcCustomVal').value.trim() : rfcSel;

  const cfSel = $('cellFilterSel').value;
  const soloCellFilter = cfSel==='custom' ? $('cfCustomVal').value.trim() : cfSel;

  const soloType = $('soloType').value;
  const chemParams = soloType ? getChemParams() : {};

  const feats = [...document.querySelectorAll('.sfeat:checked')].map(c=>c.value);
  const quantParts = [...document.querySelectorAll('.qmode:checked')].map(c=>c.value);
  const runMode2 = $('runMode').value;

  const body = {
    runMode:                runMode2,
    genomeDir:              $('genomeDir').value.trim(),
    outFileNamePrefix:      $('outPrefix').value.trim(),
    runThreadN:             parseInt($('threads').value)||4,
    outSAMtype:             $('samtype').value,
    readFilesCommand,
    soloType,
    soloCBwhitelist:        $('whitelist').value.trim(),
    soloFeatures:           feats,
    soloMultiMappers:       $('soloMM').value,
    soloCellFilter,
    clipAdapterType:        $('clipAdapterType').value,
    soloStrand:             soloType ? $('soloStrand').value : '',
    soloUMIdedup:           soloType ? $('soloUMIdedup').value : '',
    sjdbGTFfile:            $('gtf').value.trim(),
    twopassMode:            runMode2==='alignReads' ? $('twopassMode').value : '',
    quantMode:              quantParts.join(' '),
    outSAMstrandField:      runMode2==='alignReads' ? $('outSAMstrandField').value : '',
    outFilterMultimapNmax:  parseInt($('multimapNmax').value)||10,
    limitBAMsortRAM:        parseInt($('limitBAMsortRAM').value)||0,
    genomeFastaFiles:       runMode2==='genomeGenerate' ? $('genomeFastaFiles').value.trim().split(/\s+/).filter(Boolean) : [],
    genomeSAindexNbases:    runMode2==='genomeGenerate' ? parseInt($('genomeSAindexNbases').value)||14 : 14,
    sjdbOverhang:           runMode2==='genomeGenerate' ? parseInt($('sjdbOverhang').value)||100 : 100,
    generateReport:         $('generateReport').checked,
    extraArgs:              $('extraArgs').value.trim(),
    ...chemParams,
  };
  if (readFilesIn.length) body.readFilesIn = readFilesIn;

  const btn = $('submitBtn');
  if (btn.disabled) return;
  btn.disabled = true; btn.textContent = 'Submitting\u2026';
  try {
    const res = await fetch('/jobs',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)});
    const data = await res.json().catch(()=>({}));
    if (!res.ok) { $('ferr').textContent = data.error || ('Submission failed (HTTP '+res.status+')'); return; }
    refreshJobs();
  } catch (err) {
    $('ferr').textContent = 'Could not reach the STAR server; the job was not submitted.';
  } finally {
    btn.disabled = false; btn.textContent = 'Submit job';
  }
});
)HTML"
R"HTML(
// ── Jobs table ──
function esc(v){
  return String(v ?? '').replace(/[&<>"']/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
}
const STATES = ['queued','running','succeeded','failed','cancelled'];
function badge(s){ const k = STATES.includes(s) ? s : 'failed'; return `<span class="badge s-${k}">${esc(s)}</span>`; }
function dur(start,end){
  if (!start) return '';
  const s=new Date(start), e=end?new Date(end):new Date();
  let sec=Math.max(0,Math.floor((e-s)/1000));
  if (sec<60) return sec+'s';
  if (sec<3600) return Math.floor(sec/60)+'m '+('0'+sec%60).slice(-2)+'s';
  return Math.floor(sec/3600)+'h '+('0'+Math.floor((sec%3600)/60)).slice(-2)+'m';
}
const fmtTime = new Intl.DateTimeFormat(undefined, {hour:'2-digit', minute:'2-digit', second:'2-digit'});
const fmtDay  = new Intl.DateTimeFormat(undefined, {month:'short', day:'numeric', hour:'2-digit', minute:'2-digit'});
function when(iso){
  if (!iso) return '';
  const d = new Date(iso);
  if (isNaN(d)) return esc(iso);
  const text = d.toDateString() === new Date().toDateString() ? fmtTime.format(d) : fmtDay.format(d);
  return `<time datetime="${esc(iso)}" title="${esc(iso)}">${esc(text)}</time>`;
}

// Rows are keyed by job id and only rewritten when their content changes, and a
// focused action button gets its focus back, so the 3 s refresh never moves the
// keyboard user.
const rowHtml = new Map();
function jobRow(j){
  const id = Number(j.id);
  let why = '';
  if (j.state==='failed' && j.exitReason) {
    why = `<div class="why">${esc(j.exitReason)}`;
    if (j.errorMessage) why += `<details><summary>STAR's message</summary><pre>${esc(j.errorMessage)}</pre></details>`;
    why += '</div>';
  }
  const acts = [`<button type="button" class="btn btn-sm" data-act="logs" data-id="${id}">Logs</button>`];
  if (j.reportPath) acts.push(`<a class="btn btn-sm" href="/jobs/${id}/report" target="_blank" rel="noopener">Report</a>`);
  if (j.state==='running'||j.state==='queued') acts.push(`<button type="button" class="btn btn-sm btn-danger" data-act="cancel" data-id="${id}">Cancel</button>`);
  return `<td data-label="Job"><div class="num">#${id}</div></td>
    <td data-label="Status"><div>${badge(j.state)}${why}</div></td>
    <td data-label="Mode"><div>${esc(j.runMode)}</div></td>
    <td data-label="Output"><div class="path" title="${esc(j.outputPrefix)}">${esc(j.outputPrefix)}</div></td>
    <td data-label="Submitted"><div class="num">${when(j.submitTime)}</div></td>
    <td data-label="Duration"><div class="num">${dur(j.startTime,j.state==='running'?null:j.endTime)}</div></td>
    <td class="c-acts"><div class="acts">${acts.join('')}</div></td>`;
}
function renderJobs(jobs){
  const tb = $('jtbody');
  const active = document.activeElement;
  const focusKey = active && tb.contains(active) && active.closest('tr')
    ? active.closest('tr').dataset.id + ':' + (active.dataset.act || active.textContent.trim()) : null;
  const ordered = jobs.slice().reverse();
  const keep = new Set(ordered.map(j=>String(Number(j.id))));
  [...tb.children].forEach(tr=>{ if (!keep.has(tr.dataset.id)) { rowHtml.delete(tr.dataset.id); tr.remove(); } });
  ordered.forEach((j, idx)=>{
    const key = String(Number(j.id));
    let tr = tb.querySelector(`tr[data-id="${key}"]`);
    if (!tr) { tr = document.createElement('tr'); tr.dataset.id = key; }
    const html = jobRow(j);
    if (rowHtml.get(key) !== html) { tr.innerHTML = html; rowHtml.set(key, html); }
    if (tb.children[idx] !== tr) tb.insertBefore(tr, tb.children[idx] || null);
  });
  if (focusKey && !tb.contains(document.activeElement)) {
    const [id, act] = focusKey.split(':');
    const tr = tb.querySelector(`tr[data-id="${id}"]`);
    const target = tr && ([...tr.querySelectorAll('button,a')].find(b=>(b.dataset.act||b.textContent.trim())===act)
                          || tr.querySelector('button,a'));
    if (target) target.focus();
  }
  $('nojobs').hidden = jobs.length > 0;
}
function setConnection(ok){
  const b = $('conn'), pill = $('connPill');
  pill.classList.toggle('down', !ok);
  pill.textContent = ok ? 'Connected' : 'Disconnected';
  if (ok) { b.hidden = true; b.textContent = ''; }
  else if (b.hidden) { b.textContent = 'Lost contact with the STAR server. The job list may be out of date; retrying every 3 seconds.'; b.hidden = false; }
}
async function refreshJobs(){
  let jobs;
  try {
    const r = await fetch('/jobs');
    if (!r.ok) throw new Error('HTTP '+r.status);
    jobs = await r.json();
  } catch (e) { setConnection(false); return; }
  setConnection(true);
  renderJobs(jobs);
  if (watchId!==null && jobs.some(x=>x.id===watchId)) fetchLogs(watchId);
}
$('jtbody').addEventListener('click', e=>{
  const b = e.target.closest('button[data-act]');
  if (!b) return;
  const id = Number(b.dataset.id);
  if (b.dataset.act === 'logs') viewLogs(id);
  else if (b.dataset.act === 'cancel') cancelJob(id);
});
)HTML"
R"HTML(
async function viewLogs(id){
  watchId=id;
  $('log-jid').textContent=id;
  $('log-box').hidden=false;
  lastLog = null;
  await fetchLogs(id, true);
  $('log-box').scrollIntoView({behavior: matchMedia('(prefers-reduced-motion: reduce)').matches ? 'auto' : 'smooth'});
}
// Follows the end of the log only while the reader is already there, and leaves
// the text alone (scroll position, selection) when nothing new arrived.
let lastLog = null;
async function fetchLogs(id, jumpToEnd){
  let text;
  try {
    const data = await fetch('/jobs/'+id+'/logs').then(r=>r.json());
    text = data.logs || (data.error ? 'Error: '+data.error : '(no logs yet)');
  } catch (e) { return; }
  if (id !== watchId || text === lastLog) return;
  const el = $('log-pre');
  const atEnd = el.scrollHeight - el.scrollTop - el.clientHeight < 30;
  const sel = window.getSelection();
  if (!jumpToEnd && sel && !sel.isCollapsed && el.contains(sel.anchorNode)) return; // reader is selecting
  lastLog = text;
  const top = el.scrollTop;
  el.textContent = text;
  el.scrollTop = (jumpToEnd || atEnd) ? el.scrollHeight : top;
}
function closeLogs(){ watchId=null; lastLog=null; $('log-box').hidden=true; }
$('closeLogsBtn').addEventListener('click', closeLogs);
async function cancelJob(id){
  if (!confirm('Cancel job '+id+'? STAR will be stopped, and any outputs it has written will be incomplete.')) return;
  try {
    const r = await fetch('/jobs/'+id+'/cancel',{method:'POST',headers:{'Content-Type':'application/json'},body:'{}'});
    if (!r.ok) { const d = await r.json().catch(()=>({})); alert('Could not cancel job '+id+': '+(d.error||'HTTP '+r.status)); }
  } catch (e) { alert('Could not reach the STAR server; job '+id+' was not cancelled.'); }
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
