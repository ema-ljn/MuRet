#include "World/PortableParticleTrigonometry.h"

#include "Core/Expected.h"
#include "Render/Models/OriginalRetainedMath.h"
#include "World/Particles.h"

#include <optional>

namespace Client::World
{
Core::Expected<double, ParticleError> PortableParticleTrigonometry::SineProductPC53(double argument,
                                                                                    float multiplier) const
{
    const std::optional<double> product = Render::Models::Detail::SineProductPC53(argument, multiplier);
    if (!product)
    {
        return Core::Unexpected(ParticleError::UnrepresentableTrigonometry);
    }
    return *product;
}

Core::Expected<double, ParticleError> PortableParticleTrigonometry::CosineProductPC53(double argument,
                                                                                      float multiplier) const
{
    const std::optional<double> product = Render::Models::Detail::CosineProductPC53(argument, multiplier);
    if (!product)
    {
        return Core::Unexpected(ParticleError::UnrepresentableTrigonometry);
    }
    return *product;
}
} // namespace Client::World
