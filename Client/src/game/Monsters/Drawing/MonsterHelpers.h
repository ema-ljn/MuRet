#pragma once

#include "Core/Expected.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace Client::Assets
{
class FaunaModels;
}

namespace Client::Data::Terrain
{
struct TerrainHeights;
}

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

namespace Client::Kernel
{
struct ActorState;
class WorldState;
} // namespace Client::Kernel

// The per-type helpers of the monsters' draw (0x441400, 0x434180 and 0x4271d0, of the 0x510a1e chain) and of the step
// from frame 3 (0x432bd0, of 0x4fcee0), CL9 milestone 2, phase 6d. Contract: 06 § El dibujo del juego, "Los helpers de
// los monstruos".
//
// Everything that is state, sound or rand() is ported, and the particles (0x53fe70) Particles::Particles creates
// (0x5ca/0, 0x5f2/11, 0x5f3/0, 0x5d9/0 and 0x729/0 and /1) are created with their rand(). Those MuRet does not have cut
// with the call's address only if their table has room: when full, the original creates nothing, takes no rand() and
// goes on, and MuRet goes on with it. The effects (0x518cf0), joints (0x530860), trails (0x52e470) and 0x51fe10 still
// unported belong to CL11 and cut with the call's address, without checking whether their table is full (the pass does
// not carry it, P-C336); the deaths of 0x1a4 and 0x1a6 (0x4ee770) and the 0x12e/3 stone of 0x1a6 (0x442397) are done. A
// named bone (0x4010b0 and 0x4010d0 → 0x4016c0) that the monster's spawn did not register in WorldRuntime::namedBones
// also cuts: the original would leave the point unwritten, with whatever was on the stack.
namespace Client::Monsters::Drawing
{

// What the draw helpers read: the character (the record or its copy between original frames), its pool slot (the key of
// 0x4016c0), the draw's rand(), the frame's sprites (0x5491e0) and particles (0x53fe70) and the world
// clock ([0x5767b88], in ms). The sounds (0x41ed10, in WorldRuntime::lastSoundId for CL10) are requested only in an
// original frame. The batch and the .OZB heights are those of the 0x69b floor marks (0x5fb2f0) of 0x204 to 0x208.
struct MonsterHelperPass
{
    Kernel::ActorState& actor;
    std::size_t slot = 0;
    Kernel::WorldState& world;
    Crt::CrtRandom& random;
    Particles::FrameSprites* sprites = nullptr;
    Particles::Particles* particles = nullptr;
    float worldClock = 0.0f;
    std::int32_t map = 0;
    bool originalFrame = true;
    Render::ModelBatch* batch = nullptr;
    const Data::Terrain::TerrainHeights* heights = nullptr;
    // 0x4ee770(0, type, subtype, 0, 0) with the monster's model over the meshes of 0x5786f9c (those of the body it just
    // drew, Q-C1581): the per-vertex death effects of 0x1a4 and 0x1a6 (Actors::Drawing::EmitVertexEffects on the
    // world's effects). Without it, those deaths cut.
    std::function<Core::Expected<void, std::string>(std::int32_t type, std::int32_t subtype)> vertexEffects{};
};

// 0x441400(c, object, model), from 0x1a3 to 0x1a9 (the table of 0x442de8): the eyes, tail and hands of Monster81 to
// Monster87, their sounds and the CL11 effects. Always returns false (0x442dda); except 0x1a8, which leaves without
// touching anything, it sets +4 to 0 in actions 0 and 1 (0x442dd2).
[[nodiscard]] Core::Expected<bool, std::string> DrawTheEffectsOf1a3To1a9(MonsterHelperPass& pass);

// 0x434180, only in map 34 (0x4303c0): from 0x1ab to 0x1b5 their sounds, the sprites of 0x1af, 0x1b1 and 0x1b2 and the
// 0x5f3 particle of 0x1b1; from 0x204 to 0x208, with +0x7c & 0x40000 their own light and the 0x729/0 and /1 particles,
// and the 0x69b floor marks (0x5fb2f0) between 0x620a90 and 0x620980. The first carries the color of 0x40000 or of the
// bits 0x100000, 0x20000, 0x10000 or 0x80000; the second, the turn only 0x40000 writes. A mark whose color or turn is
// the unwritten stack (no bit, or the second without 0x40000) is not drawn (Q-C1100): it only binds its texture.
// Returns true with 0x1ad and 0x1af to 0x1b4, which cut the 0x510a1e chain.
[[nodiscard]] Core::Expected<bool, std::string> DrawTheCrywolfEffectsOf1abTo208(MonsterHelperPass& pass);

// 0x4271d0(c, object), from 0x1b6 to 0x1bb (the table of 0x428dd4): the 0x5ad sprites of Monster100 to Monster105,
// their sounds and their particles. Always returns false (0x428dc5); within its range it sets +4 to 0 in actions 0
// and 1.
[[nodiscard]] Core::Expected<bool, std::string> DrawTheEffectsOf1b6To1bb(MonsterHelperPass& pass);

// 0x510a42..0x510a77: 0x441400, 0x42fa30 (always 0, does nothing), 0x434180 and 0x4271d0, up to the first that returns
// non-zero. The first two of the chain (0x43c150 and 0x42dfe0) go before, in MonsterDrawing.
[[nodiscard]] Core::Expected<void, std::string> DrawTheMonsterHelpers(MonsterHelperPass& pass);

// 0x432bd0(c, object, model), from 0x4fcee0 (0x4fd5d7) with any type from frame 3 without a trail: the rand() of
// 0x432bf1 unconditionally and, from 0x1ab to 0x1b4 in their action, the trail's poses in the global buffer (0x4eb860
// on 0x68b181c, WorldState::PoseCarry). 0x1b0 in action 4 does its ten rounds, with two rand() each and, with the frame
// in (5, 7), the 0x5d9/0 particle (in `particles`, with `worldClock`) of bone 16 of each pose. The trails use real
// ribbons; 0x1ab adds 0x640/0 before each sample in (3.5, 4.5), and 0x1ae in action 8 does not pose: it creates
// 0x5f2/27 and two 0x5ad/2. The reservations of 0x145/2 and 0x147/0/1 keep the order and limits of their tables. The
// model's action, scale and position (+0xa0, +0x68 and +0x6c, from its last 0x5ffdf0) are the character's (Q-C1102,
// *inferred*).
[[nodiscard]] Core::Expected<void, std::string>
StepTheSwingsOf1abTo1b4(Kernel::ActorState& actor, std::size_t slot, Kernel::WorldState& world,
                        Assets::FaunaModels* faunaModels, Crt::CrtRandom& random, Particles::Particles* particles,
                        float worldClock);
} // namespace Client::Monsters::Drawing
