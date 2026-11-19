#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

// El alta del calco de colores 0x61e (Effect\Magic_Ground2), que piden las skills y el pase del suelo: 06, "Los efectos
// con modelo de los tipos altos".
namespace Client::Render::Models
{
class CrtRandom;
}

namespace Client::World
{
struct ModelEffect;
struct ModelEffectArguments;

// Los subtipos del 0x61e con caso propio en el alta (0x51eebb), además de los de ModelEffects.h.
inline constexpr std::int32_t PulsingDecalSubtype = 4; // el de la 0x30, entre los joints 0x613/2 (0x4fe127)
inline constexpr std::int32_t BindingDecalSubtype = 6; // el de la 0x44 y la 0x47, sobre el blanco (0x4fe424)
inline constexpr std::int32_t BindingChildSubtype = 7; // el que el dibujo del /6 suelta cada 25 vidas (0x52e04c)
inline constexpr std::int32_t SpawningDecalSubtype = 8;
inline constexpr std::int32_t GrowingDecalSubtype = 9; // el que el dibujo del /8 suelta en las vidas pares (0x52e0a2)
inline constexpr std::int32_t CoolPulsingDecalSubtype = 0xa; // el de la 0x45, entre los joints 0x613/0x15 (0x4fe360)

// 0x51eebb..0x51efa4, el caso del 0x61e después del prefijo: vida 20; el /4 y el /0xa, vida 40 y la escala
// (rand() % 50 + 50) · 0,04f; el /6, vida 60, esa escala y la partida en la posición; el /7, vida 40 y el ángulo z en
// rand() % 360; el /8, vida 20; el /9, vida 10 y la escala en 0,1f. Los demás, solo la vida.
void BeginColoredDecal(ModelEffect& effect, Render::Models::CrtRandom& random);

// 0x518cf0(0x61e, …) sobre `table`: el prefijo (BeginModelEffect) y BeginColoredDecal. Con la tabla llena, nada y sin
// rand().
[[nodiscard]] std::optional<std::size_t> CreateColoredDecal(std::span<ModelEffect> table,
                                                            const ModelEffectArguments& arguments,
                                                            Render::Models::CrtRandom& random);
} // namespace Client::World
