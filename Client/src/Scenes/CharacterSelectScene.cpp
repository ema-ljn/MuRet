#include "Scenes/CharacterSelectScene.h"

#include "Core/Expected.h"
#include "Input/InputState.h"
#include "Scenes/SceneCanvas.h"
#include "Scenes/SceneId.h"

#include <optional>
#include <string>

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

bool CharacterSelectScene::WantsToQuit() const
{
    return false;
}

bool CharacterSelectScene::WantsTextInput() const
{
    return false;
}

bool CharacterSelectScene::HoldsTheLastFrame() const
{
    return false;
}
} // namespace Client::Scenes
