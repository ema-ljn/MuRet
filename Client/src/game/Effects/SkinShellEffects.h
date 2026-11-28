#pragma once

#include "Core/Expected.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace Client::Kernel
{
struct ModelEffectArguments;
} // namespace Client::Kernel

namespace Client::Crt
{
class CrtRandom;
}
namespace Client::Effects
{
class ModelEffects;
struct ModelEffect;
struct ModelEffectStepServices;

// Skill\skinshell (0x61672d): the shells 0x4ee770 drops from the vertices of the 0x20b (Player\Shadow01) when the joint
// 0x632/0 or /1 with an owner reaches the ground (0x53c227, 0x53c241), with the joint's subtype.
inline constexpr std::int16_t SkinShellEffect = 0x137;

// 0x518cf0 with the 0x137 (0x51d8c4..0x51da45), after the common prefix (ModelEffects::Begin): the push (in y) at
// ((rand() & 127) + 32) · 0.1f, z + 50, the life (rand() & 15) + 32, the scale (rand() % 3 + 3) · 0.1f, the alpha at
// 0.5, the angles' z at rand() % 360 and the speed (+0xcc), the push rotated by the angles; the fall (+0xe4)
// rand() % 5 + 2 plus rand() % 5, and the three angles rand() % 360. The light: (0.5, 0.5, 0.5) the /0 and
// (0.1f, 0.6f, 1) the /1; with another subtype the request's stays. Nine rand(). With the table full, nothing (not even
// rand()).
[[nodiscard]] std::optional<std::size_t> CreateSkinShell(const Kernel::ModelEffectArguments& arguments,
                                                         ModelEffects& effects, Crt::CrtRandom& random);

// 0x529926..0x529a4a, the step's body (the caller runs 0x5208a0's common prologue and epilogue): advances by the speed,
// brakes it by 0.9f, falls by +0xe4, accelerates it by 1.1f and the fall drops 3. With the terrain (0x5f93c0,
// unrounded) strictly above z, z on it, the fall times −0.2f, five lives less and the angles' x minus the scale · 128;
// otherwise (also with NaN), minus the scale · 16. The alpha, life · 0.1f. No rand().
[[nodiscard]] Core::Expected<void, std::string> StepSkinShell(ModelEffect& effect,
                                                              const ModelEffectStepServices& services);
} // namespace Client::Effects
