#pragma once

#include "Core/Expected.h"

#include <cstdint>

namespace Client::Render::Models
{
class CrtRandom;
}
namespace Client::World
{
struct Particle;
enum class ParticleError : std::uint8_t;

// Las dos partículas que crean los objetos de Land of Trials (el mapa 31) en 0x4401e0: la salpicadura 6f2/3
// del 3 (Effect\WATERFALL3.jpg, 0x440378) y el humo 5f2/22 del 0x35 (Effect\smoke01.jpg, 0x44023a). La 6f3/3
// comparte las instrucciones de la 6f2/3 en 0x53fe70 y en 0x5446e0. Las dos van al billboard común.
inline constexpr std::int32_t TrialsSplashSubtype = 3;
inline constexpr std::int32_t TrialsSmokeSubtype = 22;
[[nodiscard]] bool IsTrialsParticle(std::int32_t type, std::int32_t subtype);

// 0x544319 (6f2/3): vida 60, velocidad z −1, escala (rand() % 10 + 15) · escala · 0,02f, giro rand() % 360 y
// la x y la z corridas rand() % 40 − 20 y rand() % 25 − 10. 0x5423e0 (5f2/22): vida 60, escala
// ((rand() & 63) + 64) · escala · 0,005f, giro rand() % 360 y velocidad vertical ((rand() & 31) + 60) · 0,1f.
void CreateTrialsParticle(Particle& particle, Render::Models::CrtRandom& random);

// Después de la vida y el movimiento comunes de 0x5446e0. 0x54815d (6f2/3): la escala +0,005f, la velocidad
// z −0,05f y la luz por 0,97f. 0x546706 (5f2/22): la luz (vida · 0,02f) por (0,9; 0,5; 0,5), la velocidad
// vertical −0,1f, la x más sin(reloj) por esa velocidad sin redondear por 0,05f, la z más la velocidad y la
// escala +0,04f. Sin el reloj del mundo (0x5767b88), o con un seno fuera del dominio portado, el 5f2/22
// corta antes de tocar la x.
[[nodiscard]] Core::Expected<void, ParticleError> StepTrialsParticle(Particle& particle, const float* clock);
} // namespace Client::World
