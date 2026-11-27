#pragma once

#include <cstdint>
#include <optional>
#include <span>

#include "Actors/ActorPickingBox.h"
#include <glm/ext/vector_float3.hpp>

namespace Client::Terrain
{
struct TerrainPickingSegment;
}

namespace Client::Kernel
{
class WorldState;
} // namespace Client::Kernel

// The picking of the game's characters: 0x59e700, which 0x59ec80 calls with masks 0x22, 1 and 4.
// Contract: Client/docs/research/06-render-escenas-y-ui.md, "El picking de los personajes del juego". The box read
// is the last one the drawing published in +0x13c..+0x168; the `pickingBoxes` parameter lends its projected copy.
namespace Client::Actors
{

// 0x59e700(mask) in the game (scene 5: the published hero is not picked, 0x59e70d..0x59e717 and 0x59e952). The
// first 400 with +0x88 inside the mask (+0x88 & mask == +0x88), alive, in view (+0x16c) and with alpha above 0 are
// tinted, the hero included: +0xe8 at 1 (0x59e779) and the own light (+0xf4) by the WORD of +0x1d6, from 0 to 4:
// (0.2, 0.2, 0.2), (−0.4, −0.4, −0.4), (0.2, −0.6, −0.6), (1.5, 1.5, 1.5) and (0.3, 0.2, −0.5); with 5 or more it
// leaves it. With the PK (+0x375) at 6 or more, (−0.4, −0.4, −0.4). Then, of the same ones, without the dying one
// (+0x389), without +0x7c & 0x10000000, without the published hero and with +0x42c, the one with the smallest
// distance² between its position and the camera (0x7d36884) whose box touches the mouse segment (0x622900, from
// 0x7d36834 to 0x7d366c0): that of +0x13c..+0x168 from the last drawing, published from the bounds +0x124..+0x138
// (Q-C100 with the 0x20a). In Lorencia (0) a candidate over layer 4 of the .map, and in Devias (2) over layer 3,
// counts only if it is the layer of the hero's cell (0x77f36d0). The first one wins exact ties; best rounded to
// float can let the next one through, and unordered is accepted too (research13). −1 if none. Each player (+0x88 at
// 1) that passes the first conditions looks for its place in the party first (MatchThePartyMembers) and, at the end,
// the places without a character stay at −1 or −3 (MarkTheMissingMembers). Without a segment (before the first
// drawing), it tints and looks for the party, without picking. No rand().
[[nodiscard]] std::int32_t PickTheCharacter(Kernel::WorldState& world, std::uint8_t mask,
                                            const glm::vec3& distanceOrigin,
                                            const std::optional<Terrain::TerrainPickingSegment>& ray,
                                            std::span<const ActorPickingBox> pickingBoxes = {});
} // namespace Client::Actors
