#pragma once

#include "Core/Expected.h"

#include <string>

namespace Client::World
{
struct WorldObject;
}
namespace Client::Scenes
{
struct GameObjectPass;

// 0x42c9f0 (0x602ed0, después de 0x4059d0 y 0x438c10), con el mapa 30, después del rand() común de 0x602ef4
// (que no usa). Sin el asedio:
// - el 0: con rand() % 3 en 0, la chispa 6ec/0 sobre el objeto, con luz (1, 1, 1), su escala y sin dueño;
// - el 0x29: la luz del blendMesh (+0x68) en sin(T · 0,002f) · 0,3f + 0,7f, que queda en el objeto;
// - el 0x34: la caída 6f4/6 sobre el objeto, con luz (1, 1, 1), su escala y sin dueño, sin rand();
// - el 0x36: la salpicadura 0x6f2 + (rand() & 1) con el subtipo 0 y la escala 1.
// Con el asedio, el 0 no hace nada, el 0x2a (con rand() % 3 en 0) suelta el humo 5f2/0x15 con luz (1, 1, 1), su
// escala y sin dueño, el 0x35 hace lo que el 0 sin el asedio y el 0x34 y el 0x36 no hacen nada. Los demás tipos no
// hacen nada. Sin el pool, el alta corta después de sus rand(), con su dirección.
[[nodiscard]] Core::Expected<void, std::string> StepCastleSiegeObjectFrame(World::WorldObject& object,
                                                                           const GameObjectPass& pass);
} // namespace Client::Scenes
