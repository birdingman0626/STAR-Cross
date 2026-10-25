#!/usr/bin/env python3
"""Exercise the actual embedded HTTP/JSON server: endpoints, the request guard
(Host/Origin/Content-Type), runMode allow-listing and, on POSIX, job queueing,
cancellation and reaping with real STAR child processes."""
import argparse
import json
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import threading
import time
import urllib.error
import urllib.request


class Server:
    def __init__(self, executable, directory):
        with socket.socket() as sock:
            sock.bind(("127.0.0.1", 0))
            self.port = sock.getsockname()[1]
        self.base = f"http://127.0.0.1:{self.port}"
        self.directory = Path(directory)
        # Do not route local tests through inherited corporate HTTP proxies.
        self.client = urllib.request.build_opener(urllib.request.ProxyHandler({}))
        self.log = open(self.directory / "server.log", "wb")
        self.process = subprocess.Popen(
            [str(executable), "--runMode", "webui", "--webuiHost", "127.0.0.1",
             "--webuiPort", str(self.port), "--outFileNamePrefix", str(self.directory) + "/"],
            stdout=self.log, stderr=subprocess.STDOUT,
        )

    def request(self, path, data=None, headers=None, method=None):
        """Returns (status, parsed JSON or raw bytes)."""
        request = urllib.request.Request(self.base + path, data=data, headers=headers or {}, method=method)
        try:
            with self.client.open(request, timeout=10) as response:
                status, body, kind = response.status, response.read(), response.headers.get("Content-Type", "")
        except urllib.error.HTTPError as error:
            status, body, kind = error.code, error.read(), error.headers.get("Content-Type", "")
        return status, (json.loads(body) if "json" in kind else body)

    def post_json(self, path, value, headers=None):
        return self.request(path, json.dumps(value).encode(),
                            {"Content-Type": "application/json", **(headers or {})})

    def wait_ready(self):
        deadline = time.monotonic() + 20
        while True:
            if self.process.poll() is not None:
                raise RuntimeError("WebUI exited before becoming ready")
            try:
                if self.request("/health")[1] == {"status": "ok"}:
                    return
            except (urllib.error.URLError, TimeoutError, ConnectionError):
                pass
            if time.monotonic() >= deadline:
                raise RuntimeError("WebUI startup timed out")
            time.sleep(0.1)

    def persisted(self):
        path = self.directory / ".webui_jobs.jsonl"
        if not path.is_file():
            return {}
        return {record["id"]: record for record in map(json.loads, path.read_text().splitlines()) if record}

    def stop(self):
        if self.process.poll() is None:
            self.process.terminate()
            try:
                self.process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait(timeout=5)
        self.log.close()


def check_endpoints(server):
    status, props = server.request("/props")
    assert status == 200 and props["version"].startswith("STAR-Cross ") and "webui" in props["runModes"]
    status, html = server.request("/")
    assert status == 200 and b"STAR-Cross" in html
    # Server-provided paths must not be spliced into inline handlers.
    assert b'onclick="applyCRRef' not in html and b'onclick="switchToGenomeGenerate' not in html
    assert b"function esc(" in html
    assert server.request("/jobs") == (200, [])
    status, error = server.request("/jobs", b"{invalid", {"Content-Type": "application/json"})
    assert (status, error["error"]) == (400, "Invalid JSON"), (status, error)
    assert server.request("/jobs/999999999")[0] == 404


def check_guard(server):
    job = {"runMode": "alignReads", "outFileNamePrefix": str(server.directory / "guard"),
           "genomeDir": str(server.directory / "missing"), "readFilesIn": ["r.fq"]}
    # A browser may send text/plain cross-site without a CORS preflight.
    status, error = server.request("/jobs", json.dumps(job).encode(), {"Content-Type": "text/plain"})
    assert status == 415, (status, error)
    status, error = server.post_json("/jobs", job, {"Origin": "http://evil.example"})
    assert status == 403 and "Cross-origin" in error["error"], (status, error)
    # DNS rebinding: the attacker's hostname resolves to loopback.
    status, _ = server.request("/genome/scan?base=" + urllib.request.quote(str(server.directory)),
                               headers={"Host": f"attacker.example:{server.port}"})
    assert status == 403, status
    status, error = server.post_json("/jobs", {**job, "runMode": "<img src=x onerror=alert(1)>"})
    assert (status, error["error"]) == (400, "Unsupported runMode"), (status, error)
    status, error = server.post_json("/jobs/1/cancel", {}, {"Origin": "http://evil.example"})
    assert status == 403, status
    # The page's own origin and Origin-less clients remain allowed.
    assert server.request("/jobs", headers={"Origin": server.base})[0] == 200
    assert not (server.directory / "guard").exists(), "a rejected request spawned a job"


def wait_for(predicate, timeout, what):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        value = predicate()
        if value:
            return value
        time.sleep(0.2)
    raise AssertionError(f"timed out waiting for {what}")


def zombie_children(pid):
    zombies = []
    for stat in Path("/proc").glob("[0-9]*/stat"):
        try:
            fields = stat.read_text().rsplit(")", 1)[1].split()
        except OSError:
            continue
        if fields[0] == "Z" and int(fields[1]) == pid:
            zombies.append(stat.parent.name)
    return zombies


def check_queue(server, executable):
    """Blocking jobs read from a FIFO; nothing below polls GET /jobs while waiting."""
    root = server.directory
    genome = root / "genome"
    genome.mkdir()
    (root / "ref.fa").write_text(">chr1\n" + "ACGTTGCAAGCTTAGCCATG" * 500 + "\n")
    subprocess.run([str(executable), "--runMode", "genomeGenerate", "--genomeDir", str(genome),
                    "--genomeFastaFiles", str(root / "ref.fa"), "--genomeSAindexNbases", "4",
                    "--outFileNamePrefix", str(root) + "/genome_"],
                   check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=120)

    def blocking_job(name):
        fifo = root / f"{name}.fifo"
        os.mkfifo(fifo)
        return fifo, {"runMode": "alignReads", "outFileNamePrefix": str(root / name),
                      "genomeDir": str(genome), "readFilesIn": [str(fifo)], "generateReport": False}

    def failing_job(name):  # exits immediately once started
        return {"runMode": "alignReads", "outFileNamePrefix": str(root / name),
                "genomeDir": str(root / "missing-genome"), "readFilesIn": [str(root / "none.fq")],
                "generateReport": False}

    def release(fifo):  # opening for write unblocks STAR's open(); closing gives EOF
        threading.Thread(target=lambda: open(fifo, "w").close(), daemon=True).start()

    fifo_a, job_a = blocking_job("a")
    status, a = server.post_json("/jobs", job_a)
    assert status == 201 and a["state"] == "running", (status, a)
    status, b = server.post_json("/jobs", failing_job("b"))
    assert status == 201 and b["state"] == "queued", (status, b)
    status, b = server.post_json(f"/jobs/{b['id']}/cancel", {})
    assert status == 200 and b["state"] == "cancelled", (status, b)
    status, c = server.post_json("/jobs", failing_job("c"))
    assert status == 201 and c["state"] == "queued", (status, c)
    release(fifo_a)
    # The background poller must finish A and run C without any client polling.
    records = wait_for(lambda: c["id"] in server.persisted() and server.persisted(), 30, "queued job promotion")
    assert records[a["id"]]["state"] in ("succeeded", "failed")
    assert records[b["id"]]["state"] == "cancelled"
    assert records[c["id"]]["state"] == "failed" and records[c["id"]]["exitCode"] != 0
    assert (root / "c" / "WebUI_console.log").is_file(), "job console output not captured"

    fifo_d, job_d = blocking_job("d")
    status, d = server.post_json("/jobs", job_d)
    assert status == 201 and d["state"] == "running", (status, d)
    status, e = server.post_json("/jobs", failing_job("e"))
    assert status == 201 and e["state"] == "queued", (status, e)
    time.sleep(1)  # let D reach its blocking read
    status, d = server.post_json(f"/jobs/{d['id']}/cancel", {})
    assert status == 200 and d["state"] == "cancelled", (status, d)
    wait_for(lambda: e["id"] in server.persisted(), 30, "promotion after cancelling the running job")
    if sys.platform.startswith("linux"):
        wait_for(lambda: not zombie_children(server.process.pid), 10, "cancelled job to be reaped")
    status, jobs = server.request("/jobs")
    states = {job["id"]: job["state"] for job in jobs}
    assert states[d["id"]] == "cancelled" and states[e["id"]] == "failed", states
    try:  # in case the cancel did not stop D, never leave it blocked
        release(fifo_d)
    except OSError:
        pass


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--star-exe", required=True, type=Path)
    args = parser.parse_args()
    executable = args.star_exe.resolve(strict=True)
    with tempfile.TemporaryDirectory(prefix="star-webui-") as directory:
        server = Server(executable, directory)
        try:
            server.wait_ready()
            check_endpoints(server)
            check_guard(server)
            checks = "health, properties, HTML, jobs, malformed JSON, Host/Origin/Content-Type guard, runMode allow-list"
            if hasattr(os, "mkfifo"):
                check_queue(server, executable)
                checks += ", queue promotion, queued/running cancel, reaping"
            else:
                checks += " (queue checks need POSIX FIFOs; skipped)"
            print("PASS: WebUI " + checks)
        finally:
            server.stop()


if __name__ == "__main__":
    main()
