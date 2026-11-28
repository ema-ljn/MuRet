#pragma once

#include "Core/Expected.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// MuRet's own emotes, which the original does not have: Data\MuRet\Emotes.txt, written by `mutools emote add`. Each row
// is an action added to Player.bmd after its 205, played and sent the way a case of 0x576eb0 is.
namespace Client::Tables
{
inline constexpr std::string_view AddedEmotesFile = "MuRet\\Emotes.txt";
inline constexpr std::string_view AddedEmotesHeader = "MuRet emotes 1";
// After Player.bmd's 205 actions, and C1 18 carries one byte.
inline constexpr std::uint8_t FirstAddedEmoteAction = 205;
inline constexpr float MostKeysPerStep = 10.0f;

struct AddedEmote
{
    std::uint8_t action = 0;
    std::uint8_t elfAction = 0;
    float keysPerStep = 0.0f;
    bool repeats = true;
    // Byte for byte as the chat has them: CP1252 in the western profile.
    std::vector<std::string> triggers;

    friend bool operator==(const AddedEmote&, const AddedEmote&) = default;
};

// Line 1 is AddedEmotesHeader. Each line after it has tab-separated columns: the action (FirstAddedEmoteAction to
// 255), the elf's action (0 to 255), the keys per step (above 0, at most MostKeysPerStep), `loop` or `once`, and one or
// more triggers, none empty. "\r\n" ends a line as "\n" does, an empty line is skipped and an action comes once.
[[nodiscard]] Core::Expected<std::vector<AddedEmote>, std::string> ReadAddedEmotes(std::span<const std::byte> file);

[[nodiscard]] const AddedEmote* FindAddedEmote(std::span<const AddedEmote> emotes, std::uint8_t action);
} // namespace Client::Tables
