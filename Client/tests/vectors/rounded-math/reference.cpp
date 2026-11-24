// The values Client/tests/render/test_rounded_math.cpp expects, from MPFR: README.md.
#include <mpfr.h>

#include <bit>
#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <random>
#include <vector>

using MpfrFunction = int (*)(mpfr_ptr, mpfr_srcptr, mpfr_rnd_t);

constexpr std::size_t Count = 100000;
constexpr std::uint64_t Seed = 20261121;

struct Exponents
{
    int lowest;
    int highest;
};

float NextFloat(std::mt19937_64& engine, Exponents exponents)
{
    const std::uint64_t bits = engine();
    const auto span = static_cast<std::uint64_t>(exponents.highest - exponents.lowest + 1);
    const auto exponent =
        static_cast<std::uint32_t>(static_cast<std::uint64_t>(exponents.lowest + 127) + (bits >> 32) % span);
    return std::bit_cast<float>((static_cast<std::uint32_t>(bits) & 0x807fffffu) | (exponent << 23));
}

double NextDouble(std::mt19937_64& engine, Exponents exponents)
{
    const std::uint64_t bits = engine();
    const auto span = static_cast<std::uint64_t>(exponents.highest - exponents.lowest + 1);
    const std::uint64_t exponent = static_cast<std::uint64_t>(exponents.lowest + 1023) + ((bits >> 52) & 0x7ffu) % span;
    return std::bit_cast<double>((bits & 0x800fffffffffffffu) | (exponent << 52));
}

float Float(MpfrFunction function, float x)
{
    mpfr_t in, out;
    mpfr_init2(in, 24);
    mpfr_init2(out, 24);
    mpfr_set_flt(in, x, MPFR_RNDN);
    function(out, in, MPFR_RNDN);
    const float result = mpfr_get_flt(out, MPFR_RNDN);
    mpfr_clear(in);
    mpfr_clear(out);
    return result;
}

double Double(MpfrFunction function, double x)
{
    mpfr_t in, out;
    mpfr_init2(in, 53);
    mpfr_init2(out, 53);
    mpfr_set_d(in, x, MPFR_RNDN);
    function(out, in, MPFR_RNDN);
    const double result = mpfr_get_d(out, MPFR_RNDN);
    mpfr_clear(in);
    mpfr_clear(out);
    return result;
}

std::uint64_t Fnv(const unsigned char* bytes, std::size_t size)
{
    std::uint64_t hash = 0xcbf29ce484222325;
    for (std::size_t i = 0; i < size; ++i)
        hash = (hash ^ bytes[i]) * 0x100000001b3;
    return hash;
}

std::uint64_t FloatHash(MpfrFunction function, Exponents common, Exponents whole)
{
    std::mt19937_64 engine(Seed);
    std::vector<std::uint32_t> results;
    for (const Exponents exponents : {common, whole})
        for (std::size_t i = 0; i < Count; ++i)
            results.push_back(std::bit_cast<std::uint32_t>(Float(function, NextFloat(engine, exponents))));
    return Fnv(reinterpret_cast<const unsigned char*>(results.data()), results.size() * 4);
}

std::uint64_t DoubleHash(MpfrFunction function, Exponents common, Exponents whole, bool positive = false)
{
    std::mt19937_64 engine(Seed);
    std::vector<std::uint64_t> results;
    for (const Exponents exponents : {common, whole})
        for (std::size_t i = 0; i < Count; ++i)
        {
            double x = NextDouble(engine, exponents);
            if (positive)
                x = std::bit_cast<double>(std::bit_cast<std::uint64_t>(x) & 0x7fffffffffffffffu);
            results.push_back(std::bit_cast<std::uint64_t>(Double(function, x)));
        }
    return Fnv(reinterpret_cast<const unsigned char*>(results.data()), results.size() * 8);
}

int main()
{
    std::printf("sinf(1)   %08" PRIx32 "\n", std::bit_cast<std::uint32_t>(Float(mpfr_sin, 1.0f)));
    std::printf("cosf(1)   %08" PRIx32 "\n", std::bit_cast<std::uint32_t>(Float(mpfr_cos, 1.0f)));
    std::printf("tanf(1)   %08" PRIx32 "\n", std::bit_cast<std::uint32_t>(Float(mpfr_tan, 1.0f)));
    std::printf("acosf(.5) %08" PRIx32 "\n", std::bit_cast<std::uint32_t>(Float(mpfr_acos, 0.5f)));
    std::printf("sin(1)    %016" PRIx64 "\n", std::bit_cast<std::uint64_t>(Double(mpfr_sin, 1.0)));
    std::printf("cos(1)    %016" PRIx64 "\n", std::bit_cast<std::uint64_t>(Double(mpfr_cos, 1.0)));
    std::printf("tan(1)    %016" PRIx64 "\n", std::bit_cast<std::uint64_t>(Double(mpfr_tan, 1.0)));
    std::printf("acos(.5)  %016" PRIx64 "\n", std::bit_cast<std::uint64_t>(Double(mpfr_acos, 0.5)));
    std::printf("log10(2)  %016" PRIx64 "\n", std::bit_cast<std::uint64_t>(Double(mpfr_log10, 2.0)));
    std::printf("sinf   %016" PRIx64 "\n", FloatHash(mpfr_sin, {-8, 8}, {-126, 127}));
    std::printf("cosf   %016" PRIx64 "\n", FloatHash(mpfr_cos, {-8, 8}, {-126, 127}));
    std::printf("tanf   %016" PRIx64 "\n", FloatHash(mpfr_tan, {-8, 8}, {-126, 127}));
    std::printf("acosf  %016" PRIx64 "\n", FloatHash(mpfr_acos, {-8, -1}, {-126, -1}));
    std::printf("sin    %016" PRIx64 "\n", DoubleHash(mpfr_sin, {-8, 8}, {-1022, 1023}));
    std::printf("cos    %016" PRIx64 "\n", DoubleHash(mpfr_cos, {-8, 8}, {-1022, 1023}));
    std::printf("tan    %016" PRIx64 "\n", DoubleHash(mpfr_tan, {-8, 8}, {-1022, 1023}));
    std::printf("acos   %016" PRIx64 "\n", DoubleHash(mpfr_acos, {-8, -1}, {-1022, -1}));
    std::printf("log10  %016" PRIx64 "\n", DoubleHash(mpfr_log10, {-8, 8}, {-1022, 1023}, true));
    return 0;
}
