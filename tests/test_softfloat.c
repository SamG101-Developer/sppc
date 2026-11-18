#include <sppc/sppc.h>
#include <stdio.h>
#include <string.h>

// The half conversions (sppc/softfloat.h). Where the compiler has "_Float16"
// (gcc and clang on x86-64 and AArch64), every half is checked both ways
// against it, and a sweep of floats and doubles, the boundaries included, one
// way; elsewhere (msvc), known values. On x86-64 Windows the helpers LLVM calls
// are checked too, through the float they pass a half in.

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

static uint32_t f32_bits(const float f) { uint32_t u; memcpy(&u, &f, sizeof u); return u; }
static uint64_t f64_bits(const double d) { uint64_t u; memcpy(&u, &d, sizeof u); return u; }
static float bits_f32(const uint32_t u) { float f; memcpy(&f, &u, sizeof f); return f; }
static double bits_f64(const uint64_t u) { double d; memcpy(&d, &u, sizeof d); return d; }

// Two NaNs agree here if both are NaNs: payloads are not compared.
static int same_f16(const uint16_t a, const uint16_t b) {
  const int a_nan = (a & 0x7C00u) == 0x7C00u && (a & 0x3FFu) != 0;
  const int b_nan = (b & 0x7C00u) == 0x7C00u && (b & 0x3FFu) != 0;
  return a_nan || b_nan ? a_nan && b_nan : a == b;
}

static void check_known_values(void) {
  // Half to float: 1, -2, the largest, the smallest normal and subnormal, an
  // infinity, a NaN.
  CHECK(_sppc_f16_to_f32_bits(0x3C00u) == f32_bits(1.0f));
  CHECK(_sppc_f16_to_f32_bits(0xC000u) == f32_bits(-2.0f));
  CHECK(_sppc_f16_to_f32_bits(0x7BFFu) == f32_bits(65504.0f));
  CHECK(_sppc_f16_to_f32_bits(0x0400u) == f32_bits(6.103515625e-05f));
  CHECK(_sppc_f16_to_f32_bits(0x0001u) == f32_bits(5.9604644775390625e-08f));
  CHECK(_sppc_f16_to_f32_bits(0x8000u) == f32_bits(-0.0f));
  CHECK(_sppc_f16_to_f32_bits(0x7C00u) == 0x7F800000u);
  CHECK((_sppc_f16_to_f32_bits(0x7E00u) & 0x7FC00000u) == 0x7FC00000u);

  // Double and float to half: exact, ties to even, overflow, underflow.
  CHECK(_sppc_f64_to_f16_bits(f64_bits(1.0)) == 0x3C00u);
  CHECK(_sppc_f64_to_f16_bits(f64_bits(65504.0)) == 0x7BFFu);
  CHECK(_sppc_f64_to_f16_bits(f64_bits(65520.0)) == 0x7C00u);
  CHECK(_sppc_f64_to_f16_bits(f64_bits(65519.99)) == 0x7BFFu);
  CHECK(_sppc_f64_to_f16_bits(f64_bits(1.0 + 1.0 / 2048.0)) == 0x3C00u);
  CHECK(_sppc_f64_to_f16_bits(f64_bits(1.0 + 3.0 / 2048.0)) == 0x3C02u);
  CHECK(_sppc_f64_to_f16_bits(f64_bits(2.98023223876953125e-08)) == 0x0000u);
  // Half the smallest subnormal is a tie (to even, 0); the next double above it rounds up.
  CHECK(_sppc_f64_to_f16_bits(f64_bits(2.98023223876953125e-08) + 1) == 0x0001u);
  CHECK(_sppc_f64_to_f16_bits(f64_bits(-1e-300)) == 0x8000u);
  CHECK(_sppc_f64_to_f16_bits(f64_bits(1e300)) == 0x7C00u);
  CHECK(_sppc_f32_to_f16_bits(f32_bits(-0.5f)) == 0xB800u);
  CHECK(_sppc_f32_to_f16_bits(f32_bits(6.0975551605224609375e-05f)) == 0x03FFu);
  CHECK(_sppc_f32_to_f16_bits(0xFF800000u) == 0xFC00u);
  CHECK((_sppc_f32_to_f16_bits(0x7FC00000u) & 0x7E00u) == 0x7E00u);
}

#if defined(__FLT16_MANT_DIG__)
static uint16_t f16_bits(const _Float16 h) { uint16_t u; memcpy(&u, &h, sizeof u); return u; }

static void check_against_float16(void) {
  // Every half, to float and back.
  for (uint32_t h = 0; h <= 0xFFFFu; ++h) {
    _Float16 value;
    const uint16_t bits = (uint16_t)h;
    memcpy(&value, &bits, sizeof value);
    const float wide = (float)value;
    const int nan = wide != wide;
    CHECK(nan ? (_sppc_f16_to_f32_bits(bits) & 0x7F800000u) == 0x7F800000u
              && (_sppc_f16_to_f32_bits(bits) & 0x7FFFFFu) != 0
              : _sppc_f16_to_f32_bits(bits) == f32_bits(wide));
    CHECK(same_f16(_sppc_f32_to_f16_bits(f32_bits(wide)), bits));
  }

  // Floats: every 4093rd bit pattern (a prime stride, so every exponent and
  // many mantissas), and every float within reach of a half's boundaries.
  for (uint64_t u = 0; u <= 0xFFFFFFFFull; u += 4093) {
    const float f = bits_f32((uint32_t)u);
    CHECK(same_f16(_sppc_f32_to_f16_bits((uint32_t)u), f16_bits((_Float16)f)));
  }
  static const float edges[] = {65504.0f, 65520.0f, 6.103515625e-05f, 5.9604644775390625e-08f, 2.98023223876953125e-08f};
  for (size_t i = 0; i < sizeof edges / sizeof edges[0]; ++i) {
    const uint32_t centre = f32_bits(edges[i]);
    for (uint32_t d = 0; d < 4096; ++d) {
      for (int s = 0; s < 2; ++s) {
        const uint32_t u = (centre - 2048 + d) | (s ? 0x80000000u : 0u);
        CHECK(same_f16(_sppc_f32_to_f16_bits(u), f16_bits((_Float16)bits_f32(u))));
      }
    }
  }

  // Doubles: a stride over the bit patterns, and the same boundaries, where a
  // double rounded through a float first would round twice.
  for (uint64_t u = 0; u < 0xFFF0000000000000ull; u += 0x0000FFFFFFFFull * 37 + 1) {
    CHECK(same_f16(_sppc_f64_to_f16_bits(u), f16_bits((_Float16)bits_f64(u))));
  }
  for (size_t i = 0; i < sizeof edges / sizeof edges[0]; ++i) {
    const uint64_t centre = f64_bits((double)edges[i]);
    for (uint64_t d = 0; d < 4096; ++d) {
      const uint64_t u = centre - 2048 + d * ((1ull << 29) / 4096) ;
      CHECK(same_f16(_sppc_f64_to_f16_bits(u), f16_bits((_Float16)bits_f64(u))));
    }
  }
  const double just_above_half_ulp = 1.0 + 1.0 / 2048.0 + 1.0 / 4194304.0;
  CHECK(_sppc_f64_to_f16_bits(f64_bits(just_above_half_ulp)) == f16_bits((_Float16)just_above_half_ulp));
}
#endif

#if SPPC_PLATFORM_WINDOWS && SPPC_ARCH_X86_64
static void check_exported_helpers(void) {
  // A half travels in a float's low 16 bits.
  CHECK(f32_bits(__extendhfsf2(bits_f32(0x3C00u))) == f32_bits(1.0f));
  CHECK(f32_bits(__extendhfsf2(bits_f32(0xABCD0000u | 0xC000u))) == f32_bits(-2.0f));
  CHECK((f32_bits(__truncsfhf2(65504.0f)) & 0xFFFFu) == 0x7BFFu);
  CHECK((f32_bits(__truncdfhf2(1.0 + 3.0 / 2048.0)) & 0xFFFFu) == 0x3C02u);
  CHECK((f32_bits(__truncdfhf2(1e300)) & 0xFFFFu) == 0x7C00u);
}
#endif

int main(void) {
  check_known_values();
#if defined(__FLT16_MANT_DIG__)
  check_against_float16();
#endif
#if SPPC_PLATFORM_WINDOWS && SPPC_ARCH_X86_64
  check_exported_helpers();
#endif
  if (failures != 0) {
    printf("%d failure(s)\n", failures);
    return 1;
  }
  printf("softfloat: ok\n");
  return 0;
}
