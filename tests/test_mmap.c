// Regression test: sppc_mmap must hand back the mapping and report failure.
//
// It used to take the destination as a parameter, pass it to mmap as the
// placement hint, and throw the result away, so the caller never learned where
// the mapping landed. It also tested the result against NULL, but mmap signals
// failure with MAP_FAILED, so every error was reported as success.
//
// It now returns the mapping like the other allocators: NULL on failure with
// errno set.

#include <sppc/sppc.h>
#include "harness.h"

int main(void) {
  TEST_BEGIN("mmap");
  sppc_init();
  char detail[96];

  const size_t len = 4096;

  // --- anonymous mapping is returned and usable ---
  void *addr = sppc_mmap(-1, len, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, 0);
  snprintf(detail, sizeof detail, "addr=%p", addr);
  CHECK("anonymous mmap returns a mapping", addr != NULL && addr != MAP_FAILED, detail);

  if (addr != NULL && addr != MAP_FAILED) {
    // Writing through it proves the mapping is real and read-write.
    memset(addr, 0x5A, len);
    unsigned char *p = addr;
    int intact = 1;
    for (size_t i = 0; i < len; i++) if (p[i] != 0x5A) { intact = 0; break; }
    CHECK("mapping is readable and writable", intact, intact ? "" : "content mismatch");
    CHECK_EQ("munmap round-trips", sppc_munmap(addr, &len), 0);
  }

  // --- failure is NULL, never MAP_FAILED, and errno is set ---
  errno = 0;
  void *bad = sppc_mmap(-1, 0, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, 0);
  snprintf(detail, sizeof detail, "ret=%p errno=%d EINVAL=%d", bad, errno, EINVAL);
  CHECK("length 0 returns NULL with EINVAL", bad == NULL && errno == EINVAL, detail);

  errno = 0;
  void *bad2 = sppc_mmap(-1, len, PROT_READ, MAP_PRIVATE, 0);   // file map, bad fd
  snprintf(detail, sizeof detail, "ret=%p errno=%d EBADF=%d", bad2, errno, EBADF);
  CHECK("bad fd returns NULL with EBADF", bad2 == NULL && errno == EBADF, detail);

  CHECK("failure is never MAP_FAILED", bad != MAP_FAILED && bad2 != MAP_FAILED,
        "callers only have to test for NULL");

  // --- file-backed mapping reflects file contents ---
  char path[] = "/tmp/sppc_test_mmap_XXXXXX";
  int fd = -1;
  CHECK_EQ("mktemp", sppc_mktemp(path, &fd), 0);
  ssize_t written = 0;
  sppc_write("mapped-file-content", 1, 19, fd, &written);
  CHECK_EQ("  wrote 19 bytes", written, 19);

  void *fmap = sppc_mmap(fd, 19, PROT_READ, MAP_PRIVATE, 0);
  CHECK("file-backed mmap returns a mapping", fmap != NULL && fmap != MAP_FAILED,
        fmap ? "mapped" : "NULL");
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