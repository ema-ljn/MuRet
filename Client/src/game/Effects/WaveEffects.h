#pragma once

#include "Core/Expected.h"

#include <cstdint>
#include <string>

// El 0x114 (Skill\Wave) y el 0x115 (Skill\Piercing): las altas de 0x518cf0 (0x51c129 y 0x51c8e8) que llegan por los
// tipos altos. Los pasos (0x526dd0 y 0x526f9d) son de World (StepGameModelEffect): no leen nada de afuera salvo la
// tabla de efectos. 06, "Los efectos con modelo de los tipos altos".
namespace Client::Kernel
{
struct ModelEffectArguments;
} // namespace Client::Kernel

namespace Client::Effects
{
struct ModelEffect;
} // namespace Client::Effects

namespace Client::Effects
{
struct HighEffectContext;

inline constexpr std::int16_t WaveEffect = 0x114;
inline constexpr std::int16_t PiercingEffect = 0x115;

// 0x51c129..0x51c2b2 y 0x51db73: el 0x114 que no es de World (la /2..6 son Effects::ModelEffects::CreateWaveEffect). Lo
// común (vida 20, +0xe4 en 0,1f, +0x64 en −2, +0x68 en 1, los ángulos en 0, la partida en la posición y la z + 50) y:
// - la /0 (0x51c186): sesenta joints 0x5ac/0 (0x530860) en la posición, cada uno con rand() % 40 + 70 de escala;
// - la /1 (0x51c1ca): vida 15, +0x74 en 0x2000, la z + 80, la escala en (rand() % 50) · 0,01f + 0,1f, +0xe4 en 0,01f,
//   la x de los ángulos en 90 y la z la del argumento; dos joints 0x6e8/0 de escala rand() % 40 + 70 y después la x y
//   la y + rand() % 100 − 50 cada una.
// El 0x6e8/0 y el 0x5ac/0 los crea el lote de joints de la CL11 (con el rand() de su luz y los tres del 0x5ac,
// FlareJoints.cpp); con los joints llenos, sigue sin crearlos.
[[nodiscard]] Core::Expected<void, std::string> BeginTheWave(Effects::ModelEffect& effect,
                                                             const Kernel::ModelEffectArguments& arguments,
                                                             const HighEffectContext& context);
} // namespace Client::Effects
