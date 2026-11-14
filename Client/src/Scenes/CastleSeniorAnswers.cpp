#include "Scenes/CastleSeniorAnswers.h"

#include "Core/Log.h"
#include "Protocol/CastleSiege.h"
#include "Protocol/Outgoing.h"
#include "UI/CastleSiegeState.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>

namespace Client::Scenes
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
    const std::uint32_t record = firstAt + context.data.U32(selectedAt) * UI::NpcRecordBytes;
    if (!UI::CastleData::Holds(record, UI::NpcRecordBytes))
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
    const std::uint32_t type = context.data.U32(*record + UI::RecordType);
    const std::uint32_t index = context.data.U32(*record + UI::RecordIndex);
    if (context.data.bytes.at(*record + UI::RecordAlive - UI::CastleDataBase) == 0)
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

std::int32_t NextValue(const UI::CastleData& data, std::uint32_t record, Upgrade upgrade)
{
    const std::uint32_t type = data.U32(record + UI::RecordType);
    const bool gateOrStatue = type == UI::GateType || type == UI::StatueType;
    switch (upgrade)
    {
    case Upgrade::Life:
    {
        const auto maxLife = static_cast<std::int32_t>(data.U32(record + UI::RecordMaxLife));
        return UI::MaxLifeOfLevel(type, UI::LifeLevelOf(type, maxLife) + 1);
    }
    case Upgrade::Defense:
        return (gateOrStatue ? static_cast<std::int32_t>(data.U32(record + UI::RecordDefense)) : -1) + 1;
    case Upgrade::Recovery:
        if (type == UI::GateType)
        {
            return 1;
        }
        return (type == UI::StatueType ? static_cast<std::int32_t>(data.U32(record + UI::RecordRecovery)) : -1) + 1;
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
    Send(context,
         Castle::WriteNpcUpgrade(context.data.U32(*record + UI::RecordType),
                                 context.data.U32(*record + UI::RecordIndex), kind, static_cast<std::uint32_t>(value)));
}

bool IsCrtSpace(char c)
{
    return c == ' ' || (c >= '\t' && c <= '\r');
}

// sscanf("%u") del CRT de VC6 (*inferido*, sin leer su _input): saltea los espacios, toma un signo, y los dígitos con
// la vuelta de 32 bits; un menos niega. Sin ningún dígito no asigna.
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
    BuyOrRepair(answer, context, UI::SelectedGateAt, UI::FirstGateAt, "0x6623e0");
}

void AnswerTheStatueDialog(std::int32_t answer, const CastleAnswerContext& context)
{
    BuyOrRepair(answer, context, UI::SelectedStatueAt, UI::FirstStatueAt, "0x664370");
}

void AnswerTheGateLifeDialog(std::int32_t answer, const CastleAnswerContext& context)
{
    SendTheUpgrade(answer, context, UI::SelectedGateAt, UI::FirstGateAt, Upgrade::Life, LifeKind, "0x662d50");
}

void AnswerTheStatueLifeDialog(std::int32_t answer, const CastleAnswerContext& context)
{
    SendTheUpgrade(answer, context, UI::SelectedStatueAt, UI::FirstStatueAt, Upgrade::Life, LifeKind, "0x664ce0");
}

void AnswerTheGateDefenseDialog(std::int32_t answer, const CastleAnswerContext& context)
{
    SendTheUpgrade(answer, context, UI::SelectedGateAt, UI::FirstGateAt, Upgrade::Defense, DefenseKind, "0x663440");
}

void AnswerTheStatueDefenseDialog(std::int32_t answer, const CastleAnswerContext& context)
{
    SendTheUpgrade(answer, context, UI::SelectedStatueAt, UI::FirstStatueAt, Upgrade::Defense, DefenseKind, "0x6653d0");
}

void AnswerTheStatueRecoveryDialog(std::int32_t answer, const CastleAnswerContext& context)
{
    SendTheUpgrade(answer, context, UI::SelectedStatueAt, UI::FirstStatueAt, Upgrade::Recovery, RecoveryKind,
                   "0x665ab0");
}

void AnswerTheTaxDialog(std::int32_t answer, const CastleAnswerContext& context)
{
    if (answer != CastleSendAnswer)
    {
        return;
    }
    Send(context, Castle::WriteTaxRate(1, context.data.U32(UI::ChaosTaxAt)));
    Send(context, Castle::WriteTaxRate(2, context.data.U32(UI::StoreTaxAt)));
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
} // namespace Client::Scenes
