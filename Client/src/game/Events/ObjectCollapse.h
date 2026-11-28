#pragma once

#include "Core/Expected.h"

#include <string>

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
class Particles;
} // namespace Client::Particles

namespace Client::Kernel
{
struct WorldObject;
struct WorldRuntime;
} // namespace Client::Kernel

// The collapse of the Blood Castle and Chaos Castle objects (CL9 milestone 11, phase 3; 06 § El derrumbe de los
// objetos): 0x5ff930 requests it from the event packets (EventGlobals::objectCollapse) and 0x5ff960 performs it on
// each object the object step walks (0x606560).
namespace Client::Events
{
struct ObjectCollapse;

// What 0x5ff960 and 0x405630 read and write outside the object.
struct ObjectCollapseContext
{
    // The request (0x6c9dd4..0x6c9de0), the current map (0x6c9dd0), the Chaos Castle area (0x6c634c) and the last sound
    // (0x41ed10, CL10).
    Kernel::WorldRuntime& runtime;
    Crt::CrtRandom& random;
    // 0x53fe70: the Blood Castle gate's 0x5f3/0 dust and the Chaos Castle 0x5f6/0 stone. Without the pool, stops where
    // it asks for it.
    Particles::Particles* particles = nullptr;
    // The world clock (0x5767b88) with which 0x53fe70 creates.
    float worldClock = 0.0f;
    // The hero's +0x18 ([0x743a170]): the stone's z.
    float heroZ = 0.0f;
    // 0x7d325c8, the camera shake. nullptr: stops if the branch asks for it.
    float* cameraShake = nullptr;
    // The map's .att (0x7d0d348), which the Blood Castle gate opens when done (0x5f8750). nullptr: stops there.
    Data::Terrain::TerrainAttributes* attributes = nullptr;
};

// 0x5ff966..0x5ff997: the request holds with the map, the type and the count at 0 or more and the requested map equal
// to the current one.
[[nodiscard]] bool ObjectCollapseActive(const Kernel::WorldRuntime& runtime);

// 0x5ff910: the request to −1, −1, −1 and −1.0f.
void ResetTheObjectCollapse(ObjectCollapse& collapse);

// 0x606610..0x606618: the collapse count drops by one at the end of each object step, also from −1 (a 32-bit DEC,
// which wraps around).
void CountDownTheObjectCollapse(Kernel::WorldRuntime& runtime);

// 0x5ff960(object): without an active request, nothing. In maps 18 to 23, 0x405630 (the Chaos Castle steps by the area
// of 0x6c634c, never going on); otherwise objects 9 and 10 with the count at 0 and those of the requested type (the
// Blood Castle gate: the turn of +0x1c with the delay, sound 0x89 with the count at 20, the dust with +0x1c at exactly
// 80 and the end with the count at 0, which removes attribute 8 from (13..15, 70..75) and clears the request). Stops
// where MuRet lacks what the branch asks for, with what came before done.
[[nodiscard]] Core::Expected<void, std::string> CollapseTheObject(Kernel::WorldObject& object,
                                                                  const ObjectCollapseContext& context);
} // namespace Client::Events
