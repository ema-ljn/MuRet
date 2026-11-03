#pragma once

#include "Core/Log.h"
#include "Data/Text/TextTable.h"
#include "Render/ITextureStore.h"

#include <filesystem>
#include <optional>

// Lo que comparten las escenas (CL5.4). Lo arma main.cpp y vive más que el SceneManager; los tests,
// con un FakeTextureStore (support/).
namespace Client::Scenes
{
// De dónde leen (el Data\ del cliente), adónde suben sus texturas (Render::ITextureStore, CL5.1) y
// el log.
struct SceneContext
{
    std::filesystem::path dataRoot;
    Render::ITextureStore& textures;
    Core::Log& log;
};

// Lo que se lee una vez y usan las escenas que siguen, como lo que WinMain carga antes de la
// primera escena (Text.bmd, en 0x4b9b9f).
struct GameData
{
    // Text.bmd (CL1): los textos se piden por índice.
    std::optional<Data::Text::TextTable> text;
};
} // namespace Client::Scenes
