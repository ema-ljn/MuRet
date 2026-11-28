#include "Actors/ViewportRecord.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Kernel/ActorState.h"
#include "Models/Cloth.h"
#include "Models/LogicalAccess.h"
#include "Pathfinding/PathFinder.h"
#include "Events/Crywolf/CrywolfContract.h"
#include "Protocol/FrontEnd/CharacterName.h"
#include "Session/WorldEntry.h"
#include "Terrain/TerrainDefines.h"
#include "FrontEnd/NameChecks.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace Client::Actors
{
namespace
{
constexpr std::string_view HiddenName = "webzen"; // 0x6c8418 and 0x6c8420
using Terrain::CellSize;                          // 0x6b86b4
constexpr float CellCenter = 0.5f;                // 0x6b882c
constexpr float DirectionDegrees = 45.0f;         // 0x6b8848, with the 1 from 0x6b87d4

// The ten bytes of the name with the 0 at -0x4a (0x4c88b7) or at -0x12 (0x4c92e2) after them.
std::string NameText(const Protocol::FrontEnd::NameBytes& name)
{
    std::string text;
    for (const std::byte value : name)
    {
        if (value == std::byte{0})
            break;
        text.push_back(static_cast<char>(std::to_integer<std::uint8_t>(value)));
    }
    return text;
}
} // namespace

// A write of +0x41c or +0x420: opens the entry, writes and closes it (4 rand() if no access is left open).
void WriteCell(std::uint8_t value, std::uint8_t& cell, std::uint8_t& depth, Crt::CrtRandom& random)
{
    const Models::LogicalAccess access(depth, random, Models::SmallLogicalCloseRandoms);
    cell = value;
}

// A read: opens, reads and closes (0x40b800 or the inline close).
std::uint8_t ReadCell(const std::uint8_t& cell, std::uint8_t& depth, Crt::CrtRandom& random)
{
    const Models::LogicalAccess access(depth, random, Models::SmallLogicalCloseRandoms);
    return cell;
}

// 0x4c8b0c..0x4c8f1b and 0x4c939e..0x4c9523: the protected cell and the target with the same values.
void WriteTheCells(Kernel::ActorState& actor, std::uint8_t x, std::uint8_t y, Crt::CrtRandom& random)
{
    WriteCell(x, actor.cellX, actor.cellXAccesses, random);
    WriteCell(y, actor.cellY, actor.cellYAccesses, random);
    actor.targetX = x;
    actor.targetY = y;
}

// 0x4c8f21..0x4c8f42 and 0x4c950c..0x4c9540: the high nibble of the direction.
float AngleOf(std::uint8_t directionAndPk)
{
    constexpr int DirectionShift = 4;
    return (static_cast<float>(directionAndPk >> DirectionShift) - 1.0f) * DirectionDegrees;
}

std::uint8_t PkOf(std::uint8_t directionAndPk)
{
    constexpr std::uint8_t PkMask = 0xf;
    return directionAndPk & PkMask;
}

// 0x4c8f4b..0x4c905c: the X and then the Y, read from the table, to the center of the cell.
void PutInTheCellCenter(Kernel::ActorState& actor, Crt::CrtRandom& random)
{
    const std::uint8_t x = ReadCell(actor.cellX, actor.cellXAccesses, random);
    actor.position.x = (static_cast<float>(x) + CellCenter) * CellSize;
    const std::uint8_t y = ReadCell(actor.cellY, actor.cellYAccesses, random);
    actor.position.y = (static_cast<float>(y) + CellCenter) * CellSize;
}

// 0x4c907c..0x4c91a3 and 0x4c9684..0x4c972c: the Y, the X and 0x4eac00(X, Y, target, +0x3e8, 0); +0x378 at 1 only if
// there is a path (without one it stays as it was).
Core::Expected<void, std::string> WalkToTheTarget(Kernel::ActorState& actor, std::uint8_t targetX, std::uint8_t targetY,
                                                  const Scenes::WorldEntryResources& resources)
{
    auto& random = resources.cloth.random;
    const std::uint8_t y = ReadCell(actor.cellY, actor.cellYAccesses, random);
    const std::uint8_t x = ReadCell(actor.cellX, actor.cellXAccesses, random);
    const auto& attributes = Scenes::TerrainOf(resources).attributes;
    if (!attributes)
        return Core::Unexpected(std::string("0x4eac00: no current walls for another player's path"));
    Pathfinding::PathFinder finder(attributes->cells);
    // 0x4eac0a..0x4eac41: with a Crywolf altar at 0x6c8e54, the A* flag also on another character's path.
    const auto path = finder.Find({x, y}, {targetX, targetY}, Scenes::CrywolfAltarGoal(resources.world));
    if (!path)
        return {};
    actor.path = *path;
    actor.movement = 1;
    return {};
}

bool IsHiddenName(const Protocol::FrontEnd::NameBytes& name)
{
    return UI::ContainsLikeTheOriginal(NameText(name), HiddenName);
}
} // namespace Client::Actors
