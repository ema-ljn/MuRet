#pragma once

#include "Core/Expected.h"
#include "Protocol/Npc/NpcWindows.h"
#include "Protocol/Outgoing.h"
#include "Items/InventoryMoves.h"
#include "Npc/NpcWindowState.h"
#include "Ui/PanelRegistry.h"

#include <cstdint>
#include <functional>
#include <string>

// The NPC windows on the scene side (CL9 milestone 6): what opening and closing the warehouse, the chaos box and the
// shop does outside the panels (0x6599a0 and 0x657ab0), the incoming packets (the dispatcher 0x4e2b10, 0x4d1e00,
// 0x4c5f60 and 0x4da280) and what the windows send. Contract: Client/docs/research/04-protocolo.md, "Las ventanas de
// los NPC".
namespace Client::Crt
{
class CrtRandom;
}

namespace Client::Kernel
{
class WorldState;
} // namespace Client::Kernel

namespace Client::Hud
{
class GamePanels;
} // namespace Client::Hud

namespace Client::Scenes
{
struct WorldEntryResources;
} // namespace Client::Scenes

namespace Client::Npc
{

// Sending a window packet through the session (WorldEntryResources::sendNpcWindowPacket). Whoever builds it draws the
// C3's rand() first.
using NpcWindowSender = std::function<Core::Expected<void, std::string>(Protocol::Outgoing)>;

// What the wiring of 0x6599a0 and 0x657ab0 reads: the world, the rand() and, for 0x5be150 and 0x555190, the inventory
// services.
struct NpcWindowContext
{
    Kernel::WorldState& world;
    Crt::CrtRandom& random;
    const Items::InventoryServices& services;
    const NpcWindowSender& send;
};

// Ui::PanelContext::npcWindowOpened. 0x40: 0x6c932f and 0x78abb20 at 0 (0x659f8d..0x659f94). 0x100: 0x7d2dc9d at 0,
// the C1 04 BD 09 with the XOR32 in the clear (0x65a94f..0x65ac07) and 0x7d2dc80 at 0 (0x65ac28). 0x8000: 0x449940(1)
// (0x65be8b), the trainer's mode 1 and its box empty.
[[nodiscard]] Core::Expected<void, std::string> NpcWindowOpened(const NpcWindowContext& context, Ui::PanelId id);

// Ui::PanelContext::npcWindowClosing. 0x40 (0x65852b..0x65871d): with the 24's lock (0x78abb39), false; otherwise, the
// grid at 0x78a7070 empty, 0x5be150 with an item on the cursor, the C1 03 82 in the clear and 0x6c932f at 0. 0x100
// (0x658741..0x658961): with something in the box (0x5e3c20) or on the cursor, error text 593 and false; otherwise, the
// C1 03 87 in the clear and the grid at 0x7d2d380 empty. 0x8000 (0x658dda..0x658dfd): with something in the trainer
// box (0x449c10), error text 1211 and false; otherwise, 0x4499b0 (PetTrainer/PetTrainer.h). 0x10000 (0x659063..): the
// C1 03 31 in the clear and the grid at 0x78a7070 empty; the registry already lowered the protected byte.
[[nodiscard]] Core::Expected<bool, std::string> NpcWindowClosing(const NpcWindowContext& context, Ui::PanelId id);

// 0x4d28a9..0x4d28f6: where 0x4d1e00 sends the cursor with SetCursorPos, from the mouse in 640 × 480 (0x7d3682c and
// 0x7d36828) and the screen size (0x6d5d60 and 0x6d5d64): x = (260 · mx / 640, signed) · width / 640 and
// y = height · my / 480, unsigned.
[[nodiscard]] CursorWarp NpcTalkCursorWarp(std::int32_t mouseX, std::int32_t mouseY, std::int32_t screenWidth,
                                           std::int32_t screenHeight);

// The encrypted 30 (0x4d250c..0x4d28f6): 0x657a90 (close everything), what the NPC opens by pkt[3] (the table at
// 0x4d2914), the two sounds (0x19 and 0x1c, CL10) and the cursor request (NpcWindowState::cursorWarpRequested). With 3,
// the six rates of pkt[4..9]; those the packet does not carry stay at 0 (Q-C920).
[[nodiscard]] Core::Expected<void, std::string> ApplyTalkResult(const Protocol::Npc::TalkResult& talk,
                                                                const Scenes::WorldEntryResources& resources);

// The 31 (0x4c5f60): with grid 3, 0x7d2dc80 at 2, sounds 0x57 and 0x44, the chaos box empty and each entry with
// 0x5bc740(0x7d2d380, 8, 4, slot, data, 1); with grid 5, the trainer box; with another, the grid at 0x78a7070 empty
// and each entry with 0x5bc150.
[[nodiscard]] Core::Expected<void, std::string> ApplyItemList(const Protocol::Npc::ItemList& list,
                                                              const Scenes::WorldEntryResources& resources);

// The 32 (0x4e3c2a..0x4e3c58): with a slot, 0x5bc740(0x78a9bb0, 8, 8, slot, data, 0) and sound 0x1d; always,
// 0x5767950 at 0.
[[nodiscard]] Core::Expected<void, std::string> ApplyBuyResult(const Protocol::Npc::BuyResult& result,
                                                               const Scenes::WorldEntryResources& resources);

// The 33 (0x4e3c6e..0x4e3cc6): with 0, 0x5be150; otherwise, the cursor empty and the new zen in +0xecc of the own
// record, open (0x4e79a0 and 0x40f370), and sound 0x1d.
[[nodiscard]] Core::Expected<void, std::string> ApplySellResult(const Protocol::Npc::SellResult& result,
                                                                const Scenes::WorldEntryResources& resources);

// The 34 (0x4e3cd7..0x4e3d24): with zen, in +0xecc of the own record open, 0x550d10 and sound 0x25.
[[nodiscard]] Core::Expected<void, std::string> ApplyRepairResult(const Protocol::Npc::RepairResult& result,
                                                                  const Scenes::WorldEntryResources& resources);

// The 81 (0x4e4692..0x4e46ce): with pkt[3] other than 0, the warehouse zen (+0xed0) and the inventory zen (+0xecc)
// with the own record open.
[[nodiscard]] Core::Expected<void, std::string> ApplyWarehouseMoney(const Protocol::Npc::WarehouseMoney& money,
                                                                    const Scenes::WorldEntryResources& resources);

// The 82 and the 87 (0x4e4819..0x4e485d): the cursor empty and, with dialog 0x73, its close and the chat input.
[[nodiscard]] Core::Expected<void, std::string> ApplyNpcWindowClosed(const Protocol::Npc::NpcWindowClosed&,
                                                                     const Scenes::WorldEntryResources& resources);

// The 83 (0x4e46df..0x4e47f2, the tables at 0x4e60c0 and 0x4e60dc): 0 and 1 the warehouse lock, 0xa, 0xb and 0xd its
// dialogs, 0xc what the password was waiting for.
[[nodiscard]] Core::Expected<void, std::string> ApplyWarehouseState(const Protocol::Npc::WarehouseState& state,
                                                                    const Scenes::WorldEntryResources& resources);

// The 86 (0x4da280, the tables at 0x4da38c and 0x4da3a8): the mix result.
[[nodiscard]] Core::Expected<void, std::string> ApplyChaosMixResult(const Protocol::Npc::ChaosMixResult& result,
                                                                    const Scenes::WorldEntryResources& resources);

// 0x5c4472..0x5c4a89, from 0x5c4110 with the repair mode: the click consumed, the C3's two rand() and the C1 05 34
// with the slot (row · columns + column + 12) and 0x78abb10.
[[nodiscard]] Core::Expected<void, std::string> SendRepair(Crt::CrtRandom& random, const NpcWindowSender& send,
                                                           std::uint8_t position, std::uint8_t requestPosition);
// The wiring of 0x6599a0 and 0x657ab0 from `resources` into `panels`: the world, the cloth's rand(), the inventory
// services and the send.
void LendTheNpcWindows(Hud::GamePanels& panels, const Scenes::WorldEntryResources& resources);
} // namespace Client::Npc
