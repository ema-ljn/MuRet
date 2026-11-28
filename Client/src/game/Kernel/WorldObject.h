#pragma once

#include "Audio/SoundId.h"
#include "Models/ObjectState.h"
#include "Models/Pose.h"

#include <glm/ext/vector_float3.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>

namespace Client::Effects
{
class ModelEffects;
} // namespace Client::Effects

namespace Client::Particles
{
class Particles;
} // namespace Client::Particles

namespace Client::Terrain
{
struct TerrainLighting;
} // namespace Client::Terrain

// A world's object (0x1d4 bytes) and what its step reads outside it: what game/WorldObjects, the map scripts, the
// events and the siege take. The objects' logic is game/WorldObjects'.
namespace Client::Kernel
{
// 06 § Lo propio de Lorencia: 0x606630's auxiliary registry, separate from the objects and actors.
// Modern non-owning identity: 0 represents the null argument, not a live object.
using WorldObjectIdentity = std::uint64_t;
inline constexpr std::size_t WorldObjectRegistrationCount = 200;

struct WorldObjectRegistration
{
    std::uint8_t active = 0;
    // Original +1..+7: the registry neither interprets nor reinitializes those bytes.
    std::array<std::uint8_t, 7> metadata{};
    WorldObjectIdentity object = 0;

    friend bool operator==(const WorldObjectRegistration&, const WorldObjectRegistration&) = default;
};

// The caller's durable state. The defaults pick an empty port state; they do not prove the native reset.
// `nextIdentity` is the next free ID in this world, also when restoring preexisting entries. The caller does not
// reuse IDs while an entry may refer to them, nor allows overflow. The entries store identities, not addresses:
// moving or reallocating objects does not invalidate them. Copying a WorldObject keeps a snapshot's identity; it does
// not create another world entity.
struct WorldObjectRegistrations
{
    std::array<WorldObjectRegistration, WorldObjectRegistrationCount> entries{};
    WorldObjectIdentity nextIdentity = 1;
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
    float cullingMargin = -30.0f; // +0xdc, the initial margin of 0x6067ae
    float fieldE0 = 50.0f;        // +0xe0, meaning/consumer not modeled
    glm::vec3 velocity{};         // +0xcc..d4, the native zero of 0x606830..0x60683c
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

// +0x58 = −2: 0x6002e0 does not draw it (DAT_077f3b18, the editor mode, is never on).
inline constexpr std::int16_t HiddenObject = -2;

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

// model+88: a selection shared per type, not a per-instance field. Absent equals the loaded model's initial -1. The
// owner keeps or clears the table with its models' lifecycle.
struct ObjectModelStepState
{
    std::map<std::int16_t, std::int8_t> unlitMeshes;
};

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
} // namespace Client::Kernel
