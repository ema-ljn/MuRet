#pragma once

#include "Core/Expected.h"

#include <glm/ext/vector_float3.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

// El 0x11b: el /1, los tres anillos que la habilidad 0x37 de Kalima (0x439b20) suelta sobre su dueño, con los dos
// joints 0x618/0xa y /0xb que siguen sus puntos +0x40 y +0x4c; el /0, el destello del joint 0x613/0x13 (0x539b08).
// Su alta (0x51c782) y su paso (0x526507). Su dibujo es el caso propio de 0x6002e0 (0x601e68, DrawTheHighEffectBody
// de Scenes/HighEffectDrawing), que en cada pase deja +0x40 y +0x4c en los huesos 2 y 3. 06, "Los efectos con modelo
// de los tipos altos".
namespace Client::Effects
{
class ModelEffects;
struct ModelEffect;
} // namespace Client::Effects

namespace Client::Effects
{
struct HighEffectContext;
struct HighEffectStepContext;

inline constexpr std::int16_t RingEffect = 0x11b;

// 0x51c782..0x51c883, sin rand(). La /0: vida 1 y escala 2,3f. La /1: vida 49, escala 2, +0x64 en −2, +0x68 en 0,5,
// los ángulos copiados (+0x28) en 0, la velocidad (0, −45, 0), la partida (+0x17c), el destino (+0x1b0) y los puntos
// +0x40 y +0x4c en la posición, y los joints 0x618/0xa y /0xb (0x51c857 y 0x51c875) en la posición, con el efecto de
// dueño y ancho 30. Otro subtipo, nada (0x51db73).
[[nodiscard]] Core::Expected<void, std::string> BeginTheRing(Effects::ModelEffect& effect,
                                                             const HighEffectContext& context);

// 0x518cf0(0x11b, punto, ángulos, luz, 0, 0, −1, 0, 0, 0, 0) desde el paso del joint 0x613/0x13 (0x539b08): el
// prefijo, vida 1 y escala 2,3f (0x51c789), sin rand(). Lleno, ninguno.
std::optional<std::size_t> CreateTheRingSpark(Effects::ModelEffects& effects, const glm::vec3& position,
                                              const glm::vec3& angles, const glm::vec3& light);

// 0x526507..0x5266e5, la /1 con dueño (+0x108, el personaje): con la vida múltiplo de 10, el destino (+0x1b0) en la
// posición; la partida en el dueño 200 más arriba y 0x4e9810 gira hacia ella de a 10. Dos (rand() & 31) − 16 sacuden
// los ángulos copiados (x · 0,2f y z · 0,8f), que se suman a los ángulos y se frenan (0,6f y 0,8f). Con menos de 10 de
// vida, el alpha por 0,909f, +0x68 por 0,833f y la x de los ángulos 5 más hasta pasar 80; si no, con 0x5f93c0, a
// menos de 100 sobre el terreno sube (x en −5) y a más de 400 baja (x en 5), con la x copiada en 0. Después, la
// partícula 0x5d9/5 (luz 1, escala 1,2f) y la luz dinámica (l; l · 0,3f; l · 0,1f) de radio 4. Sin dueño o con otro
// subtipo, nada.
[[nodiscard]] Core::Expected<void, std::string> StepTheRing(Effects::ModelEffect& effect, float luminosity,
                                                            const HighEffectStepContext& context);
} // namespace Client::Effects
