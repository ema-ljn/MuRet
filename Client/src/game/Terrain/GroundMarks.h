#pragma once

#include "Core/Expected.h"
#include "Data/Terrain/TerrainHeights.h"
#include "Render/ModelBatch.h"

#include <glm/ext/vector_float3.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>

namespace Client::Crt
{
class CrtRandom;
} // namespace Client::Crt

// The ground marks: the table of 100 places of 0x78 bytes at 0x760cab0..0x760f990, in the .bss, which 0x548fe0 fills,
// 0x549090 ages on each game step and 0x549130 draws with the terrain (0x5fc365). The map load does not clear it
// (Q-C228). Contract: Client/docs/research/06-render-escenas-y-ui.md, "Las marcas del suelo".
namespace Client::Terrain
{
// A place of the table. Of its 0x78 bytes, these functions touch only these fields.
struct GroundMark
{
    // +0
    bool live = false;
    // +4: the type, which is also its texture's slot: both creators set 0x5e3, Effect\blood01.tga.
    std::int32_t type = 0;
    // +0x10: the decal's side, in cells.
    float size = 0.0f;
    // +0x14..+0x1c, in the world. The z is copied and nobody reads it.
    glm::vec3 position{};
    // +0x28: the turn, in degrees; the drawing passes it negated.
    float angle = 0.0f;
    // +0x2c..+0x34
    glm::vec3 color{};
    // +0x38
    float alpha = 0.0f;
    // +0x3c, in game steps.
    std::int32_t life = 0;

    friend bool operator==(const GroundMark&, const GroundMark&) = default;
};

// 0x548fe0..0x54900a: the 100 places, (0x760f990 − 0x760cab0) / 0x78.
inline constexpr std::size_t GroundMarkCount = 100;
using GroundMarkTable = std::array<GroundMark, GroundMarkCount>;

// The arguments of 0x548fe0, in their order.
struct GroundMarkRequest
{
    std::int32_t type = 0;
    glm::vec3 position{};
    float angle = 0.0f;
    glm::vec3 color{};
    float size = 0.0f;
};

// 0x548fe0: in the first place that is off, the live mark, with the alpha at 1 and, with type 0x5e3, 0x5e4 or 0x606,
// the life at rand() % 32 + 50 (0x54906e); with another type the life stays as it was. Without a place that is off it
// turns off the one with the least life (the first, on a tie) and does not place the new one, without rand()
// (Q-C231). It fails, without touching the table, if moreover no life is below 9,999: the original writes a 0 at the
// type's address.
[[nodiscard]] Core::Expected<void, std::string> AddGroundMark(GroundMarkTable& marks, const GroundMarkRequest& request,
                                                              Crt::CrtRandom& random);

// 0x549090, from the game step (0x63712d): lowers the life of each live mark. 0x5e3 grows 0.004 per side; 0x5e3,
// 0x5e4 and 0x606 take the color (0.1, 0, 0), turn off without life and, below 50, carry the alpha at life · 0.02. 7
// shrinks the side by 0.05 and turns off below 0.1. No rand().
void StepGroundMarks(GroundMarkTable& marks);

// The components of the slot's texture (+0x28 of the 0x7d3a198 slot): no value for a slot MuRet did not upload.
using SlotComponents = std::function<std::optional<std::uint8_t>(std::int32_t slot)>;

// 0x549130, with the depth 0x5fc260 left (0x5fc35b): each live mark, in table order, with the additive blend if its
// type's texture has 3 components and the normal one if not, and the 0x5fb2f0 decal with its side, its color, the
// turn negated, its alpha and 5 above the terrain. It fails at the first mark whose slot MuRet does not know, or if
// the decal fails.
[[nodiscard]] Core::Expected<void, std::string> DrawGroundMarks(Render::ModelBatch& batch, const GroundMarkTable& marks,
                                                                const Data::Terrain::TerrainHeights& heights,
                                                                const SlotComponents& components);
} // namespace Client::Terrain
