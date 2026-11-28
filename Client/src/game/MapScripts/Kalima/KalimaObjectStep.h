#pragma once

#include "Core/Expected.h"

#include <cstdint>
#include <string>

namespace Client::Models
{
struct ClothWorld;
}
namespace Client::Kernel
{
class WorldState;
} // namespace Client::Kernel

namespace Client::MapScripts::Kalima
{
// 0x4379a0: public class/level table. Keeps the return of 7 when no range accepts.
[[nodiscard]] std::uint8_t KalimaLevelOf(std::uint8_t characterClass, std::uint16_t level);
// 0x606560→0x6061c0→0x4385a0: before any object, even with an empty pool. Returns the pass's local selector;
// WorldRuntime keeps the previous count until this prefix completes.
[[nodiscard]] Core::Expected<std::int32_t, std::string> StepKalimaObjectPrelude(Kernel::WorldState& world,
                                                                                Models::ClothWorld& cloth);
} // namespace Client::MapScripts::Kalima
