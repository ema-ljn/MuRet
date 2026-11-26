#pragma once

#include "EngineApi.h"

#include <glm/ext/vector_float3.hpp>

#include <cstddef>
#include <optional>
#include <span>

// Character picking: the mouse segment (Render::Pass3D::RayThrough, 0x620480) against each one's box. Contract:
// Client/docs/research/06-render-escenas-y-ui.md, "El picking", and Q-C100. All *static*.
namespace Client::Models
{
// Axis-aligned, in world space.
struct Box
{
    glm::vec3 min{};
    glm::vec3 max{};
};

// The 0x20a box from its position (0x511290, 0x512270 and 0x5122d4): from (−60, −60, 0) to (40, 40, 120), shifted
// and not rotated with the character (Q-C100).
inline constexpr glm::vec3 PlayerBoxMin{-60.0f, -60.0f, 0.0f};
inline constexpr glm::vec3 PlayerBoxMax{40.0f, 40.0f, 120.0f};
[[nodiscard]] ENGINE_API Box PlayerBoxAt(const glm::vec3& position);

// 0x622900: whether the segment from `origin` to `end` touches the box, by separation on 6 axes: the box's 3 and
// their cross product with the segment (0x6227b0). A barely touching edge counts.
[[nodiscard]] ENGINE_API bool SegmentHitsBox(const glm::vec3& origin, const glm::vec3& end, const Box& box);

// 0x59e700: of the characters in `positions` whose player box touches the segment, the one with the smallest
// distance² from `origin` (the camera) to its position, not to the hit point. std::nullopt if none is touched. With
// two at the same distance, the first.
[[nodiscard]] ENGINE_API std::optional<std::size_t> NearestHit(const glm::vec3& origin, const glm::vec3& end,
                                                               std::span<const glm::vec3> positions);
} // namespace Client::Models
