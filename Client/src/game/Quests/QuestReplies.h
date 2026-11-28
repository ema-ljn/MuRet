#pragma once

#include "Core/Expected.h"

#include <string>

namespace Client::Protocol::Quests
{
struct QuestFlag;
struct QuestList;
struct QuestStateChange;
struct SetQuestResult;
struct QuestResult;
} // namespace Client::Protocol::Quests

namespace Client::Kernel
{
class WorldState;
} // namespace Client::Kernel

namespace Client::Scenes
{
struct WorldEntryResources;
} // namespace Client::Scenes

// The incoming 9F and A0..A3 (CL9 milestone 2), over WorldRuntime's quest object. Contract: 04 § Las misiones y los
// NPC, and 06, "Las misiones".
namespace Client::Quests
{

// 0x4ddaa0: with pkt[3] from 0 to 3, +6 + pkt[3] of [0x710148] = pkt[4]; otherwise, nothing.
void ApplyQuestFlag(const Protocol::Quests::QuestFlag& flag, Kernel::WorldState& world);

// 0x4ddad0 → 0x418e20 (ApplyTheQuestList). What the copy would write past the object is discarded with a warning in
// resources.log (Q-C1126).
void ApplyQuestList(const Protocol::Quests::QuestList& list, const Scenes::WorldEntryResources& resources);

// 0x4ddb00: 0x418ee0(pkt[3], pkt[4]), 0x657a90 (all panels: the open quests send the C1 03 31 with
// resources.sendWindowClose) and 0x4198b0(−1), the NPC dialog window. A record outside the 200 in the dialog (Q-C842)
// discards what is missing with a warning in resources.log, without cutting.
[[nodiscard]] Core::Expected<void, std::string> ApplyQuestState(const Protocol::Quests::QuestStateChange& change,
                                                                const Scenes::WorldEntryResources& resources);

// 0x4ddb40: with pkt[4] at 0, the same as the A1 with pkt[3] and pkt[5]; otherwise, nothing.
[[nodiscard]] Core::Expected<void, std::string> ApplySetQuestResult(const Protocol::Quests::SetQuestResult& result,
                                                                    const Scenes::WorldEntryResources& resources);

// 0x4ddb80: sound 0x66 (lastSoundId, CL10) and the key's character (0x5110d0: the 400 with none). With 0xc8 and 0xca,
// the hero adds pkt[6] to the points at +0x68 of the own record; with 0xc9, the class ((pkt[6] & 0x10) | (pkt[6] >> 4))
// >> 1 in the character's +0x1d4 and, if it is the hero, in +0xb of the own record. Each own record access opens and
// closes it (3848 rand() if it leaves it at 0). All four go on with fifteen turns of joints with the character as owner
// (0x530860: the 0x60f/0 at its position; in 0xc9, 0xca and 0xcb, also the 0x60f/0xa from 200 higher) and the decal
// 0x61e/0 (0x518cf0). The 0xc8 ends with sound 0x65; the other three, with 0x513100 (Actors/ClassParts.h), action
// 0xb8 (0x4ea030) and the 0x66 again. With another type, nothing more. Without Player.bmd, a player cuts before
// 0x513100.
[[nodiscard]] Core::Expected<void, std::string> ApplyQuestResult(const Protocol::Quests::QuestResult& result,
                                                                 const Scenes::WorldEntryResources& resources);
} // namespace Client::Quests
