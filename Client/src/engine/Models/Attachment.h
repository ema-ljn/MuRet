#pragma once

#include "EngineApi.h"
#include "Math/HalfLifeMath.h"

#include <glm/ext/vector_float3.hpp>

#include <cstdint>

// Hands, wings and pet hang from a Player.bmd bone with an offset (0x511290 and 0x50ba30), as 0x509340 hangs them with
// the flag at 0: the root of 0x4ec190 and the pose of 0x4eb860 with its seventh argument at 1 (PoseAttached). The
// vertices go later through SkinMesh and the character's object, like the body parts.
// Contract: Client/docs/research/06-render-escenas-y-ui.md, "El modelo en el binario". The sword on 33 and the pet on
// 34 are **verified** against the selection trace; the wings, *static*.
namespace Client::Models
{
enum class AttachmentSlot : std::uint8_t
{
    RightHand, // +0x2bd
    LeftHand,  // +0x2dd
    Wings,     // +0x2fd
    Pet,       // +0x31d
};

struct Attachment
{
    std::int16_t bone = 0;
    glm::vec3 offset{};
};

// Right hand on bone 33 and left on 42, no offset; wings on 47 with (0, 0, 15); pet on 34 with (20, 0, 0).
[[nodiscard]] ENGINE_API Attachment AttachmentOf(AttachmentSlot slot);

// Q-C98: of the pets, only 0x1c13 (Helper02) is drawn on this path.
inline constexpr std::uint16_t DrawnPet = 0x1c13;
[[nodiscard]] constexpr bool DrawsThePet(std::uint16_t model)
{
    return model == DrawnPet;
}

// 0x509340 exits without drawing for the Dark Lord's cape (0x509363: CMP EDI,0x1c30 and JZ to the end), wherever it
// hangs from: DarkLordRobe.bmd is the inventory plane, and what shows is the cloth of 0x50ba30.
inline constexpr std::uint16_t LordsCape = 0x1c30;
[[nodiscard]] constexpr bool HangsAsAModel(std::uint16_t model)
{
    return model != LordsCape;
}

// 0x4ec190: the bone it hangs from, with the offset rotated by it and added to its translation. Not the character's
// scale: it goes on each vertex (verified: with it, the pet drops to 1.8).
[[nodiscard]] ENGINE_API Math::BoneTransform AttachmentRoot(const Math::BoneTransform& parent, const glm::vec3& offset);

// The 0x509340 path with its flag (the low byte: a model from 0x212 to 0x1011, a weapon): one AngleMatrix per model, in
// degrees (0x5fc8f0), with its translation, chained to the bone (R_ConcatTransforms, 0x5fca90).
struct HangingPlacement
{
    glm::vec3 angles{};
    glm::vec3 translation{};
};

// The quiver's, for bolts (0xa19) and arrows (0xa21), which 0x50ba30 hangs on the back from the wings' bone:
// (70, 0, 90) and (−10, 5, 10) (0x509535..0x5095bf).
inline constexpr HangingPlacement QuiverPlacement{{70.0f, 0.0f, 90.0f}, {-10.0f, 5.0f, 10.0f}};

// The root of what hangs by that path: the bone times its place's matrix, with the x87's bits (OriginalAngleMatrix and
// OriginalConcatTransforms). As in the other path, the character's scale goes on each vertex.
[[nodiscard]] ENGINE_API Math::BoneTransform PlacedRoot(const Math::BoneTransform& parent,
                                                        const HangingPlacement& placement);
} // namespace Client::Models
