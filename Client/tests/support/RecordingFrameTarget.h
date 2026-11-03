#pragma once

#include "App/WindowState.h"
#include "Core/IClock.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Scenes/IFrameTarget.h"
#include "Scenes/SceneCanvas.h"

#include <gtest/gtest.h>

#include <optional>
#include <utility>
#include <vector>

// El IFrameTarget de los tests de las escenas (CL5.4), en el lugar del Gl::GlFrameTarget de
// main.cpp: da un lienzo con un SpriteBatch de verdad y anota cada frame, con el reloj con que
// empezó, si se presentó y lo que juntó el lote hasta el Present. Un BeginFrame con el frame
// anterior abierto, o un Present sin frame, hacen fallar el test.
namespace Client::Tests
{
struct RecordedFrame
{
    Core::Duration clock{};
    bool presented = false;
    // Lo que tenía el lote en el Present.
    std::vector<Render::DrawCommand> commands;
    std::vector<Render::SpriteVertex> vertices;
};

class RecordingFrameTarget final : public Scenes::IFrameTarget
{
public:
    explicit RecordingFrameTarget(App::PixelSize screen = {.width = 640, .height = 480}) : m_screen(screen) {}

    Scenes::SceneCanvas& BeginFrame(Core::Duration frameClock) override
    {
        if (FrameOpen())
        {
            ADD_FAILURE() << "BeginFrame con el frame anterior sin Present";
        }
        RecordedFrame frame;
        frame.clock = frameClock;
        frames.push_back(std::move(frame));
        m_sprites.Begin(m_screen);
        m_canvas.emplace(Scenes::SceneCanvas{.sprites = m_sprites, .screen = m_screen, .frameClock = frameClock});
        return *m_canvas;
    }

    void Present() override
    {
        if (!FrameOpen())
        {
            ADD_FAILURE() << "Present sin BeginFrame";
            return;
        }
        RecordedFrame& frame = frames.back();
        frame.presented = true;
        frame.commands.assign(m_sprites.Commands().begin(), m_sprites.Commands().end());
        frame.vertices.assign(m_sprites.Vertices().begin(), m_sprites.Vertices().end());
    }

    // Si hay un frame entre su BeginFrame y su Present.
    [[nodiscard]] bool FrameOpen() const
    {
        return !frames.empty() && !frames.back().presented;
    }

    // El área de dibujo de los frames que siguen, como cuando cambia la ventana.
    void SetScreen(App::PixelSize screen)
    {
        m_screen = screen;
    }

    std::vector<RecordedFrame> frames;

private:
    App::PixelSize m_screen;
    Render::SpriteBatch m_sprites;
    std::optional<Scenes::SceneCanvas> m_canvas;
};
} // namespace Client::Tests
