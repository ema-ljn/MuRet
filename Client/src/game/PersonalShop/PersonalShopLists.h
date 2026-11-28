#pragma once

#include "Core/Expected.h"
#include "Protocol/PersonalShop/PersonalShop.h"
#include "Social/SocialEvents.h"
#include "Social/SocialState.h"
#include "Ui/PanelRegistry.h"

#include <string>

// The personal shops' item lists (CL9 milestone 7, phase 4; 04 § Las tiendas personales and 06 § Lo social en
// pantalla, "Las tiendas personales"): opening and closing the two windows (0x800, the own one, and 0x20000, the
// other's), the price maps (0x7245e4 and 0x7245e0, from slot to price) and the incoming 3F 01, 05, 06 and 0x13.
// GameServer 0.97 has no 3F: the oracle is the client's ASM (*static*). The window's step (0x5ea290), what the mouse
// does over the grids (0x5c5110, 0x5c5242 and 0x5dec19) and the answers to its warnings are in
// PersonalShop/PersonalShopWindow (phase 5); its draw (0x5f3030), in Hud/GameInventoryHud (phase 6a).
namespace Client::Crt
{
class CrtRandom;
}

namespace Client::Kernel
{
class WorldState;
} // namespace Client::Kernel

namespace Client::Scenes
{
struct WorldEntryResources;
} // namespace Client::Scenes

namespace Client::PersonalShop
{

// 0x65ac65..0x65b21d (0x800) and 0x65b33a..0x65ba37 (0x20000), after opening the inventory: with 0x6c931c other than
// −1, the C1 04 F3 10 in C3 (two rand(), the padding and the serial); the two maps if neither was there (it builds them
// together, and only the return to the selection, ResetThePersonalShops, releases them); 0x6c931c at 1 (the own one)
// or 2 (the other's).
[[nodiscard]] Core::Expected<void, std::string> OpenThePersonalShop(Kernel::WorldState& world, Crt::CrtRandom& random,
                                                                    Ui::PanelId id,
                                                                    const Social::GamePacketSender& send);

// 0x5eb020, on closing either of the two: with 0x6c931c at 2, the own shop's grid comes back from its copy (0x78a6768
// → 0x78ab0b8), the window's title (0x78aba64) goes back to the hero's sign's (0x451810 and 0x451860) or stays empty,
// and with the owner's key (0x78a6514) other than 0, the 3F 07 in C3. Then, with dialog 0x91 or the 0x94 with
// 0x78abb14 at 5, dialog 0 (0x6236f0); 0x6c931c at −1 and the owner's copy (0x78a6320) zeroed.
[[nodiscard]] Core::Expected<void, std::string> CloseThePersonalShop(Kernel::WorldState& world, Crt::CrtRandom& random,
                                                                     const Social::GamePacketSender& send);

// 0x5eb960, through 0x657690's jmp at the end of 0x657670 (the return to the selection, 0x4c26e7): 0x78aba50 at 0,
// 0x6c931c at −1, 0x78aba60 at 0 and the title (0x78aba64) empty; the signs (0x4514a0 on 0x7245c8, which touches
// neither the draw count nor whether they are drawn) and the two price maps (0x4c0340 and the delete of 0x7245e4 and
// 0x7245e0, which stay at 0: the next Open builds them again). No rand(). The owner's copy (0x78a6320), the saved grid
// (0x78a6768) and the windows' corner stay as they were.
void ResetThePersonalShops(Social::PersonalShopState& shops);

// 0x4db120 (3F 01): with a result other than 1 and 0x6c931c at 1, 0x5be150 with an item on the cursor, the slot leaves
// the own shop's map (0x7245e4) and the C1 04 F3 10 in C3. Otherwise, nothing (the log 0x6c84a0 always).
[[nodiscard]] Core::Expected<void, std::string> ApplyPriceResult(const Protocol::PersonalShop::PriceResult& result,
                                                                 const Scenes::WorldEntryResources& resources,
                                                                 const Social::GamePacketSender& send);

// 0x4db890 (3F 05): with 1, Close(0x40) and Close(0x4000000) if open, the owner's copy zeroed, the own grid to its copy
// if 0x20000 was not open, the title (strncpy of 0x24), the shop's grid emptied, Open(0x20000) and the other's map
// emptied; for each entry, with a price of 0 or less, Close(0x800) and Close(0x20000) and it leaves; otherwise,
// 0x5bc740(0x78a9bb0, 8, 8, slot, item, 0) and the price in the map. At the end, the owner's copy if it is in the
// viewport (0x5110d0). With 3, text 1120 with type 2; with another, the log. A placed entry whose footprint overruns
// its grid (Q-C200) drops the whole packet, before closing anything, with a warning in the log.
[[nodiscard]] Core::Expected<void, std::string> ApplyShopItemList(const Protocol::PersonalShop::ShopItemList& list,
                                                                  const Scenes::WorldEntryResources& resources,
                                                                  const Social::GamePacketSender& send);

// 0x4dbcb0 (3F 06): with 1 and 0x6c931c at 2, 0x78a6758's slot leaves the other's map and the grid (0x5bd750); with 1
// and another mode, that map emptied; with 1, the bought item in its slot (0x5bc740 with the last argument at 0). With
// 6, text 1166 and Close(0x800) and Close(0x20000); with 7, the 423; with 8, the 375 (all with type 2); with 7, 8 or
// another, 0x5be150. With 1 and a footprint that overruns its grid (Q-C200), the whole packet is dropped with a
// warning.
[[nodiscard]] Core::Expected<void, std::string> ApplyShopPurchase(const Protocol::PersonalShop::ShopPurchase& purchase,
                                                                  const Scenes::WorldEntryResources& resources,
                                                                  const Social::GamePacketSender& send);

// 0x4dbb40 (3F 0x13): with 1 and 0x6c931c at 2, the shop's grid emptied, the other's map emptied and each entry in its
// slot with its price, without looking at the price. Otherwise, the log. An entry whose footprint overruns its grid
// (Q-C200) drops the whole packet, with a warning in the log.
[[nodiscard]] Core::Expected<void, std::string> ApplyShopRefresh(const Protocol::PersonalShop::ShopRefresh& refresh,
                                                                 const Scenes::WorldEntryResources& resources);
} // namespace Client::PersonalShop
