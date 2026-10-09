import contextlib
import io
import json
import hashlib
import os
from pathlib import Path
import tempfile
import types
import unittest
import zipfile
from unittest.mock import patch
import run_seed_experiment as runner

class ReceiptTest(unittest.TestCase):
    def fixture(self,root,label):
        genome=root/"index";genome.mkdir(exist_ok=True)
        for name in ("Genome","SA","SAindex","genomeParameters.txt"):(genome/name).write_bytes(b"fixture")
        binary=root/"binary";binary.write_bytes(b"binary");reads=root/"reads";reads.write_bytes(b"reads")
        args=["run_seed_experiment.py","--benchmark",str(binary),"--genome",str(genome),
              "--fastq",str(reads),"--output",str(root/label)]
        return binary,reads,args

    def test_success_failure_and_changed_producer(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp)
            for label in ("pass","execution-failure","changed-binary"):
                binary,reads,args=self.fixture(root,label)
                def execute(*a,**kw):
                    if label=="changed-binary":binary.write_bytes(b"changed producer")
                    return types.SimpleNamespace(returncode=1 if label=="execution-failure" else 0,
                        stdout=json.dumps({"status":"PASS","queries":2}),stderr="")
                with patch("sys.argv",args),patch.object(runner.subprocess,"run",side_effect=execute),contextlib.redirect_stdout(io.StringIO()):
                    if label=="pass":runner.main()
                    else:
                        with self.assertRaises(SystemExit):runner.main()
                expected={"pass":"PASS_SCOPED_FIELDS","execution-failure":"FAILED_EXECUTION","changed-binary":"FAILED_VALIDATION"}[label]
                self.assertEqual(json.loads((root/label/"result.json").read_text())["status"],expected)

    def test_input_mutation_during_hash_rejected_before_execution(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);binary,reads,args=self.fixture(root,"changed-input")
            original=runner.sha256
            def digest(path):
                result=original(path)
                if path==reads.resolve():reads.write_bytes(b"changed during hashing")
                return result
            with patch("sys.argv",args),patch.object(runner,"sha256",side_effect=digest),patch.object(runner.subprocess,"run") as run:
                with self.assertRaisesRegex(ValueError,"fingerprinting"):runner.main()
                run.assert_not_called()

    def test_same_size_preserved_timestamp_input_mutation_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);binary,reads,args=self.fixture(root,"same-stat-mutation")
            stamp=reads.stat()
            def execute(*a,**kw):
                self.assertEqual(a[0][-1],"cpu")
                reads.write_bytes(b"other")
                os.utime(reads,ns=(stamp.st_atime_ns,stamp.st_mtime_ns))
                return types.SimpleNamespace(returncode=0,stdout='{"status":"PASS"}',stderr="")
            with patch("sys.argv",args),patch.object(runner.subprocess,"run",side_effect=execute),contextlib.redirect_stdout(io.StringIO()):
                with self.assertRaises(SystemExit):runner.main()
            result=json.loads((root/"same-stat-mutation/result.json").read_text())
            self.assertEqual(result["status"],"FAILED_VALIDATION")
            self.assertEqual(result["provenance_status"],"FAILED_CHANGED_INPUT_OR_PRODUCER")

    def test_explicit_cuda_and_memory_scope(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);binary,reads,args=self.fixture(root,"cuda")
            def execute(command,**kw):
                self.assertEqual(command[-1],"cuda")
                return types.SimpleNamespace(returncode=0,stdout=json.dumps({"status":"PASS",
                    "memory":{"status":"MEASURED","peak_rss_bytes":1024}}),stderr="")
            with patch("sys.argv",args+["--executor","cuda"]),patch.object(runner.subprocess,"run",side_effect=execute),contextlib.redirect_stdout(io.StringIO()):runner.main()
            result=json.loads((root/"cuda/result.json").read_text())
            self.assertEqual(result["peak_rss_status"],"MEASURED")
            self.assertIn("NOT_PERFORMED",result["emitted_seed_verification"])

    def test_capture_abi_rejected_before_execution(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);binary,reads,args=self.fixture(root,"bad-abi")
            reads.unlink();reads.mkdir();(reads/"queries-0.bin").write_bytes(b"STARSD01")
            (reads/"capture_metadata.json").write_text(json.dumps({"format":1,"native_uint_bytes":4,
                "query_bytes":64,"match_bytes":32,"byte_order":"little"}))
            with patch("sys.argv",args),patch.object(runner.subprocess,"run") as run:
                with self.assertRaisesRegex(ValueError,"ABI"):runner.main()
                run.assert_not_called()

    def test_capture_metadata_and_stats_are_content_bound(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);binary,reads,args=self.fixture(root,"capture-stats")
            reads.unlink();reads.mkdir();(reads/"queries-0.bin").write_bytes(b"STARSD01")
            stats=reads/"queries-0.bin.json";stats.write_text('{"recorded":1}')
            metadata=reads/"capture_metadata.json"
            metadata.write_text(json.dumps({"format":1,"native_uint_bytes":8,"query_bytes":64,
                "match_bytes":32,"byte_order":runner.sys.byteorder}))
            def execute(*a,**kw):
                stats.write_text('{"recorded":2}')
                return types.SimpleNamespace(returncode=0,stdout='{"status":"PASS"}',stderr="")
            with patch("sys.argv",args),patch.object(runner.subprocess,"run",side_effect=execute),contextlib.redirect_stdout(io.StringIO()):
                with self.assertRaises(SystemExit):runner.main()
            signatures=json.loads((root/"capture-stats/input_signatures.json").read_text())
            self.assertIn(str(stats.resolve()),signatures)
            self.assertIn(str(metadata.resolve()),signatures)

    def test_foreign_effective_index_rejected_before_execution(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);binary,reads,args=self.fixture(root,"foreign-index")
            reads.unlink();reads.mkdir();(reads/"queries-0.bin").write_bytes(b"STARSD01")
            for name in ("Genome","SA","genomeParameters.txt"):(reads/name).write_bytes(b"different index")
            with patch("sys.argv",args),patch.object(runner.subprocess,"run") as run:
                with self.assertRaisesRegex(ValueError,"captured effective index"):runner.main()
                run.assert_not_called()

    def test_invalid_stdout_writes_failure_receipt(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);binary,reads,args=self.fixture(root,"invalid-json")
            with patch("sys.argv",args),patch.object(runner.subprocess,"run",return_value=
                    types.SimpleNamespace(returncode=0,stdout="incomplete JSON",stderr="")),contextlib.redirect_stdout(io.StringIO()):
                with self.assertRaises(SystemExit):runner.main()
            self.assertEqual(json.loads((root/"invalid-json/result.json").read_text())["status"],"FAILED_INVALID_OUTPUT")

    def test_capture_directory_uses_actual_index_without_prefix_index(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);binary,reads,args=self.fixture(root,"capture")
            reads.unlink();reads.mkdir();(reads/'queries-0.bin').write_bytes(b'STARSD01')
            (root/'index/SAindex').unlink()
            with patch('sys.argv',args),patch.object(runner.subprocess,'run',return_value=
                    types.SimpleNamespace(returncode=0,stdout='{"status":"PASS","queries":1}',stderr='')),contextlib.redirect_stdout(io.StringIO()):
                runner.main()
            result=json.loads((root/'capture/result.json').read_text())
            self.assertEqual(result['status'],'PASS_SCOPED_FIELDS')
            self.assertIn('post-clipping',result['scope'])

    def build_receipt(self,root,binary):
        receipt=root/'build.json';archive=receipt.with_suffix('.source.zip');content=b'captured build source'
        with zipfile.ZipFile(archive,'w') as saved:saved.writestr('test/source.cpp',content)
        sources={'test/source.cpp':hashlib.sha256(content).hexdigest()}
        receipt.write_text(json.dumps(dict(binaries={str(binary.resolve()):runner.sha256(binary)},
            source_files=sources,source_tree_sha256=hashlib.sha256(json.dumps(sources,sort_keys=True).encode()).hexdigest(),
            source_archive_sha256=runner.sha256(archive))))
        return receipt,archive

    def test_pwl_parameters_and_verified_build_receipt(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);binary,reads,args=self.fixture(root,'pwl')
            receipt,archive=self.build_receipt(root,binary)
            def execute(command,**kw):
                self.assertEqual(command[-3:],['pwl','21','256'])
                return types.SimpleNamespace(returncode=0,stdout='{"status":"PASS"}',stderr='')
            with patch('sys.argv',args+['--executor','pwl','--hint-k','21','--hint-error','256','--build-receipt',str(receipt)]),patch.object(runner.subprocess,'run',side_effect=execute),contextlib.redirect_stdout(io.StringIO()):runner.main()
            result=json.loads((root/'pwl/result.json').read_text())
            self.assertEqual(result['producer_source_status'],'VERIFIED_RECEIPT_CONTENT_AND_BINARY_BINDING')
            self.assertTrue((root/'pwl/build_receipt.source.zip').is_file())
            snapshots=json.loads((root/'pwl/source_snapshot_status.json').read_text())
            self.assertIn('NOT_ATTESTED',snapshots['status'])

    def test_build_receipt_wrong_binary_signature_or_archive_rejected(self):
        for defect in ('binary','signature','archive','source-content'):
            with self.subTest(defect=defect),tempfile.TemporaryDirectory() as tmp:
                root=Path(tmp);binary,reads,args=self.fixture(root,'bad-receipt');receipt,archive=self.build_receipt(root,binary)
                data=json.loads(receipt.read_text())
                if defect=='binary':data['binaries'][str(binary.resolve())]='wrong'
                elif defect=='signature':data['source_tree_sha256']='wrong'
                elif defect=='archive':data['source_archive_sha256']='wrong'
                else:
                    with zipfile.ZipFile(archive,'w') as saved:saved.writestr('test/source.cpp',b'different source')
                    data['source_archive_sha256']=runner.sha256(archive)
                receipt.write_text(json.dumps(data))
                with patch('sys.argv',args+['--build-receipt',str(receipt)]),patch.object(runner.subprocess,'run') as run:
                    with self.assertRaisesRegex(ValueError,'receipt'):runner.main()
                    run.assert_not_called()

    def test_no_receipt_keeps_current_source_unverified(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);binary,reads,args=self.fixture(root,'unverified')
            with patch('sys.argv',args),patch.object(runner.subprocess,'run',return_value=types.SimpleNamespace(returncode=0,stdout='{"status":"PASS"}',stderr='')),contextlib.redirect_stdout(io.StringIO()):runner.main()
            self.assertEqual(json.loads((root/'unverified/result.json').read_text())['producer_source_status'],'UNVERIFIED')

if __name__=="__main__":unittest.main()
