// Runs a program with io_uring taken away, as an old kernel or a
// sandbox would: a seccomp filter fails "io_uring_setup" with
// ENOSYS, so the runtime starts on its no-ring fallback. The
// filter is kept across exec and needs no privilege.
//
//   no_uring <program> [args...]

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <sppc/config.h>
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <unistd.h>
#include <linux/audit.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <sys/prctl.h>
#include <sys/syscall.h>

#if SPPC_ARCH_X86_64
#define NO_URING_ARCH AUDIT_ARCH_X86_64
#elif SPPC_ARCH_ARM64
#define NO_URING_ARCH AUDIT_ARCH_AARCH64
#elif SPPC_ARCH_S390X
#define NO_URING_ARCH AUDIT_ARCH_S390X
#elif SPPC_ARCH_X86
#define NO_URING_ARCH AUDIT_ARCH_I386
#elif SPPC_ARCH_PPC64 && defined(__LITTLE_ENDIAN__)
#define NO_URING_ARCH AUDIT_ARCH_PPC64LE
#elif SPPC_ARCH_PPC64
#define NO_URING_ARCH AUDIT_ARCH_PPC64
#elif SPPC_ARCH_PPC
#define NO_URING_ARCH AUDIT_ARCH_PPC
#elif SPPC_ARCH_RISCV64
#define NO_URING_ARCH AUDIT_ARCH_RISCV64
#elif SPPC_ARCH_ARM
#define NO_URING_ARCH AUDIT_ARCH_ARM
#elif SPPC_ARCH_MIPS && defined(__MIPSEL__)
#define NO_URING_ARCH AUDIT_ARCH_MIPSEL
#elif SPPC_ARCH_MIPS
#define NO_URING_ARCH AUDIT_ARCH_MIPS
#elif SPPC_ARCH_MIPS64 && defined(__MIPSEL__)
#define NO_URING_ARCH AUDIT_ARCH_MIPSEL64
#elif SPPC_ARCH_MIPS64
#define NO_URING_ARCH AUDIT_ARCH_MIPS64
#elif SPPC_ARCH_LOONGARCH64
#define NO_URING_ARCH AUDIT_ARCH_LOONGARCH64
#elif SPPC_ARCH_M68K
#define NO_URING_ARCH AUDIT_ARCH_M68K
#elif SPPC_ARCH_SPARC64
#define NO_URING_ARCH AUDIT_ARCH_SPARC64
#else
#error "no_uring: unknown architecture"
#endif

int main(const int argc, char **argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: no_uring <program> [args...]\n");
    return 2;
  }

  // Another architecture's numbers mean other calls, so only this
  // one's "io_uring_setup" is failed.
  struct sock_filter code[] = {
    BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, arch)),
    BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, NO_URING_ARCH, 0, 3),
    BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, nr)),
    BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_io_uring_setup, 0, 1),
    BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | ENOSYS),
    BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
  };
  struct sock_fprog prog = {sizeof code / sizeof code[0], code};
  if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0 ||
      prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog) != 0) {
    perror("no_uring: seccomp");
    return 2;
  }

  // A filter that did not take would leave the ring in place, and
  // the run would pass without testing the fallback.
  if (syscall(__NR_io_uring_setup, 1, NULL) != -1 || errno != ENOSYS) {
    fprintf(stderr, "no_uring: io_uring_setup is still reachable\n");
    return 2;
  }

  execv(argv[1], argv + 1);
  perror("no_uring: exec");
  return 2;
}
