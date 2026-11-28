#include "Kernel/ModelEffectArguments.h"
#include "Scenes/CastleSiegeAttack.h"
#include "Crt/CrtRandom.h"
#include "Core/Expected.h"
#include "Kernel/ActorState.h"
#include "World/ModelEffects.h"
#include <glm/ext/vector_float3.hpp>
#include <cstdint>
#include <string>
namespace Client::Scenes
{
namespace
{
Core::Expected<void, std::string> Emit(const CastleAttackEmitter& emit, const Kernel::ModelEffectArguments& arguments)
{
    if (!emit)
        return Core::Unexpected(std::string("0x42ea90: falta el pool del impacto sobre la construcción del asedio"));
    return emit(arguments);
}
float DebrisHeight(const Kernel::ActorState& target, Crt::CrtRandom& random)
{
    return static_cast<float>((static_cast<double>(random.Next() % 30) + target.position.z) + 200.0);
}
} // namespace
Core::Expected<void, std::string> EmitTheCastleAttackHit(const Kernel::ActorState& target, std::uint32_t heroFlags,
                                                         Crt::CrtRandom& random, const CastleAttackEmitter& emit)
{
    constexpr int Attempts = 5;
    if (target.type == 0x19c)
    {
        for (int attempt = 0; attempt < Attempts; ++attempt)
        {
            if ((random.Next() & 1) != 0)
                continue;
            const glm::vec3 point{target.position.x, target.position.y, DebrisHeight(target, random)};
            if (auto created =
                    Emit(emit, {.type = 0x10b, .position = point, .angles = target.angles, .light = target.light});
                !created.HasValue())
                return created;
        }
        return {};
    }
    if (target.type != 0x19b || (target.flags78 & 0x20000) != 0)
        return {};
    if ((heroFlags & 0x18000) == 0)
    {
        const glm::vec3 point{static_cast<float>(static_cast<double>(target.position.x) + 60.0),
                              static_cast<float>(static_cast<double>(target.position.y) - 100.0),
                              static_cast<float>(static_cast<double>(target.position.z) + 150.0)};
        if (auto created = Emit(emit, {.type = 0x114,
                                       .position = point,
                                       .angles = target.angles,
                                       .light = target.light,
                                       .subtype = 6,
                                       .key = 0});
            !created.HasValue())
            return created;
    }
    for (int attempt = 0; attempt < Attempts; ++attempt)
    {
        const glm::vec3 point{target.position.x, target.position.y, DebrisHeight(target, random)};
        const int subtype = (random.Next() & 1) + 1;
        const auto type = static_cast<std::int16_t>(0x131 + random.Next() % 3);
        if (auto created = Emit(
                emit,
                {.type = type, .position = point, .angles = target.angles, .light = target.light, .subtype = subtype});
            !created.HasValue())
            return created;
    }
    return {};
}
} // namespace Client::Scenes
