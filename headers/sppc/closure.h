#pragma once

// An S++ callable is a closure: a function pointer plus
// the environment it captures, and the function's own
// first parameter is that environment. It is laid out as
// this pair and passed by value, which the SysV ABI puts
// in two integer registers - the same as any two-pointer
// struct - so C sees it as one argument and everything
// after it stays where it belongs. Taking only the function
// pointer would lose the captures and would shift every
// following argument along by one register.
//
// It lives in a header of its own because both the public
// api and the green-thread runtime take one, and the
// runtime is included first: a task body is a closure, so
// "async2.h" needs the type before "sppc.h" has declared
// anything.
typedef struct {
  void (*fn)(void *);
  void *env;
} sppc_closure;
