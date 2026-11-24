#include "Network/XorRemapObfuscation.h"

#include "Core/Expected.h"
#include "Network/Crypto/Xor32.h"
#include "Network/ObfuscationError.h"
#include "Network/Packet.h"
#include "Network/PacketWriter.h"
#include "Network/Xor32Packet.h"
#include "Network/XorRemapTables.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace Client::Network
{
namespace
{
// En lo que se cifra, el serial va en el byte 0 y el opcode en el 1. Los campos llevan el XOR32
// del C1 corrido un byte, porque falta el byte del tipo: el primero se encadena con el opcode y
// usa la clave del byte 3 del C1 (Xor32.h, "FoxAnti"). Al deshacerlo, cambiar el opcode y
// rehacerlo, la clave se cancela: cada byte desde el 2 queda con XOR (viejo ^ nuevo). Por eso
// ningún test puede fijar el corrimiento de la clave; se escribe como el original.
constexpr std::size_t SealedOpcodeIndex = 1;
constexpr std::size_t SealedFirstChained = 2;
constexpr std::size_t SealedKeyShift = 1;

// La lista del laboratorio: C2 <tamaño:2> F4 02 <cantidad:1> y entradas de 4 bytes (código u16
// little-endian, carga, y un byte que no se lee). La de main.exe lleva la cantidad en 2 bytes,
// big-endian, así que sus entradas empiezan un byte después y mide uno más.
constexpr std::size_t ListSubIndex = 4;
constexpr std::size_t LabCountIndex = 5;
constexpr std::size_t LabEntriesOffset = 6;
constexpr std::size_t ListCountIndex = 6;
constexpr std::size_t ListEntriesOffset = 7;
constexpr std::size_t ListEntrySize = 4;

// Dónde va la carga en una entrada: después del código.
constexpr std::size_t EntryLoadIndex = 2;

// C1 06 F4 03: el único paquete que encode_outbound reconoce entero (lo compara como un u32). El
// código va en el cuerpo después del opcode y el subcódigo, en little-endian.
constexpr std::size_t ServerSelectSize = 6;
constexpr std::size_t SelectCodeInBody = 2;

// El F3 11 (la lista de skills, 04-protocolo.md): reset_queue le mete un 00 en pkt[5], donde
// main.exe espera un byte que el laboratorio no manda, así que necesita al menos hasta pkt[4].
constexpr std::size_t SubcodeIndex = 3;
constexpr std::size_t InsertedByteIndex = 5;

std::uint8_t U8At(std::span<const std::byte> bytes, std::size_t index)
{
    return std::to_integer<std::uint8_t>(bytes[index]);
}

std::uint8_t Remap(std::span<const OpcodeRemap> remaps, std::uint8_t opcode)
{
    const auto found = std::ranges::find(remaps, opcode, &OpcodeRemap::from);
    return found == remaps.end() ? opcode : found->to;
}

std::uint16_t U16LeAt(std::span<const std::byte> bytes, std::size_t index)
{
    return static_cast<std::uint16_t>(U8At(bytes, index) | (U8At(bytes, index + 1) << 8));
}

void WriteU16Le(std::span<std::byte> out, std::uint16_t value)
{
    out[0] = static_cast<std::byte>(value & 0xFF);
    out[1] = static_cast<std::byte>(value >> 8);
}

// El código de una entrada en la lista de main.exe, o std::nullopt si parse_f4_02 no lo escribe
// (desde el grupo 9, con las tablas del laboratorio). Hasta el grupo 8 el código es a lo sumo 97 y
// el delta, 400: la suma no pasa de 497, y entra en 16 bits.
std::optional<std::uint16_t> ListCode(const XorRemapTables& tables, std::uint16_t labCode)
{
    const std::uint32_t group = (std::uint32_t{labCode} + 1) / tables.codeGroupSize;
    if (group >= tables.listCodeDeltas.size())
    {
        return std::nullopt;
    }
    return static_cast<std::uint16_t>(labCode + tables.listCodeDeltas[group]);
}

// encode_outbound has no 0x154 step of parse_f4_02, so the round trip breaks from code 21 (Q-C17).
std::uint16_t SelectCode(const XorRemapTables& tables, std::uint16_t code)
{
    for (const LadderStep& step : tables.selectLadder)
    {
        if (code >= step.first && code < step.first + tables.ladderStepCodes)
        {
            return static_cast<std::uint16_t>(code - step.subtract);
        }
    }
    return code;
}

bool IsLabServerList(const XorRemapTables& tables, const Packet& packet)
{
    const std::span<const std::byte> bytes = packet.Bytes();
    return packet.Type() == PacketType::C2 && packet.Opcode() == tables.serverOpcode && bytes.size() > ListSubIndex &&
           U8At(bytes, ListSubIndex) == tables.labServerListSub;
}

bool IsSkillList(const XorRemapTables& tables, const Packet& packet)
{
    const std::span<const std::byte> bytes = packet.Bytes();
    return packet.Type() == PacketType::C1 && packet.Opcode() == tables.characterOpcode &&
           bytes.size() > SubcodeIndex && U8At(bytes, SubcodeIndex) == tables.skillListSub;
}

bool IsServerSelect(const XorRemapTables& tables, const Packet& packet)
{
    const std::span<const std::byte> bytes = packet.Bytes();
    return packet.Type() == PacketType::C1 && bytes.size() == ServerSelectSize &&
           packet.Opcode() == tables.serverOpcode && U8At(bytes, SubcodeIndex) == tables.serverSelectSub;
}

// parse_f4_02: la lista entera de nuevo, un byte más larga. Lo que venga después de las entradas
// queda en 0, como el calloc del original. Una cantidad que no entra en el paquete, o una lista
// que traducida pasaría de MaxPacketSize, es InvalidPacket: el original lee y escribe de más. A
// una lista cuya última entrada no trae el cuarto byte el original la acepta, porque ese byte no
// lo lee; MuRet la rechaza igual, porque está mal armada (Q-C09).
Core::Expected<Packet, ObfuscationError> TranslateServerList(const XorRemapTables& tables, const Packet& packet)
{
    const std::span<const std::byte> lab = packet.Bytes();
    if (lab.size() <= LabCountIndex)
    {
        return Core::Unexpected(ObfuscationError::InvalidPacket);
    }
    const std::uint8_t count = U8At(lab, LabCountIndex);
    const std::size_t size = lab.size() + 1;
    if (LabEntriesOffset + std::size_t{count} * ListEntrySize > lab.size() || size > MaxPacketSize)
    {
        return Core::Unexpected(ObfuscationError::InvalidPacket);
    }

    std::vector<std::byte> list(size);
    list[0] = static_cast<std::byte>(PacketType::C2);
    list[1] = static_cast<std::byte>(size >> 8);
    list[2] = static_cast<std::byte>(size & 0xFF);
    list[3] = static_cast<std::byte>(tables.serverOpcode);
    list[ListSubIndex] = static_cast<std::byte>(tables.serverListSub);
    list[ListCountIndex] = static_cast<std::byte>(count);
    for (std::size_t entry = 0; entry < std::size_t{count}; ++entry)
    {
        const std::span<const std::byte> in = lab.subspan(LabEntriesOffset + entry * ListEntrySize, ListEntrySize);
        const std::span<std::byte> out =
            std::span<std::byte>(list).subspan(ListEntriesOffset + entry * ListEntrySize, ListEntrySize);
        if (const std::optional<std::uint16_t> code = ListCode(tables, U16LeAt(in, 0)))
        {
            WriteU16Le(out, *code);
        }
        out[EntryLoadIndex] = in[EntryLoadIndex];
    }
    return Packet::FromBytes(std::move(list), packet.WasDecrypted()).Value();
}

// reset_queue: el mismo paquete con un 00 en pkt[5] y un byte más. Sin pkt[4], o con 255 bytes,
// que pasaría a 256 y en el original queda con tamaño 0, es InvalidPacket.
Core::Expected<Packet, ObfuscationError> InsertSkillListByte(Packet packet)
{
    const bool wasDecrypted = packet.WasDecrypted();
    std::vector<std::byte> bytes = std::move(packet).TakeBytes();
    if (bytes.size() < InsertedByteIndex || bytes.size() >= MaxC1Size)
    {
        return Core::Unexpected(ObfuscationError::InvalidPacket);
    }
    bytes.insert(bytes.begin() + static_cast<std::ptrdiff_t>(InsertedByteIndex), std::byte{0});
    bytes[1] = static_cast<std::byte>(bytes.size());
    return Packet::FromBytes(std::move(bytes), wasDecrypted).Value();
}
} // namespace

XorRemapObfuscation::XorRemapObfuscation(const XorRemapTables& tables) : m_tables(tables) {}

std::byte XorRemapObfuscation::InboundMask(std::byte firstWireByte) const
{
    return PacketTypeFromByte(firstWireByte ^ m_tables.inboundXor) ? m_tables.inboundXor : std::byte{0};
}

Core::Expected<Packet, ObfuscationError> XorRemapObfuscation::AfterCut(Packet packet) const
{
    if (IsLabServerList(m_tables, packet))
    {
        return TranslateServerList(m_tables, packet);
    }
    return packet;
}

Core::Expected<Packet, ObfuscationError> XorRemapObfuscation::BeforeDispatch(Packet packet) const
{
    if (IsSkillList(m_tables, packet))
    {
        return InsertSkillListByte(std::move(packet));
    }
    return packet;
}

// Como send_forward: deshace el XOR32, remapea y lo vuelve a aplicar. Sin remapeo queda igual; con
// uno, cambia también el relleno, que se encadena con el anterior.
void XorRemapObfuscation::BeforeEncrypt(std::span<std::byte> sealed) const
{
    if (sealed.size() <= SealedOpcodeIndex)
    {
        return;
    }
    Crypto::Xor32DecodeBytes(sealed, SealedFirstChained, SealedKeyShift);
    sealed[SealedOpcodeIndex] = static_cast<std::byte>(
        Remap(m_tables.encryptedRemaps, std::to_integer<std::uint8_t>(sealed[SealedOpcodeIndex])));
    Crypto::Xor32EncodeBytes(sealed, SealedFirstChained, SealedKeyShift);
}

// Como encode_outbound: C1 06 F4 03 cambia el código y no toca el XOR32, que no lleva; cualquier
// otro C1 deshace el XOR32, remapea y lo vuelve a aplicar.
Packet XorRemapObfuscation::BeforeSend(Packet packet) const
{
    if (packet.Type() != PacketType::C1)
    {
        return packet;
    }
    if (IsServerSelect(m_tables, packet))
    {
        const std::span<std::byte> code = packet.MutableBody().subspan(SelectCodeInBody);
        WriteU16Le(code, SelectCode(m_tables, U16LeAt(code, 0)));
        return packet;
    }
    packet = Xor32Decode(std::move(packet));
    const std::span<std::byte> body = packet.MutableBody();
    body[0] = static_cast<std::byte>(Remap(m_tables.clearRemaps, std::to_integer<std::uint8_t>(body[0])));
    return Xor32Encode(std::move(packet));
}
} // namespace Client::Network
