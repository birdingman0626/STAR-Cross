#!/usr/bin/env python3
"""Exercise the actual embedded HTTP/JSON server without submitting analysis jobs."""
import argparse
import json
from pathlib import Path
import socket
import subprocess
import tempfile
import time
import urllib.error
import urllib.request


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--star-exe", required=True, type=Path)
    args = parser.parse_args()
    executable = args.star_exe.resolve(strict=True)
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        port = sock.getsockname()[1]
    base = f"http://127.0.0.1:{port}"
    # Do not route local tests through inherited corporate HTTP proxies.
    client = urllib.request.build_opener(urllib.request.ProxyHandler({}))
    with tempfile.TemporaryDirectory(prefix="star-webui-") as directory:
        with open(Path(directory) / "server.log", "wb") as log:
            process = subprocess.Popen(
                [str(executable), "--runMode", "webui", "--webuiHost", "127.0.0.1",
                 "--webuiPort", str(port), "--outFileNamePrefix", directory + "/"],
                stdout=log, stderr=subprocess.STDOUT,
            )
            try:
                deadline = time.monotonic() + 20
                while True:
                    if process.poll() is not None:
                        raise RuntimeError("WebUI exited before becoming ready")
                    try:
                        with client.open(base + "/health", timeout=1) as response:
                            assert json.load(response) == {"status": "ok"}
                        break
                    except (urllib.error.URLError, TimeoutError):
                        if time.monotonic() >= deadline:
                            raise RuntimeError("WebUI startup timed out")
                        time.sleep(0.1)
                with client.open(base + "/props", timeout=5) as response:
                    props = json.load(response)
                    assert props["version"].startswith("STAR-Cross ")
                    assert "webui" in props["runModes"]
                with client.open(base + "/", timeout=5) as response:
                    assert "text/html" in response.headers["Content-Type"]
                    assert b"STAR-Cross" in response.read()
                with client.open(base + "/jobs", timeout=5) as response:
                    assert json.load(response) == []
                request = urllib.request.Request(
                    base + "/jobs", data=b"{invalid", headers={"Content-Type": "application/json"}
                )
                try:
                    client.open(request, timeout=5).close()
                    raise AssertionError("Malformed JSON was accepted")
                except urllib.error.HTTPError as error:
                    assert error.code == 400
                    assert json.load(error)["error"] == "Invalid JSON"
                print("PASS: WebUI health, properties, HTML, empty jobs and malformed JSON")
            finally:
                if process.poll() is None:
                    process.terminate()
                    try:
                        process.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait(timeout=5)


if __name__ == "__main__":
    main()
