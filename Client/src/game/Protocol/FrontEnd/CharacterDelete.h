#pragma once

#include "Core/Expected.h"
#include "Network/Packet.h"
#include "Network/ProtocolError.h"
#include "Protocol/FrontEnd/CharacterName.h"
#include "Protocol/Outgoing.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

// Deleting a character, F3 02: the request and its reply (04, "La selección de personaje (F3)").
namespace Client::Protocol::FrontEnd
{
// The delete window's field, a personal number of the account in other versions: like the password, never logged.
inline constexpr std::size_t DeleteCodeSize = 10;

// The field as 0x643740 copies it to 0x7798bf8, which 0x6436c0 cleared on opening: the text, then zeros. A longer
// text is cut (the field takes up to 7).
[[nodiscard]] std::array<std::byte, DeleteCodeSize> DeleteCodeFromText(std::string_view text);

// 0x643740: C1 18 F3 02, the chosen character's name up to its end and the field as it is, with the XOR32 and in
// clear.
[[nodiscard]] Outgoing WriteCharacterDelete(std::span<const std::byte, NameSize> name,
                                            std::span<const std::byte, DeleteCodeSize> code);

// pkt[4] of the reply, which 0x4e2b10 reads itself to open a notice: 1 is notice 0x60, which deletes the chosen
// character; 0 is 0x92 (text 1654), 3 is 0x62 (text 439) and any other 0x89 (text 401).
inline constexpr std::uint8_t CharacterDeleted = 1;

[[nodiscard]] Core::Expected<std::uint8_t, Network::ProtocolError>
ReadCharacterDeleteResult(const Network::Packet& packet);
} // namespace Client::Protocol::FrontEnd
