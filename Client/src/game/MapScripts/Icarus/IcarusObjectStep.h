#pragma once

#include "Core/Expected.h"

#include <glm/ext/vector_float3.hpp>

#include <cstdint>
#include <functional>
#include <string>

namespace Client::Crt
{
class CrtRandom;
}
namespace Client::Terrain
{
struct TerrainLighting;
} // namespace Client::Terrain

namespace Client::Kernel
{
struct JointRequest;
} // namespace Client::Kernel

namespace Client::Effects
{
class ModelEffects;
} // namespace Client::Effects

namespace Client::WorldObjects
{
struct WorldObject;
} // namespace Client::WorldObjects

namespace Client::Particles
{
class FrameSprites;
} // namespace Client::Particles

// What 0x606560 does in Icarus (map 10) outside 0x604ab0: the prelude 0x6061c0 (with 0x605d70) before the objects
// and 0x6063d0 with each seen object. Contract: 06 § Los objetos de Icarus.
namespace Client::MapScripts::Icarus
{
struct IcarusObjectServices
{
    Crt::CrtRandom& random;
    // The hero's +0x10 (0x743a170).
    const glm::vec3& heroPosition;
    // The durable dynamic light (0x7b4ccc4).
    Terrain::TerrainLighting* lighting = nullptr;
    // 0x518cf0.
    Effects::ModelEffects* effects = nullptr;
    // 0x5491e0.
    Particles::FrameSprites* sprites = nullptr;
    // 0x530860.
    std::function<Core::Expected<void, std::string>(const Kernel::JointRequest&)> createJoint;
};

// 0x6061c0 on map 10, with `count` the previous pass's 0x7d365a0 (not yet reset to zero): 0x605d70 gives the
// selector and, with rand() % 10 == 0, the effect 0x5ac at ±2500 from the hero in x and y (rand() % 5000 − 2500 as
// an integer, each added on the x87) and 1000 lower, with no turn, light 1.
// 0x605d70, with rand() % 50 == 0: a dynamic light of radius 2 at (hero x + rand() % 300 − 150; hero y), with
// L = ((rand() & 3) + 4) · 0.05f and the color (L · 0.3f; L · 0.3f; L · 0.081f) (one rand() is discarded in
// between), the effect 0xbb on the hero with that color, the selector rand() % count if count is not zero, and with
// rand() % 5 == 0 two joints 0x615/0 on one of four sides (rand() & 3), of width rand() % 10 + 40. Returns the
// selector (0 without the first gate).
[[nodiscard]] Core::Expected<std::int32_t, std::string> StepIcarusObjectPrelude(std::int32_t count,
                                                                                const IcarusObjectServices& services);

// 0x6063d0 on map 10: `count` (0x7d365a0) goes up by one; with the selector at zero, nothing else. Otherwise, with
// rand() % 10 == 0 and the type from 0 to 5, a sprite 0x623 of scale 0.5 on the object with the light
// ((rand() % 10) · 0.02f) per channel and two object joints 0x614/6, of width rand() % 20 + 10; if not, count goes
// back down.
[[nodiscard]] Core::Expected<void, std::string> StepIcarusSeenObject(WorldObjects::WorldObject& object,
                                                                     std::int32_t selector, std::int32_t& count,
                                                                     const IcarusObjectServices& services);
} // namespace Client::MapScripts::Icarus
