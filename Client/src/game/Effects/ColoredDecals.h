#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

// The creation of colored decal 0x61e (Effect\Magic_Ground2), which the skills and the ground pass ask for: 06, "Los
// efectos con modelo de los tipos altos".
namespace Client::Kernel
{
struct ModelEffectArguments;
} // namespace Client::Kernel

namespace Client::Crt
{
class CrtRandom;
}

namespace Client::World
{
struct ModelEffect;
} // namespace Client::World

namespace Client::Terrain
{

// The subtypes of 0x61e with their own case in the creation (0x51eebb), besides those of ModelEffects.h.
inline constexpr std::int32_t PulsingDecalSubtype = 4; // that of 0x30, between joints 0x613/2 (0x4fe127)
inline constexpr std::int32_t BindingDecalSubtype = 6; // that of 0x46, over the target (0x4fe383..0x4fe43d)
inline constexpr std::int32_t BindingChildSubtype = 7; // the one the drawing of /6 sheds every 25 lives (0x52e04c)
inline constexpr std::int32_t SpawningDecalSubtype = 8;
inline constexpr std::int32_t GrowingDecalSubtype = 9;       // the one the drawing of /8 sheds on even lives (0x52e0a2)
inline constexpr std::int32_t CoolPulsingDecalSubtype = 0xa; // that of 0x45, between joints 0x613/0x15 (0x4fe360)

// 0x51eebb..0x51efa4, the case of 0x61e after the prefix: life 20; /4 and /0xa, life 40 and the scale
// (rand() % 50 + 50) · 0.04f; /6, life 60, that scale and the start at the position; /7, life 40 and the z angle at
// rand() % 360; /8, life 20; /9, life 10 and the scale at 0.1f. The rest, only the life.
void BeginColoredDecal(World::ModelEffect& effect, Crt::CrtRandom& random);

// 0x518cf0(0x61e, …) on `table`: the prefix (BeginModelEffect) and BeginColoredDecal. With the table full, nothing and
// no rand().
[[nodiscard]] std::optional<std::size_t> CreateColoredDecal(std::span<World::ModelEffect> table,
                                                            const Kernel::ModelEffectArguments& arguments,
                                                            Crt::CrtRandom& random);
} // namespace Client::Terrain
