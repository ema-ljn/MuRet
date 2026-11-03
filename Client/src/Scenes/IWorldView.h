#pragma once

#include "Scenes/SceneWorlds.h"

// El pase 3D de una escena (CL5.4): el terreno, los objetos, los personajes y los efectos del
// mundo, en el viewport que dice el original (en el login, 0x620db0(0, 25, 640, 430), 06 § El
// login, el dibujo). Lo dibuja la CL7, para cualquier mapa; hasta entonces, EmptyWorldView, y donde
// iría el mundo queda el negro con que empieza el frame (pregunta 3 del plan de la CL5).
namespace Client::Scenes
{
class IWorldView
{
public:
    virtual ~IWorldView() = default;
    // `alpha` en [0, 1), el de ILoopClient::Render. Va después del fin del 2D (flushSprites), y lo
    // que dibuja la escena después va encima.
    virtual void Draw(SceneWorld world, float alpha) = 0;
};

// El de la CL5: no dibuja nada.
class EmptyWorldView final : public IWorldView
{
public:
    void Draw(SceneWorld /*world*/, float /*alpha*/) override {}
};
} // namespace Client::Scenes
