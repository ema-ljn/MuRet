// Deterministic input sets. No std distributions: their output is implementation-defined.
#pragma once
#include <bit>
#include <cstdint>
#include <vector>

struct SplitMix64
{
    std::uint64_t state;
    std::uint64_t Next()
    {
        std::uint64_t z = (state += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    // Uniform in [0, 1) with 53 bits; exact operations only.
    double Unit() { return static_cast<double>(Next() >> 11) * 0x1.0p-53; }
    float UnitF() { return static_cast<float>(Next() >> 40) * 0x1.0p-24f; }
};

// pi/180 as the float the original keeps at 0x6ba360.
inline constexpr float DegreesF = std::bit_cast<float>(0x3C8EFA35u);

// "grados": float degrees times the float pi/180, as OriginalAngleMatrix does.
inline std::vector<double> DomainDegrees(std::size_t n)
{
    SplitMix64 rng{0x1001};
    std::vector<double> xs;
    xs.reserve(n);
    for (std::size_t i = 0; i < n; ++i)
    {
        float degrees;
        if (i % 2 == 0)
        {
            degrees = static_cast<float>(static_cast<std::int64_t>(rng.Next() % 5761) - 2880) * 0.25f;
        }
        else
        {
            degrees = rng.UnitF() * 720.0f - 360.0f;
        }
        xs.push_back(static_cast<double>(degrees) * static_cast<double>(DegreesF));
    }
    return xs;
}

// "radianes": doubles spread over [-2pi, 2pi).
inline std::vector<double> DomainRadians(std::size_t n)
{
    SplitMix64 rng{0x2002};
    std::vector<double> xs;
    xs.reserve(n);
    for (std::size_t i = 0; i < n; ++i)
    {
        xs.push_back(rng.Unit() * 12.566370614359172 - 6.283185307179586);
    }
    return xs;
}

// "reloj": a float world clock in milliseconds times a float rate, as the glow pulses do.
inline std::vector<double> DomainClock(std::size_t n)
{
    SplitMix64 rng{0x3003};
    static constexpr float Rates[] = {0.001f, 0.002f, 0.02f, 0.0005f};
    std::vector<double> xs;
    xs.reserve(n);
    for (std::size_t i = 0; i < n; ++i)
    {
        const auto clock = static_cast<float>(rng.Next() % (1u << 27));
        xs.push_back(static_cast<double>(clock) * static_cast<double>(Rates[i % 4]));
    }
    return xs;
}

// "cosenos": floats in [-1, 1], half of them close to +-1, as the slerp feeds acos.
inline std::vector<double> DomainCosines(std::size_t n)
{
    SplitMix64 rng{0x4004};
    std::vector<double> xs;
    xs.reserve(n);
    for (std::size_t i = 0; i < n; ++i)
    {
        float c;
        if (i % 2 == 0)
        {
            c = rng.UnitF() * 2.0f - 1.0f;
        }
        else
        {
            // Two statements: the order of two calls inside one expression is unspecified, and MSVC and GCC differ.
            const float unit = rng.UnitF();
            const float scale = std::bit_cast<float>(0x3F800000u - ((static_cast<std::uint32_t>(rng.Next() % 20) + 1u) << 23));
            const float gap = unit * scale;
            c = (i % 4 == 1) ? 1.0f - gap : gap - 1.0f;
        }
        xs.push_back(static_cast<double>(c));
    }
    return xs;
}

// "positivos": ratios for log10, in (0.001, 1000).
inline std::vector<double> DomainPositive(std::size_t n)
{
    SplitMix64 rng{0x5005};
    std::vector<double> xs;
    xs.reserve(n);
    for (std::size_t i = 0; i < n; ++i)
    {
        const float mantissa = 1.0f + rng.UnitF();
        const int exponent = static_cast<int>(rng.Next() % 20) - 10;
        xs.push_back(static_cast<double>(std::bit_cast<float>(std::bit_cast<std::uint32_t>(mantissa) + (static_cast<std::uint32_t>(exponent) << 23))));
    }
    return xs;
}

// The probe of PR #415 (commit e69ac7b): 400,001 steps.
inline std::vector<double> DomainProbe415Angles()
{
    std::vector<double> xs;
    for (int step = -200000; step <= 200000; ++step)
    {
        xs.push_back(static_cast<double>(step) * 0.0000314159);
    }
    return xs;
}
inline std::vector<double> DomainProbe415Cosines()
{
    std::vector<double> xs;
    for (int step = -200000; step <= 200000; ++step)
    {
        xs.push_back(static_cast<double>(step) / 200000.0);
    }
    return xs;
}

// Float inputs: half angles in radians, as glm::quat(vec3) feeds sinf and cosf.
inline std::vector<float> DomainHalfAnglesF(std::size_t n)
{
    SplitMix64 rng{0x6006};
    std::vector<float> xs;
    xs.reserve(n);
    for (std::size_t i = 0; i < n; ++i)
    {
        xs.push_back((rng.UnitF() * 6.2831855f - 3.1415927f) * 0.5f);
    }
    return xs;
}
inline std::vector<float> DomainRadiansF(std::size_t n)
{
    SplitMix64 rng{0x7007};
    std::vector<float> xs;
    xs.reserve(n);
    for (std::size_t i = 0; i < n; ++i)
    {
        xs.push_back(rng.UnitF() * 12.566371f - 6.2831855f);
    }
    return xs;
}
inline std::vector<float> DomainCosinesF(std::size_t n)
{
    std::vector<float> xs;
    xs.reserve(n);
    for (const double c : DomainCosines(n))
    {
        xs.push_back(static_cast<float>(c));
    }
    return xs;
}
