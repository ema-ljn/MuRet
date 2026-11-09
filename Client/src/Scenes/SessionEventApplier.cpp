#include "Scenes/SessionEventApplier.h"

#include "App/CharacterEvent.h"
#include "App/ServerSession.h"
#include "App/WorldEvent.h"
#include "Core/Expected.h"
#include "Protocol/Character/CharacterSelectResult.h"
#include "Protocol/Character/InventoryList.h"
#include "Scenes/InventoryReply.h"
#include "Scenes/IWorldEntryPublisher.h"
#include "Scenes/SelectionState.h"
#include "Scenes/WorldEntry.h"

#include <span>
#include <string>
#include <variant>

namespace Client::Scenes
{
namespace
{
// El 0x3d de la respuesta va a la sesión que la está aplicando.
class SessionPublisher final : public IWorldEntryPublisher
{
public:
    explicit SessionPublisher(App::ServerSession& session) : m_session(session) {}

    void PublishWorldEntry() override
    {
        m_session.PublishWorldEntry();
    }

private:
    App::ServerSession& m_session;
};
} // namespace

SessionEventApplier::SessionEventApplier(const WorldEntryResources& resources) : m_resources(resources) {}

Core::Expected<void, std::string> SessionEventApplier::ApplyCharacter(const App::CharacterEvent& event,
                                                                      const App::ServerSession&)
{
    return m_resources.selection.Apply(std::span(&event, 1), m_resources.camera, m_resources.cameraInput);
}

Core::Expected<void, std::string> SessionEventApplier::ApplyWorld(const App::WorldEvent& event,
                                                                  App::ServerSession& session)
{
    if (std::holds_alternative<App::ClearInventoryReceived>(event))
    {
        ClearForInventoryReply(m_resources);
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
