#pragma once

#include "Core/Expected.h"

#include <string>

namespace Client::World
{
struct ObjectDraw;
struct WorldObject;
} // namespace Client::World
namespace Client::Scenes
{
struct GameObjectPass;

// 0x430510 (0x602ed0 → 0x6044ed), con el mapa 34 (0x4303c0), después del rand() común de 0x602ef4 (que no usa):
// cada objeto suma 1 a la cuenta del clima (0x723c0c) y, pasados 70.000, un rand() la vuelve a 0 y deja el clima
// (0x5763358) en rand() % 3. El 0x29 gira 1° el brillo (0x723c10, que vuelve a 0 en 360) y deja dos sprites 0x60f de
// 1,3 en blanco sobre él, a 350 más la cuenta de 0x6046a0 (0x5763538, o 40 menos ella pasados 20) de altura: el
// primero sin giro y el segundo con el del brillo. El 0x54 saca un rand() y, en 0 módulo 3 con el estado de Crywolf
// en 1, suelta el humo 5f2/21 con luz (1, 1, 1), su escala y sin dueño. Sin los globales, el pool o la cuenta, corta
// donde lo pide el ASM, con lo anterior ya hecho.
[[nodiscard]] Core::Expected<void, std::string> StepCrywolfObjectFrame(const World::WorldObject& object,
                                                                       const GameObjectPass& pass);

// 0x4306a0, el último eslabón de la cadena de 0x6002e0, con el mapa 34 y un objeto que 0x6002e0 dibuja (`draw`, el
// de World::DrawOf, en Body). Con el 0x24, el color del modelo en 0,4 y su RenderBody; con el 0xfa, en (0; 0; 0,9).
// El 0x29 y el 0x38 dibujan el cuerpo con el color que traen; el 0x39, el 0x47, el 0x48, el 0x49, el 0x4a, el 0x4d,
// el 0x4e y el 0x51 salen sin cuerpo (Hidden). Los demás siguen a 0x4339d0, que no tiene nada fuera de 0x171..0x1b4,
// y 0x6002e0 dibuja su cuerpo. Con un frame del original, además, los sorteos de cada tipo, antes del rand() común
// de 0x602ef4: el 0x38 con el estado en 0, un rand() para la luz (rand() % 10.000 · 0,0001f) y, con otro en 0 módulo
// 3, el efecto 0x61d/3 (World::ModelEffects::CreateFadingDecal, con 4 en +0x8a); el 0x39 y el 0x47, con rand() % 3 en
// 0, la chispa 6ec/5 (y el 0x39, el humo 5f2/21) con luz (1, 1, 1), su escala y sin dueño; el 0x48, con rand() % 10
// en 0, otro rand() para la luz L = ((rand() & 3) + 3) · 0,1f y el humo 5f2/33 50 más arriba, con luz (L; L · 0,6f;
// L · 0,2f), su escala y sin dueño; el 0x4a, el humo con el estado en 1; el 0x4d y el 0x4e, con rand() % 6 en 0, la
// nube 622/9 con su luz; y el 0x49, con el clima en 0, la arena del HUD (0x576353c) en 1. El 0x51 con el estado en 2
// corta.
[[nodiscard]] Core::Expected<void, std::string> CrywolfObjectBody(const World::WorldObject& object,
                                                                  World::ObjectDraw& draw, const GameObjectPass& pass);
} // namespace Client::Scenes
