#include "check.h"

// Error numbers on Windows (sppc/errors.h, platform/windows.h), from each of its three
// sources: the C library's errno, through the same table as every other
// platform (identity to 34, its own numbers past it); Win32's
// "GetLastError", many to one; and Winsock's "WSAGetLastError". Where a
// real call can produce the error it is made to, so the code under test is
// the one Windows actually reports.

#if !SPPC_PLATFORM_WINDOWS

int main(void) {
  printf("skipped: Windows only\n");
  return CHECK_SKIP_STATUS;
}

#else

#include <errno.h>

// ==================== C LIBRARY ====================

static void check_crt(void) {
  CHECK(_sppc_errno(0) == 0);
  CHECK(_sppc_errno(EPERM) == SPPC_EPERM);
  CHECK(_sppc_errno(ERANGE) == SPPC_ERANGE);

  // Its own numbers past 34, each to its namesake: the ones the pthreads
  // shim returns among them.
  CHECK(EDEADLK != SPPC_EDEADLK && _sppc_errno(EDEADLK) == SPPC_EDEADLK);
  CHECK(ETIMEDOUT != SPPC_ETIMEDOUT && _sppc_errno(ETIMEDOUT) == SPPC_ETIMEDOUT);
  CHECK(_sppc_errno(ENAMETOOLONG) == SPPC_ENAMETOOLONG);
  CHECK(_sppc_errno(ENOSYS) == SPPC_ENOSYS);
  CHECK(_sppc_errno(ENOTEMPTY) == SPPC_ENOTEMPTY);
  CHECK(_sppc_errno(EILSEQ) == SPPC_EILSEQ);
  CHECK(_sppc_errno(EADDRINUSE) == SPPC_EADDRINUSE);
  CHECK(_sppc_errno(ECONNREFUSED) == SPPC_ECONNREFUSED);
  CHECK(_sppc_errno(EOWNERDEAD) == SPPC_EOWNERDEAD);
  CHECK(_sppc_errno(ENOTRECOVERABLE) == SPPC_ENOTRECOVERABLE);
  CHECK(_sppc_errno(ETXTBSY) == SPPC_ETXTBSY);

  // Its second numbers for one meaning: EWOULDBLOCK is EAGAIN on Linux,
  // ENOTSUP is EOPNOTSUPP.
  CHECK(EWOULDBLOCK != EAGAIN && _sppc_errno(EWOULDBLOCK) == SPPC_EAGAIN);
  CHECK(ENOTSUP != EOPNOTSUPP && _sppc_errno(ENOTSUP) == SPPC_EOPNOTSUPP);
  CHECK(_sppc_errno(EOPNOTSUPP) == SPPC_EOPNOTSUPP);

  // What has no counterpart, and what is no error number at all.
  CHECK(_sppc_errno(STRUNCATE) == SPPC_EIO);
#ifdef EOTHER // msvc's C library only
  CHECK(_sppc_errno(EOTHER) == SPPC_EIO);
#endif
  CHECK(_sppc_errno(-1) == SPPC_EIO);
  CHECK(_sppc_errno(100000) == SPPC_EIO);

  // Every number the C library has comes out a real sppc one.
  for (int e = 1; e <= 200; ++e) {
    const int out = _sppc_errno(e);
    CHECK(out >= SPPC_EPERM && out <= SPPC_EHWPOISON);
  }

  // And from a real call.
  errno = 0;
  CHECK(strtol("99999999999999999999", NULL, 10) == LONG_MAX && _sppc_errno(errno) == SPPC_ERANGE);
}

// ==================== WIN32 ====================

static void check_win32(void) {
  CHECK(_sppc_errno_win32(ERROR_SUCCESS) == 0);

  char dir[MAX_PATH], sub[MAX_PATH], file[MAX_PATH];
  GetTempPathA(sizeof dir, dir);
  snprintf(sub, sizeof sub, "%ssppc_errno_%lu", dir, GetCurrentProcessId());
  snprintf(file, sizeof file, "%s\\f", sub);

  // A path that is not there, at its end or on the way.
  CHECK(GetFileAttributesA("C:\\sppc_no_such_file_here") == INVALID_FILE_ATTRIBUTES);
  CHECK(_sppc_errno_win32(GetLastError()) == SPPC_ENOENT);
  CHECK(GetFileAttributesA("C:\\sppc_no_such_dir_here\\f") == INVALID_FILE_ATTRIBUTES);
  CHECK(_sppc_errno_win32(GetLastError()) == SPPC_ENOENT);

  // A directory made twice; one not empty; a file where one is wanted.
  CHECK(CreateDirectoryA(sub, NULL));
  CHECK(!CreateDirectoryA(sub, NULL) && _sppc_errno_win32(GetLastError()) == SPPC_EEXIST);
  HANDLE h = CreateFileA(file, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
  CHECK(h != INVALID_HANDLE_VALUE);
  CHECK(!RemoveDirectoryA(sub) && _sppc_errno_win32(GetLastError()) == SPPC_ENOTEMPTY);
  CHECK(!RemoveDirectoryA(file) && _sppc_errno_win32(GetLastError()) == SPPC_ENOTDIR);

  // Opened again while held without sharing: in use, not denied.
  const HANDLE again = CreateFileA(file, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
  CHECK(again == INVALID_HANDLE_VALUE && _sppc_errno_win32(GetLastError()) == SPPC_EBUSY);
  CloseHandle(h);
  CHECK(CreateFileA(file, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL) == INVALID_HANDLE_VALUE);
  CHECK(_sppc_errno_win32(GetLastError()) == SPPC_EEXIST);
  DeleteFileA(file);
  RemoveDirectoryA(sub);

  // A handle that is not one; memory there is not.
  DWORD n = 0;
  CHECK(!ReadFile(INVALID_HANDLE_VALUE, dir, 1, &n, NULL) && _sppc_errno_win32(GetLastError()) == SPPC_EBADF);
  CHECK(VirtualAlloc(NULL, (SIZE_T)1 << 62, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE) == NULL);
  CHECK(_sppc_errno_win32(GetLastError()) == SPPC_ENOMEM || _sppc_errno_win32(GetLastError()) == SPPC_EINVAL);

  // A pipe whose reader has gone.
  HANDLE r, w;
  CHECK(CreatePipe(&r, &w, NULL, 0));
  CloseHandle(r);
  CHECK(!WriteFile(w, "x", 1, &n, NULL) && _sppc_errno_win32(GetLastError()) == SPPC_EPIPE);
  CloseHandle(w);

  // The rest of the table, by its constants.
  CHECK(_sppc_errno_win32(ERROR_ACCESS_DENIED) == SPPC_EACCES);
  CHECK(_sppc_errno_win32(ERROR_WRITE_PROTECT) == SPPC_EROFS);
  CHECK(_sppc_errno_win32(ERROR_DISK_FULL) == SPPC_ENOSPC);
  CHECK(_sppc_errno_win32(ERROR_FILENAME_EXCED_RANGE) == SPPC_ENAMETOOLONG);
  CHECK(_sppc_errno_win32(ERROR_NOT_SAME_DEVICE) == SPPC_EXDEV);
  CHECK(_sppc_errno_win32(ERROR_PRIVILEGE_NOT_HELD) == SPPC_EPERM);
  CHECK(_sppc_errno_win32(ERROR_INVALID_PARAMETER) == SPPC_EINVAL);
  CHECK(_sppc_errno_win32(ERROR_CALL_NOT_IMPLEMENTED) == SPPC_ENOSYS);
  CHECK(_sppc_errno_win32(WAIT_TIMEOUT) == SPPC_ETIMEDOUT);
  CHECK(_sppc_errno_win32(ERROR_CRC) == SPPC_EIO); // no counterpart
  CHECK(_sppc_errno_win32(0xFFFFFFFFu) == SPPC_EIO);
}

// ==================== WINSOCK ====================

static void check_wsa(void) {
  CHECK(_sppc_errno_wsa(0) == 0);

  // Before Winsock is started: no counterpart.
  CHECK(socket(AF_INET, SOCK_STREAM, 0) == INVALID_SOCKET);
  CHECK(_sppc_errno_wsa(WSAGetLastError()) == SPPC_EIO);

  WSADATA wsa;
  CHECK(WSAStartup(MAKEWORD(2, 2), &wsa) == 0);

  // An address already taken.
  const SOCKET a = socket(AF_INET, SOCK_STREAM, 0), b = socket(AF_INET, SOCK_STREAM, 0);
  struct sockaddr_in addr = {0};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  CHECK(bind(a, (struct sockaddr*)&addr, sizeof addr) == 0);
  int len = sizeof addr;
  getsockname(a, (struct sockaddr*)&addr, &len);
  CHECK(listen(a, 1) == 0);
  CHECK(bind(b, (struct sockaddr*)&addr, sizeof addr) == SOCKET_ERROR);
  CHECK(_sppc_errno_wsa(WSAGetLastError()) == SPPC_EADDRINUSE);

  // Nothing to accept on a non-blocking socket; nothing to send unconnected.
  u_long on = 1;
  ioctlsocket(a, FIONBIO, &on);
  CHECK(accept(a, NULL, NULL) == INVALID_SOCKET && _sppc_errno_wsa(WSAGetLastError()) == SPPC_EAGAIN);
  CHECK(send(b, "x", 1, 0) == SOCKET_ERROR && _sppc_errno_wsa(WSAGetLastError()) == SPPC_ENOTCONN);
  CHECK(recv((SOCKET)12345, (char*)&on, 1, 0) == SOCKET_ERROR && _sppc_errno_wsa(WSAGetLastError()) == SPPC_ENOTSOCK);
  closesocket(a);
  closesocket(b);
  WSACleanup();

  CHECK(_sppc_errno_wsa(WSAECONNREFUSED) == SPPC_ECONNREFUSED);
  CHECK(_sppc_errno_wsa(WSAECONNRESET) == SPPC_ECONNRESET);
  CHECK(_sppc_errno_wsa(WSAETIMEDOUT) == SPPC_ETIMEDOUT);
  CHECK(_sppc_errno_wsa(WSAESHUTDOWN) == SPPC_EPIPE);
  CHECK(_sppc_errno_wsa(WSAEINPROGRESS) == SPPC_EINPROGRESS);
  CHECK(_sppc_errno_wsa(WSAEAFNOSUPPORT) == SPPC_EAFNOSUPPORT);
  CHECK(_sppc_errno_wsa(WSAHOST_NOT_FOUND) == SPPC_EIO); // a resolver's error, not errno's
}

int main(void) {
  check_crt();
  check_win32();
  check_wsa();
  return check_done();
}

#endif
