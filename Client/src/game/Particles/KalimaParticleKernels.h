#pragma once

#include "Core/Expected.h"
#include "Particles/Particles.h"

#include <glm/ext/vector_float2.hpp>

#include <cstdint>
#include <optional>

namespace Client::Crt
{
class CrtRandom;
}
namespace Client::Particles
{
struct Particle;
class IParticleTrigonometry;
enum class ParticleError : std::uint8_t;
// The 0x5ac (Effect\flare01.jpg): 6 and 7 of Kalima, 0 of Icarus's 10 (0x604143), 1 the trail of Icarus's 0x5ac
// effect, 9 that of Aida's 0xb3 effect (0x520d04), 5 that of the skull 0x628/5 (0x52b843) and of the push of skill
// 0x28 (0x4fbd3a), and 8 that of the 0x61e/6 (0x52baf5).
inline constexpr std::int32_t KalimaFlare = 0x5ac;
inline constexpr std::int32_t KalimaFanta = 0x6ed;
// Effect\fantaF.jpg: Crywolf's 0x39 and 0x47 create 6ec/5 (0x430825 and 0x430890), and Castle Siege's 0 and 0x35,
// 6ec/0 (0x42cb52). 0x53fe70 (0x543ae3), 0x5446e0 (0x547a40) and 0x548550 (to the default, 0x54889a) treat the 0x6ec
// and the 0x6ed alike; the /5 is born with 20 life (0x543bd5) and neither of the two checks the owner in the step (only
// 1 to 4).
inline constexpr std::int32_t CrywolfFanta = 0x6ec;
inline constexpr std::int32_t ShortFantaSubtype = 5;
// 0x543be5: the /6 (that of 0x428adc and 0x428c52) lives 32. 0x543aed: the /7 is born shifted and bigger, and its step
// (0x547cc2) shrinks 0.1f and rises 2 more. The /1..4 are born like the /0 and, with an owner (CreateOwnedChecked),
// read its action (the /1 and the /2, 0x547a4d) or Monster82's hand (the /3 of the 0x1a4 and the 0x1a9 and the /4 of
// the 0x1a4, 0x547a73); the other subtypes are the /0.
inline constexpr std::int32_t LongFantaSubtype = 6;
inline constexpr std::int32_t SwirlFantaSubtype = 7;
inline constexpr std::int32_t LastOwnedFantaSubtype = 4;
inline constexpr std::int32_t KalimaWaterfallFirst = 0x6f0;
inline constexpr std::int32_t KalimaWaterfallMist = 0x6f1;
// 0x6f2 and 0x6f3: the waterfalls' splashes (the 0x39 of 54 creates 6f2/4).
inline constexpr std::int32_t WaterfallSplashFirst = 0x6f2;
inline constexpr std::int32_t KalimaWaterfallLast = 0x6f4;
// 0x6f4/6, the fall of Castle Siege's 0x34 braziers (0x42caf4): born at 0x543f04, and its step (0x547fa4) only touches
// the light.
inline constexpr std::int32_t BrazierFallSubtype = 6;
// Those of Kundun's joints 0x613/0xe and 0x6ee/0xe: the fall 6f4/3 (0x521aa3 and 0x539120, born at 0x543dc0, and its
// step, 0x547f4c, turns 4 besides the 6f4's fall), and the splashes 6f2/2, 6f3/2 (0x5390e6 and 0x539109) and 6f4/4,
// born with the x angle's rise (0x544295 and 0x543e14) and advancing 20 toward their z angle (0x5480fd and 0x547f6b).
// All go to the common billboard (0x54889a).
inline constexpr std::int32_t KundunFallSubtype = 3;
// The 6f4/2, the hundred from a piece of Kundun hitting the ground (0x52a37d): born at 0x543d68 with life 20, the turn
// rand() % 360, the scale 0.5, 50 higher and the rise rand() % 5 + 10 (two rand(), with the requested light), and its
// step (0x547ee2) grows 0.1f up to 1, shifts it rand() % 10 − 5 in x and y, subtracts 1 from the rise and fades like
// the 6f4.
inline constexpr std::int32_t KundunDebrisSubtype = 2;
inline constexpr std::int32_t KundunSplashSubtype = 2;
inline constexpr std::int32_t KundunSplashLastSubtype = 4;
[[nodiscard]] bool IsKalimaParticle(std::int32_t type, std::int32_t subtype);
// After the prefix of 0x53fe70. The 6f1/0 queries Scene after 3 CRT and before storing vz; without it, it keeps
// Initialize/life/rotation/scale and returns MissingScene, without jitter. The 6f1/1 overrides that light (0x544057)
// and is born without a scene.
[[nodiscard]] Core::Expected<void, ParticleError> CreateKalimaParticle(Particle& particle, Crt::CrtRandom& random,
                                                                       std::optional<std::int32_t> scene);
// After the life/movement of 0x5446e0, even on the tick that turns alive off. `wind` is this step's wind
// (0x7609bb8/bbc), already moved; only the 5ac/0 reads it. `ownerStates` is read by the fanta 6ec/1..4 with an owner:
// without it, MissingOwner.
[[nodiscard]] Core::Expected<void, ParticleError>
StepKalimaParticle(Particle& particle, Crt::CrtRandom& random, const float* clock, IParticleTrigonometry* trigonometry,
                   const glm::vec2& wind, const ParticleOwnerStates& ownerStates = {});
} // namespace Client::Particles
