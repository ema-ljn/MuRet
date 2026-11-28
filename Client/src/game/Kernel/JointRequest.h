#pragma once

#include <glm/ext/vector_float3.hpp>

#include <cstdint>

// The arguments of 0x530860, which creates a joint: what every module that creates one hands it (the characters, the
// monsters, the fauna, the effects, the world's objects and the events). The joint's table and its logic are
// game/Joints'.
namespace Client::Kernel
{
// What is behind `ownerContext`. Almost always the joint's type says it (Scenes/FaunaJointCreation.h): a FaunaObject,
// the character's ActorState or an opaque owner. The 0x60f/0 the Dark Horse releases around it (0x444740) carries the
// horse's HelperState instead, and those of the raven's orders (0x418390 and 0x418400, the A9 of Scenes/PetStats) the
// raven's DarkSpirit. In the original they are all the owner's object and the joint reads the same offsets: the light
// (+0xf4) when it is born and the action (+0x111) in the step. MuRet's own: the original has no such argument.
enum class JointOwnerKind : std::uint8_t
{
    ByType,
    // The 0x610/8, which by its type carries Atlans' fish (FaunaObject), with the ActorState of the player who enters
    // the viewport in Kalima (0x4c91dc): it reads the owner's light (+0xf4), position (+0x10) and angles (+0x1c) when
    // it is born, and its live byte (+0) and position in the step. With another type, CreateFaunaJoint stops before
    // writing (0x530860).
    Actor,
    Helper,
    Raven,
    ModelEffect, // The 0x619/1 of the 0x61e/1 and the 0x610/13 of the 0xe1/1; no ActorState alias.
};

struct JointRequest
{
    std::int32_t type;
    glm::vec3& firstPosition;
    glm::vec3& secondPosition;
    glm::vec3& angles;
    std::int32_t subtype;
    void* ownerContext; // It may be kept; the other arguments are only lent for the call.
    float scale;
    std::int32_t key;
    std::int32_t skill;
    std::int32_t skillSerial;
    std::int32_t target;
};
} // namespace Client::Kernel
