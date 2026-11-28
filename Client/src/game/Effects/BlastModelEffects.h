#pragma once

#include "Core/Expected.h"
#include "Audio/SoundId.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>

namespace Client::Crt
{
class CrtRandom;
} // namespace Client::Crt

namespace Client::Models
{
struct Skeleton;
} // namespace Client::Models

// The model effects of CL11's "switch" batch (0x518cf0 and 0x5208a0): the stones and sparks of the wizard and monster
// skills without a file of their own. What is left out (the particles and joints MuRet does not create yet) stops at
// the call that asks for it.
namespace Client::Kernel
{
struct ModelEffectArguments;
struct JointRequest;
} // namespace Client::Kernel

namespace Client::Kernel
{
class KalimaWater;
} // namespace Client::Kernel

namespace Client::Particles
{
class Particles;
} // namespace Client::Particles

namespace Client::Effects
{
struct ModelEffect;
struct ModelEffectOwner;
struct ModelEffectStepServices;
class ModelEffects;

// What this batch's creations ask for outside their slot: the own record (0x7681eb4: its depth, which spends the
// close's 3848 rand(), and its count +0xf04), the published hero (0x743a170), 0x517a20's targets (the 0xf6 with the
// hero as owner), 0x530860's joints (`effectOwner` when the owner is the effect just created) and the sound of
// 0x41ed10(id, 0, 0). Without what a creation asks for, it stops at its address.
struct BlastEffectServices
{
    std::uint8_t* ownRecordDepth = nullptr;
    std::uint8_t* ownRecordCount = nullptr;
    const void* hero = nullptr;
    std::function<Core::Expected<void, std::string>(const ModelEffect&, float)> skillHits{};
    std::function<Core::Expected<void, std::string>(const Kernel::JointRequest&, bool effectOwner)> joint{};
    std::function<void(Audio::SoundId)> sound{};
};

// The types CreateBlastModelEffect creates: 0xe4, 0xe5/0xe6 with subtype 5, 0xe7..0xe9, 0xec, 0xf2..0xf6, 0x105/1 (the
// 0x106's child), 0x106..0x109, 0x10c, 0x10d, 0x10f, 0x112, 0x113 and 0x5de with subtypes 1 and 2.
[[nodiscard]] bool IsBlastModelEffect(std::int16_t type, std::int32_t subtype);

// 0x518cf0 with `arguments` for one of IsBlastModelEffect's types: the ModelEffects::Begin prefix, `owner` (what the
// step reads live from +0x108) and the type's case (0x518e66 the 0x106, which returns to 0x518d0a with the 0x105 of its
// subtype; 0x5192ed the 0xf5; 0x5198cd the 0xf6; …). Returns the requested effect's slot (the 0x106's, not its
// 0x105's), or nothing with the table full, without rand(). Another type, or what the case asks for and MuRet lacks,
// stops.
[[nodiscard]] Core::Expected<std::optional<std::size_t>, std::string>
CreateBlastModelEffect(ModelEffects& effects, const Kernel::ModelEffectArguments& arguments,
                       const ModelEffectOwner& owner, Crt::CrtRandom& random, const BlastEffectServices& services);

// The effects StepBlastModelEffect steps: those of IsBlastModelEffect, with the 0x105 only in subtype 1.
[[nodiscard]] bool StepsAsBlastModelEffect(const ModelEffect& effect);

// 0x5208a0 on one of StepsAsBlastModelEffect, after its first rand() (`lightDraw`, the local light) and with `before`,
// the position before the step: the type's case and 0x52cb98's tail (the published action and the animation of the
// BMDs in [0xad, 0x151), 0x51fc80 with the angles or, for the 0x10f, without them; the 0xf3 and the 0xf4 only lose
// life; the 0xe7..0xe9 and the 0x113 without an owner return without the tail). Leaves `before` in previousPosition.
[[nodiscard]] Core::Expected<void, std::string>
StepBlastModelEffect(ModelEffect& effect, const Models::Skeleton* skeleton, Particles::Particles& particles,
                     float worldClock, Crt::CrtRandom& random, int lightDraw, Kernel::KalimaWater* kalima,
                     const ModelEffectStepServices& services);
} // namespace Client::Effects
