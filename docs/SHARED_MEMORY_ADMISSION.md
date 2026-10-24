# Shared-memory genome admission

Changed 2026-10-23 on `codex/cxx20-migration` (base `b56f3bb`). Applies to
`--genomeLoad LoadAndKeep | LoadAndRemove | LoadAndExit | Remove` on Linux and
macOS. Windows supports only `NoSharedMemory`, and the new code is compiled out
there.

## Problems in the previous protocol

Jobs sharing a genome coordinated through three things only: `IPC_EXCL` segment
creation, the attach count of a 1-byte "counter" segment at `key+1`, and two
size fields at the start of the genome that the loading job wrote last.

| # | Problem | Effect |
| --- | --- | --- |
| 1 | `LoadAndRemove` release did count → detach → remove with no lock. Two jobs finishing together could each see the other still attached, and both leave | Genome left in memory after the last job (reproduced: 9 of 10 runs of the new concurrent-release test fail without the lock) |
| 2 | A job arriving during that release could attach the counter just before it was removed, then fail its own usage query at exit and keep the segment | Orphaned segment, or a job attached to a removed genome |
| 3 | Readiness was two plain stores (NG, then NSA) with no ordering; waiters read NG with a plain load | A waiter could see NG before NSA and stop with a false "SA file size did not match" error; formally a data race |
| 4 | A loader that crashed left a segment that never became ready | Every later job slept up to 100 minutes, then exited with code 107 |
| 5 | Waiters polled every 60 seconds | Up to a minute of idle time after a fast load |
| 6 | `--genomeLoad Remove` of a genome that was not loaded exited without detaching | One leaked 1-byte counter segment per such call (seen on the test host) |

## New protocol

`SharedMemory` (`source/SharedMemory.cpp`) owns a SysV semaphore set with three
semaphores, keyed by the genome key. Semaphore keys are a separate namespace from
shared-memory keys, so it cannot collide with the data or counter segments.

| Semaphore | Meaning | Operations |
| --- | --- | --- |
| 0, admission lock | 0 = unlocked | lock: one atomic `semop` {wait for 0, +1}; unlock: {−1}; both `SEM_UNDO` |
| 1, loader alive | count of live loaders | +1 `SEM_UNDO` when a job creates the segment; −1 after it publishes the genome |
| 2, loader recorded | segment was created under this protocol | +1 (no undo) together with semaphore 1 |

- **Lock scope.** The lock covers counter attach + segment open (constructor),
  segment creation (`Allocate`), count + detach + remove (destructor), and
  `Clean()`. A job arriving during the last release is either counted before the
  decision or starts after the removal. The loading itself runs outside the lock.
- **Crash safety.** `SEM_UNDO` releases a lock held by a killed process and drops
  "loader alive" for a loader that died. A new set is all zeros, which already
  means unlocked, so creating it needs no separate initialization step.
- **Removal.** `Clean()` removes the semaphore set last, while holding the lock.
  Jobs waiting on the lock get `EIDRM` (Linux) or `EINVAL` (macOS), create a new
  set, and continue.
- **Publish.** The loader stores NSA, then NG with `std::atomic_ref` release order,
  then decrements "loader alive". Waiters load NG with acquire order.
- **Waiting** (`Genome_genomeLoad.cpp`):
  - Waiters poll every second, log once a minute, and keep the 100-minute
    time-out.
  - If the segment is not ready, the loader was recorded, and none is alive, the
    job stops at once with exit code 106 and asks for `--genomeLoad Remove`. It
    re-checks NG first, so a loader that just finished is not reported as dead.
  - A size mismatch now stops at once with exit code 111 instead of waiting
    100 minutes.
- **Unused handles.** A job that never found or created a segment, and is the
  last one attached, removes the counter and semaphore set when it is released.
  The `Remove` path releases before it exits.
- **Older STAR builds.** A segment created by a build without this protocol has
  no recorded loader. Waiters then keep the previous time-out behavior and do
  not report a dead loader. Old and new builds do not share the lock, so jobs
  from both on one genome get only the old guarantees.

## Tests

`test/shared_memory_lifecycle.cpp` (SysV and POSIX backends, CTest time-out
raised to 90 s) adds:

- 10 rounds of 8 processes starting together on an absent genome: exactly one
  allocator, then all 8 release together and nothing is left
- 8 processes × 40 rounds of attach/allocate/release churn: nothing is left
- a process killed with `SIGKILL` while holding the lock: the next job proceeds
- a loader killed before publishing: reported as `Loading` while alive and
  `Exited` after
- `LoaderFinished()` is idempotent
- a segment created without the protocol reports `Unknown`
- a job that never allocates leaves no IPC objects
- every lifecycle step checks the semaphore set as well as the segment and
  counter

`scripts/test_cpu_upstream.py --shared-memory` adds two checks:

- Four real `LoadAndRemove` STAR jobs start together on an unloaded genome. Their
  BAM and SJ.out.tab must match ordinary loading, and the genome must be gone
  afterwards.
- With `--ipc-helper test/star_ipc_sysv` (now passed in CI), a simulated loader
  dies before publishing. The next STAR job must exit with 106 in under 60 s,
  and `Remove` must recover.

## Results

| Run | Result |
| --- | --- |
| Oracle Linux 10, GCC Release: 128 unit tests; IPC tests ×3 | PASS |
| Same build: miniature harness with `--shared-memory --ipc-helper` | PASS; no semaphore sets or segments left |
| Negative control: lock disabled by a test-only patch | 9 of 10 runs fail "concurrent last detach removes every IPC object once"; with the lock, 3 of 3 pass |
| Clang 21 TSan: 128 unit tests; IPC tests ×3 | PASS, 0 reports |
| GCC ASan + UBSan (CI configuration, `halt_on_error=1`, leak detection): 128 unit tests, IPC tests ×3, harness with `--shared-memory --ipc-helper` | PASS |
| Windows MSVC 14.51: build and 125 CTest | PASS (code compiled out) |

Clang 21 ASan + UBSan also reported existing `pointer-overflow` findings in code
this change does not touch (`SuffixArrayFuns.cpp`, `Genome_genomeGenerate.cpp`,
`ReadAlignChunk_readChunk.cpp`, `extendAlign.cpp`). GCC does not report them.
They were fixed in a follow-up on the same branch, which also added a Clang
ASan + UBSan CI job (`clang-asan-ubsan`).

## Counter key collision

The counter segment is at `key+1` in the shared-memory namespace. With the SysV
backend, a genome directory whose inode is `key+1` uses that same key for its
data segment. Moving the counter would break running jobs from older builds, so
the collision is detected instead. Counters are always exactly 1 byte (Linux and
macOS both report `shm_segsz == 1`).

- A counter key holding a segment of any other size is another genome's data.
- A data key holding a 1-byte segment is another genome's counter.

Either case stops with `EKEYCOLLISION`. The message says to copy the genome to a
new directory (which gives it a new inode) or to use `NoSharedMemory`. The other
genome's segment is never attached as a counter or removed. Both directions are
tested in `shared_memory_lifecycle.cpp`.

## macOS

The protocol uses only `semget`, `semop` and `semctl(GETVAL | IPC_RMID)`. A
removed set returns `EINVAL` on macOS, which is handled like `EIDRM`. Default
macOS limits allow 87,381 semaphore sets (`kern.sysv.semmni`). Shared-memory
segments are limited to 4 MB (`kern.sysv.shmmax`), as before.

Tested on macOS 26.6 with Apple clang 21 and Homebrew `libomp`:

- IPC tests, both backends, under ASan+UBSan: 3 of 3 runs pass
- STAR build, 128 unit tests: pass
- miniature harness with `--shared-memory --ipc-helper` (concurrent jobs, dead
  loader): pass, nothing left in shared memory

CI's macOS job now runs the shared-memory harness as well.

The Apple clang build first failed on a separate problem. On case-insensitive
filesystems (macOS, Windows), `source/VERSION` shadowed the C++20 `<version>`
header for every target with `source/` on its include path, so doctest and
nlohmann/json read STAR's version file instead of the standard header. The file
is now `source/STARVersion.h`.

## Not changed

- **No automatic recovery.** A dead loader's segment is not reloaded
  automatically, because other jobs may still be attached to it. The error tells
  the user to run `--genomeLoad Remove`.
