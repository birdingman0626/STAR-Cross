import contextlib
import io
import json
from pathlib import Path
import tempfile
import types
import unittest
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

if __name__=="__main__":unittest.main()
