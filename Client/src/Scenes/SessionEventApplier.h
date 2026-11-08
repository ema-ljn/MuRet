#pragma once

#include "App/CharacterEvent.h"
#include "App/ISessionEventSink.h"
#include "App/WorldEvent.h"
#include "Core/Expected.h"

#include <string>

namespace Client::App
{
class ServerSession;
}
namespace Client::World
{
struct GameCamera;
struct GameCameraInput;
} // namespace Client::World
namespace Client::Scenes
{
class SelectionState;

// Vive durante RunScenes, más que la sesión; aplica con cámara y terreno contemporáneos.
class SessionEventApplier final : public App::ISessionEventSink
{
public:
    SessionEventApplier(SelectionState& selection, World::GameCamera& camera, World::GameCameraInput& input);
    Core::Expected<void, std::string> ApplyCharacter(const App::CharacterEvent& event,
                                                     const App::ServerSession& session) override;
    Core::Expected<void, std::string> ApplyWorld(const App::WorldEvent& event, App::ServerSession& session) override;

private:
    SelectionState& m_selection;
    World::GameCamera& m_camera;
    World::GameCameraInput& m_input;
};
} // namespace Client::Scenes
