#pragma once

#include "Core/Expected.h"
#include "Protocol/Player/OptionData.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

// The game dialogs in the step (06 § HUD y ventanas, "Los diálogos del juego", CL8 milestone 6): what 0x623e70 does
// with the open dialog (0x7d3a10c) after Esc, from 0x623f78. Each id goes through its range of the dispatch table: the
// Esc menu, the options, the boxes with buttons, the generic OK, or its wave's stop with the range's address.
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
struct GameUiContext;
} // namespace Client::Hud

namespace Client::Hud::Dialogs
{

enum class DialogStep : std::uint8_t
{
    // 0x623f90 (0x6e)
    EscapeMenu,
    // 0x62556f (0x93)
    Options,
    // 0x62d756: 0x7d368b8's two buttons (and 0x8c's arrows, and 0x8d's answers).
    Buttons,
    // 0x6263df: Enter or the button (0x11c, 0x62, 0x46, 0x15) close it.
    GenericOk,
    // 0x62a7f7: yes (0xea, 0x62, 0x46 × 0x15) and no (0x14e) or Esc, of the party invitation (0x77, the C1 05 41 in C3)
    // and the trade request (0x78, the C1 04 37 in clear).
    YesNo,
    // 0x6260d1 (CL9 milestone 7): the confirmation box 0x9c, 0x471930 over 0x575d9d0 (0x471e80, which builds it the
    // first time).
    ConfirmBox,
    // 0x6260f6 (CL9 milestone 7): the personal shop's title or price (0x91), with the EDIT of [0x5763504].
    ShopDialog,
    // 0x6282dc..0x62843f and 0x62a779 (CL9 milestones 6 and 7): the two yes/no buttons (and Esc, the no) of 0x94, 0x96,
    // 0x98, 0x99, 0x9a, 0xa3, 0xa8 (with its third), 0xa9 and 0xaa. 0x94, 0x98 and 0x99 leave the answer in 0x6c932e.
    Warning,
    // 0x625910 (0x8e)
    ChaosMixMode,
    // 0x6259c6, 0x625b23, 0x625cf4 and 0x625f98: the jewel mix (0xa1, 0xa2, 0xa4 and 0xa6, ChaosMix/JewelMix.h).
    JewelMenu,
    JewelChoice,
    JewelUnmix,
    JewelResult,
    // 0x62601e: closing the war outcome without consuming the click.
    WarOutcome,
    // A range of another wave: it stops with `address` and `wave`.
    Cut,
};

struct DialogRoute
{
    std::int32_t id = 0;
    DialogStep step = DialogStep::GenericOk;
    // The original's range and the wave that ports it, for the stop.
    std::string_view address{};
    std::string_view wave{};
};

// The dispatch table of 0x623f78..0x6263d9 in the original's order. An id that is not there goes to the generic OK,
// as in the original, which reaches it after all the comparisons.
[[nodiscard]] std::span<const DialogRoute> DialogRoutes();

[[nodiscard]] DialogRoute DialogRouteOf(std::int32_t id);

// 0x623f78: with a dialog open, its range. With none, nothing. `escape` is 0x623e70's [EBP−0x7d]: Esc read
// (0x553520(0x1b)) with the friends window closed, which 0x62c50e's no and 0x628409's yes/no dialogs look at.
[[nodiscard]] Core::Expected<void, std::string> StepTheOpenDialog(const GameUiContext& context, bool escape);

// 0x61c210 up to the packet: the 24 bytes of its stack. It opens and closes the own record (0xf08 rand() of the
// protection table, 0x61c26a..0x61c34f) and, for each slot `at` from 0 to 0x71 with a key (+0x26c + chosen · 0x100 +
// at is not 0xff), writes the skill's byte (+0x6c + at) at [key]. Then, in [10], OR of 1 or 2 (auto attack), of 4 or 8
// (the notices' sound) and of 0x10 without the slides; in [11..13], the low byte of Q, W and E (0x6c8e40..48), and in
// [14] the log rows / 3 (0 with its place at 0) in the high nibble and, in the low one, the transparency · 10 truncated
// (0x69fd3c). Without a chosen one, or with a key of 24 or more (it would overwrite the stack), it stops with an error.
[[nodiscard]] Core::Expected<Protocol::Player::OptionBody, std::string>
OptionBodyOf(Kernel::WorldState& world, Crt::CrtRandom& random, std::optional<std::uint8_t> chosenSlot);
} // namespace Client::Hud::Dialogs
