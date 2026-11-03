#pragma once

#include "Core/Log.h"
#include "Render/ITextureStore.h"

#include <filesystem>

// Lo que comparten las escenas (CL5.4). Lo arma main.cpp y vive más que el SceneManager; los tests,
// con un FakeTextureStore (support/). Lo que se leyó antes de la primera escena va aparte, en
// GameData.h.
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
} // namespace Client::Scenes
