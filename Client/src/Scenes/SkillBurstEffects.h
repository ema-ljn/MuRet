#pragma once

#include "Core/Expected.h"

#include <cstdint>
#include <string>

// CL11, lote de los tipos altos: los efectos sin dibujo que sólo sueltan partículas, luz o un joint en su paso, y el
// paso del tiro de la skill 0x2e.
// - El 0x5ca, la bola de la habilidad 0x11 (0x505053): alta 0x51e529, paso 0x52abbe y su rama de 0x518b60.
// - El 0x5da, el golpe del 0x33 (0x4f4fb9): alta 0x51e773 y paso 0x52acc3.
// - El 0x5be, el barrido de la habilidad 0x37 (0x4f9fae): alta 0x51e397 y paso 0x52a6d6.
// - El 0x13a, el tiro de la skill 0x2e (su alta es la de Scenes/ArrowEffects, CreateTheBlast): paso 0x529dee.
// Los tres primeros son tipos de 0x151 en adelante sin caso en 0x52d620 ni en el pase del suelo (0x52dc20): no se
// dibujan. El 0x13a va por el camino común (0x52da92 → 0x602e90). 06, "Los efectos con modelo de los tipos altos".
namespace Client::World
{
struct ModelEffect;
} // namespace Client::World

namespace Client::Scenes
{
struct HighEffectStepContext;

inline constexpr std::int16_t SweepEmitterEffect = 0x5be; // el que suelta el joint 0x61c/0 de la habilidad 0x37
inline constexpr std::int16_t EnergyBallEffect = 0x5ca;   // la bola de la habilidad 0x11
inline constexpr std::int16_t FlameBurstEffect = 0x5da;   // el golpe del 0x33
inline constexpr std::int16_t ArrowBlastEffect = 0x13a;   // el tiro de la skill 0x2e

// 0x51e397..0x51e3eb, el 0x5be de cualquier subtipo: vida 30, la luz en 0,8f, los ángulos copiados (+0x28) en
// (0, 0, z + 45) con la z de los ángulos del argumento (un fadd y un fstp) y la partida (+0x17c) en la posición. Sin
// rand().
void BeginTheSweepEmitter(World::ModelEffect& effect);

// 0x51e529..0x51e569, el 0x5ca de cualquier subtipo: vida 20, la velocidad (0, −60, 0), la z 100 más y, con el /1, la
// escala en 1,5f. Sin rand().
void BeginTheEnergyBall(World::ModelEffect& effect);

// 0x51e773..0x51e7d7, el 0x5da de cualquier subtipo: vida 10 y (0, −60, 0) girado por los ángulos sumado a la
// posición, con 130 más en la z (en la x87 hasta el fstp). Sin rand().
void BeginTheFlameBurst(World::ModelEffect& effect);

// 0x52a6d6..0x52a732: con la vida en 30, el joint 0x61c/0 (0x52a72a) en (x, y, z + 100), con los ángulos copiados,
// el dueño del efecto (+0x108, el personaje), ancho 150, la clave (+0x8a) y la skill (+0x89) del efecto. Corta donde
// corta el alta del joint.
[[nodiscard]] Core::Expected<void, std::string> StepTheSweepEmitter(World::ModelEffect& effect,
                                                                    const HighEffectStepContext& context);

// 0x52abbe..0x52ac75: el nivel vida · 0,2f; el color, (nivel, nivel, nivel) con el /0, (1; 0,8f; 0,2f) con el /1 y
// el blanco local (0x520921) con los demás; las partículas 0x5ca/0 (escala 1) y 0x5c6/0 (escala 4) con ese color; la
// luz dinámica (nivel · 0,2f; nivel · 0,4f; nivel) de radio 2; y 0x518b60: con el dueño (+0x108) vivo, visto (+0x16c)
// y a 100 o menos en el plano, la vida en 1 y la partícula 0x5c6/1 en blanco de escala 6. Un dueño que no es uno de
// los personajes del mundo corta (0x518b7a).
[[nodiscard]] Core::Expected<void, std::string> StepTheEnergyBall(World::ModelEffect& effect,
                                                                  const HighEffectStepContext& context);

// 0x52acc3..0x52b3b1: la partícula 0x5da/1 en blanco de escala 1 y la luz dinámica (L; L · 0,6f; L · 0,3f) de radio
// 2, con L la luz local del paso (0x5208c5).
[[nodiscard]] Core::Expected<void, std::string> StepTheFlameBurst(World::ModelEffect& effect, float luminosity,
                                                                  const HighEffectStepContext& context);

// 0x529dee..0x529f07: la x de los ángulos 5 menos y la y de la velocidad 8 menos; con la vida debajo de 2, el /0 y
// dueño (+0x108, el blanco del tiro): la x de los ángulos en 90, el subtipo en 1 y, en la posición del dueño con
// rand() % 100 − 50 más en x y en y y 1200 más en z, los joints 0x632/2 y /3 (0x529eca y 0x529eed) con el efecto de
// dueño y ancho 50, que MuRet no tiene: con lugar en los joints corta en el primero. Si no, con el /1, el lugar libre.
// Un dueño que no es uno de los personajes del mundo corta (0x529e3e).
[[nodiscard]] Core::Expected<void, std::string> StepTheArrowBlast(World::ModelEffect& effect,
                                                                  const HighEffectStepContext& context);
} // namespace Client::Scenes
