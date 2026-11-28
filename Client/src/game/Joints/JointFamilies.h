#pragma once

#include "Core/Expected.h"
#include "Kernel/DrawTailState.h"
#include "Joints/JointKernels.h"
#include "Kernel/JointRequest.h"

#include <cstdint>
#include <string>

namespace Client::Crt
{
class CrtRandom;
} // namespace Client::Crt

// The joint families that live in their own file. FaunaJointCreation.cpp calls their creation after the first segment
// (0x530afa) and their step after the common move (0x53590e); the step returns to the common tail (0x53c393).
namespace Client::Joints::JointFamilies
{
// The 0x614 (LightningJoints.cpp): creation 0x5309d6 and step 0x53683e..0x537278. The /0xa and /0xe have their own
// creation in FaunaJointCreation.cpp. The /0xc has creation and step, but no live creator: nobody known in c32bbd5f
// requests it; the /0xf stops in the step, at the light of its 0x5ca (0x5370e8).
inline constexpr std::int32_t LightningJoint = 0x614;
[[nodiscard]] Core::Expected<void, std::string> CreateLightningJoint(Kernel::JointState& joint,
                                                                     const Kernel::JointRequest& request,
                                                                     Crt::CrtRandom& random,
                                                                     const JointKernels::CreationServices* services);
[[nodiscard]] Core::Expected<void, std::string> StepLightningJoint(Kernel::JointState& joint,
                                                                   const JointKernels::StepServices& services);

// The 0x61b and 0x5bc (HandBoltJoints.cpp): creation 0x5335cf/0x5335d6 and step 0x539ccc..0x539f16.
inline constexpr std::int32_t RedHandBoltJoint = 0x61b;
inline constexpr std::int32_t BlueHandBoltJoint = 0x5bc;
void CreateHandBoltJoint(Kernel::JointState& joint);
[[nodiscard]] Core::Expected<void, std::string> StepHandBoltJoint(Kernel::JointState& joint,
                                                                  const JointKernels::StepServices& services);

// The 0x613 and 0x6ee (SpiritJoints.cpp): creation 0x53265e (blend at 3 and table 0x534f18) and step 0x538d77. The /4
// (and the 6ee/9) have their own creation and step in FaunaJointCreation.cpp; the 6ee/4 and 613/9 stop at creation.
inline constexpr std::int32_t SpiritJoint = 0x613;
inline constexpr std::int32_t SecondSpiritJoint = 0x6ee;
[[nodiscard]] Core::Expected<void, std::string> CreateSpiritJoint(Kernel::JointState& joint,
                                                                  const Kernel::JointRequest& request,
                                                                  Crt::CrtRandom& random,
                                                                  const JointKernels::CreationServices* services);
[[nodiscard]] Core::Expected<void, std::string> StepSpiritJoint(Kernel::JointState& joint,
                                                                const JointKernels::StepServices& services);

// The 0x60f/2 and 0x60f/0x18 (DropJoints.cpp), the drop that the 0x613/2 releases in its second step (0x5396ff):
// creation 0x533677 and 0x5340dc, and step 0x53b156. The /0x13, which falls (creation 0x533a28 and step 0x53a12c), is
// requested by the columns of the 0xc3 circle with the 0x62b (0x5236ce and 0x523898). The 0x62b shares creation and
// step.
inline constexpr std::int32_t DropJoint = 0x60f;
inline constexpr std::int32_t DropSubtype = 2;
inline constexpr std::int32_t DarkDropSubtype = 0x18;
inline constexpr std::int32_t SinkingDropSubtype = 0x13;
[[nodiscard]] bool IsDrop(std::int32_t type, std::int32_t subtype);
void CreateDropJoint(Kernel::JointState& joint, const Kernel::JointRequest& request, Crt::CrtRandom& random);
void StepDropJoint(Kernel::JointState& joint);

// The 0x60f/7, /0xb and /0x19 (WanderingJoints.cpp): creation 0x533bbd, 0x533d5f and 0x533db8 and step
// 0x53a87a..0x53b01f. They wander around their owner with the step counter: the /7 around a character (0x4f8592), the
// /0xb and /0x19 around the 0xf8 arrow (JointOwnerKind::ModelEffect, 0x51a405 and 0x51a3b7), which with skill 1 follows
// the joint and hits (0x520170).
inline constexpr std::int32_t WanderSubtype = 7;
inline constexpr std::int32_t ArrowWanderSubtype = 0xb;
inline constexpr std::int32_t PinkWanderSubtype = 0x19;
void CreateWanderingJoint(Kernel::JointState& joint, const Kernel::JointRequest& request, Crt::CrtRandom& random);
[[nodiscard]] Core::Expected<void, std::string> StepWanderingJoint(Kernel::JointState& joint,
                                                                   const JointKernels::StepServices& services);

// The 0x60f/4 and /0xc and the 0x62b/4 and /0xc (HelixJoints.cpp): creation 0x533faf..0x5340d7 and step
// 0x53b08a..0x53b14c. The 0x812's /4 and /0xc (0x52c9da and 0x52c9a7) carry the effect as owner
// (JointOwnerKind::ModelEffect); the /4 reads its light. FaunaJointCreation.cpp repeats their step while the remaining
// life is not a multiple of 12 (0x53c3c6).
inline constexpr std::int32_t SecondHelixJoint = 0x62b;
inline constexpr std::int32_t HelixSubtype = 4;
inline constexpr std::int32_t WideHelixSubtype = 0xc;
[[nodiscard]] bool IsHelix(std::int32_t type, std::int32_t subtype);
[[nodiscard]] Core::Expected<void, std::string>
CreateHelixJoint(Kernel::JointState& joint, const Kernel::JointRequest& request, Crt::CrtRandom& random);
[[nodiscard]] Core::Expected<void, std::string> StepHelixJoint(Kernel::JointState& joint);

// The 0x61c/2..6 (InfernoJoints.cpp): creation 0x534444..0x53463b and step 0x53b6a0..0x53b7ec. The /4 (skill 0x49,
// 0x504b33) releases each step a 0x62b/6 and two sprites; the /2, the 0xf6/6 effect every five lives (0x53b760).
inline constexpr std::int32_t InfernoJoint = 0x61c;
[[nodiscard]] bool IsInferno(std::int32_t type, std::int32_t subtype);
void CreateInfernoJoint(Kernel::JointState& joint, const Kernel::JointRequest& request);
[[nodiscard]] Core::Expected<void, std::string> StepInfernoJoint(Kernel::JointState& joint,
                                                                 const JointKernels::StepServices& services);

// The 0x5f2 (FireTrailJoints.cpp): creation 0x531229..0x53128f and 0x532073 and step 0x5366bd..0x5366ee. The trail of
// the wizards' 0xc2/6 and /8 meteors (0x51b098 and 0x51b268), owned by the effect (Effects::ModelEffect), which it
// follows while the effect lives and is 0xc2.
inline constexpr std::int32_t FireTrailJoint = 0x5f2;
void CreateFireTrailJoint(Kernel::JointState& joint, const Kernel::JointRequest& request);
[[nodiscard]] Core::Expected<void, std::string> StepFireTrailJoint(Kernel::JointState& joint);

// The 0x10f with a character (ActorState) as owner (AuraJoints.cpp): creation 0x530edd..0x531214 and step
// 0x535d4f..0x5366a7. The /1 (the fairy) and /3 (Blood Castle) carry a FaunaObject and go on in
// FaunaJointCreation.cpp. The /5..7 skip the first segment (0x530a5c): FaunaJointCreation.cpp calls them earlier.
inline constexpr std::int32_t AuraJoint = 0x10f;
[[nodiscard]] Core::Expected<void, std::string> CreateAuraJoint(Kernel::JointState& joint,
                                                                const Kernel::JointRequest& request);
[[nodiscard]] Core::Expected<void, std::string> StepAuraJoint(Kernel::JointState& joint,
                                                              const JointKernels::StepServices& services);

// The 0x618 (BeamJoints.cpp): creation 0x532125..0x5323ce and step 0x5381f3..0x538704. The owner (+0x54) is a
// character (ActorState) in the /0, /1, /2, /3, /0xc, /0xd, /0x10 and /0x11, the FaunaObject in the /4 (0x447374) and
// the effect with a model (JointOwnerKind::ModelEffect) in the arrows' /5 and the 0x11b/1's /0xa and /0xb; the /6 and
// /9 do not read it.
inline constexpr std::int32_t BeamJoint = 0x618;
[[nodiscard]] Core::Expected<void, std::string>
CreateBeamJoint(Kernel::JointState& joint, const Kernel::JointRequest& request, Crt::CrtRandom& random);
[[nodiscard]] Core::Expected<void, std::string> StepBeamJoint(Kernel::JointState& joint,
                                                              const JointKernels::StepServices& services);

// The 0x632/7 and its three 0x632/6 (StormJoints.cpp): creation 0x534b27 and 0x534a16 and step 0x53bec2. The /6 skips
// the first segment (0x530a5c); FaunaJointCreation.cpp creates the three /6 at the end of the /7's creation
// (0x534b3f..0x534bc0). The /5, the star that falls from the siege's 0x61c/0 pulse (0x52c266, owned by the effect,
// which it does not read): creation 0x534bcb and step 0x53c0a5. The /0..3 fall like the /5 (the 0x44's /0, 0x4fe235,
// and the 0x47's /1, 0x4fe4e7, owned by the target character, which they read only on reaching the ground); the /4 (the
// 0x13a's bolt, 0x51e01a) follows its owner effect: creation 0x534977 and step 0x53be90.
inline constexpr std::int32_t StormJoint = 0x632;
inline constexpr std::int32_t LastFallingSubtype = 3;
inline constexpr std::int32_t BlastSubtype = 4;
inline constexpr std::int32_t FallingStarSubtype = 5;
inline constexpr std::int32_t BoltSubtype = 6;
inline constexpr std::int32_t StormSubtype = 7;
[[nodiscard]] Core::Expected<void, std::string>
CreateStormJoint(Kernel::JointState& joint, const Kernel::JointRequest& request, Crt::CrtRandom& random);
[[nodiscard]] Core::Expected<void, std::string> StepStormJoint(Kernel::JointState& joint,
                                                               const JointKernels::StepServices& services);

// The 0x62c (SpiralJoints.cpp): creation 0x5346d8..0x534954 and step 0x53b93f..0x53be71, without the common move
// (0x5358a3). The /0 rises straight; the /1..4 and /0xb..0xd rise spinning (the 0x41's 0x62c/0 effect, 0x51f25e, and
// the 0x4c's six, 0x50293c); the /5..7 follow the owner's bone with its shared model (0x53bc71). Their only creator,
// the 0x62c/1 effect (0x51f32e), is created behind the 0x50e0c0 cut (the effect of +0x78 & 0x800).
inline constexpr std::int32_t SpiralJoint = 0x62c;
[[nodiscard]] Core::Expected<void, std::string>
CreateSpiralJoint(Kernel::JointState& joint, const Kernel::JointRequest& request, Crt::CrtRandom& random);
[[nodiscard]] Core::Expected<void, std::string> StepSpiralJoint(Kernel::JointState& joint,
                                                                const JointKernels::StepServices& services);

// The 0x5ac (FlareJoints.cpp): creation 0x530c93..0x530dd8 and step 0x5359d7..0x535b32, without the common tail and
// move. The sixty /0 of the 0x114/0 (0x51c1b8) and of the BlastModelEffects combo (0x51c004), ownerless.
inline constexpr std::int32_t FlareJoint = 0x5ac;
void CreateFlareJoint(Kernel::JointState& joint, const Kernel::JointRequest& request, Crt::CrtRandom& random);
[[nodiscard]] Core::Expected<void, std::string> StepFlareJoint(Kernel::JointState& joint);

// The 0x60f and 0x62b /0x10, /0xe and /0xf (OrbitJoints.cpp): creation 0x5337fa and 0x534132 and step 0x53a5c7 and
// 0x53b18a. The /0x10 is the column of the 0xbd shadow rain (0x51bcb0), owned by its character; the /0xe and /0xf are
// requested by the 0x60f effect (0x52b408 and 0x52b42b, owned by it, JointOwnerKind::ModelEffect), which MuRet does not
// create.
inline constexpr std::int32_t ColumnSubtype = 0x10;
inline constexpr std::int32_t WhirlSubtype = 0xe;
inline constexpr std::int32_t MirroredWhirlSubtype = 0xf;
[[nodiscard]] bool IsOrbit(std::int32_t type, std::int32_t subtype);
[[nodiscard]] Core::Expected<void, std::string>
CreateOrbitJoint(Kernel::JointState& joint, const Kernel::JointRequest& request, Crt::CrtRandom& random);
[[nodiscard]] Core::Expected<void, std::string> StepOrbitJoint(Kernel::JointState& joint,
                                                               const JointKernels::StepServices& services);

// The 0x61c/0 (SweepJoints.cpp): creation 0x5342e4 and step 0x53b3e1, without the common tail and move. Requested by
// the step of the 0x5be effect (0x52a72a), which the 0x37 creates (0x4f9fae); with an owner and life over 18, it stops
// at 0x53b548 (the hero).
inline constexpr std::int32_t SweepSubtype = 0;
void CreateSweepJoint(Kernel::JointState& joint, const Kernel::JointRequest& request);
[[nodiscard]] Core::Expected<void, std::string> StepSweepJoint(Kernel::JointState& joint,
                                                               const JointKernels::StepServices& services);

// The 0x14f (PlasmaJoints.cpp): creation 0x530ddd and step 0x535b37, one turn per point toward its owner character
// (0x4e9810). Requested by the 0x4c (0x5026a4..0x502841, AttackSkillCases.cpp), with the subtype from 0x4f2230 (the
// +0x31b, pet.flags1b).
inline constexpr std::int32_t PlasmaJoint = 0x14f;
void CreatePlasmaJoint(Kernel::JointState& joint, const Kernel::JointRequest& request);
[[nodiscard]] Core::Expected<void, std::string> StepPlasmaJoint(Kernel::JointState& joint,
                                                                const JointKernels::StepServices& services);

// The 0x610/5 and the 0x60f/0x29 and /0x2a (SparkJoints.cpp): the sparks of Kalima's sixth helper, the 0x196
// (0x43e904; creation 0x531dd0 and step 0x537a00), and the one that turns around NPC 257 (0x5165d1, owned by it;
// creation 0x5339a5 and step 0x53a039), which releases the /0x29 (0x53a0eb; creation 0x533915 and step 0x539fc2). The
// /0x2a does not move with the common step.
inline constexpr std::int32_t HelperSparkJoint = 0x610;
inline constexpr std::int32_t HelperSparkSubtype = 5;
inline constexpr std::int32_t FallingSparkleSubtype = 0x29;
inline constexpr std::int32_t SpinningSparkleSubtype = 0x2a;
[[nodiscard]] bool IsSpark(std::int32_t type, std::int32_t subtype);
[[nodiscard]] Core::Expected<void, std::string>
CreateSparkJoint(Kernel::JointState& joint, const Kernel::JointRequest& request, Crt::CrtRandom& random);
[[nodiscard]] Core::Expected<void, std::string> StepSparkJoint(Kernel::JointState& joint,
                                                               const JointKernels::StepServices& services);
} // namespace Client::Joints::JointFamilies
