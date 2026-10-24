#pragma once
#include <sppc/macros.h>
#include <stdint.h>
#include <stdlib.h>

/// An S++ callable is a closure: a function pointer plus
/// the environment it captures, and the function's own
/// first parameter is that environment. It is laid out as
/// this pair and passed by value, which the SysV ABI puts
/// in two integer registers - the same as any two-pointer
/// struct - so C sees it as one argument and everything
/// after it stays where it belongs. Taking only the function
/// pointer would lose the captures and would shift every
/// following argument along by one register.
typedef struct {
  void (*fn)(void *);
  void *env;
} sppc_closure;

/// What the environment of every closure starts with, as the
/// compiler lays it out ("ClosureEnvType"): whether it owns its
/// storage (on the heap) or not (on a stack), and the function
/// dropping what the closure captured by value, null when none
/// of it needs dropping. A closure built in C for the runtime
/// starts its environment with one too, or has none at all.
typedef struct {
  uint8_t owns;
  void (*drop_captures)(void *env);
} sppc_closure_env;

/// Give up a closure that will not be called. Running it would
/// have consumed what it captured, so its captures are dropped
/// instead, and its environment is freed if it owns it.
_gnu_inline
void _sppc_closure_discard(const sppc_closure closure) {
  sppc_closure_env *const env = (sppc_closure_env*)closure.env;
  if (env == NULL) { return; }
  if (env->drop_captures != NULL) { env->drop_captures(env); }
  if (env->owns) { free(env); }
}
