#pragma once

#include "Core/Expected.h"

#include <glm/ext/vector_float3.hpp>

#include <cstddef>
#include <cstdint>
#include <string>

// El 0x140, la marca que 0x510ef0 pone sobre un personaje con +0x369 en 1 (0x510f4d): su alta (0x51e0cb) y su paso
// (0x529f0c). Se dibuja por el camino común (0x52da92 → 0x602e90). 06, "Los efectos con modelo de los tipos altos".
namespace Client::Render::Models
{
class CrtRandom;
} // namespace Client::Render::Models

namespace Client::World
{
class ModelEffects;
class Particles;
struct ModelEffect;
} // namespace Client::World

namespace Client::Scenes
{
struct ActorState;
struct HighEffectStepContext;

inline constexpr std::int16_t GuildMarkEffect = 0x140;

// El hueso del dueño que la marca sigue: 0x4ec110(modelo del dueño, +0x120 + 0x3c0, (0, 0, 0), punto, 1).
inline constexpr std::size_t GuildMarkBone = 20;

// 0x51e0cb..0x51e233, después del prefijo de 0x518cf0 en `slot`: el alpha en 0, la z de los ángulos en 45, la x y la y
// del punto del hueso 20 del dueño (`bonePoint`) y la z ((punto + z del dueño) − z del dueño) + 60, vida 130, escala
// 3,2f, la luz en 1 y la partida en la posición. Después, diez veces: el giro rand() % 360 y dos radios rand() % 20 +
// 15 (cada uno sacado justo antes de usarlo), y la chispa 0x5c6/4 (escala 0,6f, con el efecto de dueño, hacia su
// posición) en (FSIN(giro) · r + x, FCOS(giro) · r2 + y, z). Treinta rand(); los de la chispa, ninguno.
[[nodiscard]] Core::Expected<void, std::string> BeginTheGuildMark(World::ModelEffect& effect, std::size_t slot,
                                                                  const ActorState& owner, const glm::vec3& bonePoint,
                                                                  World::Particles& particles,
                                                                  Render::Models::CrtRandom& random, float worldClock);

// 0x510ef0 hasta 0x518cf0: +0x369 en 1 y +0x36c (un float) en 0 o un NaN, o el reloj del mundo más de 5000 después.
[[nodiscard]] bool GuildMarkIsDue(const ActorState& actor, float worldClock);

// 0x510ef0 con +0x369 en 1: si +0x36c (un float) es 0 o un NaN, o si el reloj del mundo le lleva más de 5000, el 0x140
// (0x518cf0(0x140, posición, ángulos, luz, 0, el personaje, −1, 0, 0, 0, 0), con la tabla llena nada), y +0x36c pasa
// a ser el reloj. `bonePoint` es el punto del hueso 20 del personaje con el modelo como lo dejó su dibujo.
[[nodiscard]] Core::Expected<void, std::string> RaiseTheGuildMark(ActorState& actor, const glm::vec3& bonePoint,
                                                                  World::ModelEffects& effects,
                                                                  World::Particles& particles,
                                                                  Render::Models::CrtRandom& random, float worldClock);

// 0x529f0c..0x52a176. Sin dueño (+0x108), la vida en 0. Con el alpha en 1 o más, la z de los ángulos 5 más. Con la
// vida entre 121 y 124, cinco chispas 0x5c5/6 (escala 1,5f, 0x529f71); entre 96 y 120, el alpha 0,4f más hasta pasar 1
// (y entonces 1); por debajo de 50, 0,1f menos hasta pasar 0 hacia abajo (y entonces 0). Sigue al hueso 20 del dueño:
// x e y en dueño − héroe ([0x743a170]) + punto, y la z hacia ((punto + z) − z) + 60, en un décimo si está más arriba o
// a la mitad si no. Con la vida entre 40 y 59: el giro rand() % 360, dos radios rand() % 26 − 13 y rand() % 5, y la
// chispa 0x5c5/5 (escala 2, 0x52a16e) en (FSIN(giro) · r + x, FCOS(giro) · r2 + y, z + 48 − vida + k). Las 0x5c5/5 y
// /6 guardan la marca de dueño (su lugar en la tabla, el ebx de 0x52a134 y 0x529f5b); la /5 lee su posición al nacer
// (0x5403ad) y las dos la siguen en su paso. Con las partículas llenas, sin chispas ni sus rand().
[[nodiscard]] Core::Expected<void, std::string> StepTheGuildMark(World::ModelEffect& effect,
                                                                 const HighEffectStepContext& context);
} // namespace Client::Scenes
