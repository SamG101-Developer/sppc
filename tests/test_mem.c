#include "check.h"

// Memory: the heap, mappings and their protection, and the byte and string
// helpers, including the out-of-range and not-found cases each one reports.

// ==================== HEAP ====================

static void check_heap(void) {
  unsigned char *p = sppc_malloc(64);
  CHECK(p != NULL);
  if (p != NULL) { memset(p, 0xAB, 64); }

  // realloc keeps the contents, growing or shrinking.
  p = sppc_realloc(p, 4096);
  CHECK(p != NULL && p[0] == 0xAB && p[63] == 0xAB);
  p = sppc_realloc(p, 8);
  CHECK(p != NULL && p[7] == 0xAB);
  sppc_free(p);

  // realloc of NULL is malloc; free of NULL is nothing.
  p = sppc_realloc(NULL, 16);
  CHECK(p != NULL);
  sppc_free(p);
  sppc_free(NULL);

  unsigned char *z = sppc_calloc(100, 10);
  int all_zero = z != NULL;
  for (size_t i = 0; z != NULL && i < 1000; ++i) { all_zero = all_zero && z[i] == 0; }
  CHECK(all_zero);
  sppc_free(z);

  for (size_t align = 16; align <= 4096; align *= 4) {
    void *a = sppc_aligned_alloc(align * 2, align);
    CHECK(a != NULL && ((uintptr_t)a % align) == 0);
    sppc_free(a);
  }

  // A request no machine can meet fails rather than returning something. The
  // result is kept volatile, or the compiler drops an allocation nothing uses
  // and assumes it succeeded.
  volatile size_t huge = SIZE_MAX / 2 + 1;
  void *volatile big = sppc_malloc(huge);
  CHECK(big == NULL);
  sppc_free(big);
  big = sppc_calloc(huge, 4);
  CHECK(big == NULL);
  sppc_free(big);
}

// ==================== MAPPINGS ====================

static size_t page_size(void) {
  return (size_t)sysconf(_SC_PAGESIZE);
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

  // Protection really protects: a write to a read-only page faults.
  const size_t one = page_size();
  CHECK(sppc_memprotect(p, one, SPPC_PROT_READ) == 0);
  read_only_page = p;
  char err[4096];
#if SPPC_KERNEL_DARWIN
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

  // Shared memory is not private to this mapping, so dropping its pages here
  // keeps what is in them.
  char *s = sppc_mmap(-1, len, SPPC_PROT_READ | SPPC_PROT_WRITE, SPPC_MAP_SHARED | SPPC_MAP_ANONYMOUS, 0);
  CHECK(s != NULL);
  if (s == NULL) { return; }
  s[0] = 'a';
  s[len - 1] = 'z';
  CHECK(sppc_madvise(s, &len, SPPC_MADV_DONTNEED) == 0 && s[0] == 'a' && s[len - 1] == 'z');
  CHECK(sppc_munmap(s, &len) == 0);

  // Values sppc has no name for are refused before reaching the platform.
  CHECK(sppc_mmap(-1, one, 0x100, SPPC_MAP_PRIVATE | SPPC_MAP_ANONYMOUS, 0) == NULL);
  CHECK(sppc_mmap(-1, one, SPPC_PROT_READ, SPPC_MAP_PRIVATE | SPPC_MAP_ANONYMOUS | 0x40000000, 0) == NULL);
  CHECK(sppc_mmap(-1, 0, SPPC_PROT_READ, SPPC_MAP_PRIVATE | SPPC_MAP_ANONYMOUS, 0) == NULL); // empty
}

static void check_file_map(void) {
  char dir[64], path[128];
  check_temp_dir(dir, sizeof dir, "mem");
  snprintf(path, sizeof path, "%s/mapped", dir);
  const size_t len = page_size();

  int fd = -1;
  CHECK(sppc_open(path, SPPC_O_RDWR | SPPC_O_CREAT, 0600, &fd) == 0);
  CHECK(sppc_ftruncate(fd, (int64_t)len) == 0);

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
    CHECK(sppc_munmap(p, &len) == 0);
  }

  // A file opened read-only cannot be mapped for shared writing.
  int ro = -1;
  CHECK(sppc_open(path, SPPC_O_RDONLY, 0, &ro) == 0);
  CHECK(sppc_mmap(ro, len, SPPC_PROT_READ | SPPC_PROT_WRITE, SPPC_MAP_SHARED, 0) == NULL);
  sppc_close(ro);

  sppc_close(fd);
  CHECK(sppc_unlink(path) == 0 && sppc_rmdir(dir) == 0);
}

// ==================== BYTES ====================

static void check_bytes(void) {
  char buf[16];

  // memcpy and memset take an offset into each buffer.
  memset(buf, '.', sizeof buf);
  CHECK(sppc_memcpy(buf, "0123456789", 3, 2, 5) == 0 && memcmp(buf, "..567...", 8) == 0);
  CHECK(sppc_memset(buf, 'x', 2, 6) == 0 && memcmp(buf, "..567.xx.", 9) == 0);
  CHECK(sppc_memcpy(buf, "abc", 0, 0, 0) == 0 && buf[0] == '.'); // nothing to copy

  // memmove: overlapping either way.
  memcpy(buf, "abcdefgh", 8);
  CHECK(sppc_memmove(buf + 2, buf, 5) == 0 && memcmp(buf, "ababcdeh", 8) == 0);
  memcpy(buf, "abcdefgh", 8);
  CHECK(sppc_memmove(buf, buf + 2, 5) == 0 && memcmp(buf, "cdefgfgh", 8) == 0);

  // memcmp gives the sign only.
  int out = 99;
  CHECK(sppc_memcmp("abc", "abd", 3, &out) == 0 && out == -1);
  CHECK(sppc_memcmp("abd", "abc", 3, &out) == 0 && out == 1);
  CHECK(sppc_memcmp("abc", "abd", 2, &out) == 0 && out == 0);
  CHECK(sppc_memcmp("\x80", "\x01", 1, &out) == 0 && out == 1); // bytes compare unsigned

  // memmem: the offset of the first match, which may contain a NUL; nothing
  // found is SPPC_ENOENT with the index left alone.
  size_t idx = 999;
  CHECK(sppc_memmem("ab\0cab\0c", 8, "b\0c", 3, &idx) == 0 && idx == 1);
  CHECK(sppc_memmem("hello", 5, "lo", 2, &idx) == 0 && idx == 3);
  idx = 999;
  CHECK(sppc_memmem("hello", 5, "xyz", 3, &idx) == SPPC_ENOENT && idx == 999);
  CHECK(sppc_memmem("hello", 3, "lo", 2, &idx) == SPPC_ENOENT); // past the length given
}

// ==================== STRINGS ====================

static void check_strings(void) {
  char buf[32];
  CHECK(sppc_strcpy(buf, "hello") == 0 && strcmp(buf, "hello") == 0);
  CHECK(sppc_strcat(buf, ", world") == 0 && strcmp(buf, "hello, world") == 0);
  CHECK(sppc_strcpy(buf, "") == 0 && buf[0] == '\0');

  bool same = false;
  CHECK(sppc_strcmp("abc", "abc", &same) == 0 && same);
  CHECK(sppc_strcmp("abc", "abC", &same) == 0 && !same);
  CHECK(sppc_strcmp("abc", "abcd", &same) == 0 && !same);
  CHECK(sppc_strcasecmp("aBc", "AbC", &same) == 0 && same);
  CHECK(sppc_strcasecmp("abc", "abd", &same) == 0 && !same);

  size_t idx = 999;
  CHECK(sppc_strchr("banana", 'n', &idx) == 0 && idx == 2);
  CHECK(sppc_strrchr("banana", 'n', &idx) == 0 && idx == 4);
  CHECK(sppc_strchr("banana", '\0', &idx) == 0 && idx == 6); // the terminator: how the stl measures a c string
  CHECK(sppc_strstr("banana", "ana", &idx) == 0 && idx == 1);
  CHECK(sppc_strrstr("banana", "ana", &idx) == 0 && idx == 3); // overlapping matches count
  CHECK(sppc_strrstr("banana", "", &idx) == 0 && idx == 6);
  CHECK(sppc_strstr("banana", "", &idx) == 0 && idx == 0);
  CHECK(sppc_strcasestr("BaNaNa", "nan", &idx) == 0 && idx == 2);
  CHECK(sppc_strpbrk("banana", "xyn", &idx) == 0 && idx == 2);

  // Nothing found: SPPC_ENOENT and the index untouched, whatever errno held.
  idx = 999;
  errno = 0;
  CHECK(sppc_strchr("banana", 'z', &idx) == SPPC_ENOENT);
  CHECK(sppc_strrchr("banana", 'z', &idx) == SPPC_ENOENT);
  CHECK(sppc_strstr("banana", "nab", &idx) == SPPC_ENOENT);
  CHECK(sppc_strrstr("banana", "nab", &idx) == SPPC_ENOENT);
  CHECK(sppc_strcasestr("banana", "NAB", &idx) == SPPC_ENOENT);
  CHECK(sppc_strpbrk("banana", "xyz", &idx) == SPPC_ENOENT);
  errno = EINTR;
  CHECK(sppc_strstr("banana", "nab", &idx) == SPPC_ENOENT);
  CHECK(idx == 999);

  char *dup = sppc_strdup("copy");
  CHECK(dup != NULL && strcmp(dup, "copy") == 0);
  sppc_free(dup);
}

int main(void) {
  if (sppc_init() != 0) { return 1; }
  check_heap();
  check_anonymous_map();
  check_file_map();
  check_bytes();
  check_strings();
  sppc_cleanup();
  return check_done();
}
