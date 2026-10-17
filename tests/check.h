#pragma once

// What the area tests (test_fs.c, test_net.c, ...) share. Unlike the async
// tests, which stop at the first failed "assert", these carry on past a
// failed check, so one run lists everything that is wrong.
//
// They go through the public api, with sppc's own types and constants only,
// so the same files can run unchanged on every platform; where a check needs
// the platform's own view to compare against, it says so.

#include <sppc/sppc.h>
#include <stdio.h>

static int check_failures = 0;

#define CHECK(cond) do { \
    if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++check_failures; } \
  } while (0)

/// ctest's "skipped" (SKIP_RETURN_CODE), for a check the machine cannot run.
#define CHECK_SKIP_STATUS 77

static int check_done(void) {
  printf("%s (%d failures)\n", check_failures == 0 ? "ok" : "FAILED", check_failures);
  return check_failures == 0 ? 0 : 1;
}

/// A fresh directory under the system's temporary one, for a test's files.
static void check_temp_dir(char *out, size_t size, char const *name) {
  snprintf(out, size, "/tmp/sppc_%s_XXXXXX", name);
  if (sppc_mktemp_dir(out) != 0) {
    printf("FAIL: cannot make a temporary directory\n");
    ++check_failures;
  }
}

#if !SPPC_PLATFORM_WINDOWS
#include <sys/wait.h>

/// Run "fn" in a child process and report how it ended: its exit status, or
/// 128 plus the signal that killed it, as a shell would. What it wrote to
/// stderr lands in "err" (terminated, cut to fit). For checking the paths that
/// end the process, and that a fault really faults.
static int check_dies(void (*fn)(void), char *err, size_t err_size) {
  int pipe_fds[2];
  if (pipe(pipe_fds) != 0) { return -1; }
  fflush(stdout);
  const pid_t pid = fork();
  if (pid < 0) { return -1; }
  if (pid == 0) {
    dup2(pipe_fds[1], STDERR_FILENO);
    close(pipe_fds[0]);
    close(pipe_fds[1]);
    fn();
    _exit(0);
  }
  close(pipe_fds[1]);
  size_t len = 0;
  ssize_t n;
  while (err_size > 0 && len < err_size - 1 && (n = read(pipe_fds[0], err + len, err_size - 1 - len)) > 0) {
    len += (size_t)n;
  }
  if (err_size > 0) { err[len] = '\0'; }
  char rest[256];
  while (read(pipe_fds[0], rest, sizeof rest) > 0) {} // the child must not block on a full pipe
  close(pipe_fds[0]);
  int status = 0;
  while (waitpid(pid, &status, 0) < 0) {}
  return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
}
#endif
