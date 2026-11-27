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

namespace Client::Scenes
{
// 4379a0: tabla pública clase/nivel. Conserva el retorno7 cuando ningún rango acepta.
[[nodiscard]] std::uint8_t KalimaLevelOf(std::uint8_t characterClass, std::uint16_t level);
// 606560→6061c0→4385a0: antes de cualquier objeto, aun con el pool vacío. Devuelve el selector
// local del pase; WorldRuntime conserva el count anterior hasta completar este prefijo.
[[nodiscard]] Core::Expected<std::int32_t, std::string> StepKalimaObjectPrelude(Kernel::WorldState& world,
                                                                                Models::ClothWorld& cloth);
} // namespace Client::Scenes
