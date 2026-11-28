#pragma once

#include <glm/ext/vector_float3.hpp>

#include <cstdint>

// The arguments of 0x518cf0, which creates a model effect: what every module that creates one hands it (the characters,
// the monsters, the skills, the joints, the world's objects and the events). The effects' table and its logic are
// game/Effects'.
namespace Client::Kernel
{
// The eleven, as the prefix writes them (0x518d28..0x518e36): the type at +0x2, the position at +0x10, the angles at
// +0x1c, the light at +0xf4, the subtype at +0x4, the owner at +0x108 (`owner`, opaque), +0x8a, +0x89, +0x88 and
// +0x1bd, and the scale (0.9 at 0 or below).
struct ModelEffectArguments
{
    std::int16_t type = 0;
    glm::vec3 position{};
    glm::vec3 angles{};
    glm::vec3 light{};
    std::int32_t subtype = 0;
    void* owner = nullptr;
    std::uint16_t key = 0xffff;
    std::uint8_t byte89 = 0;
    std::uint8_t byte88 = 0;
    std::uint8_t byte1bd = 0;
    float scale = 0.0f;
};
} // namespace Client::Kernel
