#pragma once

#include "Core/Expected.h"
#include "Tables/QuestTable.h"
#include "Tables/TextTable.h"
#include "Quests/QuestState.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace Client::Crt
{
class CrtRandom;
}

namespace Client::Kernel
{
class WorldState;
struct OwnInventory;
struct WorldRuntime;
} // namespace Client::Kernel

// The functions of the quest object [0x710148] (QuestState) that do not draw: the state arriving in the A0, the A1 and
// the A2, the quest.bmd conditions, the requested items and the window's opening. Contract:
// Client/docs/research/06-render-escenas-y-ui.md, "Las misiones", and Q-C840 to Q-C845 in 07.
namespace Client::Quests
{

// What the functions that open the own record or show a dialog read.
struct QuestContext
{
    Kernel::WorldState& world;
    // The CRT rand(): the own record's closes (3848 for each one that leaves it at 0).
    Crt::CrtRandom& random;
    // Text.bmd: the 609 of 0x419710's empty answers. nullptr: a dialog without answers cuts.
    const Tables::TextTable* texts = nullptr;
};

// +0x2d + class + 0x14 · request of the record (0x418f58, 0x418fe3, 0x4194ef, 0x419b68 and, in the window's drawing,
// 0x41b8b9): the mark of +4's class & 7, which is not clipped. A class 5, 6 or 7 reads the dialogs of the same
// request, and the 0xff before the first A0 (0x41cb60 and 0x4c2480) reads 0xff bytes after the first mark: another
// request or a condition (*static*). The two padding bytes at +0x166, which MuRet does not keep, are not reached with
// any class (0x14 · request + 7 + class gives neither 0x140 nor 0x141).
[[nodiscard]] std::uint8_t QuestClassFlagOf(const Tables::QuestRecord& quest, std::size_t entry,
                                            std::uint8_t characterClass);

// 0x4195c0(index) (and 0x419560 with an index): the byte index / 4 (signed division) shifted 2 · ((index − index / 4)
// & 0xf) bits, & 3, of the `runtime.quest` object. The 8-bit SHR gives 0 with a count of 8 or more: only quests 0 to 3
// are read where the GS writes them (Q-C841). From 200 to 255 it reads the fields that follow the 50 bytes (+0x2137e to
// +0x2138b, with the window of +0x21383 in `runtime.panels`): the 255 of Q-C842's wraparound reads the high byte of the
// zen at +0x21388, 0 with the real quest.bmd. A negative index or one from 256 gives 0 in MuRet.
[[nodiscard]] std::uint8_t QuestStateOf(const Kernel::WorldRuntime& runtime, std::int32_t index);

// 0x419560(−1): the state of the current quest (+0x2137e, unsigned) in +0x21386.
void ReadTheCurrentQuestState(Kernel::WorldRuntime& runtime);

// 0x558410(type, count, level): the 64 inventory cells with that type (the short at +0, signed), occupied (+0x3c > 0)
// and, if the level is not −1, with an equal ((+4 >> 3) & 0xf). 0 if it reaches the count; otherwise, how many are
// missing.
[[nodiscard]] std::int32_t MissingQuestItems(const Kernel::OwnInventory& inventory, std::int32_t type,
                                             std::int32_t count, std::int32_t level);

// 0x4ddad0 → 0x418e20(pkt + 4, pkt[3], hero class): the class (& 7 and >> 3) in +4 and +5, +0x2138d at 1 and 0x6eeda8
// at 0 (the class arrives as an unsigned byte: it is never −1); the 50 state bytes at 0 and `count` packet bytes on
// top; the first of the `count` quests that is not done in +0x2137e and +0x2137f; and the hero's +0x390 at 1 if quest 3
// is done. `bytes` are those the packet carries after pkt[3]: the original copies `count` even if the packet is
// shorter (the GS 0.97 sends the count without the bytes, Q-C840); what is missing goes at 0 (P-C171). A count over 50
// goes on through the object's fields (+0x2137e.., also the +0x2138d just set and the corner) up to its end, +0x21398,
// as the original does; what would go past it is not written (Q-C1126). Returns how many bytes were left out.
[[nodiscard]] std::size_t ApplyTheQuestList(Kernel::WorldState& world, std::uint8_t count,
                                            std::span<const std::uint8_t> bytes);

// 0x418ee0(index, state), from the A1 and the A2: +0x2137e = index, +0x2137f = the greater, the whole byte index >> 2
// with `state` (the GS sends the byte with the four quests, Q-C843) and the hero's +0x390 by quest 3. From quest 200 on
// the byte is one of the fields that follow the 50 (also the +0x2137e and +0x2137f just set, Q-C1126).
void SetTheQuestState(Kernel::WorldState& world, std::uint8_t index, std::uint8_t state);

// Whether `error` is the one of a record outside the 200 at +0xc (Q-C842): the original keeps reading past the object,
// memory MuRet does not model. The packets that get there (the A1 and the A2) discard it with a warning.
[[nodiscard]] bool ReadsAQuestOutsideTheRecords(std::string_view error);

// 0x418f40(record, slot): dialog `slot` (0 to 3) of the first request with +4's class mark. Without any, −−+0x2137e,
// 0x419600(0xff) and +0x21384. A record outside the 200 cuts (0 − 1 is 255, Q-C842).
[[nodiscard]] Core::Expected<std::int16_t, std::string> QuestDialogOf(const QuestContext& context, std::size_t record,
                                                                      std::size_t slot);

// 0x4194c0(record): false (and the missing-items dialog, 0x418f40(record, 1), in +0x21384) if any of the class's item
// requests is short.
[[nodiscard]] Core::Expected<bool, std::string> HasTheQuestItems(const QuestContext& context, std::size_t record);

// 0x418fb0(record, withZen): for each request of the class, each linked condition (+1 equal to the request's +6, or
// 0xff): the required quest done; the minimum and maximum level with the own record opened and closed once each; and
// the zen: without `checkZen` it only stores it in +0x21388; with it, it reads it with the record open, stores it and
// compares it. The first one not met leaves its dialog in +0x21384 and 5 in +0x21386, and gives false.
[[nodiscard]] Core::Expected<bool, std::string> CheckTheQuestConditions(const QuestContext& context, std::size_t record,
                                                                        bool checkZen);

// 0x419600(state): the class again if +0x2138d (the class check against −1 never matches, Q-C844); with 0xff the state
// of the current quest (0x419560(−1)), otherwise `state` in +0x21386; and that state's dialog. Returns +0x21386.
[[nodiscard]] Core::Expected<std::uint8_t, std::string> RefreshTheQuest(const QuestContext& context,
                                                                        std::uint8_t state);

// 0x419b10: with +0x21386 at 1, true if there is some item request of the class and none is short. Cuts with the
// current quest outside the 200 records (Q-C842) or more than 16 requests.
[[nodiscard]] Core::Expected<bool, std::string> CanCompleteTheQuest(const Kernel::WorldState& world);

// 0x4198b0(quest): the quest (−1: the current one), 0x78abb00 at 0, the window at 1 (the NPC dialog), 0x419600(0xff)
// and 0x419710 with +0x21384.
[[nodiscard]] Core::Expected<void, std::string> OpenTheQuestDialog(const QuestContext& context, std::int32_t quest);

// 0x419840(quest), from 0x6599a0(8): +0x21380 and +0x21381 at 0, the window at 2 (the list), and 0x419600(0xff) and
// 0x419710 with the highest (+0x2137f) in +0x2137e, which then returns to the previous one.
[[nodiscard]] Core::Expected<void, std::string> OpenTheQuestList(const QuestContext& context, std::int32_t quest);

// 0x4c2480 (0x4c268a..0x4c26d2), the return to selection: +4 at 0xff (the A0 is requested again, Q-C215), +0x21381,
// +0x21384, the window, +0x21382, +0x2137e and +0x2137f at 0, the 50 state bytes and +6..+8 at 0 (+9 stays).
void ResetTheQuests(QuestState& quest, std::uint8_t& window);
} // namespace Client::Quests
