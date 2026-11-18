#pragma once
#include "Core/Expected.h"
#include <cstdint>
#include <string>
namespace Client::World
{
struct ModelEffect;
struct SiegeStoneStepServices;
} // namespace Client::World
namespace Client::Scenes
{
struct WorldEntryResources;
// B7/01→44eea0 yB7/02→44efc0. El dueño del proyectil siempre es el héroe.
[[nodiscard]] Core::Expected<void, std::string> ApplyTheSiegeWeaponFire(std::uint16_t key, std::uint8_t kind,
                                                                        std::uint8_t mode, std::uint8_t x,
                                                                        std::uint8_t y,
                                                                        const WorldEntryResources& resources);
[[nodiscard]] Core::Expected<void, std::string>
ApplyTheSiegeWeaponImpact(std::uint8_t kind, std::uint8_t x, std::uint8_t y, const WorldEntryResources& resources);
// Los serviciosdurantetodoelpasodelpool; lospunterospertenecenalWorldEntryResources.
[[nodiscard]] World::SiegeStoneStepServices SiegeStoneServicesOf(const WorldEntryResources& resources);
} // namespace Client::Scenes
