#pragma once

#include "Core/Expected.h"
#include "Scenes/ActorActions.h"

#include <cstdint>
#include <string>

namespace Client::Scenes
{
struct ActorState;
struct WorldEntryResources;
[[nodiscard]] ActorState& PublishedHero(const WorldEntryResources& resources);
[[nodiscard]] Core::Expected<ActorStopContext, std::string> TransitionStopContext(const WorldEntryResources& resources);
[[nodiscard]] Core::Expected<void, std::string> StopTransitionHero(ActorState& actor,
                                                                   const WorldEntryResources& resources);
[[nodiscard]] Core::Expected<void, std::string> DressTransitionHero(ActorState& actor,
                                                                    const WorldEntryResources& resources);
[[nodiscard]] Core::Expected<void, std::string> LoadTransitionMap(std::uint8_t map, std::uint16_t playerKey,
                                                                  const WorldEntryResources& resources);
[[nodiscard]] Core::Expected<void, std::string> RefreshTransitionHeight(ActorState& actor,
                                                                        const WorldEntryResources& resources);
[[nodiscard]] Core::Expected<void, std::string> RecreateTransitionRaven(ActorState& actor,
                                                                        const WorldEntryResources& resources);
void ClearTransitionActorsAndItems(std::uint16_t playerKey, const WorldEntryResources& resources);
void CreateTransitionArrival(ActorState& actor, const WorldEntryResources& resources);
[[nodiscard]] Core::Expected<void, std::string> CloseTransitionWindows(const WorldEntryResources& resources);
} // namespace Client::Scenes
