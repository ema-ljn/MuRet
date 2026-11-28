#pragma once

#include "Core/Expected.h"
#include "Particles/ParticleOwner.h"
#include "Audio/SoundId.h"

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace Client::Kernel
{
struct WorldObject;
} // namespace Client::Kernel

namespace Client::Crt
{
class CrtRandom;
} // namespace Client::Crt

namespace Client::Terrain
{
struct TerrainLighting;
} // namespace Client::Terrain

// The table of 0x53fe70 (0x743a198, 3000 slots of 0x78 bytes), its step 0x5446e0 and the wind at 0x7609bb8 (06 §
// Los efectos del login). The drawing, 0x548550, has a single contract: Particles/ParticleDrawing.h, which follows the
// ASM's jump table. The math keeps binary32 stores and double intermediates (53 bits); it does not emulate x87
// status/exceptions nor promise equivalence for every control word or XP platform.
namespace Client::Particles
{
class FrameSprites;
class IParticleTrigonometry;

inline constexpr std::size_t ParticleCount = 3000;
// 0x52a866/0x52a89a: the two lights that follow the bones of Swell Life's target.
inline constexpr std::int32_t SwellLifeParticle = 0x5ac;
inline constexpr std::int32_t SwellLifeParticleSubtype = 4;
// 0x540260 and 0x542599: Crywolf's hand sparks 5ad/2 and mist 5f2/27. 5ad/2, /3 and /4 (with texture 0x5fd) are also
// those of the foot and the target of 0x1b6 (0x42769f..0x427a4d).
inline constexpr std::int32_t MagicSparkParticle = 0x5ad;
inline constexpr std::int32_t CrywolfTideSmokeSubtype = 25;
inline constexpr std::int32_t CrywolfSwingSmokeSubtype = 27;
inline constexpr std::int32_t MissileFireParticle = 0x5da;
inline constexpr std::int32_t DeathSpiritFireParticle = 0x5db;
inline constexpr std::int32_t MissileSmokeSubtype = 38;
inline constexpr std::int32_t ExplosionSmokeSubtype = 35;
// 5f2/34 of joint 0x610/0xe of arrow 0xf9 (0x537f39): born like /35 but still (0x541a69 → 0x5435bb), and shares its
// step (0x5446e0, case 0x22..0x23) and drawing.
inline constexpr std::int32_t ViperSmokeSubtype = 34;
inline constexpr std::int32_t FireworkParticle = 0x60d;
inline constexpr std::int32_t PoundingBallParticle = 0x640;
inline constexpr std::int32_t ShinyParticle = 0x5fc;
inline constexpr std::int32_t ThunderParticle = 0x5ca;
// 0x5410d9: /1 (from 0x53fd42) gray, fading with life; /2 (from 0x51df52) with texture 0x61e.
inline constexpr std::int32_t ThunderGraySubtype = 1;
inline constexpr std::int32_t ThunderBoltSubtype = 2;
inline constexpr std::int32_t FairySparkParticle = 0x5c5;
inline constexpr std::int32_t FireParticle = 0x5d9;
// 0x4fd580/0x43ebb7: Kalima's trail blue flame, texture Effect\Fire05.jpg (0x61ddac).
inline constexpr std::int32_t BlueTrailFireParticle = 0x5dc;
inline constexpr std::int32_t BlueTrailFireSubtype = 12;
// Fire cases (5d9, 5db and 5dc, 0x541172 and 0x5457a5): /7 and /11 sway sideways, /9 follows its owner (+0x40) from a
// ring start, /12 is the trail's high one and /13 rises straight.
inline constexpr std::int32_t FireSwaySubtype = 7;
inline constexpr std::int32_t FireRingSubtype = 9;
inline constexpr std::int32_t FireLongSwaySubtype = 11;
inline constexpr std::int32_t FireTallSubtype = BlueTrailFireSubtype;
inline constexpr std::int32_t FireFountainSubtype = 13;
inline constexpr std::int32_t DropParticle = 0x607;
inline constexpr std::int32_t WorldSmokeSubtype = 2;
inline constexpr std::uint16_t BreathParticle = 0x5f2;
// Effect\Spark03.jpg, subtype 5 (06 § El cuervo).
inline constexpr std::uint16_t SparkParticle = 0x5c6;
inline constexpr std::int32_t PiercingSparkSubtype = 2;
// 0x42ddb0: over a character near the siege crown/mark.
inline constexpr std::int32_t SiegeBlessingParticle = 0x638;
inline constexpr std::int32_t TeleportSparkSubtype = 1;
inline constexpr std::int32_t RavenSparkSubtype = 5;
inline constexpr std::int32_t MapSparkSubtype = 9;
// The twenty sparks of 0x815 on the own record's left, requested by the attack step (0x4fb0d1): born at 0x540a04 with
// texture 0x5fc, six rand() and +0x38 at 1.0f; their step (0x545397) takes a rand() for the light.
inline constexpr std::int32_t BowSparkSubtype = 12;
// Effect\lightning2.jpg, left by joint 0x618/0 or /1 of the killer (0x53867e, /0 each step; 0x538577, /1 on arrival).
// Born without rand() (0x540bbe and 0x540bfe); their step (0x545419) turns with the clock and requests the dynamic
// light; 0x548550 draws them with the common billboard.
inline constexpr std::int32_t LightningParticle = 0x5d5;
inline constexpr std::int32_t LightningTrailSubtype = 0;
inline constexpr std::int32_t LightningHitSubtype = 1;
// The other cases of 0x540bb2: /2 (life 20, from 0x4c965c) follows its owner (+0x40, a character) in its step, /3
// lives 1 step with the requested scale, and /4 and /5 keep the requested light.
inline constexpr std::int32_t LightningFollowSubtype = 2;
inline constexpr std::int32_t LightningFlashSubtype = 3;
inline constexpr std::int32_t LightningPlainSubtype = 4;
inline constexpr std::int32_t LightningPlainScaledSubtype = 5;
inline constexpr std::uint16_t CloudParticle = 0x622;
inline constexpr std::int32_t LoginCloudSubtype = 10;
// Both share creation and step; 68 also shifts the position at birth.
inline constexpr std::int32_t Particle67 = 0x67;
inline constexpr std::int32_t Particle68 = 0x68;
// 0x60f/3 is born with 60 life, and 0x548550 does not draw it while it keeps that life, except /4 (Blood Castle's
// torch, born with 40). /0 (the full set's, 0x510339) is born and turns like /3; with its owner (the set's character,
// or the 0x61d decal from 0x52b8ff and 0x52b977) in certain actions it becomes /1, which only fades (0x54735c).
inline constexpr std::int32_t OrbitParticle = 0x60f;
inline constexpr std::int32_t OrbitLife = 60;
// Flowers 608, 609 and 60a (Skill\flower1..3.tga) of arrow 0xe1 (0x526a8b): born at 0x542c4b without reading the
// subtype; their step (0x546da6) drops them, /0 and /1 with their drift, down to the terrain (0x5f93c0), turning.
inline constexpr std::int32_t FirstFlowerParticle = 0x608;
inline constexpr std::int32_t LastFlowerParticle = 0x60a;
// Column 62b/0 (Effect\flareBlue.jpg) of circle 0xcc/3 and /4 (0x523677 and 0x52384c; table 0x52cef4 sends 0xcc to
// 0x523374): born at 0x542d01, rises faster and faster (0x546f0d). The other subtypes leave 0x53fe70 at 0x54304c with
// only the prefix and have no step of their own (0x546f12).
inline constexpr std::int32_t FlareColumnParticle = 0x62b;
// Effect\Hole.jpg of Kalima's gate 0xc5 (0x43c20d): born at 0x543bf1; turns, shrinks and fades with life (0x547d42),
// without reading the subtype.
inline constexpr std::int32_t HoleParticle = 0x6ae;
// Crywolf's objects and water (map 34): smoke 5f2/21 (0x542370, through 5f2/2's step, 0x5466c3), cloud 622/9
// (0x54330a, with 622/8's step, 0x5476a8), orbit 60f/10 of effect 0x61d/3 (0x542ed0, stays at its start, 0x547297) and
// the water's bubble 698/0 (Effect\coll.jpg, 0x543152 and 0x54719f).
inline constexpr std::int32_t CrywolfSmokeSubtype = 21;
// Smoke 5f2/33 of Crywolf's 0x48 (0x430932): born at 0x541a8d with life and angles from four rand(); its step
// (0x545ffc) slows the velocity and raises it with the scale, which grows.
inline constexpr std::int32_t CrywolfRisingSmokeSubtype = 33;
// Dust 5f2/11 of the dark horse's stomp stones 0x118/0x119 (0x527631): born at 0x541d81 shifted by three rand(), tilted
// and turned, with the creation light kept in +0x60..+0x68; its step (0x546111) fades it with life, slows, enlarges and
// lowers it.
inline constexpr std::int32_t StoneDustSubtype = 11;
inline constexpr std::int32_t CrywolfCloudSubtype = 9;
inline constexpr std::int32_t AnchoredOrbitSubtype = 10;
inline constexpr std::int32_t BubbleParticle = 0x698;
// Dust 5f3/0 (Effect\smoke02.tga) of the dark horse's gallop outside maps 2 and 10 (0x4444dd): born at 0x54271f; its
// step (0x546b66) keeps it on the terrain with its texture's height.
inline constexpr std::int32_t DustParticle = 0x5f3;
// Dust 5f6/1 of Crywolf's route (0x403c0c): shares 5f3's step, but is born with life 40, velocity 0 and six rand() at
// 0x54271f..0x542897. Creation does not tell the two types apart: 5f3/1 of the +0x3e5 brake (0x506a73) is born like
// 5f6/1, and 5f6/0 of Chaos Castle's stones (0x405630 and 0x405e00) like 5f3/0.
inline constexpr std::int32_t ArrivalDustParticle = 0x5f6;
// Explosion 5ed/0 (Effect\Explotion01.jpg, four by four frames) released by earthquake 0xfa at its life 11 (0x521dfd):
// born at 0x542a60 with life 20; its step (0x5459a4) advances the frame (+0x48) with life and lights the dynamic light.
// Creation ends with the sound 0x41ed10(0x40, 0, 0), which the caller makes, not Particles.
inline constexpr std::int32_t ExplosionParticle = 0x5ed;
// Blaze 5ee (Effect\DinoE.jpg, four frames in a row) of 0x6ac (0x51ffd5) and joint 0x632/6 (0x53bf6e): born at
// 0x542a94 with life 12 and the requested scale, any subtype, no rand(); its step (0x545ce3) sets the frame (+0x48) to
// (12 − life) / 3 and 0x5486b6 draws it like the fire (a quarter of the width, frame % 4 and the turn of +0x20).
// Creation ends with the sound 0x41ed10(0x40, 0, 0): the caller makes it.
inline constexpr std::int32_t BlazeParticle = 0x5ee;
// Cinder 6ad (Effect\PoundingBall.jpg) of the bouncing fire 0xc2/5 (/0, 0x524f05) and the javelin 0x139 (/1,
// 0x529c69): born at 0x543939 with life 24 or 15, three rand() and the velocity toward −y turned by its angles; its
// step (0x5478d7) raises it faster and faster, shrinks it and, for /1, fades it with life. 0x5488f6 draws it with the
// common billboard.
inline constexpr std::int32_t CinderParticle = 0x6ad;
// Effect\ShockWave.jpg as a particle: 0x6eb/3 of 0x1b7's left arm (0x427fad). Born at 0x5443ae with life 7, no rand();
// its step (0x5481ab) fades the light by 0.5555556f and grows 0.3f, and 0x5488f6 draws it with the common billboard. It
// does not read the subtype.
inline constexpr std::int32_t ShockWaveParticle = 0x6eb;
// Effect\blood.tga (0x61d7c9), released by the attack step's damaging hit (0x50419c, ten on the target): born at
// 0x542b3b with four rand() and the velocity turned by the hitter's angles; its step (0x546b04) slows it and advances
// the frame (+0x48), and 0x548550 draws it with two-by-two frames (0x5487b9). It does not read the subtype. 5e3 shares
// its creation (without the light) and step, but nothing in MuRet requests it.
inline constexpr std::int32_t BloodParticle = 0x5e4;
// Effect\Shiny03.jpg (0x61d937) of the weapon skills (0x55be23, over the hero): born at 0x542bff with life 18, no
// rand(), and 0x53fdb0 puts it on its owner's bone (+0x40); its step (0x546d54) gives it the scale sin(life · π/18) · 3
// and puts it back on the bone. 0x548550 draws it with the common billboard (0x5488f6 → 0x54889a). It does not read the
// subtype; 0x53fdb0 does.
inline constexpr std::int32_t SkillShineParticle = 0x5fe;
// Effect\Shiny02.jpg of the skill that hits several (0x53d810, of 1A): /0 and /2 are born at 0x542b0e with life 36 and
// angle x at 45, and 0x53fdb0 puts them in their owner's hand, a character (the caller does it:
// Effects::RaiseTheHandShine). Their step (0x546cd4) pulses the scale with the sine of life, turns /2 and puts them
// back in the hand; 0x548550 draws them with the common billboard (0x5488f6 → 0x54889a).
inline constexpr std::int32_t HandShineParticle = 0x5fd;
inline constexpr std::int32_t HandShineSecondSubtype = 2;

// The offsets are the original's; this DTO does not promise the 0x78-byte binary layout.
struct Particle
{
    bool alive = false;          // +0x0
    std::int32_t type = 0;       // +0x4
    std::int32_t texture = 0;    // +0x8; 0x53fe70 copies the type into it
    std::int32_t subtype = 0;    // +0xc
    float scale = 0.0f;          // +0x10
    glm::vec3 position{};        // +0x14
    glm::vec3 angles{};          // +0x20, in degrees: the velocity's
    glm::vec3 light{};           // +0x2c
    std::uint32_t field38 = 0;   // +0x38: kept by the prefix; 622/8 initializes it
    std::int32_t life = 0;       // +0x3c, steps left
    std::uint32_t owner = 0;     // +0x40, a pointer in the original
    float rotation = 0.0f;       // +0x44, the billboard's, in degrees
    float field48 = 0.0f;        // +0x48: raw32 frame by bit_cast for 5d9 and 607; no universal x87 ABI guarantee
    bool moves = false;          // +0x4c: the step adds the velocity turned by the angles
    float verticalSpeed = 0.0f;  // +0x50
    glm::vec3 velocity{};        // +0x54
    float rotationFactor = 0.0f; // +0x60: 622/10 keeps it on creation and writes 1.5 on step; 622/8 initializes it;
                                 // 5f2/11 stores its creation light's red
    glm::vec2 field64{};         // +0x64..68: kept on creation; 5f2/11 stores its light's green and blue
    glm::vec3 start{};           // +0x6c
    // In the Kalima families this identity is the owner; the DWORD `owner` above only keeps the legacy API.
    ParticleOwner typedOwner{};
};

// 0x7609bb8 (x, y), between −1.7 and 1.7, and its change per step, 0x7609bc4, between −0.6 and 0.6.
struct ParticleWind
{
    glm::vec2 wind{};
    glm::vec2 change{};
};

enum class ParticleError : std::uint8_t
{
    UnsupportedType,
    UnsupportedSubtype,
    MissingTextureSize,
    MissingMaterialFlag,
    MissingMap,
    MissingColorFlag,
    MissingDrawingService,
    UnrepresentableTrigonometry,
    MissingTerrainHeight,
    MissingClock,
    MissingScene,
    // 0x5476f0: a 622/1's owner (+0x40) is not among the map's objects; 0x54735c: a 60f/0's does not answer (neither a
    // pool character through ParticleActorActions nor an effect table slot, the 0x61d decal, through
    // ParticleOwnerStates).
    MissingOwner,
    // 0x545cd6: 5ed requests the dynamic light (0x7b4ccc4) and it is missing, or its center does not fit 0x5f9780's
    // int32.
    MissingDynamicLight,
    // 0x546c96: 5fc/2 leaves a sprite in the frame's table (0x5491e0) each step, and the table is missing.
    MissingSprites,
};

// 0x5f93c0: return value kept, read after the prefix of slot 5c5.
using ParticleTerrainHeight = std::function<double(float, float)>;
// The texture height of a 0x7d3a198 slot (+0x24, the buffer's side), or nullopt if not loaded: read by 5f3's step
// (0x546be9).
using ParticleTextureHeight = std::function<std::optional<float>(std::int32_t)>;
// The world object of a ParticleOwner::identity, which the original reads through the +0x40 pointer; nullptr if
// missing.
using ParticleObjectOwners = std::function<Kernel::WorldObject*(std::uint64_t)>;
// The action (+0x111) of an identity's character (ParticleOwnerKind::Actor, the pool slot), which orbit 60f/0's step
// reads through the +0x40 pointer (0x54735f); nullopt if missing. With a model effect as owner, the step reads it from
// ParticleOwnerStates.
using ParticleActorActions = std::function<std::optional<std::uint8_t>(std::uint64_t)>;
// 0x53fdb0(particle): the world point of the bone 5fe follows, its owner's (+0x40, a ParticleOwner of the character
// pool), chosen with the subtype by the published hero. It also resolves 5ac/4 by its own rotation and pose, without
// the model's scale. nullopt if the owner is missing or unresolved; 5ac/4 then fades and finishes its two rand(). For
// 5c6/2 it resolves the center of the physical ModelEffect slot while alive.
using ParticleOwnerBones = std::function<std::optional<glm::vec3>(const Particle&)>;
// 0x53fdb0 in 5fd's step (0x546d37 and 0x546d47): the hand point of the owner character (ParticleOwnerKind::Actor, the
// pool slot) for the subtype; nullopt if missing.
using ParticleActorHands = std::function<std::optional<glm::vec3>(std::uint64_t, std::int32_t)>;

// What the step reads through the +0x40 pointer of the particles that follow their owner (5c6/4, 5d5/2, fire /9 and
// 5de/4): the alive byte (+0x0), the position (+0x10) and, only from a model effect, its start (+0x17c). The action
// (+0x111): from a character it is read by the fanta 6ec/1 and /2 (0x547a54) and orbit 60f/0 (0x54735f), which also
// reads it from the model effect 0x61d that releases it (0x52b8ff, 0x52b977). From a character, also its named bones:
// 0x4010b0(owner, name, point), without offset, read by 6ec/3 and /4 (0x547c1e); nullopt if 0x4016c0 does not find it
// (or the owner is not a character).
struct ParticleOwnerState
{
    bool alive = false;
    glm::vec3 position{};
    std::optional<glm::vec3> start;
    std::uint8_t action = 0;
    std::function<std::optional<glm::vec3>(std::string_view)> namedBone{};
};
using ParticleOwnerStates = std::function<std::optional<ParticleOwnerState>(const ParticleOwner&)>;

class Particles
{
public:
    // 0x53fe70(type, position, angles, light, subtype, scale, owner) in the first free slot, with the world clock
    // (DAT_05767b88, in ms) and the CRT's rand(); with the 3000 full it neither creates nor takes rand(). It accepts
    // BindingError's list plus the Kalima, selection, trials and Blood Castle families (among them Kundun's hit 6f4/2,
    // 0x52a37d); the orbit 60f/0,3,4,10 needs the overload with the trigonometry. Create is the login compatibility;
    // Core uses CreateChecked to tell errors from a full pool. The mist takes five rand() and keeps +0x60 from the
    // slot's previous use; 622/8 takes seven rand().
    std::optional<std::size_t> Create(std::int32_t type, const glm::vec3& position, const glm::vec3& angles,
                                      const glm::vec3& light, std::int32_t subtype, float scale, float worldClock,
                                      Crt::CrtRandom& random);
    // A full pool is success without an index; an unimplemented binding is an explicit error.
    [[nodiscard]] Core::Expected<std::optional<std::size_t>, ParticleError>
    CreateChecked(std::int32_t type, const glm::vec3& position, const glm::vec3& angles, const glm::vec3& light,
                  std::int32_t subtype, float scale, float worldClock, Crt::CrtRandom& random, std::uint32_t owner = 0);
    // Borrowed clock: 5f2/4 loads it after R2; 6/7/8 do not read it on creation. `typedOwner` is orbit 60f/0's owner
    // (+0x40), read by its step (0x54735c); the others ignore it.
    [[nodiscard]] Core::Expected<std::optional<std::size_t>, ParticleError>
    CreateChecked(std::int32_t type, const glm::vec3& position, const glm::vec3& angles, const glm::vec3& light,
                  std::int32_t subtype, float scale, std::reference_wrapper<const float> worldClock,
                  Crt::CrtRandom& random, std::uint32_t owner, IParticleTrigonometry& trigonometry,
                  ParticleOwner typedOwner = {});
    // Only the credited Kalima families. Keeps the identity by value, never a host address.
    [[nodiscard]] Core::Expected<std::optional<std::size_t>, ParticleError>
    CreateKalimaChecked(std::int32_t type, const glm::vec3& position, const glm::vec3& angles, const glm::vec3& light,
                        std::int32_t subtype, float scale, float worldClock, Crt::CrtRandom& random, std::int32_t scene,
                        ParticleOwner owner = {});
    // CreateKalimaChecked adds 5ac/6,7, 6ed/0, 6f0..4/0, 6f1/3 and 6f4/1; borrowed scene for 6f1. The typed-owner
    // families: Kalima's and the selection's (SelectionParticleKernels.h: 622/1, 5de/7 and 729/0,1), plus 6f0/2, 6f1/2
    // and 6f2,6f3/4 of the objects of 54 and 33, the hand's 5fd/0 and /2 (which read neither the clock nor the scene,
    // nor take rand()), and Swell Life's 5ac/4 (0x540112), without rand(). Also those that follow their owner without
    // reading it at birth (5d5/2, fire /9 and 5f2/36: FollowsItsOwner).
    [[nodiscard]] Core::Expected<std::optional<std::size_t>, ParticleError>
    CreateOwnedChecked(std::int32_t type, const glm::vec3& position, const glm::vec3& angles, const glm::vec3& light,
                       std::int32_t subtype, float scale, float worldClock, Crt::CrtRandom& random, std::int32_t scene,
                       ParticleOwner owner = {});
    // 0x53fe70 with the skills' 5fe (SkillShineParticle): the common prefix, life 18 (0x542c00) and 0x53fdb0 with
    // `bones` and `owner` (+0x40), which overwrites the position. No rand(). With the 3000 full, nothing; without the
    // owner, MissingOwner with the slot off. Another type, UnsupportedType.
    [[nodiscard]] Core::Expected<std::optional<std::size_t>, ParticleError>
    CreateFollowingChecked(std::int32_t type, const glm::vec3& position, const glm::vec3& angles,
                           const glm::vec3& light, std::int32_t subtype, float scale, ParticleOwner owner,
                           const ParticleOwnerBones& bones);
    // 0x53fe70 with those that read their owner's position at birth (+0x40 → +0x10): 5c6/4 (0x540686, the velocity
    // toward it), 5de/4 (0x5417ba, the start) and 5c5/5 (0x5403ad, the start). The others ignore it; the owner stays in
    // typedOwner for their step. Without it, CreateChecked and CreateOwnedChecked reject those three with MissingOwner
    // or UnsupportedSubtype before touching the slot.
    [[nodiscard]] Core::Expected<std::optional<std::size_t>, ParticleError>
    CreateTowardChecked(std::int32_t type, const glm::vec3& position, const glm::vec3& angles, const glm::vec3& light,
                        std::int32_t subtype, float scale, std::reference_wrapper<const float> worldClock,
                        Crt::CrtRandom& random, IParticleTrigonometry& trigonometry, ParticleOwner owner,
                        const glm::vec3& ownerPosition);
    // 0x53fe70 with those that request the terrain height (0x5f93c0) at their point at birth: the mist 5f2/28, /29 and
    // /30 of storm 0xcf/3..7 (0x5425f9, 0x54264d and 0x54269a). /30 also shifts x and y with the sine of the clock
    // (0x5426ae, `trigonometry`). The others ignore it. Without it, CreateChecked rejects those three with
    // MissingTerrainHeight before touching the slot.
    [[nodiscard]] Core::Expected<std::optional<std::size_t>, ParticleError>
    CreateOnGroundChecked(std::int32_t type, const glm::vec3& position, const glm::vec3& angles, const glm::vec3& light,
                          std::int32_t subtype, float scale, std::reference_wrapper<const float> worldClock,
                          Crt::CrtRandom& random, IParticleTrigonometry& trigonometry,
                          const ParticleTerrainHeight& terrainHeight);
    // 0x54061d: spark 5c6/2 toward the center of its physical slot 621, without the CRT.
    std::optional<std::size_t> CreatePiercingSpark(const glm::vec3& position, float scale, ParticleOwner owner,
                                                   const glm::vec3& center);
    // 0x6070a0, on entering the scene: clears every slot's alive byte. The wind stays as it was.
    void Clear();

    [[nodiscard]] std::span<Particle> All();
    [[nodiscard]] std::span<const Particle> All() const;
    [[nodiscard]] bool Any() const;
    [[nodiscard]] ParticleWind& Wind();
    [[nodiscard]] const ParticleWind& Wind() const;

private:
    [[nodiscard]] Core::Expected<std::optional<std::size_t>, ParticleError>
    CreateImpl(std::int32_t type, const glm::vec3& position, const glm::vec3& angles, const glm::vec3& light,
               std::int32_t subtype, float scale, float legacyClock, const float* worldClock, Crt::CrtRandom& random,
               std::uint32_t owner, IParticleTrigonometry* trigonometry,
               std::optional<std::int32_t> scene = std::nullopt, ParticleOwner typedOwner = {},
               const glm::vec3* ownerPosition = nullptr, const ParticleTerrainHeight* terrainHeight = nullptr);
    // On the heap: the scene that holds it must fit on the stack.
    std::vector<Particle> m_particles = std::vector<Particle>(ParticleCount);
    ParticleWind m_wind;
};

// 0x5446e0: the wind (two rand(), one per axis) and each live particle: life drops by one and at 0 it goes off (the
// common movement still runs), with +0x4c it moves (0x4e98d0), and its type's case: the breath's, the raven's spark's
// and the login mist's (0x5475b6: adds the velocity again without turning and decides growth, fade or death by the 3D
// distance to the origin); 622/8 only updates +38/Z with positive life. Login compatibility: returns an error if a new
// binding needing context appears; world callers use StepParticlesChecked and check its result.
std::optional<ParticleError> StepParticles(Particles& particles, Crt::CrtRandom& random);
// For 67 and 68, 0x4303c0 reads the raw32 map on each slot. Step keeps their subtype 1 (field +0x50 to scale) and !=1
// continuations although Create only accepts /0; do not add a common /0 guard. An unsupported binding fails after the
// wind and the current slot's life/movement. Without a map, 67/68 also fail after changing the scale and before RGB.
// The error keeps every effect of the prefix and earlier slots and leaves later ones unvisited: it is not
// transactional. Repeating the tick consumes wind and steps the earlier slots again. It neither copies nor owns
// WorldState.
[[nodiscard]] Core::Expected<void, ParticleError> StepParticlesChecked(Particles& particles, Crt::CrtRandom& random,
                                                                       const std::int32_t* map = nullptr,
                                                                       const ParticleTerrainHeight& terrainHeight = {});
// Loads the clock per slot for 6 after the common movement, without a snapshot of the pass. `owners` resolves 622/1's
// owner (0x5476f0), read and written in the step, and `actorActions` the action of orbit 60f/0's owner character
// (0x54735c); without it a live 60f/0 fails with MissingOwner. 5f3 requests the terrain height and its texture's
// height (`textureHeight`): without them it cuts after moving, slowing and enlarging. 5ed requests the dynamic light
// (`lighting`, 0x7b4ccc4): without it, it cuts with MissingDynamicLight after its frame. 5fe requests its owner's bone
// (`ownerBones`, 0x53fdb0): without it, it cuts with MissingOwner after its scale. 5fd requests its owner's hand
// (`actorHands`, 0x53fdb0): without it, it cuts with MissingOwner after the scale and the rotation.
[[nodiscard]] Core::Expected<void, ParticleError>
StepParticlesChecked(Particles& particles, Crt::CrtRandom& random, std::reference_wrapper<const float> worldClock,
                     const std::int32_t* map, IParticleTrigonometry& trigonometry,
                     const ParticleTerrainHeight& terrainHeight = {}, const ParticleObjectOwners& owners = {},
                     const ParticleActorActions& actorActions = {}, const ParticleTextureHeight& textureHeight = {},
                     Terrain::TerrainLighting* lighting = nullptr, const ParticleActorHands& actorHands = {},
                     const ParticleOwnerBones& ownerBones = {});

// Same contract with the 0x40 sounds of the explosions 1 created by 5ed/2; it does not keep the callback.
// `ownerStates` gives the owner (+0x40) of those that follow it (5c6/4, 5d5/2, fire /9 and 5de/4): without it, or
// without that owner, they cut with MissingOwner in their case. `sprites` is the frame's table (0x5491e0) where 5fc/2
// leaves its flash each step: without it, it cuts with MissingSprites after turning, taking its rand(), moving and
// fading.
[[nodiscard]] Core::Expected<void, ParticleError>
StepParticlesChecked(Particles& particles, Crt::CrtRandom& random, std::reference_wrapper<const float> worldClock,
                     const std::int32_t* map, IParticleTrigonometry& trigonometry,
                     const ParticleTerrainHeight& terrainHeight, const ParticleObjectOwners& owners,
                     const ParticleActorActions& actorActions, const ParticleTextureHeight& textureHeight,
                     Terrain::TerrainLighting* lighting, const ParticleActorHands& actorHands,
                     const ParticleOwnerBones& ownerBones, const std::function<void(Audio::SoundId)>& sound,
                     const ParticleOwnerStates& ownerStates = {}, FrameSprites* sprites = nullptr);

} // namespace Client::Particles
