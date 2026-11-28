#pragma once

#include "Core/Expected.h"

#include <glm/ext/vector_float3.hpp>

#include <array>
#include <cstdint>
#include <functional>
#include <string>

namespace Client::Crt
{
class CrtRandom;
}

// The part of the Chaos Castle maps (18 to 23) that does not depend on their packets: what each drawn cell spawns
// (0x405e00) and the alpha of 0x5ffdf0. Contract:
// Client/docs/research/06-render-escenas-y-ui.md, "Los objetos de Chaos Castle".
namespace Client::Kernel
{
struct JointRequest;
struct WorldObject;
} // namespace Client::Kernel

namespace Client::Particles
{
class Particles;
} // namespace Client::Particles

namespace Client::Events::ChaosCastle
{

// A rectangle of cells of 0x405e00, with the edges included: x0, y0, x1, y1.
struct ChaosCastleCellRect
{
    std::int32_t x0 = 0;
    std::int32_t y0 = 0;
    std::int32_t x1 = 0;
    std::int32_t y1 = 0;
};

// 0x6b86d4, 0x6b8714 and 0x6b8754: the four rectangles of areas 0, 3 and 6 of 0x6c634c.
inline constexpr std::array<ChaosCastleCellRect, 4> ChaosCastleAreaZero{
    {{23, 75, 44, 76}, {43, 77, 44, 108}, {23, 107, 42, 108}, {23, 77, 24, 106}}};
inline constexpr std::array<ChaosCastleCellRect, 4> ChaosCastleAreaThree{
    {{25, 77, 42, 78}, {41, 79, 42, 106}, {25, 105, 40, 106}, {25, 79, 26, 104}}};
inline constexpr std::array<ChaosCastleCellRect, 4> ChaosCastleAreaSix{
    {{27, 79, 40, 80}, {39, 81, 40, 104}, {27, 103, 38, 104}, {27, 81, 28, 102}}};

// 0x53fe70(0x5f6, ...): the stone that falls on the cell of 0x405e00.
inline constexpr std::int32_t ChaosCastleFallingStone = 0x5f6;

// What 0x405e00 reads and writes outside the cell. The caller lends it during the pass.
struct ChaosCastleCellContext
{
    // 0x6c634c: the area (0xff in the .data and when building the event object; the CL9.5 packets move it).
    std::uint8_t area = 0xff;
    // The hero's +0x18 (0x743a170): the stone's z.
    float heroZ = 0.0f;
    Crt::CrtRandom* random = nullptr;
    Particles::Particles* particles = nullptr;
    std::reference_wrapper<const float> worldClock;
    // 0x7d325c8, the camera shake.
    float* cameraShake = nullptr;
};

// 0x405e00(x, y) on maps 18 to 23: rand() % 8 and, with 0 and the area at 0, 3 or 6, the first rectangle that holds
// the cell: the stone 0x5f6/0 at (x · 100 + rand() % 30 − 15, y · 100 + rand() % 30 − 15, the hero's z), angles at 0,
// light 1, scale 1.5 and no owner, and with rand() % 5 at 0 the shake at (rand() % 3 − 3) · 0.1f. With another area
// or outside the rectangles, nothing more. It stops without the random source, the pool or the camera when it reads
// them.
[[nodiscard]] Core::Expected<void, std::string> StepChaosCastleCell(std::uint8_t x, std::uint8_t y,
                                                                    const ChaosCastleCellContext& context);

// What 0x405490 looks at in the hero (0x743a170) and the joint 0x6ee/9 it asks for (0x530860).
struct ChaosCastlePreludeContext
{
    Crt::CrtRandom* random = nullptr;
    glm::vec3 heroPosition{};
    glm::vec3 heroAngles{};
    // The hero's +0x3e2: the safe zone.
    bool heroInSafeZone = false;
    std::function<Core::Expected<void, std::string>(const Kernel::JointRequest&)> createJoint{};
};

// 0x405490 on maps 18 to 23, first in 0x6061c0, with the count of the 3 seen in the previous pass (0x7d365a0): with
// rand() % 10 at 0 and the count nonzero, the selector is rand() % count. Otherwise, with the hero outside the safe
// zone and another rand() % 10 at 0, the joint 0x6ee/9 at (x + rand() % 800 − 400, y + rand() % 800 − 400, z − 150)
// of the hero, with its angles, no owner and the scale rand() % 10 + 50. It returns the selector, which without the
// first case stays at 0. It stops without the random source or without whoever creates the joint.
[[nodiscard]] Core::Expected<std::int32_t, std::string>
StepChaosCastlePrelude(std::int32_t count, const ChaosCastlePreludeContext& context);

// 0x4055b0 for each seen object, after 0x604ab0: only the 3, which adds 1 to the count. With the trap armed
// (0x6c634d, 1 in the .data; only the packet of 0x404e20 arms it again) it turns on +0x8a, leaves +0x60 at 10 and
// disarms it. Otherwise, with the selector nonzero it lowers it: +0x60 = 10 and +0x8a at 0, or at 1 if it reached 0.
void StepChaosCastleSeenObject(Kernel::WorldObject& object, std::int32_t& selector, std::int32_t& count,
                               std::uint8_t& trapArmed);

// 0x5ffdf0 (0x5ffdff..0x5ffe24): on maps 18 to 23, with the hero's +0x1a1 on, the object's alpha drops by 0.15f
// (0x6b8ae8) with fsub and fstp, before comparing it with 0.01; it stays stored in the object. Otherwise, unchanged.
[[nodiscard]] float ChaosCastlePoseAlpha(std::int32_t map, std::uint8_t heroByte1a1, float alpha);
} // namespace Client::Events::ChaosCastle
