#pragma once

#include "Core/Expected.h"

#include <cstdint>
#include <string>

namespace Client::Crt
{
class CrtRandom;
} // namespace Client::Crt

namespace Client::Kernel
{
struct ActorState;
} // namespace Client::Kernel

namespace Client::Terrain
{
struct WorldTerrain;
} // namespace Client::Terrain

namespace Client::Pets
{
// What 0x4167b0 needs to create the raven at its owner's cell: the current terrain, the camera's fixed height, and the
// cloth's rand() and open counter.
struct RavenPlacement
{
    const Terrain::WorldTerrain& terrain;
    float fixedHeight = 0.0f;
    Crt::CrtRandom& random;
    std::int32_t openCounterAccesses = 0;
};

// 0x420d60: with the 0x1c17 in the left hand (+0x2d8), releases the raven of +0x430 (0x421d60) and creates it again
// (0x4167b0), on any map. The world entry (0x4c4954) and the transitions use it.
[[nodiscard]] Core::Expected<void, std::string> RecreateTheRaven(Kernel::ActorState& actor,
                                                                 const RavenPlacement& placement);

// 0x420cf0: releases the raven of +0x430 (0x421d60) and, outside maps 18 to 23, creates it again, without looking at
// the hand. The F3 10 calls it when the Dark Raven (0x1a05) reaches slot 1 (0x5bca62).
[[nodiscard]] Core::Expected<void, std::string> ReplaceTheRaven(Kernel::ActorState& actor, std::int32_t map,
                                                                const RavenPlacement& placement);
} // namespace Client::Pets
