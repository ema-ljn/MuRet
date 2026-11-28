#include "Session/ItemCreators.h"

#include "Core/Expected.h"
#include "Actors/Drawing/CharacterDrawing.h"
#include "Actors/Drawing/ItemDrawResources.h"
#include "Kernel/JointRequest.h"
#include "Fauna/FaunaEffects.h"
#include "Joints/FaunaJointCreation.h"
#include "Effects/ModelEffects.h"

#include <cstdint>
#include <string>

namespace Client::Session
{
void FillItemCreators(Actors::Drawing::ItemDrawContext& item, const Actors::Drawing::ItemDrawResources& resources)
{
    item.createJoint = [joints = resources.joints, random = &resources.random](
                           const Kernel::JointRequest& request) -> Core::Expected<void, std::string>
    {
        // Helper07's producers, with an opaque owner (the floor's FloorItemState or the ActorState of what hangs):
        // never read it as a FaunaObject.
        if (!((request.type == 0x613 && request.subtype == 4) || (request.type == 0x614 && request.subtype == 14)))
            return Core::Unexpected(std::string("0x530860: creador del joint de ítem fuera del contrato de dueño"));
        const auto created = Joints::CreateFaunaJoint(joints, request, *random);
        return created.HasValue() ? Core::Expected<void, std::string>{} : Core::Unexpected(created.Error());
    };
    item.createEffect =
        [effects = &resources.effects](std::uint16_t type, std::int32_t subtype,
                                       const Actors::Drawing::DrawnObject& object) -> Core::Expected<void, std::string>
    {
        if (type == 0xc2 && subtype == 3)
        {
            static_cast<void>(Fauna::CreateFaunaLightning(*effects, object.transform.position));
            return {};
        }
        if (subtype != 0 ||
            (type != Effects::DragonEffect && type != Effects::ArrivalEffect && type != Effects::TeleportEffect))
            return Core::Unexpected(std::string("0x518cf0: creador del efecto de ítem fuera del contrato portable"));
        static_cast<void>(effects->Create(static_cast<std::int16_t>(type), object.transform.position,
                                          object.transform.angles, object.color, object.transform.scale));
        return {};
    };
}
} // namespace Client::Session
