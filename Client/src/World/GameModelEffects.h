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
class KalimaWorld;
// 0x5208a0: CRT común, llegada/teleport, torre de Noria y9/0 de Kalima con el owner de ondas.
[[nodiscard]] Core::Expected<void, std::string>
StepGameModelEffect(ModelEffect& effect, const Render::Models::Skeleton* skeleton, Particles& particles,
                    float worldClock, Render::Models::CrtRandom& random, KalimaWorld* kalima = nullptr);
} // namespace Client::World
