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

// El kind del B2 07 que manda cada respuesta.
constexpr std::uint32_t DefenseKind = 1;
constexpr std::uint32_t RecoveryKind = 2;
constexpr std::uint32_t LifeKind = 3;

// La elegida (un int de la zona) y su registro: base + elegida · 0x1c, en 32 bits. std::nullopt, con un aviso, si el
// registro cae fuera de la zona (el original lee los globales de al lado, Q-C1360).
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

// 0x6623e0 y 0x664370.
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

// 0x662d50, 0x663440, 0x664ce0, 0x6653d0 y 0x665ab0: el valor se calcula antes de armar el paquete.
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

// 0x667a82 → 0x6a1dd4 → _input(0x6a91fc), formato "%u" en 0x6c7b04. El CRT inicial (mbcurmax=1)
// usa SPACE={09..0d,20}, DIGIT={30..39} de la tabla apuntada por 0x6e2100. 0x6a990f acepta un signo;
// 0x6a9aa4/0x6a9aea acumula módulo 2^32 y 0x6a9b1f niega después. Sin dígitos no escribe el destino
// (0x6a9b2a); no sustituir por strtoul, que puede saturar o tener un unsigned long de 64 bits.
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
        // El original manda lo que había en la pila (Q-C1363).
        context.log.Warning("0x667a40 no manda el B2 10: el campo no empieza con un número (Q-C1363)");
        return;
    }
    Send(context, Castle::WriteMoneyOut(*amount));
}
} // namespace Client::CastleSiege
