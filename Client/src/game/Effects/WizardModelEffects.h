#pragma once

#include "Core/Expected.h"
#include "Audio/SoundId.h"

#include <glm/ext/vector_float3.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>

namespace Client::Data::Terrain
{
struct TerrainAttributes;
}

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
} // namespace Client::Terrain

// CL11, wizards batch: the model effects of the spells (Data\Skill\Ice, Fire, Poison, Circle, Magic, Storm, Laser, Saw,
// Bone and Snow, 0x615700) in 0x518cf0 (the creation), 0x5208a0 (the step) and its common tail (0x52cb98). They are
// called by 0x4fd5f0's ability cases (Skills/AttackSkillCases), the monsters' attacks (0x4f5b70, 0x439bd0, 0x439cd0,
// 0x43a7a0 and 0x429110, Monsters/MonsterAttackStep), 0x4f97b0 (Skills/AttackFrameSkills), 0x4e6380's flags
// (Actors/ActorEffectFlags) and the death (0x4f5660, Combat/ActorCombat). 06, "Los efectos de los magos".
namespace Client::Kernel
{
struct ModelEffectArguments;
struct JointRequest;
} // namespace Client::Kernel

namespace Client::Particles
{
class Particles;
class FrameSprites;
} // namespace Client::Particles

namespace Client::Effects
{
struct ModelEffect;
class ModelEffects;

// The types (0x615700 loads their models; the 0xbd and the 0xc7 have none and 0x52d620 does not draw the 0xbd).
inline constexpr std::int16_t ShadowRainEffect = 0xbd;     // 0x51bc43: ten falling joints 0x60f/0x10.
inline constexpr std::int16_t IceEffect = 0xc1;            // Skill\Ice01
inline constexpr std::int16_t FireEffect = 0xc2;           // Skill\Fire01 (the meteor and its relatives)
inline constexpr std::int16_t PoisonEffect = 0xc3;         // Skill\Poison01
inline constexpr std::int16_t HellGateEffect = 0xc5;       // Skill\HellGate
inline constexpr std::int16_t PlainSkillEffect = 0xc7;     // 0x51abbf: life 10, no model.
inline constexpr std::int16_t InnerCircleEffect = 0xcb;    // Skill\Circle01
inline constexpr std::int16_t OuterCircleEffect = 0xcc;    // Skill\Circle02
inline constexpr std::int16_t FollowingMagicEffect = 0xcd; // Skill\Magic01
inline constexpr std::int16_t MagicEffect = 0xce;          // Skill\Magic02
inline constexpr std::int16_t StormEffect = 0xcf;          // Skill\Storm01
inline constexpr std::int16_t LaserEffect = 0xd0;          // Skill\Laser01
inline constexpr std::int16_t SawEffect = 0xd4;            // Skill\Saw01
inline constexpr std::int16_t FirstBoneEffect = 0xd5;      // Skill\Bone01
inline constexpr std::int16_t SecondBoneEffect = 0xd6;     // Skill\Bone02
inline constexpr std::int16_t SnowEffect = 0xd7;           // Skill\Snow01
inline constexpr std::int16_t FirstSnowStoneEffect = 0xd8; // Skill\Snow02, the 0xd7's pebble
inline constexpr std::int16_t SecondSnowStoneEffect = 0xd9;

// What the step of the 0xc1/1 and /2, the 0xc5, the 0xcd, the 0xbd, the 0xcb/1 and 0x518b60 (the 0xc2/1 and the 0xd7)
// read from the owner (+0x108): the live byte (+0x0), the type (+0x2), the position (+0x10), +0x78, the action
// (+0x111), +0x16c and +0x1bc. The owners the ported callers create are characters (Kernel::ActorState).
struct WizardOwnerView
{
    bool alive = false;
    std::uint16_t type = 0;
    glm::vec3 position{};
    std::uint32_t flags78 = 0;
    std::uint8_t action = 0;
    bool seen = false;
    std::uint8_t byte1bc = 0;
};

// What the creations and steps ask for outside the effect. Without a service its branch reaches, the case cuts at its
// address.
struct WizardEffectServices
{
    // 0x53fe70, with the world clock (0x5767b88) that its creations and the 0xc1/1's sine read.
    Particles::Particles* particles = nullptr;
    float worldClock = 0.0f;
    // 0x5491e0.
    Particles::FrameSprites* sprites = nullptr;
    // 0x5f9780 sobre 0x7b4ccc4.
    Terrain::TerrainLighting* lighting = nullptr;
    // 0x518cf0 for the children the steps create (the pebbles 0xc8/0xc9 and 0xd8/0xd9).
    ModelEffects* effects = nullptr;
    // 0x5f93c0.
    std::function<double(float, float)> terrainHeight{};
    // 0x7d0d348 (0x5f82a0 and 0x5f82c0).
    const Data::Terrain::TerrainAttributes* terrainAttributes = nullptr;
    // 0x6c9dd0.
    const std::int32_t* map = nullptr;
    // 0x7d325c8
    float* cameraShake = nullptr;
    // 0x4294b0, the byte at 0x7239ec.
    bool siegeActive = false;
    // 0x41ed10(id, 0, 0).
    std::function<void(Audio::SoundId)> sound{};
    // 0x530860.
    std::function<Core::Expected<void, std::string>(const Kernel::JointRequest&)> joint{};
    // The owner (+0x108) read live; nothing if it is not a character.
    std::function<std::optional<WizardOwnerView>(const void*)> ownerOf{};
    // [0x743a170], the published hero.
    const void* hero = nullptr;
    // 0x517a20(+0x89, the position, `radius`, the byte +0x8c, +0x8a, 0) on the effect (Actors/SkillHits).
    std::function<Core::Expected<void, std::string>(const ModelEffect&, float)> skillHits{};
    // The own record (0x7681eb4): the count of open accesses and its +0xf04.
    std::uint8_t* ownRecordDepth = nullptr;
    std::uint8_t* ownRecordCount = nullptr;
    // 0x52cc21: the tail publishes the effect's action in the models table before animating.
    std::function<void(std::uint16_t, std::uint8_t)> publishAction{};
    // 0x518cf0 with a type from another batch: the 0xf6/2 that the 0xc2/6..8 drops on falling (0x524d38), with the
    // meteor as owner (+0x108). Effects::WizardEffectServicesOf gives it with Effects::CreateBlastModelEffect.
    std::function<Core::Expected<void, std::string>(const Kernel::ModelEffectArguments&)> otherEffect{};
    // 0x524a8d..0x524b21: the 0xc2/4 touching the ground at (x, y) pushes the pool's 400 characters (0x438 each) that
    // are alive, seen (+0x16c), not the hero, not dying (+0x389 at 0) and within 200 (the root of dy² + dx² on the x87,
    // with dx and dy stored in float; `fcomp` and `test ah,0x41`): the 0x20a with Fenrir (+0x318 at 0x1c37) goes to
    // 0x4e9cd0 (the hand hit action), the 0x20a without it to 0x4ea030 with 0xc2 and the others with 5. It goes on with
    // all of them and draws no rand(); then the fire falls on the ground anyway.
    std::function<Core::Expected<void, std::string>(float, float)> pushAround{};
};

// Whether 0x518cf0 has `type`'s case in this batch.
[[nodiscard]] bool IsWizardModelEffect(std::int16_t type);

// 0x518cf0(arguments) with a type of the batch: the prefix (ModelEffects::Begin) and its case, with the rand(),
// particles, light, joints and own record it asks for. With the table full, nothing and no rand(). The 0xc1/1 and /2
// write (0.4f, 0.3f, 0.2f) into the caller's light (`callerLight`, the fourth argument, 0x51acd2). A subtype with no
// case exits through 0x51db73 with the prefix and what the case wrote before checking it. Another type, or a branch
// that asks for something MuRet does not have (CL11 of another batch), cuts with its address.
[[nodiscard]] Core::Expected<std::optional<std::size_t>, std::string>
CreateWizardModelEffect(ModelEffects& effects, const Kernel::ModelEffectArguments& arguments, Crt::CrtRandom& random,
                        const WizardEffectServices& services, glm::vec3* callerLight = nullptr);

// 0x5208a0 on an effect of the batch: the local light's first rand() (0x5208c5), the type's case and the common tail
// (0x52cb98): the animation, 0x51fc80 (unrotated with the 0xd4) and one life less; at 0 or less, it dies. The tail runs
// even if the case turned it off. `skeleton` is the model's (or nullptr). Leaves the position before the step in
// previousPosition.
[[nodiscard]] Core::Expected<void, std::string> StepWizardModelEffect(ModelEffect& effect,
                                                                      const Models::Skeleton* skeleton,
                                                                      Crt::CrtRandom& random,
                                                                      const WizardEffectServices& services);
} // namespace Client::Effects
