#pragma once

#include "Core/Expected.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

// Los efectos de las acciones 3 y 4 de Kundun (0x43d051 y 0x43d0fd, KalimaSiegeHelpers): el 0x128, sin modelo, que en
// su paso suelta diez 0x126 (Skill\phoenix, 0x527b37) con el /0 o los 0x125 (Skill\dragonhead, 0x527bb8 y 0x527c13)
// con el /1. Sus altas (0x518f38, 0x51cebf y 0x51cdc9) y sus pasos (0x527ad6, 0x52778c y 0x5276b7). El dibujo del 0x125
// es el caso de 0x438fe0 que llama 0x6002e0 (0x43909e, DrawTheHighEffectBody de Scenes/HighEffectDrawing), que sólo
// corre con el alpha en 0,01 o más (0x5ffdf0, 0x5ffe2a..0x5ffe3d): el 0x125 nace con el alpha en 0 (0x51cdf5) y sólo su
// joint 0x6ee/0x12 lo enciende (0x53949a); el 0x126 y el 0x128 van por el camino común. 06, "Los efectos con modelo de
// los tipos altos".
namespace Client::Kernel
{
struct ModelEffectArguments;
} // namespace Client::Kernel

namespace Client::Effects
{
struct ModelEffect;
class ModelEffects;
} // namespace Client::Effects

namespace Client::Effects
{
struct HighEffectContext;
struct HighEffectStepContext;

inline constexpr std::int16_t KundunSpiritEffect = 0x125;
inline constexpr std::int16_t KundunPhoenixEffect = 0x126;
inline constexpr std::int16_t KundunCallEffect = 0x128;

// 0x518f38..0x518f90, el 0x128 del lugar `slot` después del prefijo de 0x518cf0: el /0 y el /1, vida 30 (0x51cf97). El
// /2 (el de la caída de Kundun, 0x43f144) deja la vida en 40 y la clave (+0x8a) en 0 y vuelve a entrar en 0x518cf0
// como el 0x127/0 con él de dueño (0x518f90, CreateTheKundunEcho de Scenes/KundunFallEffects). Los demás subtipos,
// sólo lo común. Sin rand().
void BeginTheKundunCall(Effects::ModelEffects& effects, std::size_t slot);

// 0x518cf0(0x128, ...) sobre `effects` sin el resto del mundo, como lo piden 0x43d051 y 0x43d0fd: el prefijo
// (Effects::ModelEffects::Begin) y BeginTheKundunCall. Con la tabla llena, nada. Devuelve el lugar.
[[nodiscard]] Core::Expected<std::optional<std::size_t>, std::string>
CreateTheKundunCall(Effects::ModelEffects& effects, const Kernel::ModelEffectArguments& arguments);

// 0x527ad6..0x527c51, el paso del 0x128:
// - /0: con la vida en 30, diez 0x126 (0x527b37, i = 0, 8, …, 72) en su posición y con su luz, sin dueño, de ángulos
//   (0, 0, i + z + 320) en la x87 con un solo fstp.
// - /1: con la vida en 30, veinte 0x125 (0x527bb8); después, con la clave (+0x8a, MOVSX) entre 6 y 9, otro (0x527c13);
//   todos en su posición y con su luz, de ángulos (0, 0, clave · 30). Después la clave, uno más.
// - /2: con la clave en 0 y la vida debajo de 10, la clave en 1.
// Corta donde corta el alta de uno de ellos, con lo de antes.
[[nodiscard]] Core::Expected<void, std::string> StepTheKundunCall(Effects::ModelEffect& effect,
                                                                  const HighEffectStepContext& context);

// 0x51cebf..0x51cf6c, el 0x126: +0xd8 en 0,34f, vida 20, +0x64 en −2, escala 0,7f, la luz en 0,5 y el alpha en 0; el
// joint 0x613/0xf con el efecto de dueño, de ancho 100 y clave 0 (0x51cf45), en su posición, con los ángulos (0, 0,
// z · 0,01745329f); después el frame (+0x114) en rand() % 5. Cualquier subtipo. El 0x613/0xf (SpiritJoints.cpp) no saca
// rand() al nacer; en su paso le escribe al 0x126 los ángulos, el alpha y la posición (0x539249).
[[nodiscard]] Core::Expected<void, std::string> BeginTheKundunPhoenix(Effects::ModelEffect& effect,
                                                                      const HighEffectContext& context);

// 0x52778c..0x5277a9: con la vida debajo de 5 (edi, 0x5208df), el alpha en vida · 0,2f.
void StepTheKundunPhoenix(Effects::ModelEffect& effect);

// 0x51cdc9..0x51cebe: vida 30, +0x64 en −2, escala 1, la luz en 0,5, el alpha en 0 y la z 100 más; el giro rand() % 360
// y el radio rand() % 600 + 200, y el joint 0x6ee/0x12 con el efecto de dueño, de ancho 100 y clave 0 (0x51ce91), en
// (FSIN(giro) · radio + x, FCOS(giro) · radio + y, 0) con la x y la y de antes; después los ángulos en (−30, 0,
// rand() % 360). Cualquier subtipo. El 0x6ee/0x12 (SpiritJoints.cpp) no saca rand(); en su paso le pone al 0x125 el
// alpha y la posición (0x539494).
[[nodiscard]] Core::Expected<void, std::string> BeginTheKundunSpirit(Effects::ModelEffect& effect,
                                                                     const HighEffectContext& context);

// 0x5276b7..0x527787: la z de los ángulos rand() % 10 + 10 más; entre 300 y 600 de z (sin incluirlos), si rand() & 1
// da 0, el joint 0x6ee/0xe sin dueño, de ancho 100 y clave 0 (0x52777f), en (x, y, 350) con los ángulos (0, 0,
// (rand() % 24) · 30).
[[nodiscard]] Core::Expected<void, std::string> StepTheKundunSpirit(Effects::ModelEffect& effect,
                                                                    const HighEffectStepContext& context);
} // namespace Client::Effects
