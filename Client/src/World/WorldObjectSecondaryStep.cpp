#include "World/WorldObjectSecondaryStep.h"

#include "Core/Expected.h"
#include "World/IWorldObjectStepServices.h"
#include "World/WorldObjectStepContext.h"
#include "World/WorldObjectStepError.h"
#include "World/WorldObjects.h"

#include <bit>
#include <cmath>

namespace Client::World
{
Core::Expected<void, WorldObjectStepError> StepWorldObjectSecondary(WorldObject& object,
                                                                    WorldObjectStepContext& context)
{
    const float heroX = context.heroPosition.x;
    const float heroY = context.heroPosition.y;
    const auto action = context.heroAction;
    constexpr float Threshold = std::bit_cast<float>(0x3dcccccdu);
    constexpr float Impulse = std::bit_cast<float>(0xbecccccdu);
    constexpr float Damping = std::bit_cast<float>(0x3f19999au);
    if (action >= 13 && action <= 33 && !(object.positionIncrement.x >= Threshold))
    {
        const double dx = static_cast<double>(heroX) - object.transform.position.x;
        const double dy = static_cast<double>(heroY) - object.transform.position.y;
        const float dy32 = static_cast<float>(dy);
        const double distance = std::sqrt(dy * dy32 + dx * dx);
        if (!(distance >= 50.0))
        {
            object.positionIncrement.z = 0.0f;
            object.positionIncrement.x = static_cast<float>(dx * Impulse);
            object.positionIncrement.y = static_cast<float>(static_cast<double>(dy32) * Impulse);
            object.copiedAngles.y = static_cast<float>(dx * (-4.0));
            object.copiedAngles.x = static_cast<float>(static_cast<double>(dy32) * (-4.0));
            auto sound = context.services.PlayObjectSound(124, object, 0);
            if (!sound.HasValue())
                return Core::Unexpected(sound.Error());
        }
    }
    // X integra el producto retenido; los otros cinco integran su store float32.
    const double x = static_cast<double>(object.positionIncrement.x) * Damping;
    object.positionIncrement.x = static_cast<float>(x);
    object.positionIncrement.y = static_cast<float>(static_cast<double>(object.positionIncrement.y) * Damping);
    object.positionIncrement.z = static_cast<float>(static_cast<double>(object.positionIncrement.z) * Damping);
    object.copiedAngles.x = static_cast<float>(static_cast<double>(object.copiedAngles.x) * Damping);
    object.copiedAngles.y = static_cast<float>(static_cast<double>(object.copiedAngles.y) * Damping);
    object.copiedAngles.z = static_cast<float>(static_cast<double>(object.copiedAngles.z) * Damping);
    object.transform.position.x = static_cast<float>(x + object.transform.position.x);
    object.transform.position.y =
        static_cast<float>(static_cast<double>(object.positionIncrement.y) + object.transform.position.y);
    object.transform.position.z =
        static_cast<float>(static_cast<double>(object.positionIncrement.z) + object.transform.position.z);
    object.transform.angles.x =
        static_cast<float>(static_cast<double>(object.copiedAngles.x) + object.transform.angles.x);
    object.transform.angles.y =
        static_cast<float>(static_cast<double>(object.copiedAngles.y) + object.transform.angles.y);
    object.transform.angles.z =
        static_cast<float>(static_cast<double>(object.copiedAngles.z) + object.transform.angles.z);
    return {};
}
} // namespace Client::World
