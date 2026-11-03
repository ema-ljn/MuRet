#include "Scenes/CharacterSelectScene.h"

namespace Client::Scenes
{
Core::Expected<void, std::string> CharacterSelectScene::Enter()
{
    return {};
}

void CharacterSelectScene::Step(const Input::InputState& /*input*/) {}

void CharacterSelectScene::Render(SceneCanvas& /*canvas*/, float /*alpha*/) {}

void CharacterSelectScene::Leave() {}

std::optional<SceneId> CharacterSelectScene::Next() const
{
    return std::nullopt;
}
} // namespace Client::Scenes
