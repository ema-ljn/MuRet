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
// 0x425260 (0x604535), con el mapa 33 o el 54, después del rand() común de 0x602ef4 (que no usa): los
// sprites 0x5c6 de los huesos 6, 7, 8, 12, 13 y 17 del 0x1e y del 0x47; las cascadas 6f0/2 (0x38, rand()
// & 1), 6f2/4 (0x39) y 6f1/2 (0x3a, rand() % 3); las veinte neblinas 622/1 con dueño del 0x3b, el 0x3e y
// el 0x3f mientras +0x58 no sea −2; la mariposa 0xae del 0x3c con timeGetTime() & 0x3ff < 10, que después
// deja el objeto en −2; la luz 0,3 y la llama 5de/7 del 0x43; y el 0x46, que con rand() % 5 en 1 y
// timeGetTime() & 0x3ff < 30 crea el efecto 0xb3 (World::ModelEffects::CreateAidaWisp). El 0x29 y el
// 0x41/0x42 sólo dibujan (DrawAidaMeshPass) y acá no hacen nada. Un pool o un reloj ausente corta donde lo
// pide el ASM, conservando lo ya creado.
[[nodiscard]] Core::Expected<void, std::string> StepAidaObjectFrame(World::WorldObject& object,
                                                                    const GameObjectPass& pass);
} // namespace Client::Scenes
