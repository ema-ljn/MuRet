#pragma once

#include "Core/Expected.h"
#include "Core/IClock.h"
#include "Math/HalfLifeMath.h"
#include "Kernel/DrawTailState.h"

#include <glm/ext/vector_float3.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace Client::Crt
{
class CrtRandom;
} // namespace Client::Crt

namespace Client::Render
{
class ModelBatch;
} // namespace Client::Render

namespace Client::Particles
{
class FrameSprites;
class Particles;
} // namespace Client::Particles

namespace Client::Models
{
class IModelLibrary;
}

namespace Client::Kernel
{
struct ActorState;
struct PoseBuffer;
} // namespace Client::Kernel

namespace Client::Guild
{
struct GuildState;
} // namespace Client::Guild

// What hangs from a player in the game's draw (0x50ba30, from 0x50db57 to 0x50e9fa): the weapon on the back, the GMs'
// layer, the wings, Helper02 and the hands' loop, each one except the layer with 0x509340 (posed on the global pose
// buffer, drawn with 0x60b3d0 and with its model's sprites). What asks for an effect or a joint MuRet does not have
// stops with its address. Contract: 06 § El dibujo del juego, "Lo que cuelga en el juego".
namespace Client::Actors
{
class IJointHooks;
} // namespace Client::Actors

namespace Client::Actors::Drawing
{
struct ItemDrawContext;

struct GameHangingPass
{
    Render::ModelBatch& batch;
    const Models::IModelLibrary& models;
    // The body's bones for this frame: the character's own buffer (+0x120).
    std::span<const Math::BoneTransform> body;
    // The global rand() of 0x69fd70.
    Crt::CrtRandom& random;
    // The global pose buffer (0x68b181c) and the frame's sprite table (0x5491e0). Needed only if something hangs.
    Kernel::PoseBuffer* carry = nullptr;
    Particles::FrameSprites* sprites = nullptr;
    // The frame's joints of 0x530860, who creates them, and the particles of 0x53fe70. Needed only if something
    // creates them: the 0x617 of action 0x8c (0x50a110), the 0x618/0x11 of 0xc1e, the 0x610/4 of 0x223 and the 0x614/10
    // of 0x224. The 0x610/4 and 0x618/0x11 keep the hanging character as owner (+0x54): an ActorState.
    std::span<Kernel::JointState> joints{};
    const IJointHooks* jointHooks = nullptr;
    Particles::Particles* particles = nullptr;
    // The globals of 0x60b3d0 the frame's owner lends (without it, those of an empty ItemDrawContext).
    const ItemDrawContext* itemContext = nullptr;
    Core::Duration frameClock{};
    // DAT_05767b88: the world clock in ms, as a float.
    float worldClock = 0.0f;
    std::int32_t map = 0; // 0x6c9dd0
    // The hero's +0x1a1 (0x743a170), which 0x5ffdf0 reads in maps 18 to 23.
    std::uint8_t heroByte1a1 = 0;
    // 0x4294b0, the siege byte (0x7239ec), which 0x4142f0 looks at with layers 6 to 13.
    bool siegeActive = false;
    std::int32_t logicSteps = 0; // 0x7d3a0f0
    // In an original frame the animation of what hangs advances; between frames, it is drawn as it was left.
    bool originalFrame = true;
    // 0x414820 reads the guild and alliance names only for a master of role 6, 7, 8 or 12.
    const Guild::GuildState* guild = nullptr;
    // +0x6c..+0x74 of the type's model ([ebp−0x14] of 0x50ba30, 0x50ba7f): the position 0x6001b0 copied to it for the
    // player before the floor of Blood Castle's shadow (0x50bcaf), and for a monster that of the last 0x5ffdf0 or that
    // of its shadow's 0x60cf70 (0x50beb6, already on the floor of 0x50be4d; without it, 0x18e, 0x18f and 0xa8 to 0xac
    // stay at the earlier z: MonsterFrame::modelOrigin). 0x4ec110 translates the body bones' points with it (0x50e022,
    // 0x50e13a, 0x50e9c6..0x50f804) and 0x5092d0 the sprites of the global buffer's bones (0x50f1bf, 0x50f423); what
    // hangs comes from the object's (+0x10, 0x50966e), already on the floor. Without it, the character's.
    std::optional<glm::vec3> modelOrigin{};
};

// 0x50db57..0x50e9fa on a 0x20a already drawn (body and cloth): the rand() of the light of what hangs, the back (a
// weapon or the quiver of bone 47 with its angle), the GMs' layer (MuSign on bone 20, which it creates and advances in
// +0x360 and +0x364), the wings and Helper02 (outside maps 18 to 23), and outside the back the two hands, each with
// +0x38e opened and closed first. It writes the bone, the action, the speed and the animation of each hanging part,
// like the original, and +0x3b4 must already have the light of what hangs (0x50db94). With a monster or an NPC
// (0x50dcc2 jumps to 0x50e060), only the rand(), the back (which stops what follows) and the hands, without the effect
// of +0x78 & 0x800, which belongs to the 0x20a; `body` is its own bones.
[[nodiscard]] Core::Expected<void, std::string> DrawGameHanging(Kernel::ActorState& actor, const GameHangingPass& pass);

// 0x509340 from what 0x50ba30 adds by a monster's +0x376 (0x50c79a, 0x50c803 and 0x50c8b8): the model of the wings'
// slot (+0x2f8), which the caller already set up (the model, the bone, the two actions and the speed), without level or
// excellent. Without `placed`, from its body bone with `offset`; with it, through the place of 0x5093d2 (that of the
// statues 0x84 to 0x86: 0xc1c, 0x225 and 0xa24; another model stops at 0x5095e0). `bodyScale` is the scale the body was
// posed with (+0x68 of the type's model, which 0x4ec190 reads), and the character's, that of the hanging model. With
// 0xef, its origin and its turn (0x5096ac). It draws the rand() calls of 0x509340 (those of 0x607b60, the close of
// +0x38e and those of the model's sprites), not that of 0x50db57; `pass.body` is the character's own bones. Between
// original frames the animation of what hangs does not advance.
[[nodiscard]] Core::Expected<void, std::string> HangFromTheWingsSlot(Kernel::ActorState& actor,
                                                                     const GameHangingPass& pass,
                                                                     const glm::vec3& offset, bool placed,
                                                                     float bodyScale);
} // namespace Client::Actors::Drawing
