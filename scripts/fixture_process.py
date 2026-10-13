"""Bound miniature regression processes while retaining live diagnostic output."""
import os
from pathlib import Path
import signal
import subprocess


def run_fixture(command, log_path, *, env=None, timeout=180):
    log_path = Path(log_path)
    # A file avoids communicate() waiting on a pipe inherited by a read producer
    # after STAR has exited. Timeout must fail the test, never accept its output.
    with log_path.open("w") as log:
        process = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT,
                                   env=env, start_new_session=os.name != "nt")
        try:
            code = process.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            if os.name == "nt":
                try:
                    subprocess.run(["taskkill", "/PID", str(process.pid), "/T", "/F"],
                                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                                   timeout=10, check=False)
                finally:
                    process.kill()
                    process.wait(timeout=10)
            else:
                try:
                    os.killpg(process.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
            process.kill()
            process.wait(timeout=10)
            raise RuntimeError(f"Fixture exceeded {timeout}s: {command!r}; log: {log_path}") from None
    return subprocess.CompletedProcess(command, code, log_path.read_text(errors="replace"))
