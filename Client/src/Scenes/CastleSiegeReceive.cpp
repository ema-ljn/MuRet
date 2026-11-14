#include "Scenes/CastleSiegeReceive.h"

#include "Core/Expected.h"
#include "Core/Log.h"
#include "Protocol/CastleSiege.h"
#include "Scenes/ActorState.h"
#include "Scenes/GameMessages.h"
#include "Scenes/GamePanels.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/CastleSiegeState.h"
#include "UI/GameText.h"
#include "UI/MessageArea.h"
#include "UI/PanelRegistry.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace Client::Scenes
{
namespace
{
namespace Castle = Protocol::CastleSiege;

// Los textos de Text.bmd que muestran los casos (0x76bd1dc + id × 300).
constexpr std::size_t Text860 = 860;   // 0x76fc1ac
constexpr std::size_t Text1386 = 1386; // 0x7722a14
constexpr std::size_t Text1609 = 1609; // 0x7732f68
constexpr std::size_t Text1616 = 1616; // 0x773379c

// 0x4df806: la vida del registro que arma el B2 05, 500000 para una puerta y 400000 para lo demás.
constexpr std::uint32_t BoughtGateLife = 500000;
constexpr std::uint32_t BoughtStatueLife = 400000;
constexpr std::uint8_t NoSavedSkill = 0xff;

std::string_view CString(std::string_view text)
{
    const std::size_t end = text.find('\0');
    return end == std::string_view::npos ? text : text.substr(0, end);
}

// 0x555190(nombre, texto, 1). El nombre es un string vacío de la .bss (0x5767ab0..0x5767b48, que solo se apilan).
Core::Expected<void, std::string> ShowMessage(const CastleReceiveContext& context, std::size_t index)
{
    if (context.measure == nullptr)
    {
        return Core::Unexpected(std::string("0x555190: no hay con qué medir el mensaje"));
    }
    const std::optional<std::string_view> text = UI::TextAt(context.texts, index);
    if (!text)
    {
        return Core::Unexpected("el texto " + std::to_string(index) + " de Text.bmd no está");
    }
    return AddGameMessage(context.world.Runtime(), *context.measure, context.screen, std::string_view{}, CString(*text),
                          UI::SystemMessage);
}

UI::CastleSiegeState& CastleOf(const CastleReceiveContext& context)
{
    return context.world.Runtime().castleSiege;
}

// Lo que cae fuera de 0x7d5c504..0x7d5c658: el original escribe igual (Q-C1360); MuRet descarta el paquete entero.
Core::Expected<void, std::string> Discard(const CastleReceiveContext& context, std::string_view what,
                                          std::uint32_t address)
{
    context.log.Warning("se descarta {}: el registro de {:#x} cae fuera de 0x7d5c504..0x7d5c658 (Q-C1360)", what,
                        address);
    return {};
}

// 0x668fd0: dónde copia un registro entero; std::nullopt para un tipo que no es puerta ni estatua (no copia nada).
std::optional<std::uint32_t> RecordTarget(std::uint32_t type, std::uint32_t index)
{
    if (type != UI::GateType && type != UI::StatueType)
    {
        return std::nullopt;
    }
    return UI::NpcRecordAddress(type, index);
}

std::uint32_t RecordU32(const Castle::NpcRecord& record, std::size_t at)
{
    std::uint32_t value = 0;
    for (std::size_t i = 4; i-- > 0;)
    {
        value = (value << 8u) | std::to_integer<std::uint32_t>(record[at + i]);
    }
    return value;
}

// 0x668fd0 sobre varios registros: los mira todos antes de copiar el primero.
Core::Expected<void, std::string> StoreTheNpcRecords(const CastleReceiveContext& context, std::string_view what,
                                                     const std::vector<Castle::NpcRecord>& records)
{
    for (const Castle::NpcRecord& record : records)
    {
        const auto target = RecordTarget(RecordU32(record, UI::RecordType), RecordU32(record, UI::RecordIndex));
        if (target && !UI::CastleData::Holds(*target, UI::NpcRecordBytes))
        {
            return Discard(context, what, *target);
        }
    }
    for (const Castle::NpcRecord& record : records)
    {
        const auto target = RecordTarget(RecordU32(record, UI::RecordType), RecordU32(record, UI::RecordIndex));
        if (!target)
        {
            continue;
        }
        std::array<std::uint8_t, UI::NpcRecordBytes> bytes{};
        std::ranges::transform(record, bytes.begin(), [](std::byte b) { return std::to_integer<std::uint8_t>(b); });
        CastleOf(context).data.SetBytes(*target, bytes);
    }
    return {};
}

void PutU32(Castle::NpcRecord& record, std::size_t at, std::uint32_t value)
{
    for (std::size_t i = 0; i < 4; ++i)
    {
        record[at + i] = static_cast<std::byte>(value >> (8u * i));
    }
}

// B2 00, 0x4e50bd (tabla 0x4e61c4).
Core::Expected<void, std::string> ApplyState(const Castle::CastleState& state, const CastleReceiveContext& context)
{
    switch (state.result)
    {
    case 0:
        return ShowMessage(context, 1500);
    case 1:
    case 2:
    {
        // 0x4e50f2: 0x6599a0(0x80000, 0) sin mirar lo que devuelve, y 0x64b0f0.
        GamePanels panels(context.world, context.random);
        auto opened = panels.Registry().Open(UI::CastleSiegePanel);
        if (!opened.HasValue())
        {
            return Core::Unexpected(std::move(opened).Error());
        }
        StoreTheCastleState(state, context.world.Runtime());
        return {};
    }
    case 3:
        return ShowMessage(context, 1501);
    case 4:
        return ShowMessage(context, 1502);
    default:
        return {};
    }
}

// B2 01, 0x4df080 (tabla 0x4df170): un mensaje por resultado, del 1503 al 1511; con 1, antes, 0x64d290(1).
Core::Expected<void, std::string> ApplyRegister(const Castle::RegisterResult& result,
                                                const CastleReceiveContext& context)
{
    constexpr std::uint8_t LastResult = 8;
    if (result.result > LastResult)
    {
        return {};
    }
    if (result.result == Castle::ResultOk)
    {
        CastleOf(context).registration.registered = 1;
    }
    return ShowMessage(context, 1503 + std::size_t{result.result});
}

// B2 02, 0x4df1a0 (tabla 0x4df6ac).
Core::Expected<void, std::string> ApplyGiveUp(const Castle::GiveUpResult& result, const CastleReceiveContext& context)
{
    switch (result.result)
    {
    case 0:
        return ShowMessage(context, 1512);
    case 1:
        // 0x4df1e7..0x4df660: el C1 04 B2 03 y el C1 03 B4 por el socket del juego, 0x64d290(0) y el mensaje.
        if (!context.send)
        {
            return Core::Unexpected(std::string("0x4df1e7: el B2 02 con 1 manda dos paquetes y no hay GameServer"));
        }
        context.send(Castle::WriteRegistrationRequest());
        context.send(Castle::WriteSiegeGuildListRequest());
        CastleOf(context).registration.registered = 0;
        return ShowMessage(context, 1513);
    case 2:
        return ShowMessage(context, 1514);
    case 3:
        return ShowMessage(context, 1515);
    default:
        return {};
    }
}

// B2 03, 0x4e5186: con 0 o 2, 0x64d290(0); con 1, 0x64d290(pkt[0x11] == 0) y 0x64d2b0 con las marcas.
void ApplyRegistration(const Castle::RegistrationInfo& info, UI::SiegeRegistrationWindow& window)
{
    if (info.result == 0 || info.result == 2)
    {
        window.registered = 0;
    }
    else if (info.result == Castle::ResultOk)
    {
        window.registered = info.gaveUp == 0 ? 1u : 0u;
        window.marks = info.marks;
    }
}

// B2 04, 0x4e51fa (tabla 0x4e61d8).
Core::Expected<void, std::string> ApplyRegisterMark(const Castle::RegisterMarkResult& result,
                                                    const CastleReceiveContext& context)
{
    switch (result.result)
    {
    case 0:
        return ShowMessage(context, 1516);
    case 1:
        // 0x64d2b0, sin mensaje.
        CastleOf(context).registration.marks = result.marks;
        return {};
    case 2:
        return ShowMessage(context, 1517);
    case 3:
        return ShowMessage(context, 1518);
    default:
        return {};
    }
}

// B2 05, 0x4df7d0 (tabla 0x4df890).
Core::Expected<void, std::string> ApplyNpcBuy(const Castle::NpcBuyResult& result, const CastleReceiveContext& context)
{
    switch (result.result)
    {
    case 0:
        return ShowMessage(context, 1519);
    case 1:
    {
        // 0x4df806: el registro en la pila, con la defensa y la recuperación en 0, las dos vidas y +0x1a en 1. +0x18,
        // +0x19 y +0x1b quedan con lo que había en la pila (Q-C1361): MuRet, 0.
        Castle::NpcRecord record{};
        const std::uint32_t life = result.type == UI::GateType ? BoughtGateLife : BoughtStatueLife;
        PutU32(record, UI::RecordType, result.type);
        PutU32(record, UI::RecordIndex, result.index);
        PutU32(record, UI::RecordMaxLife, life);
        PutU32(record, UI::RecordLife, life);
        record[UI::RecordAlive] = std::byte{1};
        return StoreTheNpcRecords(context, "el B2 05", std::vector<Castle::NpcRecord>{record});
    }
    case 2:
        return ShowMessage(context, Text1386);
    case 3:
        return ShowMessage(context, 1520);
    case 4:
        return ShowMessage(context, Text1616);
    default:
        return {};
    }
}

// B2 06, 0x4e52b7 (tabla 0x4e61e8): con 1, 0x669030 y las dos vidas, cruzadas.
Core::Expected<void, std::string> ApplyNpcRepair(const Castle::NpcRepairResult& result,
                                                 const CastleReceiveContext& context)
{
    switch (result.result)
    {
    case 0:
        return ShowMessage(context, 1519);
    case 1:
    {
        const std::uint32_t record = UI::NpcRecordAddress(result.type, result.index);
        if (!UI::CastleData::Holds(record + UI::RecordMaxLife, 8))
        {
            return Discard(context, "el B2 06", record);
        }
        CastleOf(context).data.SetU32(record + UI::RecordLife, result.life);
        CastleOf(context).data.SetU32(record + UI::RecordMaxLife, result.maxLife);
        return {};
    }
    case 2:
        return ShowMessage(context, Text1386);
    case 3:
        return ShowMessage(context, 1520);
    default:
        return {};
    }
}

// B2 07, 0x4df950 (tabla 0x4dfa64): con 1, 0x669030 y lo que sube (1 la defensa, 2 la recuperación, 3 las dos vidas).
Core::Expected<void, std::string> ApplyNpcUpgrade(const Castle::NpcUpgradeResult& result,
                                                  const CastleReceiveContext& context)
{
    constexpr std::uint8_t LastResult = 7;
    if (result.result == 0)
    {
        return ShowMessage(context, 1519);
    }
    if (result.result == 2)
    {
        return ShowMessage(context, Text1386);
    }
    if (result.result >= 3 && result.result <= LastResult)
    {
        return ShowMessage(context, 1520 + std::size_t{result.result} - 3);
    }
    if (result.result != Castle::ResultOk)
    {
        return {};
    }
    const std::uint32_t record = UI::NpcRecordAddress(result.type, result.index);
    std::uint32_t at = 0;
    std::uint32_t size = 4;
    switch (result.kind)
    {
    case 1:
        at = UI::RecordDefense;
        break;
    case 2:
        at = UI::RecordRecovery;
        break;
    case 3:
        at = UI::RecordMaxLife;
        size = 8;
        break;
    default:
        return {};
    }
    if (!UI::CastleData::Holds(record + at, size))
    {
        return Discard(context, "el B2 07", record);
    }
    CastleOf(context).data.SetU32(record + at, result.value);
    if (result.kind == 3)
    {
        CastleOf(context).data.SetU32(record + UI::RecordLife, result.value);
    }
    return {};
}

// B2 08, 0x4e537b: con 1, 0x669070.
Core::Expected<void, std::string> ApplyTaxInfo(const Castle::TaxInfo& info, const CastleReceiveContext& context)
{
    switch (info.result)
    {
    case 0:
        return ShowMessage(context, 1525);
    case 1:
    {
        UI::CastleData& data = CastleOf(context).data;
        data.SetU32(UI::ServerChaosTaxAt, info.chaosTax);
        data.SetU32(UI::ChaosTaxAt, info.chaosTax);
        data.SetU32(UI::ServerStoreTaxAt, info.storeTax);
        data.SetU32(UI::StoreTaxAt, info.storeTax);
        data.SetU64(UI::CastleMoneyAt, info.money);
        return {};
    }
    case 2:
        return ShowMessage(context, Text1386);
    default:
        return {};
    }
}

// B2 09, 0x4e53e7: con 1, el precio de la zona de caza ([0x7d53700] + 0x18) o 0x6690f0.
Core::Expected<void, std::string> ApplyTaxRate(const Castle::TaxRateResult& rate, const CastleReceiveContext& context)
{
    switch (rate.result)
    {
    case 0:
        return ShowMessage(context, 1526);
    case 1:
    {
        UI::CastleSiegeState& castle = CastleOf(context);
        if (rate.kind == 3)
        {
            castle.huntZone.fee = rate.value;
        }
        else if (rate.kind == 1)
        {
            castle.data.SetU32(UI::ServerChaosTaxAt, rate.value);
            castle.data.SetU32(UI::ChaosTaxAt, rate.value);
        }
        else if (rate.kind == 2)
        {
            castle.data.SetU32(UI::ServerStoreTaxAt, rate.value);
            castle.data.SetU32(UI::StoreTaxAt, rate.value);
        }
        return {};
    }
    case 2:
        return ShowMessage(context, Text1386);
    default:
        return {};
    }
}

// B2 10, 0x4e548a: con 1, 0x669160.
Core::Expected<void, std::string> ApplyMoneyOut(const Castle::MoneyOutResult& out, const CastleReceiveContext& context)
{
    switch (out.result)
    {
    case 0:
        return ShowMessage(context, 1527);
    case 1:
        CastleOf(context).data.SetU64(UI::CastleMoneyAt, out.money);
        return {};
    case 2:
        return ShowMessage(context, Text1386);
    default:
        return {};
    }
}

// B2 1A, 0x4e54f6.
void ApplySiegeFlags(const Castle::SiegeFlags& flags, UI::CastleSiegeState& castle)
{
    if (flags.which == 1)
    {
        castle.siegeFlag1 = flags.value;
    }
    else if (flags.which == 2)
    {
        castle.siegeFlag2 = flags.value;
    }
}

// B2 1F, 0x4e5532.
Core::Expected<void, std::string> ApplyHuntZoneGate(const Castle::HuntZoneGateResult& gate,
                                                    const CastleReceiveContext& context)
{
    switch (gate.result)
    {
    case 0:
        return Core::Unexpected(std::string("0x4e5594: el B2 1F con 0 cierra el diálogo de los paneles (0x6603c0) y "
                                            "abre el del texto 860 (0x6600d0): es del hito 8 de la CL9 (P-C90)"));
    case 1:
        CastleOf(context).huntZone.allowed = gate.allowed != 0 ? 1 : 0;
        return {};
    case 2:
        return Core::Unexpected(std::string("0x4e5546: el B2 1F con 2 cierra el diálogo de los paneles (0x6603c0) y "
                                            "abre el del texto 1386 (0x6600d0): es del hito 8 de la CL9 (P-C90)"));
    default:
        return {};
    }
}

// B3, 0x4dfc60.
Core::Expected<void, std::string> ApplyNpcList(const Castle::NpcList& list, const CastleReceiveContext& context)
{
    switch (list.result)
    {
    case 0:
        return ShowMessage(context, Text860);
    case 1:
        return StoreTheNpcRecords(context, "el B3", list.records);
    case 2:
        return ShowMessage(context, Text1386);
    default:
        return {};
    }
}

// B4, 0x4dfce0.
Core::Expected<void, std::string> ApplySiegeGuildList(const Castle::SiegeGuildList& list,
                                                      const CastleReceiveContext& context)
{
    if (list.result == 0)
    {
        return ShowMessage(context, Text860);
    }
    if (list.result == Castle::ResultOk)
    {
        return Core::Unexpected(std::string("0x4dfcfb: vaciar, llenar y ordenar la lista de los gremios del asedio "
                                            "(0x64d2f0, 0x64d2c0, 0x64d300; la lista 0x46f320) es de las ventanas "
                                            "del hito 12 de la CL9 (P-C300)"));
    }
    return {};
}

// B5, 0x4dfd80 (tabla 0x4dfe24).
Core::Expected<void, std::string> ApplyRegisteredGuildList(const Castle::RegisteredGuildList& list,
                                                           const CastleReceiveContext& context)
{
    switch (list.result)
    {
    case 0:
        return ShowMessage(context, Text860);
    case 1:
        return Core::Unexpected(std::string("0x4dfdb5: vaciar y llenar la lista de los gremios registrados "
                                            "(0x64d340, 0x64d310; la lista 0x4706f0) es de las ventanas del hito 12 "
                                            "de la CL9 (P-C300)"));
    case 2:
    case 3:
        return ShowMessage(context, Text1609);
    default:
        return {};
    }
}

// B6, 0x4e0ed0: solo con el panel del asedio de la clase 2 ([0x5763530], el primer método).
Core::Expected<void, std::string> ApplyMinimapPoints(const CastleReceiveContext& context)
{
    constexpr std::uint8_t MinimapPanelKind = 2;
    if (context.world.Runtime().siegePanelKind != MinimapPanelKind)
    {
        return {};
    }
    return Core::Unexpected(std::string("0x4e0ee8: los puntos del panel del asedio de la clase 2 (0x411e80, "
                                        "0x411ed0) son de la batalla, el hito 13 de la CL9 (P-C301)"));
}

// B8 01, 0x4e0700 sobre el héroe ([0x743a170]).
void ApplyKillCount(const Castle::KillCount& count, WorldState& world)
{
    ActorState& hero = world.Actors()[world.Runtime().hero];
    if (count.value != 0)
    {
        hero.byte372 = count.value;
        return;
    }
    if (hero.byte373 != NoSavedSkill)
    {
        hero.skillIndex = hero.byte373;
    }
    hero.byte373 = NoSavedSkill;
    hero.byte372 = 0;
}

// strncpy(destino, origen, n): hasta el primer 0, y el resto en 0.
template <std::size_t Size, std::size_t Out>
void StrNCopy(const std::array<std::byte, Size>& from, std::array<std::uint8_t, Out>& to)
{
    static_assert(Size <= Out);
    bool ended = false;
    for (std::size_t i = 0; i < Size; ++i)
    {
        ended = ended || from[i] == std::byte{0};
        to[i] = ended ? std::uint8_t{0} : std::to_integer<std::uint8_t>(from[i]);
    }
}
} // namespace

void StoreTheCastleState(const Protocol::CastleSiege::CastleState& state, WorldRuntime& runtime)
{
    UI::SiegeRegistrationWindow& window = runtime.castleSiege.registration;
    runtime.panels.delegated.castleSiege = 1;
    window.state = state.state;
    StrNCopy(state.ownerGuild, window.ownerGuild);
    StrNCopy(state.ownerMaster, window.ownerMaster);
    for (std::size_t i = 0; i < window.dates.size(); ++i)
    {
        window.dates[i] = UI::SiegeDate{.year = state.dates[i].year, .rest = state.dates[i].rest};
    }
    window.remaining = state.remaining;
}

Core::Expected<void, std::string> ApplyCastleSiegePacket(const Protocol::CastleSiege::CastleSiegePacket& packet,
                                                         const CastleReceiveContext& context)
{
    return std::visit(
        [&context](const auto& received) -> Core::Expected<void, std::string>
        {
            using Received = std::decay_t<decltype(received)>;
            if constexpr (std::is_same_v<Received, Castle::CastleState>)
                return ApplyState(received, context);
            else if constexpr (std::is_same_v<Received, Castle::RegisterResult>)
                return ApplyRegister(received, context);
            else if constexpr (std::is_same_v<Received, Castle::GiveUpResult>)
                return ApplyGiveUp(received, context);
            else if constexpr (std::is_same_v<Received, Castle::RegistrationInfo>)
            {
                ApplyRegistration(received, CastleOf(context).registration);
                return {};
            }
            else if constexpr (std::is_same_v<Received, Castle::RegisterMarkResult>)
                return ApplyRegisterMark(received, context);
            else if constexpr (std::is_same_v<Received, Castle::NpcBuyResult>)
                return ApplyNpcBuy(received, context);
            else if constexpr (std::is_same_v<Received, Castle::NpcRepairResult>)
                return ApplyNpcRepair(received, context);
            else if constexpr (std::is_same_v<Received, Castle::NpcUpgradeResult>)
                return ApplyNpcUpgrade(received, context);
            else if constexpr (std::is_same_v<Received, Castle::TaxInfo>)
                return ApplyTaxInfo(received, context);
            else if constexpr (std::is_same_v<Received, Castle::TaxRateResult>)
                return ApplyTaxRate(received, context);
            else if constexpr (std::is_same_v<Received, Castle::MoneyOutResult>)
                return ApplyMoneyOut(received, context);
            else if constexpr (std::is_same_v<Received, Castle::SiegeFlags>)
            {
                ApplySiegeFlags(received, CastleOf(context));
                return {};
            }
            else if constexpr (std::is_same_v<Received, Castle::HuntZoneGateResult>)
                return ApplyHuntZoneGate(received, context);
            else if constexpr (std::is_same_v<Received, Castle::NpcList>)
                return ApplyNpcList(received, context);
            else if constexpr (std::is_same_v<Received, Castle::SiegeGuildList>)
                return ApplySiegeGuildList(received, context);
            else if constexpr (std::is_same_v<Received, Castle::RegisteredGuildList>)
                return ApplyRegisteredGuildList(received, context);
            else if constexpr (std::is_same_v<Received, Castle::MinimapPoints>)
                return ApplyMinimapPoints(context);
            else
            {
                static_assert(std::is_same_v<Received, Castle::KillCount>);
                ApplyKillCount(received, context.world);
                return {};
            }
        },
        packet);
}
} // namespace Client::Scenes
