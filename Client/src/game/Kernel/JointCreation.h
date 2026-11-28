#pragma once

#include "Core/Expected.h"
#include "Kernel/DrawTailState.h"
#include "Kernel/JointRequest.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace Client::Crt
{
class CrtRandom;
} // namespace Client::Crt

// 0x530860 as the model effects reach it. The joints create model effects (game/Joints uses game/Effects), so the
// effects create joints through this port, which the composition root fills with Joints::CreateJoint.
namespace Client::Kernel
{
// The joint types the model effects name; the rest of the space is Joints/JointFamilies.h's.
inline constexpr std::int32_t BeamJoint = 0x618;
inline constexpr std::int32_t StormJoint = 0x632;

// The slot the joint took, or nothing with the table full.
using CreateJointFunction = Core::Expected<std::optional<std::size_t>, std::string> (*)(std::span<JointState> joints,
                                                                                        const JointRequest& request,
                                                                                        Crt::CrtRandom& random,
                                                                                        JointOwnerKind ownerKind);
} // namespace Client::Kernel
