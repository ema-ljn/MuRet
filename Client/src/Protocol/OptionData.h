#pragma once

#include "Core/Expected.h"
#include "Network/Packet.h"
#include "Protocol/ProtocolError.h"

#include <array>
#include <cstddef>
#include <cstdint>

// 04 § El chat que llega (CL8 hito 3): las opciones guardadas del personaje, que 0x4e32c9 le pasa a 0x4dd570.
// C1 13 F3 30: las diez teclas de skills, las banderas, Q, W y E y el registro del chat. Tipo, opcode o subcódigo
// distintos → WrongPacket; sin los 0x13 bytes → TooShort. Los bytes sobrantes se ignoran.
namespace Client::Protocol::OptionData
{
inline constexpr std::uint8_t OptionDataSubcode = 0x30;
inline constexpr std::size_t SkillKeyCount = 10;
inline constexpr std::size_t QuickSlotCount = 3;

struct OptionData
{
    // pkt[4..0xd]: el número de skill de cada tecla, 0xff sin skill.
    std::array<std::uint8_t, SkillKeyCount> skillKeys{};
    // pkt[0xe]: 0x01 el ataque automático, 0x04 el sonido de los avisos y 0x10 los carteles apagados.
    std::uint8_t flags = 0;
    // pkt[0xf..0x11]: Q, W y E, sin el 0x1c00 que les suma 0x4dd800.
    std::array<std::uint8_t, QuickSlotCount> quickSlots{};
    // pkt[0x12]: el nibble alto por 3 son los renglones del registro; el bajo por 0,1, su transparencia.
    std::uint8_t chatWindow = 0;
};

[[nodiscard]] Core::Expected<OptionData, ProtocolError> ReadOptionData(const Network::Packet& packet);
} // namespace Client::Protocol::OptionData
