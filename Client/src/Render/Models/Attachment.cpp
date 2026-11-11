#include "Render/Models/Attachment.h"

#include "Render/Models/HalfLifeMath.h"
#include "Render/Models/OriginalRotation.h"

#include <glm/ext/vector_float3.hpp>

namespace Client::Render::Models
{
Attachment AttachmentOf(AttachmentSlot slot)
{
    switch (slot)
    {
    case AttachmentSlot::RightHand:
        return {33, {}};
    case AttachmentSlot::LeftHand:
        return {42, {}};
    case AttachmentSlot::Wings:
        return {47, {0.0f, 0.0f, 15.0f}};
    case AttachmentSlot::Pet:
        return {34, {20.0f, 0.0f, 0.0f}};
    }
    return {};
}

BoneTransform AttachmentRoot(const BoneTransform& parent, const glm::vec3& offset)
{
    BoneTransform root = parent;
    root[3] += VectorRotate(offset, parent);
    return root;
}

// 0x5fc8f0 en el buffer local de 0x509340 (ebp−0x64), la traslación en sus +0xc, +0x1c y +0x2c, y
// 0x5fca90 con el hueso, todo con los bits de la x87.
BoneTransform PlacedRoot(const BoneTransform& parent, const HangingPlacement& placement)
{
    OriginalMatrix local = OriginalAngleMatrix(placement.angles);
    local[3] = placement.translation.x;
    local[7] = placement.translation.y;
    local[11] = placement.translation.z;
    return BoneTransformOf(OriginalConcatTransforms(OriginalMatrixOf(parent), local));
}
} // namespace Client::Render::Models
