#pragma once
#include "Core/Expected.h"
#include <cstdint>
#include <string>
namespace Client::Effects
{
struct ModelEffect;
} // namespace Client::Effects

namespace Client::CastleSiege
{
struct SiegeStoneStepServices;
} // namespace Client::CastleSiege
namespace Client::Scenes
{
struct WorldEntryResources;
} // namespace Client::Scenes

namespace Client::CastleSiege
{
// B7/01→44eea0 yB7/02→44efc0. El dueño del proyectil siempre es el héroe.
[[nodiscard]] Core::Expected<void, std::string> ApplyTheSiegeWeaponFire(std::uint16_t key, std::uint8_t kind,
                                                                        std::uint8_t mode, std::uint8_t x,
                                                                        std::uint8_t y,
                                                                        const Scenes::WorldEntryResources& resources);
[[nodiscard]] Core::Expected<void, std::string> ApplyTheSiegeWeaponImpact(std::uint8_t kind, std::uint8_t x,
                                                                          std::uint8_t y,
                                                                          const Scenes::WorldEntryResources& resources);
// Los serviciosdurantetodoelpasodelpool; lospunterospertenecenalWorldEntryResources.
[[nodiscard]] CastleSiege::SiegeStoneStepServices SiegeStoneServicesOf(const Scenes::WorldEntryResources& resources);
} // namespace Client::CastleSiege
