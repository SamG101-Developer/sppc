# Tests

Run them all through ctest (on by default; `-DSPPC_BUILD_TESTS=OFF` skips them):

    cmake -S . -B build && cmake --build build && ctest --test-dir build --output-on-failure

Each is linked with the library's object files directly rather than against the shared library, because the library
exports only the public api and these reach into the runtime's own entry points. They are built with `NDEBUG` undefined
in every configuration, since they check with `assert`, and run with `MALLOC_CHECK_=3`. One by hand:

    gcc -std=c17 -g -Iheaders -o /tmp/t tests/test_gt_scheduling.c sources/sppc/sppc.c headers/sppc/asm/gt_switch.S
    MALLOC_CHECK_=3 /tmp/t

| File                   | Covers                                                                                                                                                                                                                                                                      |
|------------------------|-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `test_gt_scheduling.c` | round-robin order, deep recursion on a task stack, a retired handle refusing to resolve, several tasks awaiting one, detach                                                                                                                                                 |
| `test_gt_io.c`         | a task parking on an io_uring read while another runs; a sleeping task not stalling the others, directly and through `sppc_clock_nanosleep` (relative and absolute); a clock the ring can't use falling back, with its error reported                                       |
| `test_gt_safestack.c`  | one unsafe stack per task, restored across switches, with the thread's own preserved                                                                                                                                                                                        |
| `test_gt_locks.c`      | a mutex, rwlock and spin lock contended between two tasks on one thread: the waiter runs the holder rather than blocking, and a release hands the lock to the waiter ahead of the holder taking it straight back; timed mutex/rwlock waits returning `ETIMEDOUT`            |
| `test_gt_stress.c`     | many rounds of spawn/await/detach/yield with heap traffic between them, to shake out corruption                                                                                                                                                                             |
| `test_fs.c`            | read/write and their positioned and vector forms, open flags, dup, truncate, copy_file_range and sendfile, directories, rename, hard and symbolic links, realpath/getcwd/chdir, chown, utimensat, access, temporary names; each with the sppc error for its common failures |
| `test_net.c`           | address helpers (host-order in, network-order on the wire), TCP and UDP over IPv4 and IPv6 loopback: names, peek, non-blocking, shutdown, EPIPE, EADDRINUSE, ECONNREFUSED; a server task and a client task on one thread (with a ring)                                      |
| `test_mem.c`           | malloc/calloc/realloc/aligned_alloc, anonymous and file-backed mappings, madvise/msync/mlock, a write to a read-only page faulting, the `mem*`/`str*` helpers including overlap and not-found                                                                               |
| `test_threads.c`       | create/join/detach/self/equal, once, mutexes (plain and recursive), condition variables (signal, broadcast, timed), rwlocks, spin locks and barriers between os threads, each `try` and timed form, unsafe stacks per thread, a task waiting on a lock a thread holds       |
| `test_process.c`       | environment, pids, getrandom, the standard streams, and every ending: exit (atexit and pending tasks run), halt (nothing runs), abort and each runtime check (message, backtrace naming the caller, status 134), fatal signals                                              |
| `test_types`           | sppc's own types and constants: every translation table entry both ways, values the platform must refuse, and the public functions end to end through sppc types (stat, poll, fcntl locks, sockets, socket options, select, readv, barriers, errno)                         |
| `test_strict_c17`      | `sppc.c` and every header it includes compile as `-std=c17 -pedantic-errors -Wall -Wextra -Wvla -Werror` (gcc and clang)                                                                                                                                                    |
