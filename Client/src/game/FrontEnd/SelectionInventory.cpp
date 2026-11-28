#include "FrontEnd/SelectionInventory.h"

#include "Crt/CrtRandom.h"
#include "Models/LogicalAccess.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Kernel/OwnInventory.h"
#include "Npc/NpcWindowState.h"
#include "ChaosMix/JewelMix.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Kernel/ItemState.h"

#include <span>

namespace Client::FrontEnd
{
namespace
{
// 0x5bbf84/0x5bbf88 and 0x5bc0a1/0x5bc0a5: only type and occupancy, without clearing flags or level.
void Clear(std::span<Kernel::ItemState> items)
{
    for (Kernel::ItemState& item : items)
    {
        item.id = Kernel::EmptyOwnItemId;
        item.occupied = 0;
    }
}
} // namespace

void ClearSelectionInventory(Kernel::WorldState& world, Crt::CrtRandom& random)
{
    {
        // The close of 0x5bc092 comes before the independent access of 0x633f96.
        const Models::LogicalAccess access(world.Runtime().ownRecordDepth, random, Models::OwnRecordCloseRandoms);
        Clear(world.OwnCharacter().equipment);
    }
    Clear(world.Inventory().cells);
    Clear(world.Runtime().npcWindows.grid);
    Clear(world.Runtime().npcWindows.chaosBox);
    Clear(world.Runtime().npcWindows.trainerBox);
    ChaosMix::ResetTheJewelMix(world.Runtime().npcWindows);
    // 0x78ab0b8 does exist, but it is not one of the grids this function of the original empties.
}
} // namespace Client::FrontEnd
