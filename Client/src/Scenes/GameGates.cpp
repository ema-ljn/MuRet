#include "Scenes/GameGates.h"

#include "Core/Expected.h"
#include "Data/Tables/GateRecord.h"
#include "Data/Tables/GateTable.h"
#include "GameLogic/GateCheck.h"
#include "GameLogic/GateCheckState.h"
#include "GameLogic/GateDecision.h"
#include "Render/Models/LogicalAccess.h"
#include "Scenes/ActorState.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace Client::Scenes
{
namespace
{
constexpr std::size_t SilentLevelGate = 28;
constexpr std::size_t WingsSlot = 7;
constexpr std::size_t MountSlot = 8;
constexpr std::int32_t CastleSiegeMap = 30;
constexpr std::int32_t LandOfTrialMap = 31;

std::int32_t ReadCell(std::uint8_t value, std::uint8_t& depth, const GameGateContext& context)
{
    const Render::Models::LogicalAccess access(depth, context.random, Render::Models::SmallLogicalCloseRandoms);
    return value;
}

// 0x599f41..0x599ffd: no convertir en una lectura de X/Y por paso; cada comparación cierra la suya.
bool ContainsWithAccesses(const Data::Tables::GateRecord& gate, ActorState& hero, const GameGateContext& context)
{
    return ReadCell(hero.cellX, hero.cellXAccesses, context) >= gate.x1 &&
           ReadCell(hero.cellY, hero.cellYAccesses, context) >= gate.y1 &&
           ReadCell(hero.cellX, hero.cellXAccesses, context) <= gate.x2 &&
           ReadCell(hero.cellY, hero.cellYAccesses, context) <= gate.y2;
}

std::int32_t ReadWait(const GameGateContext& context)
{
    WorldRuntime& runtime = context.world.Runtime();
    const Render::Models::LogicalAccess access(runtime.warpDepth, context.random,
                                               Render::Models::SmallLogicalCloseRandoms);
    return runtime.warpCounter;
}

void WriteWait(std::int32_t value, const GameGateContext& context)
{
    WorldRuntime& runtime = context.world.Runtime();
    const Render::Models::LogicalAccess access(runtime.warpDepth, context.random,
                                               Render::Models::SmallLogicalCloseRandoms);
    runtime.warpCounter = value;
}

GameLogic::GateDecision DecideGate(std::size_t index, const Data::Tables::GateRecord& gate,
                                   const GameGateContext& context)
{
    const WorldRuntime& runtime = context.world.Runtime();
    const ActorState& hero = context.world.Actors()[runtime.hero];
    const OwnCharacterRecord& own = context.world.OwnCharacter();
    const GameLogic::GateCheckState state{.x = hero.cellX,
                                          .y = hero.cellY,
                                          .map = runtime.map,
                                          .level = own.level,
                                          .characterClass = hero.characterClass,
                                          .mount = static_cast<std::int16_t>(own.equipment[MountSlot].id),
                                          .wings = static_cast<std::int16_t>(own.equipment[WingsSlot].id),
                                          .alternateMount = context.alternateMount,
                                          .alternateMountActive = runtime.cursorItemCount > 0,
                                          .warpCounter = runtime.warpCounter,
                                          .elapsedMs = context.ticksMs - runtime.movement.lastTeleportMs,
                                          .heroGateBlocked = hero.byte399 != 0,
                                          .requestBlocked = runtime.movement.gateRequestBlocked != 0,
                                          .warpPending = runtime.movement.warpPending};
    return GameLogic::CheckGate(index, gate, state);
}

// 0x59a094..0x59a16c: el gate28 abre/cierra sólo para probar el nivel, aun si después vuelve a abrir.
bool PassesSilentLevelCheck(std::size_t index, const Data::Tables::GateRecord& gate, const GameGateContext& context)
{
    if (index != SilentLevelGate)
    {
        return true;
    }
    const Render::Models::LogicalAccess access(context.world.Runtime().ownRecordDepth, context.random,
                                               Render::Models::OwnRecordCloseRandoms);
    const GameLogic::GateDecision decision = DecideGate(index, gate, context);
    // En el gate28 sólo el nivel insuficiente no produce ninguno de estos efectos.
    return decision.request.has_value() || decision.clearWarpPending || decision.waitSteps != 0 ||
           !decision.warnings.empty();
}

Core::Expected<void, std::string> SendGate(std::uint8_t index, const Data::Tables::GateRecord& gate,
                                           const GameGateContext& context)
{
    WorldRuntime& runtime = context.world.Runtime();
    if (index == 0)
    {
        runtime.movement.gateRequestBlocked = 1; // 0x59a477, antes de SendPacket.
    }
    if (!context.requestGate)
    {
        return Core::Unexpected(std::string("0x599ef0: falta el servicio de envío del pedido de gate"));
    }
    context.requestGate(index);
    WriteWait(WorldLoadingWait, context); // 0x59ac6c, después de enviar y antes de las macros.
    const auto destination = Data::Tables::ReadGateRecord(context.gates, gate.target);
    if (!destination)
    {
        return Core::Unexpected(std::string("0x599ef0: el destino del gate no está en la tabla"));
    }
    if (destination->map == CastleSiegeMap || destination->map == LandOfTrialMap)
    {
        return Core::Unexpected(std::string("0x61c210 y 0x61c170: las macros de los mapas 30/31 no están portadas"));
    }
    runtime.movement.warpPending = true;
    runtime.selectedTargets.fill(-1);
    runtime.movement.movementTarget = 0;
    return {};
}

Core::Expected<GameLogic::GateDecision, std::string>
ApplyEligibleGate(std::size_t index, const Data::Tables::GateRecord& gate, const GameGateContext& context)
{
    WorldRuntime& runtime = context.world.Runtime();
    // 0x59a172..0x59adc7: sigue abierto durante el envío, contador y resets, y cierra al final.
    const Render::Models::LogicalAccess access(runtime.ownRecordDepth, context.random,
                                               Render::Models::OwnRecordCloseRandoms);
    GameLogic::GateDecision decision = DecideGate(index, gate, context);
    if (decision.waitSteps != 0)
    {
        WriteWait(decision.waitSteps, context);
        return decision;
    }
    if (!decision.warnings.empty())
    {
        return decision;
    }
    // 0x59a41f..0x59a434: el nivel alcanzó, vuelve a leer el contador antes de los bloqueos del pedido.
    static_cast<void>(ReadWait(context));
    if (decision.clearWarpPending)
    {
        runtime.movement.warpPending = false;
    }
    if (decision.request)
    {
        const auto sent = SendGate(*decision.request, gate, context);
        if (!sent.HasValue())
        {
            return Core::Unexpected(sent.Error());
        }
    }
    return decision;
}

void AppendDecision(GameLogic::GateDecision& result, const GameLogic::GateDecision& decision)
{
    result.warnings.insert(result.warnings.end(), decision.warnings.begin(), decision.warnings.end());
    result.clearWarpPending = result.clearWarpPending || decision.clearWarpPending;
    if (decision.request)
    {
        result.request = decision.request;
    }
    if (decision.waitSteps != 0)
    {
        result.waitSteps = decision.waitSteps;
    }
}
} // namespace

Core::Expected<GameLogic::GateDecision, std::string> CheckGameGates(const GameGateContext& context)
{
    WorldRuntime& runtime = context.world.Runtime();
    if (runtime.hero >= context.world.Actors().size())
    {
        return Core::Unexpected(std::string("0x599ef0: el héroe publicado no está en el pool"));
    }
    ActorState& hero = context.world.Actors()[runtime.hero];
    GameLogic::GateDecision result;
    for (std::size_t index = 0; index < Data::Tables::GateCount && index < context.gates.Count(); ++index)
    {
        const auto gate = Data::Tables::ReadGateRecord(context.gates, index);
        if (!gate || gate->flag != 1 || gate->map != runtime.map || !ContainsWithAccesses(*gate, hero, context))
        {
            continue;
        }
        if (ReadWait(context) != 0 || hero.byte399 != 0 || !PassesSilentLevelCheck(index, *gate, context))
        {
            continue;
        }
        const auto decision = ApplyEligibleGate(index, *gate, context);
        if (!decision.HasValue())
        {
            return Core::Unexpected(decision.Error());
        }
        AppendDecision(result, decision.Value());
        // 0x59adcc: aun con el contador recién escrito, continúa leyendo los siguientes rectángulos.
    }
    return result;
}
} // namespace Client::Scenes
