#include "Scenes/SessionEventApplier.h"

#include "App/CharacterEvent.h"
#include "App/ServerSession.h"
#include "App/WorldEvent.h"
#include "Core/Expected.h"
#include "Data/Models/PlayerModels.h"
#include "Protocol/Character/CharacterSelectResult.h"
#include "Protocol/Character/InventoryList.h"
#include "Protocol/Movement.h"
#include "Protocol/World.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/IWorldEntryPublisher.h"
#include "Scenes/IWorldTransitionPublisher.h"
#include "Scenes/InventoryReply.h"
#include "Scenes/MovementReplies.h"
#include "Scenes/ReturnToSelection.h"
#include "Scenes/SelectionState.h"
#include "Scenes/WorldEntry.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "Scenes/WorldTransitions.h"
#include "World/LeafWeather.h"

#include <cstdint>
#include <span>
#include <string>
#include <variant>

namespace Client::Scenes
{
namespace
{
void ApplyWeather(const Protocol::World::Weather& received, World::LeafWeather& weather)
{
    // 0x4e36c4/0x4e36ed: sólo target; el default de 0x4e5b4a no escribe nada ni pide rand().
    constexpr std::uint8_t NibbleShift = 4;
    constexpr std::uint8_t IntensityMask = 0x0f;
    constexpr std::int32_t TargetPerIntensity = 6;
    const auto mode = received.code >> NibbleShift;
    if (mode == 0)
        weather.target = 0;
    else if (mode == 1)
        weather.target = static_cast<std::int32_t>(received.code & IntensityMask) * TargetPerIntensity;
}

// El 0x3d de la respuesta va a la sesión que la está aplicando.
class SessionPublisher final : public IWorldEntryPublisher, public IWorldTransitionPublisher
{
public:
    explicit SessionPublisher(App::ServerSession& session) : m_session(session) {}

    void PublishWorldEntry() override
    {
        m_session.PublishWorldEntry();
    }

    void ConfirmTeleport() override
    {
        m_session.ConfirmTeleport();
    }
    void RequestCrywolfState() override
    {
        m_session.RequestCrywolfState();
    }

private:
    App::ServerSession& m_session;
};
} // namespace

SessionEventApplier::SessionEventApplier(const WorldEntryResources& resources) : m_resources(resources) {}

Core::Expected<void, std::string> SessionEventApplier::ApplyCharacter(const App::CharacterEvent& event,
                                                                      const App::ServerSession&)
{
    return m_resources.selection.Apply(std::span(&event, 1), m_resources.camera, m_resources.cameraInput,
                                       &m_resources.cloth,
                                       m_resources.models.SkeletonOf(Data::Models::PlayerSkeletonModel));
}

Core::Expected<void, std::string> SessionEventApplier::ApplyWorld(const App::WorldEvent& event,
                                                                  App::ServerSession& session)
{
    if (const auto* walk = std::get_if<Protocol::Movement::Walk>(&event))
        return ApplyWalkReply(*walk, session.PlayerIndex(), m_resources);
    if (const auto* damage = std::get_if<Protocol::Movement::Damage>(&event))
        return ApplyDamageReply(*damage, session.PlayerIndex(), m_resources);
    if (const auto* teleport = std::get_if<Protocol::World::Teleport>(&event))
    {
        SessionPublisher publisher(session);
        return ApplyTeleport(*teleport, session.PlayerIndex(), m_resources, publisher);
    }
    if (const auto* regen = std::get_if<Protocol::World::Regen>(&event))
        return ApplyRegen(*regen, session.PlayerIndex(), m_resources);
    return ApplyWorldContent(event, session);
}

Core::Expected<void, std::string> SessionEventApplier::ApplyWorldContent(const App::WorldEvent& event,
                                                                         App::ServerSession& session)
{
    if (const auto* weather = std::get_if<Protocol::World::Weather>(&event))
    {
        ApplyWeather(*weather, m_resources.world.Runtime().leafWeather);
        return {};
    }
    if (std::holds_alternative<App::ClearInventoryReceived>(event))
    {
        ClearForInventoryReply(m_resources);
        return {};
    }
    if (std::holds_alternative<App::ReturnToSelectionReceived>(event))
    {
        ApplyReturnToSelection(m_resources);
        return {};
    }
    if (const auto* inventory = std::get_if<Protocol::Character::InventoryList>(&event))
    {
        const auto applied = ApplyInventory(*inventory, m_resources);
        if (!applied.HasValue())
        {
            return Core::Unexpected(applied.Error());
        }
        // Los sonidos que pidió quedan sin tocar: MuRet no tiene audio hasta la CL10.
        return {};
    }
    const auto& reply = std::get<Protocol::Character::CharacterSelectResult>(event);
    SessionPublisher publisher(session);
    const auto entered = ApplyWorldEntry(reply, session.PlayerIndex(), m_resources, publisher);
    if (!entered.HasValue())
    {
        return Core::Unexpected(entered.Error());
    }
    // Los sonidos que pidió quedan sin tocar: MuRet no tiene audio hasta la CL10.
    return {};
}
} // namespace Client::Scenes
