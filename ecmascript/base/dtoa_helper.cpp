/*
 * Copyright (c) 2024 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "ecmascript/base/dtoa_helper.h"
#include "ecmascript/base/number_helper.h"

#ifndef UINT64_C2
#define UINT64_C2(high32, low32) ((static_cast<uint64_t>(high32) << 32) | static_cast<uint64_t>(low32))
#endif
namespace panda::ecmascript::base {
DtoaHelper::DiyFp DtoaHelper::GetCachedPowerByIndex(size_t index)
{
    // 10^-348, 10^-340, ..., 10^340
    static const uint64_t kCachedPowers_F[] = {
        UINT64_C2(0xfa8fd5a0, 0x081c0288), UINT64_C2(0xbaaee17f, 0xa23ebf76), UINT64_C2(0x8b16fb20, 0x3055ac76),
        UINT64_C2(0xcf42894a, 0x5dce35ea), UINT64_C2(0x9a6bb0aa, 0x55653b2d), UINT64_C2(0xe61acf03, 0x3d1a45df),
        UINT64_C2(0xab70fe17, 0xc79ac6ca), UINT64_C2(0xff77b1fc, 0xbebcdc4f), UINT64_C2(0xbe5691ef, 0x416bd60c),
        UINT64_C2(0x8dd01fad, 0x907ffc3c), UINT64_C2(0xd3515c28, 0x31559a83), UINT64_C2(0x9d71ac8f, 0xada6c9b5),
        UINT64_C2(0xea9c2277, 0x23ee8bcb), UINT64_C2(0xaecc4991, 0x4078536d), UINT64_C2(0x823c1279, 0x5db6ce57),
        UINT64_C2(0xc2109436, 0x4dfb5637), UINT64_C2(0x9096ea6f, 0x3848984f), UINT64_C2(0xd77485cb, 0x25823ac7),
        UINT64_C2(0xa086cfcd, 0x97bf97f4), UINT64_C2(0xef340a98, 0x172aace5), UINT64_C2(0xb23867fb, 0x2a35b28e),
        UINT64_C2(0x84c8d4df, 0xd2c63f3b), UINT64_C2(0xc5dd4427, 0x1ad3cdba), UINT64_C2(0x936b9fce, 0xbb25c996),
        UINT64_C2(0xdbac6c24, 0x7d62a584), UINT64_C2(0xa3ab6658, 0x0d5fdaf6), UINT64_C2(0xf3e2f893, 0xdec3f126),
        UINT64_C2(0xb5b5ada8, 0xaaff80b8), UINT64_C2(0x87625f05, 0x6c7c4a8b), UINT64_C2(0xc9bcff60, 0x34c13053),
        UINT64_C2(0x964e858c, 0x91ba2655), UINT64_C2(0xdff97724, 0x70297ebd), UINT64_C2(0xa6dfbd9f, 0xb8e5b88f),
        UINT64_C2(0xf8a95fcf, 0x88747d94), UINT64_C2(0xb9447093, 0x8fa89bcf), UINT64_C2(0x8a08f0f8, 0xbf0f156b),
        UINT64_C2(0xcdb02555, 0x653131b6), UINT64_C2(0x993fe2c6, 0xd07b7fac), UINT64_C2(0xe45c10c4, 0x2a2b3b06),
        UINT64_C2(0xaa242499, 0x697392d3), UINT64_C2(0xfd87b5f2, 0x8300ca0e), UINT64_C2(0xbce50864, 0x92111aeb),
        UINT64_C2(0x8cbccc09, 0x6f5088cc), UINT64_C2(0xd1b71758, 0xe219652c), UINT64_C2(0x9c400000, 0x00000000),
        UINT64_C2(0xe8d4a510, 0x00000000), UINT64_C2(0xad78ebc5, 0xac620000), UINT64_C2(0x813f3978, 0xf8940984),
        UINT64_C2(0xc097ce7b, 0xc90715b3), UINT64_C2(0x8f7e32ce, 0x7bea5c70), UINT64_C2(0xd5d238a4, 0xabe98068),
        UINT64_C2(0x9f4f2726, 0x179a2245), UINT64_C2(0xed63a231, 0xd4c4fb27), UINT64_C2(0xb0de6538, 0x8cc8ada8),
        UINT64_C2(0x83c7088e, 0x1aab65db), UINT64_C2(0xc45d1df9, 0x42711d9a), UINT64_C2(0x924d692c, 0xa61be758),
        UINT64_C2(0xda01ee64, 0x1a708dea), UINT64_C2(0xa26da399, 0x9aef774a), UINT64_C2(0xf209787b, 0xb47d6b85),
        UINT64_C2(0xb454e4a1, 0x79dd1877), UINT64_C2(0x865b8692, 0x5b9bc5c2), UINT64_C2(0xc83553c5, 0xc8965d3d),
        UINT64_C2(0x952ab45c, 0xfa97a0b3), UINT64_C2(0xde469fbd, 0x99a05fe3), UINT64_C2(0xa59bc234, 0xdb398c25),
        UINT64_C2(0xf6c69a72, 0xa3989f5c), UINT64_C2(0xb7dcbf53, 0x54e9bece), UINT64_C2(0x88fcf317, 0xf22241e2),
        UINT64_C2(0xcc20ce9b, 0xd35c78a5), UINT64_C2(0x98165af3, 0x7b2153df), UINT64_C2(0xe2a0b5dc, 0x971f303a),
        UINT64_C2(0xa8d9d153, 0x5ce3b396), UINT64_C2(0xfb9b7cd9, 0xa4a7443c), UINT64_C2(0xbb764c4c, 0xa7a44410),
        UINT64_C2(0x8bab8eef, 0xb6409c1a), UINT64_C2(0xd01fef10, 0xa657842c), UINT64_C2(0x9b10a4e5, 0xe9913129),
        UINT64_C2(0xe7109bfb, 0xa19c0c9d), UINT64_C2(0xac2820d9, 0x623bf429), UINT64_C2(0x80444b5e, 0x7aa7cf85),
        UINT64_C2(0xbf21e440, 0x03acdd2d), UINT64_C2(0x8e679c2f, 0x5e44ff8f), UINT64_C2(0xd433179d, 0x9c8cb841),
        UINT64_C2(0x9e19db92, 0xb4e31ba9), UINT64_C2(0xeb96bf6e, 0xbadf77d9), UINT64_C2(0xaf87023b, 0x9bf0ee6b)};
    static const int16_t kCachedPowers_E[] = {
        -1220, -1193, -1166, -1140, -1113, -1087, -1060, -1034, -1007, -980, -954, -927, -901, -874, -847,
        -821,  -794,  -768,  -741,  -715,  -688,  -661,  -635,  -608,  -582, -555, -529, -502, -475, -449,
        -422,  -396,  -369,  -343,  -316,  -289,  -263,  -236,  -210,  -183, -157, -130, -103, -77,  -50,
        -24,   3,     30,    56,    83,    109,   136,   162,   189,   216,  242,  269,  295,  322,  348,
        375,   402,   428,   455,   481,   508,   534,   561,   588,   614,  641,  667,  694,  720,  747,
        774,   800,   827,   853,   880,   907,   933,   960,   986,   1013, 1039, 1066};
    return DtoaHelper::DiyFp(kCachedPowers_F[index], kCachedPowers_E[index]);
}

void DtoaHelper::GrisuRound(char *buffer, int len, uint64_t delta, uint64_t rest, uint64_t tenKappa, uint64_t distance)
{
    while (rest < distance && delta - rest >= tenKappa &&
        (rest + tenKappa < distance || distance - rest > rest + tenKappa - distance)) {
        buffer[len - 1]--;
        rest += tenKappa;
    }
}

int DtoaHelper::CountDecimalDigit32(uint32_t n)
{
    if (n < TEN) {
        return 1; // 1: means the decimal digit
    } else if (n < TEN2POW) {
        return 2; // 2: means the decimal digit
    } else if (n < TEN3POW) {
        return 3; // 3: means the decimal digit
    } else if (n < TEN4POW) {
        return 4; // 4: means the decimal digit
    } else if (n < TEN5POW) {
        return 5; // 5: means the decimal digit
    } else if (n < TEN6POW) {
        return 6; // 6: means the decimal digit
    } else if (n < TEN7POW) {
        return 7; // 7: means the decimal digit
    } else if (n < TEN8POW) {
        return 8; // 8: means the decimal digit
    } else {
        return 9; // 9: means the decimal digit
    }
}

void DtoaHelper::DigitGen(const DiyFp &W, const DiyFp &Mp, uint64_t delta, char *buffer, int *len, int *K)
{
    const DiyFp one(uint64_t(1) << -Mp.e, Mp.e);
    const DiyFp distance = Mp - W;
    uint32_t p1 = static_cast<uint32_t>(Mp.f >> -one.e);
    ASSERT(one.f > 0);
    uint64_t p2 = Mp.f & (one.f - 1);
    int kappa = CountDecimalDigit32(p1); // kappa in [0, 9]
    *len = 0;
    while (kappa > 0) {
        uint32_t d = 0;
        switch (kappa) {
            case 9: // 9: means the decimal digit
                d = p1 / TEN8POW;
                p1 %= TEN8POW;
                break;
            case 8: // 8: means the decimal digit
                d = p1 / TEN7POW;
                p1 %= TEN7POW;
                break;
            case 7: // 7: means the decimal digit
                d = p1 / TEN6POW;
                p1 %= TEN6POW;
                break;
            case 6: // 6: means the decimal digit
                d = p1 / TEN5POW;
                p1 %= TEN5POW;
                break;
            case 5: // 5: means the decimal digit
                d = p1 / TEN4POW;
                p1 %= TEN4POW;
                break;
            case 4: // 4: means the decimal digit
                d = p1 / TEN3POW;
                p1 %= TEN3POW;
                break;
            case 3: // 3: means the decimal digit
                d = p1 / TEN2POW;
                p1 %= TEN2POW;
                break;
            case 2: // 2: means the decimal digit
                d = p1 / TEN;
                p1 %= TEN;
                break;
            case 1: // 1: means the decimal digit
                d = p1;
                p1 = 0;
                break;
            default:;
        }
        if (d || *len) {
            buffer[(*len)++] = static_cast<char>('0' + static_cast<char>(d));
        }
        kappa--;
        uint64_t tmp = (static_cast<uint64_t>(p1) << -one.e) + p2;
        if (tmp <= delta) {
            *K += kappa;
            GrisuRound(buffer, *len, delta, tmp, POW10[kappa] << -one.e, distance.f);
            return;
        }
    }

    // kappa = 0
    for (;;) {
        p2 *= TEN;
        delta *= TEN;
        char d = static_cast<char>(p2 >> -one.e);
        if (d || *len) {
            buffer[(*len)++] = static_cast<char>('0' + d);
        }
        ASSERT(one.f > 0);
        p2 &= one.f - 1;
        kappa--;
        if (p2 < delta) {
            *K += kappa;
            int index = -kappa;
            GrisuRound(buffer, *len, delta, p2, one.f, distance.f * (index < kIndex ? POW10[index] : 0));
            return;
        }
    }
}

// Grisu2 algorithm use the extra capacity of the used integer type to shorten the produced output
void DtoaHelper::Grisu(double value, char *buffer, int *length, int *K)
{
    const DiyFp v(value);
    DiyFp mMinus;
    DiyFp mPlus;
    v.NormalizedBoundaries(&mMinus, &mPlus);

    const DiyFp cached = GetCachedPower(mPlus.e, K);
    const DiyFp W = v.Normalize() * cached;
    DiyFp wPlus = mPlus * cached;
    DiyFp wMinus = mMinus * cached;
    wMinus.f++;
    wPlus.f--;
    DigitGen(W, wPlus, wPlus.f - wMinus.f, buffer, length, K);
}

void DtoaHelper::Dtoa(double value, char *buffer, int *point, int *length)
{
    // Exceptional case such as NAN, 0.0, negative... are processed in DoubleToEcmaString
    // So use Dtoa should avoid Exceptional case.
    ASSERT(value > 0);
    int k;
    Grisu(value, buffer, length, &k);
    *point = *length + k;
}

void DtoaHelper::FillDigits32FixedLength(uint32_t number, int requested_length,
                                         BufferVector<char> buffer, int* length)
{
    for (int i = requested_length - 1; i >= 0; --i) {
        buffer[(*length) + i] = '0' + number % TEN;
        number /= TEN;
    }
    *length += requested_length;
}

void DtoaHelper::FillDigits32(uint32_t number, BufferVector<char> buffer, int* length)
{
    int number_length = 0;
    // We fill the digits in reverse order and exchange them afterwards.
    while (number != 0) {
        int digit = static_cast<int>(number % TEN);
        number /= TEN;
        buffer[(*length) + number_length] = '0' + digit;
        number_length++;
    }
    // Exchange the digits.
    int i = *length;
    int j = *length + number_length - 1;
    while (i < j) {
        char tmp = buffer[i];
        buffer[i] = buffer[j];
        buffer[j] = tmp;
        i++;
        j--;
    }
    *length += number_length;
}

void DtoaHelper::FillDigits64FixedLength(uint64_t number, [[maybe_unused]] int requested_length,
                                         BufferVector<char> buffer, int* length)
{
    // For efficiency cut the number into 3 uint32_t parts, and print those.
    uint32_t part2 = static_cast<uint32_t>(number % TEN7POW);
    number /= TEN7POW;
    uint32_t part1 = static_cast<uint32_t>(number % TEN7POW);
    uint32_t part0 = static_cast<uint32_t>(number / TEN7POW);
    FillDigits32FixedLength(part0, 3, buffer, length); // 3: parameter
    FillDigits32FixedLength(part1, 7, buffer, length); // 7: parameter
    FillDigits32FixedLength(part2, 7, buffer, length); // 7: parameter
}

void DtoaHelper::FillDigits64(uint64_t number, BufferVector<char> buffer, int* length)
{
    // For efficiency cut the number into 3 uint32_t parts, and print those.
    uint32_t part2 = static_cast<uint32_t>(number % TEN7POW);
    number /= TEN7POW;
    uint32_t part1 = static_cast<uint32_t>(number % TEN7POW);
    uint32_t part0 = static_cast<uint32_t>(number / TEN7POW);
    if (part0 != 0) {
        FillDigits32(part0, buffer, length);
        FillDigits32FixedLength(part1, 7, buffer, length); // 7: means the decimal digit
        FillDigits32FixedLength(part2, 7, buffer, length); // 7: means the decimal digit
    } else if (part1 != 0) {
        FillDigits32(part1, buffer, length);
        FillDigits32FixedLength(part2, 7, buffer, length); // 7: means the decimal digit
    } else {
        FillDigits32(part2, buffer, length);
    }
}

void DtoaHelper::RoundUp(BufferVector<char> buffer, int* length, int* decimal_point)
{
    // An empty buffer represents 0.
    if (*length == 0) {
        buffer[0] = '1';
        *decimal_point = 1;
        *length = 1;
        return;
    }
    buffer[(*length) - 1]++;
    for (int i = (*length) - 1; i > 0; --i) {
        if (buffer[i] != '0' + 10) { // 10: means the decimal digit
            return;
        }
        buffer[i] = '0';
        buffer[i - 1]++;
    }
    if (buffer[0] == '0' + 10) { // 10: means the decimal digit
        buffer[0] = '1';
        (*decimal_point)++;
    }
}

void DtoaHelper::FillFractionals(uint64_t fractionals, int exponent, int fractional_count,
                                 BufferVector<char> buffer, int* length, int* decimal_point)
{
    ASSERT(NEGATIVE_128BIT <= exponent && exponent <= 0);
    // 'fractionals' is a fixed-point number, with binary point at bit
    // (-exponent). Inside the function the non-converted remainder of fractionals
    // is a fixed-point number, with binary point at bit 'point'.
    if (-exponent <= EXPONENT_64) {
        // One 64 bit number is sufficient.
        ASSERT((fractionals >> 56) == 0); // 56: parameter
        int point = -exponent;
        for (int i = 0; i < fractional_count; ++i) {
            if (fractionals == 0) break;
            fractionals *= 5; // 5: parameter
            point--;
            int digit = static_cast<int>(fractionals >> point);
            buffer[*length] = '0' + digit;
            (*length)++;
            fractionals -= static_cast<uint64_t>(digit) << point;
        }
        // If the first bit after the point is set we have to round up.
        if (((fractionals >> (point - 1)) & 1) == 1) {
            RoundUp(buffer, length, decimal_point);
        }
    } else {  // We need 128 bits.
        ASSERT(EXPONENT_64 < -exponent && -exponent <= EXPONENT_128);
        UInt128 fractionals128 = UInt128(fractionals, 0);
        fractionals128.Shift(-exponent - EXPONENT_64);
        int point = 128;
        for (int i = 0; i < fractional_count; ++i) {
            if (fractionals128.IsZero()) break;
            // As before: instead of multiplying by 10 we multiply by 5 and adjust the
            // point location.
            // This multiplication will not overflow for the same reasons as before.
            fractionals128.Multiply(5); // 5: parameter
            point--;
            int digit = fractionals128.DivModPowerOf2(point);
            buffer[*length] = '0' + digit;
            (*length)++;
        }
        if (fractionals128.BitAt(point - 1) == 1) {
            RoundUp(buffer, length, decimal_point);
        }
    }
}

// Removes leading and trailing zeros.
// If leading zeros are removed then the decimal point position is adjusted.
void DtoaHelper::TrimZeros(BufferVector<char> buffer, int* length, int* decimal_point)
{
    while (*length > 0 && buffer[(*length) - 1] == '0') {
        (*length)--;
    }
    int first_non_zero = 0;
    while (first_non_zero < *length && buffer[first_non_zero] == '0') {
        first_non_zero++;
    }
    if (first_non_zero != 0) {
        for (int i = first_non_zero; i < *length; ++i) {
            buffer[i - first_non_zero] = buffer[i];
        }
        *length -= first_non_zero;
        *decimal_point -= first_non_zero;
    }
}

bool DtoaHelper::FixedDtoa(double v, int fractional_count, BufferVector<char> buffer,
                           int* length, int* decimal_point)
{
    if (v == 0) {
        buffer[0] = '0';
        buffer[1] = '\0';
        *length = 1;
        *decimal_point = 1;
        return true;
    }
    uint64_t significand = NumberHelper::Significand(v);
    int exponent = NumberHelper::Exponent(v);
    if (exponent > 20) return false; // 20: max parameter
    if (fractional_count > 20) return false; // 20: max parameter
    *length = 0;
    if (exponent + kDoubleSignificandSize > EXPONENT_64) {
        const uint64_t kFive17 = 0xB1'A2BC'2EC5;  // 5^17
        uint64_t divisor = kFive17;
        int divisor_power = 17;
        uint64_t dividend = significand;
        uint32_t quotient;
        uint64_t remainder;
        if (exponent > divisor_power) {
            // We only allow exponents of up to 20 and therefore (17 - e) <= 3
            dividend <<= exponent - divisor_power;
            quotient = static_cast<uint32_t>(dividend / divisor);
            remainder = (dividend % divisor) << divisor_power;
        } else {
            divisor <<= divisor_power - exponent;
            quotient = static_cast<uint32_t>(dividend / divisor);
            remainder = (dividend % divisor) << exponent;
        }
        FillDigits32(quotient, buffer, length);
        FillDigits64FixedLength(remainder, divisor_power, buffer, length);
        *decimal_point = *length;
    } else if (exponent >= 0) {
        // 0 <= exponent <= 11
        significand <<= exponent;
        FillDigits64(significand, buffer, length);
        *decimal_point = *length;
    } else if (exponent > -kDoubleSignificandSize) {
        // We have to cut the number.
        uint64_t integrals = significand >> -exponent;
        uint64_t fractionals = significand - (integrals << -exponent);
        if (integrals > kMaxUInt32) {
            FillDigits64(integrals, buffer, length);
        } else {
            FillDigits32(static_cast<uint32_t>(integrals), buffer, length);
        }
        *decimal_point = *length;
        FillFractionals(fractionals, exponent, fractional_count,
                        buffer, length, decimal_point);
    } else if (exponent < NEGATIVE_128BIT) {
        ASSERT(fractional_count <= 20); // 20: parameter
        buffer[0] = '\0';
        *length = 0;
        *decimal_point = -fractional_count;
    } else {
        *decimal_point = 0;
        FillFractionals(significand, exponent, fractional_count,
                        buffer, length, decimal_point);
    }
    TrimZeros(buffer, length, decimal_point);
    buffer[*length] = '\0';
    if ((*length) == 0) {
        *decimal_point = -fractional_count;
    }
    return true;
}
}