#pragma once

#include "Kernel/JointRequest.h"
#include "Models/ObjectState.h"
#include "Models/Pose.h"
#include "Models/RayPicking.h"

#include <glm/ext/vector_float3.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

// The lists the draw tail draws (0x6374fe..0x637572): the floor items, the joints (0x74a8d78), the ribbons and the
// damage numbers, which the characters, the effects and the windows fill.
namespace Client::Kernel
{
struct FloorItemState
{
    bool alive = false;
    bool seen = false;
    std::int16_t type = 0;
    glm::vec3 position{};
    glm::vec3 angles{};
    float scale = 0;
    float alpha = 0;
    bool vertexLight = false;
    glm::vec3 light{};
    Models::AnimationState animation{};
    Models::ObjectState objectState{};
    std::int32_t value = 0;
    std::uint8_t excellent = 0;
    std::uint8_t ancient = 0;
    std::int16_t screenX = 0;
    std::int16_t screenY = 0;
    std::int32_t effectCounter = 0; // +4; 0x608780 uses the previous value, then adds one.
    float dropSpeed = 0;            // +0xe4; 0x608880 subtracts 6 even after touching the ground.
    // CL9 milestone 5: the item's type (+4 of the 0x220-byte entry, 0x54b3a0's; `type` is the model, +0x4e) and its
    // durability (+0x1e), which 0x608350 writes.
    std::int16_t itemType = 0;
    std::uint8_t durability = 0;
    // +0x124 and +0x130 of the object: 0x608350's box from (−30, −30, −30) to (30, 30, 30).
    glm::vec3 boundingMin{};
    glm::vec3 boundingMax{};
    // +0x13c: the box 0x60cf70 (0x4ebc80) builds on each body drawn with alpha > 0.01, from that draw's position; the
    // picking reads it (0x59e2e0). Without a draw the previous one stays.
    Models::Box pickBox{};
};

struct JointState
{
    bool alive = false;
    std::int32_t count = 0;
    std::uint8_t drawEnabled = 0; // +11: bits 1/2 pick the segment's two faces
    std::uint8_t category = 0;
    std::uint8_t blend = 0;
    std::int32_t type = 0;
    std::int32_t subtype = 0;
    std::uint16_t texture = 0;
    glm::vec3 light{};
    std::int32_t maximumCount = 0;                     // +0x6c, the UV divisor
    bool repeatedTexture = false;                      // +0x9f0
    std::uint8_t orientation = 0;                      // +0x9f1
    std::array<std::array<glm::vec3, 4>, 50> points{}; // +0x70, four points per stretch
    float width = 0;
    glm::vec3 position{};
    glm::vec3 angles{};
    glm::vec3 bendAngles{}; // +0x3c: 0x60f/0x17's own turn, separate from the common movement's angles.
    glm::vec3 targetPosition{};
    void* ownerContext = nullptr;
    // MuRet's own: how to read ownerContext. 0x530860 writes it on each creation.
    JointOwnerKind ownerKind = JointOwnerKind::ByType;
    std::int32_t life = 0;
    std::uint8_t field9d4 = 0;
    float speed = 0;
    glm::vec3 velocity{};
    std::int16_t key = 0;
    std::uint8_t skill = 0;
    std::uint8_t skillSerial = 0;
    // +0x9eb: the volley count (+0xf04 of the own record) that 0x613/0 and /5 copy at birth (0x53293b, 0x532e75) and
    // send with their targets (0x517a20, 0x539ac5). 0x530860 does not write it: the others keep the previous one.
    std::uint8_t volley = 0;
    bool tailEnabled = false;
    std::int32_t field9ec = 0;
    // D-C4: two turns of the last original draw; not part of the native record.
    std::optional<std::array<float, 2>> icarusRotations{};
    glm::vec3 pathIncrement{}; // +24; 0x531851/0x5332fa store the delta of the Helper07 ribbons.
    // +0x9f4..+0xa08: the character slots that 0x614/0xf chains (0x531981, at most six). The sixth overwrites the DWORD
    // at +0xa08, whose first byte is skillSerial: the creation writes both (see CreateLightningJoint).
    std::array<std::int32_t, 6> chainTargets{};
    // +0xa0c: the character slot that 0x10f/4 and /9 look at in their step (0x535d4f). Only the creation of those two
    // with a target ≠ −1 writes it (0x53098d); otherwise, the previous use's stays (the BSS starts at 0).
    std::int32_t target = 0;
};

inline constexpr std::size_t RibbonPointCount = 30;
struct RibbonState
{
    bool alive = false;
    std::int32_t type = 0;
    // A live read of the durable owner's +0x1d6; never a temporary view.
    const std::uint16_t* ownerEquipment = nullptr;
    std::int32_t count = 0;
    glm::vec3 light{};
    std::array<glm::vec3, RibbonPointCount> first{};
    std::array<glm::vec3, RibbonPointCount> second{};
    const void* ownerContext = nullptr; // 0x52e470 compares the owner before looking for a free slot.
    std::int32_t key = 0;               // +0x2f0; it only discriminates when the request brings key>0.
    std::int32_t life = 0;              // +8; 30 or 15 on creation, not renewed on reuse.
};

struct DamageNumberState
{
    bool alive = false;
    std::int32_t number = 0;
    float scale = 0.0f; // +10, the atlas size; speed*.4 is its alpha
    glm::vec3 position{};
    glm::vec3 light{};
    std::int32_t timer = 0;
    bool rise = false;
    float speed = 0.0f;
};

struct GameDrawTailState
{
    std::array<FloorItemState, 1000> floorItems{};
    std::array<JointState, 500> joints{};
    std::array<RibbonState, 100> ribbons{};
    std::array<DamageNumberState, 100> damage{};
};
} // namespace Client::Kernel
