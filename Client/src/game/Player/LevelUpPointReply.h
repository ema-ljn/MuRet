#pragma once

#include "Core/Expected.h"

#include <string>

// The F3 06 reply (0x4d3200, from 0x4e30cd in the F3 sub-dispatcher): the level-up point asked for with the character
// window's buttons (Protocol/Player/LevelUpPoint.h, 0x5e468e). Contract: Client/docs/research/04-protocolo.md, "El F3
// 06 que llega".
namespace Client::Protocol::Player
{
struct LevelUpPointResult;
}

namespace Client::Scenes
{
struct WorldEntryResources;
} // namespace Client::Scenes

namespace Client::Player
{

// 0x4d3200: with pkt[4]'s high nibble (0x4d320c), with the own record open (inline, like 0x40b240, or 0x40ad90's
// creation without the entry): the points to spend (+0x68, a word) minus 1, before the switch (0x4d32d8); the low
// nibble's stat plus 1, by 0x4d34f4's table (0 the strength, 1 the agility, 2 the vitality with pkt[6..7]'s max life, 3
// the energy with the max mana, 4 the command; 5 to 15, none, by the JA of 0x4d32df); and the max BP (+0x3a) from
// pkt[8..9] (0x4d3356). It closes it (like 0x40b4d0: 3,848 rand() calls if no access is left). Then, always
// (0x4d341a..0x4d34ed), it opens it, runs 0x550d10 (RecalculateOwnStats) and closes it.
[[nodiscard]] Core::Expected<void, std::string>
ApplyLevelUpPointResult(const Protocol::Player::LevelUpPointResult& reply,
                        const Scenes::WorldEntryResources& resources);
} // namespace Client::Player
