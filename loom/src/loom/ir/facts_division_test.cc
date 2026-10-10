// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <array>
#include <bitset>
#include <climits>
#include <cstdint>
#include <vector>

#include "iree/testing/gtest.h"
#include "loom/ir/facts.h"

namespace loom {
namespace {

static uint64_t RawBits(int64_t value, int32_t bit_count) {
  return (uint64_t)value & iree_math_mask_low_bits_u64(UINT64_MAX, bit_count);
}

static int64_t SignedRepresentation(uint64_t raw_bits, int32_t bit_count) {
  raw_bits &= iree_math_mask_low_bits_u64(UINT64_MAX, bit_count);
  if (bit_count == 1) {
    return (int64_t)raw_bits;
  }
  const uint64_t sign_bit = UINT64_C(1) << (bit_count - 1);
  if ((raw_bits & sign_bit) == 0) {
    return (int64_t)raw_bits;
  }
  const int64_t signed_sign_bit =
      bit_count == 64 ? INT64_MIN : -(int64_t)sign_bit;
  return signed_sign_bit + (int64_t)(raw_bits & (sign_bit - 1));
}

static loom_value_facts_t ExactBits(uint64_t raw_bits, int32_t bit_count) {
  return loom_value_facts_exact_i64(SignedRepresentation(raw_bits, bit_count));
}

static void ExpectContains(loom_value_facts_t facts, int64_t value) {
  EXPECT_LE(facts.range_lo, value);
  EXPECT_GE(facts.range_hi, value);
  EXPECT_EQ(value % facts.known_divisor, 0);
}

TEST(UnsignedDivisionTransfer, ExactSmallWidths) {
  for (int32_t bit_count = 1; bit_count <= 8; ++bit_count) {
    const uint64_t mask = iree_math_mask_low_bits_u64(UINT64_MAX, bit_count);
    for (uint64_t lhs_bits = 0; lhs_bits <= mask; ++lhs_bits) {
      const loom_value_facts_t lhs = ExactBits(lhs_bits, bit_count);
      for (uint64_t rhs_bits = 1; rhs_bits <= mask; ++rhs_bits) {
        SCOPED_TRACE(::testing::Message() << "width=" << bit_count << " lhs="
                                          << lhs_bits << " rhs=" << rhs_bits);
        const loom_value_facts_t rhs = ExactBits(rhs_bits, bit_count);
        EXPECT_EQ(loom_value_facts_remui_is_identity(lhs, rhs, bit_count),
                  lhs_bits < rhs_bits);
        loom_value_facts_t quotient;
        loom_value_facts_divui(&lhs, &rhs, bit_count, &quotient);
        EXPECT_TRUE(loom_value_facts_is_exact(quotient));
        EXPECT_EQ(quotient.range_lo,
                  SignedRepresentation(lhs_bits / rhs_bits, bit_count));

        loom_value_facts_t remainder;
        loom_value_facts_remui(&lhs, &rhs, bit_count, &remainder);
        EXPECT_TRUE(loom_value_facts_is_exact(remainder));
        EXPECT_EQ(remainder.range_lo,
                  SignedRepresentation(lhs_bits % rhs_bits, bit_count));
      }
    }
  }
}

TEST(UnsignedDivisionTransfer, RepresentativeIntervalsContainEveryResult) {
  constexpr int64_t kEndpoints[] = {-8, -5, -1, 0, 1, 3, 7};
  for (int64_t lhs_lo : kEndpoints) {
    for (int64_t lhs_hi : kEndpoints) {
      if (lhs_lo > lhs_hi) {
        continue;
      }
      for (int64_t lhs_divisor :
           {INT64_C(1), INT64_C(2), INT64_C(3), INT64_C(4)}) {
        const loom_value_facts_t lhs =
            loom_value_facts_make(lhs_lo, lhs_hi, lhs_divisor);
        for (int64_t rhs_lo : kEndpoints) {
          for (int64_t rhs_hi : kEndpoints) {
            if (rhs_lo > rhs_hi) {
              continue;
            }
            SCOPED_TRACE(::testing::Message()
                         << "lhs=[" << lhs_lo << "," << lhs_hi
                         << "] divisor=" << lhs_divisor << " rhs=[" << rhs_lo
                         << "," << rhs_hi << "]");
            const loom_value_facts_t rhs =
                loom_value_facts_make(rhs_lo, rhs_hi, 1);
            loom_value_facts_t quotient;
            loom_value_facts_divui(&lhs, &rhs, 4, &quotient);
            loom_value_facts_t remainder;
            loom_value_facts_remui(&lhs, &rhs, 4, &remainder);
            const bool identity =
                loom_value_facts_remui_is_identity(lhs, rhs, 4);
            for (int64_t lhs_value = lhs_lo; lhs_value <= lhs_hi; ++lhs_value) {
              if (lhs_value % lhs.known_divisor != 0) {
                continue;
              }
              const uint64_t lhs_bits = RawBits(lhs_value, 4);
              for (int64_t rhs_value = rhs_lo; rhs_value <= rhs_hi;
                   ++rhs_value) {
                if (rhs_value == 0) {
                  continue;
                }
                const uint64_t rhs_bits = RawBits(rhs_value, 4);
                if (identity) {
                  EXPECT_LT(lhs_bits, rhs_bits);
                }
                ExpectContains(quotient,
                               SignedRepresentation(lhs_bits / rhs_bits, 4));
                ExpectContains(remainder,
                               SignedRepresentation(lhs_bits % rhs_bits, 4));
              }
            }
          }
        }
      }
    }
  }
}

TEST(UnsignedDivisionTransfer, ExactNegativeRepresentationsUseDeclaredWidth) {
  const loom_value_facts_t dividend = loom_value_facts_exact_i64(-30);
  const loom_value_facts_t divisor = loom_value_facts_exact_i64(5);
  for (int32_t bit_count : {32, 64}) {
    const uint64_t dividend_bits = RawBits(-30, bit_count);
    loom_value_facts_t quotient;
    loom_value_facts_divui(&dividend, &divisor, bit_count, &quotient);
    EXPECT_TRUE(loom_value_facts_is_exact(quotient));
    EXPECT_EQ(quotient.range_lo,
              SignedRepresentation(dividend_bits / 5, bit_count));
    EXPECT_NE(quotient.known_divisor, 6);

    loom_value_facts_t remainder;
    loom_value_facts_remui(&dividend, &divisor, bit_count, &remainder);
    EXPECT_TRUE(loom_value_facts_is_exact(remainder));
    EXPECT_EQ(remainder.range_lo,
              SignedRepresentation(dividend_bits % 5, bit_count));
  }
}

TEST(UnsignedDivisionTransfer, UnknownDividendRetainsSmallDivisorBounds) {
  loom_value_facts_t dividend = loom_value_facts_unknown();
  loom_value_facts_mark_lane_varying(&dividend);
  const loom_value_facts_t divisor = loom_value_facts_exact_i64(9);
  for (int32_t bit_count : {8, 16, 32, 64}) {
    loom_value_facts_t quotient;
    loom_value_facts_divui(&dividend, &divisor, bit_count, &quotient);
    EXPECT_EQ(quotient.range_lo, 0);
    EXPECT_EQ(
        quotient.range_hi,
        (int64_t)(iree_math_mask_low_bits_u64(UINT64_MAX, bit_count) / 9));
    EXPECT_TRUE(loom_value_facts_is_lane_varying(quotient));

    loom_value_facts_t remainder;
    loom_value_facts_remui(&dividend, &divisor, bit_count, &remainder);
    EXPECT_EQ(remainder.range_lo, 0);
    EXPECT_EQ(remainder.range_hi, 8);
    EXPECT_TRUE(loom_value_facts_is_non_negative(remainder));
    EXPECT_TRUE(loom_value_facts_is_lane_varying(remainder));
  }
}

TEST(UnsignedDivisionTransfer, NegativeRangeRetainsOnlyRawBitDivisibility) {
  const loom_value_facts_t dividend = loom_value_facts_make(-32, -16, 16);
  const loom_value_facts_t divisor = loom_value_facts_exact_i64(4);
  loom_value_facts_t quotient;
  loom_value_facts_divui(&dividend, &divisor, 8, &quotient);
  EXPECT_EQ(quotient.range_lo, 56);
  EXPECT_EQ(quotient.range_hi, 60);
  EXPECT_EQ(quotient.known_divisor, 4);

  loom_value_facts_t remainder;
  loom_value_facts_remui(&dividend, &divisor, 8, &remainder);
  EXPECT_TRUE(loom_value_facts_is_exact(remainder));
  EXPECT_EQ(remainder.range_lo, 0);

  const loom_value_facts_t odd_dividend = loom_value_facts_make(-30, -15, 15);
  const loom_value_facts_t five = loom_value_facts_exact_i64(5);
  loom_value_facts_divui(&odd_dividend, &five, 8, &quotient);
  EXPECT_EQ(quotient.known_divisor, 1);
}

TEST(UnsignedDivisionTransfer, HighBitDivisorsUseRawWidth) {
  const loom_value_facts_t dividend = loom_value_facts_unknown();
  loom_value_facts_t divisor = ExactBits(UINT64_C(0x80), 8);
  loom_value_facts_t quotient;
  loom_value_facts_divui(&dividend, &divisor, 8, &quotient);
  EXPECT_EQ(quotient.range_lo, 0);
  EXPECT_EQ(quotient.range_hi, 1);
  loom_value_facts_t remainder;
  loom_value_facts_remui(&dividend, &divisor, 8, &remainder);
  EXPECT_EQ(remainder.range_lo, 0);
  EXPECT_EQ(remainder.range_hi, 127);

  divisor = ExactBits(UINT64_C(0xFF), 8);
  loom_value_facts_remui(&dividend, &divisor, 8, &remainder);
  EXPECT_EQ(remainder.range_lo, -128);
  EXPECT_EQ(remainder.range_hi, 127);
}

TEST(UnsignedDivisionTransfer, NonzeroFactExcludesDynamicZeroDivisor) {
  const loom_value_facts_t dividend = loom_value_facts_make(0, 63, 1);
  loom_value_facts_t divisor = loom_value_facts_make(0, 9, 1);
  loom_value_facts_t quotient;
  loom_value_facts_divui(&dividend, &divisor, 8, &quotient);
  EXPECT_EQ(quotient.range_lo, -128);
  EXPECT_EQ(quotient.range_hi, 127);
  loom_value_facts_t remainder;
  loom_value_facts_remui(&dividend, &divisor, 8, &remainder);
  EXPECT_EQ(remainder.range_lo, -128);
  EXPECT_EQ(remainder.range_hi, 127);

  divisor.flags |= LOOM_VALUE_FACT_NON_ZERO;
  loom_value_facts_divui(&dividend, &divisor, 8, &quotient);
  EXPECT_EQ(quotient.range_lo, 0);
  EXPECT_EQ(quotient.range_hi, 63);
  loom_value_facts_remui(&dividend, &divisor, 8, &remainder);
  EXPECT_EQ(remainder.range_lo, 0);
  EXPECT_EQ(remainder.range_hi, 8);
}

TEST(UnsignedDivisionTransfer, SupportsEitherOutputAlias) {
  loom_value_facts_t dividend = loom_value_facts_exact_i64(-30);
  loom_value_facts_t divisor = loom_value_facts_exact_i64(5);
  loom_value_facts_divui(&dividend, &divisor, 32, &dividend);
  EXPECT_EQ(dividend.range_lo, 858993453);

  dividend = loom_value_facts_exact_i64(-30);
  loom_value_facts_remui(&dividend, &divisor, 32, &divisor);
  EXPECT_EQ(divisor.range_lo, 1);
}

TEST(UnsignedDivisionTransfer, IdentityRetainsDividendFacts) {
  for (int32_t bit_count : {8, 16, 32, 64}) {
    loom_value_facts_t dividend = loom_value_facts_make(12, 60, 6);
    loom_value_facts_mark_uniform_at_scope(
        &dividend, LOOM_VALUE_FACT_UNIFORM_SCOPE_WORKGROUP);
    loom_value_facts_t divisor = loom_value_facts_make(61, 100, 1);
    loom_value_facts_mark_lane_varying(&divisor);
    EXPECT_TRUE(
        loom_value_facts_remui_is_identity(dividend, divisor, bit_count));
    loom_value_facts_t remainder;
    loom_value_facts_remui(&dividend, &divisor, bit_count, &remainder);
    EXPECT_TRUE(loom_value_facts_equal(remainder, dividend));

    // The negative representation is contiguous above the unsigned sign bit.
    dividend = loom_value_facts_make(-32, -16, 4);
    divisor = loom_value_facts_make(-15, -1, 1);
    EXPECT_TRUE(
        loom_value_facts_remui_is_identity(dividend, divisor, bit_count));
    loom_value_facts_remui(&dividend, &divisor, bit_count, &remainder);
    EXPECT_TRUE(loom_value_facts_equal(remainder, dividend));
  }
}

TEST(UnsignedDivisionTransfer, IdentityExcludesZeroOverlapAndWrapping) {
  for (int32_t bit_count : {1, 8, 16, 32, 64}) {
    const loom_value_facts_t zero = loom_value_facts_exact_i64(0);
    const loom_value_facts_t one = loom_value_facts_exact_i64(1);
    EXPECT_FALSE(loom_value_facts_remui_is_identity(zero, zero, bit_count));
    EXPECT_TRUE(loom_value_facts_remui_is_identity(zero, one, bit_count));
    EXPECT_FALSE(loom_value_facts_remui_is_identity(one, one, bit_count));
    EXPECT_FALSE(loom_value_facts_remui_is_identity(
        one, loom_value_facts_make(0, 1, 1), bit_count));
    EXPECT_FALSE(loom_value_facts_remui_is_identity(loom_value_facts_unknown(),
                                                    one, bit_count));
  }
  EXPECT_FALSE(loom_value_facts_remui_is_identity(
      loom_value_facts_make(0, 255, 1), loom_value_facts_exact_i64(257), 8));
}

TEST(RemsiTransfer, ExactMinimumOverflowPair) {
  loom_value_facts_t dividend = loom_value_facts_exact_i64(INT64_MIN);
  loom_value_facts_t divisor = loom_value_facts_exact_i64(-1);
  loom_value_facts_t out;
  loom_value_facts_remsi(&dividend, &divisor, &out);
  EXPECT_TRUE(loom_value_facts_is_exact(out));
  EXPECT_EQ(out.range_lo, 0);
}

TEST(RemsiTransfer, ExactRemainderPreservesDividendSign) {
  loom_value_facts_t out;
  loom_value_facts_t dividend = loom_value_facts_exact_i64(-17);
  loom_value_facts_t divisor = loom_value_facts_exact_i64(5);
  loom_value_facts_remsi(&dividend, &divisor, &out);
  EXPECT_TRUE(loom_value_facts_is_exact(out));
  EXPECT_EQ(out.range_lo, -2);

  dividend = loom_value_facts_exact_i64(17);
  divisor = loom_value_facts_exact_i64(-5);
  loom_value_facts_remsi(&dividend, &divisor, &out);
  EXPECT_TRUE(loom_value_facts_is_exact(out));
  EXPECT_EQ(out.range_lo, 2);
}

TEST(RemsiTransfer, MinimumDivisorHasRepresentableRemainderBounds) {
  loom_value_facts_t dividend = loom_value_facts_unknown();
  loom_value_facts_t divisor = loom_value_facts_exact_i64(INT64_MIN);
  loom_value_facts_t out;
  loom_value_facts_remsi(&dividend, &divisor, &out);
  EXPECT_EQ(out.range_lo, -INT64_MAX);
  EXPECT_EQ(out.range_hi, INT64_MAX);
}

constexpr loom_value_facts_quotient_kind_t kQuotientKinds[] = {
    LOOM_VALUE_FACTS_QUOTIENT_DIVUI,      LOOM_VALUE_FACTS_QUOTIENT_DIVSI,
    LOOM_VALUE_FACTS_QUOTIENT_CEILDIVUI,  LOOM_VALUE_FACTS_QUOTIENT_CEILDIVSI,
    LOOM_VALUE_FACTS_QUOTIENT_FLOORDIVSI, LOOM_VALUE_FACTS_QUOTIENT_SHRUI,
    LOOM_VALUE_FACTS_QUOTIENT_SHRSI,
};

// Concrete arithmetic oracle, independent of inverse interval formulas.
// False leaves the result unconstrained: these corners cannot establish any
// restriction on a dividend without a stronger source arithmetic contract.
static bool EvaluateQuotient(loom_value_facts_quotient_kind_t kind,
                             int32_t bit_count, int64_t dividend, int64_t scale,
                             int64_t* out_quotient) {
  const bool shift = kind == LOOM_VALUE_FACTS_QUOTIENT_SHRUI ||
                     kind == LOOM_VALUE_FACTS_QUOTIENT_SHRSI;
  if (shift && (scale < 0 || scale >= bit_count)) {
    return false;
  }
  if (kind == LOOM_VALUE_FACTS_QUOTIENT_DIVUI ||
      kind == LOOM_VALUE_FACTS_QUOTIENT_CEILDIVUI ||
      kind == LOOM_VALUE_FACTS_QUOTIENT_SHRUI) {
    const uint64_t numerator = RawBits(dividend, bit_count);
    const uint64_t denominator =
        shift ? UINT64_C(1) << scale : RawBits(scale, bit_count);
    if (denominator == 0) {
      return false;
    }
    const uint64_t quotient = numerator / denominator;
    const uint64_t rounded =
        quotient + (kind == LOOM_VALUE_FACTS_QUOTIENT_CEILDIVUI &&
                    numerator % denominator != 0);
    *out_quotient = SignedRepresentation(rounded, bit_count);
    return true;
  }
  if (bit_count == 1) {
    dividend = -dividend;
    if (!shift) {
      scale = -scale;
    }
  }
  if (shift) {
    // Arithmetic shift is floor division. C division below handles the other
    // signed cases; an unsigned magnitude handles the 2^63 shift divisor.
    const uint64_t denominator = UINT64_C(1) << scale;
    const uint64_t magnitude = iree_math_magnitude_i64(dividend);
    const uint64_t rounded = magnitude / denominator +
                             (dividend < 0 && magnitude % denominator != 0);
    const int64_t quotient = dividend >= 0 ? (int64_t)rounded
                             : rounded == (UINT64_C(1) << 63)
                                 ? INT64_MIN
                                 : -(int64_t)rounded;
    *out_quotient = bit_count == 1 ? -quotient : quotient;
    return true;
  }
  const int64_t minimum =
      bit_count == 64 ? INT64_MIN : -(INT64_C(1) << (bit_count - 1));
  if (scale == 0 || (dividend == minimum && scale == -1)) {
    return false;
  }
  int64_t quotient = dividend / scale;
  const int64_t remainder = dividend % scale;
  if (remainder != 0) {
    if (kind == LOOM_VALUE_FACTS_QUOTIENT_FLOORDIVSI &&
        (dividend < 0) != (scale < 0)) {
      --quotient;
    } else if (kind == LOOM_VALUE_FACTS_QUOTIENT_CEILDIVSI &&
               (dividend < 0) == (scale < 0)) {
      ++quotient;
    }
  }
  *out_quotient = bit_count == 1 ? -quotient : quotient;
  return true;
}

TEST(QuotientPreimage, FiniteIntervalsContainEveryAdmittedDividend) {
  for (int32_t bit_count : {1, 4, 8}) {
    const int64_t minimum =
        bit_count == 1 ? 0 : -(INT64_C(1) << (bit_count - 1));
    const int64_t maximum =
        bit_count == 1 ? 1 : (INT64_C(1) << (bit_count - 1)) - 1;
    std::vector<loom_value_facts_t> intervals;
    for (int64_t lower = minimum; lower <= maximum; ++lower) {
      for (int64_t upper = lower; upper <= maximum; ++upper) {
        if (bit_count != 8 || lower == minimum || upper == maximum ||
            lower == upper) {
          intervals.push_back(loom_value_facts_make(lower, upper, 1));
        }
      }
    }
    std::vector<loom_value_facts_t> scales = intervals;
    if (bit_count == 8) {
      scales.clear();
      for (int64_t value :
           {-128, -127, -33, -32, -3, -1, 0, 1, 2, 3, 31, 32, 33, 126, 127}) {
        scales.push_back(loom_value_facts_exact_i64(value));
      }
      for (auto range : {std::pair{-128, -1}, std::pair{-3, 3},
                         std::pair{1, 127}, std::pair{16, 32}}) {
        scales.push_back(loom_value_facts_make(range.first, range.second, 1));
      }
    }
    for (auto kind : kQuotientKinds) {
      for (auto scale : scales) {
        std::array<std::bitset<256>, 256> outputs;
        for (int64_t dividend = minimum; dividend <= maximum; ++dividend) {
          auto& possible = outputs[dividend - minimum];
          for (int64_t value = scale.range_lo; value <= scale.range_hi;
               ++value) {
            int64_t quotient = 0;
            if (EvaluateQuotient(kind, bit_count, dividend, value, &quotient)) {
              possible.set(quotient - minimum);
            } else {
              possible.set();
              break;
            }
          }
        }
        for (auto quotient : intervals) {
          const auto preimage = loom_value_facts_quotient_preimage(
              kind, bit_count, quotient, scale);
          std::bitset<256> admitted;
          for (int64_t value = quotient.range_lo; value <= quotient.range_hi;
               ++value) {
            admitted.set(value - minimum);
          }
          for (int64_t dividend = minimum; dividend <= maximum; ++dividend) {
            if ((outputs[dividend - minimum] & admitted).any()) {
              ASSERT_TRUE(dividend >= preimage.range_lo &&
                          dividend <= preimage.range_hi)
                  << "kind=" << kind << " width=" << bit_count << " quotient=["
                  << quotient.range_lo << "," << quotient.range_hi
                  << "] scale=[" << scale.range_lo << "," << scale.range_hi
                  << "] preimage=[" << preimage.range_lo << ","
                  << preimage.range_hi << "] dividend=" << dividend;
            }
          }
        }
      }
    }
  }
}

TEST(QuotientPreimage, WideCarrierBoundaryPoints) {
  for (int32_t bit_count : {16, 32, 64}) {
    const auto domain = loom_value_facts_make_signed_bit_count_range(bit_count);
    const int64_t values[] = {domain.range_lo,
                              domain.range_lo + 1,
                              domain.range_lo / 2,
                              -63,
                              -3,
                              -1,
                              0,
                              1,
                              2,
                              3,
                              31,
                              63,
                              domain.range_hi / 2,
                              domain.range_hi - 1,
                              domain.range_hi};
    for (auto kind : kQuotientKinds) {
      for (int64_t scale : values) {
        for (int64_t dividend : values) {
          int64_t quotient = 0;
          const bool constrained =
              EvaluateQuotient(kind, bit_count, dividend, scale, &quotient);
          for (int64_t lower : {domain.range_lo, quotient}) {
            for (int64_t upper : {quotient, domain.range_hi}) {
              const auto preimage = loom_value_facts_quotient_preimage(
                  kind, bit_count, loom_value_facts_make(lower, upper, 1),
                  loom_value_facts_exact_i64(scale));
              SCOPED_TRACE(::testing::Message()
                           << "kind=" << kind << " width=" << bit_count
                           << " scale=" << scale << " dividend=" << dividend
                           << " constrained=" << constrained);
              ExpectContains(preimage, dividend);
            }
          }
        }
      }
    }
  }
}

TEST(QuotientPreimage, RetainsRoundingResiduesAndWideEndpoints) {
  struct Case {
    // Quotient operation semantics.
    loom_value_facts_quotient_kind_t kind;
    // Inclusive quotient lower bound.
    int64_t lower_quotient;
    // Inclusive quotient upper bound.
    int64_t upper_quotient;
    // Exact divisor or shift amount.
    int64_t scale;
    // Expected inclusive dividend lower bound.
    int64_t lower;
    // Expected inclusive dividend upper bound.
    int64_t upper;
  };
  const Case cases[] = {
      {LOOM_VALUE_FACTS_QUOTIENT_SHRUI, 0, 72, 5, 0, 2335},
      {LOOM_VALUE_FACTS_QUOTIENT_DIVSI, 0, 0, 32, -31, 31},
      {LOOM_VALUE_FACTS_QUOTIENT_FLOORDIVSI, 0, 0, 32, 0, 31},
      {LOOM_VALUE_FACTS_QUOTIENT_CEILDIVSI, 0, 0, 32, -31, 0},
      {LOOM_VALUE_FACTS_QUOTIENT_FLOORDIVSI, -2, -1, -32, 1, 64},
      {LOOM_VALUE_FACTS_QUOTIENT_CEILDIVSI, -2, -1, -32, 32, 95},
      {LOOM_VALUE_FACTS_QUOTIENT_DIVUI, -1, -1, 1, -1, -1},
      {LOOM_VALUE_FACTS_QUOTIENT_CEILDIVUI, -1, -1, 1, -1, -1},
      {LOOM_VALUE_FACTS_QUOTIENT_SHRUI, 1, 1, 63, INT64_MIN, -1},
      {LOOM_VALUE_FACTS_QUOTIENT_SHRSI, -1, -1, 63, INT64_MIN, -1},
      {LOOM_VALUE_FACTS_QUOTIENT_DIVSI, INT64_MAX, INT64_MAX, 1, INT64_MAX,
       INT64_MAX},
      {LOOM_VALUE_FACTS_QUOTIENT_DIVSI, INT64_MIN, INT64_MIN, 1, INT64_MIN,
       INT64_MIN},
      {LOOM_VALUE_FACTS_QUOTIENT_DIVSI, 1, 1, INT64_MIN, INT64_MIN, INT64_MIN},
  };
  for (auto test : cases) {
    const auto actual = loom_value_facts_quotient_preimage(
        test.kind, 64,
        loom_value_facts_make(test.lower_quotient, test.upper_quotient, 1),
        loom_value_facts_exact_i64(test.scale));
    EXPECT_EQ(actual.range_lo, test.lower);
    EXPECT_EQ(actual.range_hi, test.upper);
  }
}

}  // namespace
}  // namespace loom
