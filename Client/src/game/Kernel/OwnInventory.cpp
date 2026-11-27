#include "Kernel/OwnInventory.h"

#include "Kernel/OwnCharacterRecord.h"
#include "Kernel/ItemState.h"

#include <span>

namespace Client::Kernel
{
namespace
{
// The same three as in the record's equipment (0x4c56ad..0x4c56b7 and 0x4c56ca..0x4c56d4).
void Clear(std::span<Kernel::ItemState> cells)
{
    for (Kernel::ItemState& item : cells)
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
} // namespace Client::Kernel
