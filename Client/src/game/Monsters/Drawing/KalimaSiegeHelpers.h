#pragma once

#include "Core/Expected.h"
#include "Kernel/DrawTailState.h"
#include "Audio/SoundId.h"

#include <glm/ext/vector_float3.hpp>

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>

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

namespace Client::Models
{
struct Skeleton;
} // namespace Client::Models

namespace Client::Effects
{
class ModelEffects;
} // namespace Client::Effects

namespace Client::Kernel
{
class KalimaWater;
} // namespace Client::Kernel

namespace Client::Particles
{
class FrameSprites;
class Particles;
} // namespace Client::Particles

namespace Client::Kernel
{
struct ActorState;
} // namespace Client::Kernel

// The per-type effect helpers of the 0x510a1e chain (0x43c150 and 0x42dfe0) and the bone loop of type 0x16e (0x5108b6),
// over a monster's already drawn body (MonsterDrawing.h). Contract: 06 § Los helpers de Kalima y del asedio (0x43c150 y
// 0x42dfe0). Model effects (0x518cf0), joints (0x530860) and particles (0x53fe70) that MuRet does not have cut with
// their address (CL11) if their table has room; when full, the original creates nothing and goes on.
namespace Client::Monsters::Drawing
{

struct KalimaSiegeHelperPass
{
    Render::ModelBatch& batch;
    // The character and the object (+0x8 and +0xc of 0x43c150): the same record, or its copy between original frames.
    Kernel::ActorState& actor;
    // The drawing's CRT rand() (or its copy between frames).
    Crt::CrtRandom& random;
    Particles::FrameSprites* sprites = nullptr;
    Particles::Particles* particles = nullptr;
    std::span<Kernel::JointState> joints{};
    Effects::ModelEffects* effects = nullptr;
    // 0x723c48: without it, 0x437860 disturbs nothing.
    Kernel::KalimaWater* kalima = nullptr;
    // 0x7d325c8
    float* cameraShake = nullptr;
    // The .OZB heights of 0x5fb2f0, for the decals of 0x42a1b0.
    const Data::Terrain::TerrainHeights* heights = nullptr;
    // 0x41ed10 (CL10): WorldRuntime::lastSoundId in the original's frame; between frames, nullptr.
    std::optional<Audio::SoundId>* sound = nullptr;
    std::function<void(Audio::SoundId)> soundRequest{};
    // 0x5767b88, in ms.
    float worldClock = 0.0f;
    std::int32_t map = 0;
    // 0x4294b0 (0x7239ec).
    bool siegeActive = false;
    // +0x6c..+0x74 of the model (+0x10 of 0x43c150, [ebp−0x14] of 0x50ba30): the position the shadow's 0x60cf70 copied
    // to it (0x50beb6, already on the Blood Castle floor of 0x50be4d) or, without that call, 0x5ffdf0's
    // (MonsterFrame::modelOrigin). Without the position, the character's.
    std::optional<glm::vec3> modelOrigin{};
};

// 0x510a1e..0x510a36: 0x43c150 and, if the type is not its own, 0x42dfe0. Returns true if one of them has the
// type (+0x2), which skips the rest of the chain. Without the sprite table, only a type that uses it cuts.
//
// 0x43c150, by type:
// - 0xc5 (the Kalima gate): with action 1, if +0x60 is 0, particle 0x6ae (CL11) from bone 8 (the point overwrites the
//   position 20 higher that the original builds before); +0x60 counts from 0 to 4.
// - 0x191: outside action 6, from bones 74..78 and 62..66 two 0x5ac each with a rand() & 1 for particle 0x607/1 (CL11),
//   and the 0x5ca and 0x5fd of bones 8, 22 and 15 with +0x68; always, the 0x5ac of bones 51..55 and 29..33.
// - 0x192 (Kundun): in action 6 it drops the left hand; otherwise, the 0x5ca of bones 8 and 9, the 0x62b (and in action
//   5 the 0x62d with the pulse of +0x8a and +0x89) of bone 100 and a rand() & 1 for particle 0x5f2/13 (CL11). In action
//   5, with the frame under 2, sound 0x1db (CL10); past 4, in Kalima, 0x437860 on its cell; past 3, the shake with
//   rand() & 7; in Kalima between 3 and 14, two 9 effects (0x518cf0) with rand() % 360 and rand() % 600. Actions 3 and
//   4 emit effect 0x128 (the /0 at the object's x and y with bone 49's z, 0x43d051; the /1 at bone 49 at height 400,
//   0x43d0fd; Effects/KundunEffects), the 0x6ee/14 joints (CL11), 0x437860 and the shake with rand() & 15.
// - 0x193, 0x194, 0x195, 0x196 and 0x197: their sprites (0x194 with two rand() % 360); 0x196 emits the 0x5ac/0 mist
//   from bone 60 and, in actions 0 and 1, rand() % 50 per bone; the 0x614/7 and 0x610/5 joints and particle 0x5f2/13
//   cut (CL11); the 0x5da/1 (Particles::MissileFireParticleKernels) and the 0x5dc/12 are created. 0x195 and 0x197 drop
//   both hands in action 6.
// 0x42dfe0, only in map 30: 0x19c, 0x1a1, 0x1a8 and 0x1fd, with their sprites, +0x68, +0x42c, +0x58 and the decals of
// 0x42a1b0, which in mode 0x80 overwrite the light they receive (+0xf4 of 0x19c without the siege and of 0x1a1 with it,
// Q-C876).
[[nodiscard]] Core::Expected<bool, std::string> DrawKalimaSiegeHelpers(const KalimaSiegeHelperPass& pass);

// 0x5108b6..0x510a1c, type 0x16e: for each bone that is not a dummy, outside 15..20 and 27..32, the 0x5fd at scale 2.5
// additive (with +0x1d6 at 0) or the green 0x61e at 0.8, and rand() & 3 at 0 in actions 3 and 4 for particle 0x5ca with
// the same light. Then it skips the helper chain.
[[nodiscard]] Core::Expected<void, std::string> DrawTheBoneSparks(const KalimaSiegeHelperPass& pass,
                                                                  const Models::Skeleton& skeleton);

// 0x42a1b0(0x61e, mode, x, y, side, side, light): the two turned decals of 0x5fb2f0, with 0x620b10 in mode 0x80 (the
// light becomes v and then v · (1 − v)) and with 0x620a90 in the others (the light times v), v = sin(T · 0.0015f) ·
// 0.3f + 0.7f.
[[nodiscard]] Core::Expected<void, std::string> DrawCastleGlow(const KalimaSiegeHelperPass& pass, std::int32_t mode,
                                                               float x, float y, float side, glm::vec3& light);
} // namespace Client::Monsters::Drawing
