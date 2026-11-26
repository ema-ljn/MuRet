#pragma once

#include "Protocol/Outgoing.h"

#include <cstddef>
#include <span>
#include <string_view>

// What the chat entry sends (04, "El chat"): the chat (C1 00) and the whisper (C1 02), as the WndProc builds them on
// Enter (0x4b5b0d and 0x4b70a7) and the macros (0x5a1388), with the XOR32 from byte 3 and in clear by send(), past the
// encrypted queue. The GameServer's PMSG_CHATDATA and PMSG_CHATDATA_WHISPER: the name at +3 and the text at +0xd, up
// to 60 bytes. The player wrote what it carries: it never goes to the log.
namespace Client::Protocol::Chat
{
// The speaker's name (the hero's +0x1d9) or the recipient's (0x7798cf8): ten bytes, with whatever follows the '\0'.
inline constexpr std::size_t ChatNameSize = 10;
// 0x4b7aaf and 0x5a1515: the text with its '\0' if it fits in less than 0x3c bytes, else its first 0x3c, without it.
inline constexpr std::size_t ChatTextLimit = 0x3c;

// The text's bytes the packet carries: `text` up to its '\0', and the '\0', cut at ChatTextLimit.
[[nodiscard]] std::size_t ChatTextBytes(std::string_view text);

// C1 (0xd + n) 00, the name and the text.
[[nodiscard]] Outgoing WriteChat(std::span<const std::byte, ChatNameSize> name, std::string_view text);
// C1 (0xd + n) 02, the recipient and the text.
[[nodiscard]] Outgoing WriteWhisper(std::span<const std::byte, ChatNameSize> target, std::string_view text);
} // namespace Client::Protocol::Chat
