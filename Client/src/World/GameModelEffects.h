#pragma once

#include "Core/Expected.h"

#include <functional>
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
class FrameSprites;
// Lo que el caso de la mariposa (0x520d11) pide afuera de su efecto: la altura del terreno (0x5f93c0) y
// la tabla de sprites del frame (0x5491e0). Sin ellos, el caso corta en su dirección.
struct ModelEffectStepServices
{
    std::function<double(float, float)> terrainHeight{};
    FrameSprites* sprites = nullptr;
};
// 0x5208a0: CRT común, llegada/teleport, torre de Noria, 9/0 de Kalima con el owner de ondas y la mariposa
// 0xae (sus rand() del vuelo, la luz del subtipo, las chispas 5c5/7 del /3 y el sprite 0x5ac).
[[nodiscard]] Core::Expected<void, std::string>
StepGameModelEffect(ModelEffect& effect, const Render::Models::Skeleton* skeleton, Particles& particles,
                    float worldClock, Render::Models::CrtRandom& random, KalimaWorld* kalima = nullptr,
                    const ModelEffectStepServices& services = {});
} // namespace Client::World
