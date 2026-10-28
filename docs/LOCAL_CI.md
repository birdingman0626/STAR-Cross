# Local CI

`scripts/local_ci.py` runs the jobs of `.github/workflows/build.yml` on your
own machines before you push. It reads the workflow file itself: matrix
expansion, `if:` conditions, job and step `env`, and each step's `run:` script.
A local run therefore follows CI as the workflow changes.

```bash
python scripts/local_ci.py --list                         # lanes and the host each runs on
python scripts/local_ci.py --worktree --skip build-s390x  # everything but the 2-hour emulated job
python scripts/local_ci.py --only 'build-linux*'          # one job family, committed HEAD
```

- Each lane gets a fresh copy of the tree: the committed `HEAD`, or with
  `--worktree` your tracked changes too. Untracked files are left out; stage
  them first.
- The copy is made from `git ls-tree`, not `git archive`, because HTSlib's
  `.gitattributes` export-ignore rules would drop files a CI checkout keeps.
  It becomes a one-commit git repository on the host, so version stamping and
  `git ls-files` checks behave as on CI.
- Hosts run in parallel. Lanes on one host run one at a time.
- Logs, and a `summary.json`, go to `data/local-ci/<time>-<sha>/`. The exit
  code is non-zero if any lane fails.

Requires Python 3 with PyYAML on the machine that starts the run (only there).

## Hosts file

Copy `scripts/local_ci_hosts.example.json` to `local_ci_hosts.json` in the
repository root (gitignored) and fill in your machines. `runners` maps each
`runs-on` label to a host; a label with no host is reported as `SKIPPED`.

| Host `type` | Runs on | Notes |
| --- | --- | --- |
| `ssh` | Linux or macOS over key-based SSH (`target`, `key`) | Lanes run in `~/star-local-ci/<lane>`, replaced on each run |
| `lima` | a Lima VM (`vm`) on a Mac reached over SSH | The VM is started if needed, and stopped afterwards if the runner started it |
| `local` | this Windows machine (`"shell": "pwsh"`) | Steps run in `pwsh` with the Visual Studio x64 environment (found with `vswhere`) |

Host `env` values are shell expressions expanded on the host (for example a
`PATH` prefix). `step_overrides` replace (a string) or skip (`null`) a step by
`name` or `job/name` on that host only. The summary names every override, so a
substituted lane is never mistaken for an exact replay. The example uses this
for Intel Macs: Homebrew no longer supports them, so they build with Apple
clang and a source-built `libomp` (see `docs/MACOS_BUILD_HOST.md`) instead of
Homebrew GCC.

`uses:` steps are not run, except:

- `actions/checkout` of another `ref`, which is fetched from `origin`;
- `docker/setup-qemu-action`, which only checks that the host already emulates
  the platform (`qemu-user-static`);
- `ilammy/msvc-dev-cmd`, for which the local MSVC environment is loaded.

Upload and download of artifacts are skipped.

## Linux x86_64 host (Proxmox VM)

The Ubuntu jobs need `sudo` without a password: they install packages and set
`vm.mmap_rnd_bits` for the sanitizers. They also need Docker plus QEMU for
s390x, and memory overcommit. Use a dedicated VM rather than a shared machine
or an LXC container: the sanitizer and binfmt settings are kernel-wide.

Create the VM from an Ubuntu 24.04 cloud image. Run this on the Proxmox host,
substituting a free `VMID` and your public key:

```bash
wget https://cloud-images.ubuntu.com/noble/current/noble-server-cloudimg-amd64.img
qm create VMID --name star-ci-x86 --ostype l26 --cpu host --cores 8 --memory 12288 \
  --scsihw virtio-scsi-single --net0 virtio,bridge=vmbr0 --onboot 0
qm set VMID --scsi0 local-lvm:0,import-from=$PWD/noble-server-cloudimg-amd64.img,iothread=1,ssd=1,discard=on
qm disk resize VMID scsi0 80G
qm set VMID --ide2 local-lvm:cloudinit --boot order=scsi0 --serial0 socket --vga serial0
qm set VMID --ciuser ci --sshkeys ci_key.pub --ipconfig0 ip=dhcp
qm start VMID
```

Check the image against `SHA256SUMS` from the same directory. Then install
the union of the jobs' packages inside the VM:

```bash
sudo apt-get update
sudo apt-get install -y --no-install-recommends build-essential cmake ninja-build zlib1g-dev \
  clang libomp-dev libclang-rt-dev python3 git curl ca-certificates bzip2 xz-utils \
  docker.io qemu-user-static binfmt-support
sudo usermod -aG docker ci
echo vm.overcommit_memory=1 | sudo tee /etc/sysctl.d/90-ci-overcommit.conf && sudo sysctl --system
```

Memory overcommit matches GitHub's runners. Without it, the duplicate-marking
test in `scripts/test_bam_tools.py` fails in VMs with less than 16 GB, because
`--limitBAMsortRAM 0` reserves 16 GB up front.

The ARM Linux host is a Lima VM on an Apple Silicon Mac, and the macOS hosts
are set up as in `docs/MACOS_BUILD_HOST.md`.

## Coverage

With every runner mapped, all jobs in `build.yml` run locally. Still not
covered:

- the exact GitHub runner images, with their patch levels and preinstalled
  tool versions;
- artifact upload, and therefore `release.yml`'s packaging and publication;
- CodeQL;
- any substituted steps the summary lists.

GitHub CI stays the final gate.
