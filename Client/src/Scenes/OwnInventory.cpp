#include "Scenes/OwnInventory.h"

#include "Scenes/OwnCharacterRecord.h"
#include "World/ItemState.h"

#include <span>

namespace Client::Scenes
{
namespace
{
// El mismo trío que en el equipo del registro (0x4c56ad..0x4c56b7 y 0x4c56ca..0x4c56d4).
void Clear(std::span<World::ItemState> cells)
{
    for (World::ItemState& item : cells)
    {
        item.id = EmptyOwnItemId;
        item.occupied = 0;
        item.flags1b = 0;
    }
}
} // namespace

void OwnInventory::ClearForInventoryReply()
{
    Clear(cells);
    Clear(secondCells);
}
} // namespace Client::Scenes
