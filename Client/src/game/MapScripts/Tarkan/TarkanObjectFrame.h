#pragma once

#include "Core/Expected.h"

#include <string>

namespace Client::Kernel
{
struct GameObjectPass;
struct WorldObject;
} // namespace Client::Kernel

// 0x602ed0 with map 8 (0x603a3e, table 0x6045f8), after the common rand() of 0x602ef4. Contract:
// 06 § Los objetos de Tarkan.
namespace Client::MapScripts::Tarkan
{

// With the world clock T and s = sin(phase · 0.002f) · 0.3f + 0.7f on the x87 (L, s stored as float):
// - the 0: a sprite 0x5c6 (Spark03) on bone 6 of the global pose buffer, of scale L · 5 and light
//   (s · 0.5882353f; s; s), with phase T;
// - the 0x3f: a 0x716 (Impack03) on bone 2, of scale L · 1.5 and the same light, with phase z angle · 5 + T; the
//   0x40, the same but with the light (L; L · 0.32f; L · 0.32f);
// - the 0x3c: the first time (+0x58 other than −2), twenty smokes 0x5f2/6 with the object's light (+0xf4) and
//   scale; then +0x58 = −2;
// - the 0x46: +0x58 = −2 and, with rand() % 5 == 0, a 0x5f2/7;
// - the 0x4c: +0x58 = −2 and, with ftol(T) % 5000 above 4500, a 0x5f2/4;
// - the 0x53: +0x58 = −2 and, with ftol(T) % 10000 between ftol(z angle) · 10 + 3500 and + 4000 (borders
//   excluded), a 0x5f2/8; with rand() % 3 == 0, also a 0x5f2/4 shifted ((rand() & 127) − 64) in x and y, at half
//   scale, and the pebble 200 + rand() % 2 (0x518cf0) on the object.
// The smokes go to the particles' shared pool (0x53fe70) with the borrowed clock, with no owner; a full one is not
// an error. Without the pool, the sprites or the effect table it cuts with its address, after what it already did.
[[nodiscard]] Core::Expected<void, std::string> StepTarkanObjectFrame(Kernel::WorldObject& object,
                                                                      const Kernel::GameObjectPass& pass);
} // namespace Client::MapScripts::Tarkan
