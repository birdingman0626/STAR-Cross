"""Negative controls for exact BAM and Log.final scientific comparisons."""
import gzip
from pathlib import Path
import struct
import tempfile
import unittest
from test_cpu_upstream import bam_records, bam_scientific_header, scientific_final_fields


class ComparisonTest(unittest.TestCase):
    def bam(self,path,records,reference=b'chr1\0',group='sample1',command='baseline'):
        text=f'@HD\tVN:1.4\tSO:unsorted\n@SQ\tSN:chr1\tLN:1000\n@RG\tID:{group}\n@PG\tID:STAR\tCL:{command}\n'.encode()
        raw=b'BAM\1'+struct.pack('<i',len(text))+text+struct.pack('<ii',1,len(reference))+reference+struct.pack('<i',1000)
        raw+=b''.join(struct.pack('<i',len(record))+record for record in records)
        with gzip.open(path,'wb') as out:out.write(raw)

    def test_multiplicity_fields_and_reference_ids_are_not_ignored(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);original=root/'original.bam';candidate=root/'candidate.bam'
            # Structurally framed records; equality checks do not reinterpret tags.
            record=bytes(32)+b'NH\x69\x01\x00\x00\x00'
            other=bytes(32)+b'NH\x69\x02\x00\x00\x00'
            self.bam(original,[record,other])
            self.bam(candidate,[other,record],command='candidate')
            self.assertEqual(bam_records(original),bam_records(candidate))
            self.assertEqual(bam_scientific_header(original),bam_scientific_header(candidate))
            for changed in ([record],[record,other,record],[record,record]):
                self.bam(candidate,changed)
                self.assertNotEqual(bam_records(original),bam_records(candidate))
            self.bam(candidate,[record,other],reference=b'chr2\0')
            self.assertNotEqual(bam_scientific_header(original),bam_scientific_header(candidate))
            self.bam(candidate,[record,other],group='sample2')
            self.assertNotEqual(bam_scientific_header(original),bam_scientific_header(candidate))
            candidate.write_bytes(b'not a BAM')
            with self.assertRaises((OSError,AssertionError)):
                bam_records(candidate)

    def test_only_declared_timing_fields_are_excluded(self):
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/'Log.final.out'
            path.write_text('Started job on | yesterday\nNumber of input reads | 100\nFinished on | yesterday\nMapping speed, Million of reads per hour | 3\n')
            baseline=scientific_final_fields(path)
            path.write_text('Started job on | today\nNumber of input reads | 100\nFinished on | today\nMapping speed, Million of reads per hour | 9\n')
            self.assertEqual(baseline,scientific_final_fields(path))
            path.write_text('Number of input reads | 99\n')
            self.assertNotEqual(baseline,scientific_final_fields(path))
            path.write_text('Number of input reads | 100\nUnknown speed biology metric | 1\n')
            self.assertNotEqual(baseline,scientific_final_fields(path))


if __name__=='__main__':unittest.main()
