#pragma once

#include "Core/Expected.h"

#include <cstdint>
#include <string>

// El 0x139 (Skill\Javelin), los tres de la 0x2d de 0x4fd5f0 sobre o con el blanco de dueño (0x504a3a, 0x504a72,
// 0x504aa9): su alta (0x51dacb) y su paso (0x529b86). Se dibuja por el camino común (0x52da92 → 0x602e90) y la cola
// lo anima sin moverlo (0x52ccb0 → 0x52cd94). 06, "Los efectos con modelo de los tipos altos".
namespace Client::World
{
struct ModelEffect;
} // namespace Client::World

namespace Client::Scenes
{
struct HighEffectContext;
struct HighEffectStepContext;

inline constexpr std::int16_t JavelinEffect = 0x139;

// 0x51dacb..0x51db6e: vida 35, +0xe4 en 2, +0xd8 en 10, escala 1,2f, la velocidad (0, −5, 0), los ángulos copiados en
// +0x28, la partida en la posición del dueño (+0x108; sin dueño el original lee la dirección 0x10, MuRet corta) y la z
// 150 más. Después, rand() % 80 + 10 (r): la z de +0x28 más (subtipo · r − r).
[[nodiscard]] Core::Expected<void, std::string> BeginTheJavelin(World::ModelEffect& effect,
                                                                const HighEffectContext& context);

// 0x529b86..0x529de9: con dueño, la partida en su posición con la z 150 más; la escala 0,015f más. Con 25 o más de
// vida, +0xe4 10 más y avanza con +0x28. Si no, por debajo de 20, 0x4e9810 gira +0x28 hacia la partida de a +0xd8:
// a menos de 100, a la partida, la escala 0,04f más y, con la vida múltiplo de 3, la partícula 0x6ad/1; más lejos, la
// partícula siempre. Después (desde 24), +0xd8 1,5 más, avanza con +0x28, +0xe4 en rand() % 30 + 30 y, por encima de
// −50, la velocidad y 8 menos. Siempre: la z de los ángulos más +0xe4, +0x68 y el alpha en vida · 0,1f, la z en la de
// la partida más (el /1) o menos (el /2) FSIN(vida · 0,1f) · 30 y la luz dinámica (1; 0,6f; 0,3f) de radio 2.
[[nodiscard]] Core::Expected<void, std::string> StepTheJavelin(World::ModelEffect& effect,
                                                               const HighEffectStepContext& context);
} // namespace Client::Scenes
