#pragma once

#include "Core/Expected.h"

#include <span>
#include <string>

namespace Client::Scenes
{
struct FloorItemState;
struct FloorItemDrawContext;
// 608a00: pose previa al alpha, RenderItem repetido y proyección de la posición original.
[[nodiscard]] Core::Expected<void, std::string> DrawFloorItemDrawing(std::span<FloorItemState> items,
                                                                     const FloorItemDrawContext& context);
} // namespace Client::Scenes
