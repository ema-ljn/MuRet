#pragma once

#include "Core/Expected.h"

#include <cstdint>
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
// piedras, la tabla de sprites del frame (0x5491e0) de la mariposa, la de los efectos (0x518cf0) y la luz
// dinámica (0x7b4ccc4) del chorro 0x5de y de las piedras 0x118/0x119, y timeGetTime() (0x6b8544) del calco 0x14e de los
// Fenrir. Sin ellos, el caso corta en su dirección.
struct ModelEffectStepServices
{
    std::function<double(float, float)> terrainHeight{};
    FrameSprites* sprites = nullptr;
    ModelEffects* effects = nullptr;
    TerrainLighting* lighting = nullptr;
    std::function<std::uint32_t()> systemTime{};
};
// 0x5208a0: CRT común, llegada/teleport, torre de Noria, 9/0 de Kalima con el owner de ondas, la mariposa
// 0xae (sus rand() del vuelo, la luz del subtipo, las chispas 5c5/7 del /3 y el sprite 0x5ac) y los del
// Dungeon y Lost Tower: la piedra 0xda que rebota (0x52444d), el chorro 0x5de/0 (0x52ad21: seis llamas
// 5de/0, a veces una piedrita y su luz, con la luz local del primer rand()) y las piedritas 0xc8/0xc9 con el
// subtipo 0 (0x5238be: rebotan, se inclinan y a veces sueltan un fuego 5d9 de subtipo 1 a 3) y con el 10 (igual, sin
// el fuego ni su rand()). Esos cuatro dejan en previousPosition la posición de antes del paso entero. Las piedras
// 0x118/0x119 del pisotón del caballo (0x5275ca), en sus pasos 33 a 36, sueltan el polvo 5f2/11 y una piedrita
// 0xc8/0xc9 con el subtipo 10 (de la tabla de efectos), con menos de 8 de vida se apagan, y siempre prenden la luz
// dinámica de radio 2. El 0xb3 de Aida (0x520b77) vaga con sus
// dos giros sorteados y el seno del reloj, y suelta una 5ac/9 por paso. El 0x135 del 30 (0x52973e) no
// muere y, con dueño, sube y baja con el seno del reloj. El calco 0x61d/3 del 0x38 de Crywolf (0x52b933), con la
// vida entre 6 y 9, suelta una órbita 60f/10; el /0, el /2 y el /7 cortan en 0x52b8a6. La onda 0x6eb/1 del galope del
// caballo en Icarus (0x52c5ca) crece y tiembla con tres rand(); la /0 del pisotón se achica 1 por paso, sin rand(); los
// otros subtipos del 0x6eb cortan. El rayo 0x14d de
// los Fenrir (0x5295a4) se prende de a 0,3f con el subtipo 0 y se apaga con el 1, y gira 0,15f por eje; el calco
// 0x14e (0x52965a) gira 0,1f en x, se apaga de a 0,05f (la luz según el subtipo) y cambia de cuadro cada 200 ms de
// timeGetTime(), hasta el quinto.
[[nodiscard]] Core::Expected<void, std::string>
StepGameModelEffect(ModelEffect& effect, const Render::Models::Skeleton* skeleton, Particles& particles,
                    float worldClock, Render::Models::CrtRandom& random, KalimaWorld* kalima = nullptr,
                    const ModelEffectStepServices& services = {});
} // namespace Client::World
