#pragma once

/// Half-precision ("F16") conversions in integer arithmetic, and on x86-64
/// Windows the compiler-rt helpers LLVM calls for them. Baseline x86-64 has no
/// half instructions (F16C is not in it), so LLVM converts a half through a
/// libcall: libgcc has these on Linux and libSystem on macOS, but the MSVC
/// runtime has none, and a program using F16 failed to link ("undefined symbol:
/// __truncdfhf2"). AArch64 converts with "fcvt" and needs none. Each rounds once,
/// to nearest even, and keeps a NaN's sign and the top of its payload, quiet.

#include <sppc/config.h>
#include <sppc/macros.h>
#include <stdint.h>
#include <string.h>

// ==================== SOFT FLOAT ====================

/// A double's bits as a half's, rounded to nearest even: an infinity past the
/// half's range, a signed zero below its smallest subnormal.
_gnu_inline
uint16_t _sppc_f64_to_f16_bits(const uint64_t bits) {
  const uint16_t sign = (uint16_t)((bits >> 48) & 0x8000u);
  const int exponent = (int)((bits >> 52) & 0x7FFu);
  const uint64_t mantissa = bits & 0xFFFFFFFFFFFFFull;

  // An infinity stays one; a NaN keeps its top payload bits and is made quiet.
  if (exponent == 0x7FF) {
    if (mantissa == 0) { return (uint16_t)(sign | 0x7C00u); }
    return (uint16_t)(sign | 0x7E00u | (uint16_t)(mantissa >> 42));
  }

  // A double subnormal is far below a half's range: a signed zero.
  if (exponent == 0) { return sign; }

  // The half's biased exponent, and how far the 53-bit significand is shifted
  // down to the half's 11 (10 stored); a half subnormal (biased exponent 0 or
  // below) is shifted further, one place per step below 1.
  const int half_exponent = exponent - 1023 + 15;
  if (half_exponent >= 0x1F) { return (uint16_t)(sign | 0x7C00u); }
  const uint64_t significand = mantissa | (1ull << 52);
  const int shift = half_exponent > 0 ? 42 : 43 - half_exponent;
  if (shift > 63) { return sign; }

  uint64_t kept = significand >> shift;
  const uint64_t dropped = significand & ((1ull << shift) - 1);
  const uint64_t halfway = 1ull << (shift - 1);
  if (dropped > halfway || (dropped == halfway && (kept & 1u) != 0)) { ++kept; }

  // A subnormal's bits are the kept value itself: rounding up into the
  // smallest normal (0x400) is that normal's encoding too.
  if (half_exponent <= 0) { return (uint16_t)(sign | kept); }

  // Rounding up past 11 bits carries into the exponent.
  int biased = half_exponent;
  if ((kept >> 11) != 0) {
    kept >>= 1;
    ++biased;
    if (biased >= 0x1F) { return (uint16_t)(sign | 0x7C00u); }
  }
  return (uint16_t)(sign | (uint16_t)(biased << 10) | (uint16_t)(kept & 0x3FFu));
}

/// A float's bits as a half's: through a double, which holds every float
/// exactly, so it is still rounded only once.
_gnu_inline
uint16_t _sppc_f32_to_f16_bits(const uint32_t bits) {
  const uint64_t sign = (uint64_t)(bits & 0x80000000u) << 32;
  const uint32_t exponent = (bits >> 23) & 0xFFu;
  const uint64_t mantissa = bits & 0x7FFFFFu;
  if (exponent == 0xFF) { return _sppc_f64_to_f16_bits(sign | 0x7FF0000000000000ull | (mantissa << 29)); }
  // A float subnormal is far below a half's range too.
  if (exponent == 0) { return (uint16_t)(sign >> 48); }
  return _sppc_f64_to_f16_bits(sign | ((uint64_t)(exponent - 127 + 1023) << 52) | (mantissa << 29));
}

/// A half's bits as a float's, exactly: every half is a float.
_gnu_inline
uint32_t _sppc_f16_to_f32_bits(const uint16_t bits) {
  const uint32_t sign = (uint32_t)(bits & 0x8000u) << 16;
  const uint32_t exponent = (bits >> 10) & 0x1Fu;
  uint32_t mantissa = bits & 0x3FFu;

  // An infinity or a NaN, payload kept.
  if (exponent == 0x1F) { return sign | 0x7F800000u | (mantissa << 13); }

  if (exponent == 0) {
    if (mantissa == 0) { return sign; }
    // A half subnormal is a float normal: shift its leading one into the
    // implicit place, one exponent step per place.
    int unbiased = -14;
    while ((mantissa & 0x400u) == 0) {
      mantissa <<= 1;
      --unbiased;
    }
    return sign | ((uint32_t)(unbiased + 127) << 23) | ((mantissa & 0x3FFu) << 13);
  }
  return sign | ((exponent - 15 + 127) << 23) | (mantissa << 13);
}

// The helpers LLVM calls on x86-64 Windows. A half travels in XMM0's low 16
// bits there, as a float does in XMM0 under the Microsoft x64 convention, so
// each takes or returns a "float" whose low 16 bits are the half: msvc has no
// "_Float16" to spell it with. Exported from the dll, as programs call them
// through sppc.lib.
#if SPPC_PLATFORM_WINDOWS && SPPC_ARCH_X86_64

/// A half (in a float's low 16 bits) as a float.
_gnu_inline
_sppc_api float __extendhfsf2(const float half) {
  uint32_t in;
  memcpy(&in, &half, sizeof in);
  const uint32_t out = _sppc_f16_to_f32_bits((uint16_t)(in & 0xFFFFu));
  float result;
  memcpy(&result, &out, sizeof result);
  return result;
}

/// A float as a half, in the result's low 16 bits.
_gnu_inline
_sppc_api float __truncsfhf2(const float value) {
  uint32_t in;
  memcpy(&in, &value, sizeof in);
  const uint32_t out = _sppc_f32_to_f16_bits(in);
  float result;
  memcpy(&result, &out, sizeof result);
  return result;
}

/// A double as a half, in the result's low 16 bits.
_gnu_inline
_sppc_api float __truncdfhf2(const double value) {
  uint64_t in;
  memcpy(&in, &value, sizeof in);
  const uint32_t out = _sppc_f64_to_f16_bits(in);
  float result;
  memcpy(&result, &out, sizeof result);
  return result;
}

#endif
