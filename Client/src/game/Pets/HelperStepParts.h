#pragma once

#include "Core/Expected.h"
#include "Math/HalfLifeMath.h"

#include <glm/ext/vector_float3.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace Client::Kernel
{
struct ActorState;
struct HelperState;
class WorldState;
} // namespace Client::Kernel

// What the 0x444010 bodies share (Pets/HelperTable.cpp and Pets/PetHelperSteps.cpp). Contract:
// Client/docs/research/06-render-escenas-y-ui.md, "El caballo oscuro" and "Las mascotas".
namespace Client::Pets
{
struct HelperStepContext;

// 0x444174..0x444182, 0x444a04..0x444a12 and 0x444c5a..0x444c6b.
inline constexpr glm::vec3 HelperDustLight{1.0f, 1.0f, 1.0f};

// 0x4440b8..0x4440e9 (also 0x4447c4..0x4447f4 and 0x444af0..0x444b20): the rider's position cell, times 0.01f and
// truncated, with the low byte of each (0x5f82a0), and bit 0 of its attribute (0x7d0d348), in the scene 4 load or the
// game map. `address` is the attribute read's.
[[nodiscard]] Core::Expected<bool, std::string>
InTheRidersSafeCell(const HelperStepContext& context, const Kernel::ActorState& rider, std::string_view address);

// 0x4ec110(bone, offset, out, 0) on the global pose buffer (0x68b181c + bone · 0x30): without the model's scale or
// origin, the point the last model posed in the previous draw leaves, which MuRet keeps in WorldState::PoseCarry
// (Q-C401). A bone no draw has written yet is the zero matrix (the buffer holds 200 bones in the .bss) and gives the
// origin; beyond 200 it cuts.
[[nodiscard]] Core::Expected<glm::vec3, std::string> GlobalPosePoint(const Kernel::WorldState& world, std::size_t bone,
                                                                     const glm::vec3& offset, std::string_view address);
// The same on `bones`, the global buffer a draw carries (WorldState's or its projected copy).
[[nodiscard]] Core::Expected<glm::vec3, std::string> GlobalPosePoint(std::span<const Math::BoneTransform> bones,
                                                                     std::size_t bone, const glm::vec3& offset,
                                                                     std::string_view address);

// 0x53fe70(type, position, angles, light, subtype, 1, 0) on the world's particles (0x743a198), with its clock. When
// full, none and no rand(); one MuRet cannot create cuts at `address`.
[[nodiscard]] Core::Expected<void, std::string> EmitHelperParticle(const HelperStepContext& context, std::int32_t type,
                                                                   std::int32_t subtype, const glm::vec3& position,
                                                                   const glm::vec3& angles, const glm::vec3& light,
                                                                   std::string_view address);

// 0x444425..0x4444dd, 0x444a19..0x444aa8 and 0x444e6f..0x444f1d: (rand() & 63) − 32 in x and y and (rand() & 31) − 16
// in z, each axis on the x87 until its fstp, and the 5f2 on map 2, none on 10 or the 5f3 elsewhere, with the dust
// light and the helper's angles.
[[nodiscard]] Core::Expected<void, std::string>
ScatterTheDust(const Kernel::HelperState& helper, const HelperStepContext& context, std::string_view address);

// The Icarus gallop's 5f2 (0x444325..0x4443d1 and 0x444d68..0x444e16) from `base`: the guard draws a rand() % 5 + 5,
// always positive, then they go one by one while their count does not reach a new rand() % 5 + 5 each loop (Q-C400),
// each shifted rand() % 50 − 25 in x and y and (rand() & 15) − 8 − 10 in z.
[[nodiscard]] Core::Expected<void, std::string> PuffTheIcarusDust(const glm::vec3& base, const glm::vec3& angles,
                                                                  const HelperStepContext& context,
                                                                  std::string_view address);

// 0x44480b..0x44484a and 0x444b3b..0x444b7a: with the rider's +0x80 at 1 or 2 alpha falls 0.1f, the x87 subtraction
// stored as float and compared unrounded: below 0, 0. Otherwise, 1.
void FadeWithTheRider(Kernel::HelperState& helper, const Kernel::ActorState& rider);
} // namespace Client::Pets
