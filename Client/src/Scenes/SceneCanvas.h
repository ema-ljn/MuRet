#pragma once

#include "App/WindowState.h"
#include "Core/IClock.h"
#include "Render/SpriteBatch.h"

// Lo que una escena necesita para dibujar un frame sin ver GL (CL5.4). Lo da el IFrameTarget en
// BeginFrame y vale hasta su Present.
namespace Client::Scenes
{
struct SceneCanvas
{
    // El 2D del frame, vacío (SpriteBatch::Begin) y con el estado que dejó el frame anterior
    // (Q-C75).
    Render::SpriteBatch& sprites;
    // El área de dibujo en píxeles: el W y el H del original.
    App::PixelSize screen;
    // El reloj del frame: el Now() del IClock del bucle al empezarlo. En Main es el de SDL, que
    // cuenta desde que arrancó MuRet: con él se mueve la arena del login (Q-C65).
    Core::Duration frameClock{};
};
} // namespace Client::Scenes
