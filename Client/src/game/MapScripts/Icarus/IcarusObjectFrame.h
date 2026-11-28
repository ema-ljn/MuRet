#pragma once

#include "Core/Expected.h"

#include <string>

namespace Client::World
{
struct WorldObject;
}
// 0x602ed0 con el mapa 10 (0x603eef, la tabla 0x60466c), después del rand() común de 0x602ef4. Contrato:
// 06 § Los objetos de Icarus.
namespace Client::Scenes
{
struct GameObjectPass;

// - Del 0 al 5: la primera vez (+0x58 distinto de −2), nubes 622 con el subtipo del tipo, veinte del 0 al 2
//   y diez del 3 al 5, con la posición, los ángulos y la escala del objeto, la luz (0,1f; 0,1f; 0,1f) y el
//   objeto de dueño; después +0x58 = −2.
// - El 10: el punto del hueso 3 del buffer global de la pose (0x4ec110 con desplazamiento cero) y una 5ac/0
//   ahí, con los ángulos del objeto, luz 1, escala 1 y sin dueño.
// - Del 6 al 9, nada.
// Las partículas van al pool compartido (0x53fe70); uno lleno no es error. Sin pool o con un alta que MuRet
// no tiene corta con su dirección, después de lo que ya hizo.
[[nodiscard]] Core::Expected<void, std::string> StepIcarusObjectFrame(World::WorldObject& object,
                                                                      const GameObjectPass& pass);
} // namespace Client::Scenes
