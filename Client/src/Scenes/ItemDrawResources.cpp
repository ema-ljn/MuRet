#include "Scenes/ItemDrawResources.h"

#include "Core/Expected.h"
#include "Render/Models/HalfLifeMath.h"
#include "Render/Models/OriginalRotation.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/FaunaDrawing.h"
#include "Scenes/FaunaEffects.h"
#include "Scenes/FaunaJointCreation.h"
#include "Scenes/GameItemRenderState.h"
#include "Scenes/GameItemSpecialDrawing.h"
#include "World/ModelEffects.h"

#include <glm/ext/vector_float3.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <ratio>
#include <span>
#include <string>

namespace Client::Scenes
{
void FillItemDrawContexts(ItemSpecialDrawContext& special, ItemDrawContext& item, const ItemDrawResources& resources)
{
    special.worldClock = static_cast<float>(std::chrono::duration<double, std::milli>(resources.frameClock).count());
    special.random = &resources.random;
    special.pulse = &resources.itemRender.specialPulse;
    special.sprites = &resources.sprites;
    FaunaDrawCarry* carry = &resources.poseCarry;
    special.bonePoint = [carry](std::size_t bone) -> Core::Expected<glm::vec3, std::string>
    {
        const auto& bones = carry->bones;
        if (bone >= bones.size())
            return Core::Unexpected(std::string("0x4ec110: el hueso del ítem excede la pose compartida"));
        return Render::Models::OriginalVectorTransform({}, Render::Models::OriginalMatrixOf(bones[bone]));
    };
    // privateBoneOrigin sigue ausente: object+120 no está representado por el scratch global.
    item.special = &special;
    item.glowPulse = resources.itemRender.glowPulse;
    item.selections = &resources.itemRender.selections;
    item.originalFrame = resources.originalFrame;
    item.modelState = &resources.modelState;
    item.map = resources.map;
    item.poseBones = [carry]() { return std::span<const Render::Models::BoneTransform>(carry->bones); };
    item.createJoint = [joints = resources.joints, random = &resources.random](
                           const World::FishEffectRequest& request) -> Core::Expected<void, std::string>
    {
        // Productores de Helper07, dueño opaco (FloorItemState del piso o ActorState de lo que cuelga): nunca
        // interpretarlo como FaunaObject.
        if (!((request.type == 0x613 && request.subtype == 4) || (request.type == 0x614 && request.subtype == 14)))
            return Core::Unexpected(std::string("0x530860: creador del joint de ítem fuera del contrato de dueño"));
        const auto created = CreateFaunaJoint(joints, request, *random);
        return created.HasValue() ? Core::Expected<void, std::string>{} : Core::Unexpected(created.Error());
    };
    item.createEffect = [effects = &resources.effects](std::uint16_t type, std::int32_t subtype,
                                                       const DrawnObject& object) -> Core::Expected<void, std::string>
    {
        if (type == 0xc2 && subtype == 3)
        {
            static_cast<void>(CreateFaunaLightning(*effects, object.transform.position));
            return {};
        }
        if (subtype != 0 ||
            (type != World::DragonEffect && type != World::ArrivalEffect && type != World::TeleportEffect))
            return Core::Unexpected(std::string("0x518cf0: creador del efecto de ítem fuera del contrato portable"));
        static_cast<void>(effects->Create(static_cast<std::int16_t>(type), object.transform.position,
                                          object.transform.angles, object.color, object.transform.scale));
        return {};
    };
}
} // namespace Client::Scenes
