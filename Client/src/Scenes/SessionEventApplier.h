#pragma once

#include "App/CharacterEvent.h"
#include "App/ISessionEventSink.h"
#include "App/WorldEvent.h"
#include "Core/Expected.h"
#include "Scenes/WorldEntry.h"

#include <optional>
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
// movimiento y daño; 1C y F3 04 a las transiciones síncronas antes del próximo evento del cable.
class SessionEventApplier final : public App::ISessionEventSink
{
public:
    explicit SessionEventApplier(const WorldEntryResources& resources);

    Core::Expected<void, std::string> ApplyCharacter(const App::CharacterEvent& event,
                                                     const App::ServerSession& session) override;
    Core::Expected<void, std::string> ApplyWorld(const App::WorldEvent& event, App::ServerSession& session) override;

private:
    Core::Expected<void, std::string> ApplyWorldContent(const App::WorldEvent& event, App::ServerSession& session);
    // Los otros jugadores en el viewport (ViewportPlayers.h). nullopt si `event` no es uno de ellos.
    std::optional<Core::Expected<void, std::string>> ApplyViewport(const App::WorldEvent& event);
    WorldEntryResources m_resources;
};
} // namespace Client::Scenes
