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

// El 0x613 y el 0x6ee (SpiritJoints.cpp): alta 0x53265e (la mezcla en 3 y la tabla 0x534f18) y paso 0x538d77. La /4
// (y la 6ee/9) tienen alta y paso propios en FaunaJointCreation.cpp; la 6ee/4 y la 613/9 cortan en el alta.
inline constexpr std::int32_t SpiritJoint = 0x613;
inline constexpr std::int32_t SecondSpiritJoint = 0x6ee;
[[nodiscard]] Core::Expected<void, std::string> CreateSpiritJoint(JointState& joint,
                                                                  const World::FishEffectRequest& request,
                                                                  Render::Models::CrtRandom& random,
                                                                  const JointKernels::CreationServices* services);
[[nodiscard]] Core::Expected<void, std::string> StepSpiritJoint(JointState& joint,
                                                                const JointKernels::StepServices& services);

// El 0x60f/2 y el 0x60f/0x18 (DropJoints.cpp), la gota que el 0x613/2 suelta en su segundo paso (0x5396ff): alta
// 0x533677 y 0x5340dc, y paso 0x53b156.
inline constexpr std::int32_t DropJoint = 0x60f;
inline constexpr std::int32_t DropSubtype = 2;
inline constexpr std::int32_t DarkDropSubtype = 0x18;
void CreateDropJoint(JointState& joint, const World::FishEffectRequest& request, Render::Models::CrtRandom& random);
void StepDropJoint(JointState& joint);

// El 0x10f con un personaje (ActorState) de dueño (AuraJoints.cpp): alta 0x530edd..0x531214 y paso 0x535d4f..0x5366a7.
// La /1 (el hada) y la /3 (Blood Castle) llevan un FaunaObject y siguen en FaunaJointCreation.cpp. Las /5..7 no hacen
// el primer tramo (0x530a5c): FaunaJointCreation.cpp las llama antes.
inline constexpr std::int32_t AuraJoint = 0x10f;
[[nodiscard]] Core::Expected<void, std::string> CreateAuraJoint(JointState& joint,
                                                                const World::FishEffectRequest& request);
[[nodiscard]] Core::Expected<void, std::string> StepAuraJoint(JointState& joint,
                                                              const JointKernels::StepServices& services);

// El 0x632/7 y sus tres 0x632/6 (StormJoints.cpp): alta 0x534b27 y 0x534a16 y paso 0x53bec2. La /6 salta el primer
// tramo (0x530a5c); FaunaJointCreation.cpp crea los tres /6 al final del alta de la /7 (0x534b3f..0x534bc0).
inline constexpr std::int32_t StormJoint = 0x632;
inline constexpr std::int32_t BoltSubtype = 6;
inline constexpr std::int32_t StormSubtype = 7;
[[nodiscard]] Core::Expected<void, std::string>
CreateStormJoint(JointState& joint, const World::FishEffectRequest& request, Render::Models::CrtRandom& random);
[[nodiscard]] Core::Expected<void, std::string> StepStormJoint(JointState& joint,
                                                               const JointKernels::StepServices& services);
} // namespace Client::Scenes::JointFamilies
