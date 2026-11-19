#pragma once

#include "Core/Expected.h"
#include "World/Particles.h"

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
// 0x5ac de Icarus, 9 la del efecto 0xb3 de Aida (0x520d04), 5 la de la calavera 0x628/5 (0x52b843) y del empujón de
// la skill 0x28 (0x4fbd3a), y 8 la del 0x61e/6 (0x52baf5).
inline constexpr std::int32_t KalimaFlare = 0x5ac;
inline constexpr std::int32_t KalimaFanta = 0x6ed;
// Effect\fantaF.jpg: el 0x39 y el 0x47 de Crywolf crean 6ec/5 (0x430825 y 0x430890), y el 0 y el 0x35 de Castle
// Siege, 6ec/0 (0x42cb52). 0x53fe70 (0x543ae3), 0x5446e0 (0x547a40) y 0x548550 (al default, 0x54889a) tratan igual
// el 0x6ec y el 0x6ed; la /5 nace con 20 de vida (0x543bd5) y ninguna de las dos mira el dueño en el paso (solo la 1
// a la 4).
inline constexpr std::int32_t CrywolfFanta = 0x6ec;
inline constexpr std::int32_t ShortFantaSubtype = 5;
// 0x543be5: la /6 (la de 0x428adc y 0x428c52) vive 32. 0x543aed: la /7 nace corrida y más grande, y su paso (0x547cc2)
// se achica 0,1f y sube 2 más. La /1..4 nacen como la /0 y, con dueño (CreateOwnedChecked), leen su acción (la /1 y la
// /2, 0x547a4d) o la mano del Monster82 (la /3 del 0x1a4 y del 0x1a9 y la /4 del 0x1a4, 0x547a73); los demás subtipos
// son la /0.
inline constexpr std::int32_t LongFantaSubtype = 6;
inline constexpr std::int32_t SwirlFantaSubtype = 7;
inline constexpr std::int32_t LastOwnedFantaSubtype = 4;
inline constexpr std::int32_t KalimaWaterfallFirst = 0x6f0;
inline constexpr std::int32_t KalimaWaterfallMist = 0x6f1;
// 0x6f2 y 0x6f3: las salpicaduras de las cascadas (el 0x39 del 54 crea 6f2/4).
inline constexpr std::int32_t WaterfallSplashFirst = 0x6f2;
inline constexpr std::int32_t KalimaWaterfallLast = 0x6f4;
// 0x6f4/6, la caída de los braseros 0x34 de Castle Siege (0x42caf4): nace en 0x543f04 y su paso (0x547fa4) solo toca
// la luz.
inline constexpr std::int32_t BrazierFallSubtype = 6;
// Las del joint 0x613/0xe y 0x6ee/0xe de Kundun: la caída 6f4/3 (0x521aa3 y 0x539120, nace en 0x543dc0 y su paso,
// 0x547f4c, gira 4 además de la caída de la 6f4), y las salpicaduras 6f2/2, 6f3/2 (0x5390e6 y 0x539109) y 6f4/4, que
// nacen con la subida del ángulo x (0x544295 y 0x543e14) y avanzan 20 hacia su ángulo z (0x5480fd y 0x547f6b). Todas
// van al billboard común (0x54889a).
inline constexpr std::int32_t KundunFallSubtype = 3;
// La 6f4/2, las cien del golpe de un pedazo de Kundun contra el suelo (0x52a37d): nace en 0x543d68 con vida 20, el giro
// rand() % 360, la escala 0,5, 50 más arriba y la subida rand() % 5 + 10 (dos rand(), con la luz del pedido), y su paso
// (0x547ee2) crece 0,1f hasta 1, la corre rand() % 10 − 5 en x y en y, le resta 1 a la subida y se apaga como la 6f4.
inline constexpr std::int32_t KundunDebrisSubtype = 2;
inline constexpr std::int32_t KundunSplashSubtype = 2;
inline constexpr std::int32_t KundunSplashLastSubtype = 4;
[[nodiscard]] bool IsKalimaParticle(std::int32_t type, std::int32_t subtype);
// Después del prefijo53fe70.6f1/0 consultaScene tras3CRT y antes de almacenar vz;
// su ausencia conserva Initialize/vida/rotación/escala y devuelve MissingScene, sin jitter. La 6f1/1 pisa esa luz
// (0x544057) y nace sin escena.
[[nodiscard]] Core::Expected<void, ParticleError>
CreateKalimaParticle(Particle& particle, Render::Models::CrtRandom& random, std::optional<std::int32_t> scene);
// Después de vida/movimiento5446e0, incluso en el tick que apaga alive. `wind` es el viento de este paso
// (0x7609bb8/bbc), ya movido; sólo lo lee el 5ac/0. `ownerStates` lo lee la fanta 6ec/1..4 con dueño: sin él,
// MissingOwner.
[[nodiscard]] Core::Expected<void, ParticleError>
StepKalimaParticle(Particle& particle, Render::Models::CrtRandom& random, const float* clock,
                   IParticleTrigonometry* trigonometry, const glm::vec2& wind,
                   const ParticleOwnerStates& ownerStates = {});
} // namespace Client::World
