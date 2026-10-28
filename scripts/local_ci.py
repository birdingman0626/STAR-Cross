#!/usr/bin/env python3
"""Run the jobs of .github/workflows/build.yml on your own machines.

The steps come from the workflow file itself, so a local run follows CI as the
workflow changes. A hosts file (see scripts/local_ci_hosts.example.json) maps
each GitHub runner label to a machine:

  ssh    a Linux or macOS machine reached with key-based SSH
  lima   a Lima VM on a Mac, reached through SSH to the Mac
  local  this machine (Windows: steps run in pwsh inside the MSVC environment)

Runner labels without a host are reported as SKIPPED. Each job runs in a fresh
copy of the tree: the committed HEAD, or with --worktree the tracked changes as
well (untracked files are left out; stage them first).

  python scripts/local_ci.py --list
  python scripts/local_ci.py                       # every mapped job
  python scripts/local_ci.py --only 'build-linux*' --skip build-s390x

Logs and a summary go to data/local-ci/<time>-<sha>/. `uses:` steps are not
run, except checkout of another ref (cloned from origin), docker/setup-qemu-action
(checked, the host must already emulate the platform) and ilammy/msvc-dev-cmd
(the local MSVC environment is loaded).
"""
import argparse
import concurrent.futures
import datetime
import fnmatch
import io
import itertools
import json
import os
import re
import shlex
import subprocess
import sys
import tarfile
import threading
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
WS = "\x00WORKSPACE\x00"  # placeholder for the job's workspace path


# ---------------------------------------------------------------- expressions
_TOKEN = re.compile(r"\s*(?:('(?:[^']|'')*')|(&&|\|\||==|!=|!|\(|\))|([A-Za-z_][\w.-]*\(\))|([A-Za-z_][\w.-]*)|(\d+(?:\.\d+)?))")
_FUNCS = {"success()": True, "always()": True, "failure()": False, "cancelled()": False}


def evaluate(expr, ctx):
    """Evaluate a GitHub expression with literals, context paths, ==, !=, !, &&, || and status functions."""
    out, pos, prev_value = [], 0, False
    expr = expr.strip()
    while pos < len(expr):
        m = _TOKEN.match(expr, pos)
        if not m or m.end() == pos:
            raise ValueError(f"unsupported expression: {expr!r}")
        pos = m.end()
        string, op, func, path, number = m.groups()
        if op == "(" and prev_value:
            raise ValueError(f"unsupported call in expression: {expr!r}")
        prev_value = op is None or op == ")"
        if string is not None:
            out.append(repr(string[1:-1].replace("''", "'")))
        elif op is not None:
            out.append({"&&": " and ", "||": " or ", "!": " not "}.get(op, f" {op} "))
        elif func is not None:
            if func not in _FUNCS:
                raise ValueError(f"unsupported function {func} in {expr!r}")
            out.append(repr(_FUNCS[func]))
        elif path is not None:
            if path in ("true", "false", "null"):
                out.append({"true": "True", "false": "False", "null": "None"}[path])
            else:
                out.append(repr(lookup(path, ctx)))
        else:
            out.append(number)
    try:
        return eval("".join(out), {"__builtins__": {}}, {})  # only literals and operators remain
    except Exception as e:
        raise ValueError(f"cannot evaluate {expr!r}: {e}") from None


def lookup(path, ctx):
    value = ctx
    for part in path.split("."):
        if isinstance(value, dict) and part in value:
            value = value[part]
        else:
            return ""  # GitHub yields an empty value for missing properties
    return value


def substitute(text, ctx):
    """Replace ${{ expr }} in a string."""
    def repl(m):
        value = evaluate(m.group(1), ctx)
        if value is True or value is False:
            return "true" if value else "false"
        return "" if value is None else str(value)
    return re.sub(r"\$\{\{(.*?)\}\}", repl, str(text))


# ---------------------------------------------------------------- workflow
def load_workflow(path):
    import yaml  # only the runner needs PyYAML; the unit tests pass dicts
    with open(path, encoding="utf-8") as f:
        return yaml.safe_load(f)


def expand_matrix(strategy):
    matrix = dict((strategy or {}).get("matrix") or {})
    include = matrix.pop("include", []) or []
    exclude = matrix.pop("exclude", []) or []
    if isinstance(matrix, str):
        raise ValueError("dynamic matrices are not supported")
    keys = list(matrix)
    combos = [dict(zip(keys, values)) for values in itertools.product(*(matrix[k] for k in keys))] if keys else []
    combos = [c for c in combos if not any(all(c.get(k) == v for k, v in ex.items()) for ex in exclude)]
    combos += [dict(i) for i in include]
    return combos or [{}]


def _flatten(value):
    if isinstance(value, dict):
        return [x for v in value.values() for x in _flatten(v)]
    if isinstance(value, list):
        return [x for v in value for x in _flatten(v)]
    return [str(value)]


def lanes_from_workflow(workflow):
    lanes = []
    wf_env = workflow.get("env") or {}
    for job_id, job in (workflow.get("jobs") or {}).items():
        if "steps" not in job:
            continue  # reusable-workflow calls
        for matrix in expand_matrix(job.get("strategy")):
            values = _flatten(matrix)
            name = job_id + (" (" + ", ".join(values) + ")" if values else "")
            lane_id = re.sub(r"[^A-Za-z0-9._-]+", "-", job_id + ("-" + "-".join(values) if values else "")).strip("-")
            ctx = {"matrix": matrix, "github": {"workspace": WS, "event_name": "push", "ref": "refs/heads/main"},
                   "runner": {}, "inputs": {}}
            env = {}
            for k, v in {**wf_env, **(job.get("env") or {})}.items():
                env[k] = substitute(v, {**ctx, "env": env})
            ctx["env"] = env
            lanes.append({"id": lane_id, "name": name, "job": job_id, "matrix": matrix,
                          "runs_on": substitute(job.get("runs-on", ""), ctx), "env": env, "ctx": ctx,
                          "steps": job.get("steps") or []})
    return lanes


# ---------------------------------------------------------------- step plans
def plan_steps(lane, host, origin_url):
    """Return [(name, kind, payload, step_env, note)] where kind is run/skip and payload is script text."""
    shell = "pwsh" if host.get("shell") == "pwsh" else "bash"
    overrides = host.get("step_overrides") or {}
    plan = []
    for i, step in enumerate(lane["steps"], 1):
        ctx = lane["ctx"]
        name = step.get("name") or step.get("uses") or step.get("run", "").splitlines()[0][:60]
        cond = step.get("if")
        if cond is not None:
            cond = str(cond).strip()
            cond = cond[3:-2] if cond.startswith("${{") and cond.endswith("}}") else cond
            if not evaluate(cond, ctx):
                plan.append((name, "skip", None, {}, f"if: {cond}"))
                continue
        step_env = {k: substitute(v, ctx) for k, v in (step.get("env") or {}).items()}
        key = f"{lane['job']}/{name}" if f"{lane['job']}/{name}" in overrides else name
        if key in overrides:
            replacement = overrides[key]
            if replacement is None:
                plan.append((name, "skip", None, {}, "skipped by host override"))
            else:
                plan.append((name, "run", replacement, step_env, "replaced by host override"))
            continue
        if "uses" in step:
            plan.append(_plan_action(step, name, ctx, shell, origin_url))
            continue
        script = substitute(step["run"], ctx)
        script = script.replace(WS, "$env:GITHUB_WORKSPACE" if shell == "pwsh" else "${GITHUB_WORKSPACE}")
        if step.get("working-directory"):
            wd = substitute(step["working-directory"], ctx)
            script = f"cd {shlex.quote(wd)}\n" + script
        plan.append((name, "run", script, step_env, ""))
    return plan


def _plan_action(step, name, ctx, shell, origin_url):
    action = step["uses"].split("@")[0]
    params = {k: substitute(v, ctx) for k, v in (step.get("with") or {}).items()}
    if action == "actions/checkout":
        if not params.get("ref") and not params.get("path"):
            return (name, "skip", None, {}, "tree already copied")
        if shell != "bash":
            raise ValueError("checkout of another ref is only supported on bash hosts")
        path, ref = params.get("path") or ".", params.get("ref") or "HEAD"
        script = (f"rm -rf {shlex.quote(path)} && git init -q {shlex.quote(path)}\n"
                  f"git -C {shlex.quote(path)} fetch -q --depth 1 {shlex.quote(origin_url)} {shlex.quote(ref)}\n"
                  f"git -C {shlex.quote(path)} -c advice.detachedHead=false checkout -q FETCH_HEAD\n")
        return (name, "run", script, {}, f"git fetch {ref} from origin")
    if action == "docker/setup-qemu-action":
        plats = [p.strip() for p in params.get("platforms", "").split(",") if p.strip()]
        checks = "\n".join(f"test -e /proc/sys/fs/binfmt_misc/qemu-{shlex.quote(p)} || "
                           f"{{ echo 'host does not emulate {p}: install qemu-user-static'; exit 1; }}" for p in plats)
        return (name, "run", checks or "true", {}, "checked host QEMU emulation")
    if action == "ilammy/msvc-dev-cmd":
        return (name, "skip", None, {}, "MSVC environment loaded for the whole job")
    return (name, "skip", None, {}, f"action {action} not emulated")


def _bash_value(value):
    parts = str(value).split(WS)
    return '"' + "${GITHUB_WORKSPACE}".join(p.replace("\\", "\\\\").replace('"', '\\"').replace("$", "\\$").replace("`", "\\`") for p in parts) + '"'


def _pwsh_value(value):
    parts = str(value).split(WS)
    return '"' + "$env:GITHUB_WORKSPACE".join(p.replace("`", "``").replace('"', '`"').replace("$", "`$") for p in parts) + '"'


def bash_bundle_files(lane, host, plan):
    """Driver and per-step scripts for a bash host, as {relative path: text}."""
    files = {}
    lines = ["#!/usr/bin/env bash", "# Generated by scripts/local_ci.py", "set -u",
             'cd "$(dirname "$0")/.."', 'export GITHUB_WORKSPACE="$PWD" CI=true',
             'export GITHUB_ENV="$GITHUB_WORKSPACE/.local-ci/github_env"; : > "$GITHUB_ENV"']
    for k, v in (host.get("env") or {}).items():  # host env: shell expressions, expanded on the host
        lines.append(f'export {k}="{v}"')
    for k, v in lane["env"].items():
        lines.append(f"export {k}={_bash_value(v)}")
    lines += [
        'if [ ! -d .git ] && command -v git >/dev/null; then',
        '  git init -q && git add -A -f && git -c user.name=local-ci -c user.email=local-ci@localhost commit -qm "local CI tree" --no-verify',
        "fi",
        "run_step() {",
        '  echo "::local-ci:: step $1 start: $2"',
        '  ( while IFS= read -r line; do [ -n "$line" ] && export "$line"; done < "$GITHUB_ENV"',
        '    cd "$GITHUB_WORKSPACE" && bash --noprofile --norc -e ".local-ci/steps/$1.sh" )',
        '  rc=$?; echo "::local-ci:: step $1 exit $rc"; return $rc',
        "}"]
    for i, (name, kind, script, step_env, note) in enumerate(plan, 1):
        n = f"{i:02d}"
        if kind == "skip":
            lines.append(f"echo {shlex.quote(f'::local-ci:: step {n} skip: {name} ({note})')}")
            continue
        body = ["# " + name] + [f"export {k}={_bash_value(v)}" for k, v in step_env.items()] + [script]
        files[f".local-ci/steps/{n}.sh"] = "\n".join(body) + "\n"
        lines.append(f"run_step {n} {shlex.quote(name)} || exit $?")
    lines.append('echo "::local-ci:: job passed"')
    files[".local-ci/run.sh"] = "\n".join(lines) + "\n"
    return files


# ---------------------------------------------------------------- transport
def tree_files(treeish):
    """Yield (path, mode, bytes) for every file in treeish, like a checkout.

    Not `git archive`: it drops paths marked export-ignore (HTSlib's own
    .gitattributes does this), which a CI checkout keeps.
    """
    listing = subprocess.run(["git", "-C", str(ROOT), "ls-tree", "-r", "-z", "--full-tree", treeish],
                             check=True, stdout=subprocess.PIPE).stdout.split(b"\0")
    entries = []
    for item in filter(None, listing):
        meta, path = item.split(b"\t", 1)
        mode, kind, sha = meta.split()
        if kind == b"blob":
            entries.append((path.decode(), mode.decode(), sha.decode()))
    batch = subprocess.Popen(["git", "-C", str(ROOT), "cat-file", "--batch"], stdin=subprocess.PIPE, stdout=subprocess.PIPE)
    try:
        for path, mode, sha in entries:
            batch.stdin.write(sha.encode() + b"\n")
            batch.stdin.flush()
            size = int(batch.stdout.readline().split()[2])
            data = batch.stdout.read(size)
            batch.stdout.read(1)  # trailing newline
            yield path, mode, data
    finally:
        batch.stdin.close()
        batch.wait()


def make_bundle(treeish, extra_files):
    buf = io.BytesIO()
    now = int(time.time())
    with tarfile.open(fileobj=buf, mode="w:gz") as dst:
        for path, mode, data in tree_files(treeish):
            info = tarfile.TarInfo(path)
            info.mtime = now
            if mode == "120000":
                info.type, info.linkname = tarfile.SYMTYPE, data.decode()
                dst.addfile(info)
            else:
                info.size, info.mode = len(data), 0o755 if mode == "100755" else 0o644
                dst.addfile(info, io.BytesIO(data))
        for rel, text in extra_files.items():
            data = text.encode()
            info = tarfile.TarInfo(rel)
            info.size, info.mode, info.mtime = len(data), 0o755, int(time.time())
            dst.addfile(info, io.BytesIO(data))
    return buf.getvalue()


def ssh_base(host):
    cmd = ["ssh", "-o", "BatchMode=yes", "-o", "StrictHostKeyChecking=accept-new", "-o", "ServerAliveInterval=60"]
    if host.get("key"):
        cmd += ["-i", os.path.expanduser(host["key"])]
    return cmd


def run_logged(cmd, log, timeout=None, **kw):
    """Run cmd, append its output to log, return (exit code, last step line)."""
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, **kw)
    last_start = ""
    timer = threading.Timer(timeout, proc.kill) if timeout else None
    if timer:
        timer.start()
    try:
        for raw in proc.stdout:
            line = raw.decode("utf-8", "replace")
            log.write(line)
            log.flush()
            if line.startswith("::local-ci:: step") and " start: " in line:
                last_start = line.split(" start: ", 1)[1].strip()
        return proc.wait(), last_start
    finally:
        if timer:
            timer.cancel()


def run_remote(lane, host, bundle, log, timeout):
    remote_dir = f"star-local-ci/{lane['id']}"
    tmp = f"/tmp/star-local-ci-{lane['id']}.tgz"
    prep = (f"rm -rf ~/{remote_dir} && mkdir -p ~/{remote_dir} && tar xzf {tmp} -C ~/{remote_dir} "
            f"&& rm -f {tmp} && bash ~/{remote_dir}/.local-ci/run.sh")
    target = host["target"]
    if host["type"] == "ssh":
        up = subprocess.run(ssh_base(host) + [target, f"cat > {tmp}"], input=bundle, capture_output=True)
        if up.returncode:
            log.write(up.stderr.decode(errors="replace"))
            return up.returncode, "upload"
        return run_logged(ssh_base(host) + [target, prep], log, timeout)
    if host["type"] == "lima":
        limactl, vm = host.get("limactl", "limactl"), host["vm"]
        up = subprocess.run(ssh_base(host) + [target, f"cat > {tmp}"], input=bundle, capture_output=True)
        if up.returncode:
            log.write(up.stderr.decode(errors="replace"))
            return up.returncode, "upload"
        cmd = (f"{limactl} copy {tmp} {vm}:{tmp} && rm -f {tmp} && "
               f"{limactl} shell {vm} bash -c {shlex.quote(prep)}")
        return run_logged(ssh_base(host) + [target, cmd], log, timeout)
    raise ValueError(f"unknown host type {host['type']}")


def lima_state(host):
    out = subprocess.run(ssh_base(host) + [host["target"], f"{host.get('limactl', 'limactl')} list --format '{{{{.Status}}}}' {host['vm']}"],
                         capture_output=True, text=True)
    return out.stdout.strip()


def lima_power(host, action):
    subprocess.run(ssh_base(host) + [host["target"], f"{host.get('limactl', 'limactl')} {action} --tty=false {host['vm']}"
                                     if action == "start" else f"{host.get('limactl', 'limactl')} stop {host['vm']}"],
                   capture_output=True)


def msvc_environment():
    vswhere = Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")) / "Microsoft Visual Studio/Installer/vswhere.exe"
    install = subprocess.run([str(vswhere), "-latest", "-prerelease", "-products", "*", "-requires",
                              "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property", "installationPath"],
                             capture_output=True, text=True, check=True).stdout.strip()
    vcvars = Path(install) / "VC/Auxiliary/Build/vcvars64.bat"
    out = subprocess.run(f'cmd /s /c ""{vcvars}" >nul && set"', capture_output=True, text=True, shell=True, check=True).stdout
    return dict(line.split("=", 1) for line in out.splitlines() if "=" in line)


def run_local_windows(lane, host, plan, bundle, workdir, log, timeout):
    import shutil
    ws = workdir / lane["id"]
    if ws.exists():
        shutil.rmtree(ws)
    ws.mkdir(parents=True)
    with tarfile.open(fileobj=io.BytesIO(bundle), mode="r:gz") as tar:
        tar.extractall(ws, filter="data")
    env = msvc_environment() if any(s.get("uses", "").startswith("ilammy/msvc-dev-cmd") for s in lane["steps"]) else dict(os.environ)
    env.update({"GITHUB_WORKSPACE": str(ws), "CI": "true"})
    env.update({k: str(v).replace(WS, str(ws)) for k, v in lane["env"].items()})
    if shutil.which("git"):
        subprocess.run("git init -q && git add -A -f && git -c user.name=local-ci -c user.email=local-ci@localhost "
                       'commit -qm "local CI tree" --no-verify', cwd=ws, shell=True, capture_output=True)
    github_env = ws / ".local-ci" / "github_env"
    github_env.parent.mkdir(exist_ok=True)
    github_env.write_text("")
    deadline = time.time() + timeout if timeout else None
    for i, (name, kind, script, step_env, note) in enumerate(plan, 1):
        if kind == "skip":
            log.write(f"::local-ci:: step {i:02d} skip: {name} ({note})\n")
            continue
        log.write(f"::local-ci:: step {i:02d} start: {name}\n")
        log.flush()
        step_file = ws / ".local-ci" / f"step{i:02d}.ps1"
        step_file.write_text("$ErrorActionPreference = 'stop'\n" + script +
                             "\nif ((Test-Path -LiteralPath variable:\\LASTEXITCODE)) { exit $LASTEXITCODE }\n", encoding="utf-8")
        step_env_full = dict(env)
        for line in github_env.read_text().splitlines():
            if "=" in line:
                k, v = line.split("=", 1)
                step_env_full[k] = v
        step_env_full.update({k: str(v).replace(WS, str(ws)) for k, v in step_env.items()})
        step_env_full["GITHUB_ENV"] = str(github_env)
        remaining = max(1, deadline - time.time()) if deadline else None
        rc, _ = run_logged(["pwsh", "-NoProfile", "-NonInteractive", "-File", str(step_file)], log, remaining,
                           cwd=ws, env=step_env_full)
        log.write(f"::local-ci:: step {i:02d} exit {rc}\n")
        if rc:
            return rc, name
    log.write("::local-ci:: job passed\n")
    return 0, ""


# ---------------------------------------------------------------- main
def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--hosts", default=str(ROOT / "local_ci_hosts.json"), help="hosts file (default: local_ci_hosts.json)")
    ap.add_argument("--workflow", default=str(ROOT / ".github/workflows/build.yml"))
    ap.add_argument("--only", action="append", default=[], help="run only lanes matching this glob (repeatable)")
    ap.add_argument("--skip", action="append", default=[], help="skip lanes matching this glob (repeatable)")
    ap.add_argument("--worktree", action="store_true", help="include uncommitted changes to tracked files")
    ap.add_argument("--list", action="store_true", help="show lanes and their hosts, run nothing")
    ap.add_argument("--timeout", type=float, default=240, help="minutes per lane (default 240)")
    args = ap.parse_args(argv)

    hosts_cfg = json.loads(Path(args.hosts).read_text(encoding="utf-8"))
    runner_map, hosts = hosts_cfg.get("runners", {}), hosts_cfg.get("hosts", {})
    lanes = lanes_from_workflow(load_workflow(args.workflow))
    for lane in lanes:
        lane["host"] = runner_map.get(lane["runs_on"])
        matched = (not args.only or any(fnmatch.fnmatch(lane["id"], p) for p in args.only))
        lane["selected"] = matched and not any(fnmatch.fnmatch(lane["id"], p) for p in args.skip)

    if args.list:
        for lane in lanes:
            where = lane["host"] or "-- no host (SKIPPED)"
            print(f"{'*' if lane['selected'] else ' '} {lane['id']:<55} {lane['runs_on']:<18} {where}")
        return 0

    treeish = "HEAD"
    if args.worktree:
        stash = subprocess.run(["git", "-C", str(ROOT), "stash", "create"], capture_output=True, text=True, check=True).stdout.strip()
        treeish = stash or "HEAD"
    sha = subprocess.run(["git", "-C", str(ROOT), "rev-parse", "--short", treeish], capture_output=True, text=True, check=True).stdout.strip()
    origin = subprocess.run(["git", "-C", str(ROOT), "remote", "get-url", "origin"], capture_output=True, text=True).stdout.strip()
    stamp = datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    outdir = ROOT / "data" / "local-ci" / f"{stamp}-{sha}{'-dirty' if args.worktree and treeish != 'HEAD' else ''}"
    outdir.mkdir(parents=True)
    print(f"local CI of {sha}{' + uncommitted changes' if treeish != 'HEAD' else ''}; logs in {outdir}")

    results, lock = {}, threading.Lock()

    def report(lane, status, seconds=0.0, step="", note=""):
        with lock:
            results[lane["id"]] = {"lane": lane["id"], "runs_on": lane["runs_on"], "host": lane["host"],
                                   "status": status, "seconds": round(seconds), "failed_step": step, "note": note}
            print(f"  {status:<7} {lane['id']}" + (f"  [{step}]" if step else "") + (f"  ({note})" if note else ""), flush=True)

    by_host = {}
    for lane in lanes:
        if not lane["selected"]:
            continue
        if not lane["host"]:
            report(lane, "SKIPPED", note=f"no host for {lane['runs_on']}")
        elif lane["host"] not in hosts:
            report(lane, "ERROR", note=f"host {lane['host']} not defined")
        else:
            by_host.setdefault(lane["host"], []).append(lane)

    def run_host(host_name, host_lanes):
        host = hosts[host_name]
        started_vm = False
        if host["type"] == "lima" and lima_state(host) != "Running":
            lima_power(host, "start")
            started_vm = True
        try:
            for lane in host_lanes:
                t0 = time.time()
                try:
                    plan = plan_steps(lane, host, origin)
                    overridden = [n for n, k, s, e, note in plan if "override" in note]
                    note = ("host overrides: " + ", ".join(overridden)) if overridden else ""
                    files = {} if host["type"] == "local" else bash_bundle_files(lane, host, plan)
                    bundle = make_bundle(treeish, files)
                    with open(outdir / f"{lane['id']}.log", "w", encoding="utf-8") as log:
                        if host["type"] == "local":
                            rc, step = run_local_windows(lane, host, plan, bundle, ROOT / "data" / "local-ci" / "work", log, args.timeout * 60)
                        else:
                            rc, step = run_remote(lane, host, bundle, log, args.timeout * 60)
                    report(lane, "PASS" if rc == 0 else "FAIL", time.time() - t0, "" if rc == 0 else step, note)
                except Exception as e:  # report and continue with the next lane
                    report(lane, "ERROR", time.time() - t0, note=str(e))
        finally:
            if started_vm and host.get("stop_after_if_started", True):
                lima_power(host, "stop")

    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, len(by_host))) as pool:
        for f in [pool.submit(run_host, h, ls) for h, ls in by_host.items()]:
            f.result()

    summary = [results[l["id"]] for l in lanes if l["id"] in results]
    (outdir / "summary.json").write_text(json.dumps({"tree": sha, "worktree": treeish != "HEAD", "lanes": summary}, indent=2))
    print(f"\n{'lane':<55} {'host':<12} {'result':<8} {'min':>5}")
    for r in summary:
        print(f"{r['lane']:<55} {str(r['host'] or '-'):<12} {r['status']:<8} {r['seconds'] / 60:>5.1f}"
              + (f"  failed at: {r['failed_step']}" if r["failed_step"] else ""))
    return 1 if any(r["status"] in ("FAIL", "ERROR") for r in summary) else 0


if __name__ == "__main__":
    sys.exit(main())
