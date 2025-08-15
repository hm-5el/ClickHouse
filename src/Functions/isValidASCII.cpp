#include <DataTypes/DataTypeString.h>
#include <Functions/FunctionFactory.h>
#include <Functions/FunctionStringOrArrayToT.h>

#include <base/simd.h>
#include <base/types.h>

#include <algorithm>

#if defined(__AVX512F__) || defined(__AVX__) && defined(__AVX2__)
#    include <immintrin.h>
#elif defined(__SSE2__)
#    include <emmintrin.h>
#elif defined(__aarch64__) && defined(__ARM_NEON)
#    include <arm_neon.h>
#    pragma clang diagnostic ignored "-Wreserved-identifier"
#endif

namespace DB
{

namespace ErrorCodes
{
extern const int ILLEGAL_TYPE_OF_ARGUMENT;
}

/// inspired by https://github.com/cyb70289/utf8/
/*
MIT License

Copyright (c) 2019 Yibo Cai

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

namespace
{
/*
* Checks if a data buffer contains only valid ASCII characters by:
* - Accumulate ranges of the data into 2 SIMD registers using bitwise OR.
* - Accumulate the 2 registers into one, check if any byte has a set 7th bit, and 
*   return 0 if this is the case.
* - Advance the data pointer and update the length.
*/
#if defined(__AVX512F__)
UInt8 isValidASCIIWithSIMD(const UInt8 *& data, UInt64 & len)
{
    if (len >= 128)
    {
        __m512i first_mask = _mm512_setzero_si512();
        __m512i second_mask = _mm512_setzero_si512();

        while (len >= 128)
        {
            __m512i first_input = _mm512_loadu_si512(reinterpret_cast<const __m512i *>(data));
            __m512i second_input = _mm512_loadu_si512(reinterpret_cast<const __m512i *>(data + 64));

            first_mask = _mm512_or_si512(first_mask, first_input);
            second_mask = _mm512_or_si512(second_mask, second_input);

            data += 128;
            len -= 128;
        }

        first_mask = _mm512_or_si512(first_mask, second_mask);
        if (_mm512_cmplt_epi8_mask(first_mask, _mm512_set1_epi8(0)))
        {
            return 0;
        }
    }

    return 1;
}

#elif defined(__AVX__) && defined(__AVX2__)
UInt8 isValidASCIIWithSIMD(const UInt8 *& data, UInt64 & len)
{
    if (len >= 64)
    {
        __m256i first_mask = _mm256_setzero_si256();
        __m256i second_mask = _mm256_setzero_si256();

        while (len >= 64)
        {
            __m256i first_input = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(data));
            __m256i second_input = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(data + 32));

            first_mask = _mm256_or_si256(first_mask, first_input);
            second_mask = _mm256_or_si256(second_mask, second_input);

            data += 64;
            len -= 64;
        }

        first_mask = _mm256_or_si256(first_mask, second_mask);
        if (_mm256_movemask_epi8(_mm256_cmpgt_epi8(_mm256_set1_epi8(0), first_mask)))
        {
            return 0;
        }
    }

    return 1;
}

#elif defined(__SSE2__)
UInt8 isValidASCIIWithSIMD(const UInt8 *& data, UInt64 & len)
{
    if (len >= 32)
    {
        __m128i first_mask = _mm_set1_epi8(0);
        __m128i second_mask = _mm_set1_epi8(0);

        while (len >= 32)
        {
            __m128i first_input = _mm_loadu_si128(reinterpret_cast<const __m128i *>(data));
            __m128i second_input = _mm_loadu_si128(reinterpret_cast<const __m128i *>(data + 16));

            first_mask = _mm_or_si128(first_mask, first_input);
            second_mask = _mm_or_si128(second_mask, second_input);

            data += 32;
            len -= 32;
        }

        first_mask = _mm_or_si128(first_mask, second_mask);
        if (_mm_movemask_epi8(_mm_cmplt_epi8(first_mask, _mm_set1_epi8(0))))
        {
            return 0;
        }
    }

    return 1;
}

#elif defined(__aarch64__) && defined(__ARM_NEON)
UInt8 isValidASCIIWithSIMD(const UInt8 *& data, UInt64 & len)
{
    if (len >= 32)
    {
        uint8x16_t first_mask = vdupq_n_u8(0);
        uint8x16_t second_mask = vdupq_n_u8(0);

        while (len >= 32)
        {
            const uint8x16_t first_input = vld1q_u8(reinterpret_cast<const uint8_t *>(data));
            const uint8x16_t second_input = vld1q_u8(reinterpret_cast<const uint8_t *>(data + 16));

            first_mask = vorrq_u8(first_mask, first_input);
            second_mask = vorrq_u8(second_mask, second_input);

            data += 32;
            len -= 32;
        }

        first_mask = vorrq_u8(first_mask, second_mask);
        if (vmaxvq_u8(first_mask) >= 0x80)
        {
            return 0;
        }
    }

    return 1;
}
#endif

UInt8 isValidASCIIWithoutSIMD(const UInt8 * data, UInt64 len)
{
    UInt8 all_mask = 0;

    if (len >= 16)
    {
        UInt64 first_mask = 0;
        UInt64 second_mask = 0;

        do
        {
            UInt64 first_input;
            std::memcpy(&first_input, data, sizeof(first_input));
            first_mask |= first_input;

            UInt64 second_input;
            std::memcpy(&second_input, data + 8, sizeof(second_input));
            second_mask |= second_input;

            data += 16;
            len -= 16;
        } while (len >= 16);

        // if any byte has a set high bit, the result will be !(non zero) - 1 = 0 - 1 = 0xFF.
        // if all byte have a clear high bit, the result will be !(zero) - 1 = 1 - 1 = 0x00.
        all_mask = !((first_mask | second_mask) & 0x8080808080808080ULL) - 1;
    }

    // iterate through remaining bytes.
    std::for_each(data, data + len, [&](UInt8 byte) { all_mask |= byte; });

    return all_mask < 0x80;
}
}

struct ValidASCIIImpl
{
    static UInt8 isValidASCII(const UInt8 * data, UInt64 len)
    {
#if defined(__AVX512F__) || defined(__AVX__) && defined(__AVX2__) || defined(__SSE2__) || defined(__aarch64__) && defined(__ARM_NEON)
        // advances the data pointer and updates the length.
        if (!isValidASCIIWithSIMD(data, len))
        {
            return 0;
        }
#endif
        return isValidASCIIWithoutSIMD(data, len);
    }

    static constexpr bool is_fixed_to_constant = false;

    static void
    vector(const ColumnString::Chars & data, const ColumnString::Offsets & offsets, PaddedPODArray<UInt8> & res, size_t input_rows_count)
    {
        size_t prev_offset = 0;
        for (size_t i = 0; i < input_rows_count; ++i)
        {
            res[i] = isValidASCII(data.data() + prev_offset, offsets[i] - 1 - prev_offset);
            prev_offset = offsets[i];
        }
    }

    [[noreturn]] static void vectorFixedToConstant(const ColumnString::Chars &, size_t, UInt8 &, size_t)
    {
        throw Exception(ErrorCodes::NOT_IMPLEMENTED, "vectorFixedToConstant not implemented for function isValidASCII");
    }

    static void vectorFixedToVector(const ColumnString::Chars & data, size_t n, PaddedPODArray<UInt8> & res, size_t input_rows_count)
    {
        for (size_t i = 0; i < input_rows_count; ++i)
            res[i] = isValidASCII(data.data() + i * n, n);
    }

    [[noreturn]] static void array(const ColumnString::Offsets &, PaddedPODArray<UInt8> &, size_t)
    {
        throw Exception(ErrorCodes::ILLEGAL_TYPE_OF_ARGUMENT, "Cannot apply function isValidASCII to Array argument");
    }

    [[noreturn]] static void uuid(const ColumnUUID::Container &, size_t &, PaddedPODArray<UInt8> &, size_t)
    {
        throw Exception(ErrorCodes::ILLEGAL_TYPE_OF_ARGUMENT, "Cannot apply function isValidASCII to UUID argument");
    }

    [[noreturn]] static void ipv6(const ColumnIPv6::Container &, size_t &, PaddedPODArray<UInt8> &, size_t)
    {
        throw Exception(ErrorCodes::ILLEGAL_TYPE_OF_ARGUMENT, "Cannot apply function isValidASCII to IPv6 argument");
    }

    [[noreturn]] static void ipv4(const ColumnIPv4::Container &, size_t &, PaddedPODArray<UInt8> &, size_t)
    {
        throw Exception(ErrorCodes::ILLEGAL_TYPE_OF_ARGUMENT, "Cannot apply function isValidASCII to IPv4 argument");
    }
};

struct NameIsValidASCII
{
    static constexpr auto name = "isValidASCII";
};
using FunctionValidASCII = FunctionStringOrArrayToT<ValidASCIIImpl, NameIsValidASCII, UInt8>;

REGISTER_FUNCTION(IsValidASCII)
{
    factory.registerFunction<FunctionValidASCII>();
    factory.registerAlias("isASCII", "isValidASCII", FunctionFactory::Case::Sensitive);
}

}
