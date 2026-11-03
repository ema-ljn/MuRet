#pragma once

#include "Core/IClock.h"
#include "Scenes/SceneCanvas.h"

// El frame del lado de GL (CL5.4): en main.cpp, el Gl::GlFrameTarget (el GlSpriteRenderer, el
// texto y el swap de la ventana); en los tests, el RecordingFrameTarget de support/, que anota. El
// SceneManager abre un frame por Render, la escena dibuja en el lienzo y el frame se presenta.
namespace Client::Scenes
{
class IFrameTarget
{
public:
    virtual ~IFrameTarget() = default;
    // Limpia la pantalla y da el lienzo del frame, con el SpriteBatch vacío. Vale hasta Present.
    virtual SceneCanvas& BeginFrame(Core::Duration frameClock) = 0;
    // Dibuja lo que juntó el SpriteBatch y presenta: el swap va adentro de Render (ILoopClient.h).
    virtual void Present() = 0;
};
} // namespace Client::Scenes
