#pragma once

#include "Core/Expected.h"

#include <cstdint>
#include <string>

namespace Client::Kernel
{
struct ObjectModelStepState;
struct ObjectStepGlobals;
struct WorldObject;
} // namespace Client::Kernel

// The Tarkan cases of 0x604ab0 (map 8, table 0x605cc0 by type − 2), after the common alpha and animation.
// Contract: 06 § Los objetos de Tarkan.
namespace Client::MapScripts::Tarkan
{

// 0x61a145..0x61a18f, at the end of the world models' load (0x616ec0): on map 8, the model+0x88 of 11, 12, 13, 73,
// 75 and 79 at 0, so their mesh 0 goes unlit and with the U and the V of its step. The other maps do not touch it.
void SeedTarkanModelState(std::uint8_t map, Kernel::ObjectModelStepState& state);

// 0x604c70..0x604c8e: on map 8, type 8 animates with the speed (+0xd8) times 4 (0x6b8a44) in a copy; +0xd8 does
// not change. The others, with their own.
[[nodiscard]] float TarkanAnimationSpeed(std::int16_t type, float speed);

// Map 8, with the world clock T and L = sin(T · 0.002f) · 0.35f + 0.65f (the sine on the x87):
// - the 2: blendMesh 0 and the U at (−ftol(T) % 1000) · 0.001f;
// - the 4: blendMesh 0, the blendMesh light at L, the V at (−ftol(T) % 10000) · 0.0001f and a dynamic light
//   (L; L; L) of radius 3;
// - the 7: blendMesh 0 and the blendMesh light at L with T + z angle · 100, and a light (L; L · 0.6f; L · 0.2f) of
//   radius 3;
// - the 0xb, the 0xd, the 0x49, the 0x4b and the 0x4f: the V at (−ftol(T) % 10000) · 0.0002f; the 0x48 too, plus
//   the blendMesh at 0;
// - the 0xc: the U and the V at (−ftol(T) % 50000) · 0.00005f;
// - the 0x3d, the 0x41 and the 0x42: blendMesh 1, the V at (−ftol(T) % 1000) · 0.001f and a light
//   (L; L · 0.6f; L · 0.2f) of radius 2, without touching the blendMesh one;
// - the 0x3f and the 0x40: +0x58 = −2;
// - the 0x52: blendMesh 0 and the object's light (+0xf4) at (1; 1; 1).
// A clock without a credited ftol or a missing light destination cuts with its address after what it already
// wrote.
[[nodiscard]] Core::Expected<void, std::string> StepTarkanObject(Kernel::WorldObject& object,
                                                                 const Kernel::ObjectStepGlobals& globals);
} // namespace Client::MapScripts::Tarkan
