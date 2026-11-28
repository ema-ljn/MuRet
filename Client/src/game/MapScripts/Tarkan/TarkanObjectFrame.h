#pragma once

#include "Core/Expected.h"

#include <string>

namespace Client::World
{
struct WorldObject;
}
// 0x602ed0 con el mapa 8 (0x603a3e, la tabla 0x6045f8), después del rand() común de 0x602ef4. Contrato:
// 06 § Los objetos de Tarkan.
namespace Client::Scenes
{
struct GameObjectPass;

// Con el reloj del mundo T y s = sin(fase · 0,002f) · 0,3f + 0,7f en la x87 (L, s guardada en float):
// - el 0: un sprite 0x5c6 (Spark03) en el hueso 6 del buffer global de la pose, de escala L · 5 y luz
//   (s · 0,5882353f; s; s), con la fase T;
// - el 0x3f: un 0x716 (Impack03) en el hueso 2, de escala L · 1,5 y la misma luz, con la fase
//   ángulo z · 5 + T; el 0x40, igual pero con la luz (L; L · 0,32f; L · 0,32f);
// - el 0x3c: la primera vez (+0x58 distinto de −2), veinte humos 5f2/6 con la luz (+0xf4) y la escala del
//   objeto; después +0x58 = −2;
// - el 0x46: +0x58 = −2 y, con rand() % 5 == 0, un 5f2/7;
// - el 0x4c: +0x58 = −2 y, con ftol(T) % 5.000 mayor que 4.500, un 5f2/4;
// - el 0x53: +0x58 = −2 y, con ftol(T) % 10.000 entre ftol(ángulo z) · 10 + 3.500 y + 4.000 (sin los bordes),
//   un 5f2/8; con rand() % 3 == 0, además un 5f2/4 corrido ((rand() & 127) − 64) en x y en y, de media
//   escala, y la piedrita 200 + rand() % 2 (0x518cf0) en el objeto.
// Los humos van al pool compartido de las partículas (0x53fe70) con el reloj prestado, sin dueño; uno lleno
// no es error. Sin pool, sin sprites o sin la tabla de efectos corta con su dirección, después de lo que ya
// hizo.
[[nodiscard]] Core::Expected<void, std::string> StepTarkanObjectFrame(World::WorldObject& object,
                                                                      const GameObjectPass& pass);
} // namespace Client::Scenes
