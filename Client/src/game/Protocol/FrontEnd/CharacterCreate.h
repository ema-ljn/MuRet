#pragma once

#include "Core/Expected.h"
#include "Network/Packet.h"
#include "Network/ProtocolError.h"
#include "Protocol/FrontEnd/CharacterName.h"
#include "Protocol/Outgoing.h"

#include <cstddef>
#include <cstdint>
#include <span>

// Creating a character, F3 01: the request and its reply (04, "La selección de personaje (F3)").
namespace Client::Protocol::FrontEnd
{
// 0x63d990: C1 0F F3 01, the name up to its end and the preview's class times 16 plus its skin (+0x1d5, which nobody
// writes: 0), with the XOR32 and in clear.
[[nodiscard]] Outgoing WriteCharacterCreate(std::span<const std::byte, NameSize> name, std::uint8_t characterClass);

// pkt[4] of the reply (0x4c2290). 0 opens notice 0x36 (text 1716) and 2 notice 0x37 (text 396), and both reopen the
// create window; any other value does nothing.
inline constexpr std::uint8_t CharacterCreated = 1;
inline constexpr std::uint8_t CreateRefused = 0;
inline constexpr std::uint8_t CreateRefusedOther = 2;

// With CharacterCreated, the new character; with another result, only the result.
struct CharacterCreateResult
{
    std::uint8_t result = 0;
    std::uint8_t slot = 0;
    NameBytes name{};
    std::uint16_t level = 0;
    std::uint8_t classByte = 0; // like an entry's [0xf] in the list: CharacterClassOf
};

// TooShort without pkt[4] or, for a new character, without its class at pkt[0x12]: 0x4c2290 does not check the size.
[[nodiscard]] Core::Expected<CharacterCreateResult, Network::ProtocolError>
ReadCharacterCreateResult(const Network::Packet& packet);
} // namespace Client::Protocol::FrontEnd
