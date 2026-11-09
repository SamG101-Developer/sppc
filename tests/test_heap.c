#include "check.h"

// The heap and the byte and string helpers, which every platform has: blocks aligned as
// asked (and plain ones for any type), their contents kept across every kind
// of resize, the sizes no machine can meet refused, and memcpy, memset,
// memmove, memcmp and memmem with their offsets, overlaps and not-found
// cases. Where C libraries answer differently (a resize to 0, an alignment
// that is not a power of two, or below a pointer's size), sppc gives one
// answer everywhere, and this checks it is the same on each.

#include <errno.h>
#include <stddef.h>

// What a plain block must suit: any type of fundamental alignment (msvc's C
// library has no "max_align_t" to say so).
typedef union {
  long double ld;
  long long ll;
  double d;
  void *p;
  void (*fn)(void);
} fundamental;

static void fill(unsigned char *p, const size_t n, const unsigned seed) {
  for (size_t i = 0; i < n; ++i) { p[i] = (unsigned char)(i * 31u + seed); }
}

static bool filled(unsigned char const *p, const size_t n, const unsigned seed) {
  for (size_t i = 0; i < n; ++i) {
    if (p[i] != (unsigned char)(i * 31u + seed)) { return false; }
  }
  return true;
}

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

static void check_alignment(void) {
  // A plain block suits any type.
  for (size_t n = 1; n < 300; n += 7) {
    unsigned char *const p = (unsigned char*)sppc_malloc(n);
    CHECK(p != NULL && (uintptr_t)p % _Alignof(fundamental) == 0);
    fill(p, n, 1);
    CHECK(filled(p, n, 1));
    sppc_free(p);
  }

  // Every power of two, the ones below a pointer's size included, to past a
  // page; and for any size, a whole number of them or not.
  for (size_t align = 1; align <= 65536; align *= 2) {
    const size_t n = align * ((200 + align - 1) / align) + (align > 1 ? 1 : 0);
    unsigned char *const p = (unsigned char*)sppc_aligned_alloc(n, align);
    CHECK(p != NULL && (uintptr_t)p % align == 0);
    fill(p, n, 2);
    CHECK(filled(p, n, 2));
    sppc_free(p);
  }

  // Anything else is refused. The results are kept volatile, as below: an
  // allocation only compared with NULL may otherwise be folded away, and
  // taken to have succeeded.
  void *volatile refused;
  errno = 0;
  refused = sppc_aligned_alloc(64, 0);
  CHECK(refused == NULL && errno == EINVAL);
  errno = 0;
  refused = sppc_aligned_alloc(96, 48);
  CHECK(refused == NULL && errno == EINVAL);
  errno = 0;
  refused = sppc_aligned_alloc(96, 3);
  CHECK(refused == NULL && errno == EINVAL);
}

static void check_resize(void) {
  // A plain block, grown and shrunk, keeps what fits.
  unsigned char *p = (unsigned char*)sppc_malloc(40);
  fill(p, 40, 3);
  p = (unsigned char*)sppc_realloc(p, 100000);
  CHECK(p != NULL && filled(p, 40, 3));
  fill(p, 100000, 4);
  p = (unsigned char*)sppc_realloc(p, 10);
  CHECK(p != NULL && filled(p, 10, 4));
  sppc_free(p);

  // An aligned one too, whether it grows or shrinks (its alignment is not
  // kept, as POSIX keeps none across a realloc).
  unsigned char *a = (unsigned char*)sppc_aligned_alloc(4096, 4096);
  fill(a, 500, 5);
  a = (unsigned char*)sppc_realloc(a, 5000);
  CHECK(a != NULL && filled(a, 500, 5));
  sppc_free(a);
  a = (unsigned char*)sppc_aligned_alloc(512, 256);
  fill(a, 500, 6);
  a = (unsigned char*)sppc_realloc(a, 20);
  CHECK(a != NULL && filled(a, 20, 6));
  a = (unsigned char*)sppc_realloc(a, 3000);
  CHECK(a != NULL && filled(a, 20, 6));
  sppc_free(a);

  // A resize to nothing frees the block; of no block, it is a fresh one.
  p = (unsigned char*)sppc_malloc(32);
  void *volatile freed = sppc_realloc(p, 0);
  CHECK(freed == NULL);
  void *volatile fresh = sppc_realloc(NULL, 0);
  CHECK(fresh != NULL);
  sppc_free(fresh);
}

static void check_too_big(void) {
  // POSIX's ENOMEM for a size no machine can meet, and for a calloc whose
  // size overflows; a failed realloc leaves the block as it was. Kept
  // volatile, or the compiler folds an allocation away.
  volatile size_t max = SIZE_MAX;
  void *volatile none;
  errno = 0;
  none = sppc_malloc(max);
  CHECK(none == NULL && errno == ENOMEM);
  errno = 0;
  none = sppc_malloc(max - 8);
  CHECK(none == NULL && errno == ENOMEM);
  errno = 0;
  none = sppc_calloc(max / 2, 3);
  CHECK(none == NULL && errno == ENOMEM);
  none = sppc_aligned_alloc(max - 63, 64);
  CHECK(none == NULL);

  unsigned char *p = (unsigned char*)sppc_malloc(64);
  fill(p, 64, 7);
  errno = 0;
  CHECK(sppc_realloc(p, max) == NULL && errno == ENOMEM);
  CHECK(filled(p, 64, 7));
  unsigned char *a = (unsigned char*)sppc_aligned_alloc(128, 128);
  fill(a, 64, 8);
  CHECK(sppc_realloc(a, max) == NULL);
  CHECK(filled(a, 64, 8));
  sppc_free(a);
  sppc_free(p);
}

// ==================== BYTES ====================

static void check_bytes(void) {
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

  // An empty needle is found at once; a needle longer than the haystack, or
  // an empty haystack, never; a match at the very end is found.
  char const hay[] = "abcabcabd";
  CHECK(sppc_memmem(hay, 9, "", 0, &idx) == 0 && idx == 0);
  CHECK(sppc_memmem(hay, 9, "abd", 3, &idx) == 0 && idx == 6);
  CHECK(sppc_memmem(hay, 9, "d", 1, &idx) == 0 && idx == 8);
  CHECK(sppc_memmem(hay, 2, "abc", 3, &idx) == SPPC_ENOENT);
  CHECK(sppc_memmem(hay, 0, "a", 1, &idx) == SPPC_ENOENT);
  char const bin[] = {1, 0, 2, 0, 3, 0, 3};
  char const pat[] = {0, 3, 0, 3};
  CHECK(sppc_memmem(bin, sizeof bin, pat, sizeof pat, &idx) == 0 && idx == 3);
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
  check_alignment();
  check_resize();
  check_too_big();
  check_bytes();
  check_strings();
  sppc_cleanup();
  return check_done();
}
