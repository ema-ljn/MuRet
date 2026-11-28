#include "Particles/ObjectFire.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Kernel/WorldObject.h"
#include "Math/FloatConstants.h"
#include "Math/OriginalRotation.h"
#include "Particles/Particles.h"
#include "Terrain/DynamicWorldLight.h"

#include <glm/ext/vector_float3.hpp>

#include <bit>
#include <cstdint>
#include <format>
#include <string>

namespace Client::Particles
{
namespace
{
using Math::Tenth;
constexpr float WarmGreen = std::bit_cast<float>(0x3f19999au);
constexpr float FireBlue = std::bit_cast<float>(0x3ecccccdu);
constexpr std::int32_t FireRadius = 4;
constexpr std::int32_t JitterMask = 15;
constexpr std::int32_t JitterCenter = 8;

Core::Expected<void, std::string> EmitParticle(const Kernel::ObjectStepGlobals& globals, const glm::vec3& position,
                                               const glm::vec3& angles, const glm::vec3& light, std::int32_t type,
                                               std::int32_t subtype, Crt::CrtRandom& random)
{
    if (globals.particles == nullptr)
    {
        return Core::Unexpected(std::string("0x53fe70: falta el pool de partículas 0x743a198"));
    }
    const auto emitted =
        globals.particles->CreateChecked(type, position, angles, light, subtype, 1.0f, globals.worldClock, random, 0);
    if (!emitted.HasValue())
    {
        return Core::Unexpected(std::format("0x53fe70: creación de partícula {:#x}/{} rechazada ({})", type, subtype,
                                            static_cast<int>(emitted.Error())));
    }
    // No index means the pool is full: it does not interrupt the caller or its later light.
    return {};
}

} // namespace

// fst keeps the retained red: its products do not start from the channel already rounded to float.
glm::vec3 WarmLight(std::int32_t intensity, float green, float blue)
{
    const double red = static_cast<double>(intensity) * static_cast<double>(Tenth);
    return {static_cast<float>(red), static_cast<float>(red * static_cast<double>(green)),
            static_cast<float>(red * static_cast<double>(blue))};
}

Core::Expected<void, std::string> EmitObjectFire(std::int32_t fireClass, const Kernel::WorldObject& object,
                                                 const glm::vec3& offset, Crt::CrtRandom& random,
                                                 const Kernel::ObjectStepGlobals& globals)
{
    return EmitObjectFire(fireClass, object.transform.angles, object.transform.position, object.light, offset, random,
                          globals);
}

Core::Expected<void, std::string> EmitObjectFire(std::int32_t fireClass, const glm::vec3& angles,
                                                 const glm::vec3& origin, const glm::vec3& objectLight,
                                                 const glm::vec3& offset, Crt::CrtRandom& random,
                                                 const Kernel::ObjectStepGlobals& globals)
{
    const auto matrix = Math::OriginalAngleMatrix(angles);
    glm::vec3 position = Math::OriginalVectorRotate(offset, matrix);
    for (int axis = 0; axis < 3; ++axis)
    {
        position[axis] = static_cast<float>(static_cast<double>(position[axis]) + origin[axis]);
    }
    // Three separate stores after the three previous sums, in X/Y/Z.
    for (int axis = 0; axis < 3; ++axis)
    {
        const int jitter = (random.Next() & JitterMask) - JitterCenter;
        position[axis] = static_cast<float>(static_cast<double>(jitter) + position[axis]);
    }
    if (fireClass == 0)
    {
        const glm::vec3 light = WarmLight(random.Next() % 6 + 6, WarmGreen, FireBlue);
        if ((random.Next() & 1) == 0)
        {
            const int subtype = random.Next() & 3;
            const auto emitted = EmitParticle(globals, position, angles, light, FireParticle, subtype, random);
            if (!emitted.HasValue())
            {
                return emitted;
            }
        }
        return Terrain::AddDynamicLight(globals.dynamicLighting, position, light, FireRadius, "0x52f534");
    }
    if ((fireClass == 1 || fireClass == 2) && (random.Next() & 1) == 0)
    {
        return EmitParticle(globals, position, angles, objectLight, BreathParticle,
                            fireClass == 1 ? 0 : WorldSmokeSubtype, random);
    }
    return {};
}
} // namespace Client::Particles
