#include "Scenes/SessionEventApplier.h"

#include "App/CharacterEvent.h"
#include "App/WorldEvent.h"
#include "Core/Expected.h"
#include "Scenes/SelectionState.h"

#include <span>
#include <string>

namespace Client::Scenes
{
SessionEventApplier::SessionEventApplier(SelectionState& selection, World::GameCamera& camera,
                                         World::GameCameraInput& input)
    : m_selection(selection), m_camera(camera), m_input(input)
{
}

Core::Expected<void, std::string> SessionEventApplier::ApplyCharacter(const App::CharacterEvent& event,
                                                                      const App::ServerSession&)
{
    return m_selection.Apply(std::span(&event, 1), m_camera, m_input);
}

Core::Expected<void, std::string> SessionEventApplier::ApplyWorld(const App::WorldEvent&, App::ServerSession&)
{
    // Límite local de C1: la recepción no certifica carga, héroe ni equipo nativos.
    // Esta rama no se publica hasta integrar el aplicador real y su punto state3d.
    return Core::Unexpected(std::string("falta integrar el aplicador de mundo nativo (F3/03 y F3/10)"));
}
} // namespace Client::Scenes
