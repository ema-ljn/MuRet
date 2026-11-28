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
// B7/01 → 0x44eea0 and B7/02 → 0x44efc0. The projectile's owner is always the hero.
[[nodiscard]] Core::Expected<void, std::string> ApplyTheSiegeWeaponFire(std::uint16_t key, std::uint8_t kind,
                                                                        std::uint8_t mode, std::uint8_t x,
                                                                        std::uint8_t y,
                                                                        const Scenes::WorldEntryResources& resources);
[[nodiscard]] Core::Expected<void, std::string> ApplyTheSiegeWeaponImpact(std::uint8_t kind, std::uint8_t x,
                                                                          std::uint8_t y,
                                                                          const Scenes::WorldEntryResources& resources);
// The services for the whole pool step; the pointers belong to the WorldEntryResources.
[[nodiscard]] CastleSiege::SiegeStoneStepServices SiegeStoneServicesOf(const Scenes::WorldEntryResources& resources);
} // namespace Client::CastleSiege
