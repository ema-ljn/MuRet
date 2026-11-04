#pragma once

#include "App/WindowState.h"
#include "Core/IClock.h"
#include "Render/ModelBatch.h"
#include "Render/SpriteBatch.h"
#include "Scenes/ITextDrawer.h"
#include "Scenes/IWorldView.h"

#include <functional>

// Lo que una escena necesita para dibujar un frame sin ver GL (CL5.4). Lo da el IFrameTarget en
// BeginFrame y vale hasta su Present.
namespace Client::Scenes
{
struct SceneCanvas
{
    // El 2D del frame, vacío (SpriteBatch::Begin) y con el estado que dejó el frame anterior
    // (Q-C75).
    Render::SpriteBatch& sprites;
    // Los textos, en el mismo lote, con el color y la mezcla que haya.
    ITextDrawer& text;
    // El pase 3D (IWorldView.h).
    IWorldView& world;
    // Los modelos de un pase 3D (CL6.3): la escena lo empieza con su pase (ModelBatch::Begin) y le
    // manda los triángulos de sus personajes, en el mundo.
    Render::ModelBatch& models;
    // El fin del 2D (0x621a90): dibuja lo que juntó `sprites` y lo vacía, sin tocar su estado. Va
    // antes del pase 3D, que dibuja aparte: sin él, lo del lote saldría encima del mundo.
    std::function<void()> flushSprites;
    // El fin del pase 3D: dibuja lo que juntó `models` con el estado de 0x620db0 y lo vacía. Va
    // después de los modelos y antes del 2D que va encima; el 2D apaga la profundidad (CL6.3).
    std::function<void()> flushModels;
    // El área de dibujo en píxeles: el W y el H del original.
    App::PixelSize screen;
    // El reloj del frame: el Now() del IClock del bucle al empezarlo. En Main es el de SDL, que
    // cuenta desde que arrancó MuRet: con él se mueve la arena del login (Q-C65).
    Core::Duration frameClock{};
};
} // namespace Client::Scenes
