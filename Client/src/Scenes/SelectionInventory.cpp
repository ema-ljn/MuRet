#include "Scenes/SelectionInventory.h"

#include "Render/Models/LogicalAccess.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/OwnInventory.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "World/ItemState.h"

#include <span>

namespace Client::Scenes
{
namespace
{
// 0x5bbf84/0x5bbf88 y 0x5bc0a1/0x5bc0a5: sólo tipo y ocupación, sin limpiar flags ni nivel.
void Clear(std::span<World::ItemState> items)
{
    for (World::ItemState& item : items)
    {
        item.id = EmptyOwnItemId;
        item.occupied = 0;
    }
}
} // namespace

void ClearSelectionInventory(WorldState& world, Render::Models::CrtRandom& random)
{
    {
        // El cierre de 0x5bc092 precede al acceso independiente de 0x633f96.
        const Render::Models::LogicalAccess access(world.Runtime().ownRecordDepth, random,
                                                   Render::Models::OwnRecordCloseRandoms);
        Clear(world.OwnCharacter().equipment);
    }
    Clear(world.Inventory().cells);
    // Las otras seis grillas de 0x5bbe10 no están representadas todavía en main (H6/H7).
    // 0x78ab0b8 sí existe, pero no es una de las que vacía esta función del original.
}
} // namespace Client::Scenes
