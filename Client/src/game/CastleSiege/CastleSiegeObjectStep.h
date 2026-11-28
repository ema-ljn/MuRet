#pragma once

#include "Core/Expected.h"
#include "Audio/SoundId.h"

#include <glm/ext/vector_float3.hpp>

#include <cstdint>
#include <functional>
#include <string>

namespace Client::Kernel
{
struct ModelEffectArguments;
struct ObjectStepGlobals;
struct WorldObject;
} // namespace Client::Kernel

namespace Client::Crt
{
class CrtRandom;
}
// The Castle Siege objects (map 30) in the step, outside the draw (06 § Los objetos de Castle Siege). What depends
// on the siege reads byte 0x7239ec through 0x4294b0, updated by the packets of 0x4294c0. The services keep the shared
// CRT, the protected accesses and the emissions in order.
namespace Client::CastleSiege
{

// 0x63741d..0x637466, at the start of the world draw in the 30: the hero in x from 16100 to 19000 and in y from
// 18900 to 21700 (0x6ba704, 0x6ba700, 0x6ba6fc and 0x6ba6f8, with the edges and without NaN) turns on the black fog
// of 0x42b0a0; outside, glDisable(GL_FOG).
[[nodiscard]] bool InTheCastleSiegeFog(const glm::vec3& hero);

// What 0x42b540 reads outside: the siege byte, the music time (0x7239f0), timeGetTime() (0x6b8544) and the sound
// request 0x41ed10(id, 0, 0).
struct CastleSiegePrelude
{
    bool siegeActive = false;
    float* musicTime = nullptr;
    std::function<std::uint32_t()> systemTime{};
    std::function<void(Audio::SoundId)> sound{};
    Crt::CrtRandom* random = nullptr;
    std::int32_t logicSteps = 0;
    glm::vec3 heroPosition{};
    // Each call opens and closes the protected +0x420; two native reads keep two closes.
    std::function<std::int32_t()> heroY{};
    std::function<Core::Expected<void, std::string>(const Kernel::ModelEffectArguments&)> emit{};
    std::function<void(bool)> updateMusic{};
};

// 0x42b540, the third link of 0x6061c0, once per step in the 30, before checking for seen objects.
// With timeGetTime() at T, if (T − 0x7239f0) · 0.001 passes 1800 (more than half an hour) or 0x7239f0 is 0, it stores
// T as a float in 0x7239f0 and picks the music: without siege it drops charge.mp3 and plays castle.mp3 (0x42b5c3).
// Then, always, sound 0x1e2. Without siege it ends there, without rand(), and 0x6061c0 does not go on. With the
// siege (the rand() of sounds 0x1e3 to 0x1e7, rocks 0x12b/0x12c and the 0xdb over the hero), it emits in order with
// the hero's protected accesses interleaved. A missing binding stops where the ASM asks for it.
[[nodiscard]] Core::Expected<void, std::string> StepCastleSiegePrelude(const CastleSiegePrelude& prelude);

// 0x42c850 (0x605a62, after 0x438a90, which does nothing in the 30), with a seen object of the 30 that passed the
// alpha and the animation. It ends the chain with every type. Without siege:
// - the 0: a dynamic light of radius 3, L = ((rand() & 3) + 3) · 0.1f and the color (L; L · 0.6f; L · 0.2f), and
//   +0x58 at −2;
// - the 4: +0x58 at −1 and the U at T · 0.0001f;
// - the 0x2a, the 0x34, the 0x35, the 0x36 and the 0x42: +0x58 at −2;
// - the 0x19f (the spearman of 0x42a760): +0x58 at −1.
// With the siege, the 0 stays at −2 without light, the 0x35 gives off the 0's light, the 0x19f stays at −2 and the 0x42
// reads the whole 0x7239e4: except at 0x20000 it shows the marker, and it always scrolls its V. Without the siege byte
// in `globals`, it stops before touching the object.
[[nodiscard]] Core::Expected<void, std::string>
StepCastleSiegeObject(Kernel::WorldObject& object, Crt::CrtRandom& random, const Kernel::ObjectStepGlobals& globals);

// 0x42c500 (from 0x6063d0, with 0x4055b0 at 0 outside Chaos Castle) with each seen object of the 30, after
// 0x604ab0. The 0x51 and the 0x53: blendMesh 1, the blendMesh light at 1 and the V at T · 0.0002f (the 0x51) or at
// T · −0.0004f (the 0x53). From 0x3d to 0x40, war restores the intact gates and the seen 99 emits a fragment of
// subtype 1, asks for sound 0x1ec and hides mesh 0. Then, always, 0x42b480 (ApplyCastleSiegeObjectState).
struct CastleGateCollapseServices
{
    std::function<Core::Expected<void, std::string>(const Kernel::ModelEffectArguments&)> emit{};
    std::function<void(Audio::SoundId)> sound{};
};
[[nodiscard]] Core::Expected<void, std::string>
StepCastleSiegeSeenObject(Kernel::WorldObject& object, bool siegeActive, float worldClock,
                          const CastleGateCollapseServices& services = {});
} // namespace Client::CastleSiege
