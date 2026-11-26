#pragma once

#include "Math/OriginalRotation.h"
#include "Models/Pose.h"

#include <glm/ext/vector_float3.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

// The Dark Lord's raven, the 0x460-byte object 0x4167b0 creates, with a CHARACTER at +0xc. Its owner's actor holds it;
// the raven's logic is game/Pets (Scenes/DarkSpirit.h).
namespace Client::Kernel
{
struct DarkSpirit
{
    float scale = 0.7f;    // +0xc
    glm::vec3 position{};  // +0x10
    glm::vec3 angles{};    // +0x1c, in degrees: x the pitch, z the heading
    std::int32_t far = 0;  // +0x60, the steps away from its owner
    glm::vec3 direction{}; // +0xcc
    float speed = 1.0f;    // +0xd8
    glm::vec3 light{0.5f}; // +0xf4
    // +0x110: 0 fast, 1 slow, 2 returning, 3 perched, 4 landing, 5 the attack, 6 the shout.
    std::uint8_t state = 0;
    Models::AnimationState animation; // +0x111 the action, +0x114 the frame
    float alpha = 1.0f;               // +0x174
    // What the orders write, at the object's offset (the CHARACTER's in parentheses); 0x4167b0 leaves them at zero.
    std::uint8_t order = 0;      // +0x45c, the A7's (0x416510), read by the step's state 5 (0x417a9d)
    std::uint8_t byte1ad = 0;    // +0x1ad (+0x1a1): at 1 the flight goes to `target` (0x4177b1); 0x417b33 clears it
    std::size_t target = 0;      // +8, a pool slot (0x416510, 0x416570)
    std::uint16_t target3b0 = 0; // +0x3b0 (+0x3a4), the A8's target, read signed (0x416a73)
    std::uint8_t byte38d = 0;    // +0x38d (+0x381), up to 15 in states 5 and 6 (0x417e15), read by 0x418920
    float floatF0 = 0.0f;        // +0xf0 (+0xe4), the attack's acceleration
    // What 0x417f30 writes with the raven's A9 (0x421db0), as 0x4221d0 does.
    std::uint16_t word1e2 = 0;        // +0x1e2 (+0x1d6)
    std::uint32_t experience = 0;     // +0x448
    std::uint32_t nextExperience = 0; // +0x44c
    std::uint16_t level = 0;          // +0x450
    std::uint16_t durability = 0;     // +0x452
    std::uint16_t word454 = 0;
    std::uint16_t word456 = 0;
    std::uint16_t word458 = 0;
    std::uint16_t word45a = 0;
    // MuRet's, to draw between two steps (D-C4).
    glm::vec3 previousPosition{};
    // +0x120, its bones in the world as the last draw left them (0x602e90); empty before the first (Q-C133).
    std::vector<Math::OriginalMatrix> pose;
};
} // namespace Client::Kernel
