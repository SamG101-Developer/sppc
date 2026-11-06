#include "check.h"

// The boot code on Windows (platform/windows.h): what "sppc_init" sets up
// there and "sppc_cleanup" takes down again. Winsock is usable only in
// between, critical-error dialogs are off, the standard streams' locks
// work, the locale is UTF-8 where the C library has one, and the cleanup
// runs a task nobody awaited.

#if !SPPC_PLATFORM_WINDOWS

int main(void) {
  printf("skipped: Windows only\n");
  return CHECK_SKIP_STATUS;
}

#else

#include <locale.h>
#include <string.h>

static bool winsock_up(void) {
  const SOCKET s = socket(AF_INET, SOCK_STREAM, 0);
  if (s == INVALID_SOCKET) { return false; }
  closesocket(s);
  return true;
}

typedef struct { sppc_closure_env head; int *ran; } flag_env;

static void set_flag(void *e) {
  *((flag_env*)e)->ran = 1;
}

int main(void) {
  CHECK(!winsock_up());
  CHECK(WSAGetLastError() == WSANOTINITIALISED);

  CHECK(sppc_init() == 0);
  CHECK(winsock_up());
  CHECK((GetErrorMode() & SEM_FAILCRITICALERRORS) != 0);
  CHECK(pthread_mutex_lock(&_stdout_mutex) == 0 && pthread_mutex_unlock(&_stdout_mutex) == 0);
  CHECK(pthread_mutex_trylock(&_stderr_mutex) == 0 && pthread_mutex_unlock(&_stderr_mutex) == 0);
  char const *const locale = setlocale(LC_ALL, NULL);
  printf("locale: %s\n", locale ? locale : "(none)");
#if SPPC_CC_MSVC || defined(_UCRT)
  CHECK(locale != NULL && strstr(locale, "UTF-8") != NULL); // the ucrt has UTF-8 locales; msvcrt does not
#endif

  // A task spawned and never awaited still runs, at the cleanup.
  int ran = 0;
  flag_env *const env = (flag_env*)sppc_malloc(sizeof *env);
  env->head = (sppc_closure_env){1, NULL};
  env->ran = &ran;
  int handle = 0;
  CHECK(sppc_async(&handle, (sppc_closure){set_flag, env}) == 0);
  CHECK(ran == 0);

  CHECK(sppc_cleanup() == 0);
  CHECK(ran == 1);
  CHECK(!winsock_up()); // its start was the only one, so it is stopped again

  // A host's own start of Winsock outlives sppc's.
  WSADATA wsa;
  CHECK(WSAStartup(MAKEWORD(2, 2), &wsa) == 0);
  CHECK(sppc_init() == 0 && sppc_cleanup() == 0);
  CHECK(winsock_up());
  WSACleanup();
  return check_done();
}

#endif
