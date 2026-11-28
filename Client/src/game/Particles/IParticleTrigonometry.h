#pragma once

#include "Core/Expected.h"

#include <cstdint>

namespace Client::Particles
{
enum class ParticleError : std::uint8_t;

// PC53 product of the trigonometric output retained in 64 bits by the exact f32 factor. The argument already comes
// prepared in PC53; the sine/cosine is not rounded to double before the product. A required evaluator, without
// access/mutation/reentry to the world, clock or RNG, nor retained references. Its implementation declares its
// domain and returns an error when it cannot represent it.
class IParticleTrigonometry
{
public:
    virtual ~IParticleTrigonometry();

    [[nodiscard]] virtual Core::Expected<double, ParticleError> SineProductPC53(double argument,
                                                                                float multiplier) const = 0;
    [[nodiscard]] virtual Core::Expected<double, ParticleError> CosineProductPC53(double argument,
                                                                                  float multiplier) const = 0;
};
} // namespace Client::Particles
