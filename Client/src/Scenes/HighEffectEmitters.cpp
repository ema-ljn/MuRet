#include "Scenes/HighEffectEmitters.h"

#include "Core/Expected.h"
#include "Render/Models/Cloth.h"
#include "Scenes/FaunaJointCreation.h"
#include "Scenes/GameDrawTail.h"
#include "Scenes/HighModelEffects.h"
#include "Scenes/WorldState.h"
#include "World/GameModelEffects.h"
#include "World/IFishStepServices.h"
#include "World/ModelEffects.h"
#include "World/Particles.h"
#include "World/Sprites.h"

#include <glm/ext/vector_float3.hpp>

#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>

namespace Client::Scenes
{
Core::Expected<void, std::string> CreateHighJoint(WorldState& world, Render::Models::CrtRandom& random,
                                                  std::int32_t type, glm::vec3 first, glm::vec3 second,
                                                  glm::vec3 angles, std::int32_t subtype, World::ModelEffect* owner,
                                                  float scale, std::uint32_t address)
{
    constexpr std::int32_t NoKey = -1, NoTarget = -1;
    const auto made = CreateFaunaJoint(
        world.DrawTail().joints,
        World::FishEffectRequest{type, first, second, angles, subtype, owner, scale, NoKey, 0, 0, NoTarget}, random,
        owner != nullptr ? JointOwnerKind::ModelEffect : JointOwnerKind::ByType);
    if (!made.HasValue())
        return Core::Unexpected(std::format("{:#x}: {} (CL11)", address, made.Error()));
    return {};
}

Core::Expected<std::optional<std::size_t>, std::string>
CreateHighParticle(WorldState& world, Render::Models::CrtRandom& random, float worldClock, std::int32_t type,
                   const glm::vec3& position, const glm::vec3& angles, const glm::vec3& light, std::int32_t subtype,
                   float scale, std::uint32_t address)
{
    const auto created =
        world.Particles().CreateChecked(type, position, angles, light, subtype, scale, worldClock, random, 0);
    if (!created.HasValue())
        return Core::Unexpected(std::format("{:#x}: la partícula {:#x}/{} falla con el error {} (CL11)", address, type,
                                            subtype, static_cast<unsigned>(created.Error())));
    return created.Value();
}

Core::Expected<void, std::string> CreateHighSprite(const HighEffectStepContext& context, std::uint16_t texture,
                                                   const glm::vec3& position, float scale, const glm::vec3& light,
                                                   float rotation, std::uint32_t address)
{
    if (context.services.sprites == nullptr)
        return Core::Unexpected(std::format("{:#x}: el sprite {:#x} pide la tabla de sprites", address, texture));
    static_cast<void>(context.services.sprites->Create(texture, position, scale, light, rotation, 0));
    return {};
}
} // namespace Client::Scenes
