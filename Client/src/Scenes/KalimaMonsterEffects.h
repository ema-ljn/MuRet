#pragma once

#include "Core/Expected.h"

#include <cstdint>
#include <string>

// Los efectos con modelo de los monstruos de Kalima (0x43a7a0) que son tipos altos: el 0x11a (Skill\seamanFX), su alta
// (0x51c884) y su paso (0x5266ea), y el 0x615 (sin dibujo: suelta los rayos 0x615/5), su alta (0x51ed79) y su paso
// (0x52b4a7). 06, "Los efectos con modelo de los tipos altos".
namespace Client::World
{
struct ModelEffect;
} // namespace Client::World

namespace Client::Scenes
{
struct HighEffectStepContext;

inline constexpr std::int16_t SeamanEffect = 0x11a;
inline constexpr std::int16_t ThunderEffect = 0x615;

// 0x51c884..0x51c8e3 y 0x51e3ca: +0x64 en −2, +0x68 en 0,2f, escala 0,4f, vida 20, +0xe4 en 120, +0xd8 en −40, la
// velocidad (0, −40, 0), la luz en (1, 1, 1) y la partida en la posición. Cualquier subtipo; sin rand().
void BeginTheSeaman(World::ModelEffect& effect);

// 0x5266ea..0x5267c2: la escala crece 0,02f, la z es la de la partida más +0xe4, +0xd8 baja 10 y +0xe4 baja 20 (no
// menos de 60); la velocidad pasa a (0, +0xd8, 0). Con la luz local `luminosity` (0x5208c5) por (0,1f; 0,3f; 0,6f),
// cuatro partículas 0x6f4/1 en su lugar con sus ángulos y la luz dinámica de radio 3. Sin rand() propios.
[[nodiscard]] Core::Expected<void, std::string> StepTheSeaman(World::ModelEffect& effect, float luminosity,
                                                              const HighEffectStepContext& context);

// 0x51ed79..0x51edf4: vida 10. Con dueño (+0x108, el personaje de 0x43ab5f) y el subtipo 0, la posición corrida
// (−25, −80, 0) girado por los ángulos del dueño; si no, nada más. Sin rand().
void BeginTheThunder(World::ModelEffect& effect);

// 0x52b4a7..0x52b568: con menos de 5 de vida (edi, 0x5208df), par y el subtipo 0, un punto corrido (rand() % 64 − 32)
// en x y en y y el joint 0x615/5 ahí con sus ángulos, sin dueño y la escala rand() % 10 + 50 (0x52b560; dos rand() más
// con lugar en los joints). Si no, nada.
[[nodiscard]] Core::Expected<void, std::string> StepTheThunder(World::ModelEffect& effect,
                                                               const HighEffectStepContext& context);
} // namespace Client::Scenes
