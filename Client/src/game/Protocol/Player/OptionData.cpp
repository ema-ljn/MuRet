#include "Protocol/Player/OptionData.h"

#include "Core/Expected.h"
#include "Network/Field.h"
#include "Network/LayoutReader.h"
#include "Network/Packet.h"
#include "Network/PacketLayout.h"
#include "Network/PacketWriter.h"
#include "Network/ProtocolError.h"
#include "Protocol/Opcodes.h"
#include "Protocol/Outgoing.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace Client::Protocol::Player
{
namespace
{
using SkillKeys = Network::Field<std::array<std::byte, SkillKeyCount>, 4>;
using Flags = Network::Field<std::uint8_t, 0xe>;
using QuickSlots = Network::Field<std::array<std::byte, QuickSlotCount>, 0xf>;
using ChatWindow = Network::Field<std::uint8_t, 0x12>;
constexpr Network::PacketLayout OptionDataLayout{
    {Network::PacketType::C1, CharacterOpcode, Character::OptionDataSubcode}, ChatWindow::End, 0x4dd570};

template <std::size_t Size> std::array<std::uint8_t, Size> Unsigned(const std::array<std::byte, Size>& bytes)
{
    std::array<std::uint8_t, Size> values{};
    std::ranges::transform(bytes, values.begin(), [](std::byte value) { return std::to_integer<std::uint8_t>(value); });
    return values;
}
} // namespace

Core::Expected<OptionData, Network::ProtocolError> ReadOptionData(const Network::Packet& packet)
{
    return Network::ReadPacket(packet, OptionDataLayout,
                               [](Network::LayoutReader& reader)
                               {
                                   return OptionData{.skillKeys = Unsigned(reader.Read<SkillKeys>()),
                                                     .flags = reader.Read<Flags>(),
                                                     .quickSlots = Unsigned(reader.Read<QuickSlots>()),
                                                     .chatWindow = reader.Read<ChatWindow>()};
                               });
}

Outgoing WriteOptionData(const OptionBody& body)
{
    return Xor32C1(Network::PacketWriter::C1(CharacterOpcode, Character::OptionDataSubcode)
                       .PutBytes(std::as_bytes(std::span<const std::uint8_t>(body))));
}
} // namespace Client::Protocol::Player
