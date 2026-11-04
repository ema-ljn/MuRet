#pragma once

#include "Core/IClock.h"
#include "Core/Log.h"
#include "Render/ITextureStore.h"

#include <filesystem>

// Lo que comparten las escenas (CL5.4). Lo arma main.cpp y vive más que el SceneManager; los tests,
// con un FakeTextureStore (support/). Lo que se leyó antes de la primera escena va aparte, en
// GameData.h.
namespace Client::Scenes
{
// De dónde leen (el Data\ del cliente), adónde suben sus texturas (Render::ITextureStore, CL5.1), el
// log y el reloj del bucle (CL6.4).
struct SceneContext
{
    std::filesystem::path dataRoot;
    Render::ITextureStore& textures;
    Core::Log& log;
    // El de SceneManager: lo que el original lee de DAT_05767b88 en la lógica, como la acción del
    // personaje del login (0x634c60). nullptr sin bucle (los tests que no lo miran): la escena hace como
    // si no lo hubiera.
    Core::IClock* clock = nullptr;
};
} // namespace Client::Scenes
