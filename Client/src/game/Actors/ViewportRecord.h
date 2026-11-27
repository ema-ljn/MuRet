#pragma once

#include "Core/Expected.h"
#include "Protocol/FrontEnd/CharacterName.h"

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

namespace Client::Scenes
{
struct WorldEntryResources;
} // namespace Client::Scenes

// What the viewport's creations share (the players, the 45, the DF and the monsters of MonsterViewport.h). Contract:
// Client/docs/research/06-render-escenas-y-ui.md, "Los otros jugadores en el viewport".
namespace Client::Actors
{
// A write of the protected cell (+0x41c or +0x420) with its access open: 4 rand() on closing if no access is left open.
void WriteCell(std::uint8_t value, std::uint8_t& cell, std::uint8_t& depth, Crt::CrtRandom& random);
// A read, with its access (0x40b800 or the inline close).
[[nodiscard]] std::uint8_t ReadCell(const std::uint8_t& cell, std::uint8_t& depth, Crt::CrtRandom& random);
// The X and then the Y of the protected cell, and +0x39a/+0x39b with the same values.
void WriteTheCells(Kernel::ActorState& actor, std::uint8_t x, std::uint8_t y, Crt::CrtRandom& random);
// (high nibble − 1) · 45 (0x6b8848, with the 1 from 0x6b87d4).
[[nodiscard]] float AngleOf(std::uint8_t directionAndPk);
// The low nibble of the same byte.
[[nodiscard]] std::uint8_t PkOf(std::uint8_t directionAndPk);
// The X and then the Y, read from the table, to the center of the cell.
void PutInTheCellCenter(Kernel::ActorState& actor, Crt::CrtRandom& random);
// The Y, the X and 0x4eac00(X, Y, target, +0x3e8, 0); +0x378 at 1 only if there is a path.
[[nodiscard]] Core::Expected<void, std::string> WalkToTheTarget(Kernel::ActorState& actor, std::uint8_t targetX,
                                                                std::uint8_t targetY,
                                                                const Scenes::WorldEntryResources& resources);
// 0x4c88d2..0x4c88dc and 0x4c9307..0x4c9311: "webzen" (0x576960) in the ten bytes of the name, up to its 0.
[[nodiscard]] bool IsHiddenName(const Protocol::FrontEnd::NameBytes& name);
} // namespace Client::Actors
