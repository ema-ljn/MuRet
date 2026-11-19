#pragma once

#include "Core/Expected.h"

#include <cstdint>
#include <string>

// Los efectos con modelo de los monstruos de Kalima (0x43a7a0) que son tipos altos: el 0x11a (Skill\seamanFX). Su alta
// (0x51c884) y su paso (0x5266ea). 06, "Los efectos con modelo de los tipos altos".
namespace Client::World
{
struct ModelEffect;
} // namespace Client::World

namespace Client::Scenes
{
struct HighEffectStepContext;

inline constexpr std::int16_t SeamanEffect = 0x11a;

// 0x51c884..0x51c8e3 y 0x51e3ca: +0x64 en −2, +0x68 en 0,2f, escala 0,4f, vida 20, +0xe4 en 120, +0xd8 en −40, la
// velocidad (0, −40, 0), la luz en (1, 1, 1) y la partida en la posición. Cualquier subtipo; sin rand().
void BeginTheSeaman(World::ModelEffect& effect);

// 0x5266ea..0x5267c2: la escala crece 0,02f, la z es la de la partida más +0xe4, +0xd8 baja 10 y +0xe4 baja 20 (no
// menos de 60); la velocidad pasa a (0, +0xd8, 0). Con la luz local `luminosity` (0x5208c5) por (0,1f; 0,3f; 0,6f),
// cuatro partículas 0x6f4/1 en su lugar con sus ángulos y la luz dinámica de radio 3. Sin rand() propios.
[[nodiscard]] Core::Expected<void, std::string> StepTheSeaman(World::ModelEffect& effect, float luminosity,
                                                              const HighEffectStepContext& context);
} // namespace Client::Scenes
