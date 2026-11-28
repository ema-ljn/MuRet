#pragma once

#include "Core/Expected.h"

#include <cstdint>
#include <span>
#include <string>

namespace Client::Kernel
{
struct FloorItemState;
} // namespace Client::Kernel

namespace Client::Scenes
{
struct FloorItemDrawContext;
} // namespace Client::Scenes

namespace Client::Items
{
// 0x608950: the z a body part adds to its root, by group (helm, armor, pants, gloves; boots and the rest 0), compared
// signed as the original does. The inventory inlines the same rule (0x5e0e92..0x5e0ee6).
[[nodiscard]] float BodyPartRootHeight(std::int16_t model);
// 0x608a00: pose before the alpha, RenderItem repeated and projection of the original position.
[[nodiscard]] Core::Expected<void, std::string> DrawFloorItemDrawing(std::span<Kernel::FloorItemState> items,
                                                                     const Scenes::FloorItemDrawContext& context);
} // namespace Client::Items
