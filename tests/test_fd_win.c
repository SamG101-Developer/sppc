#include "check.h"

// File descriptors on Windows (platform/windows.h, FD TABLE): the table under every
// fd call. Pipes read and write, with the end of input when the writers have
// gone and EPIPE when the readers have; a duplicate names the same open file,
// which lasts until its last fd is closed; "dup2" replaces, and onto 0-2
// makes the process's standard handle follow; the lowest free fd is the one
// given; the table fills at its limit; it holds up under threads. A console,
// where there is one, is written and read in UTF-8 whatever its code page.

#if !SPPC_PLATFORM_WINDOWS

int main(void) {
  printf("skipped: Windows only\n");
  return CHECK_SKIP_STATUS;
}

#else

#include <wchar.h>

static int put(const int fd, char const *s) {
  ssize_t n = 0;
  const int rc = sppc_write(s, 1, strlen(s), fd, &n);
  return rc == 0 && (size_t)n == strlen(s) ? 0 : -1;
}

static bool got(const int fd, char const *want) {
  char buf[256] = {0};
  ssize_t n = 0;
  return sppc_read(buf, 1, sizeof buf - 1, fd, &n) == 0 && (size_t)n == strlen(want) && memcmp(buf, want, (size_t)n) == 0;
}

static bool at_eof(const int fd) {
  char c;
  ssize_t n = -2;
  return sppc_read(&c, 1, 1, fd, &n) == 0 && n == 0;
}

// ==================== PIPES ====================

static void check_pipes(void) {
  int r = -1, w = -1;
  CHECK(sppc_pipe(&r, &w) == 0 && r >= 0 && w >= 0 && r != w);
  CHECK(put(w, "hello") == 0);
  CHECK(got(r, "hello"));

  // A read takes what there is, up to what it asks for.
  CHECK(put(w, "abcdef") == 0);
  char buf[4];
  ssize_t n = 0;
  CHECK(sppc_read(buf, 1, 4, r, &n) == 0 && n == 4 && memcmp(buf, "abcd", 4) == 0);
  CHECK(got(r, "ef"));

  // Each end goes one way only.
  CHECK(sppc_write("x", 1, 1, r, &n) == SPPC_EBADF && n == -1);
  CHECK(sppc_read(buf, 1, 1, w, &n) == SPPC_EBADF && n == -1);

  // The writer gone: the end of input, after what it wrote.
  CHECK(put(w, "last") == 0);
  CHECK(sppc_close(w) == 0);
  CHECK(got(r, "last"));
  CHECK(at_eof(r));
  CHECK(sppc_close(r) == 0);

  // The reader gone: EPIPE.
  CHECK(sppc_pipe(&r, &w) == 0);
  CHECK(sppc_close(r) == 0);
  CHECK(sppc_write("x", 1, 1, w, &n) == SPPC_EPIPE && n == -1);
  CHECK(sppc_close(w) == 0);
}

// ==================== THE TABLE ====================

static void check_close(void) {
  int r = -1, w = -1;
  CHECK(sppc_pipe(&r, &w) == 0);
  CHECK(sppc_close(r) == 0);
  CHECK(sppc_close(r) == SPPC_EBADF);
  ssize_t n = 0;
  char c;
  CHECK(sppc_read(&c, 1, 1, r, &n) == SPPC_EBADF && n == -1);
  CHECK(sppc_close(-1) == SPPC_EBADF);
  CHECK(sppc_close(_SPPC_FD_MAX + 5) == SPPC_EBADF);
  CHECK(sppc_write("x", 1, 1, 999999, &n) == SPPC_EBADF);

  // The lowest free fd is the next one given.
  int again = -1, w2 = -1;
  CHECK(sppc_pipe(&again, &w2) == 0 && again == r);
  sppc_close(again);
  sppc_close(w2);
  sppc_close(w);
}

static void check_dup(void) {
  int r = -1, w = -1, copy = -1;
  CHECK(sppc_pipe(&r, &w) == 0);

  // A duplicate is another name for the same end, which stays open until
  // the last of its names is closed.
  CHECK(sppc_dup(w, &copy) == 0 && copy != w && copy >= 0);
  CHECK(put(copy, "via copy") == 0);
  CHECK(got(r, "via copy"));
  CHECK(sppc_close(w) == 0);
  CHECK(put(copy, "still open") == 0);
  CHECK(got(r, "still open"));
  CHECK(sppc_close(copy) == 0);
  CHECK(at_eof(r));
  CHECK(sppc_dup(-1, &copy) == SPPC_EBADF && copy == -1);
  CHECK(sppc_dup(w, &copy) == SPPC_EBADF);
  sppc_close(r);

  // dup2 puts one in the other's place, closing what that named: here the
  // only writer, so its reader sees the end of input.
  int r1 = -1, w1 = -1, r2 = -1, w2 = -1;
  CHECK(sppc_pipe(&r1, &w1) == 0 && sppc_pipe(&r2, &w2) == 0);
  CHECK(sppc_dup2(w1, w2) == 0);
  CHECK(at_eof(r2));
  CHECK(put(w2, "into one") == 0);
  CHECK(got(r1, "into one"));
  CHECK(sppc_dup2(w1, w1) == 0);
  CHECK(sppc_dup2(-1, w1) == SPPC_EBADF);
  CHECK(sppc_dup2(w1, -1) == SPPC_EBADF);
  CHECK(sppc_dup2(w1, _SPPC_FD_MAX) == SPPC_EBADF);
  CHECK(sppc_dup2(r2, r2) == 0);

  // Far past the table's end: it grows.
  CHECK(sppc_dup2(w1, 5000) == 0);
  CHECK(put(5000, "far") == 0);
  CHECK(got(r1, "far"));
  CHECK(sppc_close(5000) == 0);
  sppc_close(r1);
  sppc_close(w1);
  sppc_close(r2);
  sppc_close(w2);
}

static void check_limit(void) {
  // Every fd up to the limit, all naming one pipe end; then EMFILE.
  int r = -1, w = -1;
  CHECK(sppc_pipe(&r, &w) == 0);
  int last = -1, made = 0, rc = 0;
  while ((rc = sppc_dup(w, &last)) == 0) { ++made; }
  CHECK(rc == SPPC_EMFILE && last == -1);
  CHECK(made > _SPPC_FD_MAX - 16);
  for (int fd = 0; fd < _SPPC_FD_MAX; ++fd) {
    if (fd != r && fd > 2) { sppc_close(fd); }
  }
  CHECK(at_eof(r)); // every name of the write end closed
  sppc_close(r);
}

// ==================== STANDARD STREAMS ====================

static int swap_in_pipe(const int fd, int *saved, int *other_end) {
  int r = -1, w = -1;
  if (sppc_pipe(&r, &w) != 0 || sppc_dup(fd, saved) != 0) { return -1; }
  sppc_dup2(fd == 0 ? r : w, fd);
  sppc_close(fd == 0 ? r : w);
  *other_end = fd == 0 ? w : r;
  return 0;
}

static void restore(const int fd, const int saved) {
  sppc_dup2(saved, fd);
  sppc_close(saved);
}

static void check_streams(void) {
  int saved = -1, end = -1;
  ssize_t n = 0;
  const HANDLE before = GetStdHandle(STD_OUTPUT_HANDLE);

  // stdout into a pipe, and the process's standard handle with it.
  CHECK(swap_in_pipe(1, &saved, &end) == 0);
  CHECK(GetStdHandle(STD_OUTPUT_HANDLE) != before);
  const int out_rc = sppc_stdout_write("to stdout", 1, 9, &n);
  restore(1, saved);
  CHECK(out_rc == 0 && n == 9);
  CHECK(got(end, "to stdout"));
  CHECK(GetStdHandle(STD_OUTPUT_HANDLE) == before);
  sppc_close(end);

  CHECK(swap_in_pipe(2, &saved, &end) == 0);
  const int err_rc = sppc_stderr_write("to stderr", 3, 3, &n);
  restore(2, saved);
  CHECK(err_rc == 0 && n == 9);
  CHECK(got(end, "to stderr"));
  sppc_close(end);

  CHECK(swap_in_pipe(0, &saved, &end) == 0);
  CHECK(put(end, "from stdin") == 0);
  sppc_close(end);
  char buf[32] = {0};
  int in_rc = sppc_stdin_read(buf, 1, sizeof buf, &n);
  CHECK(in_rc == 0 && n == 10 && memcmp(buf, "from stdin", 10) == 0);
  in_rc = sppc_stdin_read(buf, 1, sizeof buf, &n); // the writer is gone: end of input
  CHECK(in_rc == 0 && n == 0);
  restore(0, saved);
}

// ==================== THREADS ====================

static volatile LONG thread_failures = 0;

static void *churn(void *arg) {
  (void)arg;
  for (int i = 0; i < 500; ++i) {
    int r = -1, w = -1, copy = -1;
    if (sppc_pipe(&r, &w) != 0 || sppc_dup(w, &copy) != 0) {
      InterlockedIncrement(&thread_failures);
      continue;
    }
    sppc_close(w);
    if (put(copy, "ping") != 0 || !got(r, "ping")) { InterlockedIncrement(&thread_failures); }
    sppc_close(copy);
    if (!at_eof(r)) { InterlockedIncrement(&thread_failures); }
    sppc_close(r);
  }
  return NULL;
}

static void check_threads(void) {
  pthread_t t[8];
  for (int i = 0; i < 8; ++i) { pthread_create(&t[i], NULL, churn, NULL); }
  for (int i = 0; i < 8; ++i) { pthread_join(t[i], NULL); }
  CHECK(thread_failures == 0);
}

// ==================== CONSOLES ====================

/// A screen buffer of this process's console, as an fd, for writing to and
/// reading back from without showing anything; -1 without a console.
static int console_screen(HANDLE *out) {
  const HANDLE screen = CreateConsoleScreenBuffer(GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
    NULL, CONSOLE_TEXTMODE_BUFFER, NULL);
  if (screen == INVALID_HANDLE_VALUE) { return -1; }
  *out = screen;
  int fd = -1;
  return _sppc_fd_adopt(screen, _SPPC_FD_WRITE, true, &fd) == 0 ? fd : -1;
}

static void check_console_out(void) {
  HANDLE screen = NULL;
  const int fd = console_screen(&screen);
  if (fd < 0) {
    printf("no console: its output not checked\n");
    return;
  }

  // "h\u00E9llo \u20AC!" in UTF-8, cut in the middle of the e-acute and of the euro sign, and a byte
  // that starts nothing (as U+FFFD); every write takes all its bytes.
  ssize_t n = 0;
  CHECK(sppc_write("h\xC3", 1, 2, fd, &n) == 0 && n == 2);
  CHECK(sppc_write("\xA9llo \xE2\x82", 1, 7, fd, &n) == 0 && n == 7);
  CHECK(sppc_write("\xAC!", 1, 2, fd, &n) == 0 && n == 2);
  CHECK(sppc_write("\xFF", 1, 1, fd, &n) == 0 && n == 1);

  wchar_t shown[16] = {0};
  DWORD read = 0;
  const COORD origin = {0, 0};
  CHECK(ReadConsoleOutputCharacterW(screen, shown, 9, origin, &read) && read == 9);
  CHECK(wmemcmp(shown, L"h\u00E9llo \u20AC!\uFFFD", 9) == 0);

  // Reading a screen is refused, as reading a write-only fd.
  char c;
  CHECK(sppc_read(&c, 1, 1, fd, &n) == SPPC_EBADF);
  CHECK(sppc_close(fd) == 0);
}

static void type_into(HANDLE in, wchar_t const *text) {
  INPUT_RECORD keys[64];
  DWORD count = 0;
  for (wchar_t const *c = text; *c; ++c) {
    for (int down = 1; down >= 0; --down) {
      INPUT_RECORD *const k = &keys[count++];
      memset(k, 0, sizeof *k);
      k->EventType = KEY_EVENT;
      k->Event.KeyEvent.bKeyDown = down;
      k->Event.KeyEvent.wRepeatCount = 1;
      k->Event.KeyEvent.uChar.UnicodeChar = *c;
      k->Event.KeyEvent.wVirtualKeyCode = *c == L'\r' ? VK_RETURN : 0;
    }
  }
  DWORD wrote = 0;
  WriteConsoleInputW(in, keys, count, &wrote);
}

static void check_console_in(void) {
  const HANDLE in = CreateFileW(L"CONIN$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
    OPEN_EXISTING, 0, NULL);
  if (in == INVALID_HANDLE_VALUE) {
    printf("no console: its input not checked\n");
    return;
  }
  DWORD mode = 0;
  GetConsoleMode(in, &mode);
  SetConsoleMode(in, ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT); // no echo onto the screen
  FlushConsoleInputBuffer(in);
  int fd = -1;
  HANDLE mine = NULL;
  DuplicateHandle(GetCurrentProcess(), in, GetCurrentProcess(), &mine, 0, FALSE, DUPLICATE_SAME_ACCESS);
  CHECK(_sppc_fd_adopt(mine, _SPPC_FD_READ, true, &fd) == 0);

  // A typed line comes back in UTF-8, ending in "\n" alone; one longer than
  // a read is handed out over several.
  type_into(in, L"h\u00E9 \u20AC\r");
  char buf[32] = {0};
  ssize_t n = 0;
  CHECK(sppc_read(buf, 1, 3, fd, &n) == 0 && n == 3 && memcmp(buf, "h\xC3\xA9", 3) == 0);
  CHECK(sppc_read(buf, 1, sizeof buf, fd, &n) == 0 && n == 5 && memcmp(buf, " \xE2\x82\xAC\n", 5) == 0);

  // Ctrl+Z at the start of a line is the end of input.
  type_into(in, L"\x1A\r");
  CHECK(sppc_read(buf, 1, sizeof buf, fd, &n) == 0 && n == 0);
  CHECK(sppc_write("x", 1, 1, fd, &n) == SPPC_EBADF);

  sppc_close(fd);
  SetConsoleMode(in, mode);
  CloseHandle(in);
}

int main(void) {
  CHECK(sppc_init() == 0);
  check_pipes();
  check_close();
  check_dup();
  check_limit();
  check_streams();
  check_threads();
  check_console_out();
  check_console_in();
  CHECK(sppc_cleanup() == 0);
  return check_done();
}

#endif
