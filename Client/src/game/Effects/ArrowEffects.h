#pragma once

#include "Core/Expected.h"
#include "Protocol/Outgoing.h"
#include "Audio/SoundId.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

#include <glm/ext/vector_float3.hpp>

// CL9 milestone 3, phase 2e: the elf's arrow with a bow or crossbow and the bows on Uniria, Dinorant and Fenrir. The
// shot (0x53e2b0 and 0x53d9a0), each arrow's creation in the model effect table (0x518cf0's cases) and its step
// (0x5208a0's cases with the hit of 0x520170 and 0x517850). 06, "La flecha". CL9 milestone 4: the shooting skills
// (0x53e741, 0x53e2e0's flag, the 0x13a, the 0x105 and the arrows 0xdb/3 and /4 and 0xe1/1) and the hit of the skill
// arrows (0x5201ed, with 0x517a20, Actors/SkillHits).
namespace Client::Crt
{
class CrtRandom;
} // namespace Client::Crt

namespace Client::Models
{
struct Skeleton;
} // namespace Client::Models

namespace Client::Terrain
{
struct TerrainLighting;
struct WorldTerrain;
} // namespace Client::Terrain

namespace Client::Particles
{
class FrameSprites;
class Particles;
} // namespace Client::Particles

namespace Client::Effects
{
struct ModelEffect;
} // namespace Client::Effects

namespace Client::Actors
{
struct CharacterStepContext;
} // namespace Client::Actors

namespace Client::Kernel
{
struct ModelEffectArguments;
struct ActorState;
class WorldState;
} // namespace Client::Kernel

namespace Client::Effects
{

// The arrow types 0x53d9a0 creates: 0xdb..0xe3, 0xf7..0xf9, 0x10e, 0x111 and 0x11c.
[[nodiscard]] bool IsArrowEffect(std::int16_t type);

// 0x518cf0 with an arrow type: the prefix (Effects::ModelEffects::Begin) and the type's creation (0x51c43b, 0x51a41e,
// 0x51a1e6..0x51a405, 0x51be81 and the exit 0x51a1b6, which copies the own record's +0xf04 to +0x8c). 0x53d9a0 calls it
// for each hand; the 0xdb with subtype 3 or 4 is created by the siege (0x42c319 and 0x42c4e0, CL9.5) and the 0xe1/1 by
// the fortress and Crywolf (0x42fd32 and 0x43252d); its 0x610/13 keeps the effect as owner. With subtype 2 (skill
// 0x34), first the 0x105 with the arrow as owner and four 0x610 joint attempts (0x51a08a). With the table full,
// nothing and without rand().
[[nodiscard]] Core::Expected<void, std::string> CreateTheArrowEffect(const Kernel::ModelEffectArguments& arguments,
                                                                     Kernel::WorldState& world, Crt::CrtRandom& random,
                                                                     glm::vec3* callerAngles = nullptr);

// 0x53e2b0(c, o, target, slot, flag, skill), as the attack step calls it (0x50377d, 0x5037d0, 0x504060 and 0x5040a9)
// and the monsters' case 0x4f814e (0x4f816f). `actor` is c and o; `target`, the target (nullptr without one), which
// only the 0x13a effect of skill 0x2e reads, as its owner. With skill 0x34, 0x33 or 0x2e (0x53e741), one 0x53d9a0 shot
// and the volley count (the own record's +0xf04, which it opens and closes), and it returns. With the flag at 1 (skill
// 0x18 without a target, 0x53e2e0), three shots with o's +0x24 at a, a + 15 and a − 15 (four, at a + 5, a + 15, a − 5
// and a − 15, with 0xa24 or 0xa25 in +0x2b8: +0x24 ends at a + 15, Q-C1218) and the volley count. Otherwise, one shot.
// Each 0x53d9a0 shot: the weapons (with the hero, the own record's, which it opens and closes; with another, those at
// +0x2b8 and +0x2d8), the skill in [0x5767948] (WorldRuntime::lastShotSkill) and one arrow per hand with a bow or
// crossbow, each with its creation; with the 0x2e, only the 0x13a effect, with its joint 0x632/4 (0x51e01a). After the
// flag at 1 or a shot, with 0xa25 in +0x2b8, the hand's shine (0x53e642). What it did before a stop stays, with its
// rand().
[[nodiscard]] Core::Expected<void, std::string> FireTheArrow(Kernel::ActorState& actor, Kernel::ActorState* target,
                                                             std::size_t slot, std::int32_t skillIndex,
                                                             std::uint8_t flag, std::uint8_t skill,
                                                             const Actors::CharacterStepContext& context);

// 0x518cf0 of the arrow 0xe2, with 0x4f7f67's native fields.
[[nodiscard]] Core::Expected<void, std::string> CreateAquagoldArrow(Kernel::ActorState& owner, const glm::vec3& angles,
                                                                    const Actors::CharacterStepContext& context);

// 0x51fff0 with the combat burst 0x51fe10 and the sound 0x40 of 0x542a7c.
[[nodiscard]] Core::Expected<void, std::string> CreateCombatBurstAt(const glm::vec3& position,
                                                                    const Actors::CharacterStepContext& context);

// 0x51fff0(position, 0): eight groups with 0x51fe10 and a selector per 0xc8/0xc9 stone attempt, first /1 and then /0.
[[nodiscard]] Core::Expected<void, std::string> ScatterCombatBurstAt(const glm::vec3& position,
                                                                     const Actors::CharacterStepContext& context);

// What an arrow's step reads and writes outside its slot: the character pool, the own record and the hero (0x520170),
// the particles (0x53fe70), the frame's sprites (0x5491e0), the dynamic light (0x5f9780) and the world clock
// (0x5767b88). CL9 milestone 4: the ground height (0x5f93c0, the arrow 0xdb/3 and /4; it reads the attributes from
// `terrain`), the sounds (0x41ed10, CL10: they stay in WorldRuntime::lastSoundId) and what the skill arrows' hit sends
// (0x517a20, the C1 10). Empty, the height stops, the sound is not requested and the packet is not sent (its rand()
// are).
struct ArrowStepContext
{
    Kernel::WorldState& world;
    Crt::CrtRandom& random;
    Particles::Particles* particles = nullptr;
    Particles::FrameSprites* sprites = nullptr;
    Terrain::TerrainLighting* lighting = nullptr;
    float worldClock = 0.0f;
    // The map's load (0x7d0d348); without it, an empty one.
    const Terrain::WorldTerrain* terrain = nullptr;
    std::function<double(float, float)> terrainHeight{};
    std::function<void(Audio::SoundId)> soundRequest{};
    std::function<void(Protocol::Outgoing)> send{};
};

// 0x5208a0 on a live arrow: the local light's rand() (0x5208c5), its type's case, the hit (0x520170) and the common
// tail (0x52cb98: the animation with `skeleton`, the turned velocity and one life less), which also runs if the hit
// turned it off. It stops, with what came before and its rand(), where it asks for something unported.
[[nodiscard]] Core::Expected<void, std::string>
StepArrow(Effects::ModelEffect& effect, const Models::Skeleton* skeleton, const ArrowStepContext& context);

// 0x520170(effect) outside the arrow's step: the 0xf8's 0x60f/0xb with skill 1 puts it at its position and makes it
// hit from the joints' step (0x53ae6d).
[[nodiscard]] Core::Expected<void, std::string> HitTheArrow(Effects::ModelEffect& effect,
                                                            const ArrowStepContext& context);
} // namespace Client::Effects
