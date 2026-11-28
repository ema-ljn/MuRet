#pragma once

#include "Core/Expected.h"
#include "Models/Skeleton.h"

#include <glm/ext/vector_float2.hpp>

#include <cstdint>
#include <string>

namespace Client::Crt
{
class CrtRandom;
} // namespace Client::Crt

namespace Client::Kernel
{
struct ActorState;
struct HelperState;
} // namespace Client::Kernel

// The 0x444010 bodies of the pets other than the Dark Horse: the Uniria (0xc6), the Dinorant (0x110), the Fenrir
// (0x14a..0x14c) and the Guardian Angel (0x1c12). They run after the 0x4e9980 alpha and before the 0x445006 advance,
// which Pets/HelperTable.cpp adds. Contract: Client/docs/research/06-render-escenas-y-ui.md, "Las mascotas".
namespace Client::Pets
{
struct HelperStepContext;

inline constexpr std::uint16_t UniriaModel = 0xc6;
inline constexpr std::uint16_t DinorantModel = 0x110;
inline constexpr std::uint16_t FirstFenrirModel = 0x14a;
inline constexpr std::uint16_t LastFenrirModel = 0x14c;
inline constexpr std::uint16_t GuardianAngelModel = 0x1c12;

// 0x4447c4..0x444ac4, the Uniria and the Dinorant: on the rider's safe cell, alpha to 0. Otherwise the alpha with the
// rider's +0x80, its position (the Dinorant at −10 on maps 8 and 10 and at −30 on the others, except −1) and angles,
// the action for the rider's (0x4ea030), with the gallop the dust (rand() % 2, outside map 10 and without the rider's
// 0x4000), speed 0.34 and the rider's live byte.
[[nodiscard]] Core::Expected<void, std::string> StepRidingHelper(Kernel::HelperState& helper,
                                                                 const Kernel::ActorState& rider,
                                                                 const Models::Skeleton& skeleton,
                                                                 const HelperStepContext& context);

// 0x444af0..0x444f25, the Fenrir: the same safe cell; the alpha with the rider's +0x80, its head, position and angles,
// the action and speed for the rider's, and with the run or the trot the dust: on map 10, from bone 22 of the global
// buffer with the frame between 1 and 1.2, or from 44 (and 700 higher, Q-C421) between 4.8 and 5, the 0x6eb/1 wave and
// the 5f2; on the others, rand() % 3, outside Kalima and without the rider's 0x4000.
[[nodiscard]] Core::Expected<void, std::string> StepFenrir(Kernel::HelperState& helper, const Kernel::ActorState& rider,
                                                           const Models::Skeleton& skeleton,
                                                           const HelperStepContext& context);

// 0x444f2a..0x444fff, the Guardian Angel: without the rider's 0x4000, four 5c5/1 with light 0.4 at (rand() & 15) − 8
// from the helper on each axis.
[[nodiscard]] Core::Expected<void, std::string> SparkleTheGuardianAngel(const Kernel::HelperState& helper,
                                                                        const Kernel::ActorState& rider,
                                                                        const HelperStepContext& context);

// 0x445052..0x445221, after the advance: the Guardian Angel turns 20 degrees toward where the rider was at the start of
// the step (`riderStart`, 0x444080..0x44408b) if it is 150 or more away, moves with its speed rotated by its angles and
// (rand() & 15) − 8 in z, with rand() % 32 == 0 takes another speed (and another course if near), and raises or lowers
// its z speed by 1.5 to stay between 100 and 200 above the rider.
void FlyTheGuardianAngel(Kernel::HelperState& helper, const Kernel::ActorState& rider, glm::vec2 riderStart,
                         Crt::CrtRandom& random);
} // namespace Client::Pets
