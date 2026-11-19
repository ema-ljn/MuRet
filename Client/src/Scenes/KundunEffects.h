#pragma once

#include "Core/Expected.h"

#include <cstdint>
#include <string>

// El 0x125, lo que suelta el paso del 0x128 de Kundun (0x527bb8 y 0x527c13): su alta (0x51cdc9) y su paso (0x5276b7).
// El 0x128 todavía no nace (0x43d051 y 0x43d0fd cortan, KalimaSiegeHelpers), así que hoy sólo lo alcanzan los tests.
// Se dibuja por el camino común (0x52da92 → 0x602e90). 06, "Los efectos con modelo de los tipos altos".
namespace Client::World
{
struct ModelEffect;
} // namespace Client::World

namespace Client::Scenes
{
struct HighEffectContext;
struct HighEffectStepContext;

inline constexpr std::int16_t KundunSpiritEffect = 0x125;

// 0x51cdc9..0x51cebe: vida 30, +0x64 en −2, escala 1, la luz en 0,5, el alpha en 0 y la z 100 más; el giro rand() % 360
// y el radio rand() % 600 + 200, y el joint 0x6ee/0x12 con el efecto de dueño, de ancho 100 y clave 0 (0x51ce91), en
// (FSIN(giro) · radio + x, FCOS(giro) · radio + y, 0) con la x y la y de antes; después los ángulos en (−30, 0,
// rand() % 360). Cualquier subtipo. El 0x6ee/0x12 con un efecto de dueño es del lote de joints: con lugar corta ahí,
// después de los dos rand(); con los joints llenos sigue.
[[nodiscard]] Core::Expected<void, std::string> BeginTheKundunSpirit(World::ModelEffect& effect,
                                                                     const HighEffectContext& context);

// 0x5276b7..0x527787: la z de los ángulos rand() % 10 + 10 más; entre 300 y 600 de z (sin incluirlos), si rand() & 1
// da 0, el joint 0x6ee/0xe sin dueño, de ancho 100 y clave 0 (0x52777f), en (x, y, 350) con los ángulos (0, 0,
// (rand() % 24) · 30).
[[nodiscard]] Core::Expected<void, std::string> StepTheKundunSpirit(World::ModelEffect& effect,
                                                                    const HighEffectStepContext& context);
} // namespace Client::Scenes
