#pragma once

/// The platform's error numbers as sppc's. sppc's are Linux's generic ones,
/// which most Linux architectures use, so there "_sppc_errno" returns the
/// number it is given; the preprocessor checks every one of them, and any
/// that differs picks the table instead. powerpc differs in EDEADLOCK, mips
/// and sparc in nearly every number, and Darwin past 34 (EAGAIN is 35, not
/// 11).

#include <sppc/config.h>
#include <sppc/macros.h>
#include <sppc/types.h>
#include <errno.h>
#include <stdint.h>

#if !(SPPC_KERNEL_LINUX || SPPC_KERNEL_DARWIN)
#error "sppc: no errno translation for this platform yet"
#endif

#if EPERM == SPPC_EPERM \
  && ENOENT == SPPC_ENOENT \
  && ESRCH == SPPC_ESRCH \
  && EINTR == SPPC_EINTR \
  && EIO == SPPC_EIO \
  && ENXIO == SPPC_ENXIO \
  && E2BIG == SPPC_E2BIG \
  && ENOEXEC == SPPC_ENOEXEC \
  && EBADF == SPPC_EBADF \
  && ECHILD == SPPC_ECHILD \
  && EAGAIN == SPPC_EAGAIN \
  && ENOMEM == SPPC_ENOMEM \
  && EACCES == SPPC_EACCES \
  && EFAULT == SPPC_EFAULT \
  && ENOTBLK == SPPC_ENOTBLK \
  && EBUSY == SPPC_EBUSY \
  && EEXIST == SPPC_EEXIST \
  && EXDEV == SPPC_EXDEV \
  && ENODEV == SPPC_ENODEV \
  && ENOTDIR == SPPC_ENOTDIR \
  && EISDIR == SPPC_EISDIR \
  && EINVAL == SPPC_EINVAL \
  && ENFILE == SPPC_ENFILE \
  && EMFILE == SPPC_EMFILE \
  && ENOTTY == SPPC_ENOTTY \
  && ETXTBSY == SPPC_ETXTBSY \
  && EFBIG == SPPC_EFBIG \
  && ENOSPC == SPPC_ENOSPC \
  && ESPIPE == SPPC_ESPIPE \
  && EROFS == SPPC_EROFS \
  && EMLINK == SPPC_EMLINK \
  && EPIPE == SPPC_EPIPE \
  && EDOM == SPPC_EDOM \
  && ERANGE == SPPC_ERANGE \
  && EDEADLK == SPPC_EDEADLK \
  && ENAMETOOLONG == SPPC_ENAMETOOLONG \
  && ENOLCK == SPPC_ENOLCK \
  && ENOSYS == SPPC_ENOSYS \
  && ENOTEMPTY == SPPC_ENOTEMPTY \
  && ELOOP == SPPC_ELOOP \
  && EWOULDBLOCK == SPPC_EWOULDBLOCK \
  && ENOMSG == SPPC_ENOMSG \
  && EIDRM == SPPC_EIDRM \
  && ECHRNG == SPPC_ECHRNG \
  && EL2NSYNC == SPPC_EL2NSYNC \
  && EL3HLT == SPPC_EL3HLT \
  && EL3RST == SPPC_EL3RST \
  && ELNRNG == SPPC_ELNRNG \
  && EUNATCH == SPPC_EUNATCH \
  && ENOCSI == SPPC_ENOCSI \
  && EL2HLT == SPPC_EL2HLT \
  && EBADE == SPPC_EBADE \
  && EBADR == SPPC_EBADR \
  && EXFULL == SPPC_EXFULL \
  && ENOANO == SPPC_ENOANO \
  && EBADRQC == SPPC_EBADRQC \
  && EBADSLT == SPPC_EBADSLT \
  && EDEADLOCK == SPPC_EDEADLOCK \
  && EBFONT == SPPC_EBFONT \
  && ENOSTR == SPPC_ENOSTR \
  && ENODATA == SPPC_ENODATA \
  && ETIME == SPPC_ETIME \
  && ENOSR == SPPC_ENOSR \
  && ENONET == SPPC_ENONET \
  && ENOPKG == SPPC_ENOPKG \
  && EREMOTE == SPPC_EREMOTE \
  && ENOLINK == SPPC_ENOLINK \
  && EADV == SPPC_EADV \
  && ESRMNT == SPPC_ESRMNT \
  && ECOMM == SPPC_ECOMM \
  && EPROTO == SPPC_EPROTO \
  && EMULTIHOP == SPPC_EMULTIHOP \
  && EDOTDOT == SPPC_EDOTDOT \
  && EBADMSG == SPPC_EBADMSG \
  && EOVERFLOW == SPPC_EOVERFLOW \
  && ENOTUNIQ == SPPC_ENOTUNIQ \
  && EBADFD == SPPC_EBADFD \
  && EREMCHG == SPPC_EREMCHG \
  && ELIBACC == SPPC_ELIBACC \
  && ELIBBAD == SPPC_ELIBBAD \
  && ELIBSCN == SPPC_ELIBSCN \
  && ELIBMAX == SPPC_ELIBMAX \
  && ELIBEXEC == SPPC_ELIBEXEC \
  && EILSEQ == SPPC_EILSEQ \
  && ERESTART == SPPC_ERESTART \
  && ESTRPIPE == SPPC_ESTRPIPE \
  && EUSERS == SPPC_EUSERS \
  && ENOTSOCK == SPPC_ENOTSOCK \
  && EDESTADDRREQ == SPPC_EDESTADDRREQ \
  && EMSGSIZE == SPPC_EMSGSIZE \
  && EPROTOTYPE == SPPC_EPROTOTYPE \
  && ENOPROTOOPT == SPPC_ENOPROTOOPT \
  && EPROTONOSUPPORT == SPPC_EPROTONOSUPPORT \
  && ESOCKTNOSUPPORT == SPPC_ESOCKTNOSUPPORT \
  && EOPNOTSUPP == SPPC_EOPNOTSUPP \
  && EPFNOSUPPORT == SPPC_EPFNOSUPPORT \
  && EAFNOSUPPORT == SPPC_EAFNOSUPPORT \
  && EADDRINUSE == SPPC_EADDRINUSE \
  && EADDRNOTAVAIL == SPPC_EADDRNOTAVAIL \
  && ENETDOWN == SPPC_ENETDOWN \
  && ENETUNREACH == SPPC_ENETUNREACH \
  && ENETRESET == SPPC_ENETRESET \
  && ECONNABORTED == SPPC_ECONNABORTED \
  && ECONNRESET == SPPC_ECONNRESET \
  && ENOBUFS == SPPC_ENOBUFS \
  && EISCONN == SPPC_EISCONN \
  && ENOTCONN == SPPC_ENOTCONN \
  && ESHUTDOWN == SPPC_ESHUTDOWN \
  && ETOOMANYREFS == SPPC_ETOOMANYREFS \
  && ETIMEDOUT == SPPC_ETIMEDOUT \
  && ECONNREFUSED == SPPC_ECONNREFUSED \
  && EHOSTDOWN == SPPC_EHOSTDOWN \
  && EHOSTUNREACH == SPPC_EHOSTUNREACH \
  && EALREADY == SPPC_EALREADY \
  && EINPROGRESS == SPPC_EINPROGRESS \
  && ESTALE == SPPC_ESTALE \
  && EUCLEAN == SPPC_EUCLEAN \
  && ENOTNAM == SPPC_ENOTNAM \
  && ENAVAIL == SPPC_ENAVAIL \
  && EISNAM == SPPC_EISNAM \
  && EREMOTEIO == SPPC_EREMOTEIO \
  && EDQUOT == SPPC_EDQUOT \
  && ENOMEDIUM == SPPC_ENOMEDIUM \
  && EMEDIUMTYPE == SPPC_EMEDIUMTYPE \
  && ECANCELED == SPPC_ECANCELED \
  && ENOKEY == SPPC_ENOKEY \
  && EKEYEXPIRED == SPPC_EKEYEXPIRED \
  && EKEYREVOKED == SPPC_EKEYREVOKED \
  && EKEYREJECTED == SPPC_EKEYREJECTED \
  && EOWNERDEAD == SPPC_EOWNERDEAD \
  && ENOTRECOVERABLE == SPPC_ENOTRECOVERABLE \
  && ERFKILL == SPPC_ERFKILL \
  && EHWPOISON == SPPC_EHWPOISON \
  && (!defined(ENOTSUP) || ENOTSUP == SPPC_EOPNOTSUPP)
  #define _SPPC_ERRNO_IDENTITY 1
#else
  #define _SPPC_ERRNO_IDENTITY 0
#endif

/// The platform's error number as sppc's.
_gnu_inline
int _sppc_errno(const int err) {
#if _SPPC_ERRNO_IDENTITY
  return err;
#else
  // Indexed by the platform's number, so sized by the largest of them. Each
  // name a platform lacks is left out, and an alias only has an entry where
  // it is a number of its own. Anything without an sppc counterpart (the Sun
  // RPC errors, EFTYPE, EAUTH, EPWROFF, ...), and anything past the table, is
  // SPPC_EIO.
  static const uint8_t table[] = {
#ifdef EPERM
    [EPERM]            = SPPC_EPERM,
#endif
#ifdef ENOENT
    [ENOENT]           = SPPC_ENOENT,
#endif
#ifdef ESRCH
    [ESRCH]            = SPPC_ESRCH,
#endif
#ifdef EINTR
    [EINTR]            = SPPC_EINTR,
#endif
#ifdef EIO
    [EIO]              = SPPC_EIO,
#endif
#ifdef ENXIO
    [ENXIO]            = SPPC_ENXIO,
#endif
#ifdef E2BIG
    [E2BIG]            = SPPC_E2BIG,
#endif
#ifdef ENOEXEC
    [ENOEXEC]          = SPPC_ENOEXEC,
#endif
#ifdef EBADF
    [EBADF]            = SPPC_EBADF,
#endif
#ifdef ECHILD
    [ECHILD]           = SPPC_ECHILD,
#endif
#ifdef EAGAIN
    [EAGAIN]           = SPPC_EAGAIN,
#endif
#ifdef ENOMEM
    [ENOMEM]           = SPPC_ENOMEM,
#endif
#ifdef EACCES
    [EACCES]           = SPPC_EACCES,
#endif
#ifdef EFAULT
    [EFAULT]           = SPPC_EFAULT,
#endif
#ifdef ENOTBLK
    [ENOTBLK]          = SPPC_ENOTBLK,
#endif
#ifdef EBUSY
    [EBUSY]            = SPPC_EBUSY,
#endif
#ifdef EEXIST
    [EEXIST]           = SPPC_EEXIST,
#endif
#ifdef EXDEV
    [EXDEV]            = SPPC_EXDEV,
#endif
#ifdef ENODEV
    [ENODEV]           = SPPC_ENODEV,
#endif
#ifdef ENOTDIR
    [ENOTDIR]          = SPPC_ENOTDIR,
#endif
#ifdef EISDIR
    [EISDIR]           = SPPC_EISDIR,
#endif
#ifdef EINVAL
    [EINVAL]           = SPPC_EINVAL,
#endif
#ifdef ENFILE
    [ENFILE]           = SPPC_ENFILE,
#endif
#ifdef EMFILE
    [EMFILE]           = SPPC_EMFILE,
#endif
#ifdef ENOTTY
    [ENOTTY]           = SPPC_ENOTTY,
#endif
#ifdef ETXTBSY
    [ETXTBSY]          = SPPC_ETXTBSY,
#endif
#ifdef EFBIG
    [EFBIG]            = SPPC_EFBIG,
#endif
#ifdef ENOSPC
    [ENOSPC]           = SPPC_ENOSPC,
#endif
#ifdef ESPIPE
    [ESPIPE]           = SPPC_ESPIPE,
#endif
#ifdef EROFS
    [EROFS]            = SPPC_EROFS,
#endif
#ifdef EMLINK
    [EMLINK]           = SPPC_EMLINK,
#endif
#ifdef EPIPE
    [EPIPE]            = SPPC_EPIPE,
#endif
#ifdef EDOM
    [EDOM]             = SPPC_EDOM,
#endif
#ifdef ERANGE
    [ERANGE]           = SPPC_ERANGE,
#endif
#ifdef EDEADLK
    [EDEADLK]          = SPPC_EDEADLK,
#endif
#ifdef ENAMETOOLONG
    [ENAMETOOLONG]     = SPPC_ENAMETOOLONG,
#endif
#ifdef ENOLCK
    [ENOLCK]           = SPPC_ENOLCK,
#endif
#ifdef ENOSYS
    [ENOSYS]           = SPPC_ENOSYS,
#endif
#ifdef ENOTEMPTY
    [ENOTEMPTY]        = SPPC_ENOTEMPTY,
#endif
#ifdef ELOOP
    [ELOOP]            = SPPC_ELOOP,
#endif
#if defined(EWOULDBLOCK) && EWOULDBLOCK != EAGAIN
    [EWOULDBLOCK]      = SPPC_EAGAIN,
#endif
#ifdef ENOMSG
    [ENOMSG]           = SPPC_ENOMSG,
#endif
#ifdef EIDRM
    [EIDRM]            = SPPC_EIDRM,
#endif
#ifdef ECHRNG
    [ECHRNG]           = SPPC_ECHRNG,
#endif
#ifdef EL2NSYNC
    [EL2NSYNC]         = SPPC_EL2NSYNC,
#endif
#ifdef EL3HLT
    [EL3HLT]           = SPPC_EL3HLT,
#endif
#ifdef EL3RST
    [EL3RST]           = SPPC_EL3RST,
#endif
#ifdef ELNRNG
    [ELNRNG]           = SPPC_ELNRNG,
#endif
#ifdef EUNATCH
    [EUNATCH]          = SPPC_EUNATCH,
#endif
#ifdef ENOCSI
    [ENOCSI]           = SPPC_ENOCSI,
#endif
#ifdef EL2HLT
    [EL2HLT]           = SPPC_EL2HLT,
#endif
#ifdef EBADE
    [EBADE]            = SPPC_EBADE,
#endif
#ifdef EBADR
    [EBADR]            = SPPC_EBADR,
#endif
#ifdef EXFULL
    [EXFULL]           = SPPC_EXFULL,
#endif
#ifdef ENOANO
    [ENOANO]           = SPPC_ENOANO,
#endif
#ifdef EBADRQC
    [EBADRQC]          = SPPC_EBADRQC,
#endif
#ifdef EBADSLT
    [EBADSLT]          = SPPC_EBADSLT,
#endif
#if defined(EDEADLOCK) && EDEADLOCK != EDEADLK
    [EDEADLOCK]        = SPPC_EDEADLK,
#endif
#ifdef EBFONT
    [EBFONT]           = SPPC_EBFONT,
#endif
#ifdef ENOSTR
    [ENOSTR]           = SPPC_ENOSTR,
#endif
#ifdef ENODATA
    [ENODATA]          = SPPC_ENODATA,
#endif
#ifdef ETIME
    [ETIME]            = SPPC_ETIME,
#endif
#ifdef ENOSR
    [ENOSR]            = SPPC_ENOSR,
#endif
#ifdef ENONET
    [ENONET]           = SPPC_ENONET,
#endif
#ifdef ENOPKG
    [ENOPKG]           = SPPC_ENOPKG,
#endif
#ifdef EREMOTE
    [EREMOTE]          = SPPC_EREMOTE,
#endif
#ifdef ENOLINK
    [ENOLINK]          = SPPC_ENOLINK,
#endif
#ifdef EADV
    [EADV]             = SPPC_EADV,
#endif
#ifdef ESRMNT
    [ESRMNT]           = SPPC_ESRMNT,
#endif
#ifdef ECOMM
    [ECOMM]            = SPPC_ECOMM,
#endif
#ifdef EPROTO
    [EPROTO]           = SPPC_EPROTO,
#endif
#ifdef EMULTIHOP
    [EMULTIHOP]        = SPPC_EMULTIHOP,
#endif
#ifdef EDOTDOT
    [EDOTDOT]          = SPPC_EDOTDOT,
#endif
#ifdef EBADMSG
    [EBADMSG]          = SPPC_EBADMSG,
#endif
#ifdef EOVERFLOW
    [EOVERFLOW]        = SPPC_EOVERFLOW,
#endif
#ifdef ENOTUNIQ
    [ENOTUNIQ]         = SPPC_ENOTUNIQ,
#endif
#ifdef EBADFD
    [EBADFD]           = SPPC_EBADFD,
#endif
#ifdef EREMCHG
    [EREMCHG]          = SPPC_EREMCHG,
#endif
#ifdef ELIBACC
    [ELIBACC]          = SPPC_ELIBACC,
#endif
#ifdef ELIBBAD
    [ELIBBAD]          = SPPC_ELIBBAD,
#endif
#ifdef ELIBSCN
    [ELIBSCN]          = SPPC_ELIBSCN,
#endif
#ifdef ELIBMAX
    [ELIBMAX]          = SPPC_ELIBMAX,
#endif
#ifdef ELIBEXEC
    [ELIBEXEC]         = SPPC_ELIBEXEC,
#endif
#ifdef EILSEQ
    [EILSEQ]           = SPPC_EILSEQ,
#endif
#ifdef ERESTART
    [ERESTART]         = SPPC_ERESTART,
#endif
#ifdef ESTRPIPE
    [ESTRPIPE]         = SPPC_ESTRPIPE,
#endif
#ifdef EUSERS
    [EUSERS]           = SPPC_EUSERS,
#endif
#ifdef ENOTSOCK
    [ENOTSOCK]         = SPPC_ENOTSOCK,
#endif
#ifdef EDESTADDRREQ
    [EDESTADDRREQ]     = SPPC_EDESTADDRREQ,
#endif
#ifdef EMSGSIZE
    [EMSGSIZE]         = SPPC_EMSGSIZE,
#endif
#ifdef EPROTOTYPE
    [EPROTOTYPE]       = SPPC_EPROTOTYPE,
#endif
#ifdef ENOPROTOOPT
    [ENOPROTOOPT]      = SPPC_ENOPROTOOPT,
#endif
#ifdef EPROTONOSUPPORT
    [EPROTONOSUPPORT]  = SPPC_EPROTONOSUPPORT,
#endif
#ifdef ESOCKTNOSUPPORT
    [ESOCKTNOSUPPORT]  = SPPC_ESOCKTNOSUPPORT,
#endif
#ifdef EOPNOTSUPP
    [EOPNOTSUPP]       = SPPC_EOPNOTSUPP,
#endif
#ifdef EPFNOSUPPORT
    [EPFNOSUPPORT]     = SPPC_EPFNOSUPPORT,
#endif
#ifdef EAFNOSUPPORT
    [EAFNOSUPPORT]     = SPPC_EAFNOSUPPORT,
#endif
#ifdef EADDRINUSE
    [EADDRINUSE]       = SPPC_EADDRINUSE,
#endif
#ifdef EADDRNOTAVAIL
    [EADDRNOTAVAIL]    = SPPC_EADDRNOTAVAIL,
#endif
#ifdef ENETDOWN
    [ENETDOWN]         = SPPC_ENETDOWN,
#endif
#ifdef ENETUNREACH
    [ENETUNREACH]      = SPPC_ENETUNREACH,
#endif
#ifdef ENETRESET
    [ENETRESET]        = SPPC_ENETRESET,
#endif
#ifdef ECONNABORTED
    [ECONNABORTED]     = SPPC_ECONNABORTED,
#endif
#ifdef ECONNRESET
    [ECONNRESET]       = SPPC_ECONNRESET,
#endif
#ifdef ENOBUFS
    [ENOBUFS]          = SPPC_ENOBUFS,
#endif
#ifdef EISCONN
    [EISCONN]          = SPPC_EISCONN,
#endif
#ifdef ENOTCONN
    [ENOTCONN]         = SPPC_ENOTCONN,
#endif
#ifdef ESHUTDOWN
    [ESHUTDOWN]        = SPPC_ESHUTDOWN,
#endif
#ifdef ETOOMANYREFS
    [ETOOMANYREFS]     = SPPC_ETOOMANYREFS,
#endif
#ifdef ETIMEDOUT
    [ETIMEDOUT]        = SPPC_ETIMEDOUT,
#endif
#ifdef ECONNREFUSED
    [ECONNREFUSED]     = SPPC_ECONNREFUSED,
#endif
#ifdef EHOSTDOWN
    [EHOSTDOWN]        = SPPC_EHOSTDOWN,
#endif
#ifdef EHOSTUNREACH
    [EHOSTUNREACH]     = SPPC_EHOSTUNREACH,
#endif
#ifdef EALREADY
    [EALREADY]         = SPPC_EALREADY,
#endif
#ifdef EINPROGRESS
    [EINPROGRESS]      = SPPC_EINPROGRESS,
#endif
#ifdef ESTALE
    [ESTALE]           = SPPC_ESTALE,
#endif
#ifdef EUCLEAN
    [EUCLEAN]          = SPPC_EUCLEAN,
#endif
#ifdef ENOTNAM
    [ENOTNAM]          = SPPC_ENOTNAM,
#endif
#ifdef ENAVAIL
    [ENAVAIL]          = SPPC_ENAVAIL,
#endif
#ifdef EISNAM
    [EISNAM]           = SPPC_EISNAM,
#endif
#ifdef EREMOTEIO
    [EREMOTEIO]        = SPPC_EREMOTEIO,
#endif
#ifdef EDQUOT
    [EDQUOT]           = SPPC_EDQUOT,
#endif
#ifdef ENOMEDIUM
    [ENOMEDIUM]        = SPPC_ENOMEDIUM,
#endif
#ifdef EMEDIUMTYPE
    [EMEDIUMTYPE]      = SPPC_EMEDIUMTYPE,
#endif
#ifdef ECANCELED
    [ECANCELED]        = SPPC_ECANCELED,
#endif
#ifdef ENOKEY
    [ENOKEY]           = SPPC_ENOKEY,
#endif
#ifdef EKEYEXPIRED
    [EKEYEXPIRED]      = SPPC_EKEYEXPIRED,
#endif
#ifdef EKEYREVOKED
    [EKEYREVOKED]      = SPPC_EKEYREVOKED,
#endif
#ifdef EKEYREJECTED
    [EKEYREJECTED]     = SPPC_EKEYREJECTED,
#endif
#ifdef EOWNERDEAD
    [EOWNERDEAD]       = SPPC_EOWNERDEAD,
#endif
#ifdef ENOTRECOVERABLE
    [ENOTRECOVERABLE]  = SPPC_ENOTRECOVERABLE,
#endif
#ifdef ERFKILL
    [ERFKILL]          = SPPC_ERFKILL,
#endif
#ifdef EHWPOISON
    [EHWPOISON]        = SPPC_EHWPOISON,
#endif
    // Errors of a platform's own, where a Linux one means the same thing.
#if defined(ENOTSUP) && ENOTSUP != EOPNOTSUPP
    [ENOTSUP]          = SPPC_EOPNOTSUPP, // Darwin's second "not supported", one with EOPNOTSUPP on Linux
#endif
#ifdef EPROCLIM
    [EPROCLIM]         = SPPC_EAGAIN, // too many processes: Linux's fork says EAGAIN
#endif
#ifdef EBADEXEC
    [EBADEXEC]         = SPPC_ENOEXEC, // a binary that cannot be run, for one reason
#endif
#ifdef EBADARCH
    [EBADARCH]         = SPPC_ENOEXEC, // or another
#endif
#ifdef ESHLIBVERS
    [ESHLIBVERS]       = SPPC_ENOEXEC,
#endif
#ifdef EBADMACHO
    [EBADMACHO]        = SPPC_ENOEXEC,
#endif
#if defined(ENOATTR) && ENOATTR != ENODATA
    [ENOATTR]          = SPPC_ENODATA, // no such extended attribute, as Linux's xattr calls say
#endif
#ifdef EQFULL
    [EQFULL]           = SPPC_ENOBUFS, // an interface's output queue is full
#endif
  };
  if (err == 0) { return 0; }
  const int out = err > 0 && (size_t)err < sizeof table ? table[err] : 0;
  return out != 0 ? out : SPPC_EIO;
#endif
}
