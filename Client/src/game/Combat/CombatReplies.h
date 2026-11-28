#pragma once

#include "Core/Expected.h"

#include <string>

namespace Client::Protocol::Combat
{
struct KillExperience;
struct Death;
} // namespace Client::Protocol::Combat
namespace Client::Chat
{
struct ChatReceiveContext;
} // namespace Client::Chat

namespace Client::Combat
{
struct ActorCombatContext;

// 0x4cf200 (0x16) and 0x4cfce0 (0x9C), with the decryption flag at 1 (with 0 the session sends the F1 03): the dead one
// from 0x5110d0 (slot 400 if nobody has the key, as with Blood Castle's 0x9C: Q-C1160); with bit 15, 0x4f5660 on it;
// without it, the hero records the kill (+0x380 at 2, the damage in +0x382 and the slot in +0x3a4). Then the damage
// number (0x548e40 with light 1, 0.6, 0 and scale 15), +0x389 at 1 and +0x378 at 0, the experience added to the own
// record (+0x10, 3848 rand() when it closes) and, if not 0, Text.bmd's text 486 with it to the chat (0x555190 of type
// 1, with the name of 0x576798c, or 0x5767990 for the 0x9C: empty, P-C252 d).
[[nodiscard]] Core::Expected<void, std::string> ApplyKillExperience(const Protocol::Combat::KillExperience& reply,
                                                                    const ActorCombatContext& combat,
                                                                    const Chat::ChatReceiveContext& chat);

// 0x4d0a30 (0x17): the dying one (0x5110d0, unmasked) gets +0x389 at 1, +0x378 at 0 and the skill in +0x391. In Blood
// Castle and Chaos Castle (maps 11 to 23), 0x4d07c0 with the TerrainWall cell; elsewhere, skill 0x28 or 0x3b pushes it
// (two rand()) and turns it toward the killer.
[[nodiscard]] Core::Expected<void, std::string> ApplyDeath(const Protocol::Combat::Death& reply,
                                                           const ActorCombatContext& combat);
} // namespace Client::Combat
