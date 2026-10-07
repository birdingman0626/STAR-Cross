#!/usr/bin/env python3
"""Reassess preserved paired receipts without rerunning or altering measurements."""
import argparse
import hashlib
import json
from pathlib import Path
from qualification import paired_assessment


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--receipt", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--labels", nargs=2, required=True)
    args = parser.parse_args()
    payload = args.receipt.read_bytes()
    receipt = json.loads(payload)
    if receipt.get("status") != "PASSED_DECLARED_RAW_ARTIFACTS" or not all(
            run.get("exit_code") == 0 and run.get("full_scientific_contract") == "PASS" for run in receipt.get("runs", [])):
        raise ValueError("Incomplete or scientifically unqualified receipt")
    result = {"measurement_receipt_sha256": hashlib.sha256(payload).hexdigest(),
              "assessment_code_sha256": hashlib.sha256(Path(__file__).with_name("qualification.py").read_bytes()).hexdigest(),
              "assessment": paired_assessment(receipt["runs"], args.labels),
              "scope": "Preserved measurements; no new performance run or environment qualification"}
    with args.output.open("x") as stream:
        json.dump(result, stream, indent=2)
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
