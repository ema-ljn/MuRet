#include "CastleSiege/CastleSeniorAnswers.h"

#include "Core/Log.h"
#include "Protocol/CastleSiege/CastleSiege.h"
#include "Protocol/Outgoing.h"
#include "CastleSiege/CastleSiegeState.h"

#include <cstddef>
#include <bit>
#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>

namespace Client::CastleSiege
{
namespace
{
namespace Castle = Protocol::CastleSiege;

// The B2 07 kind each answer sends.
constexpr std::uint32_t DefenseKind = 1;
constexpr std::uint32_t RecoveryKind = 2;
constexpr std::uint32_t LifeKind = 3;

// The selected one (an int of the zone) and its record: base + selected · 0x1c, in 32 bits. std::nullopt, with a
// warning, if the record falls outside the zone (the original reads the neighbouring globals, Q-C1360).
std::optional<std::uint32_t> SelectedRecord(const CastleAnswerContext& context, std::uint32_t selectedAt,
                                            std::uint32_t firstAt, std::string_view what)
{
    const std::uint32_t record = firstAt + context.data.U32(selectedAt) * CastleSiege::NpcRecordBytes;
    if (!CastleSiege::CastleData::Holds(record, CastleSiege::NpcRecordBytes))
    {
        context.log.Warning("{} no manda nada: el registro elegido, {:#x}, cae fuera de 0x7d5c504..0x7d5c658 "
                            "(Q-C1360)",
                            what, record);
        return std::nullopt;
    }
    return record;
}

void Send(const CastleAnswerContext& context, Protocol::Outgoing packet)
{
    if (context.send)
    {
        context.send(std::move(packet));
    }
}

// 0x6623e0 and 0x664370.
void BuyOrRepair(std::int32_t answer, const CastleAnswerContext& context, std::uint32_t selectedAt,
                 std::uint32_t firstAt, std::string_view what)
{
    if (answer != CastleSendAnswer)
    {
        return;
    }
    const auto record = SelectedRecord(context, selectedAt, firstAt, what);
    if (!record)
    {
        return;
    }
    const std::uint32_t type = context.data.U32(*record + CastleSiege::RecordType);
    const std::uint32_t index = context.data.U32(*record + CastleSiege::RecordIndex);
    if (context.data.bytes.at(*record + CastleSiege::RecordAlive - CastleSiege::CastleDataBase) == 0)
    {
        Send(context, Castle::WriteNpcBuy(type, index));
    }
    else
    {
        Send(context, Castle::WriteNpcRepair(type, index));
    }
}

enum class Upgrade : std::uint8_t
{
    Life,     // 0x660d20(tipo, 0x660da0 + 1)
    Defense,  // 0x660f30 + 1
    Recovery, // 0x661020 + 1
};

std::int32_t NextValue(const CastleSiege::CastleData& data, std::uint32_t record, Upgrade upgrade)
{
    const std::uint32_t type = data.U32(record + CastleSiege::RecordType);
    const bool gateOrStatue = type == CastleSiege::GateType || type == CastleSiege::StatueType;
    switch (upgrade)
    {
    case Upgrade::Life:
    {
        const auto maxLife = static_cast<std::int32_t>(data.U32(record + CastleSiege::RecordMaxLife));
        return CastleSiege::MaxLifeOfLevel(type, CastleSiege::LifeLevelOf(type, maxLife) + 1);
    }
    case Upgrade::Defense:
        return std::bit_cast<std::int32_t>(
            (gateOrStatue ? data.U32(record + CastleSiege::RecordDefense) : 0xffffffffu) + 1u);
    case Upgrade::Recovery:
        if (type == CastleSiege::GateType)
        {
            return 1;
        }
        return std::bit_cast<std::int32_t>(
            (type == CastleSiege::StatueType ? data.U32(record + CastleSiege::RecordRecovery) : 0xffffffffu) + 1u);
    }
    return 0;
}

// 0x662d50, 0x663440, 0x664ce0, 0x6653d0 and 0x665ab0: the value is computed before building the packet.
void SendTheUpgrade(std::int32_t answer, const CastleAnswerContext& context, std::uint32_t selectedAt,
                    std::uint32_t firstAt, Upgrade upgrade, std::uint32_t kind, std::string_view what)
{
    if (answer != CastleSendAnswer)
    {
        return;
    }
    const auto record = SelectedRecord(context, selectedAt, firstAt, what);
    if (!record)
    {
        return;
    }
    const std::int32_t value = NextValue(context.data, *record, upgrade);
    Send(context, Castle::WriteNpcUpgrade(context.data.U32(*record + CastleSiege::RecordType),
                                          context.data.U32(*record + CastleSiege::RecordIndex), kind,
                                          static_cast<std::uint32_t>(value)));
}

bool IsCrtSpace(char c)
{
    return c == ' ' || (c >= '\t' && c <= '\r');
}

// 0x667a82 → 0x6a1dd4 → _input(0x6a91fc), format "%u" at 0x6c7b04. The initial CRT (mbcurmax=1)
// uses SPACE={09..0d,20}, DIGIT={30..39} from the table 0x6e2100 points to. 0x6a990f accepts a sign;
// 0x6a9aa4/0x6a9aea accumulates modulo 2^32 and 0x6a9b1f negates afterwards. Without digits it does not write the
// destination (0x6a9b2a); do not replace it with strtoul, which can saturate or have a 64-bit unsigned long.
std::optional<std::uint32_t> ScanUnsigned(std::string_view text)
{
    std::size_t at = 0;
    while (at < text.size() && IsCrtSpace(text[at]))
    {
        ++at;
    }
    bool negative = false;
    if (at < text.size() && (text[at] == '-' || text[at] == '+'))
    {
        negative = text[at] == '-';
        ++at;
    }
    std::uint32_t value = 0;
    bool digits = false;
    while (at < text.size() && text[at] >= '0' && text[at] <= '9')
    {
        value = value * 10u + static_cast<std::uint32_t>(text[at] - '0');
        digits = true;
        ++at;
    }
    if (!digits)
    {
        return std::nullopt;
    }
    return negative ? 0u - value : value;
}
} // namespace

void AnswerTheGateDialog(std::int32_t answer, const CastleAnswerContext& context)
{
    BuyOrRepair(answer, context, CastleSiege::SelectedGateAt, CastleSiege::FirstGateAt, "0x6623e0");
}

void AnswerTheStatueDialog(std::int32_t answer, const CastleAnswerContext& context)
{
    BuyOrRepair(answer, context, CastleSiege::SelectedStatueAt, CastleSiege::FirstStatueAt, "0x664370");
}

void AnswerTheGateLifeDialog(std::int32_t answer, const CastleAnswerContext& context)
{
    SendTheUpgrade(answer, context, CastleSiege::SelectedGateAt, CastleSiege::FirstGateAt, Upgrade::Life, LifeKind,
                   "0x662d50");
}

void AnswerTheStatueLifeDialog(std::int32_t answer, const CastleAnswerContext& context)
{
    SendTheUpgrade(answer, context, CastleSiege::SelectedStatueAt, CastleSiege::FirstStatueAt, Upgrade::Life, LifeKind,
                   "0x664ce0");
}

void AnswerTheGateDefenseDialog(std::int32_t answer, const CastleAnswerContext& context)
{
    SendTheUpgrade(answer, context, CastleSiege::SelectedGateAt, CastleSiege::FirstGateAt, Upgrade::Defense,
                   DefenseKind, "0x663440");
}

void AnswerTheStatueDefenseDialog(std::int32_t answer, const CastleAnswerContext& context)
{
    SendTheUpgrade(answer, context, CastleSiege::SelectedStatueAt, CastleSiege::FirstStatueAt, Upgrade::Defense,
                   DefenseKind, "0x6653d0");
}

void AnswerTheStatueRecoveryDialog(std::int32_t answer, const CastleAnswerContext& context)
{
    SendTheUpgrade(answer, context, CastleSiege::SelectedStatueAt, CastleSiege::FirstStatueAt, Upgrade::Recovery,
                   RecoveryKind, "0x665ab0");
}

void AnswerTheTaxDialog(std::int32_t answer, const CastleAnswerContext& context)
{
    if (answer != CastleSendAnswer)
    {
        return;
    }
    Send(context, Castle::WriteTaxRate(1, context.data.U32(CastleSiege::ChaosTaxAt)));
    Send(context, Castle::WriteTaxRate(2, context.data.U32(CastleSiege::StoreTaxAt)));
}

void AnswerTheMoneyDialog(std::int32_t answer, std::string_view field, const CastleAnswerContext& context)
{
    if (answer != CastleMoneyAnswer)
    {
        return;
    }
    const auto amount = ScanUnsigned(field);
    if (!amount)
    {
        // The original sends whatever was on the stack (Q-C1363).
        context.log.Warning("0x667a40 no manda el B2 10: el campo no empieza con un número (Q-C1363)");
        return;
    }
    Send(context, Castle::WriteMoneyOut(*amount));
}
} // namespace Client::CastleSiege
