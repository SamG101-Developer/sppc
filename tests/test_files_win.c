#include "check.h"

// Files on Windows (platform/windows.h, PATHS / METADATA / FILES): what
// test_fs.c takes for granted from POSIX, made to hold there. A name goes
// at once, even while the file is open, and a rename replaces a file open
// or read-only; the read-only attribute is the owner's write bit, through
// chmod, stat and access; a program is one by its extension; fcntl's flags
// and record locks; a non-ASCII name crosses intact; and what a file has
// that a pipe does not (ESPIPE, the type stat reports).

#if !SPPC_PLATFORM_WINDOWS

int main(void) {
  printf("skipped: Windows only\n");
  return CHECK_SKIP_STATUS;
}

#else

static char dir[256];

static void at(char *out, char const *name) {
  snprintf(out, 300, "%s\\%s", dir, name);
}

static int make_file(char const *path, char const *contents) {
  int fd = -1;
  ssize_t n = 0;
  if (sppc_open(path, SPPC_O_WRONLY | SPPC_O_CREAT | SPPC_O_TRUNC, 0644, &fd) != 0) { return -1; }
  const int rc = sppc_write(contents, 1, strlen(contents), fd, &n);
  sppc_close(fd);
  return rc;
}

static bool exists(char const *path) {
  struct sppc_stat st;
  return sppc_lstat(path, &st) == 0;
}

// ==================== NAMES ====================

static void check_names_while_open(void) {
  char a[300], b[300];
  at(a, "open-a");
  at(b, "open-b");
  CHECK(make_file(a, "aaaa") == 0);
  CHECK(make_file(b, "bbbb") == 0);

  // Unlinked while open: the name is gone at once, the file readable to the end.
  int fd = -1;
  ssize_t n = 0;
  char buf[8] = {0};
  CHECK(sppc_open(a, SPPC_O_RDONLY, 0, &fd) == 0);
  CHECK(sppc_unlink(a) == 0);
  CHECK(!exists(a));
  CHECK(make_file(a, "new") == 0); // and the name is free again
  CHECK(sppc_read(buf, 1, sizeof buf, fd, &n) == 0 && n == 4 && memcmp(buf, "aaaa", 4) == 0);
  sppc_close(fd);

  // Renamed onto a file that is open, and onto one that is read-only.
  CHECK(sppc_open(b, SPPC_O_RDONLY, 0, &fd) == 0);
  CHECK(sppc_rename(a, b) == 0);
  CHECK(!exists(a));
  sppc_close(fd);
  CHECK(make_file(a, "again") == 0);
  CHECK(sppc_chmod(b, 0444) == 0);
  CHECK(sppc_rename(a, b) == 0);
  CHECK(sppc_open(b, SPPC_O_RDONLY, 0, &fd) == 0);
  CHECK(sppc_read(buf, 1, sizeof buf, fd, &n) == 0 && n == 5 && memcmp(buf, "again", 5) == 0);
  sppc_close(fd);

  // A read-only file goes, as POSIX asks only for its directory's leave.
  CHECK(sppc_chmod(b, 0444) == 0);
  CHECK(sppc_unlink(b) == 0 && !exists(b));
}

static void check_unicode(void) {
  // "h\u00E9llo-\u20AC" in UTF-8: made, found, and resolved back the same.
  char path[300], real[MAX_PATH], want[MAX_PATH];
  at(path, "h\xC3\xA9llo-\xE2\x82\xAC");
  CHECK(make_file(path, "x") == 0);
  CHECK(sppc_realpath(dir, want) == 0);
  strcat(want, "\\h\xC3\xA9llo-\xE2\x82\xAC");
  CHECK(sppc_realpath(path, real) == 0 && strcmp(real, want) == 0);
  int fd = -1;
  CHECK(sppc_open(path, SPPC_O_RDONLY, 0, &fd) == 0);
  sppc_close(fd);
  CHECK(sppc_unlink(path) == 0);

  // Bytes that are not UTF-8 are not a name.
  CHECK(sppc_open("bad\xFF", SPPC_O_RDONLY, 0, &fd) == SPPC_EILSEQ);
  CHECK(sppc_open("", SPPC_O_RDONLY, 0, &fd) == SPPC_ENOENT);
}

// ==================== METADATA ====================

static void check_modes(void) {
  char path[300], prog[300];
  at(path, "mode");
  at(prog, "tool.EXE");
  CHECK(make_file(path, "") == 0);
  CHECK(make_file(prog, "") == 0);

  // The owner's write bit is the read-only attribute, every other bit
  // follows from it.
  struct sppc_stat st;
  int ok = -1;
  CHECK(sppc_stat(path, &st) == 0 && (st.mode & 07777) == 0666);
  CHECK(sppc_access(path, SPPC_R_OK | SPPC_W_OK, &ok) == 0 && ok == 0);
  CHECK(sppc_chmod(path, 0444) == 0);
  CHECK(sppc_stat(path, &st) == 0 && (st.mode & 07777) == 0444);
  CHECK(sppc_access(path, SPPC_W_OK, &ok) == SPPC_EACCES && ok == -1);
  int fd = -1;
  CHECK(sppc_open(path, SPPC_O_WRONLY, 0, &fd) == SPPC_EACCES);
  CHECK(sppc_chmod(path, 0600) == 0);
  CHECK(sppc_stat(path, &st) == 0 && (st.mode & 07777) == 0666);
  CHECK(sppc_chmod(path, 01000000) == SPPC_EINVAL);

  // A program is one by its extension, whatever its case.
  CHECK(sppc_stat(prog, &st) == 0 && (st.mode & 0111) == 0111);
  CHECK(sppc_access(prog, SPPC_X_OK, &ok) == 0);
  CHECK(sppc_access(path, SPPC_X_OK, &ok) == SPPC_EACCES);
  CHECK(sppc_access(dir, SPPC_X_OK, &ok) == 0);

  // A file made without the owner's write bit is read-only, but the
  // descriptor that made it still writes, as POSIX's does.
  char made[300];
  at(made, "made-ro");
  ssize_t n = 0;
  CHECK(sppc_open(made, SPPC_O_RDWR | SPPC_O_CREAT | SPPC_O_EXCL, 0444, &fd) == 0);
  CHECK(sppc_write("w", 1, 1, fd, &n) == 0 && n == 1);
  sppc_close(fd);
  CHECK(sppc_stat(made, &st) == 0 && (st.mode & 07777) == 0444 && st.size == 1);

  // Ownership: every file is uid and gid 0's, and stays so.
  CHECK(st.uid == 0 && st.gid == 0);
  CHECK(sppc_chown(made, 0, 0) == 0 && sppc_chown(made, UINT32_MAX, 0) == 0);
  CHECK(sppc_chown(made, 1000, 0) == SPPC_EPERM && sppc_chown(made, 0, 5) == SPPC_EPERM);

  CHECK(sppc_unlink(made) == 0 && sppc_unlink(prog) == 0 && sppc_unlink(path) == 0);
}

static void check_kinds(void) {
  // A pipe has no position and no size; a directory cannot be read.
  int r = -1, w = -1;
  CHECK(sppc_pipe(&r, &w) == 0);
  struct sppc_stat st;
  int64_t pos = 0;
  char buf[4];
  ssize_t n = 0;
  CHECK(sppc_fstat(r, &st) == 0 && (st.mode & SPPC_S_IFMT) == SPPC_S_IFIFO);
  CHECK(sppc_lseek(r, 0, SPPC_SEEK_CUR, &pos) == SPPC_ESPIPE && pos == -1);
  CHECK(sppc_pread(r, buf, 1, 1, 0, &n) == SPPC_ESPIPE);
  CHECK(sppc_ftruncate(w, 0) == SPPC_EINVAL);
  CHECK(sppc_fsync(w) == SPPC_EINVAL);
  sppc_close(r);
  sppc_close(w);

  int d = -1;
  CHECK(sppc_open(dir, SPPC_O_RDONLY | SPPC_O_DIRECTORY, 0, &d) == 0);
  CHECK(sppc_fstat(d, &st) == 0 && (st.mode & SPPC_S_IFMT) == SPPC_S_IFDIR);
  CHECK(sppc_read(buf, 1, sizeof buf, d, &n) == SPPC_EISDIR);
  CHECK(sppc_fsync(d) == 0);
  sppc_close(d);
}

static void check_statvfs(void) {
  struct sppc_statvfs vfs;
  CHECK(sppc_statvfs(dir, &vfs) == 0);
  CHECK(vfs.block_size > 0 && vfs.blocks > 0 && vfs.blocks_free <= vfs.blocks && vfs.namemax >= 255);
  CHECK(sppc_statvfs("C:\\nonexistent\\sppc", &vfs) == SPPC_ENOENT);
}

// ==================== DESCRIPTORS ====================

static void check_open_modes(void) {
  char path[300];
  at(path, "modes");
  CHECK(make_file(path, "0123456789") == 0);

  // O_TRUNC truncates even a read-only open, as Linux's does.
  int fd = -1;
  CHECK(sppc_open(path, SPPC_O_RDONLY | SPPC_O_TRUNC, 0, &fd) == 0);
  struct sppc_stat st;
  CHECK(sppc_fstat(fd, &st) == 0 && st.size == 0);
  ssize_t n = 0;
  CHECK(sppc_write("x", 1, 1, fd, &n) == SPPC_EBADF);
  sppc_close(fd);

  // O_APPEND puts every write at the end, while another descriptor of the
  // same file moves the end along.
  int app = -1, other = -1;
  CHECK(sppc_open(path, SPPC_O_WRONLY | SPPC_O_APPEND, 0, &app) == 0);
  CHECK(sppc_open(path, SPPC_O_WRONLY, 0, &other) == 0);
  CHECK(sppc_write("abc", 1, 3, other, &n) == 0);
  CHECK(sppc_write("12", 1, 2, app, &n) == 0);
  CHECK(sppc_write("d", 1, 1, other, &n) == 0); // at its own position: 3, over the "1"
  CHECK(sppc_write("3", 1, 1, app, &n) == 0);
  sppc_close(other);
  sppc_close(app);
  char buf[16] = {0};
  CHECK(sppc_open(path, SPPC_O_RDONLY, 0, &fd) == 0);
  CHECK(sppc_read(buf, 1, sizeof buf, fd, &n) == 0 && n == 6 && memcmp(buf, "abcd23", 6) == 0);

  // readv gathers into each buffer in turn, short at the end.
  char one[2], two[8];
  sppc_iovec iov[2] = {{one, 2}, {two, 8}};
  int64_t pos = 0;
  CHECK(sppc_lseek(fd, 0, SPPC_SEEK_SET, &pos) == 0);
  CHECK(sppc_readv(fd, iov, 2, &n) == 0 && n == 6 && memcmp(one, "ab", 2) == 0 && memcmp(two, "cd23", 4) == 0);
  CHECK(sppc_readv(fd, iov, -1, &n) == SPPC_EINVAL);
  sppc_close(fd);

  // Flags O_PATH and O_TMPFILE mean nothing here; nor a mode beyond the
  // permission bits.
  CHECK(sppc_open(path, SPPC_O_PATH, 0, &fd) == SPPC_EINVAL && fd == -1);
  CHECK(sppc_open(path, SPPC_O_RDWR | SPPC_O_CREAT, 0100644, &fd) == SPPC_EINVAL);
  CHECK(sppc_unlink(path) == 0);
}

static void check_fcntl(void) {
  char path[300];
  at(path, "fcntl");
  CHECK(make_file(path, "data") == 0);
  int fd = -1, flags = -1;
  CHECK(sppc_open(path, SPPC_O_RDWR | SPPC_O_APPEND | SPPC_O_CLOEXEC, 0, &fd) == 0);

  CHECK(sppc_fcntl_get(fd, SPPC_F_GETFD, &flags) == 0 && flags == SPPC_FD_CLOEXEC);
  CHECK(sppc_fcntl_set(fd, SPPC_F_SETFD, 0) == 0);
  CHECK(sppc_fcntl_get(fd, SPPC_F_GETFD, &flags) == 0 && flags == 0);
  CHECK(sppc_fcntl_set(fd, SPPC_F_SETFD, 8) == SPPC_EINVAL);

  CHECK(sppc_fcntl_get(fd, SPPC_F_GETFL, &flags) == 0 && flags == (SPPC_O_RDWR | SPPC_O_APPEND));
  CHECK(sppc_fcntl_set(fd, SPPC_F_SETFL, SPPC_O_NONBLOCK) == 0);
  CHECK(sppc_fcntl_get(fd, SPPC_F_GETFL, &flags) == 0 && flags == (SPPC_O_RDWR | SPPC_O_NONBLOCK));
  CHECK(sppc_fcntl_get(fd, 99, &flags) == SPPC_EINVAL && flags == -1);
  CHECK(sppc_fcntl_get(-1, SPPC_F_GETFD, &flags) == SPPC_EBADF);

  // A dup shares the description, not the descriptor's own flag.
  int copy = -1;
  CHECK(sppc_fcntl_set(fd, SPPC_F_SETFD, SPPC_FD_CLOEXEC) == 0);
  CHECK(sppc_dup(fd, &copy) == 0);
  CHECK(sppc_fcntl_get(copy, SPPC_F_GETFD, &flags) == 0 && flags == 0);
  CHECK(sppc_fcntl_get(copy, SPPC_F_GETFL, &flags) == 0 && flags == (SPPC_O_RDWR | SPPC_O_NONBLOCK));
  sppc_close(copy);

  // Record locks: a write lock keeps a second open of the file out (and is
  // reported to it), a lock taken again replaces this descriptor's own, and
  // an unlock lets the other in.
  int second = -1;
  CHECK(sppc_open(path, SPPC_O_RDWR, 0, &second) == 0);
  sppc_flock lock = {.start = 0, .len = 0, .type = SPPC_F_WRLCK, .whence = SPPC_SEEK_SET};
  CHECK(sppc_fcntl_lock(fd, SPPC_F_SETLK, &lock) == 0);
  CHECK(sppc_fcntl_lock(fd, SPPC_F_SETLK, &lock) == 0);
  sppc_flock probe = {.start = 0, .len = 4, .pid = 99, .type = SPPC_F_WRLCK, .whence = SPPC_SEEK_SET};
  CHECK(sppc_fcntl_lock(second, SPPC_F_GETLK, &probe) == 0 && probe.type == SPPC_F_WRLCK && probe.pid == 0);
  sppc_flock theirs = {.start = 0, .len = 4, .type = SPPC_F_RDLCK, .whence = SPPC_SEEK_SET};
  CHECK(sppc_fcntl_lock(second, SPPC_F_SETLK, &theirs) == SPPC_EAGAIN);
  lock.type = SPPC_F_UNLCK;
  CHECK(sppc_fcntl_lock(fd, SPPC_F_SETLK, &lock) == 0);
  probe.type = SPPC_F_WRLCK;
  CHECK(sppc_fcntl_lock(second, SPPC_F_GETLK, &probe) == 0 && probe.type == SPPC_F_UNLCK);
  CHECK(sppc_fcntl_lock(second, SPPC_F_SETLKW, &theirs) == 0);
  theirs.type = SPPC_F_UNLCK;
  CHECK(sppc_fcntl_lock(second, SPPC_F_SETLK, &theirs) == 0);

  // A write lock asks for a descriptor open for writing; nonsense is refused.
  int reader = -1;
  CHECK(sppc_open(path, SPPC_O_RDONLY, 0, &reader) == 0);
  sppc_flock wr = {.start = 0, .len = 1, .type = SPPC_F_WRLCK, .whence = SPPC_SEEK_SET};
  CHECK(sppc_fcntl_lock(reader, SPPC_F_SETLK, &wr) == SPPC_EBADF);
  wr.whence = 7;
  CHECK(sppc_fcntl_lock(fd, SPPC_F_SETLK, &wr) == SPPC_EINVAL);
  sppc_close(reader);
  sppc_close(second);
  sppc_close(fd);
  CHECK(sppc_unlink(path) == 0);
}

static void check_temp_and_random(void) {
  char made[300];
  snprintf(made, sizeof made, "%s\\f_XXXXXX", dir);
  int fd = -1;
  CHECK(sppc_mktemp(made, &fd) == 0 && fd >= 0 && strstr(made, "XXXXXX") == NULL);
  int flags = -1;
  CHECK(sppc_fcntl_get(fd, SPPC_F_GETFD, &flags) == 0 && flags == SPPC_FD_CLOEXEC);
  sppc_close(fd);
  CHECK(sppc_unlink(made) == 0);
  char bad[] = "no-template";
  CHECK(sppc_mktemp(bad, &fd) == SPPC_EINVAL && fd == -1);

  // Random bytes: not all the same, and two draws differ.
  unsigned char a[64] = {0}, b[64] = {0};
  CHECK(sppc_getrandom(sizeof a, (char*)a) == 0 && sppc_getrandom(sizeof b, (char*)b) == 0);
  CHECK(memcmp(a, b, sizeof a) != 0);
  CHECK(sppc_getrandom(0, (char*)a) == 0);
}

static void check_copies(void) {
  char src[300], dst[300];
  at(src, "src");
  at(dst, "dst");
  CHECK(make_file(src, "0123456789") == 0);
  int in = -1, out = -1;
  ssize_t n = 0;
  CHECK(sppc_open(src, SPPC_O_RDONLY, 0, &in) == 0);
  CHECK(sppc_open(dst, SPPC_O_RDWR | SPPC_O_CREAT | SPPC_O_TRUNC, 0644, &out) == 0);

  // sendfile without an offset: from the source's own position, which moves.
  int64_t pos = 0;
  CHECK(sppc_lseek(in, 3, SPPC_SEEK_SET, &pos) == 0);
  CHECK(sppc_sendfile(in, out, NULL, 4, &n) == 0 && n == 4);
  CHECK(sppc_lseek(in, 0, SPPC_SEEK_CUR, &pos) == 0 && pos == 7);
  char buf[8] = {0};
  CHECK(sppc_pread(out, buf, 1, 8, 0, &n) == 0 && n == 4 && memcmp(buf, "3456", 4) == 0);

  // Into a pipe, and out of one.
  int r = -1, w = -1;
  CHECK(sppc_pipe(&r, &w) == 0);
  int64_t off = 0;
  CHECK(sppc_sendfile(in, w, &off, 3, &n) == 0 && n == 3 && off == 3);
  CHECK(sppc_read(buf, 1, 3, r, &n) == 0 && n == 3 && memcmp(buf, "012", 3) == 0);
  CHECK(sppc_sendfile(in, -1, &off, 3, &n) == SPPC_EBADF && n == -1);
  sppc_close(r);
  sppc_close(w);
  sppc_close(in);
  sppc_close(out);
  CHECK(sppc_unlink(src) == 0 && sppc_unlink(dst) == 0);
}

int main(void) {
  if (sppc_init() != 0) { return 1; }
  check_temp_dir(dir, sizeof dir, "files");
  check_names_while_open();
  check_unicode();
  check_modes();
  check_kinds();
  check_statvfs();
  check_open_modes();
  check_fcntl();
  check_temp_and_random();
  check_copies();
  CHECK(sppc_rmdir(dir) == 0); // empty: nothing above left anything behind
  sppc_cleanup();
  return check_done();
}

#endif
