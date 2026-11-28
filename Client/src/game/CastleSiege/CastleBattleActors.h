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

// The castle's gates and owner changes: 0x44f340/0x44f560, 0x42a110 and 0x42ae60.
namespace Client::CastleSiege
{
inline constexpr std::size_t CastleBattleActorCount = 400;

// The viewport entry only changes the action and the collision; it keeps the flags and asks for no sound.
[[nodiscard]] Core::Expected<void, std::string> SetTheCastleGateState(Kernel::ActorState& actor, bool open,
                                                                      const Scenes::WorldEntryResources& resources);

// The B2 reply also changes the whole flags and asks for the sound after the collision.
[[nodiscard]] Core::Expected<void, std::string> SetTheCastleGate(Kernel::ActorState& cellsActor,
                                                                 Kernel::ActorState& gate, bool open,
                                                                 const Scenes::WorldEntryResources& resources);

// Compares the alliance name of the 400 live and visible characters, after dropping each layer.
[[nodiscard]] Core::Expected<void, std::string> ApplyTheCastleOwner(std::string_view name, Kernel::WorldState& world);
[[nodiscard]] Core::Expected<void, std::string>
ApplyTheCastleOwner(std::string_view name, std::span<Kernel::ActorState, CastleBattleActorCount> actors,
                    const Guild::GuildState& guild);
} // namespace Client::CastleSiege
