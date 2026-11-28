#pragma once

#include "Core/Expected.h"
#include "Audio/SoundId.h"

#include <string>
#include <vector>

namespace Client::Protocol::Items
{
struct InventoryList;
}

namespace Client::Scenes
{
struct WorldEntryResources;
} // namespace Client::Scenes

// The F3 10, 0x4c55a0 from 0x4e309c. Contract: Client/docs/research/06-render-escenas-y-ui.md, "El F3 10, el
// inventario", and 04-protocolo.md, "La entrada al mundo: la respuesta del F3 03 y el F3 10".
namespace Client::Items
{

// The Fenrir sounds each 0x4f2250 inside 0x512a30 asks for, in the order it asks for them.
struct InventoryReplyResult
{
    std::vector<Audio::SoundId> sounds;
};

// 0x4c55a0's common initialization (0x4c55c4..0x4c56e0), which runs before checking whether it came encrypted and
// also with the count at 0: opens the own record, empties its equipment (ClearEquipmentForInventory), closes it (3848
// rand() if left with no accesses) and empties both inventory grids.
void ClearForInventoryReply(const Scenes::WorldEntryResources& resources);

// 0x4c55a0 with the encrypted packet: the common initialization; 0x443d40 with the published hero, which turns off
// its horse (TurnOffHelpersOf), and 0x421d60, which in MuRet releases nothing; and 0x5bc740 for each entry, in order
// and including the repeated ones (Items::PlaceInventoryItem). The first one that stops keeps what the earlier ones
// did.
[[nodiscard]] Core::Expected<InventoryReplyResult, std::string>
ApplyInventory(const Protocol::Items::InventoryList& list, const Scenes::WorldEntryResources& resources);
} // namespace Client::Items
