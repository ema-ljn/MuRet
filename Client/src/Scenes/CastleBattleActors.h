#pragma once

#include "Core/Expected.h"

#include <cstddef>
#include <span>
#include <string>
#include <string_view>

// Las puertas y los cambios de dueño del castillo: 0x44f340/0x44f560, 0x42a110 y 0x42ae60.
namespace Client::Scenes
{
struct ActorState;
struct GuildState;
inline constexpr std::size_t CastleBattleActorCount = 400;
class WorldState;
struct WorldEntryResources;

// El alta del viewport sólo cambia acción y colisión; conserva flags y no pide sonido.
[[nodiscard]] Core::Expected<void, std::string> SetTheCastleGateState(ActorState& actor, bool open,
                                                                      const WorldEntryResources& resources);

// La respuesta del B2 cambia además las banderas completas y pide el sonido después de la colisión.
[[nodiscard]] Core::Expected<void, std::string> SetTheCastleGate(ActorState& cellsActor, ActorState& gate, bool open,
                                                                 const WorldEntryResources& resources);

// Compara el nombre de la alianza de los 400 personajes vivos y visibles, después de soltar cada capa.
[[nodiscard]] Core::Expected<void, std::string> ApplyTheCastleOwner(std::string_view name, WorldState& world);
[[nodiscard]] Core::Expected<void, std::string>
ApplyTheCastleOwner(std::string_view name, std::span<ActorState, CastleBattleActorCount> actors,
                    const GuildState& guild);
} // namespace Client::Scenes
