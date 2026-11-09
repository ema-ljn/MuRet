#include "Scenes/InventoryReply.h"

#include "Core/Expected.h"
#include "Protocol/Character/InventoryList.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/LogicalAccess.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/OwnInventory.h"
#include "Scenes/OwnItemPlacement.h"
#include "Scenes/WorldEntry.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"

#include <cstddef>
#include <format>
#include <string>

namespace Client::Scenes
{
void ClearForInventoryReply(const WorldEntryResources& resources)
{
    WorldState& world = resources.world;
    {
        // 0x4c55c4..0x4c563c lo abre y 0x4c5661..0x4c56a3 lo cierra (0x40b4d0).
        const Render::Models::LogicalAccess access(world.Runtime().ownRecordDepth, resources.cloth.random,
                                                   Render::Models::OwnRecordCloseRandoms);
        world.OwnCharacter().ClearEquipmentForInventory();
    }
    // 0x4c56a8..0x4c56e0, ya con el registro cerrado.
    world.Inventory().ClearForInventoryReply();
}

Core::Expected<InventoryReplyResult, std::string> ApplyInventory(const Protocol::Character::InventoryList& list,
                                                                 const WorldEntryResources& resources)
{
    ClearForInventoryReply(resources);
    // 0x4c5e4f..0x4c5e61: 0x443d40 apaga las entradas vivas de la tabla de 0x7d335d0 cuyo dueño (+0x108) es
    // el héroe publicado, y 0x421d60 suelta su cuervo (+0x430). MuRet no tiene ni una ni otro en el mundo,
    // como en 0x510f70 (Scenes::LoadMap): no hay nada que soltar.
    // 0x4c5e6c..0x4c5e9f: la cantidad de p+5, releída después de cada 0x5bc740, que no escribe el paquete.
    InventoryReplyResult result;
    for (std::size_t index = 0; index < list.items.size(); ++index)
    {
        const Protocol::Character::InventoryEntry& entry = list.items[index];
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
} // namespace Client::Scenes
