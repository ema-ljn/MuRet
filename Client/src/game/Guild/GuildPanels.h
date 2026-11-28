#pragma once

#include "Core/Expected.h"
#include "Core/PixelSize.h"
#include "Protocol/Outgoing.h"
#include "Ui/PanelDialog.h"
#include "Ui/PanelRegistry.h"

#include <cstdint>
#include <functional>
#include <string>

namespace Client::Render::Text
{
class ITextMeasure;
}

namespace Client::Kernel
{
class WorldState;
struct WorldRuntime;
} // namespace Client::Kernel

// The two guild windows and the panels' dialog in the registry (CL9 milestone 8; 06 § El gremio): what they do when
// closed (0x6522f0 and 0x6570b0) and the two functions that answer the dialog (0x4d6b60 and 0x653c00). What they send
// goes through `send` (the session's GameServer, in the clear).
namespace Client::Guild
{

// The guild packets' send (the session, or whatever the UI asks of it).
using GuildSender = std::function<void(Protocol::Outgoing)>;

// 0x6600d0 (CUIPopup::SetPopup, from its OutputDebugStringA of 0x6d69c4 and 0x6d69f4) from the game: opens the panels'
// dialog and returns what it gives, the GetTickCount() of the opening (+0x10), or 0 without opening it. With more than
// ten rows, 0. With another one open (0x6600f7..0x66011d), the notice of 0x6d69a8 to the chat (0x555190("", notice,
// 1)) and 0, without touching the open one. A row of 0x100 bytes or more stops (Q-C1305).
[[nodiscard]] Core::Expected<std::uint32_t, std::string> PopUpThePanelDialog(Kernel::WorldRuntime& runtime,
                                                                             const Ui::PanelDialogRequest& request,
                                                                             const Render::Text::ITextMeasure& measure,
                                                                             Core::PixelSize screen, std::uint32_t now);

// 0x4d6b60 (the 50: with 8 it accepts, C1 06 51 01; with any other reason, 00) and 0x653c00 (the E5: with 8, C1 08 E6
// with 1; with 0x10, with 0; with the others, nothing). Both return 1: the dialog closes. Without `send`, it stops.
[[nodiscard]] Core::Expected<bool, std::string> AnswerThePanelDialog(Kernel::WorldState& world, const GuildSender& send,
                                                                     Ui::PanelDialogOwner owner, std::uint32_t reason);

// 0x6522f0, the vtable + 0x2c of [0x7d536f0]: open, it closes it (+0x7c), goes back to tab 1 (+0x80 = 1, +0x84 = 0),
// plays 0x1c and cancels the dialog if it is its own (0x64fbd0: the open one is +0x428).
[[nodiscard]] Core::Expected<void, std::string> CloseTheGuildWindow(Kernel::WorldState& world, const GuildSender& send);

// 0x64fbd0: if the open panels' dialog (0x660340) is the guild window's (+0x428; both at 0 too), it cancels it
// (0x6603c0, with the answer of whoever opened it).
[[nodiscard]] Core::Expected<void, std::string> CancelTheGuildWindowDialog(Kernel::WorldState& world,
                                                                           const GuildSender& send);

// 0x6570b0, the vtable + 0x2c of [0x7d536f4]: open, it closes it; the hero with guild 1999 goes back to −1; plays
// 0x1c; 0x7d536e4, 0x7d536ec and 0x77f3b68 to 0; cancels the dialog if it is its own (0x654f50: +0x2a0 or +0x2a4) and
// sends C1 04 54 00.
[[nodiscard]] Core::Expected<void, std::string> CloseTheSecondGuildWindow(Kernel::WorldState& world,
                                                                          const GuildSender& send);

// The guild hooks in the registry's context: opening the guild asks for the list, closing both windows and answering
// the dialog. Without `send`, it sets none and whatever uses them stops.
void LendTheGuildTo(Ui::PanelContext& context, Kernel::WorldState& world, const GuildSender& send);

// 0x6605b0 in the UI step: the dialog with the mouse (640 × 480), the old UI's globals and the clock (the step's
// GetTickCount()). With the positions window's hooks (+8), its step (0x64e600, which sends through `send`).
void LendThePanelDialogStepTo(Ui::PanelContext& context, Kernel::WorldState& world, const GuildSender& send,
                              std::int32_t mouseX, std::int32_t mouseY, std::uint32_t now);
} // namespace Client::Guild
