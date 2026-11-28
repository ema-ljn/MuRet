#include "Joints/JointKernels.h"

#include "Core/Expected.h"
#include "Kernel/JointRequest.h"
#include "Math/OriginalRotation.h"
#include "Kernel/ActorState.h"
#include "Scenes/GameDrawTail.h"

#include <glm/ext/vector_float3.hpp>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <string>

namespace Client::Joints::JointKernels
{
namespace
{
void PlaceFirstQuad(Scenes::JointState& joint, const Math::OriginalMatrix& matrix, bool crossed)
{
    // 0x530b07 y 0x53553f: ancho · −0,5f (0x6ba298) y · 0,5f (0x6b882c), guardados en float.
    const auto half = static_cast<float>(static_cast<double>(joint.width) * 0.5);
    const std::array<glm::vec3, 4> straight{{{-half, 0, 0}, {half, 0, 0}, {0, 0, -half}, {0, 0, half}}};
    const std::array<glm::vec3, 4> across{{{-half, 0, 0}, {half, 0, 0}, {0, -half, 0}, {0, half, 0}}};
    const auto& offsets = crossed ? across : straight;
    for (std::size_t i = 0; i < offsets.size(); ++i)
    {
        const auto rotated = Math::OriginalVectorRotate(offsets[i], matrix);
        joint.points[0][i] = {static_cast<float>(static_cast<double>(joint.position.x) + rotated.x),
                              static_cast<float>(static_cast<double>(joint.position.y) + rotated.y),
                              static_cast<float>(static_cast<double>(joint.position.z) + rotated.z)};
    }
}

Core::Expected<void, std::string> ShiftTail(Scenes::JointState& joint)
{
    const auto count = std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(joint.count) + 1u);
    const auto maximum = std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(joint.maximumCount) - 1u);
    joint.count = count > maximum ? maximum : count;
    if (joint.count >= static_cast<std::int32_t>(joint.points.size()) || joint.count < -1)
        return Core::Unexpected(std::string("0x53553f: la cola del joint excede sus puntos canónicos"));
    for (std::int32_t n = joint.count; n > 0; --n)
        joint.points[static_cast<std::size_t>(n)] = joint.points[static_cast<std::size_t>(n - 1)];
    return {};
}
} // namespace

void FirstQuad(Scenes::JointState& joint, const Math::OriginalMatrix& matrix)
{
    // 530afa: width es el retenido del uso anterior, antes de que el caso escriba el nuevo.
    PlaceFirstQuad(joint, matrix, false);
}

void FirstQuad(Scenes::JointState& joint)
{
    FirstQuad(joint, Math::OriginalAngleMatrix(joint.angles));
}

Core::Expected<void, std::string> AppendJointTail(Scenes::JointState& joint, const Math::OriginalMatrix& matrix)
{
    if (auto shifted = ShiftTail(joint); !shifted.HasValue())
        return shifted;
    PlaceFirstQuad(joint, matrix, false);
    return {};
}

Core::Expected<void, std::string> AdvanceJointTail(Scenes::JointState& joint)
{
    return ShiftTail(joint);
}

Core::Expected<void, std::string> AppendJointTail(Scenes::JointState& joint)
{
    return AppendJointTail(joint, Math::OriginalAngleMatrix(joint.angles));
}

Core::Expected<void, std::string> AppendCrossedJointTail(Scenes::JointState& joint, const Math::OriginalMatrix& matrix,
                                                         bool crossed)
{
    if (auto shifted = ShiftTail(joint); !shifted.HasValue())
        return shifted;
    PlaceFirstQuad(joint, matrix, crossed);
    return {};
}

const Kernel::ActorState* ActorOwner(const Scenes::JointState& joint)
{
    if (joint.ownerKind != Kernel::JointOwnerKind::ByType && joint.ownerKind != Kernel::JointOwnerKind::Actor)
        return nullptr;
    return static_cast<const Kernel::ActorState*>(joint.ownerContext);
}
} // namespace Client::Joints::JointKernels
