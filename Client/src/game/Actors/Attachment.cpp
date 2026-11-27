#include "Actors/Attachment.h"

#include "Math/HalfLifeMath.h"
#include "Math/OriginalRotation.h"

#include <glm/ext/vector_float3.hpp>

namespace Client::Actors
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

Math::BoneTransform AttachmentRoot(const Math::BoneTransform& parent, const glm::vec3& offset)
{
    Math::BoneTransform root = parent;
    root[3] += Math::VectorRotate(offset, parent);
    return root;
}

// 0x5fc8f0 into the local buffer of 0x509340 (ebp−0x64), the translation at its +0xc, +0x1c and +0x2c, and 0x5fca90
// with the bone, all with the x87's bits.
Math::BoneTransform PlacedRoot(const Math::BoneTransform& parent, const HangingPlacement& placement)
{
    Math::OriginalMatrix local = Math::OriginalAngleMatrix(placement.angles);
    local[3] = placement.translation.x;
    local[7] = placement.translation.y;
    local[11] = placement.translation.z;
    return Math::BoneTransformOf(Math::OriginalConcatTransforms(Math::OriginalMatrixOf(parent), local));
}
} // namespace Client::Actors
