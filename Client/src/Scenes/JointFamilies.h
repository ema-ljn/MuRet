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
// 0x533677 y 0x5340dc, y paso 0x53b156. La /0x13, que baja (alta 0x533a28 y paso 0x53a12c), la piden las columnas del
// círculo 0xc3 con el 0x62b (0x5236ce y 0x523898). El 0x62b comparte el alta y el paso.
inline constexpr std::int32_t DropJoint = 0x60f;
inline constexpr std::int32_t DropSubtype = 2;
inline constexpr std::int32_t DarkDropSubtype = 0x18;
inline constexpr std::int32_t SinkingDropSubtype = 0x13;
[[nodiscard]] bool IsDrop(std::int32_t type, std::int32_t subtype);
void CreateDropJoint(JointState& joint, const World::FishEffectRequest& request, Render::Models::CrtRandom& random);
void StepDropJoint(JointState& joint);

// El 0x60f/7, /0xb y /0x19 (WanderingJoints.cpp): alta 0x533bbd, 0x533d5f y 0x533db8 y paso 0x53a87a..0x53b01f. Vagan
// alrededor de su dueño con el contador de pasos: la /7 de un personaje (0x4f8592), la /0xb y la /0x19 de la flecha
// 0xf8 (JointOwnerKind::ModelEffect, 0x51a405 y 0x51a3b7), que con el skill 1 sigue al joint y choca (0x520170).
inline constexpr std::int32_t WanderSubtype = 7;
inline constexpr std::int32_t ArrowWanderSubtype = 0xb;
inline constexpr std::int32_t PinkWanderSubtype = 0x19;
void CreateWanderingJoint(JointState& joint, const World::FishEffectRequest& request,
                          Render::Models::CrtRandom& random);
[[nodiscard]] Core::Expected<void, std::string> StepWanderingJoint(JointState& joint,
                                                                   const JointKernels::StepServices& services);

// El 0x60f/4 y /0xc y el 0x62b/4 y /0xc (HelixJoints.cpp): alta 0x533faf..0x5340d7 y paso 0x53b08a..0x53b14c. La /4 y
// la /0xc del 0x812 (0x52c9da y 0x52c9a7) llevan el efecto de dueño (JointOwnerKind::ModelEffect); la /4 lee su luz.
// FaunaJointCreation.cpp repite su paso mientras la vida que queda no sea múltiplo de 12 (0x53c3c6).
inline constexpr std::int32_t SecondHelixJoint = 0x62b;
inline constexpr std::int32_t HelixSubtype = 4;
inline constexpr std::int32_t WideHelixSubtype = 0xc;
[[nodiscard]] bool IsHelix(std::int32_t type, std::int32_t subtype);
[[nodiscard]] Core::Expected<void, std::string>
CreateHelixJoint(JointState& joint, const World::FishEffectRequest& request, Render::Models::CrtRandom& random);
[[nodiscard]] Core::Expected<void, std::string> StepHelixJoint(JointState& joint);

// El 0x61c/2..6 (InfernoJoints.cpp): alta 0x534444..0x53463b y paso 0x53b6a0..0x53b7ec. La /4 (la skill 0x49,
// 0x504b33) suelta en cada paso un 0x62b/6 y dos sprites; la /2, el efecto 0xf6/6 cada cinco vidas (0x53b760).
inline constexpr std::int32_t InfernoJoint = 0x61c;
[[nodiscard]] bool IsInferno(std::int32_t type, std::int32_t subtype);
void CreateInfernoJoint(JointState& joint, const World::FishEffectRequest& request);
[[nodiscard]] Core::Expected<void, std::string> StepInfernoJoint(JointState& joint,
                                                                 const JointKernels::StepServices& services);

// El 0x5f2 (FireTrailJoints.cpp): alta 0x531229..0x53128f y 0x532073 y paso 0x5366bd..0x5366ee. La estela de los
// meteoros 0xc2/6 y /8 de los magos (0x51b098 y 0x51b268), con el efecto (World::ModelEffect) de dueño, que sigue
// mientras vive y es 0xc2.
inline constexpr std::int32_t FireTrailJoint = 0x5f2;
void CreateFireTrailJoint(JointState& joint, const World::FishEffectRequest& request);
[[nodiscard]] Core::Expected<void, std::string> StepFireTrailJoint(JointState& joint);

// El 0x10f con un personaje (ActorState) de dueño (AuraJoints.cpp): alta 0x530edd..0x531214 y paso 0x535d4f..0x5366a7.
// La /1 (el hada) y la /3 (Blood Castle) llevan un FaunaObject y siguen en FaunaJointCreation.cpp. Las /5..7 no hacen
// el primer tramo (0x530a5c): FaunaJointCreation.cpp las llama antes.
inline constexpr std::int32_t AuraJoint = 0x10f;
[[nodiscard]] Core::Expected<void, std::string> CreateAuraJoint(JointState& joint,
                                                                const World::FishEffectRequest& request);
[[nodiscard]] Core::Expected<void, std::string> StepAuraJoint(JointState& joint,
                                                              const JointKernels::StepServices& services);

// El 0x618 (BeamJoints.cpp): alta 0x532125..0x5323ce y paso 0x5381f3..0x538704. El dueño (+0x54) es un personaje
// (ActorState) en la /0, /1, /2, /3, /0xc, /0xd, /0x10 y /0x11, el FaunaObject en la /4 (0x447374) y el efecto con
// modelo (JointOwnerKind::ModelEffect) en la /5 de las flechas y la /0xa y /0xb del 0x11b/1; la /6 y la /9 no lo leen.
inline constexpr std::int32_t BeamJoint = 0x618;
[[nodiscard]] Core::Expected<void, std::string>
CreateBeamJoint(JointState& joint, const World::FishEffectRequest& request, Render::Models::CrtRandom& random);
[[nodiscard]] Core::Expected<void, std::string> StepBeamJoint(JointState& joint,
                                                              const JointKernels::StepServices& services);

// El 0x632/7 y sus tres 0x632/6 (StormJoints.cpp): alta 0x534b27 y 0x534a16 y paso 0x53bec2. La /6 salta el primer
// tramo (0x530a5c); FaunaJointCreation.cpp crea los tres /6 al final del alta de la /7 (0x534b3f..0x534bc0). La /5, la
// estrella que cae del pulso 0x61c/0 del asedio (0x52c266, con el efecto de dueño, que no lee): alta 0x534bcb y paso
// 0x53c0a5.
inline constexpr std::int32_t StormJoint = 0x632;
inline constexpr std::int32_t FallingStarSubtype = 5;
inline constexpr std::int32_t BoltSubtype = 6;
inline constexpr std::int32_t StormSubtype = 7;
[[nodiscard]] Core::Expected<void, std::string>
CreateStormJoint(JointState& joint, const World::FishEffectRequest& request, Render::Models::CrtRandom& random);
[[nodiscard]] Core::Expected<void, std::string> StepStormJoint(JointState& joint,
                                                               const JointKernels::StepServices& services);

// El 0x62c (SpiralJoints.cpp): alta 0x5346d8..0x534954 y paso 0x53b93f..0x53be71, sin el movimiento común (0x5358a3).
// La /0 sube derecho; la /1..4 y la /0xb..0xd suben dando la vuelta (el efecto 0x62c/0 de la 0x41, 0x51f25e, y los
// seis de la 0x4c, 0x50293c); la /5..7 (el efecto 0x62c/1, 0x51f32e) siguen un hueso del dueño, que corta.
inline constexpr std::int32_t SpiralJoint = 0x62c;
[[nodiscard]] Core::Expected<void, std::string>
CreateSpiralJoint(JointState& joint, const World::FishEffectRequest& request, Render::Models::CrtRandom& random);
[[nodiscard]] Core::Expected<void, std::string> StepSpiralJoint(JointState& joint,
                                                                const JointKernels::StepServices& services);

// El 0x5ac (FlareJoints.cpp): alta 0x530c93..0x530dd8 y paso 0x5359d7..0x535b32, sin la cola ni el movimiento comunes.
// Los sesenta /0 del 0x114/0 (0x51c1b8) y del combo de BlastModelEffects (0x51c004), sin dueño.
inline constexpr std::int32_t FlareJoint = 0x5ac;
void CreateFlareJoint(JointState& joint, const World::FishEffectRequest& request, Render::Models::CrtRandom& random);
[[nodiscard]] Core::Expected<void, std::string> StepFlareJoint(JointState& joint);

// El 0x60f y el 0x62b /0x10, /0xe y /0xf (OrbitJoints.cpp): alta 0x5337fa y 0x534132 y paso 0x53a5c7 y 0x53b18a. La
// /0x10 es la columna de la lluvia de sombras 0xbd (0x51bcb0), con su personaje de dueño; la /0xe y la /0xf las pide el
// efecto 0x60f (0x52b408 y 0x52b42b, con él de dueño, JointOwnerKind::ModelEffect), que MuRet no crea.
inline constexpr std::int32_t ColumnSubtype = 0x10;
inline constexpr std::int32_t WhirlSubtype = 0xe;
inline constexpr std::int32_t MirroredWhirlSubtype = 0xf;
[[nodiscard]] bool IsOrbit(std::int32_t type, std::int32_t subtype);
[[nodiscard]] Core::Expected<void, std::string>
CreateOrbitJoint(JointState& joint, const World::FishEffectRequest& request, Render::Models::CrtRandom& random);
[[nodiscard]] Core::Expected<void, std::string> StepOrbitJoint(JointState& joint,
                                                               const JointKernels::StepServices& services);

// El 0x61c/0 (SweepJoints.cpp): alta 0x5342e4 y paso 0x53b3e1, sin la cola ni el movimiento comunes. Lo pide el paso
// del efecto 0x5be (0x52a72a), que MuRet no crea (0x4f9fae); con dueño y vida de más de 18, corta en 0x53b548 (el
// héroe).
inline constexpr std::int32_t SweepSubtype = 0;
void CreateSweepJoint(JointState& joint, const World::FishEffectRequest& request);
[[nodiscard]] Core::Expected<void, std::string> StepSweepJoint(JointState& joint,
                                                               const JointKernels::StepServices& services);

// El 0x14f (PlasmaJoints.cpp): alta 0x530ddd y paso 0x535b37, una vuelta por punto hacia su personaje dueño (0x4e9810).
// Su llamador (0x5026a4) sigue cortado: el subtipo sale de 0x4f2230 (el +0x31b de Fenrir, que ActorState no tiene).
inline constexpr std::int32_t PlasmaJoint = 0x14f;
void CreatePlasmaJoint(JointState& joint, const World::FishEffectRequest& request);
[[nodiscard]] Core::Expected<void, std::string> StepPlasmaJoint(JointState& joint,
                                                                const JointKernels::StepServices& services);
} // namespace Client::Scenes::JointFamilies
