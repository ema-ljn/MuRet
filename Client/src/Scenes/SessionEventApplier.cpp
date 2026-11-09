#include "Scenes/SessionEventApplier.h"

#include "App/CharacterEvent.h"
#include "App/WorldEvent.h"
#include "Protocol/Movement.h"
#include "Core/Expected.h"
#include "Scenes/SelectionState.h"
#include "Scenes/WorldEntry.h"
#include <variant>
#include "Protocol/Character/CharacterSelectResult.h"

#include <span>
#include <string>

namespace Client::Scenes
{
SessionEventApplier::SessionEventApplier(SelectionState& selection, World::GameCamera& camera,
                                         World::GameCameraInput& input)
    : m_selection(selection), m_camera(camera), m_input(input)
{
}

SessionEventApplier::SessionEventApplier(SelectionState& selection, World::GameCamera& camera,
                                         World::GameCameraInput& input, const SceneContext& context,
                                         const GameData& data, const GameModels& models)
    : m_context(context), m_data(&data), m_models(&models), m_selection(selection), m_camera(camera), m_input(input)
{
}

Core::Expected<void, std::string> SessionEventApplier::ApplyCharacter(const App::CharacterEvent& event,
                                                                      const App::ServerSession&)
{
    return m_selection.Apply(std::span(&event, 1), m_camera, m_input);
}

Core::Expected<void, std::string> SessionEventApplier::ApplyWorld(const App::WorldEvent& event,
                                                                  App::ServerSession& session)
{
    if (const auto* entry = std::get_if<Protocol::Character::CharacterSelectResult>(&event);
        entry != nullptr && m_context && m_data != nullptr && m_models != nullptr)
        return ApplyWorldEntry(*entry, m_selection, *m_context, *m_data, *m_models, session);
    if (const auto* inventory = std::get_if<Protocol::Character::InventoryList>(&event);
        inventory != nullptr && m_context && m_data != nullptr)
        return ApplyInventory(*inventory, m_selection, *m_context, *m_data);
    if (const auto* teleport = std::get_if<Protocol::World::Teleport>(&event);
        teleport != nullptr && m_context && m_data != nullptr && m_models != nullptr)
        return ApplyTeleport(*teleport, m_selection, *m_context, *m_data, *m_models, session);
    if (const auto* regen = std::get_if<Protocol::World::Regen>(&event);
        regen != nullptr && m_context && m_data != nullptr && m_models != nullptr)
        return ApplyRegen(*regen, m_selection, *m_context, *m_data, *m_models);
    if (std::holds_alternative<Protocol::Movement::Walk>(event))
        return Core::Unexpected(
            std::string("D3: falta aplicar 4c6790/5110d0 con el orden de accesos del actor canónico"));
    if (std::holds_alternative<Protocol::Movement::Damage>(event))
        return Core::Unexpected(std::string("D7: falta aplicar 4caae0/4f53a0 sobre vida y acción del actor canónico"));
    return Core::Unexpected(std::string("falta contexto de aplicación del evento de mundo"));
}
} // namespace Client::Scenes
