# SPPC

The `sppc` library is a collection of very low level functions, that have kernel specific implementations. It forms the
building blocks for the `S++` standard library, which includes `sppc` via the `ffi` system. As `S++` evolves, more
system calls will be required, which this library will provide.

Other "low level" functions not exposed from this library are exposed using `llvm`, which generates assembly code
directly, such as the math `sin`, `cos`, etc. functions. Implementing the `sppc` functions in assembly would be
extremely difficult becausebthey would need to use direct system calls, which change constantly, and would need to
target multiple processors and operating systems. Using `C` allows for this to be abstracted over.
