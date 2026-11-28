#pragma once

#include "Core/Expected.h"
#include "Protocol/Outgoing.h"

#include <functional>
#include <string>

namespace Client::Kernel
{
class WorldState;
} // namespace Client::Kernel

namespace Client::Hud
{
struct GameUiContext;
} // namespace Client::Hud

namespace Client::Npc
{
struct NpcWindowState;
} // namespace Client::Npc

// The jewel mix (CL9 milestone 6, phase 3b; 06 § Los diálogos del juego, "La mezcla de joyas"): the dialogs the 30
// opens with 9 (0x4d25aa) from 0xa1 and what they do in 0x623e70's step, with the functions of 0x402a10..0x402eb0 and
// the BC (Protocol::ChaosMix). None takes rand(): the inventory (0x78a9bb0) is not in the protection table and the BC
// and the 31 go out in the clear. The drawings are in Hud/Dialogs/GameDialogDrawing; 0x6e5660's list of the 0xa4, in
// ChaosMix/JewelBundleList.
namespace Client::ChaosMix
{

// Sends the mix packets (0x402eb0's C1 03 31): GameUiContext::sendNpcWindow in the step and
// WorldEntryResources::sendNpcWindowPacket on receive.
using JewelMixSender = std::function<Core::Expected<void, std::string>(Protocol::Outgoing)>;

// 0x402e10: resets the mix's globals, keeping the bundle list.
void ResetTheJewelMix(Npc::NpcWindowState& windows);

// 0x402e50: back to choosing: 0x6e5798 to 0 if it was 1, the dialog closed (0x6236f0(0)), 0x6e5799 to 0, the jewel
// unchosen (0x6c62e0 at 0xff) and the 0xa2 (mix, 0x6e5794 at 0) or the 0xa4 (unmix, at 1); with another mode,
// everything closed (0x402eb0: 0x402e10, the dialog and the C1 03 31 in the clear through `send`).
[[nodiscard]] Core::Expected<void, std::string> ReturnToTheJewelChoice(Kernel::WorldState& world,
                                                                       const JewelMixSender& send);

// 0x6259c6..0x625b22, the 0xa1: with the click (which it clears), the two buttons at x in [245, 395] and y in
// [190, 225] and [255, 290] close and open the 0xa2 (0x6e5794 at 0, mix) or the 0xa4 (at 1, unmix), with
// MouseUpdateTime at 0 and its maximum at 6, without sound. Then x in [279, 360] and y in [320, 345] closes everything
// (0x402eb0: 0x402e10, the dialog and the C1 03 31 in the clear).
[[nodiscard]] Core::Expected<void, std::string> StepTheJewelMenu(const Hud::GameUiContext& context);

// 0x625b23..0x625cf3, the 0xa2: with the click (which it clears), the two jewels (y in [185, 210], x in [245, 325] and
// [325, 405]) set 0x6c62e0 (0 bless, 1 soul) and, with one chosen, the three bundles (x in [245, 395], y in
// [225, 260], [270, 305] and [315, 350]) set 0x6c62e1 to 10, 20 or 30 and ask with 0x402b70: if enough, they close and
// open the 0xa3 with its text (0x633300(0, 0, 0xa3)), with MouseUpdateTime at 0 and its maximum at 6; if not,
// 0x402b70's warning and 0x402e50's return. Then x in [279, 360] and y in [365, 390] closes everything (0x402eb0).
[[nodiscard]] Core::Expected<void, std::string> StepTheJewelChoice(const Hud::GameUiContext& context);

// 0x625cf4..0x625f96, the 0xa4: with the click (read first, and cleared only on a hit), the two jewels (y in
// [160, 185], x in [235, 315] and [315, 395]) set 0x6c62e0 and fill 0x6e5660's list (0x402a10 and 0x402a20); without
// bundles, error warning 1818 and 0x402e50's return. Without a jewel, x in [279.5, 359.5] and y in [200, 225] with the
// click (not cleared) closes everything (0x402eb0). With one, the list's step (0x402af0, whose left double click on a
// row asks for it with 0x402a90), the OK (x in [234.5, 314.5], y from 10 below the list, 25 high; the click, cleared,
// asks for the chosen one with 0x402a90) and the cancel (x in [324.5, 404.5]; the click, not cleared, 0x402eb0).
[[nodiscard]] Core::Expected<void, std::string> StepTheJewelUnmix(const Hud::GameUiContext& context);

// 0x625f98..0x62601d, the 0xa6: with the click (which it clears), x in [287, 352] and y in [170, 195] goes back with
// 0x402e50.
[[nodiscard]] Core::Expected<void, std::string> StepTheJewelResult(const Hud::GameUiContext& context);

// 0x62846f..0x628500, each chosen button of the 0xa3 (the yes/no step already left the click and the button at 0):
// yes sets 0x6c932e to 1, closes and opens the 0xa5 and sends the BC (0x4029e0); no sets 2 and goes back with
// 0x402e50. Both with MouseUpdateTime at 0, its maximum at 6 and sound 0x19 (CL10).
[[nodiscard]] Core::Expected<void, std::string> AnswerTheJewelMix(const Hud::GameUiContext& context, bool yes);
} // namespace Client::ChaosMix
