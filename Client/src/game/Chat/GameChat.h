#pragma once

#include "Core/Expected.h"
#include "Core/PixelSize.h"
#include "Tables/ItemSetTables.h"
#include "Tables/ItemTable.h"
#include "Tables/TextTable.h"
#include "Input/TextInputEvent.h"
#include "Protocol/Outgoing.h"
#include "Chat/ChatInput.h"

#include <array>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>

namespace Client::Assets
{
class FaunaModels;
}

namespace Client::Crt
{
class CrtRandom;
} // namespace Client::Crt

namespace Client::Models
{
class IModelLibrary;
} // namespace Client::Models

namespace Client::Render::Text
{
class ITextMeasure;
}

namespace Client::Platform
{
class IClipboard;
} // namespace Client::Platform

namespace Client::Kernel
{
class WorldState;
} // namespace Client::Kernel

// The chat input in the game (CL8 milestone 3; 06 § El chat): what the WndProc (0x4af4e0) does with the WM_CHAR
// messages it gets or that the EDITs' subclass passes it (0x467b10: Tab and Enter), what the EDITs do with the rest,
// and the macros of 0x59ff10 (Alt and a number). What is sent (the C1 00, the C1 02 and the emote's C1 18, CL9
// milestone 1) goes through `send`; what is notified, through 0x555190 (AddGameMessage). What the player types and
// whom they whisper to does not go to the log. *Static*.
namespace Client::Chat
{

// Who sends the chat packets (App::ServerSession::SendChat). Empty, nothing is sent.
using ChatSender = std::function<void(Protocol::Outgoing)>;

struct GameChatContext
{
    Kernel::WorldState& world;
    // The cloth's rand(): 0x554a40's own record spends it on closing.
    Crt::CrtRandom& random;
    // Text.bmd: the commands, the notices and the filter's "...". Without it, the first one read is the error.
    const Tables::TextTable* texts = nullptr;
    // The names' tail of 0x56c440 (Chat::ChatCommandContext); without them, there are no names.
    const Tables::ItemTable* items = nullptr;
    const Tables::ItemSetOptionTable* setOptions = nullptr;
    // The words of Filter.bmd (0x622b40).
    std::span<const std::string> filter{};
    // Those of FilterName.bmd, which 0x622c70 checks with Filter.bmd in the new guild's name (0x654f80).
    std::span<const std::string> filterName{};
    // GetTextExtentPointA on the texts' DC, which 0x555190 uses when splitting. Without it, a notice is the error.
    const Render::Text::ITextMeasure* measure = nullptr;
    Core::PixelSize screen{640, 480};
    ChatSender send{};
    // The C1 03 31 that /personalshop sends when closing the quests or an event NPC (0x419900, 0x657a90).
    std::function<void()> sendWindowClose{};
    Platform::IClipboard* clipboard = nullptr;
    // Base library: the emote checks `faunaModels` first, where the viewport loads the transformed ones, and then this
    // one. 0x4ea030 looks at the actions of the hero's type; if none offers the model, it stops.
    const Models::IModelLibrary* models = nullptr;
    // The models a creation loads (Assets::AssetsState::fauna); without them, only `models`.
    const Assets::FaunaModels* faunaModels = nullptr;
};

// 0x6a0c01 (atoi) → 0x6a0b76 (the CRT's atol) with the "C" locale: the blanks (the space and 0x09 to 0x0d), a '-' or
// a '+' and the digits, with the count in 32 bits that wraps around silently; with '-', negated. Used by Enter with
// the zen typed (0x4b5574) and by the shop price's OK (0x6261ea).
[[nodiscard]] std::int32_t AtoiLikeTheCrt(std::string_view text);

// The step's WM_CHAR messages, in their order: Tab (TabChar) to the WndProc (0x4b4f12), Enter (EnterChar) to the
// WndProc (PressEnterInTheGame), and the rest to the focused EDIT (Backspace erases; without a focused EDIT, the game
// window drops it). The subclass drops Esc and the bytes 0x19 to 0x1c; TextField, the non-printable ones.
[[nodiscard]] Core::Expected<void, std::string> TypeInTheGameChat(const GameChatContext& context,
                                                                  std::string_view typed);

// Keeps the order of SDL_TEXTINPUT, the editing keys and SDL_TEXTEDITING of the scene's step.
[[nodiscard]] Core::Expected<void, std::string> TypeTheGameInput(const GameChatContext& context,
                                                                 std::span<const Input::TextInputEvent> events);

// 0x4b50b0..0x4b88e9: Enter in the WndProc, with the EDITs' mode (0x6c7b8c at 1). The gates (Chaos Castle outside the
// safe zone, the dialogs 0x7d, 0x95, 0x8a, 0x8d and 0x8b, 0x78abb00 at 3 and the panels' key, 0x657880(0xd)); with the
// field empty it opens it (with the dialog's length at 0x2a and without the zen mode) or closes it, and with the
// dialog 0x73 it empties the EDIT of [0x5763504] and gives the focus to the game; if not, with the zen mode
// (0x77f3b6a) it stores the atoi of what was typed in 0x77f3b6c, empties and hides that EDIT and empties the field,
// and with 0x78aba60 at 1 it stores what was typed as the shop's title if 0x5ebbd0 allows it (if not, the text 1130)
// and empties the field; if not, it reads it to the history, runs the commands (0x56c440), the filter (0x622b40 with
// 1: the text 570) and, with '/' in front, without a name or without the whisper mode (F3), the chat (the emotes with
// their C1 18, the brake, "webzen", /whisper and /move, and the C1 00); if not, the whisper (0x554a40 with level 6,
// the C1 02 and the text in the log). At the end it empties the message field, hides the EDITs, scrolls the log 100
// and closes the field.
[[nodiscard]] Core::Expected<void, std::string> PressEnterInTheGame(const GameChatContext& context);

// 0x4b5734..0x4b6f54 (Enter), 0x5a1214..0x5a1e9e (the macros) and 0x4488d3..0x448ef8 (the /move window, inline): the
// brake (0x5767940 and the last thing said, 0x57676e4), "webzen" in the hero's name, /whisper off and on (alive) or
// /move (dying), and the C1 00 with the hero's name.
[[nodiscard]] Core::Expected<void, std::string> SayInTheChat(const GameChatContext& context, std::string_view said);

// 0x5a119f..0x5a2b47: with Alt (and the brake 0x77f3b74 at 0, outside Chaos Castle, which the caller checks), each
// macro of the pressed key from 1 to 9 and 0, in that order: the commands (0x56c440 from a macro), the emotes, the
// filter and the same as Enter's chat, without the history and always with the C1 00. Each one that gets past the
// commands leaves the brake at Ui::MacroLockSteps.
[[nodiscard]] Core::Expected<void, std::string> SayTheMacros(const GameChatContext& context,
                                                             const std::array<bool, Tables::MacroCount>& pressed);
} // namespace Client::Chat
