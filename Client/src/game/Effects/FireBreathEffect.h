#pragma once

#include "Core/Expected.h"

#include <cstdint>
#include <string>

// El efecto 0x6ac de la habilidad 0x31 de 0x4fd5f0 (0x4fec15), sin modelo ni dibujo: nace con cuarenta joints 0x617/1
// en dos bocas, suelta fuego 0x5db/0xa a los costados en cada paso y termina con chispas 0x5c5/2 y la 0x5ee/0 de
// 0x51fef0. Su alta (0x51f5fe) y su paso (0x52c7f0). 06, "Los efectos con modelo de los tipos altos".
namespace Client::Effects
{
struct ModelEffect;
} // namespace Client::Effects

namespace Client::Effects
{
struct HighEffectContext;
struct HighEffectStepContext;

inline constexpr std::int16_t FireBreathEffect = 0x6ac;

// 0x51f5fe..0x51f7f8: vida 10, +0xd8 en 1, la velocidad (0, −30, 0) y la posición corrida por (0, −20, 50) girado por
// sus ángulos. Después, dos bocas, (−20, −20, 60) y (30, −20, 60) giradas y sumadas a la posición que recibe el alta
// (no a la corrida): en cada una, veinte vueltas de 18 con rand() % 20 y el joint 0x617/1 (escala 10, sin dueño) ahí,
// con los ángulos (rand() % 20 + x + 5; la y que suma las vueltas; z). Cualquier subtipo.
[[nodiscard]] Core::Expected<void, std::string> BeginTheFireBreath(Effects::ModelEffect& effect,
                                                                   const HighEffectContext& context);

// 0x52c7f0..0x52c97c: la luz dinámica de radio 2 con la luz local `luminosity` (0x5208c5) por (0,5; 0,5; 0,8f); la
// escala (rand() % 3 + 2) · (15 − vida) · 0,05f; a 20 a cada lado (los ángulos con la z 90 más), la partida y en ella
// el fuego 0x5db/0xa con sus ángulos, la luz (1, 1, 1) y su escala. Con la vida en 1, 0x51fef0(posición, 1).
[[nodiscard]] Core::Expected<void, std::string> StepTheFireBreath(Effects::ModelEffect& effect, float luminosity,
                                                                  const HighEffectStepContext& context);
} // namespace Client::Effects
