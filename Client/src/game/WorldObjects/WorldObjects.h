#pragma once

#include "Core/Expected.h"
#include "Data/Terrain/TerrainObjects.h"
#include "Models/ObjectState.h"
#include "Models/Pose.h"
#include "Models/Skeleton.h"
#include "WorldObjects/WorldObjectRegistrations.h"
#include "Audio/SoundId.h"

#include <glm/ext/vector_float3.hpp>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace Client::Crt
{
class CrtRandom;
} // namespace Client::Crt

namespace Client::Terrain
{
struct TerrainLighting;
} // namespace Client::Terrain

namespace Client::Effects
{
class ModelEffects;
} // namespace Client::Effects

namespace Client::Particles
{
class Particles;
} // namespace Client::Particles

// A world's objects (CL7.2): those of the .obj as 0x606660 builds them, their step of 0x604ab0 and what 0x6002e0 does
// with each one. Contract: 06 § El mundo del login; vectors from the real code in
// Client/tests/vectors/world55/objects.txt (scripts/login/make_world55_vectors.py).
namespace Client::WorldObjects
{
// The lantern and dragon owner cases of 0x606660 only run in the login (2) and the selection (4). The per-map cases do
// not depend on the scene.
enum class ObjectScene : std::uint8_t
{
    Login,
    CharacterSelect,
    Other,
};

// Typed projection of the used fields of the 0x1d4 bytes; does not reproduce the i386 layout.
struct WorldObject
{
    WorldObjectIdentity identity = 0;  // caller's ID; 0 in the projection without a registry
    std::int16_t type = 0;             // +0x2, the model
    Models::ObjectTransform transform; // +0xc the scale, +0x10 the position, +0x1c the angles
    Models::ObjectState state;         // +0x64, +0x68, +0x6c, +0x70 and +0x58
    std::int32_t field04 = 0;          // +0x4, 0 from 0x606660; the doors of the 30 read it
    std::uint8_t field08 = 0;          // +0x8, byte written by map 30's creation
    float field84 = 0.0f;              // +0x84, FP store of map 30/type 41
    std::uint8_t field91 = 0;          // +0x91, byte written by map 30's creation
    // +0x8a: 4 when 0x5ff960 opens the 9 or the 10 of 11..17; 1 in the 3 of 18..23 that 0x4055b0 picks.
    std::int16_t field8a = 0;
    std::int32_t field60 = 0;     // +0x60, the count of the 3 of 18..23 (0x4055b0 sets it to 10, 0x4059d0 lowers it)
    glm::vec3 light{};            // +0xf4..+0xfc, added to the terrain's
    float speed = 0.0f;           // +0xd8, keys per step
    float cullingMargin = -30.0f; // +0xdc, margen inicial de 0x6067ae
    float fieldE0 = 50.0f;        // +0xe0, meaning/consumer not modeled
    glm::vec3 velocity{};         // +0xcc..d4, cero nativo606830..60683c
    glm::vec3 copiedAngles{};     // +0x28..+0x30, copy from the map 2 case
    glm::vec3 copiedPosition{};   // +0x34..+0x3c, copy from the map 2 case
    glm::vec3 basePosition{};     // +0x17c, second initial copy of the position
    glm::vec3 boundsMinimum{-40.0f, -40.0f, 0.0f}; // +0x124..+0x12c
    glm::vec3 boundsMaximum{40.0f, 40.0f, 80.0f};  // +0x130..+0x138
    bool alive = true;                             // +0x0
    float alpha = 1.0f;                            // +0x174
    float targetAlpha = 1.0f;                      // +0x170
    bool vertexLight = true;                       // +0xe8
    Models::AnimationState animation;              // +0x111 the action, +0x114 the frame
    // +0x16c: seen in the last draw; 0x606560 only advances those that have it.
    bool seen = false;
    // +8 of its sector (0x7d325d4 + 0x10 · (x · 16 + y), .bss 0): the sector passed the last draw's trapezoid
    // (0x60470b). 0x606560 does the collapse (0x5ff960) with the live ones of a sector that did not pass and clears
    // +0x16c.
    bool sectorPassed = false;
    // +0x1c8: the 16 × 16 cell sector where 0x606660 stored it, from the file's position.
    int sectorX = 0;
    int sectorY = 0;
};

class IWorldObjectCreationServices;

// Image bits of 0x6b88d4: the port's default with static storage, not a guarantee that the original value is
// immutable at run time.
inline constexpr float InitialWorldObjectRandomFactor = std::bit_cast<float>(0x3C23D70Au);

struct WorldObjectCreationContext
{
    // Borrowed and read after scene service 180, like the native global at 0x606985.
    std::int32_t map;
    ObjectScene scene;
    WorldObjectRegistrations& registrations;
    // The binding and the service stay alive and stable for the whole batch; their borrowed data can change. Set
    // between creations, not between emissions.
    IWorldObjectCreationServices* services = nullptr;
    Crt::CrtRandom* random = nullptr; // The same CRT state the effects and the world use.
    // Live byte of 0x7239ec, with the caller's storage/value/synchronization. nullptr does not fabricate an initial
    // value: it keeps the gate until the real producer lends this durable state.
    const std::uint8_t* state7239Ec = nullptr;
    // Live factor of 0x6b88d4, read after the RNG. The caller keeps this storage until the creation ends and
    // synchronizes its changes; a mutable alias can change it between calls or from an earlier emitter. The result
    // keeps no reference. Portable domain: finite factor and product representable in float; binary64/PC53-RN and one
    // binary32 store. Arbitrary x87 control words, traps or masks are not credited.
    const std::reference_wrapper<const float> randomPhaseFactor = std::cref(InitialWorldObjectRandomFactor);
};

enum class WorldObjectCreationError : std::uint8_t
{
    UnsupportedEffect,
    UnsupportedMapHandler,
    InvalidAngle, // ftol with ±infinity or outside [-2^63,2^63), unobserved; −2^63 and NaN (Q-C186) are valid.
};

struct WorldObjectCreationFailure
{
    WorldObjectCreationError error;
    std::size_t recordIndex;
};

struct WorldObjectCreationResult
{
    std::vector<WorldObject> objects;
    // Returned by value, failure keeps the earlier objects/registry/effects and includes the partially created current
    // object. Its pending fields are not a valid result of the failed case: provisional integration state, not a result
    // of the original handler.
    std::optional<WorldObjectCreationFailure> failure;
};

// Trees 0/1: binary64 reciprocal, product and binary32 store; the port's IEEE model keeps ±0/±infinity and credits no
// flags, NaN payloads, traps or universal x87 CW. It is not a scale gate. Stops at the first unimplemented dependency,
// with no rollback or silent fallback; later records are not created. Records stay stable and disjoint from the
// state/results; the caller keeps a unique identity and counter with 1<=nextIdentity and
// records.size()<=UINT64_MAX-nextIdentity, also when restoring the world. Preservation holds only on return by value:
// reserve can throw before consuming IDs; the provider must return normally. Exceptions are not caught or translated.
[[nodiscard]] WorldObjectCreationResult CreateWorldObjects(std::span<const Data::Terrain::TerrainObject> records,
                                                           WorldObjectCreationContext& context);

// +0x58 = −2: 0x6002e0 does not draw it (DAT_077f3b18, the editor mode, is never on).
inline constexpr std::int16_t HiddenObject = -2;

// The castle lantern (Object61.bmd, one bone) and what 0x602ed0 asks for in scene 2 (0x602f21): two flare01 sprites
// (0x5ac), scale 1, additive and unrotated, at the translations of bones 8 and 9 of the global pose buffer (0x68b199c
// and 0x68b19cc), those of the last posed model that has them (Q-C140).
inline constexpr std::int16_t StreetLight = 0x3c;
inline constexpr std::uint16_t StreetLightGlowTexture = 0x5ac;
inline constexpr std::array<std::size_t, 2> StreetLightGlowBones{8, 9};

// The light of the lantern's two sprites with `random`, the rand() 0x602ef4 takes per object: L =
// (random % 30 + 70) · 0.01f (0x6b88d4) stored in float, and (L; L · 0.5; L · 0.2f), each product stored in float
// (0x6b882c and 0x6b8854).
[[nodiscard]] glm::vec3 StreetLightGlowLight(int random);

// Visual projection without an auxiliary registry: applies the direct cases 0/1/2/3/7/8/55 and scenes 2/4. Omits
// 0x606630, the 180/322 emissions and the default chain; not equivalent to the full constructor. Each valid record
// creates another object, with no pool or reuse; outside the grid none is created. An uncredited conversion returns
// failure with the index and the partial prefix: check it before consuming objects, in this projection too.
[[nodiscard]] WorldObjectCreationResult CreateWorldObjects(std::span<const Data::Terrain::TerrainObject> records,
                                                           std::uint8_t map, ObjectScene scene);

// Visual projection with the caller's PRE-EXISTING registry. Applies registrations and direct cases, but omits the
// 180/322 emissions and default handlers (those need the context with services). Records must stay stable and
// disjoint from the state/results during creation. The type domain is the Data reader's sign-extended int16 (the
// native callee takes int32). The caller keeps the registry/counter for the world's lifetime and provides a free
// nextIdentity. Portable precondition: 1<=nextIdentity and records.size()<=UINT64_MAX-nextIdentity; does not
// reproduce a native counter. The table neither keeps alive nor resolves the IDs by itself.
[[nodiscard]] WorldObjectCreationResult CreateWorldObjects(std::span<const Data::Terrain::TerrainObject> records,
                                                           std::uint8_t map, ObjectScene scene,
                                                           WorldObjectRegistrations& registrations);

// 0x604ab0 on an object that was seen: the alpha toward its target (0x4e9980) and, if it did not fall below 0.01, the
// animation at its speed (0x4ec200, with `skeleton`'s action, if it has one) and the map cases: on the 0x37, markers
// 0x54, 0x55, 0x57 and 0x59 hide, whirlpool 0x56 turns −0.6° (alpha 0.2, light (1, 0, 0) and every mesh through the
// blendMesh) and the 0x5a −0.23° (alpha 1 and white light, also through the blendMesh).
void StepWorldObject(WorldObject& object, std::uint8_t map, const Models::Skeleton* skeleton);

struct ObjectModelStepState;

// What 0x604ab0 reads outside the object in the game (CL7, milestone 3).
struct ObjectStepGlobals
{
    // 0x77f36d0: layer 1 of the hero's cell, the one MoveHero writes (0x59e2ba). In the .bss, 0.
    std::int32_t heroCellLayer = 0;
    // 0x5767b88: the world clock, in ms.
    float worldClock = 0.0f;
    // 0x7b4ccc4 and 0x743a198: the step's durable destinations; no local buffers are fabricated.
    Terrain::TerrainLighting* dynamicLighting = nullptr;
    Particles::Particles* particles = nullptr;
    // The doors query the published hero and the editor mode, before moving or sounding.
    const glm::vec3* heroPosition = nullptr;
    bool editorMode = false;
    // 0x41ed10(id, 0, 0): the caller keeps or emits the request, even in consecutive steps.
    std::function<void(Audio::SoundId)> sound{};
    // 0x41ed10(id, 0, 1): the same request, looped (the 9's ambient sound, 0x604b9b).
    std::function<void(Audio::SoundId)> loopedSound{};
    ObjectModelStepState* modelState = nullptr;
    // 0x518cf0: the model effects table, where the 1's 0x34 drops stone 0xda and the 4's 0x18 the jet 0x5de; the jet
    // opens and closes its own record (0x7681eb4) with its access count.
    Effects::ModelEffects* effects = nullptr;
    std::uint8_t* ownRecordDepth = nullptr;
    // The hero's +0x111 (0x743a170): the action that 0x52f550's kick looks at in the 4's 0x26 and 0x27.
    const std::uint8_t* heroAction = nullptr;
    // 0x6b8544 (timeGetTime): the window of the 31's butterflies and its ambient sound (0x43ffd8, Q-C304).
    std::function<std::uint32_t()> systemTime{};
    // 0x723c4c: when the 31's ambient sound 0x1ef last played, in system ms. In the .bss, 0.
    std::uint32_t* trialsAmbientTime = nullptr;
    // 0x7239ec: the siege byte that 0x4294b0 reads in the 30's cases (0x42c850). Without it, the 30 cuts.
    const bool* siegeActive = nullptr;
    // 0x7239e4: the flags of the 216 for the visibility of marker 42 during the war.
    const std::uint32_t* siegeCrownFlags = nullptr;
};

// 0x604ab0 with an object seen in the game, on maps 0 to 4, 6 to 17, 30, 31, 33 and 34 (*static*, 06 § Los objetos
// del juego; those of the 1, the 4 and the 6, MapScripts/Dungeon/DungeonObjectStep.h; those of the 7,
// MapScripts/Atlans/AtlansObjectStep.h; those of the 9 and 11 to 17, Events/EventMapObjectStep.h; those of the 8, with
// type 8 animated at four times the speed, MapScripts/Tarkan/TarkanObjectStep.h; those of the 30, 0x42c850,
// CastleSiege/CastleSiegeObjectStep.h; those of the 31, 0x43ffc0, StepLandOfTrialsObject, and those of the 34,
// 0x4303e0, StepCrywolfObject, in WorldObjects/WorldObjectStepServices.h). Before the alpha, the roofs: the 0x7d and
// the 0x7e of the 0 take the alpha target (+0x170) to 0 with the hero's layer at 4 (0x604bc4), and the 0x51, 0x52,
// 0x60, 0x62 and 0x63 of the 2 with the layer at 3 or from 10 up (0x604c06); to 1 otherwise. After the
// alpha/animation, the map cases: UV and flicker, lamps 0x5a/0x96 to the dynamic destination, fires of 0x52f360 to the
// durable pool and map 2's doors with borrowed hero position/editor/sound. A missing binding cuts where the ASM does,
// keeping the earlier RNG, particles and stores. On the 0x37 StepWorldObject's part runs; the other maps cut before
// touching the object.
[[nodiscard]] Core::Expected<void, std::string> StepWorldObjectChecked(WorldObject& object, std::uint8_t map,
                                                                       const Models::Skeleton* skeleton,
                                                                       Crt::CrtRandom& random,
                                                                       const ObjectStepGlobals& globals);

// What 0x5ffdf0 and 0x6002e0 do with an object in the draw.
enum class ObjectDrawKind : std::uint8_t
{
    // Nothing: the alpha below 0.01 or +0x58 at −2.
    Hidden,
    // A RenderBody(2, alpha, blendMesh, blendMesh light, U, V, hidden mesh, −1) with the model's color.
    Body,
    // Map 0x37's whirlpool 0x5a: 0x4ecc30(0, 0x80, 1, blendMesh, 1, U, V, −1), mesh 0 alone, with GL_ZERO,
    // GL_ONE_MINUS_SRC_COLOR, white and unlit.
    SubtractiveMesh,
    // Map 0's 0x69 (Waterspout, 0x600a4a): four loose 0x4ecc30 instead of RenderBody, meshes 0 to 2 with the blendMesh,
    // the object's light and U = V = 0, and mesh 3 with blendMesh 3, the light and the object's U and V.
    Waterspout,
    // Map 8's 0x51 (0x600c81): mesh 0 with 0x4ecc30(0, 2, …, −1) and again with Chrome01's additive chrome,
    // 0x4ecc30(0, 0x44, …, 0x5c0).
    TarkanChrome,
};

struct ObjectDraw
{
    ObjectDrawKind kind = ObjectDrawKind::Hidden;
    // 0x5ffdf0 posed it in the global pose buffer (0x5fff7a): all but those with alpha below 0.01, which exit earlier
    // (0x5ffe3d); also those with +0x58 at −2, which 0x6002e0 does not draw.
    bool posed = false;
    float alpha = 1.0f;
    Models::ObjectState state;
    // The model's +0x48..+0x50, set by 0x5ffce0.
    glm::vec3 color{1.0f};
    bool vertexLight = false;
};

// `terrainLight` is the terrain's at the object's place (0x5f99e0).
[[nodiscard]] ObjectDraw DrawOf(const WorldObject& object, const glm::vec3& terrainLight, std::uint8_t map);

// Lorencia's fountain (Waterspout01): the only map 0 case in 0x6002e0 (0x600a4a) and one of the two of 0x602ed0
// (0x603011). Its creation sets its blendMesh to 3 (06 § Lo propio de Lorencia).
inline constexpr std::int16_t Waterspout = 0x69;

// 0x603011, the 0x69 case in 0x602ed0, after the common rand() of 0x602ef4 (*static*): the blendMesh light (+0x68) at
// 1, the V (+0x70) with the world clock `worldClock` (0x5767b88), −(ftol(T) % 1000) · 0.001f like the step's 0x76, and
// one rand(): odd, nothing more. Even, two 0x5f2 drops (0x53fe70) at bones 1 and 4 (0x68b184c and 0x68b18dc), each at
// (rand() % 32 − 16, −20 or −80, rand() % 32 − 16) from the bone. MuRet does not have the game's particles
// (0x743a198): it cuts at the first one (0x6030c7) after its two rand(). Without a credited ftol of the clock, it cuts
// at 0x60301e with the light already at 1, without touching the V or taking a rand().
[[nodiscard]] Core::Expected<void, std::string> StepTheWaterspoutFrame(WorldObject& object, Crt::CrtRandom& random,
                                                                       float worldClock);

// An object's 16 × 16 cell sector, 0 to 15: the one 0x606660 gave it with the file position times 0.000625f, the
// unrounded product (the x87) and ftol's low dword. Outside the grid, 0x606660 does not create it; with a NaN, the
// sector is 0 (Q-C186).
[[nodiscard]] int SectorX(const WorldObject& object);
[[nodiscard]] int SectorY(const WorldObject& object);
// A coordinate's sector, as 0x606660 computes it (0 to 15, or −1): for an object that does not come from a .obj, like
// the logo.
[[nodiscard]] int SectorOf(float coordinate);

// 0x6046a0's draw order: the sectors by x and then by y (0x7d325d0 + (x · 16 + y) · 0x10) and, in each one, the order
// in which they were created. Returns indices into `objects`.
[[nodiscard]] std::vector<std::size_t> DrawOrder(std::span<const WorldObject> objects);
} // namespace Client::WorldObjects
