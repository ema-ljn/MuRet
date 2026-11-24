#pragma once

#include "Core/Expected.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace Client::Crt
{
class CrtRandom;
}
namespace Client::World
{
class ModelEffects;
struct ModelEffect;
struct ModelEffectArguments;
struct ModelEffectStepServices;

// Skill\skinshell (0x61672d): las cáscaras que 0x4ee770 suelta de los vértices del 0x20b (Player\Shadow01) cuando el
// joint 0x632/0 o /1 con dueño llega al suelo (0x53c227, 0x53c241), con el subtipo del joint.
inline constexpr std::int16_t SkinShellEffect = 0x137;

// 0x518cf0 con el 0x137 (0x51d8c4..0x51da45), después del prefijo común (ModelEffects::Begin): el empuje (en y) en
// ((rand() & 127) + 32) · 0,1f, la z + 50, la vida (rand() & 15) + 32, la escala (rand() % 3 + 3) · 0,1f, el alpha en
// 0,5, la z de los ángulos en rand() % 360 y la velocidad (+0xcc), el empuje girado por los ángulos; la caída (+0xe4)
// rand() % 5 + 2 más rand() % 5, y los tres ángulos rand() % 360. La luz: (0,5; 0,5; 0,5) el /0 y (0,1f; 0,6f; 1) el
// /1; con otro subtipo queda la del pedido. Nueve rand(). Con la tabla llena, nada (ni rand()).
[[nodiscard]] std::optional<std::size_t> CreateSkinShell(const ModelEffectArguments& arguments, ModelEffects& effects,
                                                         Crt::CrtRandom& random);

// 0x529926..0x529a4a, el cuerpo del paso (el caller corre el prólogo y el epílogo comunes de 0x5208a0): avanza la
// velocidad, la frena por 0,9f, cae +0xe4, la acelera por 1,1f y la caída baja 3. Con el terreno (0x5f93c0, sin
// redondear) estrictamente por encima de la z, la z en él, la caída por −0,2f, cinco vidas menos y la x de los ángulos
// menos la escala · 128; si no (también con NaN), menos la escala · 16. El alpha, la vida · 0,1f. Sin rand().
[[nodiscard]] Core::Expected<void, std::string> StepSkinShell(ModelEffect& effect,
                                                              const ModelEffectStepServices& services);
} // namespace Client::World
