#pragma once

#include "Core/Expected.h"

#include <cstddef>
#include <span>
#include <string>
#include <string_view>

namespace Client::Kernel
{
struct ActorState;
class WorldState;
} // namespace Client::Kernel

namespace Client::Guild
{
struct GuildState;
} // namespace Client::Guild

namespace Client::Scenes
{
struct WorldEntryResources;
} // namespace Client::Scenes

// Las puertas y los cambios de dueño del castillo: 0x44f340/0x44f560, 0x42a110 y 0x42ae60.
namespace Client::CastleSiege
{
inline constexpr std::size_t CastleBattleActorCount = 400;

// El alta del viewport sólo cambia acción y colisión; conserva flags y no pide sonido.
[[nodiscard]] Core::Expected<void, std::string> SetTheCastleGateState(Kernel::ActorState& actor, bool open,
                                                                      const Scenes::WorldEntryResources& resources);

// La respuesta del B2 cambia además las banderas completas y pide el sonido después de la colisión.
[[nodiscard]] Core::Expected<void, std::string> SetTheCastleGate(Kernel::ActorState& cellsActor,
                                                                 Kernel::ActorState& gate, bool open,
                                                                 const Scenes::WorldEntryResources& resources);

// Compara el nombre de la alianza de los 400 personajes vivos y visibles, después de soltar cada capa.
[[nodiscard]] Core::Expected<void, std::string> ApplyTheCastleOwner(std::string_view name, Kernel::WorldState& world);
[[nodiscard]] Core::Expected<void, std::string>
ApplyTheCastleOwner(std::string_view name, std::span<Kernel::ActorState, CastleBattleActorCount> actors,
                    const Guild::GuildState& guild);
} // namespace Client::CastleSiege
