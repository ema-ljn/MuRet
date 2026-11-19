#pragma once

#include "Core/Expected.h"
#include "Scenes/GameDrawTail.h"
#include "Scenes/JointKernels.h"
#include "World/IFishStepServices.h"

#include <cstdint>
#include <string>

namespace Client::Render::Models
{
class CrtRandom;
} // namespace Client::Render::Models

// Las familias de joints que viven en su propio archivo. FaunaJointCreation.cpp llama a su alta después del primer
// tramo (0x530afa) y a su paso después del movimiento común (0x53590e); el paso vuelve a la cola común (0x53c393).
namespace Client::Scenes::JointFamilies
{
// El 0x614 (LightningJoints.cpp): alta 0x5309d6 y paso 0x53683e..0x537278. Las /0xa y /0xe tienen alta propia en
// FaunaJointCreation.cpp; la /0xc y la /0xf cortan en el alta.
inline constexpr std::int32_t LightningJoint = 0x614;
[[nodiscard]] Core::Expected<void, std::string> CreateLightningJoint(JointState& joint,
                                                                     const World::FishEffectRequest& request,
                                                                     Render::Models::CrtRandom& random,
                                                                     const JointKernels::CreationServices* services);
[[nodiscard]] Core::Expected<void, std::string> StepLightningJoint(JointState& joint,
                                                                   const JointKernels::StepServices& services);

// El 0x61b y el 0x5bc (HandBoltJoints.cpp): alta 0x5335cf/0x5335d6 y paso 0x539ccc..0x539f16.
inline constexpr std::int32_t RedHandBoltJoint = 0x61b;
inline constexpr std::int32_t BlueHandBoltJoint = 0x5bc;
void CreateHandBoltJoint(JointState& joint);
[[nodiscard]] Core::Expected<void, std::string> StepHandBoltJoint(JointState& joint,
                                                                  const JointKernels::StepServices& services);
} // namespace Client::Scenes::JointFamilies
