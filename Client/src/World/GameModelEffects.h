#pragma once

#include "Core/Expected.h"

#include <string>

namespace Client::Render::Models
{
class CrtRandom;
struct Skeleton;
} // namespace Client::Render::Models
namespace Client::World
{
struct ModelEffect;
class Particles;
// 0x5208a0: el prefijo de CRT y los tipos de llegada/teleport y de la torre de Noria.
[[nodiscard]] Core::Expected<void, std::string> StepGameModelEffect(ModelEffect& effect,
                                                                    const Render::Models::Skeleton* skeleton,
                                                                    Particles& particles, float worldClock,
                                                                    Render::Models::CrtRandom& random);
} // namespace Client::World
