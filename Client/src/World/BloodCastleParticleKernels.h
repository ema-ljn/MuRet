#pragma once

#include <cstdint>

namespace Client::Render::Models
{
class CrtRandom;
}
namespace Client::World
{
struct Particle;

// Las partículas de la antorcha 0x25 de Blood Castle (0x604171..0x6042ac): la chispa 6ab/0 y 6ab/1
// (Effect\fi01.jpg, 0x61d55e), la llama 6ac/0 (Effect\fi02.tga, 0x61d57a) y el humo 622/6 (Effect\clouds.jpg).
// La órbita 60f/4 de la misma antorcha va con la 60f/3, en Particles.cpp. Contrato: 06 § Los objetos de Devil
// Square y de Blood Castle.
inline constexpr std::int32_t TorchSparkParticle = 0x6ab;
inline constexpr std::int32_t TorchFlameParticle = 0x6ac;
// La 6ac/2 (0x53c042, en el paso de los joints 0x535840, del joint 0x632/6): se dibuja con GL_ADD (0x5489eb).
inline constexpr std::int32_t BrightFlameSubtype = 2;
inline constexpr std::int32_t TorchSmokeSubtype = 6;
[[nodiscard]] bool IsBloodCastleParticle(std::int32_t type, std::int32_t subtype);

// 0x5436fe (6ab): vida rand() % 5 + 20 y giro 0; con el subtipo 0, la escala (rand() % 10) · 0,02f + 0,5 y la
// velocidad ((rand() % 10 + 5) · 0,4f; (rand() % 10 − 5) · 0,4f; (rand() % 10 + 5) · 0,2f); con otro, la escala
// (rand() % 10) · 0,1f + 1 y la velocidad con 0,2f, 0,2f y 0,1f.
// 0x543a5d (6ac/0 y 6ac/2): vida rand() % 5 + 25, escala 0,5, giro rand() % 360 y velocidad ((rand() % 10 + 5) · 0,4f;
// 0; (rand() % 10 + 5) · 0,2f); después cae en el caso del 6ec (0x543ae3), que deja la vida en 24 y vuelve a
// tirar la x ((rand() % 10 − 5) · 0,4f) y la z ((rand() % 10 + 5) · 0,2f); el inicio (+0x6c) queda en la
// posición. 0x54327b (622/6): vida rand() % 5 + 25, escala 0,2f, giro rand() % 360 y la velocidad de la 6ac
// antes del 6ec. Cada producto de la x87 se guarda en float.
void CreateBloodCastleParticle(Particle& particle, Render::Models::CrtRandom& random);

// Después de la vida y el movimiento comunes de 0x5446e0, las tres suman la velocidad a la posición y frenan la x
// y la y por 0,95f. 0x5477e4 (6ab): la luz en vida · 0,1f, la velocidad z +0,3f y la escala +0,07f (subtipo 0)
// o +0,09f. 0x54793f (6ac) y 0x54748a (622/6): la velocidad z +0,6f, la posición corrida (rand() & 3) − 2 en x
// e y y ((rand() & 3) − 2) · 0,8f en z, la escala +0,05f, la luz en vida · 0,04f (6ac/0), vida · 0,08f − 1
// (6ac/2) o vida · 0,02f (622/6), y el giro + (rand() & 1) + 1.
void StepBloodCastleParticle(Particle& particle, Render::Models::CrtRandom& random);
} // namespace Client::World
