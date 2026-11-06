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
#if SPPC_PLATFORM_WINDOWS
  char temp[MAX_PATH + 1];
  GetTempPathA(sizeof temp, temp); // ends in a backslash
  snprintf(out, size, "%ssppc_%s_XXXXXX", temp, name);
#else
  snprintf(out, size, "/tmp/sppc_%s_XXXXXX", name);
#endif
  if (sppc_mktemp_dir(out) != 0) {
    printf("FAIL: cannot make a temporary directory\n");
    ++check_failures;
  }
}

#if SPPC_PLATFORM_WINDOWS

#if defined(_MSC_VER) && defined(_DEBUG)
  #include <crtdbg.h>
#endif

/// Run "fn" in a child process and report how it ended: its exit status
/// (sppc ends a process the ways a POSIX shell would report it, 134 for an
/// abort say, so the numbers are the same). What it wrote to stderr lands in
/// "err" (terminated, cut to fit). Windows has no fork, so the child is this
/// test run again: told which function to run (by its offset into the
/// executable, which may load elsewhere in the child), it runs it from
/// "check_child", which the test's main calls once sppc is started.
static int check_dies(void (*fn)(void), char *err, size_t err_size) {
  char offset[32];
  // Still set here, this is a child whose main never called "check_child":
  // starting another would start another in turn, without end.
  if (GetEnvironmentVariableA("SPPC_CHECK_CHILD", offset, sizeof offset) != 0) {
    fputs("FAIL: check_dies in a child: the test's main must call check_child\n", stderr);
    _exit(1);
  }
  snprintf(offset, sizeof offset, "%llx", (unsigned long long)((uintptr_t)fn - (uintptr_t)GetModuleHandleW(NULL)));
  SECURITY_ATTRIBUTES inherit = {sizeof inherit, NULL, TRUE};
  HANDLE r, w;
  if (!CreatePipe(&r, &w, &inherit, 0)) { return -1; }
  SetHandleInformation(r, HANDLE_FLAG_INHERIT, 0);
  wchar_t self[MAX_PATH];
  GetModuleFileNameW(NULL, self, MAX_PATH);
  STARTUPINFOW start;
  memset(&start, 0, sizeof start);
  start.cb = sizeof start;
  start.dwFlags = STARTF_USESTDHANDLES;
  start.hStdInput = NULL;
  start.hStdOutput = w;
  start.hStdError = w;
  PROCESS_INFORMATION child;
  fflush(stdout);
  SetEnvironmentVariableA("SPPC_CHECK_CHILD", offset);
  const BOOL started = CreateProcessW(self, NULL, NULL, NULL, TRUE, 0, NULL, NULL, &start, &child);
  SetEnvironmentVariableA("SPPC_CHECK_CHILD", NULL);
  CloseHandle(w);
  if (!started) {
    CloseHandle(r);
    return -1;
  }
  size_t len = 0;
  DWORD n = 0;
  char rest[256];
  while (err_size > 0 && len < err_size - 1 && ReadFile(r, err + len, (DWORD)(err_size - 1 - len), &n, NULL) && n > 0) {
    len += n;
  }
  if (err_size > 0) { err[len] = '\0'; }
  while (ReadFile(r, rest, sizeof rest, &n, NULL) && n > 0) {} // the child must not block on a full pipe
  CloseHandle(r);
  WaitForSingleObject(child.hProcess, INFINITE);
  DWORD status = 0;
  GetExitCodeProcess(child.hProcess, &status);
  CloseHandle(child.hProcess);
  CloseHandle(child.hThread);
  return (int)status;
}

/// In a child "check_dies" started, run the function it was started for and
/// end, as the fork's child does; anywhere else, nothing.
static void check_child(void) {
  char offset[32];
  if (GetEnvironmentVariableA("SPPC_CHECK_CHILD", offset, sizeof offset) == 0) { return; }
  SetEnvironmentVariableA("SPPC_CHECK_CHILD", NULL);
  SetErrorMode(GetErrorMode() | SEM_NOGPFAULTERRORBOX); // a crash ends the child, without a dialog
#if defined(_MSC_VER) && defined(_DEBUG)
  // msvc's debug C library reports an "abort" (and its other errors) in a
  // window, which nobody would answer: to stderr instead, as its release one.
  _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
  _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
#endif
  void (*fn)(void);
  const uintptr_t at = (uintptr_t)GetModuleHandleW(NULL) + (uintptr_t)strtoull(offset, NULL, 16);
  memcpy(&fn, &at, sizeof fn);
  fn();
  _exit(0);
}

#else

/// "check_dies" forks, so its child is already where the function runs.
static void check_child(void) {}

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
