#pragma once
#include "Core/Expected.h"
#include "World/WorldObjectDrawingError.h"
#include <cstdint>
namespace Client::World
{
struct WorldObject;
struct WorldObjectDrawingContext;
// 6002e0 no posa ni filtra alpha como602e90; conserva metadata compartida y orden.
[[nodiscard]] Core::Expected<void, WorldObjectDrawingError>
DrawWorldObject(WorldObject& object, WorldObjectDrawingContext& context, std::uint32_t mode);
// 602ed0: RNG común, todos los mapas y cola AL. Caller604a4d ignora los residuos físicos EAX.
[[nodiscard]] Core::Expected<void, WorldObjectDrawingError> DrawWorldObjectEffects(WorldObject& object,
                                                                                   WorldObjectDrawingContext& context);
} // namespace Client::World
