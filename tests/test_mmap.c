// Regression test: sppc_mmap must hand back the mapping and detect failure.
//
// It used to pass out_addr as mmap's placement hint and throw the result away,
// so the caller never learned where the mapping landed. It also tested the
// result against NULL, but mmap signals failure with MAP_FAILED, so every
// error was reported as success.

#include <sppc/sppc.h>
#include "harness.h"

int main(void) {
  TEST_BEGIN("mmap");
  sppc_init();
  char detail[96];

  const size_t len = 4096;

  // --- anonymous mapping is returned and usable ---
  void *addr = NULL;
  const int rc = sppc_mmap(-1, len, PROT_READ | PROT_WRITE,
                           MAP_PRIVATE | MAP_ANONYMOUS, 0, &addr);
  CHECK_EQ("anonymous mmap succeeds", rc, 0);
  snprintf(detail, sizeof detail, "addr=%p", addr);
  CHECK("out_addr was written", addr != NULL && addr != MAP_FAILED, detail);

  if (addr != NULL && addr != MAP_FAILED) {
    // Writing through it proves the mapping is real and read-write.
    memset(addr, 0x5A, len);
    unsigned char *p = addr;
    int intact = 1;
    for (size_t i = 0; i < len; i++) if (p[i] != 0x5A) { intact = 0; break; }
    CHECK("mapping is readable and writable", intact, intact ? "" : "content mismatch");
    CHECK_EQ("munmap round-trips", sppc_munmap(addr, &len), 0);
  }

  // --- failure must be reported, not swallowed ---
  void *bad = (void*)0x1234;
  const int rc_len = sppc_mmap(-1, 0, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, 0, &bad);
  snprintf(detail, sizeof detail, "rc=%d EINVAL=%d", rc_len, EINVAL);
  CHECK("length 0 reports EINVAL", rc_len == EINVAL, detail);
  snprintf(detail, sizeof detail, "out_addr=%p", bad);
  CHECK("out_addr untouched on failure", bad == (void*)0x1234, detail);

  void *bad2 = (void*)0x1234;
  const int rc_fd = sppc_mmap(-1, len, PROT_READ, MAP_PRIVATE, 0, &bad2);  // file map, bad fd
  snprintf(detail, sizeof detail, "rc=%d EBADF=%d", rc_fd, EBADF);
  CHECK("bad fd reports EBADF", rc_fd == EBADF, detail);

  // --- file-backed mapping reflects file contents ---
  char path[] = "/tmp/sppc_test_mmap_XXXXXX";
  int fd = -1;
  CHECK_EQ("mktemp", sppc_mktemp(path, &fd), 0);
  ssize_t written = 0;
  sppc_write("mapped-file-content", 1, 19, fd, &written);
  CHECK_EQ("  wrote 19 bytes", written, 19);

  void *fmap = NULL;
  const int rc_file = sppc_mmap(fd, 19, PROT_READ, MAP_PRIVATE, 0, &fmap);
  CHECK_EQ("file-backed mmap succeeds", rc_file, 0);
  CHECK("  contents match the file",
        fmap != NULL && fmap != MAP_FAILED && memcmp(fmap, "mapped-file-content", 19) == 0,
        fmap && fmap != MAP_FAILED ? (char*)fmap : "not mapped");
  if (fmap != NULL && fmap != MAP_FAILED) {
    const size_t flen = 19;
    CHECK_EQ("  munmap", sppc_munmap(fmap, &flen), 0);
  }
  close(fd);
  unlink(path);

  return TEST_SUMMARY();
}