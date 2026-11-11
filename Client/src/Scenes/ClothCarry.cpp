#include "Scenes/ClothCarry.h"

#include "Render/Models/Cloth.h"
#include "Render/Models/HalfLifeMath.h"

#include <glm/ext/matrix_float3x3.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/matrix.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>

namespace Client::Scenes
{
namespace
{
// El giro del hueso: sus tres primeras columnas (la cuarta es la traslación, HalfLifeMath.h).
glm::mat3 TurnOf(const Render::Models::BoneTransform& bone)
{
    return glm::mat3{bone[0], bone[1], bone[2]};
}

// El origen del hueso en el mundo: 0x4ec110 con el punto local en 0.
glm::vec3 OriginOf(const Render::Models::BoneTransform& bone, float scale, const glm::vec3& position)
{
    return position + scale * bone[3];
}
} // namespace

SimulatedClothBody SimulatedBodyOf(const Render::Models::ClothBody& body)
{
    return SimulatedClothBody{
        .bones = {body.bones.begin(), body.bones.end()}, .scale = body.scale, .position = body.position};
}

std::optional<ClothCarry> ClothCarryOf(const SimulatedClothBody& then, const Render::Models::ClothBody& now,
                                       std::int32_t bone)
{
    const auto index = static_cast<std::size_t>(bone);
    if (bone < 0 || index >= then.bones.size() || index >= now.bones.size() || then.scale == 0.0f)
    {
        return std::nullopt;
    }
    const Render::Models::BoneTransform& before = then.bones[index];
    const Render::Models::BoneTransform& after = now.bones[index];
    if (before == after && then.scale == now.scale && then.position == now.position)
    {
        return std::nullopt;
    }
    const glm::mat3 turnBefore = TurnOf(before);
    if (glm::determinant(turnBefore) == 0.0f)
    {
        return std::nullopt;
    }
    return ClothCarry{.linear = (now.scale / then.scale) * TurnOf(after) * glm::inverse(turnBefore),
                      .from = OriginOf(before, then.scale, then.position),
                      .to = OriginOf(after, now.scale, now.position)};
}
} // namespace Client::Scenes
