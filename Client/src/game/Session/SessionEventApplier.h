#pragma once

#include "Session/CharacterEvent.h"
#include "Session/ISessionEventSink.h"
#include "Session/WorldEvent.h"
#include "Core/Expected.h"
#include "Session/WorldEntry.h"

#include <cstddef>
#include <span>
#include <string>

namespace Client::App
{
class ServerSession;
}

namespace Client::Scenes
{
// Vive durante RunScenes, más que la sesión; aplica con cámara y terreno contemporáneos. Los eventos de
// personaje van a la selección, la respuesta del F3 03 a ApplyWorldEntry, el F3 10 a ApplyInventory, o sin
// cifrar a ClearForInventoryReply, el F1 02 con 1 a ApplyReturnToSelection y el 0F sólo a la meta
// durable del clima (sin avanzar su cuenta actual ni crear lluvia). D3/D7 van a los aplicadores de
// movimiento y daño, el 18 a ApplyActionReply con el C1 18 de sentarse atado a la sesión; 1C y F3 04 a las
// transiciones síncronas antes del próximo evento del cable.
class SessionEventApplier final : public App::ISessionEventSink
{
public:
    explicit SessionEventApplier(const WorldEntryResources& resources);

    Core::Expected<void, std::string> ApplyCharacter(const App::CharacterEvent& event,
                                                     const App::ServerSession& session) override;
    Core::Expected<void, std::string> ApplyWorld(const App::WorldEvent& event, App::ServerSession& session) override;
    // The dispatcher's copy to the queue (Actors::ActorsState::viewportPackets). Without the modules' states, an error.
    [[nodiscard]] Core::Expected<void, std::string> CopyToPacketRing(std::span<const std::byte> packet) override;
    void ClearLogoutRequest() override;
    [[nodiscard]] const WorldEntryResources* RoutedWorld() const override;
    // The orders the chat windows left for their rooms (a chat that was let go leaves its own).
    void AfterWorldReception(App::ServerSession& session) override;

private:
    Core::Expected<void, std::string> ApplyWorldEvent(const App::WorldEvent& event, App::ServerSession& session);
    Core::Expected<void, std::string> ApplyWorldContent(const App::WorldEvent& event, App::ServerSession& session);
    WorldEntryResources m_resources;
};
} // namespace Client::Scenes
