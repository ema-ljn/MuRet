#pragma once

#include "Core/Expected.h"
#include "Kernel/ModelEffectArguments.h"
#include "Models/ObjectState.h"
#include "Models/Pose.h"
#include "Models/Skeleton.h"

#include <glm/ext/vector_float3.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace Client::Data::Terrain
{
struct TerrainAttributes;
}

namespace Client::Crt
{
class CrtRandom;
}

namespace Client::Particles
{
class FrameSprites;
} // namespace Client::Particles

// The model effects (CL7.6 A): 0x518cf0's table (200 slots of 0x1d4 bytes). The dragon keeps the historical vectors;
// arrival, teleport and the Noria tower were checked statically against the ASM. For the game's step and its CRT, use
// Effects/GameModelEffects.h.
namespace Client::Effects
{

inline constexpr std::size_t ModelEffectCount = 200;
// The dragon that flies in the login, with its model (Assets::DragonModel) and the scale 0x606660 passes it.
inline constexpr std::int16_t DragonEffect = 0xb4;
inline constexpr float DragonScale = 1.6f;
// The arrival effect: the F3 03 reply creates it twice on the hero (06, "El héroe").
inline constexpr std::int16_t ArrivalEffect = 0x61f;
inline constexpr std::int16_t TeleportEffect = 0x5c6;
inline constexpr std::int16_t PiercingShotGlowEffect = 0x621;
// The butterfly (0x51913a): emitted by the 0x3c of maps 54 and 33 (0x425790), with the object as owner.
inline constexpr std::int16_t ButterflyEffect = 0xae;
// Those of the Dungeon and Lost Tower: the stone that falls from map 1's 0x34 (DungeonStone), the fire jet of map 4's
// 0x18 (no model: only the floor pass) and the two pebbles the jet throws (Stone01/02).
inline constexpr std::int16_t FallingStoneEffect = 0xda;
inline constexpr std::int16_t FlameJetEffect = 0x5de;
inline constexpr std::int32_t FlameColumnSubtype = 3; // the 0x5de/3 of the 0x1a4's right hand (0x441b7c)
// The stones of the 0x10a and the 0x10b (0x51bafb, 0x52531d): the damaging attack on Blood Castle's 0x18e statues drops
// the 0x10b (0x504267), which jumps, falls on the terrain and tilts.
inline constexpr std::int16_t FirstDebrisEffect = 0x10a;
inline constexpr std::int16_t DebrisEffect = 0x10b;
inline constexpr std::int16_t FirstStoneChipEffect = 0xc8;
inline constexpr std::int16_t SecondStoneChipEffect = 0xc9;
inline constexpr std::int32_t RisingStoneChipSubtype = 1;
// The pebbles' /2 (that of the 0xcf storms, 0x522f17 and 0x5231a1 → 0x52b14d): born as the /0 (0x51b72b) and rises as
// the /1, turning 20 in z each step (0x523a44).
inline constexpr std::int32_t SpinningStoneChipSubtype = 2;
// The pebbles' /0xc (that of the column 0x5de/3, 0x52afe1): born as the /0 with the scale ((& 3) + 15) · 0.1f
// (0x51b509..0x51b5d6) and falls and lights up as the /0 (0x523980, 0x523a3a, 0x523ad2).
inline constexpr std::int32_t ColumnStoneChipSubtype = 0xc;
// The big pieces (Data\Skill\BigStone1 and 2): with subtype 0 the 0x16b's death drops them (0x4f59ca), shifted
// rand() % 128 − 64 in x and y and rand() % 180 in z before the pebbles' path (0x51b659); the /5 is from the blast
// batch (Effects/BlastModelEffects) and the /10 from the falling stone (CreateGroundStoneChip).
inline constexpr std::int16_t FirstSplinterEffect = 0xe5;
inline constexpr std::int16_t SecondSplinterEffect = 0xe6;
// The ice piece 0xca (Data\Skill\Ice02.bmd, 0x615d36): the ice monster 0x161 drops ten when it finishes dying
// (0x52e810, CL9 milestone 3). Its step is the pebbles' (0x523974).
inline constexpr std::int16_t IceShardEffect = 0xca;
// Icarus's (0x605d70 and 0x6061c0): the cloud 0xbb (Object11\cloud) over the hero and the 0x5ac, with no model, which
// rises dropping particles 0x5ac/1.
inline constexpr std::int16_t IcarusCloudEffect = 0xbb;
inline constexpr std::int16_t IcarusFlareEffect = 0x5ac;
// The full set's 0x5ac/3 (0x50fdc3): every 100 draws of the character with +0x358, with it as owner. It does not move
// by itself: every 15 steps it shifts around the owner and drops a joint 0x619/9 (0x52a9ad).
inline constexpr std::int32_t FullSetFlareSubtype = 3;
// The 0x5ac/1 and /2 of the arriving skills (0x4cdd84 with skill 0x30 and 0x4cdc88 with 0x35, Skills/SkillReplies),
// with the target as owner: they live while the owner is alive with its +0x78 flag (0x10 the /1, 0x400 the /2). The /1
// drops particles 0x5ac/4 each step (0x52a7b7) and the /2, every 50, shifts around the owner and drops a joint 0x619/9
// (0x52a8a7).
inline constexpr std::int32_t SwellFlareSubtype = 1;
inline constexpr std::int32_t GuardFlareSubtype = 2;
// Aida's (0x42593f): the 0x46 creates the 0xb3, a model Aida does not load (in Atlans, Object8\Fish02), which wanders
// near where it was born and drops particles 0x5ac/9.
inline constexpr std::int16_t AidaWispEffect = 0xb3;
// Castle Siege's (0x42c6b0): object 79 of map 30 creates the 0x135 (Skill\TowerGatePlane), a plane that rises and falls
// with the clock and is drawn twice, the second time mirrored.
inline constexpr std::int16_t GatePlaneEffect = 0x135;
// The fading decal (0x61d, no model: only the floor pass draws the /1). Crywolf's 0x38 creates the /3, which drops
// orbits 0x60f/10 (0x52b933).
inline constexpr std::int16_t FadingDecalEffect = 0x61d;
inline constexpr std::int32_t CrywolfDecalSubtype = 3;
// The wave (0x6eb, Effect\ShockWave.jpg, no model: only the floor pass draws it). The dark horse's gallop in Icarus
// creates the /1 (0x44431b), which grows and shakes.
inline constexpr std::int16_t ShockWaveEffect = 0x6eb;
inline constexpr std::int32_t GallopShockWaveSubtype = 1;
// The dark horse's stomp (0x4453e0, 0x444010's action 3): every 10 logic steps the wave 0x6eb with subtype 0, which
// shrinks, and six stones 0x118 or 0x119 (Skill\groundStone and groundStone2, 0x615700) around. Each stone drops in its
// steps 33 to 36 the dust 0x5f2/11 and a pebble 0xc8/0xc9 with subtype 10 (0x5275ca).
inline constexpr std::int32_t StompShockWaveSubtype = 0;
inline constexpr std::int16_t FirstGroundStoneEffect = 0x118;
inline constexpr std::int16_t SecondGroundStoneEffect = 0x119;
inline constexpr std::int32_t GroundStoneChipSubtype = 10;
// Those of the Fenrir draw (0x60277b): the ray 0x14d (Effect\lightning_type01.bmd), born at a Fenrir bone with its
// sprite 0x5ac and flickering on and off within a few steps, and the decal 0x14e (eff_lightinga01..05, no model:
// 0x52d8f2 draws it on the floor), which changes texture every 200 ms of timeGetTime().
inline constexpr std::int16_t LightningEffect = 0x14d;
inline constexpr std::int16_t LightningDecalEffect = 0x14e;
// The colored decal (0x61e, Effect\Magic_Ground2 in the floor pass). On equipping a pet outside the safe zone, 0x5bca46
// creates the /1 over the hero, with it as owner: each step drops three joints 0x619/1 that go down toward the owner
// (0x518900).
inline constexpr std::int16_t ColoredDecalEffect = 0x61e;
inline constexpr std::int32_t LevelUpDecalSubtype = 0;
inline constexpr std::int32_t PetSummonSubtype = 1;
// The /3, which skill 0x1c creates over the target and the summons 0x1e..0x24 over their caster (0x50470f and
// 0x4fdf1e): its step drops three joints 0x619/3 (0x518900), which rise toward it like the 0x619/1.
inline constexpr std::int32_t SummonDecalSubtype = 3;
// The earthquake (the EarthQuake family): the 0xfa, which the dark horse's action 3 creates with the horse as owner and
// the state (+0x88) at 2 (0x4453e0) and which is not drawn with a state other than 0 (0x52d400). Its step drops at life
// 11 the explosion 0x5ed/0 and the cracks 0xfb..0x100, and at 10 the 0x101 and 0x102 (Data\Skill\EarthQuake01..08,
// which 0x602e90 draws). The first three (0xfd, 0xfb and 0xfc) take the 0xfa as owner; the others, the 0xfa's owner.
inline constexpr std::int16_t EarthquakeEffect = 0xfa;
inline constexpr std::int16_t EarthquakeBurstEffect = 0x103;
inline constexpr std::int16_t FirstEarthquakeCrack = 0xfb;
inline constexpr std::int16_t LastEarthquakeCrack = 0x102;
// +0x74 at 0x80: the cracks 0xfc, 0xff and 0x102 with subtype 1 (0x51a850); 0x6002e0 draws them through 0x4ef0c0 with
// 0x82 (0x6004aa).
inline constexpr std::uint32_t CrackRenderFlags = 0x80;

struct ModelEffect;
// +0x108, the owner, with what the earthquake's and its cracks' steps read live from it: the WORD +0x2 (its type)
// and +0x1c (its angles). The original stores a pointer to an object of any table; MuRet, pointers to those fields,
// which must outlive the effect (the tables' slots do not move, and a reused slot is read as the original reads it). A
// helper (Kernel::HelperState) gives `type` and `angles`; another effect, `effect` (its ModelEffect::type) and its
// angles. Without any of the three, there is no owner.
struct ModelEffectOwner
{
    const std::uint16_t* type = nullptr;
    const ModelEffect* effect = nullptr;
    const glm::vec3* angles = nullptr;
    // CL11 (Effects/BlastModelEffects): +0x10 (the position), +0 (the live byte) and +0x16c (whether it was drawn) of
    // an owner that is not another effect, as the steps of the 0xe4, the 0xe5/5, the 0xec, the 0xf6, the 0x109 and the
    // 0x113 read them live.
    const glm::vec3* position = nullptr;
    const bool* alive = nullptr;
    const bool* seen = nullptr;
};

// The fields shared by the 0x1d4-byte effects.
struct ModelEffect
{
    bool alive = false;                // +0x0
    bool seen = false;                 // +0x16c, from this slot's last draw
    std::int16_t type = 0;             // +0x2, the model
    std::int32_t subtype = 0;          // +0x4, the effect's branch
    Models::ObjectTransform transform; // +0xc the scale, +0x10 the position, +0x1c the angles
    std::int32_t life = 0;             // +0x60, steps left
    glm::vec3 velocity{};              // +0xcc, per step, rotated by the angles
    float speed = 0.0f;                // +0xd8, keys per step
    float turnPending = 0.0f;          // +0xdc: on the next far step, it turns around
    float turnDirection = 0.0f;        // +0xe4: 1 or −1, the turn direction
    float flagEc = 0.0f;               // +0xec
    std::uint8_t state = 0;            // +0x88: 0 and 1 turn, 2 goes straight
    // +0x84: at the start of each step, the state's roll goal (30, −30 or 0); on turning around, the yaw the ±60° turn
    // is compared with. Both uses are the original's.
    float reference = 0.0f;
    Models::AnimationState animation;   // +0x111 the action, +0x114 the frame, +0x118 the previous one
    glm::vec3 light{};                  // +0xf4
    float alpha = 1.0f;                 // +0x174
    Models::ObjectState objectState;    // +0x58, +0x64 and +0x68
    std::uint16_t disturbanceState = 0; // +0x8a, 0x437860's guard for effect 9
    bool vertexLight = true;            // +0xe8
    glm::vec3 point40{};                // +0x40..+0x48, native trail vector, distinct from the interpolation.
    // +0x4c..+0x54: the other point the joint 0x618/0xb of the 0x11b/1 follows (0x51c82d, 0x538256).
    glm::vec3 point4c{};
    glm::vec3 start{}; // +0x17c, the start
    // MuRet's own: the position before the last step, to draw between two steps (D-C4).
    glm::vec3 previousPosition{};
    std::int32_t textureFrame = 0;           // +0x1c4: signed selector of the 0x14e decal at 0x52d8f2.
    std::uint32_t textureTime = 0;           // +0x1c0: the timeGetTime() of the 0x14e decal's last change.
    const std::int16_t* itemType = nullptr;  // the effect's +0x108 → the durable owner's +0x8c, for 0xf4/0xfa.
    const std::uint8_t* itemLevel = nullptr; // +0x108 → +0x8e; do not infer from ActorState.
    // +0x108 other than 0: the effect has an owner. The 0x135 plane only moves with an owner (0x529745).
    bool hasOwner = false;
    // +0x108 as an opaque owner, for the steps that read it outside ModelEffects: the ActorState of the 0x5ac/3's
    // character.
    void* ownerContext = nullptr;
    // +0x108 with what the earthquake 0xfa and its cracks read live (OwnerTypeOf). The prefix overwrites it.
    ModelEffectOwner owner{};
    // +0x28..+0x30: the 0xfa copies its angles there when born (0x51a66d) and its step rotates the speed with them. The
    // prefix does not touch them.
    glm::vec3 copiedAngles{};
    // +0x74: the prefix leaves it at 0; CrackRenderFlags in the subtype 1 cracks.
    std::uint32_t renderFlags = 0;
    // +0x89: the byte of 0x518cf0's ninth argument, the skill's place in the own record that 0x520170 reads on
    // collision. +0x1bd: the tenth's, which nothing ported reads. +0x8c: the own record's +0xf04 count that the arrows'
    // exit (0x51a1b6) copies between the opening and the closing. +0x100: the prefix sets it to 0; with skill 0x34, the
    // steps it has left without colliding (0x52045c).
    std::uint8_t byte89 = 0;
    std::uint8_t byte1bd = 0;
    std::uint16_t word8c = 0;
    std::int32_t dword100 = 0;
    // +0x90 and +0x1b0: the impact threshold and the destination of the castle's projectiles and of Crywolf's 0xee.
    std::uint8_t byte90 = 0;
    glm::vec3 siegeTarget{};
    // +0x78: 0x6002e0's tail 0x602365. 0x518cf0's prefix does not reset it.
    std::uint32_t flags78 = 0;
};

// The owner's WORD +0x2, read live, or nothing without an owner.
[[nodiscard]] std::optional<std::uint16_t> OwnerTypeOf(const ModelEffectOwner& owner);

// ModelEffects::Begin on another table of the same format: the copy the floor pass repeats (0x52dc20) creates there
// what the original creates in its own (the 0x61e/7 and /9 of 0x52e04c and 0x52e0a2).
[[nodiscard]] std::optional<std::size_t> BeginModelEffect(std::span<ModelEffect> table,
                                                          const Kernel::ModelEffectArguments& arguments);

class ModelEffects
{
public:
    // 0x51c754: the 0x145/2 whirlwind of the 0x1ae's trail, without RNG.
    std::optional<std::size_t> CreateCrywolfTideEffect(const Kernel::ModelEffectArguments& arguments);
    // 0x51c129..0x51c36d: the waves 0x114/2..6, no rand(). The 0x114's common part (life 20, +0xe4 at 0.1f, +0x64
    // at −2, +0x68 at 1, the angles at 0, the start at the position and z + 50) and the subtype's: the /2, /3 and /4
    // (0x51c31f) live 15 with +0xe4 at 0.01f and the angles' x at 90; the /5 and /6 (0x51c2d0) live 10 with +0xe4 at
    // 0.08f and x at −90. All: the key's scale (+0x8a, MOVSX) times 0.05f, the key at −1, z − 50 and the angles' z the
    // argument's. The /0 and /1 drop joints (Effects/WaveEffects); another subtype, nothing. Full, none.
    std::optional<std::size_t> CreateWaveEffect(const Kernel::ModelEffectArguments& arguments);
    // 0x51efa5: Piercing Shot's 0x621/0 glow, life 10 and rotated shift, without CRT.
    std::optional<std::size_t> CreatePiercingShot(const Kernel::ModelEffectArguments& arguments);
    // 0x51c8e8..0x51c951: the 0x115 of any subtype, no rand(): scale 2, life 6 with the /1 and 10 with the
    // others, +0x64 at −2, speed (0, −60, 0), the start at the position and z + 130. Full, none.
    std::optional<std::size_t> CreatePiercingEffect(const Kernel::ModelEffectArguments& arguments);
    // 0x51c06c: BD0C's 0xee projectile, without CRT; +0x1b0 receives the angles as destination.
    std::optional<std::size_t> CreateCrywolfMissile(const Kernel::ModelEffectArguments& arguments);
    // 0x51edc4/0x51ee54: the fireworks 0x60d/0x60e, with real pools and no implicit sounds.
    std::optional<std::size_t> CreateFireworkEmitter(const Kernel::ModelEffectArguments& arguments);
    std::optional<std::size_t> CreateFireworkFlare(const Kernel::ModelEffectArguments& arguments,
                                                   Crt::CrtRandom& random);
    // 0x51f7f9: crater 0x636, subtypes 0/1/2; consumes no RNG and a full pool keeps its arguments.
    std::optional<std::size_t> CreateCraterEffect(const Kernel::ModelEffectArguments& arguments);
    // 0x51f4ad..0x51f5fd: the wave 0x6eb of any subtype: life 30 and scale 20, and then the /1 and /2 life 20 and
    // (rand() % 10 + 10) · 0.1f, the /3 life 15 and 1, the /4 life 10 and (rand() % 6 + 6) · 0.1f, the /5 20 and 9, the
    // /6 50 and 9, the /7 10 and 1 and the /8 10 and (rand() % 10 + 10) · 0.2f. Only the /1, /2, /4 and /8 draw a
    // rand(), with room.
    std::optional<std::size_t> CreateCastleShockWave(const Kernel::ModelEffectArguments& arguments,
                                                     Crt::CrtRandom& random);
    // 438a1a→5191be: tipo9/0, ownerNULL, escalaargumento0; primer libre, lleno sinCRT.
    std::optional<std::size_t> CreateKalimaEffect(const glm::vec3& position, const glm::vec3& angles,
                                                  const glm::vec3& light, Crt::CrtRandom& random);
    // 0x518cf0(type, position, angles, light, 0, owner, −1, 0, 0, 0, scale) in the first slot without the live byte, or
    // nothing if all have it. A scale of 0 or less becomes 0.9 (0x518d3c). For the 0xb4: the owner's position with z
    // 3400 higher (0x518fc9), the angles with the roll at 0 (0x518fb3), the light in white (the exit 0x51a97b
    // overwrites `light`), life 1000, speed (0, −35, 0), the animation's at 1, +0xdc, +0xe4 and +0xec at 1, per-vertex
    // light and the start at the position. For the 0x61f with subtype 0: the position, angles and light as given, 20
    // steps of life and the animation at 0.3, no rand(). The 0x5c6 keeps that prefix with life 10 (0x51e4f6), also no
    // rand(). Noria has its separate creator with CRT; the other types are not created.
    std::optional<std::size_t> Create(std::int16_t type, const glm::vec3& position, const glm::vec3& angles,
                                      const glm::vec3& light, float scale);
    // Accepts subtype 0 of the three types above. Keeps the prefix's arguments and the physical fields it does not
    // write when recycling the slot.
    std::optional<std::size_t> Create(const Kernel::ModelEffectArguments& arguments);
    // 0x51c36e/0x51c38a: the Noria tower's effects 322..324; 322/323 set sourceLight to zero.
    std::optional<std::size_t> CreateNoriaEffect(std::int16_t type, const glm::vec3& position, const glm::vec3& angles,
                                                 glm::vec3& sourceLight, Crt::CrtRandom& random);
    // 0x518cf0(0xae, position, angles, light, subtype, owner, −1, 0, 0, 0, 0) → 0x51913a: the common prefix (+0x8a
    // at −1), hidden in scene 4 (+0x58 = −2), life 250, animation at 0.3, no per-vertex light, the state with
    // rand() & 1, the speed (0, owner's scale · −5, 0) and the scale 0.9 with subtype 3 or 0.25 with the others. One
    // rand(); full, none.
    std::optional<std::size_t> CreateButterflyEffect(const glm::vec3& position, const glm::vec3& angles,
                                                     const glm::vec3& light, std::int32_t subtype, float ownerScale,
                                                     bool selectionScene, Crt::CrtRandom& random);
    // 0x518cf0(0xda, position, angles, light, 0, 0, −1, 0, 0, 0, 0) → 0x51b80a: the common prefix (+0x8a at −1) and six
    // rand(): the life (& 15) + 24, the scale ((& 7) + 6) · 0.1f, the fall −(& 3) in +0xe4 and the shift
    // ((& 63) − 32, −((& 31) + 50), (& 127) + 200), rotated by the angles and added to the position in float. Full,
    // none and no rand().
    std::optional<std::size_t> CreateFallingStone(const glm::vec3& position, const glm::vec3& angles,
                                                  const glm::vec3& light, Crt::CrtRandom& random);
    // 0x518cf0(0x10a or 0x10b, position, angles, light, 0, 0, −1, 0, 0, 0, 0) → 0x51bafb, which exits at 0x51bc42
    // without the common exit: the prefix (no owner, +0x8a at −1), z 50 more, the push (0, ((& 127) + 32) · 0.1f, 0),
    // the life (& 15) + 32, the scale ((& 3) + 8) · 0.1f, the angles' z at % 360 to rotate the push into the speed, the
    // fall +0xe4 at % 5 + 2 and, with the 0x10b, subtype 1 and another % 5 more in the fall; at the end, the three
    // angles at % 360. Nine rand() with the 0x10b, eight with the 0x10a. Full, none and no rand(). Another subtype (the
    // 0x10b/2, /3 and /4 and 0x4ee770's 0x10a/4) stays in +0x4 and exits with eight rand().
    std::optional<std::size_t> CreateDebris(std::int16_t type, const glm::vec3& position, const glm::vec3& angles,
                                            const glm::vec3& light, Crt::CrtRandom& random, std::int32_t subtype = 0);
    // 0x518cf0(0x5de, …, 0, 0, −1, 0, 0, 0, 0) → 0x51e7d8 with subtype 0: the common prefix, life 40 and the own record
    // opened and closed (copies its +0xf04 to +0x8c, which only the hero-owner branch reads, and the closing that
    // leaves the count at 0 spends its 3848 rand()). Full, none.
    std::optional<std::size_t> CreateFlameJet(const glm::vec3& position, const glm::vec3& angles,
                                              const glm::vec3& light, std::uint8_t& ownRecordDepth,
                                              Crt::CrtRandom& random);
    // 0x518cf0(0x5de, …) with subtype 3 → 0x51ecaa: the column of the 0x1a4's right hand (0x441b7c and 0x441c4c):
    // Begin's prefix and life 15, no rand(). Another type or subtype, or the table full, none.
    std::optional<std::size_t> CreateFlameColumn(const Kernel::ModelEffectArguments& arguments);
    // 0x518cf0(0x5de, point, angles, light, 0, owner, key, place, 0, 0, 0) → 0x51e7d8 with subtype 0, the Flame's jet
    // (0x500531, Skills/AttackSkillCases): Begin's prefix (the owner in +0x108, the key in +0x8a and the skill's place
    // in +0x89), life 40 (0x51e7f9) and the own record opened, its count +0xf04 (`ownRecordCount`) as a WORD in +0x8c
    // (0x51ea9f, MOVZX) and closed (0x51eab6: with `ownRecordDepth` at 0, its 3848 rand()). The step's hero-owner
    // branch (0x52ae2a) reads +0x89, +0x8a and +0x8c. Another type or subtype, or the table full, none, no rand() and
    // the record untouched.
    std::optional<std::size_t> CreateOwnedFlameJet(const Kernel::ModelEffectArguments& arguments,
                                                   std::uint8_t& ownRecordDepth, std::uint8_t ownRecordCount,
                                                   Crt::CrtRandom& random);
    // 0x518cf0(0xc8 or 0xc9, or 0xea, 0xeb, 0xe5 or 0xe6 with subtype 0, …, 0, 0, −1, 0, 0, 0, 0) → 0x51b72b: the
    // common prefix and five rand(): the push ((& 255) + 64) · 0.1f in y, the life (& 15) + 32, the scale
    // ((& 3) + 8) · 0.1f, the yaw rand() % 360 and the fall (& 15) + 8 in +0xe4; the speed is the push rotated by the
    // angles with that yaw. The /1 of 0x520119 has zero push and skips that first draw; the /0xc of the 0xc8 and the
    // 0xc9 (0x51b509..0x51b5d6) draws the same five with the scale ((& 3) + 15) · 0.1f. The 0xe5 and the 0xe6 draw
    // three more before (0x51b66b..0x51b6d6): rand() % 128 − 64 in x, another in y and rand() % 180 in z, added to the
    // position (each stored in float and then fld, fadd and fstp). Another type or the table full, none.
    std::optional<std::size_t> CreateStoneChip(std::int16_t type, const glm::vec3& position, const glm::vec3& angles,
                                               const glm::vec3& light, Crt::CrtRandom& random);
    std::optional<std::size_t> CreateStoneChip(std::int16_t type, const glm::vec3& position, const glm::vec3& angles,
                                               const glm::vec3& light, Crt::CrtRandom& random, std::int32_t subtype);
    // 0x518cf0(0xbb or 0x5ac, …, 0, 0, −1, 0, 0, 0, 0) with subtype 0. The cloud (0x51b968): life 2, blendMesh at
    // 0, scale 10, 200 more in y and 190 less in z, no per-vertex light; no rand(). The 0x5ac (0x51e3ec): the light at
    // 0.3f, s = rand() % 5 + 9 and the unrotated speed (sin 30° · cos 70°, cos 30° · cos 70°, sin 70°) · s · 0.5 on the
    // x87 over the doubles 0x6ba1c8 and 0x6ba1d0, scale 3 and life 400. Another type or the table full, none and no
    // rand().
    std::optional<std::size_t> CreateIcarusEffect(std::int16_t type, const glm::vec3& position, const glm::vec3& angles,
                                                  const glm::vec3& light, Crt::CrtRandom& random);
    // 0x518cf0(0xb3, position, angles, light, 0, object, −1, 0, ftol(the object's scale), 0, 0) → 0x51900b: the common
    // prefix (+0x8a at −1, scale 0.9) and six rand(): z, y and x ± 100 (r % 200 − 100), the yaw plus r % 360, the
    // speed's y (r & 7) · 0.1f − 5 (x and z at 0) and the scale plus (r % 5) · 0.025f. The state +0x88 at 0 (overwrites
    // the object scale's byte), +0x68 and the animation +0xd8 at 1, life 1000, +0xe4 and +0xec at 1, the start at the
    // already shifted position and the light (0.01f, 0.01f, 0.03f). Nobody reads the owner (+0x108). Full, none and no
    // rand().
    std::optional<std::size_t> CreateAidaWisp(const glm::vec3& position, const glm::vec3& angles,
                                              const glm::vec3& light, Crt::CrtRandom& random);
    // 0x518cf0(0x135, position, angles, light, 0, object, −1, 0, 0, 0, 0) → 0x51d73b: the common prefix (+0x8a
    // at −1, scale 0.9), the owner (+0x108), life 100, the meshes' light (+0x68) at 0.3f and the start at the position.
    // No rand(); full, none.
    std::optional<std::size_t> CreateGatePlane(const glm::vec3& position, const glm::vec3& angles,
                                               const glm::vec3& light, bool hasOwner);
    // 0x518cf0(0x61d, position, angles, light, subtype, owner, `disturbance`, 0, 0, 0, 0) → 0x51ee96: the common prefix
    // (scale 0.9 and +0x8a with `disturbance`), life 20 and scale 0.5; with subtype 0, life at 15. No rand(). The
    // 0x61d's step does not read the owner (+0x108). Full, none.
    std::optional<std::size_t> CreateFadingDecal(const glm::vec3& position, const glm::vec3& angles,
                                                 const glm::vec3& light, std::int32_t subtype,
                                                 std::uint16_t disturbance);
    // 0x518cf0(0x5ac, position, angles, light, 3, character, −1, 0, 0, 0, 0) → 0x51e489: the common prefix (+0x8a
    // at −1, scale 0.9) with the character as owner (+0x108), life 30 and +0xd8 at 0. No rand(); full, none.
    std::optional<std::size_t> CreateFullSetFlare(const glm::vec3& position, const glm::vec3& angles,
                                                  const glm::vec3& light, void* owner);
    // 0x518cf0(0x5ac, position, angles, light, 1 or 2, target, −1, 0, 0, 0, 0) → 0x51e4a6: the common prefix (+0x8a
    // at −1, scale 0.9) with the target as owner (+0x108), life 1000 and +0xd8 at 0. No rand(); another subtype or the
    // table full, none.
    std::optional<std::size_t> CreateSkillFlare(const glm::vec3& position, const glm::vec3& angles,
                                                const glm::vec3& light, std::int32_t subtype, void* owner);
    // 0x518cf0(0x6eb, position, angles, light, 1, 0, −1, 0, 0, 0, 0) → 0x51f4ad: the common prefix (+0x8a at −1, no
    // owner), the type's life 30 and scale 20, and with subtype 1 life 20 and scale ((rand() % 10) + 10) · 0.1f on the
    // x87 (0x51f4c3..0x51f4eb). One rand(); full, none and no rand().
    std::optional<std::size_t> CreateGallopShockWave(const glm::vec3& position, const glm::vec3& angles,
                                                     const glm::vec3& light, Crt::CrtRandom& random);
    // 0x518cf0(0x14d, position, angles, light, 0, owner, −1, 0, 0, 0, 0) → 0x51db7a: the common prefix (+0x8a
    // at −1, scale 0.9) with the owner, life 100, the scale (rand() % 100) · 0.002f + 0.3f, subtype at 0, alpha at
    // 0.7f, the three angles with rand() % 360 and, by rand() % 30, the place: the point of bone 10 (with 1 and 2), 14
    // (3), 2 (4 and 5), 50 or 51 with an even or odd rand() (6 and 7) or 53 (8) of the global pose buffer
    // (0x4ec110(…, 0), which does not look at the owner's model), with 0.2f less scale in 3, 6, 7 and 8; with 9 and 10
    // it turns it off (and the alpha at 0); with the others, 0.1f more scale and the position shifted
    // rand() % 240 − 120 in x, rand() % 10 − 5 in y and 110 in z. Always, also when off, the sprite 0x5ac of scale 2 in
    // its place with the light minus 0.3f (0x51ddab). `bonePoint` gives the point of a bone of the global buffer;
    // without it, it cuts before touching anything. Full, none and no rand().
    [[nodiscard]] Core::Expected<std::optional<std::size_t>, std::string>
    CreateLightning(const glm::vec3& position, const glm::vec3& angles, const glm::vec3& light, void* owner,
                    Crt::CrtRandom& random, const std::function<glm::vec3(std::size_t)>& bonePoint,
                    Particles::FrameSprites& sprites);
    // 0x518cf0(0x14e, position, angles, light, subtype, owner, −1, 0, 0, 0, 0) → 0x51ddfc: the common prefix (+0x8a
    // at −1, scale 0.9) with the owner, life 200, the frame (+0x1c4) at 0, the light, alpha at 1, the time (+0x1c0)
    // from timeGetTime() and z at 0. No rand(); full, none.
    std::optional<std::size_t> CreateLightningDecal(const glm::vec3& position, const glm::vec3& angles,
                                                    const glm::vec3& light, std::int32_t subtype, void* owner,
                                                    std::uint32_t systemTime);
    // 0x518cf0(0x61e, position, angles, light, `subtype`, owner, −1, 0, 0, 0, 0) → 0x51eebb: the common prefix (+0x8a
    // at −1, scale 0.9) with the owner (+0x108) and life 20; subtypes 0 to 3 (the 1 of the pet and of skill 0x1a, the 3
    // of skill 0x1c and of the summons 0x1e..0x24, Skills/AttackSkillCases) have no case and exit through 0x51db73. No
    // rand(); another subtype (the 4 and 6 to 10 have their case) or the table full, none.
    std::optional<std::size_t> CreatePetSummon(const glm::vec3& position, const glm::vec3& angles,
                                               const glm::vec3& light, void* owner,
                                               std::int32_t subtype = PetSummonSubtype);
    // 0x518cf0(0x118 or 0x119, position, angles, light, 0, 0, −1, 0, 0, 0, 0) → 0x51cc6b or 0x51cd1b: the common prefix
    // (+0x8a at −1, no owner, scale 0.9) and the attribute of the position's cell (UnmaskedCellAttribute, at 0x51cc8b
    // or 0x51cd3b): with bit 0x04, 0x08 or 0x10 the slot stays written with the live byte at 0, and none, no rand().
    // Otherwise, life 40, the scale rand() % 30 · 0.01f + 1.2f (0x118) or + 1 (0x119) on the x87, +0xd8 at 0.3f and the
    // yaw rand() % 360; 0x4ea030(effect, 0) does nothing, because the prefix already left the action at 0. Two rand().
    // Without the attributes table, or with a cell outside it, it cuts after the prefix, with the slot alive as the
    // original left it at that point. Another type or the table full, none and no rand().
    [[nodiscard]] Core::Expected<std::optional<std::size_t>, std::string>
    CreateGroundStone(std::int16_t type, const glm::vec3& position, const glm::vec3& angles, const glm::vec3& light,
                      const Data::Terrain::TerrainAttributes* attributes, Crt::CrtRandom& random);
    // 0x518cf0(0xc8 or 0xc9, …, 10, 0, −1, 0, 0, 0, 0) → 0x51b432, the pebble the stone 0x118/0x119 drops: the common
    // prefix, subtype 10 and five rand(): the push ((& 255) + 64) · 0.2 (the double 0x6ba1e8) in y, the life
    // (& 15) + 32, the scale ((& 3) + 15) · 0.05f, the yaw rand() % 360 and the fall (& 15) + 28 in +0xe4; the speed is
    // the push rotated by the angles with that yaw. Another type or the table full, none and no rand().
    std::optional<std::size_t> CreateGroundStoneChip(std::int16_t type, const glm::vec3& position,
                                                     const glm::vec3& angles, const glm::vec3& light,
                                                     Crt::CrtRandom& random);
    // 0x518cf0(0xca, position, angles, light, 0, 0, −1, 0, 0, 0, 0) → 0x51b659, 0x52e810's ice piece: the common prefix
    // (+0x8a at −1, no owner, scale 0.9) and the case, which the 0xea and the 0xeb share (0x51b65d's guard for the 0xe5
    // and the 0xe6 does not run with them): +0x64 (the blendMesh) at 0 and +0x68 (its light) at 0.3f; the push
    // ((rand() & 255) + 64) · 0.1f in y; z 50 more; the life (rand() & 15) + 32; the scale ((rand() & 3) + 8) · 0.1f;
    // the yaw rand() % 360 and the speed, the push rotated by the angles with that yaw (0x5fc8f0 and 0x5fcbd0);
    // and +0xe4 (the fall) at (rand() & 15) + 8. Five rand(). With the table full, none and no rand().
    std::optional<std::size_t> CreateIceShard(const glm::vec3& position, const glm::vec3& angles,
                                              const glm::vec3& light, Crt::CrtRandom& random);
    // 0x518cf0(0x6eb, position, angles, light, 0, 0, −1, 0, 0, 0, 0) → 0x51f4ad with subtype 0, the stomp's wave: the
    // common prefix (+0x8a at −1, no owner), life 30 and scale 20, no rand(). Full, none.
    std::optional<std::size_t> CreateStompShockWave(const glm::vec3& position, const glm::vec3& angles,
                                                    const glm::vec3& light);
    // 0x518cf0(0xfa, position, angles, light, 0, owner, −1, 0, state, 0, 0) → 0x51a66d, as the horse calls it
    // (0x4453e0, with state 2): the common prefix (+0x8a at −1, scale 0.9, the state in +0x88) with the
    // owner, +0x28..+0x30 with the angles, the start with the position, life 20, subtype rand() % 100, the angles' z
    // 330 more (fld/fadd/fstp), +0x28 80 more, +0x30 180 more and +0xe4 at 50. Then, 0x51a8d8: opens the own record
    // (0x4e79a0, spends nothing), adds 1 to its count +0xf04 (`ownRecordCount`, which wraps, Q-C426; the previous one
    // goes to +0x8c, which nothing ported reads) and closes it (0x40f370: with `ownRecordDepth` at 0, its 3848 rand()).
    // Full, none, no rand() and the record untouched.
    std::optional<std::size_t> CreateEarthquake(const glm::vec3& position, const glm::vec3& angles,
                                                const glm::vec3& light, std::uint8_t state,
                                                const ModelEffectOwner& owner, std::uint8_t& ownRecordDepth,
                                                std::uint8_t& ownRecordCount, Crt::CrtRandom& random);
    // 0x519275: pulse with no owner or randomness; first free slot, life 15 and white light.
    std::optional<std::size_t> CreateEarthquakeBurst(const glm::vec3& position, const glm::vec3& angles,
                                                     const glm::vec3& light);
    // 0x518cf0(0xfb..0x102, position, angles, light, subtype, owner, size, 0, 0, 0, 0) → 0x51a6e4..0x51a85c: the common
    // prefix with the owner and +0x8a at `size`, and the scale size · 0.01f (fild/fmul/fstp). Life: 0xfb 35 with
    // subtype 0 and 60 with 1; 0xfc 20 with 0 and 1 and 50 with 2; 0xfd, 0xfe and 0x100 35; 0xff, 0x101 and 0x102
    // 40. +0x64 at 0 in all except 0xfd and 0x100, which keep the prefix's −1. With subtype 1, 0xfc, 0xff and 0x102
    // set +0x74 to CrackRenderFlags. A subtype with no case (0xfb with other than 0 or 1, 0xfc with other than 0..2)
    // exits through 0x51db73 with no scale or life: the prefix's scale 0.9 and the life of the slot's previous use
    // (Q-C423). Another type or the table full, none. No rand().
    std::optional<std::size_t> CreateEarthquakeCrack(std::int16_t type, const glm::vec3& position,
                                                     const glm::vec3& angles, const glm::vec3& light,
                                                     std::int32_t subtype, const ModelEffectOwner& owner,
                                                     std::int16_t size);
    // 0x518cf0 up to its switch (0x518e3c): the first slot without the live byte with what the prefix writes (the live
    // byte, the arguments, +0x58 and +0x64 at −1, +0x68 at 1, the alpha at 1, +0xd8 at 0.3, the action and frame at
    // 0, +0x74 and +0x100 at 0, per-vertex light and speed at 0). The slot's other fields keep the previous use's
    // values. Full, nothing (0x51db71), no rand(). The caller continues each type's case (Effects/ArrowEffects).
    std::optional<std::size_t> Begin(const Kernel::ModelEffectArguments& arguments);
    // 0x6070a0: only clears each slot's live byte (Q-C138).
    void Clear();

    [[nodiscard]] std::span<ModelEffect> Effects();
    [[nodiscard]] std::span<const ModelEffect> Effects() const;

private:
    std::array<ModelEffect, ModelEffectCount> m_effects{};
};

// 0x518ac0(type, owner, subtype): turns off the live model effects of that type (MOVSX of +2), that owner (+0x108)
// and that subtype (+4; −1, any), among the 200. Returns whether it turned any off. No rand(). MuRet's owner is the
// effect's ownerContext (*inferred*, Q-C1200: effects owned by a character keep it there).
bool KillModelEffectsOf(ModelEffects& effects, std::int16_t type, const void* owner, std::int32_t subtype);

// 0x5f8290(ftol(x · 0.01f), ftol(y · 0.01f)) and the WORD of 0x7d0d348 at that index, as the creations of the stones
// 0x118/0x119 read it: y · 256 + x in 32 bits, without 0x5f82a0's mask. Without the table, with a coordinate that
// 0x69fd3c's ftol cannot represent or with an index outside the 65536 cells (the original would read other memory), it
// cuts with `address`.
[[nodiscard]] Core::Expected<std::uint16_t, std::string>
UnmaskedCellAttribute(const Data::Terrain::TerrainAttributes* attributes, const glm::vec3& position,
                      std::string_view address);

// 0x5208a0 on a live effect, with its model's skeleton (for the advance, or nullptr): the type's case (the 0xb4:
// 0x52096c..0x520b72) and the common tail (0x52cb98): the animation at +0xd8 (0x4ec200; if the action ends, the frame
// at its keys and the action at 0xff), the position plus the speed rotated by the angles (0x51fc80 with 0x5fc8f0 and
// 0x5fcbd0) and one life less; with life at 0, it dies.
void StepModelEffect(ModelEffect& effect, const Models::Skeleton* skeleton);
} // namespace Client::Effects
