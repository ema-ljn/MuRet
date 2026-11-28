#pragma once

#include "Core/Expected.h"
#include "Particles/IParticleTrigonometry.h"

namespace Client::Particles
{
// Limited fixed profile (ADR0014): sin/cos with OriginalRetainedMath's own binary arithmetic (error < 2^-250) →
// retained64 RN → exact PC53 product in double. Accepts finite arguments |a| < 2^63 and finite positive f32 factors.
// Does not credit FSIN/FCOS equality.
class PortableParticleTrigonometry final : public IParticleTrigonometry
{
public:
    [[nodiscard]] Core::Expected<double, ParticleError> SineProductPC53(double argument,
                                                                        float multiplier) const override;
    [[nodiscard]] Core::Expected<double, ParticleError> CosineProductPC53(double argument,
                                                                          float multiplier) const override;
};
} // namespace Client::Particles
