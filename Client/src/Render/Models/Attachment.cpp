#include "Render/Models/Attachment.h"

#include "Render/Models/HalfLifeMath.h"

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
} // namespace Client::Render::Models
