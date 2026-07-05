/**
 * The implementation is now header-only and lives entirely in <sppc/sppc.h>.
 *
 * All wrappers are `inline __attribute__((always_inline))`, so this translation
 * unit emits no out-of-line copies on its own. If the FFI needs the `c_*`
 * symbols exported from the shared object, force emission here with an `extern`
 * (non-inline) re-declaration per function, e.g.:
 *
 *     extern int c_init(void);
 *     extern int c_read(char *restrict, size_t, size_t, int, ssize_t *restrict);
 *     ...
 *
 * (verified: an `extern` re-declaration emits the symbol even for
 * always_inline definitions.)
 */

#include <sppc/sppc.h>