#pragma once

#include "Core/Expected.h"

#include <string>

namespace Client::Kernel
{
struct ObjectStepGlobals;
struct WorldObject;
} // namespace Client::Kernel

namespace Client::Crt
{
class CrtRandom;
}

namespace Client::Terrain
{
struct TerrainLighting;
} // namespace Client::Terrain

namespace Client::WorldObjects
{
// 0x604ee9/0x604f5c: lamps 0x5a/0x96 of map 0, on the durable dynamic light.
[[nodiscard]] Core::Expected<void, std::string>
StepObjectLamp(const Kernel::WorldObject& object, Crt::CrtRandom& random, const Kernel::ObjectStepGlobals& globals);

// 0x6050d4: doors of map 2; zero RNG. The sound request follows the opening stores.
[[nodiscard]] Core::Expected<void, std::string> StepDeviasObjectDoor(Kernel::WorldObject& object,
                                                                     const Kernel::ObjectStepGlobals& globals);

// 0x4250f0 (0x605a97), with map 33 or 54 (0x425070): after the chain of 0x605a62, which touches nothing on those
// maps. The V of the 0x19/0x1c, the dynamic light of the 0x1e/0x47 (one rand(), radius 2), the alpha of the 0x29, the
// −2 at +0x58 of those that emit in the draw (0x38..0x3b, 0x3e, 0x3f, 0x43 and 0x46) and the speed of the 0x40.
// Always ends with the sound request 0x246; without an emitter it cuts after the stores.
[[nodiscard]] Core::Expected<void, std::string> StepAidaObject(Kernel::WorldObject& object, Crt::CrtRandom& random,
                                                               const Kernel::ObjectStepGlobals& globals);

// 0x43ffc0 (0x605a62), with map 31 (0x43ff10): after 0x438a90 and 0x42c850, which do nothing on the 31.
// The 1, the 0x2c and the 0x2d release butterfly 0xae /0, /1 and /2 with timeGetTime() & 0x3ff below 10; the
// 0x1b raises and lowers its z with the world clock and its phase (+0x84); the 0x2a adds to the dynamic light
// ((rand() & 3) + 3) · 0.1f times (0.9; 0.2; 0.1), radius 3; the 1, the 3, the 0x2a, the 0x2c, the 0x2d and the 0x35
// are left with +0x58 at −2. With any type, the ambient sound 0x1ef if more than 300000 system ms passed since the
// previous one (0x723c4c). A missing binding cuts where the ASM does, after what is already done.
[[nodiscard]] Core::Expected<void, std::string>
StepLandOfTrialsObject(Kernel::WorldObject& object, Crt::CrtRandom& random, const Kernel::ObjectStepGlobals& globals);

// 0x4303e0 (0x605a62), with map 34 (0x4303c0): after 0x438a90, 0x42c850, 0x43ffc0, 0x42ee40 and 0x4250f0, which do
// nothing on the 34. The 0x29 adds (0.2; 0.7; 0.5) to the dynamic light with radius 2; the 0x39 and the 0x47,
// ((rand() & 3) + 3) · 0.1f times (1; 0.6; 0.2) with radius 3; the 0x51 is left with alpha 0.2 and the 0x52, 0x53 and
// 0x54 with +0x58 at −2. No sound. Without the dynamic buffer, the light cuts after its rand().
[[nodiscard]] Core::Expected<void, std::string> StepCrywolfObject(Kernel::WorldObject& object, Crt::CrtRandom& random,
                                                                  const Kernel::ObjectStepGlobals& globals);
} // namespace Client::WorldObjects
