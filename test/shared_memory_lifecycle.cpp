// Tiny IPC-only fixture: no genome allocation, no global IPC cleanup.
#include "SharedMemory.h"
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
using namespace std;
static_assert(!std::is_copy_constructible<SharedMemory>::value, "IPC handles must not copy");
static_assert(!std::is_move_constructible<SharedMemory>::value, "IPC handles must not move implicitly");

static void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
static bool absent(key_t key) {
#ifdef POSIX_SHARED_MEM
    const auto name = "/" + std::to_string(key);
    const int fd = shm_open(name.c_str(), O_RDWR, 0);
    if (fd >= 0) { close(fd); return false; }
#else
    if (shmget(key, 0, 0) >= 0) return false;
#endif
    return errno == ENOENT;
}
static void signalByte(int fd) {
    const char byte = 'x';
    require(write(fd, &byte, 1) == 1, "pipe write");
}
static void waitByte(int fd) {
    char byte;
    require(read(fd, &byte, 1) == 1, "pipe read");
}
static bool semAbsent(key_t key) {
    return semget(key, 0, 0) < 0 && errno == ENOENT;
}
static bool allAbsent(key_t key) {
    return absent(key) && shmget(key + 1, 0, 0) < 0 && errno == ENOENT && semAbsent(key);
}
static int semValue(key_t key, int index) {
    const int id = semget(key, 0, 0);
    return id < 0 ? -1 : semctl(id, index, GETVAL);
}
static pid_t spawn(char** argv, const char* mode, key_t key, int fd1, int fd2, int fd3 = -1) {
    const pid_t child = fork();
    require(child >= 0, "fork");
    if (child == 0) {
        const auto keyText = std::to_string(key);
        const auto fd1Text = std::to_string(fd1);
        const auto fd2Text = std::to_string(fd2);
        const auto fd3Text = std::to_string(fd3);
        execl(argv[0], argv[0], mode, keyText.c_str(), fd1Text.c_str(), fd2Text.c_str(), fd3Text.c_str(), nullptr);
        _exit(127);
    }
    return child;
}
static int waitExit(pid_t child) {
    int status = 0;
    require(waitpid(child, &status, 0) == child, "waitpid");
    return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + (WIFSIGNALED(status) ? WTERMSIG(status) : 0);
}

// Child modes for the concurrency checks. Each returns the process exit code.
static int raceChild(key_t key, int startFd, int resultFd, int releaseFd) {
    waitByte(startFd); // start together
    char role;
    {
        SharedMemory shm(key, true);
        if (shm.NeedsAllocation())
            shm.Allocate(128);
        require(shm.GetMapped() != nullptr, "race child mapped");
        role = shm.IsAllocator() ? 'A' : 'B';
        require(write(resultFd, &role, 1) == 1, "race role");
        waitByte(releaseFd); // release together: concurrent last-detach
    }
    return 0;
}
static int churnChild(key_t key, int rounds) {
    for (int i = 0; i < rounds; ++i) {
        SharedMemory shm(key, true);
        if (shm.NeedsAllocation())
            shm.Allocate(128);
        require(shm.GetMapped() != nullptr, "churn child mapped");
        if (shm.IsAllocator())
            *static_cast<volatile int*>(shm.GetMapped()) = 7;
        usleep(static_cast<useconds_t>((getpid() * 131 + i * 977) % 2000));
    }
    return 0;
}
static int loaderCrashChild(key_t key, int readyFd, int doneFd) {
    SharedMemory shm(key, true);
    require(shm.NeedsAllocation(), "loader child creates");
    shm.Allocate(128);
    require(shm.IsAllocator(), "loader child is allocator");
    signalByte(readyFd);
    waitByte(doneFd);
    _exit(0); // dies without publishing: no destructor, SEM_UNDO drops "alive"
}
static int lockHolderChild(key_t key, int readyFd, int /*unused*/) {
    // Take the admission lock exactly as SharedMemory does, then wait to be killed.
    const int id = semget(key, 3, IPC_CREAT | 0666);
    require(id >= 0, "holder semget");
    struct sembuf ops[2] = {{0, 0, 0}, {0, 1, SEM_UNDO}};
    require(semop(id, ops, 2) == 0, "holder lock");
    signalByte(readyFd);
    for (;;) pause();
}

int main(int argc, char** argv) {
    try {
        if (argc == 5 || argc == 6)
            alarm(60); // a child stuck after a parent failure must not outlive the test
        if (argc == 6) {
            const std::string mode = argv[1];
            const key_t key = static_cast<key_t>(std::stol(argv[2]));
            const int fd1 = std::stoi(argv[3]), fd2 = std::stoi(argv[4]), fd3 = std::stoi(argv[5]);
            if (mode == "race-child") return raceChild(key, fd1, fd2, fd3);
            if (mode == "churn-child") return churnChild(key, fd1);
            if (mode == "loader-crash") return loaderCrashChild(key, fd1, fd2);
            if (mode == "lock-holder") return lockHolderChild(key, fd1, fd2);
            return 2;
        }
        if (argc == 5) {
            const key_t key = static_cast<key_t>(std::stol(argv[2]));
            {
                SharedMemory borrower(key, true);
                require(!borrower.NeedsAllocation() && !borrower.IsAllocator(), "child attaches");
                require(borrower.GetSize() >= 128, "attached capacity covers payload");
                require(*static_cast<int*>(borrower.GetMapped()) == 42, "shared payload");
                signalByte(std::stoi(argv[3]));
                waitByte(std::stoi(argv[4]));
                require(*static_cast<int*>(borrower.GetMapped()) == 43, "survives owner detach");
                if (std::string(argv[1]) == "abrupt-child") _exit(77);
            }
            return 0;
        }
        // ftok is not collision-free. Refuse a pre-existing data OR counter key;
        // never remove a resource that this fixture did not create.
        char path[] = "/tmp/star-ipc-XXXXXX";
        const int temp = mkstemp(path);
        require(temp >= 0, "temporary key file");
        struct KeyFile {
            const char* path;
            ~KeyFile() { unlink(path); }
        } keyFile{path}; // Keep inode reserved while parallel backends execute.
        key_t key = -1;
        for (int attempt = 0; attempt < 255; ++attempt) {
            const int project = (getpid() + attempt) % 255 + 1;
            const key_t candidate = ftok(path, project);
            if (candidate != -1 && candidate != std::numeric_limits<key_t>::max()
                && absent(candidate) && shmget(candidate + 1, 0, 0) < 0 && errno == ENOENT
                && semAbsent(candidate)) {
                key = candidate;
                break;
            }
        }
        close(temp);
        require(key != -1 && key != std::numeric_limits<key_t>::max(), "valid key");
        require(allAbsent(key), "unused keys");
        alarm(60); // a lost wakeup must fail the test, not hang it
        for (bool unloadLast : {false, true}) {
            SharedMemory probe(key, unloadLast); // e.g. Remove of an unloaded genome
            require(probe.NeedsAllocation(), "nothing to attach");
        }
        require(allAbsent(key), "a job that never allocated leaves no IPC objects");
        {
            SharedMemory kept(key, false);
            require(kept.GetMapped() == nullptr, "unallocated mapping");
            bool overflow = false;
            try { kept.Allocate(std::numeric_limits<size_t>::max()); }
            catch (const SharedMemoryException& e) { overflow = e.GetErrorDetail() == EOVERFLOW; }
            require(overflow, "allocation overflow rejected");
            kept.Allocate(128);
            // POSIX shm ftruncate/fstat may expose page-rounded capacity on macOS.
            require(kept.GetSize() >= 128 && kept.IsAllocator(), "allocator capacity");
            *static_cast<int*>(kept.GetMapped()) = 42;
            bool duplicate = false;
            try { kept.Allocate(128); }
            catch (const SharedMemoryException& e) { duplicate = e.GetErrorCode() == EALREADYALLOCATED; }
            require(duplicate, "duplicate allocation rejected");
        }
        require(!absent(key), "keep retains segment");
        struct shmid_ds counterInfo;
        const int counterId = shmget(key + 1, 0, 0);
        require(counterId >= 0 && shmctl(counterId, IPC_STAT, &counterInfo) == 0,
                "kept counter accessible");
        require(counterInfo.shm_nattch == 0, "keep detaches counter without removing it");
        require(semValue(key, 0) == 0, "keep releases the admission lock");
        require(semValue(key, 1) == 0 && semValue(key, 2) == 1, "destroyed loader recorded as exited");
        for (bool abrupt : {false, true}) {
            auto owner = std::make_unique<SharedMemory>(key, true);
            if (owner->NeedsAllocation()) {
                owner->Allocate(128);
                *static_cast<int*>(owner->GetMapped()) = 42;
            }
            int ready[2], done[2];
            require(pipe(ready) == 0 && pipe(done) == 0, "pipes");
            const pid_t child = fork();
            require(child >= 0, "fork");
            if (child == 0) {
                close(ready[0]); close(done[1]);
                const auto keyText = std::to_string(key);
                const auto readyText = std::to_string(ready[1]);
                const auto doneText = std::to_string(done[0]);
                execl(argv[0], argv[0], abrupt ? "abrupt-child" : "child", keyText.c_str(), readyText.c_str(), doneText.c_str(), nullptr);
                _exit(127);
            }
            close(ready[1]); close(done[0]);
            waitByte(ready[0]);
            *static_cast<int*>(owner->GetMapped()) = 43;
            owner.reset();
            require(!absent(key), "do not remove while child attached");
            signalByte(done[1]);
            close(ready[0]); close(done[1]);
            int status = 0;
            require(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == (abrupt ? 77 : 0), "child exit");
            if (abrupt) {
                require(!absent(key), "abrupt exit does not unlink a named resource");
                SharedMemory recovery(key, true);
                require(*static_cast<int*>(recovery.GetMapped()) == 43, "recovery retains payload");
            }
            require(absent(key), "last detach removes segment");
            require(shmget(key + 1, 0, 0) < 0 && errno == ENOENT, "last detach removes counter");
            require(semAbsent(key), "last detach removes admission semaphores");
        }
#if defined(__linux__) && !defined(STAR_TEST_SKIP_ALLOCATION_LIMIT)
        {
            auto failed = std::make_unique<SharedMemory>(key, true);
            struct rlimit original;
            require(getrlimit(RLIMIT_AS, &original) == 0, "read address-space limit");
            struct rlimit limited = original;
            limited.rlim_cur = 0; // deny mapping, without allocating real RAM
            require(setrlimit(RLIMIT_AS, &limited) == 0, "inject mapping failure");
            // QEMU user-mode emulation accepts but does not enforce RLIMIT_AS for the
            // guest, so the failure cannot be injected there. Probe before trusting it.
            void* probe = mmap(nullptr, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            if (probe != MAP_FAILED) {
                munmap(probe, 4096);
                require(setrlimit(RLIMIT_AS, &original) == 0, "restore address-space limit");
                fprintf(stderr, "SKIP failed-allocation check: RLIMIT_AS is not enforced here (emulated?)\n");
            } else {
                bool rejected = false;
                try { failed->Allocate(128); }
                catch (const SharedMemoryException&) { rejected = true; }
                catch (...) { setrlimit(RLIMIT_AS, &original); throw; }
                require(setrlimit(RLIMIT_AS, &original) == 0, "restore address-space limit");
                failed.reset();
                const bool cleaned = absent(key);
                if (!cleaned) { SharedMemory recovery(key, false); recovery.Clean(); }
                require(rejected && cleaned, "failed allocation removes its own partial segment");
            }
        }
#endif
        {
            SharedMemory removed(key, false);
            removed.Allocate(128);
            removed.Clean();
            removed.Clean();
            require(removed.GetMapped() == nullptr && removed.NeedsAllocation(), "idempotent clean");
        }
        require(absent(key) && shmget(key + 1, 0, 0) < 0 && errno == ENOENT, "no destructor counter recreation");
#ifdef SHM_DEST
        // Invalidate this fixture's counter. IPC_STAT can still succeed until
        // detach, but a removed counter cannot certify absence of other users.
        {
            auto failed = std::make_unique<SharedMemory>(key, true);
            failed->Allocate(128);
            const int ownCounter = shmget(key + 1, 0, 0);
            require(ownCounter >= 0 && shmctl(ownCounter, IPC_RMID, nullptr) == 0,
                    "inject missing fixture counter");
            failed.reset();
            require(!absent(key), "failed usage query must not remove data");
            SharedMemory explicitRemover(key, false);
            explicitRemover.Clean();
        }
        require(absent(key) && shmget(key + 1, 0, 0) < 0 && errno == ENOENT,
                "failure fixture explicitly cleaned");
#endif
        require(allAbsent(key), "explicit clean removes every IPC object");

        // Concurrent admission and concurrent last detach: exactly one
        // allocator, every job attached, and the last one out removes all.
        constexpr int kRacers = 8;
        for (int round = 0; round < 10; ++round) {
            int start[2], result[2], release[2];
            require(pipe(start) == 0 && pipe(result) == 0 && pipe(release) == 0, "race pipes");
            pid_t racers[kRacers];
            for (auto& racer : racers)
                racer = spawn(argv, "race-child", key, start[0], result[1], release[0]);
            close(start[0]); close(result[1]); close(release[0]);
            const std::string go(kRacers, 'x');
            require(write(start[1], go.data(), go.size()) == (ssize_t) go.size(), "start racers");
            int allocators = 0;
            for (int i = 0; i < kRacers; ++i) {
                char role = 0;
                require(read(result[0], &role, 1) == 1, "racer role");
                allocators += role == 'A';
            }
            require(write(release[1], go.data(), go.size()) == (ssize_t) go.size(), "release racers");
            close(start[1]); close(result[0]); close(release[1]);
            for (auto racer : racers)
                require(waitExit(racer) == 0, "racer exit");
            require(allocators == 1, "exactly one concurrent allocator");
            require(allAbsent(key), "concurrent last detach removes every IPC object once");
        }

        // Arrivals racing departures (LoadAndRemove churn) leave nothing behind.
        {
            pid_t churners[kRacers];
            for (auto& churner : churners)
                churner = spawn(argv, "churn-child", key, 40, -1);
            for (auto churner : churners)
                require(waitExit(churner) == 0, "churn child exit");
            require(allAbsent(key), "churn leaves no segment, counter or semaphore");
        }

        // A job killed while holding the admission lock does not block others.
        {
            int ready[2];
            require(pipe(ready) == 0, "holder pipe");
            const pid_t holder = spawn(argv, "lock-holder", key, ready[1], -1);
            close(ready[1]);
            waitByte(ready[0]);
            close(ready[0]);
            require(semValue(key, 0) == 1, "lock held by child");
            kill(holder, SIGKILL);
            require(waitExit(holder) == 128 + SIGKILL, "holder killed");
            SharedMemory after(key, false); // would block forever without SEM_UNDO
            require(after.NeedsAllocation(), "no segment after killed holder");
            after.Clean();
        }
        require(allAbsent(key), "killed-holder fixture cleaned");

        // A loader that dies before publishing is reported as exited.
        {
            int ready[2], done[2];
            require(pipe(ready) == 0 && pipe(done) == 0, "loader pipes");
            const pid_t loader = spawn(argv, "loader-crash", key, ready[1], done[0]);
            close(ready[1]); close(done[0]);
            waitByte(ready[0]);
            {
                SharedMemory waiter(key, false);
                require(!waiter.NeedsAllocation() && !waiter.IsAllocator(), "waiter attaches");
                require(waiter.GetLoaderState() == SharedMemory::LoaderState::Loading, "live loader seen");
                signalByte(done[1]);
                require(waitExit(loader) == 0, "loader exit");
                require(waiter.GetLoaderState() == SharedMemory::LoaderState::Exited, "dead loader seen");
                waiter.Clean();
            }
            close(ready[0]); close(done[1]);
        }
        require(allAbsent(key), "dead-loader fixture cleaned");

        // A loader that publishes and calls LoaderFinished is no longer loading.
        {
            SharedMemory loader(key, false);
            loader.Allocate(128);
            require(loader.GetLoaderState() == SharedMemory::LoaderState::Loading, "own load in progress");
            loader.LoaderFinished();
            loader.LoaderFinished(); // idempotent
            require(loader.GetLoaderState() == SharedMemory::LoaderState::Exited, "finished loader");
            loader.Clean();
        }

        // A segment created without the protocol (an older STAR build) has no
        // recorded loader, so waiters keep the time-out behavior.
        {
#ifdef POSIX_SHARED_MEM
            const auto name = "/" + std::to_string(key);
            const int fd = shm_open(name.c_str(), O_CREAT | O_EXCL | O_RDWR, 0666);
            require(fd >= 0 && ftruncate(fd, 256) == 0, "legacy segment");
            close(fd);
#else
            require(shmget(key, 256, IPC_CREAT | IPC_EXCL | 0666) >= 0, "legacy segment");
#endif
            SharedMemory legacy(key, false);
            require(!legacy.NeedsAllocation(), "legacy segment attached");
            require(legacy.GetLoaderState() == SharedMemory::LoaderState::Unknown, "legacy loader unknown");
            legacy.Clean();
        }
        require(allAbsent(key), "legacy fixture cleaned");

        // Key collisions with a neighbouring genome (inode key-1 or key+1). The
        // other genome's segment must be reported, never used or removed.
        auto removeSem = [&] { const int id = semget(key, 0, 0); if (id >= 0) semctl(id, 0, IPC_RMID); };
        {
            const int foreignData = shmget(key + 1, 256, IPC_CREAT | IPC_EXCL | 0666); // genome "key+1" data
            require(foreignData >= 0, "foreign data segment");
            bool collided = false;
            try { SharedMemory shm(key, true); }
            catch (const SharedMemoryException& e) { collided = e.GetErrorCode() == EKEYCOLLISION; }
            struct shmid_ds info;
            require(shmctl(foreignData, IPC_STAT, &info) == 0 && info.shm_segsz == 256, "foreign data kept");
            shmctl(foreignData, IPC_RMID, nullptr);
            removeSem();
            require(collided, "counter key holding another genome's data is reported");
        }
#ifndef POSIX_SHARED_MEM
        {
            const int foreignCounter = shmget(key, 1, IPC_CREAT | IPC_EXCL | 0666); // genome "key-1" counter
            require(foreignCounter >= 0, "foreign counter segment");
            bool collided = false;
            try { SharedMemory shm(key, true); }
            catch (const SharedMemoryException& e) { collided = e.GetErrorCode() == EKEYCOLLISION; }
            struct shmid_ds info;
            require(shmctl(foreignCounter, IPC_STAT, &info) == 0, "foreign counter kept");
            shmctl(foreignCounter, IPC_RMID, nullptr);
            const int ownCounter = shmget(key + 1, 0, 0);
            if (ownCounter >= 0) shmctl(ownCounter, IPC_RMID, nullptr);
            removeSem();
            require(collided, "data key holding another genome's counter is reported");
        }
#endif
        require(allAbsent(key), "collision fixtures cleaned");
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "IPC lifecycle failed: " << e.what() << '\n';
        return 1;
    }
}
