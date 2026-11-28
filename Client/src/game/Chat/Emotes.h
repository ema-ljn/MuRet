#pragma once

#include "Tables/TextTable.h"
#include "Actors/Emote.h"

#include <cstddef>
#include <optional>
#include <string_view>

// The search for emotes in what was said (CL9 milestone 1; 06 § El chat, "El emote"): 0x576eb0 up to the jump to its
// case. What each case does is Actors::DoTheEmote. *Static*.
namespace Client::Chat
{
// 0x576eb0: the Text.bmd texts it searches, from 270 to 348 and the 783.
inline constexpr std::size_t FirstEmoteText = 270;
inline constexpr std::size_t LastEmoteText = 348;
inline constexpr std::size_t LastEmoteExtraText = 783;
// The 95 it searches: the 80 of Text.bmd and the 15 literals of the binary.
inline constexpr std::size_t EmoteTriggerCount = 95;

// 0x576eb0, the search (0x576ede..0x577f88): the 95 in the original's order, each with 0x576960(what was said, text,
// 0); the first one present decides the case. The 15 literals (0x6c90e0..0x6c912c) are compared in CP949 as is. The
// 291, the 317 and the 348 are tried at the end, each alone. A text Text.bmd does not have stays empty, and 0x576960
// finds an empty text in any message, like the original with its texts zeroed. Without any, empty.
[[nodiscard]] std::optional<Actors::EmoteCase> FindTheEmote(const Tables::TextTable& texts, std::string_view said);
} // namespace Client::Chat
