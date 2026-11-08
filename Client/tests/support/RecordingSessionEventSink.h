#pragma once

#include "App/CharacterEvent.h"
#include "App/ISessionEventSink.h"
#include "App/ServerSession.h"
#include "App/SessionState.h"
#include "App/WorldEvent.h"
#include "Core/Expected.h"

#include <functional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

// Copia explícita de los argumentos prestados; el collector, no la sesión, conserva las recepciones.
namespace Client::Tests
{
class RecordingSessionEventSink final : public App::ISessionEventSink
{
public:
    Core::Expected<void, std::string> ApplyCharacter(const App::CharacterEvent& event,
                                                     const App::ServerSession& session) override
    {
        characters.push_back(event);
        characterStates.push_back(session.State());
        trace.push_back(std::holds_alternative<App::CharacterListReceived>(event) ? "list"
                        : std::holds_alternative<App::CharacterCreated>(event)    ? "create"
                                                                                  : "delete");
        return onCharacter ? onCharacter(event, session) : Core::Expected<void, std::string>{};
    }

    Core::Expected<void, std::string> ApplyWorld(const App::WorldEvent& event, App::ServerSession& session) override
    {
        world.push_back(event);
        worldStates.push_back(session.State());
        trace.push_back(event.index() == 0 ? "entry" : "inventory");
        return onWorld ? onWorld(event, session) : Core::Expected<void, std::string>{};
    }

    std::vector<App::CharacterEvent> TakeCharacterEvents()
    {
        return std::exchange(characters, {});
    }
    std::vector<App::WorldEvent> TakeWorldEvents()
    {
        return std::exchange(world, {});
    }

    std::vector<App::CharacterEvent> characters;
    std::vector<App::WorldEvent> world;
    std::vector<App::SessionState> characterStates;
    std::vector<App::SessionState> worldStates;
    std::vector<std::string> trace;
    std::function<Core::Expected<void, std::string>(const App::CharacterEvent&, const App::ServerSession&)> onCharacter;
    std::function<Core::Expected<void, std::string>(const App::WorldEvent&, App::ServerSession&)> onWorld;
};
} // namespace Client::Tests
