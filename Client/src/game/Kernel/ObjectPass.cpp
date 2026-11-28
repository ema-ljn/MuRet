#include "Kernel/ObjectPass.h"

#include "Core/Expected.h"
#include "Kernel/PoseBuffer.h"
#include "Math/OriginalRotation.h"

#include <glm/ext/vector_float3.hpp>

#include <cstddef>
#include <format>
#include <string>

namespace Client::Kernel
{
Core::Expected<glm::vec3, std::string> ObjectBonePoint(const GameObjectPass& pass, std::size_t bone,
                                                       const glm::vec3& offset)
{
    if (pass.retainedPose == nullptr)
    {
        return Core::Unexpected(std::format("0x4ec110: falta la pose global del hueso {} de los objetos del {}", bone,
                                            static_cast<int>(pass.map)));
    }
    // The native .bss starts at zero; a bone not yet published keeps that matrix.
    if (bone >= pass.retainedPose->bones.size())
    {
        return Math::OriginalVectorTransform(offset, Math::OriginalMatrix{});
    }
    // Translate=0: the buffer's bone is already scaled and translated to the world.
    return Math::OriginalVectorTransform(offset, Math::OriginalMatrixOf(pass.retainedPose->bones[bone]));
}
} // namespace Client::Kernel
