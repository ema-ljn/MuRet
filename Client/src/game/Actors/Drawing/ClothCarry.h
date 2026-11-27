#pragma once

#include "Models/Cloth.h"
#include "Math/HalfLifeMath.h"

#include <glm/ext/matrix_float3x3.hpp>
#include <glm/ext/vector_float3.hpp>

#include <cstdint>
#include <optional>
#include <vector>

// What MuRet adds between two original frames, which the original does not have. The cloth is simulated once per
// original frame (Q-C124), but the body advances on each logic step and is drawn more often: in an intermediate draw,
// the cloth is carried with the bone it hangs from, from the place it had in its last simulation to the current one, as
// a rigid body. The anchored row falls on the current bone and the rest keeps the shape the simulation gave it. With
// the bone still, nothing is carried.
namespace Client::Actors::Drawing
{
// The body the cloth was last simulated with: its bones, copied, because the ClothBody's bones change with each frame's
// pose.
struct SimulatedClothBody
{
    std::vector<Math::BoneTransform> bones;
    float scale = 1.0f;
    glm::vec3 position{};
};

// 0x4ec110 places a cloth point in the world: position + scale · (bone · local). Carrying it is taking it out of the
// earlier bone's frame and putting it in the current one's: to + linear · (point − from).
struct ClothCarry
{
    glm::mat3 linear{1.0f};
    glm::vec3 from{};
    glm::vec3 to{};

    [[nodiscard]] glm::vec3 Carried(const glm::vec3& point) const
    {
        return to + linear * (point - from);
    }
};

[[nodiscard]] SimulatedClothBody SimulatedBodyOf(const Models::ClothBody& body);

// How much bone `bone` moved from `then` to `now`. None if the bone, the scale and the position did not change (the
// cloth is drawn as it was left, bit for bit), if the bone is missing in either or if its turn cannot be inverted.
[[nodiscard]] std::optional<ClothCarry> ClothCarryOf(const SimulatedClothBody& then, const Models::ClothBody& now,
                                                     std::int32_t bone);
} // namespace Client::Actors::Drawing
