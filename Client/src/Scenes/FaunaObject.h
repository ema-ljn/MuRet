#pragma once

#include "Render/Models/OriginalRotation.h"

#include <glm/ext/vector_float3.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace Client::Scenes
{
// Layout trivial común de los pools de 446120 y 447030. 06 § Los pájaros / Los peces.
// +0xc y +0xd8 intercambian su uso entre aves y peces; los nombres describen el ave.
struct FaunaObject
{
    std::uint8_t active = 0;
    std::byte padding01{};
    std::int16_t type = 0;
    std::int32_t counter = 0;
    std::array<std::byte, 4> padding08{};
    float bodyScale = 0;
    glm::vec3 position{};
    glm::vec3 angles{};
    std::array<std::byte, 24> padding28{};
    glm::vec3 previousPosition{};
    std::array<std::byte, 12> padding4c{};
    std::int32_t hiddenMesh = 0;
    std::int32_t padding5c = 0;
    std::int32_t timer = 0;
    std::int32_t blendMesh = 0;
    float ceiling = 0;
    std::array<std::byte, 24> padding6c{};
    float phase = 0;
    std::array<std::byte, 7> padding88{};
    std::uint8_t flag8f = 0;
    std::array<std::byte, 12> padding90{};
    Render::Models::OriginalMatrix matrix{};
    float aheadX = 0;
    float aheadY = 0;
    float vertical = 0;
    float scale = 0;
    float paddingDc = 0;
    float size = 0;
    float turnRate = 0;
    std::uint8_t vertexLight = 0;
    std::array<std::byte, 11> paddingE9{};
    glm::vec3 light{};
    std::array<std::byte, 16> padding100{};
    std::uint8_t motionState = 0;
    std::uint8_t action = 0;
    std::uint8_t previousAction = 0;
    std::byte padding113{};
    float frame = 0;
    float previousFrame = 0;
    std::array<std::byte, 80> padding11c{};
    std::uint8_t seen = 0;
    std::uint8_t alphaMode = 0;
    std::array<std::byte, 2> padding16e{};
    float alphaTarget = 0;
    float alpha = 0;
    std::array<std::byte, 92> padding178{};
};
static_assert(std::is_trivially_copyable_v<FaunaObject> && std::is_standard_layout_v<FaunaObject>);
static_assert(sizeof(FaunaObject) == 0x1d4);
static_assert(offsetof(FaunaObject, bodyScale) == 0xc);
static_assert(offsetof(FaunaObject, matrix) == 0x9c);
static_assert(offsetof(FaunaObject, scale) == 0xd8);
static_assert(offsetof(FaunaObject, action) == 0x111);
static_assert(offsetof(FaunaObject, seen) == 0x16c);
static_assert(offsetof(FaunaObject, alpha) == 0x174);
} // namespace Client::Scenes
