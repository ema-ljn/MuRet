#include "GameLogic/GateCheck.h"

#include "Data/Tables/GateRecord.h"
#include "Data/Tables/GateTable.h"
#include "GameLogic/GateCheckState.h"
#include "GameLogic/GateDecision.h"

#include <cstddef>
#include <cstdint>
#include <optional>

namespace Client::GameLogic
{
namespace
{
// Tipos, gates y textos de 0x59a08b..0x59a46d (06 § Los cambios de mapa).
constexpr std::int16_t Uniria = 0x1a02;
constexpr std::int16_t Dinorant = 0x1a03;
constexpr std::int16_t DarkHorse = 0x1a04;
constexpr std::int16_t Fenrir = 0x1a25;
constexpr std::int16_t FirstWings = 0x1800;
constexpr std::int16_t LastWings = 0x1806;
constexpr std::int16_t Cape = 0x1a1e;
constexpr std::size_t SilentLowLevelGate = 28;
constexpr std::size_t FirstAtlansGate = 45;
constexpr std::size_t LastAtlansGate = 49;
constexpr std::size_t FirstAdditionalAtlansGate = 55;
constexpr std::size_t LastAdditionalAtlansGate = 56;
constexpr std::size_t FirstIcarusGate = 62;
constexpr std::size_t LastIcarusGate = 65;
constexpr std::uint16_t AtlansMountText = 261;
constexpr std::uint16_t IcarusFlightText = 263;
constexpr std::uint16_t RequiredLevelText = 350;
constexpr std::uint16_t IcarusUniriaText = 569;
constexpr std::uint8_t LevelWaitSteps = 50;
constexpr std::uint32_t TeleportCooldownMs = 3000;

std::uint16_t RequiredLevel(const Data::Tables::GateRecord& gate, std::uint8_t characterClass)
{
    constexpr std::uint8_t BaseClassMask = 7;
    constexpr std::uint8_t MagicGladiatorClass = 3;
    constexpr std::uint8_t DarkLordClass = 4;
    const std::uint8_t baseClass = characterClass & BaseClassMask;
    return baseClass == MagicGladiatorClass || baseClass == DarkLordClass
               ? static_cast<std::uint16_t>(gate.level * 2 / 3)
               : gate.level;
}

bool Contains(const Data::Tables::GateRecord& gate, const GateCheckState& state)
{
    return gate.flag == 1 && gate.map == state.map && state.x >= gate.x1 && state.y >= gate.y1 && state.x <= gate.x2 &&
           state.y <= gate.y2;
}

bool IsAtlansMount(std::int16_t mount)
{
    return mount >= Uniria && mount <= Dinorant;
}

bool CanFly(const GateCheckState& state)
{
    return (state.wings >= FirstWings && state.wings <= LastWings) || state.wings == Cape || state.mount == Dinorant ||
           state.mount == DarkHorse || state.mount == Fenrir;
}

// Estos avisos no escriben el contador: 0x59ad7d cierra el registro y sigue con el próximo gate.
bool AppendEquipmentWarnings(GateDecision& decision, std::size_t index, const GateCheckState& state,
                             std::uint16_t requiredLevel)
{
    const bool isAtlans = (index >= FirstAtlansGate && index <= LastAtlansGate) ||
                          (index >= FirstAdditionalAtlansGate && index <= LastAdditionalAtlansGate);
    if (isAtlans && (IsAtlansMount(state.mount) || (state.alternateMountActive && IsAtlansMount(state.alternateMount))))
    {
        decision.warnings.push_back({AtlansMountText, std::nullopt});
        return true;
    }
    if (index < FirstIcarusGate || index > LastIcarusGate)
    {
        return false;
    }
    if (!CanFly(state))
    {
        decision.warnings.push_back({IcarusFlightText, std::nullopt});
        if (state.level < requiredLevel)
        {
            decision.warnings.push_back({RequiredLevelText, requiredLevel});
        }
        return true;
    }
    if (state.mount == Uniria)
    {
        decision.warnings.push_back({IcarusUniriaText, std::nullopt});
        return true;
    }
    return false;
}

// true cuando el contador cambiaría: los demás gates ya no pueden producir efectos ordinarios.
bool CheckEligibleGate(GateDecision& decision, std::size_t index, const Data::Tables::GateRecord& gate,
                       const GateCheckState& state, bool& warpPending)
{
    const std::uint16_t requiredLevel = RequiredLevel(gate, state.characterClass);
    if ((index == SilentLowLevelGate && state.level < requiredLevel) ||
        AppendEquipmentWarnings(decision, index, state, requiredLevel))
    {
        return false;
    }
    if (state.level < requiredLevel)
    {
        decision.warnings.push_back({RequiredLevelText, requiredLevel});
        decision.waitSteps = LevelWaitSteps;
        return true;
    }
    if (!state.requestBlocked && !warpPending && state.elapsedMs >= TeleportCooldownMs)
    {
        decision.request = static_cast<std::uint8_t>(index);
        return true;
    }
    // 0x59abdc/0x59abfa: rechazar el pedido limpia pending incluso antes de otro gate solapado.
    decision.clearWarpPending = true;
    warpPending = false;
    return false;
}
} // namespace

GateDecision CheckGate(std::size_t index, const Data::Tables::GateRecord& gate, const GateCheckState& state)
{
    GateDecision decision;
    if (index >= Data::Tables::GateCount || state.warpCounter != 0 || state.heroGateBlocked || !Contains(gate, state))
    {
        return decision;
    }
    bool warpPending = state.warpPending;
    static_cast<void>(CheckEligibleGate(decision, index, gate, state, warpPending));
    return decision;
}

GateDecision GateCheck(const Data::Tables::GateTable& table, const GateCheckState& state)
{
    GateDecision decision;
    if (state.warpCounter != 0 || state.heroGateBlocked)
    {
        return decision;
    }
    bool warpPending = state.warpPending;
    for (std::size_t index = 0; index < Data::Tables::GateCount && index < table.Count(); ++index)
    {
        const auto gate = Data::Tables::ReadGateRecord(table, index);
        if (gate && Contains(*gate, state) && CheckEligibleGate(decision, index, *gate, state, warpPending))
        {
            break;
        }
    }
    return decision;
}
} // namespace Client::GameLogic
