#include "check.h"

// Memory: mappings and their protection, including the out-of-range cases
// each one reports. The heap and the byte and string helpers, which every
// platform has, are test_heap.c's.

// ==================== MAPPINGS ====================

static size_t page_size(void) {
#if SPPC_PLATFORM_WINDOWS
  SYSTEM_INFO info;
  GetSystemInfo(&info);
  return (size_t)info.dwPageSize;
#else
  return (size_t)sysconf(_SC_PAGESIZE);
#endif
}

static char *read_only_page;

static void write_to_read_only(void) {
  read_only_page[0] = 'x';
}

static void check_anonymous_map(void) {
  const size_t len = 4 * page_size();
  char *p = sppc_mmap(-1, len, SPPC_PROT_READ | SPPC_PROT_WRITE, SPPC_MAP_PRIVATE | SPPC_MAP_ANONYMOUS, 0);
  CHECK(p != NULL);
  if (p == NULL) { return; }
  CHECK(p[0] == 0 && p[len - 1] == 0); // fresh anonymous memory is zeroed
  p[0] = 'a';
  p[len - 1] = 'z';

  CHECK(sppc_madvise(p, &len, SPPC_MADV_NORMAL) == 0);
  CHECK(sppc_madvise(p, &len, SPPC_MADV_SEQUENTIAL) == 0);
  CHECK(sppc_madvise(p, &len, SPPC_MADV_RANDOM) == 0);
  CHECK(sppc_madvise(p, &len, SPPC_MADV_WILLNEED) == 0);
  CHECK(sppc_madvise(p, &len, 77) == SPPC_EINVAL);

#if SPPC_KERNEL_FREEBSD
  // FreeBSD cannot give Linux's MADV_DONTNEED, so refuses it, keeping the
  // contents.
  CHECK(sppc_madvise(p, &len, SPPC_MADV_DONTNEED) == SPPC_ENOSYS && p[0] == 'a' && p[len - 1] == 'z');
  CHECK(sppc_mlock(p, page_size()) == 0);
  CHECK(sppc_munlock(p, page_size()) == 0);
#else
  // MADV_DONTNEED on private anonymous memory throws the contents away.
  CHECK(sppc_madvise(p, &len, SPPC_MADV_DONTNEED) == 0);
  CHECK(p[0] == 0 && p[len - 1] == 0);

  // One page locked and unlocked: within the smallest default limit. Locked
  // pages cannot be dropped.
  CHECK(sppc_mlock(p, page_size()) == 0);
  p[0] = 'a';
  CHECK(sppc_madvise(p, &len, SPPC_MADV_DONTNEED) == SPPC_EINVAL && p[0] == 'a');
  CHECK(sppc_munlock(p, page_size()) == 0);
  CHECK(sppc_madvise(p, &len, SPPC_MADV_DONTNEED) == 0 && p[0] == 0);
#endif

  // Protection really protects: a write to a read-only page faults.
  const size_t one = page_size();
  CHECK(sppc_memprotect(p, one, SPPC_PROT_READ) == 0);
  read_only_page = p;
  char err[4096];
#if SPPC_PLATFORM_WINDOWS
  // No crash reporter on Windows yet (PORTING.md): the process ends with the
  // access violation's own status.
  CHECK(check_dies(write_to_read_only, err, sizeof err) == (int)0xC0000005);
  (void)err;
#elif SPPC_KERNEL_DARWIN
  // Darwin reports a write to a protected page as a bus error.
  CHECK(check_dies(write_to_read_only, err, sizeof err) == 128 + SIGBUS);
  CHECK(strstr(err, "fatal signal: SIGBUS") != NULL);
#else
  CHECK(check_dies(write_to_read_only, err, sizeof err) == 128 + SIGSEGV);
  CHECK(strstr(err, "fatal signal: SIGSEGV") != NULL);
#endif
  CHECK(sppc_memprotect(p, one, SPPC_PROT_READ | SPPC_PROT_WRITE) == 0);
  p[0] = 'b';
  CHECK(p[0] == 'b');
  CHECK(sppc_memprotect(p, one, 0x100) == SPPC_EINVAL);
  CHECK(sppc_memprotect(p + 1, one, SPPC_PROT_READ) == SPPC_EINVAL); // not page aligned

  CHECK(sppc_munmap(p, &len) == 0);
  CHECK(sppc_munmap(p + 1, &len) == SPPC_EINVAL);
  CHECK(sppc_munmap(p, &len) == 0); // nothing mapped there now, which is no error
  CHECK(sppc_msync(p, &len, SPPC_MS_SYNC) == SPPC_ENOMEM);
  CHECK(sppc_memprotect(p, one, SPPC_PROT_READ) == SPPC_ENOMEM);

  // Part of a mapping unmapped; the rest stays, and goes later.
  const size_t three = 3 * one;
  p = sppc_mmap(-1, three, SPPC_PROT_READ | SPPC_PROT_WRITE, SPPC_MAP_PRIVATE | SPPC_MAP_ANONYMOUS, 0);
  CHECK(p != NULL);
  if (p != NULL) {
    p[0] = 'a';
    p[2 * one] = 'c';
    CHECK(sppc_munmap(p + one, &one) == 0);
    CHECK(sppc_msync(p + one, &one, SPPC_MS_ASYNC) == SPPC_ENOMEM); // the hole
    CHECK(sppc_msync(p, &one, SPPC_MS_ASYNC) == 0); // anonymous memory has nothing to write back
    CHECK(p[0] == 'a' && p[2 * one] == 'c');
    CHECK(sppc_munmap(p, &three) == 0);
  }

  // Unlocking memory that is not locked is no error.
  p = sppc_mmap(-1, one, SPPC_PROT_READ | SPPC_PROT_WRITE, SPPC_MAP_PRIVATE | SPPC_MAP_ANONYMOUS, 0);
  CHECK(p != NULL);
  if (p != NULL) {
    CHECK(sppc_munlock(p, one) == 0);
    CHECK(sppc_mlock(p, one) == 0 && sppc_munlock(p, one) == 0 && sppc_munlock(p, one) == 0);
    CHECK(sppc_munmap(p, &one) == 0);
  }

  // Shared memory is not private to this mapping, so dropping its pages here
  // keeps what is in them.
  char *s = sppc_mmap(-1, len, SPPC_PROT_READ | SPPC_PROT_WRITE, SPPC_MAP_SHARED | SPPC_MAP_ANONYMOUS, 0);
  CHECK(s != NULL);
  if (s == NULL) { return; }
  s[0] = 'a';
  s[len - 1] = 'z';
#if SPPC_KERNEL_FREEBSD
  CHECK(sppc_madvise(s, &len, SPPC_MADV_DONTNEED) == SPPC_ENOSYS && s[0] == 'a' && s[len - 1] == 'z');
#else
  CHECK(sppc_madvise(s, &len, SPPC_MADV_DONTNEED) == 0 && s[0] == 'a' && s[len - 1] == 'z');
#endif
  CHECK(sppc_munmap(s, &len) == 0);

  // Values sppc has no name for are refused before reaching the platform.
  CHECK(sppc_mmap(-1, one, 0x100, SPPC_MAP_PRIVATE | SPPC_MAP_ANONYMOUS, 0) == NULL);
  CHECK(sppc_mmap(-1, one, SPPC_PROT_READ, SPPC_MAP_PRIVATE | SPPC_MAP_ANONYMOUS | 0x40000000, 0) == NULL);
  CHECK(sppc_mmap(-1, 0, SPPC_PROT_READ, SPPC_MAP_PRIVATE | SPPC_MAP_ANONYMOUS, 0) == NULL); // empty
  CHECK(sppc_mmap(-1, one, SPPC_PROT_READ, SPPC_MAP_ANONYMOUS, 0) == NULL); // neither shared nor private
  CHECK(sppc_msync(s, &len, SPPC_MS_SYNC | SPPC_MS_ASYNC) == SPPC_EINVAL);
}

static void check_file_map(void) {
  char dir[300], path[340];
  check_temp_dir(dir, sizeof dir, "mem");
  snprintf(path, sizeof path, "%s/mapped", dir);
  const size_t len = page_size();

  int fd = -1;
  CHECK(sppc_open(path, SPPC_O_RDWR | SPPC_O_CREAT, 0600, &fd) == 0);
  CHECK(sppc_ftruncate(fd, (int64_t)(2 * len)) == 0);

  // A shared mapping's writes reach the file, and msync flushes them.
  char *p = sppc_mmap(fd, len, SPPC_PROT_READ | SPPC_PROT_WRITE, SPPC_MAP_SHARED, 0);
  CHECK(p != NULL);
  if (p != NULL) {
    memcpy(p, "through the map", 15);
    CHECK(sppc_msync(p, &len, SPPC_MS_SYNC) == 0);
    CHECK(sppc_msync(p, &len, SPPC_MS_ASYNC) == 0);
    CHECK(sppc_msync(p, &len, 0x40) == SPPC_EINVAL);
    char buf[16] = {0};
    ssize_t n = 0;
    CHECK(sppc_pread(fd, buf, 1, 15, 0, &n) == 0 && n == 15 && memcmp(buf, "through the map", 15) == 0);

    // And the file's writes show in the mapping.
    CHECK(sppc_pwrite(fd, "T", 1, 1, 0, &n) == 0);
    CHECK(p[0] == 'T');
    CHECK(sppc_munmap(p, &len) == 0);
  }

  // A private mapping's writes stay private.
  p = sppc_mmap(fd, len, SPPC_PROT_READ | SPPC_PROT_WRITE, SPPC_MAP_PRIVATE, 0);
  CHECK(p != NULL);
  if (p != NULL) {
    p[0] = 'P';
    char c = 0;
    ssize_t n = 0;
    CHECK(sppc_pread(fd, &c, 1, 1, 0, &n) == 0 && c == 'T');
    // and stay private when the page's protection changes and changes back.
    CHECK(sppc_memprotect(p, len, SPPC_PROT_READ) == 0 && p[0] == 'P');
    CHECK(sppc_memprotect(p, len, SPPC_PROT_READ | SPPC_PROT_WRITE) == 0);
    p[1] = 'Q';
    CHECK(sppc_msync(p, &len, SPPC_MS_SYNC) == 0);
    CHECK(sppc_pread(fd, &c, 1, 1, 0, &n) == 0 && c == 'T');
    CHECK(sppc_munmap(p, &len) == 0);
  }

  // A mapping from a page into the file (one page in, which Windows' views
  // cannot start at by themselves).
  ssize_t wrote = 0;
  CHECK(sppc_pwrite(fd, "second", 1, 6, (int64_t)len, &wrote) == 0 && wrote == 6);
  p = sppc_mmap(fd, len, SPPC_PROT_READ | SPPC_PROT_WRITE, SPPC_MAP_SHARED, (int64_t)len);
  CHECK(p != NULL);
  if (p != NULL) {
    CHECK(memcmp(p, "second", 6) == 0);
    p[0] = 'S';
    CHECK(sppc_msync(p, &len, SPPC_MS_SYNC) == 0);
    char c = 0;
    CHECK(sppc_pread(fd, &c, 1, 1, (int64_t)len, &wrote) == 0 && c == 'S');
    CHECK(sppc_munmap(p, &len) == 0);
  }
  CHECK(sppc_mmap(fd, len, SPPC_PROT_READ, SPPC_MAP_SHARED, 1) == NULL); // not a page's multiple

  // A file opened read-only cannot be mapped for shared writing.
  int ro = -1;
  CHECK(sppc_open(path, SPPC_O_RDONLY, 0, &ro) == 0);
  CHECK(sppc_mmap(ro, len, SPPC_PROT_READ | SPPC_PROT_WRITE, SPPC_MAP_SHARED, 0) == NULL);
  // Nor mapped for reading and made writable after.
  p = sppc_mmap(ro, len, SPPC_PROT_READ, SPPC_MAP_SHARED, 0);
  CHECK(p != NULL);
  if (p != NULL) {
    CHECK(p[0] == 'T');
    CHECK(sppc_memprotect(p, len, SPPC_PROT_READ | SPPC_PROT_WRITE) == SPPC_EACCES);
    CHECK(sppc_munmap(p, &len) == 0);
  }
#if SPPC_PLATFORM_WINDOWS
  // Windows' own limits (PORTING.md): a view goes whole, and stops at the
  // page holding the file's end.
  const size_t two = 2 * len;
  p = sppc_mmap(fd, two, SPPC_PROT_READ, SPPC_MAP_SHARED, 0);
  CHECK(p != NULL);
  if (p != NULL) {
    CHECK(sppc_munmap(p + len, &len) == SPPC_EINVAL && p[len] == 'S');
    CHECK(sppc_munmap(p, &two) == 0);
  }
  CHECK(sppc_mmap(fd, 3 * len, SPPC_PROT_READ, SPPC_MAP_SHARED, 0) == NULL);
  // Executable views, through a handle of their own (the descriptor's was
  // not opened to execute); a shared one still flushes to the file.
  p = sppc_mmap(fd, len, SPPC_PROT_READ | SPPC_PROT_EXEC, SPPC_MAP_PRIVATE, (int64_t)len);
  CHECK(p != NULL && p[0] == 'S');
  if (p != NULL) { CHECK(sppc_munmap(p, &len) == 0); }
  p = sppc_mmap(fd, len, SPPC_PROT_READ | SPPC_PROT_WRITE | SPPC_PROT_EXEC, SPPC_MAP_SHARED, (int64_t)len);
  CHECK(p != NULL);
  if (p != NULL) {
    p[1] = 'E';
    CHECK(sppc_msync(p, &len, SPPC_MS_SYNC) == 0 && sppc_munmap(p, &len) == 0);
    char c = 0;
    ssize_t n = 0;
    CHECK(sppc_pread(fd, &c, 1, 1, (int64_t)len + 1, &n) == 0 && c == 'E');
  }
#endif
  sppc_close(ro);

  sppc_close(fd);
  CHECK(sppc_unlink(path) == 0 && sppc_rmdir(dir) == 0);
}

int main(void) {
  if (sppc_init() != 0) { return 1; }
  check_child();
  check_anonymous_map();
  check_file_map();
  sppc_cleanup();
  return check_done();
}
