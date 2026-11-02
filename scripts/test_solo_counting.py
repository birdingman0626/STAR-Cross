#!/usr/bin/env python3
"""CLI regressions for directional UMI reachability and zero-support EM."""
import argparse
import hashlib
import itertools
import json
import math
from pathlib import Path
import random
import tempfile

from fixture_process import run_fixture


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--star', required=True)
    parser.add_argument('--output-dir', type=Path)
    args = parser.parse_args()
    star = Path(args.star).resolve(strict=True)
    temporary = tempfile.TemporaryDirectory(prefix='star-solo-counting-') if args.output_dir is None else None
    root = Path(temporary.name) if temporary else args.output_dir.resolve()
    if not temporary:
        root.mkdir(parents=True, exist_ok=False)
    checks = []

    def run(name, options):
        output = root / name
        output.mkdir()
        command = [str(star), *map(str, options), '--outFileNamePrefix', str(output) + '/']
        (output / 'command.json').write_text(json.dumps(command, indent=2))
        result = run_fixture(command, output / 'console.log')
        assert result.returncode == 0, result.stdout
        return output

    def fastq(path, sequences):
        path.write_text(''.join(f'@r{i}\n{s}\n+\n{"I"*len(s)}\n' for i, s in enumerate(sequences)))

    rng = random.Random(7731)
    genome = ''.join(rng.choices('ACGT', k=20000))
    fasta = root / 'genome.fa'
    fasta.write_text('>chr1\n' + genome + '\n')
    words = [''.join(p) for p in itertools.product('ACGT', repeat=2)]
    paths = [t for t in itertools.combinations(words, 3)
             if sum(sum(x != y for x, y in zip(a, b)) == 1 for a, b in itertools.combinations(t, 2)) == 2]
    barcodes = ['ACGTCGTA' + ''.join('ACGT'[(i >> s) & 3] for s in range(14, -1, -2))
                for i in range(len(paths))]
    whitelist = root / 'whitelist.txt'
    whitelist.write_text(''.join(b + '\n' for b in barcodes))
    for genes in (1, 100, 101, 150):
        gtf = root / f'{genes}.gtf'
        gtf.write_text(''.join(f'chr1\ttest\texon\t101\t400\t.\t+\t.\tgene_id "g{i}"; transcript_id "t{i}";\n'
                               for i in range(genes))
                       + 'chr1\ttest\texon\t501\t800\t.\t+\t.\tgene_id "control"; transcript_id "control";\n')
        index = root / f'index-{genes}'
        index.mkdir()
        run(f'generate-{genes}', ['--runMode', 'genomeGenerate', '--runThreadN', 1,
            '--genomeDir', index, '--genomeFastaFiles', fasta, '--sjdbGTFfile', gtf,
            '--sjdbOverhang', 79, '--genomeSAindexNbases', 4, '--genomeChrBinNbits', 10])
        cdna, cb = root / f'{genes}-cdna.fq', root / f'{genes}-barcode.fq'
        if genes == 1:
            fastq(cdna, [genome[150:230]] * (3*len(paths)))
            fastq(cb, [barcode + u + 'ACGTACGTAC' for barcode, triple in zip(barcodes, paths) for u in triple])
        else:
            fastq(cdna, [genome[150:230], genome[550:630]])
            fastq(cb, [barcodes[0] + 'ACGTACGTACGT', barcodes[0] + 'TGCATGCATGCA'])
        for mode in (('Exact', '1MM_Directional_UMItools') if genes == 1 else ('Exact',)):
            output = run(f'{genes}-{mode}', ['--genomeDir', index, '--readFilesIn', cdna, cb,
                '--runThreadN', 1, '--soloType', 'CB_UMI_Simple', '--soloCBwhitelist', whitelist,
                '--soloCBstart', 1, '--soloCBlen', 16, '--soloUMIstart', 17, '--soloUMIlen', 12,
                '--soloBarcodeReadLength', 0, '--soloCellFilter', 'None', '--soloFeatures', 'Gene',
                '--soloUMIfiltering', '-', '--soloUMIdedup', mode,
                '--soloMultiMappers', 'Uniform', 'EM', '--outSAMtype', 'SAM', '--outSAMattributes', 'NH'])
            sam = [s for s in (output / 'Aligned.out.sam').read_text().splitlines() if not s.startswith('@')]
            assert all('NH:i:1' in s.split('\t') for s in sam)
            name = 'matrix.mtx' if genes == 1 else 'UniqueAndMult-EM.mtx'
            rows = [s.split() for s in (output / 'Solo.out/Gene/raw' / name).read_text().splitlines()
                    if not s.startswith('%')][1:]
            values = [float(row[2]) for row in rows]
            assert all(math.isfinite(v) for v in values)
            if genes == 1:
                assert len(values) == len(paths)
                assert {int(row[1]) for row in rows} == set(range(1, len(paths)+1))
                assert all(row[0] == '1' for row in rows)
                assert all(v == (3 if mode == 'Exact' else 1) for v in values)
            else:
                assert len(values) == genes+1
                assert all(math.isclose(v, 1/genes, rel_tol=1e-5) for v in values[:-1])
                assert values[-1] == 1
                assert math.isclose(sum(values), 2, rel_tol=1e-5)
            checks.append({'genes': genes, 'mode': mode, 'nonzero_counts': len(values)})
    (root / 'result.json').write_text(json.dumps({'binary_sha256': hashlib.sha256(star.read_bytes()).hexdigest(),
                                                'checks': checks, 'status': 'PASS'}, indent=2))
    print(f'PASS: {len(paths)} directional paths, Exact control, and 100/101/150-gene EM conservation')
    if temporary:
        temporary.cleanup()


if __name__ == '__main__':
    main()
