#!/usr/bin/env python3
"""Differential tests for the opt-in adaptive round scheduler, using real STAR indexes.

No timing claims: synthetic fixtures exercise boundaries; real-prefix acceptance
belongs to benchmark_cpu_subset.py. Retain commands, hashes and failed outputs.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import random
import re
import tempfile

from fixture_process import run_fixture
from test_cpu_upstream import bam_records, bam_scientific_header, scientific_final_fields


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--star', required=True)
    parser.add_argument('--reference', required=True)
    parser.add_argument('--cuda', action='store_true')
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    star = str(Path(args.star).resolve(strict=True))
    reference = str(Path(args.reference).resolve(strict=True))
    root = args.output or Path(tempfile.mkdtemp(prefix='star-batched-seeds-'))
    root.mkdir(parents=True, exist_ok=True)
    print(f'Evidence: {root}', flush=True)
    checks = []

    def run(label, options, binary=star, valid=True, env=None):
        out = root / label
        out.mkdir()
        command = [binary, *options, '--outFileNamePrefix', str(out.resolve()) + os.sep]
        (out / 'command.json').write_text(json.dumps(command))
        # Small fixtures can finish before the asynchronous device setup; wait for it so the
        # device path is what gets tested.
        env = {**(env if env is not None else os.environ), 'STAR_GPU_SETUP_OVERLAP': '0'}
        process = run_fixture(command, out / 'console.log', env=env)
        assert (process.returncode == 0) == valid, (label, process.returncode, process.stdout[-3000:])
        checks.append(label)
        return out

    def signature(out):
        return (bam_records(out / 'Aligned.out.bam'), bam_scientific_header(out / 'Aligned.out.bam'),
                (out / 'SJ.out.tab').read_bytes(), scientific_final_fields(out / 'Log.final.out'))

    rng = random.Random(816)
    genome = ''.join(rng.choices('ACGT', k=20000))
    genome = genome[:1000] + 'ACGT'*100 + 'N'*8 + genome[1408:]
    fasta = root / 'reference.fa'
    fasta.write_text('>chr1\n' + genome + '\n')
    gtf = root / 'reference.gtf'
    gtf.write_text('chr1\ttest\texon\t101\t400\t.\t+\t.\tgene_id "g"; transcript_id "t";\n'
                   'chr1\ttest\texon\t701\t1000\t.\t+\t.\tgene_id "g"; transcript_id "t";\n')
    fragments = [genome[p:p+80] for p in rng.sample(range(100, 19800), 300)]
    fragments += [genome[350:400]+genome[700:750], genome[1000:1080], 'N'+genome[200:280]+'N',
                  genome[1360:1440], genome[300:310], 'T'*70, 'N'*80, 'A', 'NN', genome[700:1200]]
    fragments += [s[:25]+'A'+s[26:45]+'NN'+s[47:] for s in fragments[:20]]
    fragments += [s.translate(str.maketrans('ACGT', 'TGCA'))[::-1] for s in fragments[:50]]
    reads = []
    for mate in range(2):
        fastq = root / f'reads{mate}.fastq'
        sequences = fragments if mate == 0 else [genome[200+i:280+i] for i in range(len(fragments))]
        fastq.write_text(''.join(f'@r{i}/{mate+1} extra\n{s}\n+\n{"I"*len(s)}\n' for i, s in enumerate(sequences)))
        reads.append(str(fastq.resolve()))
    for sparse in (1, 3):
        index = root / f'index{sparse}'
        index.mkdir()
        run(f'generate{sparse}', ['--runMode', 'genomeGenerate', '--runThreadN', '2', '--genomeDir', str(index.resolve()),
             '--genomeFastaFiles', str(fasta.resolve()), '--sjdbGTFfile', str(gtf.resolve()), '--sjdbOverhang', '49',
             '--genomeSAindexNbases', '4', '--genomeChrBinNbits', '10', '--genomeSAsparseD', str(sparse)], binary=reference)
        common = ['--genomeDir', str(index.resolve()), '--runThreadN', '2', '--limitIObufferSize', '300000', '100000',
                  '--outSAMtype', 'BAM', 'Unsorted', '--outSAMunmapped', 'Within', '--outSAMattributes', 'All']
        for profile, options in (
                ('single', ['--readFilesIn', reads[0]]),
                ('fixed', ['--readFilesIn', reads[0], '--seedSearchLmax', '12']),
                ('overlap', ['--readFilesIn', reads[0], '--seedSearchStartLmax', '13']),
                ('multiplicity', ['--readFilesIn', reads[0], '--seedMultimapNmax', '1']),
                ('paired', ['--readFilesIn', *reads]),
                ('clipped', ['--readFilesIn', reads[0], '--clip5pNbases', '3', '--clip3pNbases', '2'])):
            prefix = f'sparse{sparse}-{profile}'
            expected = signature(run(prefix+'-reference', common+options, binary=reference))
            assert signature(run(prefix+'-off', common+options)) == expected, prefix+' off'
            for batch in (1, 7, 256):
                mode = 'required' if args.cuda else 'cpu'
                label = f'{prefix}-{mode}-{batch}'
                observed = run(label, common+options+['--seedSearchBackend', mode, '--seedBatchSize', str(batch)])
                assert signature(observed) == expected, label
                events = (observed / 'Log.out').read_text()
                assert 'SEED_BATCH worker=' in events
                if args.cuda:
                    assert 'SEED_BATCH setup_seconds=' in events and 'preflight_cpu_fallback' not in events
                    assert 'scheduler=device_adaptive' in events
                    assert 'consolidation=device' in events
                    assert 'hit_expansion=device' in events
                    assert sum(map(int,re.findall(r'gpu_projected_hits=(\d+)',events)))>0
                    assert sum(map(int,re.findall(r'gpu_batches=(\d+)',events)))>0
        base = common+['--readFilesIn', reads[0]]
        for label, extra in (
                ('bad-mode', ['--seedSearchBackend', 'invalid']),
                ('zero-batch', ['--seedSearchBackend', 'cpu', '--seedBatchSize', '0']),
                ('two-pass', ['--seedSearchBackend', 'cpu', '--twopassMode', 'Basic']),
                ('random-workers', ['--seedSearchBackend', 'cpu', '--outMultimapperOrder', 'Random'])):
            run(f'{sparse}-{label}', base+extra, valid=False)
        run(f'{sparse}-missing-device', base+['--seedSearchBackend', 'required'], valid=False,
            env={**os.environ, 'CUDA_VISIBLE_DEVICES': '-1'})
        fallback = run(f'{sparse}-auto-fallback', base+['--seedSearchBackend', 'auto'],
                       env={**os.environ, 'CUDA_VISIBLE_DEVICES': '-1'})
        assert 'preflight_cpu_fallback=' in (fallback / 'Log.out').read_text()
        assert signature(fallback) == signature(root / f'sparse{sparse}-single-reference')
    receipt = {'status': 'PASS', 'cuda': args.cuda, 'checks': checks,
               'binaries': {p: hashlib.sha256(Path(p).read_bytes()).hexdigest() for p in (star, reference)}}
    (root / 'result.json').write_text(json.dumps(receipt, indent=2))
    print(json.dumps(receipt, indent=2))


if __name__ == '__main__':
    main()
