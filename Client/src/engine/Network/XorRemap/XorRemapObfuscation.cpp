#include "Network/XorRemap/XorRemapObfuscation.h"

#include "Core/Expected.h"
#include "Crypto/Xor32.h"
#include "Network/ObfuscationError.h"
#include "Network/Packet.h"
#include "Network/PacketWriter.h"
#include "Network/Xor32Packet.h"
#include "Network/XorRemap/XorRemapTables.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace Client::Network::XorRemap
{
namespace
{
// In what is encrypted the serial is byte 0 and the opcode byte 1. The fields carry the C1's XOR32 shifted one byte,
// since the type byte is missing: the first chains with the opcode and uses the C1's byte 3 key (Xor32.h, "FoxAnti").
// Undoing it, changing the opcode and redoing it cancels the key: each byte from 2 on ends up XORed with (old ^ new).
// So no test can pin the key shift; it is written like the original.
constexpr std::size_t SealedOpcodeIndex = 1;
constexpr std::size_t SealedFirstChained = 2;
constexpr std::size_t SealedKeyShift = 1;

// The lab's list: C2 <size:2> F4 02 <count:1> and 4-byte entries (u16 little-endian code, load, and an unread byte).
// main.exe's carries the count in 2 bytes, big-endian, so its entries start one byte later and it is one longer.
constexpr std::size_t ListSubIndex = 4;
constexpr std::size_t LabCountIndex = 5;
constexpr std::size_t LabEntriesOffset = 6;
constexpr std::size_t ListCountIndex = 6;
constexpr std::size_t ListEntriesOffset = 7;
constexpr std::size_t ListEntrySize = 4;

constexpr std::size_t EntryLoadIndex = 2;

// C1 06 F4 03: the only packet encode_outbound recognizes whole (compares it as a u32). The code goes in the body
// after the opcode and the subcode, little-endian.
constexpr std::size_t ServerSelectSize = 6;
constexpr std::size_t SelectCodeInBody = 2;

// F3 11 (the skill list, 04-protocolo.md): reset_queue puts a 00 at pkt[5], where main.exe expects a byte the lab
// does not send, so it needs at least up to pkt[4].
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

// std::nullopt if parse_f4_02 does not write it (from group 9 on, with the lab's tables). Up to group 8 the code is
// at most 97 and the delta 400: the sum stays at 497, within 16 bits.
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
           Subcode{U8At(bytes, ListSubIndex)} == tables.labServerListSub;
}

bool IsSkillList(const XorRemapTables& tables, const Packet& packet)
{
    const std::span<const std::byte> bytes = packet.Bytes();
    return packet.Type() == PacketType::C1 && packet.Opcode() == tables.characterOpcode &&
           bytes.size() > SubcodeIndex && Subcode{U8At(bytes, SubcodeIndex)} == tables.skillListSub;
}

bool IsServerSelect(const XorRemapTables& tables, const Packet& packet)
{
    const std::span<const std::byte> bytes = packet.Bytes();
    return packet.Type() == PacketType::C1 && bytes.size() == ServerSelectSize &&
           packet.Opcode() == tables.serverOpcode && Subcode{U8At(bytes, SubcodeIndex)} == tables.serverSelectSub;
}

// parse_f4_02: the whole list again, one byte longer. What follows the entries stays 0, like the original's calloc. A
// count that does not fit in the packet, or a list that translated would exceed MaxPacketSize, is InvalidPacket: the
// original reads and writes past it. A list whose last entry lacks the fourth byte the original accepts, since it
// does not read that byte; MuRet rejects it as malformed (Q-C09).
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
    list[3] = std::byte{ByteOf(tables.serverOpcode)};
    list[ListSubIndex] = std::byte{ByteOf(tables.serverListSub)};
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

// reset_queue: the same packet with a 00 at pkt[5] and one byte more. Without pkt[4], or with 255 bytes, which would
// become 256 and in the original ends with size 0, it is InvalidPacket.
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

// Like send_forward: undoes the XOR32, remaps and reapplies it. Without a remap it stays the same; with one, the
// padding changes too, since it chains with the previous byte.
void XorRemapObfuscation::BeforeEncrypt(std::span<std::byte> sealed) const
{
    if (sealed.size() <= SealedOpcodeIndex)
    {
        return;
    }
    Client::Crypto::Xor32DecodeBytes(sealed, SealedFirstChained, SealedKeyShift);
    sealed[SealedOpcodeIndex] = static_cast<std::byte>(
        Remap(m_tables.encryptedRemaps, std::to_integer<std::uint8_t>(sealed[SealedOpcodeIndex])));
    Client::Crypto::Xor32EncodeBytes(sealed, SealedFirstChained, SealedKeyShift);
}

// Like encode_outbound: C1 06 F4 03 changes the code and leaves the XOR32, which it does not carry; any other C1
// undoes the XOR32, remaps and reapplies it.
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
} // namespace Client::Network::XorRemap
