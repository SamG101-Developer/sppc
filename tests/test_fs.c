#include "check.h"

// Files, directories and descriptors, end to end in a temporary directory:
// what each call does on success, and the sppc error each one reports for the
// common ways it fails. The directory is empty again at the end, which is
// itself checked: anything a call left behind would stop its removal.

static char dir[64];

static void at(char *out, char const *name) {
  snprintf(out, 128, "%s/%s", dir, name);
}

static int make_file(char const *path, char const *contents) {
  int fd = -1;
  ssize_t n = 0;
  if (sppc_open(path, SPPC_O_WRONLY | SPPC_O_CREAT | SPPC_O_TRUNC | SPPC_O_CLOEXEC, 0644, &fd) != 0) { return -1; }
  const size_t len = strlen(contents);
  const int rc = sppc_write(contents, 1, len, fd, &n);
  sppc_close(fd);
  return rc == 0 && (size_t)n == len ? 0 : -1;
}

static int64_t size_of(char const *path) {
  struct sppc_stat st;
  return sppc_stat(path, &st) == 0 ? st.size : -1;
}

static void sleep_ms(const uint64_t ms) {
  const sppc_timespec d = {0, ms * 1000000};
  sppc_clock_nanosleep(SPPC_CLOCK_MONOTONIC, 0, &d);
}

// ==================== READ AND WRITE ====================

static void check_read_write(void) {
  char path[128];
  at(path, "rw");
  int fd = -1;
  ssize_t n = 0;
  CHECK(sppc_open(path, SPPC_O_RDWR | SPPC_O_CREAT | SPPC_O_EXCL, 0640, &fd) == 0 && fd >= 0);

  // "size" and "count" multiply, as fwrite's do.
  CHECK(sppc_write("abcdefgh", 2, 4, fd, &n) == 0 && n == 8);
  int64_t pos = -1;
  CHECK(sppc_lseek(fd, 0, SPPC_SEEK_CUR, &pos) == 0 && pos == 8);
  CHECK(sppc_lseek(fd, -3, SPPC_SEEK_END, &pos) == 0 && pos == 5);
  char buf[16] = {0};
  CHECK(sppc_read(buf, 1, sizeof buf, fd, &n) == 0 && n == 3 && memcmp(buf, "fgh", 3) == 0);
  CHECK(sppc_read(buf, 1, sizeof buf, fd, &n) == 0 && n == 0); // end of file

  // pread / pwrite leave the offset alone.
  CHECK(sppc_pread(fd, buf, 1, 4, 2, &n) == 0 && n == 4 && memcmp(buf, "cdef", 4) == 0);
  CHECK(sppc_pwrite(fd, "XY", 1, 2, 0, &n) == 0 && n == 2);
  CHECK(sppc_lseek(fd, 0, SPPC_SEEK_CUR, &pos) == 0 && pos == 8);
  CHECK(sppc_pread(fd, buf, 1, 8, 0, &n) == 0 && n == 8 && memcmp(buf, "XYcdefgh", 8) == 0);
  CHECK(sppc_pread(fd, buf, 1, 8, 100, &n) == 0 && n == 0);

  // A write past the end leaves a hole that reads as zeros.
  CHECK(sppc_pwrite(fd, "Z", 1, 1, 12, &n) == 0 && n == 1);
  CHECK(sppc_pread(fd, buf, 1, 5, 8, &n) == 0 && n == 5 && memcmp(buf, "\0\0\0\0Z", 5) == 0);

  // writev gathers in order.
  char one[] = "one", two[] = "-two";
  sppc_iovec iov[2] = {{one, 3}, {two, 4}};
  CHECK(sppc_lseek(fd, 0, SPPC_SEEK_SET, &pos) == 0);
  CHECK(sppc_writev(fd, iov, 2, &n) == 0 && n == 7);
  CHECK(sppc_pread(fd, buf, 1, 7, 0, &n) == 0 && memcmp(buf, "one-two", 7) == 0);

  CHECK(sppc_fsync(fd) == 0);
  CHECK(sppc_fdatasync(fd) == 0);

  // The mode asked for, less the usual umask bits (0640 has none of them).
  struct sppc_stat st;
  CHECK(sppc_fstat(fd, &st) == 0 && (st.mode & 0777) == 0640 && st.size == 13);
  CHECK(sppc_close(fd) == 0);

  // Errors.
  CHECK(sppc_open(path, SPPC_O_RDWR | SPPC_O_CREAT | SPPC_O_EXCL, 0640, &fd) == SPPC_EEXIST && fd == -1);
  CHECK(sppc_read(buf, 1, 1, fd, &n) == SPPC_EBADF && n == -1);
  CHECK(sppc_close(fd) == SPPC_EBADF);
  CHECK(sppc_fsync(-1) == SPPC_EBADF);
  CHECK(sppc_unlink(path) == 0);
}

static void check_open_flags(void) {
  char path[128], link[128];
  at(path, "flags");
  at(link, "flags-link");
  CHECK(make_file(path, "12345") == 0);

  int fd = -1;
  ssize_t n = 0;
  // O_APPEND: every write lands at the end, wherever the offset was.
  CHECK(sppc_open(path, SPPC_O_WRONLY | SPPC_O_APPEND, 0, &fd) == 0);
  int64_t pos = -1;
  CHECK(sppc_lseek(fd, 0, SPPC_SEEK_SET, &pos) == 0);
  CHECK(sppc_write("67", 1, 2, fd, &n) == 0 && n == 2);
  sppc_close(fd);
  CHECK(size_of(path) == 7);

  // O_TRUNC empties it.
  CHECK(sppc_open(path, SPPC_O_WRONLY | SPPC_O_TRUNC, 0, &fd) == 0);
  sppc_close(fd);
  CHECK(size_of(path) == 0);

  // A read-only descriptor refuses a write.
  CHECK(sppc_open(path, SPPC_O_RDONLY, 0, &fd) == 0);
  CHECK(sppc_write("x", 1, 1, fd, &n) == SPPC_EBADF);
  sppc_close(fd);

  // O_DIRECTORY on a file, O_NOFOLLOW on a link.
  CHECK(sppc_open(path, SPPC_O_RDONLY | SPPC_O_DIRECTORY, 0, &fd) == SPPC_ENOTDIR);
  CHECK(sppc_open(dir, SPPC_O_RDONLY | SPPC_O_DIRECTORY, 0, &fd) == 0);
  sppc_close(fd);
  CHECK(sppc_symlink(path, link) == 0);
  CHECK(sppc_open(link, SPPC_O_RDONLY | SPPC_O_NOFOLLOW, 0, &fd) == SPPC_ELOOP);
  CHECK(sppc_open(dir, SPPC_O_WRONLY, 0, &fd) == SPPC_EISDIR);

  CHECK(sppc_unlink(link) == 0);
  CHECK(sppc_unlink(path) == 0);
}

static void check_dup(void) {
  char a[128], b[128];
  at(a, "dup-a");
  at(b, "dup-b");
  CHECK(make_file(a, "aaaa") == 0);
  CHECK(make_file(b, "bbbb") == 0);
  int fa = -1, fb = -1, copy = -1;
  CHECK(sppc_open(a, SPPC_O_RDONLY, 0, &fa) == 0);
  CHECK(sppc_open(b, SPPC_O_RDONLY, 0, &fb) == 0);

  // A duplicate shares the offset.
  CHECK(sppc_dup(fa, &copy) == 0 && copy >= 0 && copy != fa);
  char buf[4];
  ssize_t n = 0;
  CHECK(sppc_read(buf, 1, 2, fa, &n) == 0 && n == 2);
  int64_t pos = -1;
  CHECK(sppc_lseek(copy, 0, SPPC_SEEK_CUR, &pos) == 0 && pos == 2);

  // dup2 closes the target and puts the source in its place.
  CHECK(sppc_dup2(fa, fb) == 0);
  CHECK(sppc_read(buf, 1, 2, fb, &n) == 0 && n == 2 && memcmp(buf, "aa", 2) == 0);
  CHECK(sppc_dup2(fa, fa) == 0); // onto itself: nothing to do

  CHECK(sppc_dup(-1, &copy) == SPPC_EBADF && copy == -1);
  CHECK(sppc_dup2(-1, fb) == SPPC_EBADF);
  sppc_close(fa);
  sppc_close(fb);
  CHECK(sppc_unlink(a) == 0 && sppc_unlink(b) == 0);
}

static void check_truncate(void) {
  char path[128];
  at(path, "trunc");
  CHECK(make_file(path, "0123456789") == 0);
  CHECK(sppc_truncate(path, 4) == 0 && size_of(path) == 4);
  CHECK(sppc_truncate(path, 100) == 0 && size_of(path) == 100);

  int fd = -1;
  CHECK(sppc_open(path, SPPC_O_RDWR, 0, &fd) == 0);
  CHECK(sppc_ftruncate(fd, 2) == 0 && size_of(path) == 2);
  sppc_close(fd);

  // A read-only descriptor cannot truncate; a missing path cannot be.
  CHECK(sppc_open(path, SPPC_O_RDONLY, 0, &fd) == 0);
  const int rc = sppc_ftruncate(fd, 0);
  CHECK(rc == SPPC_EINVAL || rc == SPPC_EBADF);
  sppc_close(fd);
  CHECK(sppc_truncate("/nonexistent/sppc", 0) == SPPC_ENOENT);
  CHECK(sppc_unlink(path) == 0);
}

static void check_copy(void) {
  char src[128], dst[128];
  at(src, "copy-src");
  at(dst, "copy-dst");
  CHECK(make_file(src, "copy me please") == 0);

  int in = -1, out = -1;
  ssize_t n = 0;
  CHECK(sppc_open(src, SPPC_O_RDONLY, 0, &in) == 0);
  CHECK(sppc_open(dst, SPPC_O_RDWR | SPPC_O_CREAT | SPPC_O_TRUNC, 0644, &out) == 0);

  // copy_file_range: from each descriptor's own offset, which it advances.
  CHECK(sppc_copyfile(in, out, 7, 0, &n) == 0 && n == 7);
  CHECK(sppc_copyfile(in, out, 100, 0, &n) == 0 && n == 7); // only what is left
  CHECK(sppc_copyfile(in, out, 100, 0, &n) == 0 && n == 0);
  CHECK(size_of(dst) == 14);
  CHECK(sppc_copyfile(in, out, 1, 1, &n) == SPPC_EINVAL && n == -1); // no flags defined yet

  // sendfile with an offset: from there, which it moves on, and not from the
  // descriptor's own offset (left at the end by the copy above).
  int64_t off = 5;
  CHECK(sppc_ftruncate(out, 0) == 0);
  int64_t pos = -1;
  CHECK(sppc_lseek(out, 0, SPPC_SEEK_SET, &pos) == 0);
  CHECK(sppc_sendfile(in, out, &off, 2, &n) == 0 && n == 2 && off == 7);
  char buf[4] = {0};
  CHECK(sppc_pread(out, buf, 1, 2, 0, &n) == 0 && n == 2 && memcmp(buf, "me", 2) == 0);
  CHECK(sppc_sendfile(-1, out, &off, 2, &n) == SPPC_EBADF && n == -1);

  sppc_close(in);
  sppc_close(out);
  CHECK(sppc_unlink(src) == 0 && sppc_unlink(dst) == 0);
}

// ==================== NAMES ====================

static void check_directories(void) {
  char sub[128], deep[128], file[128];
  at(sub, "sub");
  at(deep, "sub/deep");
  at(file, "sub/file");

  CHECK(sppc_mkdir(sub, 0700) == 0);
  struct sppc_stat st;
  CHECK(sppc_stat(sub, &st) == 0 && (st.mode & SPPC_S_IFMT) == SPPC_S_IFDIR && (st.mode & 0777) == 0700);
  CHECK(sppc_mkdir(sub, 0700) == SPPC_EEXIST);
  CHECK(sppc_mkdir(deep, 0700) == 0);
  CHECK(make_file(file, "x") == 0);

  char under_file[160];
  snprintf(under_file, sizeof under_file, "%s/nope", file);
  CHECK(sppc_mkdir(under_file, 0700) == SPPC_ENOTDIR);
  CHECK(sppc_mkdir("/nonexistent/sppc/dir", 0700) == SPPC_ENOENT);
  CHECK(sppc_mkdir(deep, SPPC_S_IFDIR | 0700) == SPPC_EINVAL); // permission bits only

  CHECK(sppc_rmdir(sub) == SPPC_ENOTEMPTY);
  CHECK(sppc_rmdir(file) == SPPC_ENOTDIR);
  CHECK(sppc_unlink(sub) == SPPC_EISDIR);
  CHECK(sppc_unlink(file) == 0);
  CHECK(sppc_rmdir(deep) == 0);
  CHECK(sppc_rmdir(sub) == 0);
  CHECK(sppc_rmdir(sub) == SPPC_ENOENT);
}

static void check_rename(void) {
  char a[128], b[128], d1[128], d2[128], d2f[128];
  at(a, "ren-a");
  at(b, "ren-b");
  at(d1, "ren-d1");
  at(d2, "ren-d2");
  at(d2f, "ren-d2/f");
  CHECK(make_file(a, "A") == 0);
  CHECK(make_file(b, "BB") == 0);

  // Onto an existing file: replaces it.
  CHECK(sppc_rename(a, b) == 0);
  CHECK(size_of(b) == 1 && size_of(a) == -1);
  CHECK(sppc_rename(a, b) == SPPC_ENOENT);

  // A directory onto a non-empty one is refused; a file onto a directory too.
  CHECK(sppc_mkdir(d1, 0700) == 0 && sppc_mkdir(d2, 0700) == 0);
  CHECK(make_file(d2f, "f") == 0);
  const int rc = sppc_rename(d1, d2);
  CHECK(rc == SPPC_ENOTEMPTY || rc == SPPC_EEXIST);
  CHECK(sppc_rename(b, d2) == SPPC_EISDIR);
  CHECK(sppc_rename(d1, d2f) == SPPC_ENOTDIR);

  CHECK(sppc_unlink(d2f) == 0 && sppc_rmdir(d2) == 0 && sppc_rmdir(d1) == 0 && sppc_unlink(b) == 0);
}

static void check_links(void) {
  char target[128], hard[128], soft[128], dangling[128];
  at(target, "target");
  at(hard, "hard");
  at(soft, "soft");
  at(dangling, "dangling");
  CHECK(make_file(target, "data") == 0);

  // A hard link is the same file under a second name.
  struct sppc_stat t, h;
  CHECK(sppc_link(target, hard) == 0);
  CHECK(sppc_stat(target, &t) == 0 && sppc_stat(hard, &h) == 0);
  CHECK(t.ino == h.ino && t.nlink == 2);
  CHECK(sppc_link(target, hard) == SPPC_EEXIST);
  CHECK(sppc_link(dir, dangling) == SPPC_EPERM); // not a directory

  // A symbolic link: stat follows it, lstat does not.
  CHECK(sppc_symlink("target", soft) == 0);
  struct sppc_stat s;
  CHECK(sppc_stat(soft, &s) == 0 && (s.mode & SPPC_S_IFMT) == SPPC_S_IFREG && s.ino == t.ino);
  CHECK(sppc_lstat(soft, &s) == 0 && (s.mode & SPPC_S_IFMT) == SPPC_S_IFLNK && s.size == 6);
  CHECK(sppc_symlink("target", soft) == SPPC_EEXIST);

  // readlink terminates what it reads, and says when it would not fit.
  char buf[16];
  CHECK(sppc_readlink(soft, buf, sizeof buf) == 0 && strcmp(buf, "target") == 0);
  CHECK(sppc_readlink(soft, buf, 7) == 0 && strcmp(buf, "target") == 0);  // just room for the terminator
  CHECK(sppc_readlink(soft, buf, 6) == SPPC_ERANGE && buf[0] == '\0');    // none
  CHECK(sppc_readlink(soft, buf, 0) == SPPC_ERANGE);
  CHECK(sppc_readlink(target, buf, sizeof buf) == SPPC_EINVAL);           // not a link
  CHECK(sppc_readlink(dangling, buf, sizeof buf) == SPPC_ENOENT);

  // A link to nothing: lstat sees it, stat does not.
  CHECK(sppc_symlink("nothing-here", dangling) == 0);
  CHECK(sppc_lstat(dangling, &s) == 0);
  CHECK(sppc_stat(dangling, &s) == SPPC_ENOENT);

  CHECK(sppc_unlink(hard) == 0);
  CHECK(sppc_stat(target, &t) == 0 && t.nlink == 1);
  CHECK(sppc_unlink(soft) == 0 && sppc_unlink(dangling) == 0 && sppc_unlink(target) == 0);
}

static void check_paths(void) {
  char real_dir[4096], buf[4096], sub[128], file[128], twisted[192];
  at(sub, "p");
  at(file, "p/file");
  CHECK(sppc_mkdir(sub, 0700) == 0);
  CHECK(make_file(file, "") == 0);

  // realpath resolves "." and ".." (and the temporary directory itself, which
  // may sit behind a link).
  CHECK(sppc_realpath(dir, real_dir) == 0 && real_dir[0] == '/');
  snprintf(twisted, sizeof twisted, "%s/./p/../p//file", dir);
  char want[4200];
  snprintf(want, sizeof want, "%s/p/file", real_dir);
  CHECK(sppc_realpath(twisted, buf) == 0 && strcmp(buf, want) == 0);
  snprintf(twisted, sizeof twisted, "%s/p/missing", dir);
  CHECK(sppc_realpath(twisted, buf) == SPPC_ENOENT);

  // chdir and getcwd agree.
  char old[4096];
  CHECK(sppc_getcwd(old, sizeof old) == 0 && old[0] == '/');
  CHECK(sppc_chdir(sub) == 0);
  snprintf(want, sizeof want, "%s/p", real_dir);
  CHECK(sppc_getcwd(buf, sizeof buf) == 0 && strcmp(buf, want) == 0);
  CHECK(sppc_getcwd(buf, 2) == SPPC_ERANGE);
  CHECK(sppc_chdir("file") == SPPC_ENOTDIR);
  CHECK(sppc_chdir("/nonexistent/sppc") == SPPC_ENOENT);
  CHECK(sppc_chdir(old) == 0);

  CHECK(sppc_unlink(file) == 0 && sppc_rmdir(sub) == 0);
}

// ==================== METADATA ====================

static void check_ownership(void) {
  char path[128], link[128];
  at(path, "own");
  at(link, "own-link");
  CHECK(make_file(path, "") == 0);
  CHECK(sppc_symlink("own", link) == 0);

  // Giving a file to its own owner and group is always allowed, and so is
  // leaving either alone (all bits set).
  struct sppc_stat st;
  CHECK(sppc_stat(path, &st) == 0);
  CHECK(sppc_chown(path, st.uid, st.gid) == 0);
  CHECK(sppc_chown(path, UINT32_MAX, UINT32_MAX) == 0);
  CHECK(sppc_lchown(link, st.uid, st.gid) == 0);
  CHECK(sppc_chown("/nonexistent/sppc", st.uid, st.gid) == SPPC_ENOENT);

  CHECK(sppc_unlink(link) == 0 && sppc_unlink(path) == 0);
}

static int ts_after(sppc_timespec a, sppc_timespec b) {
  return a.sec > b.sec || (a.sec == b.sec && a.nsec > b.nsec);
}

static void check_times(void) {
  char path[128], link[128];
  at(path, "times");
  at(link, "times-link");
  CHECK(make_file(path, "") == 0);
  CHECK(sppc_symlink("times", link) == 0);

  struct sppc_stat before, after, link_before, link_after;
  CHECK(sppc_stat(path, &before) == 0);
  sleep_ms(20);
  CHECK(sppc_utimensat(path, 0) == 0);
  CHECK(sppc_stat(path, &after) == 0 && ts_after(after.mtim, before.mtim));

  // Through the link without following it: the link moves, its target does not.
  CHECK(sppc_lstat(link, &link_before) == 0);
  before = after;
  sleep_ms(20);
  CHECK(sppc_utimensat(link, SPPC_AT_SYMLINK_NOFOLLOW) == 0);
  CHECK(sppc_lstat(link, &link_after) == 0 && ts_after(link_after.mtim, link_before.mtim));
  CHECK(sppc_stat(path, &after) == 0 && after.mtim.sec == before.mtim.sec && after.mtim.nsec == before.mtim.nsec);

  CHECK(sppc_utimensat(path, 0x4000) == SPPC_EINVAL);
  CHECK(sppc_utimensat("/nonexistent/sppc", 0) == SPPC_ENOENT);
  CHECK(sppc_unlink(link) == 0 && sppc_unlink(path) == 0);
}

static void check_access(void) {
  char path[128];
  at(path, "access");
  CHECK(make_file(path, "") == 0);
  int out = 0;
  CHECK(sppc_access(path, SPPC_F_OK, &out) == 0 && out == 0);
  CHECK(sppc_access("/nonexistent/sppc", SPPC_F_OK, &out) == SPPC_ENOENT && out == -1);
  CHECK(sppc_access(path, 0x40, &out) == SPPC_EINVAL);
  CHECK(sppc_unlink(path) == 0);
}

static void check_temp(void) {
  // Both fill in the X's and leave the rest of the template alone.
  char made[64];
  snprintf(made, sizeof made, "%s/t_XXXXXX", dir);
  CHECK(sppc_mktemp_dir(made) == 0 && strstr(made, "XXXXXX") == NULL && strncmp(made, dir, strlen(dir)) == 0);
  struct sppc_stat st;
  CHECK(sppc_stat(made, &st) == 0 && (st.mode & SPPC_S_IFMT) == SPPC_S_IFDIR && (st.mode & 0777) == 0700);
  CHECK(sppc_rmdir(made) == 0);

  char bad[] = "/tmp/sppc_no_template";
  CHECK(sppc_mktemp_dir(bad) == SPPC_EINVAL);
  char missing[] = "/nonexistent/sppc_XXXXXX";
  CHECK(sppc_mktemp_dir(missing) == SPPC_ENOENT);
}

int main(void) {
  if (sppc_init() != 0) { return 1; }
  check_temp_dir(dir, sizeof dir, "fs");
  check_read_write();
  check_open_flags();
  check_dup();
  check_truncate();
  check_copy();
  check_directories();
  check_rename();
  check_links();
  check_paths();
  check_ownership();
  check_times();
  check_access();
  check_temp();
  CHECK(sppc_rmdir(dir) == 0); // empty: nothing above left anything behind
  sppc_cleanup();
  return check_done();
}
