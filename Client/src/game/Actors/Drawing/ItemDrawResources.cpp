#include "Actors/Drawing/ItemDrawResources.h"

#include "Core/Expected.h"
#include "Math/HalfLifeMath.h"
#include "Math/OriginalRotation.h"
#include "Actors/Drawing/CharacterDrawing.h"
#include "Kernel/PoseBuffer.h"
#include "Actors/Drawing/GameItemRenderState.h"
#include "Actors/Drawing/GameItemSpecialDrawing.h"

#include <glm/ext/vector_float3.hpp>

#include <chrono>
#include <cstddef>
#include <ratio>
#include <span>
#include <string>

namespace Client::Actors::Drawing
{
void FillItemDrawContexts(ItemSpecialDrawContext& special, ItemDrawContext& item, const ItemDrawResources& resources)
{
    special.worldClock = static_cast<float>(std::chrono::duration<double, std::milli>(resources.frameClock).count());
    special.random = &resources.random;
    special.pulse = &resources.itemRender.specialPulse;
    special.sprites = &resources.sprites;
    Kernel::PoseBuffer* carry = &resources.poseCarry;
    special.bonePoint = [carry](std::size_t bone) -> Core::Expected<glm::vec3, std::string>
    {
        const auto& bones = carry->bones;
        if (bone >= bones.size())
            return Core::Unexpected(std::string("0x4ec110: el hueso del ítem excede la pose compartida"));
        return Math::OriginalVectorTransform({}, Math::OriginalMatrixOf(bones[bone]));
    };
    // privateBoneOrigin stays absent: object+0x120 is not represented by the global scratch.
    item.special = &special;
    item.glowPulse = resources.itemRender.glowPulse;
    item.selections = &resources.itemRender.selections;
    item.originalFrame = resources.originalFrame;
    item.modelState = &resources.modelState;
    item.map = resources.map;
    item.glowDetail = resources.glowDetail;
    item.poseBones = [carry]() { return std::span<const Math::BoneTransform>(carry->bones); };
}
} // namespace Client::Actors::Drawing
