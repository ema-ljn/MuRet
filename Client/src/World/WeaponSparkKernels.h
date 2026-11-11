#pragma once

#include <cstdint>

namespace Client::Render::Models
{
class CrtRandom;
}
namespace Client::World
{
struct Particle;

// La chispa 0x5c6/0xb (Spark03) que sueltan el arco 0xa27 (0x50af72) y el bastón 0xc1e (0x50b5d8) en el dibujo de
// lo que cuelga. La 0x5c6/0xd comparte sus instrucciones en 0x53fe70 y en 0x5446e0, pero nadie la pide en MuRet. Va
// al billboard común, como todas las 0x5c6.
inline constexpr std::int32_t WeaponSparkSubtype = 11;
[[nodiscard]] bool IsWeaponSpark(std::int32_t type, std::int32_t subtype);

// 0x540977..0x5409fc, después del prefijo: +0x38 en 1,0f, la velocidad ((rand() & 7) − 4; rand() & 3; rand() & 3),
// la vida 30 entre la y y la z, y la escala (rand() % 5) · 0,1f + 1, que pisa la del argumento.
void CreateWeaponSpark(Particle& particle, Render::Models::CrtRandom& random);

// 0x545368..0x54535a, después de la vida y el movimiento comunes de 0x5446e0: la luz por 0,952381f (0x6ba264), la
// escala −0,04f y +0x38 −0,001f. Muere cuando la resta, sin redondear a float, no es mayor que 0 (fst y fcomp, Q-C360).
void StepWeaponSpark(Particle& particle);
} // namespace Client::World
