# SPPC

The `sppc` library is a collection of low level functions, that have kernel specific implementations. It forms the
building blocks for the `S++` standard library, which includes `sppc` via the `ffi` system. As `S++` evolves, more
system calls will be required, which this library will provide. Other "low level" functions not exposed from this
library are exposed using `llvm` intrinsics, such as the math `sin`, `cos`, etc. functions.

Collection of functions exposed from this library include:

- system call wrappers (streams, files, sockets, etc)
- pthread wrappers (threading, mutexes, condition variables, etc)
- memory management functions (allocation, comparison, manipulation)
- string manipulation functions (copy, compare, search, etc)

AI Usage
- Scanning for bugs
- Test suites