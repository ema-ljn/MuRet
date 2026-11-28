#pragma once

#include "Core/Expected.h"
#include "Core/PixelSize.h"
#include "Render/Text/IGlyphRasterizer.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

// The bubbles above the heads (CL8 milestone 3; 06 § El chat, "Los globos"): the .bss table at 0x77e1be0, 100 entries
// of 0x294 bytes, filled by the incoming messages (0x557170, for the 00 and the 01, and 0x557560 by name), aged by the
// game step (0x557650, 0x6370f9) and drawn by the HUD (0x5a9050, the 12). Milestone 6's character hover uses the same
// table. The texts are stored up to their '\0', as strcpy and strlen read them.
namespace Client::Tables
{
class TextTable;
}

namespace Client::Kernel
{
class WorldState;
struct ActorState;
} // namespace Client::Kernel

namespace Client::Chat
{

inline constexpr std::size_t ChatBubbleCount = 100;
// 0x557170: with 20 bytes or more, the text goes in two lines (0x5549a0).
inline constexpr std::size_t ChatBubbleSplitBytes = 20;
// 0x557170: how long a mode-0 line lasts (2 · length + 0xa0 steps) and a mode-1 one (1000); the name alone, with the
// text empty, 100 steps in a new entry and 10 in the same character's.
inline constexpr std::int32_t ChatBubbleLogSteps = 1000;
inline constexpr std::int32_t ChatBubbleBaseSteps = 0xa0;
inline constexpr std::int32_t ChatBubbleNewNameSteps = 100;
inline constexpr std::int32_t ChatBubbleNameSteps = 10;

struct ChatBubble
{
    // +0: the name, strcpy of the speaker's.
    std::string name;
    // +0x18 and +0x36: the alliance ("<%s>", with the text of +0x370) and the guild with the post ("[%s] %s"), which
    // 0x556dd0 writes with the guild table (CL9 milestone 8). Without a guild, empty; without an alliance, +0x18 empty.
    std::string guild;
    std::string guildPost;
    // +0x54: the speaker's personal shop short title (0x4518b0, CL9 milestone 7). Without a shop, empty.
    std::string shopTitle;
    // +0x64: the color, the speaker's PK (+0x375) or 0 with +0x88 at 4.
    std::uint8_t color = 0;
    // +0x65: with a guild, the speaker's +0x374; without one, 0.
    std::uint8_t guildByte = 0;
    // +0x68: how many steps the name alone shows.
    std::int32_t nameTime = 0;
    // +0x6c and +0x16c: the lines, the lower and the upper, of 0x100 bytes.
    std::string line1;
    std::string line2;
    // +0x26c and +0x270: how many steps each one shows.
    std::int32_t time1 = 0;
    std::int32_t time2 = 0;
    // +0x274: whose it is, as a pool slot. Empty, the .bss NULL.
    std::optional<std::size_t> owner{};
    // +0x278, +0x27c, +0x280 and +0x284: where it ended up and how big it is in the last draw (0x5a9050, 0x555840).
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t width = 0;
    std::int32_t height = 0;
};

using ChatBubbleTable = std::array<ChatBubble, ChatBubbleCount>;

// 0x557170's mode: 0 the chat (the bubble lasts by the length) and 1 also to the chat log (1000 steps). No MuRet
// caller passes another: the original takes a pointer as the duration (the text or the character).
enum class ChatBubbleMode : std::uint8_t
{
    Chat = 0,
    Log = 1,
};

// What the chat log measures in mode 1 (0x45bb20) and the bubble's guild texts (0x556dd0).
struct ChatBubbleMeasure
{
    const Render::Text::ITextMeasure& measure;
    Core::PixelSize screen{};
    // Text.bmd: the guild and alliance posts. Without it, a character with a guild stops.
    const Tables::TextTable* texts = nullptr;
};

// 0x557170(name, text, character, mode, color): with the character alive and in view (+0x16c), the entry that is
// already its own or the first free one (the name and the two lines without time); without any, nothing. It copies the
// name, the color (without it, the PK, or 0 with +0x88 at 4) and the guild and the shop (0x556dd0). With the text empty
// it only lights the name. In its own entry, the live line moves to the upper one. With 20 bytes or more, the text is
// split in two (0x5549a0): the head above and the tail below, with the same time. Mode 1 also adds it to the log
// (0x45bb20 with type 3) and the sound it asks for stays in WorldRuntime::lastSoundId. The guild comes from
// WorldRuntime::guild (CL9 milestone 8); with a shop sign, the bubble carries its short title (0x4518b0).
[[nodiscard]] Core::Expected<void, std::string>
AddChatBubble(Kernel::WorldState& world, const ChatBubbleMeasure& measure, std::string_view name, std::string_view text,
              std::size_t owner, ChatBubbleMode mode, std::optional<std::uint8_t> color = std::nullopt);

// 0x557560(name, text, mode): the first live character with +0x88 at 1 and that name (strcmp of +0x1d9) and, if
// there is none, the first one with +0x88 at 2; with one, 0x557170 without a color. Without any, nothing.
[[nodiscard]] Core::Expected<void, std::string> AddChatBubbleByName(Kernel::WorldState& world,
                                                                    const ChatBubbleMeasure& measure,
                                                                    std::string_view name, std::string_view text,
                                                                    ChatBubbleMode mode);

// What 0x557650 reads from the step.
struct ChatBubbleStepInput
{
    // 0x7d3682c and 0x7d36828: the mouse in 640 × 480.
    std::int32_t mouseX = 0;
    std::int32_t mouseY = 0;
    Core::PixelSize screen{};
};

// 0x557650, in each game step (0x6370f9, after the helpers and before MoveHero, also while waiting): it lowers each
// entry's three clocks and sets them to 0 if its character died or is not visible; the shops (0x451720, 0x4516d0)
// have none. A right click on someone else's bubble with the chat field open in the safe zone puts its name in the EDIT
// and in the whisper buffer (0x7798cf8) and the 0x19 sounds.
[[nodiscard]] Core::Expected<void, std::string> StepTheChatBubbles(Kernel::WorldState& world,
                                                                   const ChatBubbleStepInput& input);
} // namespace Client::Chat
