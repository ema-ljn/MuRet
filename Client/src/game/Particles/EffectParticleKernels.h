#pragma once

#include "Core/Expected.h"
#include "Particles/Particles.h"

#include <glm/ext/vector_float3.hpp>

#include <cstdint>

namespace Client::Crt
{
class CrtRandom;
}

namespace Client::World
{
class IParticleTrigonometry;

// Las partículas de los efectos de la CL11 que 0x53fe70 y 0x5446e0 tratan en casos propios: el anillo 0x600
// (Effect\ring.jpg), el humo 5f2/1 y 5f2/3 (Effect\smoke01.jpg; los demás humos de la CL11, en SmokeParticleKernels),
// la llama 5de/1..6 (Effect\Flame01.jpg; la /0 y la /7 son de la selección) y las chispas 5c6 que no tienen kernel
// propio (Effect\Spark03.jpg). Todas van al billboard común de 548550 (0x54889a; 5f2 pasando por 0x54874c).
inline constexpr std::int32_t RingParticle = 0x600;
// 0x543183: la /1 (la del 0x43a861 de Kalima, sobre el blanco) vive 15 y gira al azar; las demás viven 20.
inline constexpr std::int32_t RingPulseSubtype = 0;
inline constexpr std::int32_t RingBurstSubtype = 1;
// 0x541eb6: el humo del veneno (los diez de la skill 1, 0x50497e) y 0x541cf2, el de los efectos 0x5216d6 y 0x521960.
inline constexpr std::int32_t PoisonSmokeSubtype = 1;
inline constexpr std::int32_t EffectSmokeSubtype = 3;
// 0x5417ba: la 5de/4 sigue a su dueño (+0x40) desde la posición que tenía al nacer.
inline constexpr std::int32_t FollowingFlameSubtype = 4;
// Las chispas 5c6 (tabla de 0x5443c0 en el alta y de 0x548204 en el paso): la /0 del golpe (0x52e740), la /3 de las
// luces del 0x45 (0x50c0db), la /4 que va hacia su dueño y lo sigue, la /6 que sólo nace una de cada 50 veces, la /7
// verde, la /8 (la /0 con vida 10) y la /10, que se apaga con +0x38.
inline constexpr std::int32_t HitSparkSubtype = 0;
inline constexpr std::int32_t ShrinkingSparkSubtype = 3;
inline constexpr std::int32_t FollowingSparkSubtype = 4;
inline constexpr std::int32_t RareSparkSubtype = 6;
inline constexpr std::int32_t GreenSparkSubtype = 7;
inline constexpr std::int32_t ShortHitSparkSubtype = 8;
inline constexpr std::int32_t BurstSparkSubtype = 10;

[[nodiscard]] bool IsEffectParticle(std::int32_t type, std::int32_t subtype);
// Las que leen +0x40 en su paso (5c6/4, 5d5/2, el fuego /9, 5de/4, 5f2/36, 5c5/5 y 5c5/6): 0x53fe70 guarda el dueño
// en el lugar.
[[nodiscard]] bool FollowsItsOwner(std::int32_t type, std::int32_t subtype);
// Las que además leen la posición del dueño al nacer: 5c6/4 (0x540686), 5de/4 (0x5417ba) y 5c5/5 (0x5403ad).
[[nodiscard]] bool NeedsOwnerPosition(std::int32_t type, std::int32_t subtype);
// Las que leen la altura del terreno al nacer (0x5f93c0): la niebla 5f2/28, /29 y /30 (0x5425f9, 0x54264d, 0x54269a).
[[nodiscard]] bool NeedsTerrainHeight(std::int32_t type, std::int32_t subtype);

// Lo que el alta lee además del lugar: el reloj del mundo (0x5767b88) y su seno para la luz de 5de/2..4 y la niebla
// 5f2/30, la posición del dueño de 5c6/4 y 5de/4, el reloj por valor de los que no lo prestan (los humos de
// SmokeParticleKernels lo usan sin `worldClock`) y la altura del terreno de la niebla 5f2/28..30.
struct EffectParticleBirth
{
    const float* worldClock = nullptr;
    IParticleTrigonometry* trigonometry = nullptr;
    const glm::vec3* ownerPosition = nullptr;
    float legacyClock = 0.0f;
    const ParticleTerrainHeight* terrainHeight = nullptr;
};

// Después del prefijo de 0x53fe70 (Initialize). Sin el reloj o la trigonometría que pide la 5de/2..4 y la 5f2/30,
// MissingClock con los rand() ya sacados; sin la posición del dueño, MissingOwner, y sin la altura del terreno de la
// 5f2/28..30, MissingTerrainHeight (Particles las pide antes de tocar el lugar).
[[nodiscard]] Core::Expected<void, ParticleError> CreateEffectParticle(Particle& particle, Crt::CrtRandom& random,
                                                                       const EffectParticleBirth& birth);
// Después de la vida y el movimiento comunes de 0x5446e0. El anillo pide la trigonometría (fsin); 5c6/4 y 5de/4, el
// estado de su dueño.
[[nodiscard]] Core::Expected<void, ParticleError> StepEffectParticle(Particle& particle, Crt::CrtRandom& random,
                                                                     IParticleTrigonometry* trigonometry,
                                                                     const ParticleOwnerStates& ownerStates);

} // namespace Client::World
