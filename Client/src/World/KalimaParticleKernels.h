#pragma once

#include "Core/Expected.h"

#include <glm/ext/vector_float2.hpp>

#include <cstdint>
#include <optional>

namespace Client::Render::Models
{
class CrtRandom;
}
namespace Client::World
{
struct Particle;
class IParticleTrigonometry;
enum class ParticleError : std::uint8_t;
// El 0x5ac (Effect\flare01.jpg): 6 y 7 de Kalima, 0 del 10 de Icarus (0x604143), 1 la estela del efecto
// 0x5ac de Icarus y 9 la del efecto 0xb3 de Aida (0x520d04).
inline constexpr std::int32_t KalimaFlare = 0x5ac;
inline constexpr std::int32_t KalimaFanta = 0x6ed;
// Effect\fantaF.jpg: el 0x39 y el 0x47 de Crywolf crean 6ec/5 (0x430825 y 0x430890), y el 0 y el 0x35 de Castle
// Siege, 6ec/0 (0x42cb52). 0x53fe70 (0x543ae3), 0x5446e0 (0x547a40) y 0x548550 (al default, 0x54889a) tratan igual
// el 0x6ec y el 0x6ed; la /5 nace con 20 de vida (0x543bd5) y ninguna de las dos mira el dueño en el paso (solo la 1
// a la 4).
inline constexpr std::int32_t CrywolfFanta = 0x6ec;
inline constexpr std::int32_t ShortFantaSubtype = 5;
inline constexpr std::int32_t KalimaWaterfallFirst = 0x6f0;
inline constexpr std::int32_t KalimaWaterfallMist = 0x6f1;
// 0x6f2 y 0x6f3: las salpicaduras de las cascadas (el 0x39 del 54 crea 6f2/4).
inline constexpr std::int32_t WaterfallSplashFirst = 0x6f2;
inline constexpr std::int32_t KalimaWaterfallLast = 0x6f4;
// 0x6f4/6, la caída de los braseros 0x34 de Castle Siege (0x42caf4): nace en 0x543f04 y su paso (0x547fa4) solo toca
// la luz.
inline constexpr std::int32_t BrazierFallSubtype = 6;
[[nodiscard]] bool IsKalimaParticle(std::int32_t type, std::int32_t subtype);
// Después del prefijo53fe70.6f1/0 consultaScene tras3CRT y antes de almacenar vz;
// su ausencia conserva Initialize/vida/rotación/escala y devuelve MissingScene, sin jitter.
[[nodiscard]] Core::Expected<void, ParticleError>
CreateKalimaParticle(Particle& particle, Render::Models::CrtRandom& random, std::optional<std::int32_t> scene);
// Después de vida/movimiento5446e0, incluso en el tick que apaga alive. `wind` es el viento de este paso
// (0x7609bb8/bbc), ya movido; sólo lo lee el 5ac/0.
[[nodiscard]] Core::Expected<void, ParticleError>
StepKalimaParticle(Particle& particle, Render::Models::CrtRandom& random, const float* clock,
                   IParticleTrigonometry* trigonometry, const glm::vec2& wind);
} // namespace Client::World
