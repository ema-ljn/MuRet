#include "Protocol/CastleSiege.h"

#include "Core/Expected.h"
#include "Network/Packet.h"
#include "Network/PacketWriter.h"
#include "Protocol/Outgoing.h"
#include "Protocol/ProtocolError.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace Client::Protocol::CastleSiege
{
namespace
{
// 0x4dfc60, 0x4dfce0 y 0x4dfd80: la cuenta en pkt+8 y los registros desde pkt+0xc; 0x4e0ed0: la cuenta en pkt+4 y los
// pares desde pkt+8.
constexpr std::size_t ListCountAt = 8;
constexpr std::size_t ListRecordsAt = 0xc;
constexpr std::size_t SiegeGuildEntrySize = 0xe;
constexpr std::size_t RegisteredGuildEntrySize = 10;
constexpr std::size_t MinimapCountAt = 4;
constexpr std::size_t MinimapPointsAt = 8;

// Los campos de un paquete a desplazamientos fijos desde su primer byte, como los lee cada handler del original.
class Fields
{
public:
    explicit Fields(std::span<const std::byte> bytes) : m_bytes(bytes) {}

    [[nodiscard]] Core::Expected<std::span<const std::byte>, ProtocolError> Bytes(std::size_t at,
                                                                                  std::size_t count) const
    {
        if (at > m_bytes.size() || m_bytes.size() - at < count)
        {
            return Core::Unexpected(ProtocolError::TooShort);
        }
        return m_bytes.subspan(at, count);
    }

    [[nodiscard]] Core::Expected<std::uint8_t, ProtocolError> U8(std::size_t at) const
    {
        if (at >= m_bytes.size())
        {
            return Core::Unexpected(ProtocolError::TooShort);
        }
        return std::to_integer<std::uint8_t>(m_bytes[at]);
    }

    // MOV r32: el byte de abajo primero.
    [[nodiscard]] Core::Expected<std::uint32_t, ProtocolError> U32Le(std::size_t at) const
    {
        auto field = Bytes(at, 4);
        if (!field.HasValue())
        {
            return Core::Unexpected(field.Error());
        }
        std::uint32_t value = 0;
        for (std::size_t i = 4; i-- > 0;)
        {
            value = (value << 8u) | std::to_integer<std::uint32_t>(field.Value()[i]);
        }
        return value;
    }

    // Los que el original arma byte a byte (SHL 8 y OR): el primero arriba.
    [[nodiscard]] Core::Expected<std::uint64_t, ProtocolError> UBe(std::size_t at, std::size_t count) const
    {
        auto field = Bytes(at, count);
        if (!field.HasValue())
        {
            return Core::Unexpected(field.Error());
        }
        std::uint64_t value = 0;
        for (const std::byte b : field.Value())
        {
            value = (value << 8u) | std::to_integer<std::uint64_t>(b);
        }
        return value;
    }

    [[nodiscard]] Core::Expected<std::uint32_t, ProtocolError> U32Be(std::size_t at) const
    {
        auto value = UBe(at, 4);
        if (!value.HasValue())
        {
            return Core::Unexpected(value.Error());
        }
        return static_cast<std::uint32_t>(value.Value());
    }

    // La cuenta de una lista: un int, y el lazo (JLE antes de entrar, JL al volver) no corre con 0 ni con una
    // negativa.
    [[nodiscard]] Core::Expected<std::size_t, ProtocolError> Count(std::size_t at) const
    {
        auto count = U32Le(at);
        if (!count.HasValue())
        {
            return Core::Unexpected(count.Error());
        }
        const auto signedCount = static_cast<std::int32_t>(count.Value());
        return signedCount > 0 ? static_cast<std::size_t>(signedCount) : std::size_t{0};
    }

private:
    std::span<const std::byte> m_bytes;
};

template <std::size_t Size> std::array<std::byte, Size> CopyOf(std::span<const std::byte> field)
{
    std::array<std::byte, Size> out{};
    std::ranges::copy(field.first(Size), out.begin());
    return out;
}

// Un caso que con pkt[4] en 0 o en otro valor solo muestra un mensaje: el resultado y nada más.
template <typename Result> Core::Expected<CastleSiegePacket, ProtocolError> OnlyTheResult(const Fields& fields)
{
    auto result = fields.U8(4);
    if (!result.HasValue())
    {
        return Core::Unexpected(result.Error());
    }
    return CastleSiegePacket{Result{.result = result.Value()}};
}

// B2 00, 0x4e50bd → 0x64b0f0 con pkt[4] en 1 o 2.
Core::Expected<CastleSiegePacket, ProtocolError> ReadState(const Fields& fields)
{
    auto result = fields.U8(4);
    if (!result.HasValue())
    {
        return Core::Unexpected(result.Error());
    }
    CastleState state{.result = result.Value()};
    if (state.result != 1 && state.result != 2)
    {
        return CastleSiegePacket{state};
    }
    auto whole = fields.Bytes(5, 0x29);
    if (!whole.HasValue())
    {
        return Core::Unexpected(whole.Error());
    }
    state.state = static_cast<std::int8_t>(fields.U8(5).Value());
    for (std::size_t i = 0; i < state.dates.size(); ++i)
    {
        const std::size_t at = 6 + i * 6;
        state.dates[i].year = static_cast<std::uint16_t>(fields.UBe(at, 2).Value());
        for (std::size_t b = 0; b < state.dates[i].rest.size(); ++b)
        {
            state.dates[i].rest[b] = fields.U8(at + 2 + b).Value();
        }
    }
    state.ownerGuild = CopyOf<GuildNameSize>(fields.Bytes(0x18, GuildNameSize).Value());
    state.ownerMaster = CopyOf<CharacterNameSize>(fields.Bytes(0x20, CharacterNameSize).Value());
    state.remaining = fields.U32Be(0x2a).Value();
    return CastleSiegePacket{state};
}

// B2 03, 0x4e5186: con 1, las marcas y pkt[0x11].
Core::Expected<CastleSiegePacket, ProtocolError> ReadRegistration(const Fields& fields)
{
    auto result = fields.U8(4);
    if (!result.HasValue())
    {
        return Core::Unexpected(result.Error());
    }
    RegistrationInfo info{.result = result.Value()};
    if (info.result != ResultOk)
    {
        return CastleSiegePacket{info};
    }
    auto marks = fields.U32Be(0xd);
    auto gaveUp = fields.U8(0x11);
    if (!gaveUp.HasValue())
    {
        return Core::Unexpected(gaveUp.Error());
    }
    info.marks = marks.Value();
    info.gaveUp = gaveUp.Value();
    return CastleSiegePacket{info};
}

// B2 04, 0x4e51fa: con 1, las marcas.
Core::Expected<CastleSiegePacket, ProtocolError> ReadRegisterMark(const Fields& fields)
{
    auto result = fields.U8(4);
    if (!result.HasValue())
    {
        return Core::Unexpected(result.Error());
    }
    RegisterMarkResult mark{.result = result.Value()};
    if (mark.result != ResultOk)
    {
        return CastleSiegePacket{mark};
    }
    auto marks = fields.U32Be(0xd);
    if (!marks.HasValue())
    {
        return Core::Unexpected(marks.Error());
    }
    mark.marks = marks.Value();
    return CastleSiegePacket{mark};
}

// B2 05, 0x4df7d0: con 1, el tipo y el índice.
Core::Expected<CastleSiegePacket, ProtocolError> ReadNpcBuy(const Fields& fields)
{
    auto result = fields.U8(4);
    if (!result.HasValue())
    {
        return Core::Unexpected(result.Error());
    }
    NpcBuyResult buy{.result = result.Value()};
    if (buy.result != ResultOk)
    {
        return CastleSiegePacket{buy};
    }
    auto index = fields.U32Le(0xc);
    if (!index.HasValue())
    {
        return Core::Unexpected(index.Error());
    }
    buy.type = fields.U32Le(8).Value();
    buy.index = index.Value();
    return CastleSiegePacket{buy};
}

// B2 06, 0x4e52b7: con 1, el tipo, el índice y las dos vidas.
Core::Expected<CastleSiegePacket, ProtocolError> ReadNpcRepair(const Fields& fields)
{
    auto result = fields.U8(4);
    if (!result.HasValue())
    {
        return Core::Unexpected(result.Error());
    }
    NpcRepairResult repair{.result = result.Value()};
    if (repair.result != ResultOk)
    {
        return CastleSiegePacket{repair};
    }
    auto maxLife = fields.U32Le(0x14);
    if (!maxLife.HasValue())
    {
        return Core::Unexpected(maxLife.Error());
    }
    repair.type = fields.U32Le(8).Value();
    repair.index = fields.U32Le(0xc).Value();
    repair.life = fields.U32Le(0x10).Value();
    repair.maxLife = maxLife.Value();
    return CastleSiegePacket{repair};
}

// B2 07, 0x4df950: con 1, el tipo, el índice y cuál; el valor (pkt+0x14) solo lo leen los casos 1, 2 y 3.
Core::Expected<CastleSiegePacket, ProtocolError> ReadNpcUpgrade(const Fields& fields)
{
    auto result = fields.U8(4);
    if (!result.HasValue())
    {
        return Core::Unexpected(result.Error());
    }
    NpcUpgradeResult upgrade{.result = result.Value()};
    if (upgrade.result != ResultOk)
    {
        return CastleSiegePacket{upgrade};
    }
    auto kind = fields.U32Le(0x10);
    if (!kind.HasValue())
    {
        return Core::Unexpected(kind.Error());
    }
    upgrade.type = fields.U32Le(8).Value();
    upgrade.index = fields.U32Le(0xc).Value();
    upgrade.kind = kind.Value();
    if (upgrade.kind < 1 || upgrade.kind > 3)
    {
        return CastleSiegePacket{upgrade};
    }
    auto value = fields.U32Le(0x14);
    if (!value.HasValue())
    {
        return Core::Unexpected(value.Error());
    }
    upgrade.value = value.Value();
    return CastleSiegePacket{upgrade};
}

// B2 08, 0x4e537b → 0x669070: con 1, los dos impuestos y la plata.
Core::Expected<CastleSiegePacket, ProtocolError> ReadTaxInfo(const Fields& fields)
{
    auto result = fields.U8(4);
    if (!result.HasValue())
    {
        return Core::Unexpected(result.Error());
    }
    TaxInfo info{.result = result.Value()};
    if (info.result != ResultOk)
    {
        return CastleSiegePacket{info};
    }
    auto money = fields.UBe(7, 8);
    if (!money.HasValue())
    {
        return Core::Unexpected(money.Error());
    }
    info.chaosTax = fields.U8(5).Value();
    info.storeTax = fields.U8(6).Value();
    info.money = money.Value();
    return CastleSiegePacket{info};
}

// B2 09, 0x4e53e7: con 1, cuál; el valor lo leen el 3 (0x4e541b) y el 1 y el 2 (0x6690f0).
Core::Expected<CastleSiegePacket, ProtocolError> ReadTaxRate(const Fields& fields)
{
    auto result = fields.U8(4);
    if (!result.HasValue())
    {
        return Core::Unexpected(result.Error());
    }
    TaxRateResult rate{.result = result.Value()};
    if (rate.result != ResultOk)
    {
        return CastleSiegePacket{rate};
    }
    auto kind = fields.U8(5);
    if (!kind.HasValue())
    {
        return Core::Unexpected(kind.Error());
    }
    rate.kind = kind.Value();
    if (rate.kind < 1 || rate.kind > 3)
    {
        return CastleSiegePacket{rate};
    }
    auto value = fields.U32Be(6);
    if (!value.HasValue())
    {
        return Core::Unexpected(value.Error());
    }
    rate.value = value.Value();
    return CastleSiegePacket{rate};
}

// B2 10, 0x4e548a → 0x669160: con 1, la plata.
Core::Expected<CastleSiegePacket, ProtocolError> ReadMoneyOut(const Fields& fields)
{
    auto result = fields.U8(4);
    if (!result.HasValue())
    {
        return Core::Unexpected(result.Error());
    }
    MoneyOutResult out{.result = result.Value()};
    if (out.result != ResultOk)
    {
        return CastleSiegePacket{out};
    }
    auto money = fields.UBe(5, 8);
    if (!money.HasValue())
    {
        return Core::Unexpected(money.Error());
    }
    out.money = money.Value();
    return CastleSiegePacket{out};
}

// B2 1A, 0x4e54f6: pkt[5] solo con pkt[4] en 1 o 2.
Core::Expected<CastleSiegePacket, ProtocolError> ReadSiegeFlags(const Fields& fields)
{
    auto which = fields.U8(4);
    if (!which.HasValue())
    {
        return Core::Unexpected(which.Error());
    }
    SiegeFlags flags{.which = which.Value()};
    if (flags.which != 1 && flags.which != 2)
    {
        return CastleSiegePacket{flags};
    }
    auto value = fields.U8(5);
    if (!value.HasValue())
    {
        return Core::Unexpected(value.Error());
    }
    flags.value = value.Value();
    return CastleSiegePacket{flags};
}

// B2 1F, 0x4e5532: pkt[5] solo con 1.
Core::Expected<CastleSiegePacket, ProtocolError> ReadHuntZoneGate(const Fields& fields)
{
    auto result = fields.U8(4);
    if (!result.HasValue())
    {
        return Core::Unexpected(result.Error());
    }
    HuntZoneGateResult gate{.result = result.Value()};
    if (gate.result != ResultOk)
    {
        return CastleSiegePacket{gate};
    }
    auto allowed = fields.U8(5);
    if (!allowed.HasValue())
    {
        return Core::Unexpected(allowed.Error());
    }
    gate.allowed = allowed.Value();
    return CastleSiegePacket{gate};
}

// B3, 0x4dfc60.
Core::Expected<CastleSiegePacket, ProtocolError> ReadNpcList(const Fields& fields)
{
    auto result = fields.U8(4);
    if (!result.HasValue())
    {
        return Core::Unexpected(result.Error());
    }
    NpcList list{.result = result.Value(), .records = {}};
    if (list.result != ResultOk)
    {
        return CastleSiegePacket{list};
    }
    auto count = fields.Count(ListCountAt);
    if (!count.HasValue())
    {
        return Core::Unexpected(count.Error());
    }
    for (std::size_t i = 0; i < count.Value(); ++i)
    {
        auto record = fields.Bytes(ListRecordsAt + i * NpcRecordSize, NpcRecordSize);
        if (!record.HasValue())
        {
            return Core::Unexpected(record.Error());
        }
        list.records.push_back(CopyOf<NpcRecordSize>(record.Value()));
    }
    return CastleSiegePacket{list};
}

// B4, 0x4dfce0: cada registro como lo pasa a 0x64d2c0.
Core::Expected<CastleSiegePacket, ProtocolError> ReadSiegeGuildList(const Fields& fields)
{
    auto result = fields.U8(4);
    if (!result.HasValue())
    {
        return Core::Unexpected(result.Error());
    }
    SiegeGuildList list{.result = result.Value(), .entries = {}};
    if (list.result != ResultOk)
    {
        return CastleSiegePacket{list};
    }
    auto count = fields.Count(ListCountAt);
    if (!count.HasValue())
    {
        return Core::Unexpected(count.Error());
    }
    for (std::size_t i = 0; i < count.Value(); ++i)
    {
        const std::size_t at = ListRecordsAt + i * SiegeGuildEntrySize;
        auto record = fields.Bytes(at, SiegeGuildEntrySize);
        if (!record.HasValue())
        {
            return Core::Unexpected(record.Error());
        }
        list.entries.push_back(SiegeGuildEntry{.name = CopyOf<GuildNameSize>(record.Value()),
                                               .marks = fields.U32Be(at + 8).Value(),
                                               .first = fields.U8(at + 0xc).Value(),
                                               .second = fields.U8(at + 0xd).Value()});
    }
    return CastleSiegePacket{list};
}

// B5, 0x4dfd80: cada registro como lo pasa a 0x64d310.
Core::Expected<CastleSiegePacket, ProtocolError> ReadRegisteredGuildList(const Fields& fields)
{
    auto result = fields.U8(4);
    if (!result.HasValue())
    {
        return Core::Unexpected(result.Error());
    }
    RegisteredGuildList list{.result = result.Value(), .entries = {}};
    if (list.result != ResultOk)
    {
        return CastleSiegePacket{list};
    }
    auto count = fields.Count(ListCountAt);
    if (!count.HasValue())
    {
        return Core::Unexpected(count.Error());
    }
    for (std::size_t i = 0; i < count.Value(); ++i)
    {
        const std::size_t at = ListRecordsAt + i * RegisteredGuildEntrySize;
        auto record = fields.Bytes(at, RegisteredGuildEntrySize);
        if (!record.HasValue())
        {
            return Core::Unexpected(record.Error());
        }
        list.entries.push_back(RegisteredGuildEntry{.first = fields.U8(at).Value(),
                                                    .second = fields.U8(at + 1).Value(),
                                                    .name = CopyOf<GuildNameSize>(record.Value().subspan(2))});
    }
    return CastleSiegePacket{list};
}

// B6, 0x4e0ed0.
Core::Expected<CastleSiegePacket, ProtocolError> ReadMinimapPoints(const Fields& fields)
{
    auto count = fields.Count(MinimapCountAt);
    if (!count.HasValue())
    {
        return Core::Unexpected(count.Error());
    }
    MinimapPoints points;
    for (std::size_t i = 0; i < count.Value(); ++i)
    {
        auto pair = fields.Bytes(MinimapPointsAt + i * 2, 2);
        if (!pair.HasValue())
        {
            return Core::Unexpected(pair.Error());
        }
        points.points.push_back(MinimapPoint{.x = std::to_integer<std::uint8_t>(pair.Value()[0]),
                                             .y = std::to_integer<std::uint8_t>(pair.Value()[1])});
    }
    return CastleSiegePacket{points};
}

// El B2 por su subcódigo, la tabla 0x4e6140.
Core::Expected<CastleSiegePacket, ProtocolError> ReadCastle(const Fields& fields, std::uint8_t subcode)
{
    switch (subcode)
    {
    case StateSubcode:
        return ReadState(fields);
    case RegisterSubcode:
        return OnlyTheResult<RegisterResult>(fields);
    case GiveUpSubcode:
        return OnlyTheResult<GiveUpResult>(fields);
    case RegistrationSubcode:
        return ReadRegistration(fields);
    case RegisterMarkSubcode:
        return ReadRegisterMark(fields);
    case NpcBuySubcode:
        return ReadNpcBuy(fields);
    case NpcRepairSubcode:
        return ReadNpcRepair(fields);
    case NpcUpgradeSubcode:
        return ReadNpcUpgrade(fields);
    case TaxInfoSubcode:
        return ReadTaxInfo(fields);
    case TaxRateSubcode:
        return ReadTaxRate(fields);
    case MoneyOutSubcode:
        return ReadMoneyOut(fields);
    case SiegeFlagsSubcode:
        return ReadSiegeFlags(fields);
    case HuntZoneGateSubcode:
        return ReadHuntZoneGate(fields);
    default:
        return Core::Unexpected(ProtocolError::WrongPacket);
    }
}

// El writer del original para un C1 con el subcódigo: cifra desde el byte 3, como todo lo que mandan las ventanas.
Network::PacketWriter Castle(std::uint8_t subcode)
{
    return Network::PacketWriter::C1(CastleOpcode).PutU8(subcode);
}
} // namespace

Core::Expected<CastleSiegePacket, ProtocolError> ReadCastleSiegePacket(const Network::Packet& packet)
{
    const auto opcode = packet.Opcode();
    if (!opcode)
    {
        return Core::Unexpected(ProtocolError::WrongPacket);
    }
    const Fields fields(packet.Bytes());
    switch (*opcode)
    {
    case CastleOpcode:
    case KillCountOpcode:
    {
        auto subcode = fields.U8(Network::MinPacketSize(packet.Type()));
        if (!subcode.HasValue())
        {
            return Core::Unexpected(subcode.Error());
        }
        if (*opcode == CastleOpcode)
        {
            return ReadCastle(fields, subcode.Value());
        }
        if (subcode.Value() != KillCountSubcode)
        {
            return Core::Unexpected(ProtocolError::WrongPacket);
        }
        auto value = fields.U8(4);
        if (!value.HasValue())
        {
            return Core::Unexpected(value.Error());
        }
        return CastleSiegePacket{KillCount{.value = value.Value()}};
    }
    case NpcListOpcode:
        return ReadNpcList(fields);
    case SiegeGuildListOpcode:
        return ReadSiegeGuildList(fields);
    case RegisteredGuildListOpcode:
        return ReadRegisteredGuildList(fields);
    case MinimapOpcode:
        return ReadMinimapPoints(fields);
    default:
        return Core::Unexpected(ProtocolError::WrongPacket);
    }
}

Outgoing WriteRegister()
{
    return Xor32C1(Castle(RegisterSubcode));
}

Outgoing WriteGiveUp()
{
    return Xor32C1(Castle(GiveUpSubcode).PutU8(1));
}

Outgoing WriteRegistrationRequest()
{
    return Xor32C1(Castle(RegistrationSubcode));
}

Outgoing WriteRegisterMark(std::uint8_t slot)
{
    return Xor32C1(Castle(RegisterMarkSubcode).PutU8(slot));
}

Outgoing WriteNpcBuy(std::uint32_t type, std::uint32_t index)
{
    return Xor32C1(Castle(NpcBuySubcode).PutU32Le(type).PutU32Le(index));
}

Outgoing WriteNpcRepair(std::uint32_t type, std::uint32_t index)
{
    return Xor32C1(Castle(NpcRepairSubcode).PutU32Le(type).PutU32Le(index));
}

Outgoing WriteNpcUpgrade(std::uint32_t type, std::uint32_t index, std::uint32_t kind, std::uint32_t value)
{
    return Xor32C1(Castle(NpcUpgradeSubcode).PutU32Le(type).PutU32Le(index).PutU32Le(kind).PutU32Le(value));
}

Outgoing WriteTaxInfoRequest()
{
    return Xor32C1(Castle(TaxInfoSubcode));
}

Outgoing WriteTaxRate(std::uint8_t kind, std::uint32_t value)
{
    return Xor32C1(Castle(TaxRateSubcode)
                       .PutU8(kind)
                       .PutU8(static_cast<std::uint8_t>(value >> 24u))
                       .PutU8(static_cast<std::uint8_t>(value >> 16u))
                       .PutU8(static_cast<std::uint8_t>(value >> 8u))
                       .PutU8(static_cast<std::uint8_t>(value)));
}

Outgoing WriteMoneyOut(std::uint32_t amount)
{
    return Xor32C1(Castle(MoneyOutSubcode).PutU32Le(amount));
}

Outgoing WriteHuntZoneGate(std::uint8_t allowed)
{
    return Xor32C1(Castle(HuntZoneGateSubcode).PutU8(allowed));
}

Outgoing WriteNpcListRequest(std::uint8_t kind)
{
    return Xor32C1(Network::PacketWriter::C1(NpcListOpcode).PutU8(kind));
}

Outgoing WriteSiegeGuildListRequest()
{
    return ClearC1(Network::PacketWriter::C1(SiegeGuildListOpcode));
}

Outgoing WriteRegisteredGuildListRequest()
{
    return ClearC1(Network::PacketWriter::C1(RegisteredGuildListOpcode));
}

Outgoing WriteHuntZoneEntry(std::uint32_t fee)
{
    return Xor32C1(Network::PacketWriter::C1(HuntZoneOpcode).PutU8(HuntZoneEntrySubcode).PutU32Le(fee));
}
} // namespace Client::Protocol::CastleSiege
