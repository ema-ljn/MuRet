#include "Protocol/Connection/MainCheck.h"

#include "Core/ByteReader.h"
#include "Core/Expected.h"
#include "Network/Packet.h"
#include "Network/PacketWriter.h"
#include "Protocol/Connection/Opcodes.h"
#include "Protocol/Outgoing.h"
#include "Protocol/ProtocolError.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace Client::Protocol::Connection
{
namespace
{
// 0x4ad3d6: la clave del GS va cifrada con este XOR (EncryptCheckSumKey, 0x415546 del GS).
constexpr std::uint32_t MainCheckKeyMask = 0xB479;
// El relleno de pkt[3] que alinea la clave a dos bytes en el struct del GS.
constexpr std::size_t MainCheckKeyOffset = 4;
// 0x4e1762..0x4e178a: el byte que alinea la suma a cuatro, en 0.
constexpr std::uint8_t MainCheckReplyPadding = 0;

std::uint32_t LoadU32Le(std::span<const std::byte> image, std::size_t offset)
{
    return static_cast<std::uint32_t>(image[offset]) | (static_cast<std::uint32_t>(image[offset + 1]) << 8U) |
           (static_cast<std::uint32_t>(image[offset + 2]) << 16U) |
           (static_cast<std::uint32_t>(image[offset + 3]) << 24U);
}
} // namespace

Core::Expected<std::uint16_t, ProtocolError> ReadMainCheckKey(const Network::Packet& packet)
{
    if (packet.Type() != Network::PacketType::C1 || packet.Opcode() != MainCheckOpcode)
        return Core::Unexpected(ProtocolError::WrongPacket);
    const std::span<const std::byte> bytes = packet.Bytes();
    if (bytes.size() < MainCheckKeyOffset + sizeof(std::uint16_t))
        return Core::Unexpected(ProtocolError::TooShort);
    Core::ByteReader reader(bytes.subspan(MainCheckKeyOffset));
    const std::optional<std::uint16_t> key = reader.ReadU16Le();
    if (!key)
        return Core::Unexpected(ProtocolError::TooShort);
    return *key;
}

std::uint16_t MainCheckTable(std::uint16_t key)
{
    // 0x4ad3d3..0x4ad3ef: c = (k ^ 0xB479) & 0xFFFF; ((c >> 6) ^ c) & 0xF ^ (c >> 6).
    const std::uint32_t clear = (static_cast<std::uint32_t>(key) ^ MainCheckKeyMask) & 0xFFFFU;
    const std::uint32_t high = clear >> 6U;
    return static_cast<std::uint16_t>((((high ^ clear) & 0xFU) ^ high) & 0xFFFFU);
}

Core::Expected<std::uint32_t, std::string> MainCheckSum(std::span<const std::byte> image, std::uint16_t table)
{
    constexpr std::size_t Word = 4;
    if (image.size() < Word)
    {
        return Core::Unexpected(std::string("0x4ad418: con menos de cuatro bytes, tamaño − 4 da la vuelta y "
                                            "0x4ad400 lee después del buffer"));
    }
    const std::uint32_t key = table;
    // 0x4ad40b..0x4ad415: la suma empieza en la tabla por 512.
    std::uint32_t sum = key << 9U;
    const std::size_t last = image.size() - Word;
    for (std::size_t offset = 0; offset <= last; offset += Word)
    {
        const auto quarter = static_cast<std::uint32_t>(offset >> 2U);
        const std::uint32_t value = LoadU32Le(image, offset);
        // 0x4ad42d..0x4ad434: div sin signo por 3 de la tabla más el índice del u32.
        switch ((key + quarter) % 3U)
        {
        case 0:
            sum ^= value;
            break;
        case 1:
            sum += value;
            break;
        default:
            // 0x4ad444..0x4ad46a: shl por el u32 % 11 (de 0 a 10) y el xor del mismo u32.
            sum <<= value % 11U;
            sum ^= value;
            break;
        }
        // 0x4ad46c..0x4ad485: el offset siempre es múltiplo de cuatro, así que la mezcla corre en cada
        // vuelta, con shr por (índice & 15) + 3.
        sum ^= (sum + key) >> ((quarter & 0xFU) + 3U);
    }
    return sum;
}

std::string MainCheckFileName(std::string_view commandLine)
{
    // 0x4ad270: strchr desde el segundo carácter hasta la última barra invertida; si hay alguna en la
    // línea (aun la primera), salta una posición: en los dos casos, lo que sigue a la última. Después
    // copia y corta en el primer ' ', '"', '/' o '\'.
    const std::size_t last = commandLine.find_last_of('\\');
    std::string name(commandLine.substr(last == std::string_view::npos ? 0 : last + 1));
    const std::size_t end = name.find_first_of(" \"/\\");
    if (end != std::string::npos)
        name.resize(end);
    return name;
}

Outgoing WriteMainCheckReply(std::uint32_t sum)
{
    return EncryptedC1(Network::PacketWriter::C1(MainCheckOpcode).PutU8(MainCheckReplyPadding).PutU32Le(sum));
}
} // namespace Client::Protocol::Connection
