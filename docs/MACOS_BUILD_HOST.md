# Setting up a macOS build host

How to prepare a fresh Mac so STAR-Cross can be built and tested on it over
SSH from a workstation. The steps were worked out on an Intel Mac (macOS 15.8,
x86_64) and apply to Apple Silicon too, where noted.

Placeholders used below:

| Placeholder | Meaning | Example |
| --- | --- | --- |
| `MAC` | the Mac's address | `192.168.1.9` |
| `MACUSER` | the login account on the Mac | `pve` |
| `KEY` | the private SSH key on the workstation | `~/.ssh/vn_mac_build` |

Run the workstation commands from Git Bash, macOS Terminal or a Linux shell.
Commands marked **(on the Mac)** can also be typed into Terminal on the Mac.

macOS is licensed only for Apple-branded hardware; use a real Mac (or a
hosted macOS runner) as the build host.

## 1. Key-based SSH login (you, once)

Remote Login must be on (System Settings > General > Sharing > Remote Login).
Then copy your public key to the Mac. This is the only step that needs the
account password:

```bash
cat KEY.pub | ssh MACUSER@MAC "mkdir -p ~/.ssh && chmod 700 ~/.ssh && cat >> ~/.ssh/authorized_keys && chmod 600 ~/.ssh/authorized_keys"
```

Check that the key works without a password prompt:

```bash
ssh -o BatchMode=yes -i KEY MACUSER@MAC 'sw_vers; uname -m'
```

Optional but recommended once the key works: turn off password logins over
SSH, or at least give the account a strong password.

```bash
ssh -t -i KEY MACUSER@MAC "printf 'PasswordAuthentication no\nKbdInteractiveAuthentication no\n' | sudo tee /etc/ssh/sshd_config.d/100-keys-only.conf >/dev/null && sudo launchctl kickstart -k system/com.openssh.sshd"
```

## 2. Passwordless sudo (you, once)

Automated setup needs `sudo` without a password prompt. On a dedicated build
machine:

```bash
ssh -t -i KEY MACUSER@MAC "echo 'MACUSER ALL=(ALL) NOPASSWD: ALL' | sudo tee /etc/sudoers.d/MACUSER-nopasswd >/dev/null && sudo chmod 440 /etc/sudoers.d/MACUSER-nopasswd && sudo visudo -c"
```

`visudo -c` validates the file so a typo cannot lock `sudo`. Remove the file
again (`sudo rm /etc/sudoers.d/MACUSER-nopasswd`) if the Mac stops being a
dedicated build host.

## 3. Xcode Command Line Tools

These provide Apple clang, `git`, `python3`, `make` and the SDK. Until they are
installed, `clang` and `python3` are stubs that pop up an install dialog.

```bash
ssh -i KEY MACUSER@MAC 'softwareupdate -l 2>&1 | grep "Label: Command Line Tools"'
```

Install the newest label listed (here 16.4, about 860 MB):

```bash
ssh -i KEY MACUSER@MAC 'sudo softwareupdate -i "Command Line Tools for Xcode-16.4" --agree-to-license && sudo xcode-select -s /Library/Developer/CommandLineTools && clang --version'
```

## 4. CMake, Ninja and OpenMP

STAR-Cross needs CMake (3.15 or newer), Ninja and an OpenMP runtime. Apple
clang has OpenMP support but ships without the runtime library (`libomp`).

### Apple Silicon: Homebrew

```bash
ssh -i KEY MACUSER@MAC 'NONINTERACTIVE=1 /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)" && echo "eval \"\$(/opt/homebrew/bin/brew shellenv)\"" >> ~/.zprofile && /opt/homebrew/bin/brew install cmake ninja libomp'
```

Use `-DOpenMP_ROOT=$(brew --prefix libomp)` in step 6.

### Intel: no Homebrew

Current Homebrew refuses to install on Intel Macs ("only supported on Apple
Silicon processors"). Use these instead, all installed under `~/opt` without
`sudo`.

CMake and Ninja from PyPI, in a virtual environment:

```bash
ssh -i KEY MACUSER@MAC '/usr/bin/python3 -m venv ~/opt/buildtools && ~/opt/buildtools/bin/pip install --upgrade pip && ~/opt/buildtools/bin/pip install cmake ninja && ~/opt/buildtools/bin/cmake --version'
```

`libomp` built from the official LLVM source release. Apple clang 17 matches
LLVM 19; the runtime is ABI-stable, so a nearby release is fine. Takes a few
minutes:

```bash
ssh -i KEY MACUSER@MAC 'set -e; export PATH=~/opt/buildtools/bin:$PATH; V=19.1.7; mkdir -p ~/src/llvm-$V && cd ~/src/llvm-$V; for p in openmp cmake; do [ -d $p ] || { curl -fsSL -o $p.tar.xz https://github.com/llvm/llvm-project/releases/download/llvmorg-$V/$p-$V.src.tar.xz; tar xf $p.tar.xz; mv $p-$V.src $p; }; done; cmake -S openmp -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$HOME/opt/libomp -DLIBOMP_INSTALL_ALIASES=OFF -DOPENMP_ENABLE_LIBOMPTARGET=OFF -DLIBOMP_OMPD_SUPPORT=OFF -DOPENMP_ENABLE_OMPT_TOOLS=OFF; ninja -C build install; ls ~/opt/libomp/lib'
```

Put the tools on the login `PATH`:

```bash
ssh -i KEY MACUSER@MAC 'echo "export PATH=\$HOME/opt/buildtools/bin:\$PATH" >> ~/.zprofile'
```

Use `-DOpenMP_ROOT=$HOME/opt/libomp` in step 6.

## 5. Copy the source

From the repository root on the workstation, send the committed tree (no
build directories or local files):

```bash
git archive --format=tar.gz -o /tmp/starcross.tgz HEAD && scp -i KEY /tmp/starcross.tgz MACUSER@MAC:/tmp/ && ssh -i KEY MACUSER@MAC 'rm -rf ~/STAR-Cross && mkdir ~/STAR-Cross && tar xzf /tmp/starcross.tgz -C ~/STAR-Cross && rm /tmp/starcross.tgz'
```

To include uncommitted changes, stage them first and archive
`$(git stash create)` instead of `HEAD`. New files must be staged, or the
archive leaves them out.

## 6. Build

The first configure downloads the pinned dependencies (cpp-httplib,
nlohmann/json) and checks their hashes against `dependencies.lock.json`.

Intel (adjust `OpenMP_ROOT` for Apple Silicon as in step 4):

```bash
ssh -i KEY MACUSER@MAC 'cd ~/STAR-Cross && export PATH=~/opt/buildtools/bin:$PATH && cmake -S source -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DSTAR_BUILD_TESTS=ON -DOpenMP_ROOT=$HOME/opt/libomp && cmake --build build -j $(sysctl -n hw.ncpu)'
```

The Homebrew GCC build used by CI (see `docs/BUILD.md`) is the other supported
macOS toolchain; Apple clang plus `libomp` is enough for local testing.

## 7. Test

```bash
ssh -i KEY MACUSER@MAC 'cd ~/STAR-Cross && export PATH=~/opt/buildtools/bin:$PATH && ctest --test-dir build --no-tests=error --output-on-failure -j $(sysctl -n hw.ncpu)'
ssh -i KEY MACUSER@MAC 'cd ~/STAR-Cross && python3 -m unittest discover -s scripts -p "test_*py"'
ssh -i KEY MACUSER@MAC 'cd ~/STAR-Cross && python3 scripts/test_cpu_upstream.py --star-exe build/STAR'
```

To compare against a frozen baseline, build that commit the same way into
another directory and add `--ref-exe <baseline>/build/STAR`.

## Verified

2026-10-28, Intel Mac (macOS 15.8.2, x86_64, 4 cores), Command Line Tools
16.4 (Apple clang 17), CMake 4.4.4, Ninja 1.13.2, `libomp` from LLVM 19.1.7:
CMake found OpenMP 5.1, `STAR` links `@rpath/libomp.dylib`, CTest 125/125,
script self-tests OK (57 run, 2 skipped), `test_cpu_upstream.py` passes. The
Apple Silicon Homebrew route in step 4 was not re-run for this document.

## Cleanup

- Remove the checkout: `rm -rf ~/STAR-Cross`.
- The toolchain under `~/opt` and `~/src` can stay for the next run.
