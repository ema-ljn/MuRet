#pragma once

#include "Core/Expected.h"

#include <string>

namespace Client::App
{
class ServerSession;
}
namespace Client::Protocol::World
{
struct Teleport;
struct Regen;
} // namespace Client::Protocol::World
namespace Client::Protocol::Character
{
struct CharacterSelectResult;
struct InventoryList;
} // namespace Client::Protocol::Character
namespace Client::Scenes
{
class GameModels;
class SelectionState;
struct GameData;
struct SceneContext;
[[nodiscard]] Core::Expected<void, std::string> ApplyWorldEntry(const Protocol::Character::CharacterSelectResult& reply,
                                                                SelectionState& selection, const SceneContext& context,
                                                                const GameData& data, const GameModels& models,
                                                                App::ServerSession& session);
[[nodiscard]] Core::Expected<void, std::string> ApplyInventory(const Protocol::Character::InventoryList& reply,
                                                               SelectionState& selection, const SceneContext& context,
                                                               const GameData& data);
[[nodiscard]] Core::Expected<void, std::string> ApplyTeleport(const Protocol::World::Teleport& reply,
                                                              SelectionState& selection, const SceneContext& context,
                                                              const GameData& data, const GameModels& models,
                                                              App::ServerSession& session);
[[nodiscard]] Core::Expected<void, std::string> ApplyRegen(const Protocol::World::Regen& reply,
                                                           SelectionState& selection, const SceneContext& context,
                                                           const GameData& data, const GameModels& models);
} // namespace Client::Scenes
