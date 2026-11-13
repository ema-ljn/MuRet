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
// 0x420d60: con el 0x1c17 en la mano izquierda (+0x2d8), suelta el cuervo de +0x430 (0x421d60) y lo crea de
// nuevo (0x4167b0), en cualquier mapa. La usan la entrada al mundo (0x4c4954) y los traslados.
[[nodiscard]] Core::Expected<void, std::string> RecreateTransitionRaven(ActorState& actor,
                                                                        const WorldEntryResources& resources);
// 0x420cf0: suelta el cuervo de +0x430 (0x421d60) y, fuera de los mapas 18 a 23, lo crea de nuevo, sin mirar
// la mano. La llama el F3 10 cuando el Dark Raven (0x1a05) llega al lugar 1 (0x5bca62).
[[nodiscard]] Core::Expected<void, std::string> ReplaceTheRaven(ActorState& actor,
                                                                const WorldEntryResources& resources);
void ClearTransitionActorsAndItems(std::uint16_t playerKey, const WorldEntryResources& resources);
void CreateTransitionArrival(ActorState& actor, const WorldEntryResources& resources);
// 0x53d930(personaje): las velocidades de combate (0x4f3f80), la acción 0x93 (0x4ea030) en el frame 5, +0x80 en 3,
// el alpha de destino en 1, el efecto 0x5c6 en su lugar (0x518cf0, sin dueño) y el sonido 0x76. La llaman el 1C del
// héroe (sin cambio de mapa) y el 0x12 de un jugador con el estado 1 (0x4c8a3e).
[[nodiscard]] Core::Expected<void, std::string> StartTheTeleportAction(ActorState& actor,
                                                                       const WorldEntryResources& resources);
[[nodiscard]] Core::Expected<void, std::string> CloseTransitionWindows(const WorldEntryResources& resources);
} // namespace Client::Scenes
