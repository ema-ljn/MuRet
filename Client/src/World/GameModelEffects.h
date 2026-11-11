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
class ModelEffects;
class Particles;
class KalimaWorld;
class FrameSprites;
struct TerrainLighting;
// Lo que los casos piden afuera de su efecto: la altura del terreno (0x5f93c0) de la mariposa y de las
// piedras, la tabla de sprites del frame (0x5491e0) de la mariposa, y la de los efectos (0x518cf0) y la luz
// dinámica (0x7b4ccc4) del chorro 0x5de. Sin ellos, el caso corta en su dirección.
struct ModelEffectStepServices
{
    std::function<double(float, float)> terrainHeight{};
    FrameSprites* sprites = nullptr;
    ModelEffects* effects = nullptr;
    TerrainLighting* lighting = nullptr;
};
// 0x5208a0: CRT común, llegada/teleport, torre de Noria, 9/0 de Kalima con el owner de ondas, la mariposa
// 0xae (sus rand() del vuelo, la luz del subtipo, las chispas 5c5/7 del /3 y el sprite 0x5ac) y los del
// Dungeon y Lost Tower: la piedra 0xda que rebota (0x52444d), el chorro 0x5de/0 (0x52ad21: seis llamas
// 5de/0, a veces una piedrita y su luz, con la luz local del primer rand()) y las piedritas 0xc8/0xc9 con el
// subtipo 0 (0x5238be: rebotan, se inclinan y a veces sueltan un fuego 5d9 de subtipo 1 a 3). Esos cuatro
// dejan en previousPosition la posición de antes del paso entero. El 0xb3 de Aida (0x520b77) vaga con sus
// dos giros sorteados y el seno del reloj, y suelta una 5ac/9 por paso. El 0x135 del 30 (0x52973e) no
// muere y, con dueño, sube y baja con el seno del reloj.
[[nodiscard]] Core::Expected<void, std::string>
StepGameModelEffect(ModelEffect& effect, const Render::Models::Skeleton* skeleton, Particles& particles,
                    float worldClock, Render::Models::CrtRandom& random, KalimaWorld* kalima = nullptr,
                    const ModelEffectStepServices& services = {});
} // namespace Client::World
