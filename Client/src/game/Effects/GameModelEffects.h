#pragma once

#include "Core/Expected.h"
#include "Audio/SoundId.h"

#include <glm/ext/vector_float3.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <functional>
#include <string>

namespace Client::Crt
{
class CrtRandom;
} // namespace Client::Crt

namespace Client::Models
{
struct Skeleton;
} // namespace Client::Models
namespace Client::Data::Terrain
{
struct TerrainAttributes;
}
namespace Client::Terrain
{
struct TerrainLighting;
} // namespace Client::Terrain

namespace Client::Kernel
{
struct ModelEffectArguments;
struct JointRequest;
} // namespace Client::Kernel

namespace Client::MapScripts::Kalima
{
class KalimaWorld;
} // namespace Client::MapScripts::Kalima

namespace Client::Particles
{
class Particles;
class FrameSprites;
} // namespace Client::Particles

namespace Client::Effects
{
struct ModelEffect;
class ModelEffects;
// 0x5192a3 and 0x5212e7: creation and body of the trail shared by Crywolf and the earthquake.
inline constexpr std::int16_t SpiritTrailEffect = 0x104;
[[nodiscard]] std::optional<std::size_t> CreateSpiritTrail(const Kernel::ModelEffectArguments&, ModelEffects&);

// What the cases ask for outside their effect: the ground height (0x5f93c0) for the butterfly, the stones and the
// earthquake, the frame's sprite table (0x5491e0) for the butterfly, the effect table (0x518cf0) and the dynamic light
// (0x7b4ccc4) for the jet 0x5de, the stones 0x118/0x119 and the earthquake's cracks, timeGetTime() (0x6b8544) for the
// Fenrirs' decal 0x14e, and for the earthquake 0xfa and its cracks, the cell attributes (0x7d0d348), the map
// (0x6c9dd0), the camera shake (0x7d325c8) and the sound (0x41ed10(id, 0, 0)). Without them, the case stops at its
// address.
struct ModelEffectStepServices
{
    std::function<double(float, float)> terrainHeight{};
    Particles::FrameSprites* sprites = nullptr;
    ModelEffects* effects = nullptr;
    Terrain::TerrainLighting* lighting = nullptr;
    std::function<std::uint32_t()> systemTime{};
    const Data::Terrain::TerrainAttributes* terrainAttributes = nullptr;
    const std::int32_t* map = nullptr;
    float* cameraShake = nullptr;
    std::function<void(Audio::SoundId)> sound{};
    std::function<Core::Expected<void, std::string>(const Kernel::JointRequest&)> joint{};
    // 0x52cc21: the pulse 0x103's step publishes its action in the shared table before animating.
    std::function<void(std::uint16_t, std::uint8_t)> publishAction{};
    // 0x51ed6a and 0x52b349 ask for a sound for the flash 0x60d at its position.
    std::function<void(Audio::SoundId, const glm::vec3&)> soundAt{};
    // 0x52ae2a: the published hero ([0x743a170]), which the Flame's jet 0x5de/0 compares with its owner (+0x108), and
    // 0x517a20(+0x89, the position, `radius`, the byte +0x8c, +0x8a, 0) over that jet (Actors/SkillHits, which reads
    // the character pool and the own record and sends the C1 10; CL9 milestone 4). Without `skillHits`, the hero's jet
    // stops there.
    const void* hero = nullptr;
    std::function<Core::Expected<void, std::string>(const ModelEffect&, float)> skillHits{};
    // Effects/BlastModelEffects: the own record (0x7681eb4) that the creation of the 0xf4 dropped by the 0xf3's step
    // opens and closes: its depth (the close at 0 spends 3848 rand()) and its count +0xf04.
    std::uint8_t* ownRecordDepth = nullptr;
    std::uint8_t* ownRecordCount = nullptr;
};
// 0x52bbf6..0x52be6d: the three emissions of the glow 0x621 of any subtype (the /0 from here, the others from
// Effects/HighModelEffects): two rand() % 360 for the angles and, outside the /1 and the /2, the sprite 0x5fd with two
// more rand() (scale · 0.3f) and the spark 0x5c6/2 with another (0x52be64). The /1 and the /2, with even life, the
// joint 0x614/3 from the point to the position (0x52bcc4, 0x52bd80) and, the /1 with odd life, the spark (0x52bd02);
// then both, the sprite with scale · 0.2f. Without the sprite or effect table, or without the joint service when it
// asks for it, it stops.
[[nodiscard]] Core::Expected<void, std::string> EmitPiercingShotGlow(ModelEffect& effect,
                                                                     Particles::Particles& particles,
                                                                     Crt::CrtRandom& random,
                                                                     const ModelEffectStepServices& services);
// 0x5208c5..0x52091a: the step's local light, from the first rand(): ((r & 3) + 7) · 0.1f (0x6b87b8); with life under
// 5, minus (5 − life) · 0.2f (0x6b8854), stored as float and compared unrounded with 0.
[[nodiscard]] float StepLuminosity(int draw, std::int32_t life);

// 0x5208a0: common CRT, arrival/teleport, Noria tower, Kalima's 9/0 with the wave owner, the butterfly 0xae (its
// flight rand(), the subtype's light, the /3's sparks 0x5c5/7 and the sprite 0x5ac) and those of Dungeon and Lost
// Tower: the stone 0xda that bounces (0x52444d), the jet 0x5de/0 (0x52ad21: six flames 0x5de/0, sometimes a pebble and
// its light, with the local light from the first rand(); the Flame's, with the hero as owner and life a multiple of 20,
// sends the targets at 150 through `skillHits`, 0x52ae2a) and the pebbles 0xc8/0xc9 with subtype 0 (0x5238be: they
// bounce, tilt and sometimes drop a fire 0x5d9 of subtype 1 to 3), 1 (0x523985: they rise without reading the ground)
// and 10 (like 0, without the fire or its rand()). They leave in previousPosition the position from before the whole
// step. The stones 0x118/0x119 of the horse's stomp (0x5275ca), in their steps 33 to 36, drop the dust 0x5f2/11 and a
// pebble 0xc8/0xc9 with subtype 10 (from the effect table), fade with life under 8 and always light the radius 2
// dynamic light. Aida's 0xb3 (0x520b77) wanders with its two drawn turns and the clock's sine, and drops a 0x5ac/9 per
// step. The 30's 0x135 (0x52973e) does not die and, with an owner, rises and falls with the clock's sine. The decal
// 0x61d/3 of Crywolf's 0x38 (0x52b933), with life between 6 and 9, drops an orbit 0x60f/10; the /0 leaves the /1 each
// step and, with the /2 and the /7, drops the 0x60f/0 (0x52b8ff, 0x52b977): without the effect table it stops only if
// the orbit has room. The wave 0x6eb/1 of the horse's gallop in Icarus (0x52c5ca) grows and shakes with three rand()
// (the /2, the same by 0.025f); the stomp's /0 shrinks 1 per step, without rand(); the /3 and the /4 grow 2 and 0.3f;
// the siege's subtypes 5..8 keep their scales and RGB: the 6 emits /5 children every 8 lives, which the ascending pool
// can advance in the same step; another subtype does not touch the scale. The Fenrirs' ray 0x14d (0x5295a4) lights up
// by 0.3f with subtype 0 and goes out with 1, and turns 0.15f per axis; the decal 0x14e (0x52965a) turns 0.1f in x,
// fades by 0.05f (the light by subtype) and changes frame every 200 ms of timeGetTime(), up to the fifth. The
// earthquake 0xfa (0x521c5c): at life 11 it shifts the start behind its owner, puts it on the ground (in Kalima, with
// the wave 0x437860), drops the explosion 0x5ed/0 and, with an owner not of type 0x1b1, the cracks 0xfd, 0xfb and
// 0xfc, and five pairs 0xfe/0xff where the cell allows it; at 10, four rings of five pairs 0x101/0x102, the sound 0x97
// and life at 0; at the others, it swings with +0x28. With state 0 (the Dark Lord's skill), at life 13 it emits two
// groups of four trails 0x104 and the sound 0x96 before the swing; at 11 it emits eight joints 0x617/0, a spark
// 0x5c5/0 and the pulse 0x103. It skips the tail's animation and 0x51fc80 (0x52cbb0). The cracks 0xfb..0x102
// (0x52276a..0x522bd2) fade and sink; the 0xfb shakes the camera, the 0xfc and the 0xff sometimes drop pebbles
// 0xc8/0xc9 and, with the 0x102, light the dynamic light by their owner's type. The stones 0x10a and 0x10b of the
// Blood Castle statues (0x52531d) jump, fall onto the ground and tilt; one step in ten stops at 0x5252e6, where the
// dust 0x5f3/0 goes into an unwritten local (Q-C1176).
[[nodiscard]] Core::Expected<void, std::string>
StepGameModelEffect(ModelEffect& effect, const Models::Skeleton* skeleton, Particles::Particles& particles,
                    float worldClock, Crt::CrtRandom& random, MapScripts::Kalima::KalimaWorld* kalima = nullptr,
                    const ModelEffectStepServices& services = {});
} // namespace Client::Effects
