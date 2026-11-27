#pragma once

#include "Core/Expected.h"
#include "Actors/ActorActions.h"
#include "Player/OwnStatHelpers.h"

#include <cstdint>
#include <optional>
#include <string>

namespace Client::Tables
{
struct ItemSetTables;
}

namespace Client::Kernel
{
struct ActorState;
struct OwnCharacterRecord;
} // namespace Client::Kernel

namespace Client::Player
{
struct ItemSetState;
} // namespace Client::Player

// The own equipment on the character: 0x512a30, with 0x505820 and 0x550d10. Contract:
// Client/docs/research/06-render-escenas-y-ui.md, "El equipo propio en el personaje", "El conjunto completo
// de 0x505820" and "Los stats del registro propio", and 04, "El F3 10".
namespace Client::Actors
{
struct Scan505820;

// What 0x512a30 reads and writes outside the character. The caller lends it for the call.
struct ActorEquipmentContext
{
    // [0x7681eb4], the own record: the nine slots it reads, and what 0x550d10 writes, a
    // thiscall on it (0x512ef5).
    Kernel::OwnCharacterRecord& own;
    // The access count of its entry in the protection table (Models::LogicalAccess):
    // 0x512a30 opens it at the start and closes it at the end, with the 3,848 rand() if it leaves it at 0.
    std::uint8_t& ownRecordAccesses;
    // DAT_0743a170, the hero published at that moment: with it, 0x505820 (0x512ee2). 0x550d10 reads its
    // class and writes its +0x358 (0x40d010).
    Kernel::ActorState& publishedHero;
    // [0x743a178] and [0x743a17c] (WorldRuntime::fullSetScan): 0x505820 writes them with the published hero, and
    // 0x550d10 reads them in 0x550990 with whatever is there.
    std::optional<Scan505820>& fullSetScan;
    // That of 0x4f2250, which 0x512a30 calls in the middle (0x512bd4). Its random is the table's, and its
    // items, the Item.bmd that 0x550d10 also reads.
    ActorStopContext stop;
    // [0x6eed3c], for 0x550d10: the tables of 0x40c600 and what 0x40e1b0 and 0x40da20 write, which
    // lasts from one call to the next (WorldState::ItemSets). Without either, 0x550d10 stops at
    // 0x40e1b0 before touching anything (Player::OwnStatsContext).
    const Tables::ItemSetTables* itemSetTables = nullptr;
    Player::ItemSetState* itemSets = nullptr;
    // The cursor's item for 0x54efe0, 0x54f9b0 and 0x54fe10 (HeldItemOf): what is in their globals at the call.
    Player::OwnEquipmentOverride held{};
};

// 0x4053f0: on maps 18 to 23 (`map`), 0x4053a0 (the hands, the wings and the pet without an item, with the level at
// 0) and the event weapon by class (+0x1d4 & 7): the staff 5 5 in the right hand of 0, the crossbow 4 19 in that of
// 2 and the sword 0 16 in both hands of 1, 3 and 4. On the other maps, nothing.
void ArmForTheEventMaps(Kernel::ActorState& actor, std::int32_t map);

// 0x512930: with +0x3e3 at 0, the class's head under an open helmet (+0x1f8) and the scale by class and skin;
// with +0x3e3 other than 0, nothing.
void ApplyHeadAndScale(Kernel::ActorState& actor);

// 0x512a30 on `actor`: with type 0x20a, the models, levels and bytes of the own equipment, the
// standing action and, with +0x3e3 at 0, the clothes, the weapons of maps 18 to 23, the head, the scale, the
// full set if it is the published hero (0x505820, Actors/FullSetScan.h) and the record's stats
// (0x550d10, Player::RecalculateOwnStats). What MuRet does not have stops with an error after what the
// original does before it, and the access to the record closes all the same. It returns what 0x4f2250 asked for,
// if it ran.
[[nodiscard]] Core::Expected<ActorStopResult, std::string> ApplyActorEquipment(Kernel::ActorState& actor,
                                                                               const ActorEquipmentContext& context);
} // namespace Client::Actors
