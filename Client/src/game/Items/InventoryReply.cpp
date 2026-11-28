#include "Items/InventoryReply.h"
#include "Kernel/HelperTable.h"

#include "Core/Expected.h"
#include "Protocol/Items/InventoryList.h"
#include "Models/Cloth.h"
#include "Models/LogicalAccess.h"
#include "Pets/HelperTable.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Kernel/OwnInventory.h"
#include "Items/OwnItemPlacement.h"
#include "Session/WorldEntry.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"

#include <cstddef>
#include <format>
#include <string>

namespace Client::Items
{
void ClearForInventoryReply(const Scenes::WorldEntryResources& resources)
{
    Kernel::WorldState& world = resources.world;
    {
        // 0x4c55c4..0x4c563c opens it and 0x4c5661..0x4c56a3 closes it (0x40b4d0).
        const Models::LogicalAccess access(world.Runtime().ownRecordDepth, resources.cloth.random,
                                           Models::OwnRecordCloseRandoms);
        world.OwnCharacter().ClearEquipmentForInventory();
    }
    // 0x4c56a8..0x4c56e0, with the record already closed.
    world.Inventory().ClearForInventoryReply();
}

Core::Expected<InventoryReplyResult, std::string> ApplyInventory(const Protocol::Items::InventoryList& list,
                                                                 const Scenes::WorldEntryResources& resources)
{
    ClearForInventoryReply(resources);
    // 0x4c5e4f..0x4c5e61: 0x443d40 turns off the live entries of 0x7d335d0's table whose rider (+0x108) is the
    // published hero, and 0x421d60 releases its raven (+0x430), which MuRet does not have in the world.
    Kernel::TurnOffHelpersOf(resources.world.Helpers(), resources.world.Runtime().hero);
    // 0x4c5e6c..0x4c5e9f: the count from p+5, read again after each 0x5bc740, which does not write the packet.
    InventoryReplyResult result;
    for (std::size_t index = 0; index < list.items.size(); ++index)
    {
        const Protocol::Items::InventoryEntry& entry = list.items[index];
        const auto placed = PlaceInventoryItem(entry.slot, entry.item, resources);
        if (!placed.HasValue())
        {
            return Core::Unexpected(
                std::format("el F3 10, la entrada {} (lugar {}): {}", index, entry.slot, placed.Error()));
        }
        if (placed.Value().sound)
        {
            result.sounds.push_back(*placed.Value().sound);
        }
    }
    return result;
}
} // namespace Client::Items
