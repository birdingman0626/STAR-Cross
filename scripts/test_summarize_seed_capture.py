import copy
import json
import struct
from pathlib import Path
import tempfile
import unittest

from summarize_seed_capture import sampled, summarize, compare


class DiagnosticContractTests(unittest.TestCase):
    def fixture(self, root, selected=None, extent=10000, modulus=64):
        if selected is None:
            selected = [i for i in range(1, extent+1) if sampled(i, modulus)]
        metadata = dict(format=1, sampling='splitmix64(read_id)%modulus==0',
                        sample_modulus=modulus, limit_per_worker=5000,seed_row_limit_per_worker=100000,
                        native_uint_bytes=8,query_bytes=64,match_bytes=32,byte_order='little')
        stats = dict(format=1, sample_modulus=modulus, limit_per_worker=5000,
                     seed_row_limit_per_worker=100000,
                     reads_seen=extent, reads_selected=len(selected), seed_reads=len(selected),
                     seed_rows=0, seed_reads_dropped=0, extension_requests_seen=0,
                     selected=0, recorded=0, dropped=0,
                     first_observed_read=1, last_observed_read=extent,
                     first_selected_read=min(selected, default=0), last_selected_read=max(selected, default=0),
                     sampled_map_ns=1000, sampled_seed_ns=300, sampled_extension_ns=100,
                     sampled_stitch_ns=400, capture_io_ns=30,
                     interval_width_log2=[0]*64, query_length_log2=[0]*64, multiplicity_log2=[0]*64)
        root.mkdir(exist_ok=True)
        (root/'capture_metadata.json').write_text(json.dumps(metadata))
        (root/'queries-0.bin.json').write_text(json.dumps(stats))
        (root/'queries-0.bin').write_bytes(b'STARSD01')
        (root/'seeds-0.jsonl').write_text(''.join(json.dumps(dict(read_id=i, seeds=[]))+'\n' for i in selected))
        return stats

    def update(self, root, stats):
        (root/'queries-0.bin.json').write_text(json.dumps(stats))

    def test_full_observed_extent_and_hash_sample_pass(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);self.fixture(root)
            result=summarize(root)
            self.assertTrue(result['eligible_for_screening'])
            self.assertEqual((result['first_observed_read'], result['last_observed_read']), (1,10000))
            self.assertEqual(result['observed_read_id_span'],10000)
            self.assertTrue(all(result['sampled_read_deciles']))

    def test_early_prefix_cannot_rescale_itself_to_full_coverage(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp)
            selected=[i for i in range(1,1001) if sampled(i,64)]
            self.fixture(root,selected=selected)
            result=summarize(root)
            self.assertFalse(result['eligible_for_screening'])
            self.assertEqual(result['sampled_read_deciles'][1:],[0]*9)

    def test_missing_observed_extent_is_unverified(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);stats=self.fixture(root);del stats['last_observed_read'];self.update(root,stats)
            with self.assertRaisesRegex(ValueError,'unverified observed'):summarize(root)

    def test_wrong_hash_selection_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp)
            bad=next(i for i in range(1,100) if not sampled(i,64))
            self.fixture(root,selected=[bad])
            with self.assertRaisesRegex(ValueError,'hash selection'):summarize(root)

    def test_counter_disagreement_and_query_histograms_rejected(self):
        for field, value, reason in (('seed_reads',1,'worker counters'),
                                     ('reads_selected',1,'selected-read'),
                                     ('dropped',1,'dropped query'),
                                     ('seed_reads_dropped',1,'dropped seed-read'),
                                     ('query_length_log2',[1]+[0]*63,'histogram')):
            with self.subTest(field=field), tempfile.TemporaryDirectory() as tmp:
                root=Path(tmp);stats=self.fixture(root);stats[field]=value
                if field=='seed_reads':stats['seed_reads_dropped']=stats['reads_selected']-value
                self.update(root,stats)
                with self.assertRaisesRegex(ValueError,reason):summarize(root)

    def test_dropped_selected_requests_disable_screening(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);stats=self.fixture(root)
            stats.update(extension_requests_seen=1,selected=1,recorded=0,dropped=1,
                         interval_width_log2=[1]+[0]*63,query_length_log2=[1]+[0]*63,multiplicity_log2=[1]+[0]*63)
            self.update(root,stats)
            self.assertFalse(summarize(root)['eligible_for_screening'])

    def test_empty_workers_do_not_expand_observed_extent(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);stats=self.fixture(root)
            empty=copy.deepcopy(stats)
            for key,value in list(empty.items()):
                if isinstance(value,int) and key not in ('format','sample_modulus','limit_per_worker','seed_row_limit_per_worker'):
                    empty[key]=0
            (root/'queries-1.bin.json').write_text(json.dumps(empty))
            (root/'seeds-1.jsonl').write_text('')
            (root/'queries-1.bin').write_bytes(b'STARSD01')
            result=summarize(root)
            self.assertEqual(result['workers'],2)
            self.assertEqual(result['first_observed_read'],1)
            self.assertTrue(result['eligible_for_screening'])

    def test_unknown_worker_and_duplicate_read_are_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);self.fixture(root)
            (root/'seeds-1.jsonl').write_text('')
            with self.assertRaisesRegex(ValueError,'workers disagree'):summarize(root)
            (root/'seeds-1.jsonl').write_text((root/'seeds-0.jsonl').read_text().splitlines()[0]+'\n')
            with self.assertRaisesRegex(ValueError,'repeated'):summarize(root)

    def test_ordered_seed_changes_fail_even_with_same_counts(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);left=root/'left';right=root/'right'
            self.fixture(left,selected=[1],extent=1,modulus=1)
            self.fixture(right,selected=[1],extent=1,modulus=1)
            self.assertEqual(compare(left,right)['reads'],1)
            (right/'seeds-0.jsonl').write_text(json.dumps(dict(read_id=1,seeds=[[1,2,3,4,5,6,7]]))+'\n')
            with self.assertRaisesRegex(ValueError,'ordered seed mismatch'):compare(left,right)

    def test_splitmix64_matches_known_unsigned_output(self):
        expected=0xe220a8397b1dcdaf
        for modulus in (2,3,7,64,2**64-1):
            self.assertEqual(sampled(0,modulus),expected%modulus==0)

    def query(self,root,stats,byte_order='<'):
        identity=stats['first_selected_read']
        payload=struct.pack(byte_order+'Q8Q4Q',identity,0,4,0,4,2,9,0,1,4,5,5,1)+bytes([0,1,2,3])
        stats.update(extension_requests_seen=1,selected=1,recorded=1,
                     interval_width_log2=[1]+[0]*63,query_length_log2=[1]+[0]*63,multiplicity_log2=[1]+[0]*63)
        self.update(root,stats);(root/'queries-0.bin').write_bytes(b'STARSD01'+payload)
        return payload

    def test_streamed_complete_native_record_and_declared_cap(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);stats=self.fixture(root);self.query(root,stats)
            result=summarize(root)
            self.assertEqual(result['native_capture']['records'],1)
            self.assertEqual(result['seed_row_cap_status'],'VERIFIED_DECLARED_CAP')

    def test_missing_truncated_and_trailing_native_records_rejected(self):
        for defect in ('missing','magic','identity','query','match','payload','trailing','count'):
            with self.subTest(defect=defect),tempfile.TemporaryDirectory() as tmp:
                root=Path(tmp);stats=self.fixture(root);payload=self.query(root,stats)
                path=root/'queries-0.bin'
                if defect=='missing':path.unlink()
                elif defect=='magic':path.write_bytes(b'WRONG001'+payload)
                elif defect=='count':
                    stats.update(recorded=0,dropped=1);self.update(root,stats)
                elif defect=='trailing':path.write_bytes(b'STARSD01'+payload+b'x')
                else:
                    end={'identity':4,'query':8+63,'match':8+64+31,'payload':len(payload)-1}[defect]
                    path.write_bytes(b'STARSD01'+payload[:end])
                with self.assertRaises(ValueError):summarize(root)

    def test_declared_big_endian_capture_validates(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);stats=self.fixture(root);self.query(root,stats,'>')
            metadata=json.loads((root/'capture_metadata.json').read_text());metadata['byte_order']='big'
            (root/'capture_metadata.json').write_text(json.dumps(metadata))
            self.assertEqual(summarize(root)['native_capture']['records'],1)

    def test_unsupported_abi_and_excess_seed_rows_rejected(self):
        for defect in ('abi','cap'):
            with self.subTest(defect=defect),tempfile.TemporaryDirectory() as tmp:
                root=Path(tmp);stats=self.fixture(root)
                if defect=='abi':
                    metadata=json.loads((root/'capture_metadata.json').read_text());metadata['query_bytes']=32
                    (root/'capture_metadata.json').write_text(json.dumps(metadata))
                else:
                    metadata=json.loads((root/'capture_metadata.json').read_text());metadata['seed_row_limit_per_worker']=1
                    stats.update(seed_row_limit_per_worker=1,seed_rows=2)
                    self.update(root,stats)
                    first=stats['first_selected_read'];rows=[json.loads(line) for line in (root/'seeds-0.jsonl').read_text().splitlines()]
                    next(row for row in rows if row['read_id']==first)['seeds']=[[0]*7,[0]*7]
                    (root/'seeds-0.jsonl').write_text(''.join(json.dumps(row)+'\n' for row in rows))
                    (root/'capture_metadata.json').write_text(json.dumps(metadata))
                with self.assertRaises(ValueError):summarize(root)

    def test_legacy_row_cap_status_is_not_silently_verified(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);stats=self.fixture(root);metadata=json.loads((root/'capture_metadata.json').read_text())
            del metadata['seed_row_limit_per_worker'];del stats['seed_row_limit_per_worker']
            self.update(root,stats);(root/'capture_metadata.json').write_text(json.dumps(metadata))
            self.assertEqual(summarize(root)['seed_row_cap_status'],'UNVERIFIED_LEGACY_NO_ROW_CAP')


if __name__=='__main__':unittest.main()
