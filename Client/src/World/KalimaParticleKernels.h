#pragma once

#include "Core/Expected.h"

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
inline constexpr std::int32_t KalimaFlare = 0x5ac;
inline constexpr std::int32_t KalimaFanta = 0x6ed;
inline constexpr std::int32_t KalimaWaterfallFirst = 0x6f0;
inline constexpr std::int32_t KalimaWaterfallMist = 0x6f1;
inline constexpr std::int32_t KalimaWaterfallLast = 0x6f4;
[[nodiscard]] bool IsKalimaParticle(std::int32_t type, std::int32_t subtype);
// Después del prefijo53fe70.6f1/0 consultaScene tras3CRT y antes de almacenar vz;
// su ausencia conserva Initialize/vida/rotación/escala y devuelve MissingScene, sin jitter.
[[nodiscard]] Core::Expected<void, ParticleError>
CreateKalimaParticle(Particle& particle, Render::Models::CrtRandom& random, std::optional<std::int32_t> scene);
// Después de vida/movimiento5446e0, incluso en el tick que apaga alive.
[[nodiscard]] Core::Expected<void, ParticleError> StepKalimaParticle(Particle& particle,
                                                                     Render::Models::CrtRandom& random,
                                                                     const float* clock,
                                                                     IParticleTrigonometry* trigonometry);
} // namespace Client::World
