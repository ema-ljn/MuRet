#pragma once

#include "App/WindowState.h"
#include "Core/IClock.h"
#include "Render/ModelBatch.h"
#include "Render/ModelVertex.h"
#include "Render/Pass3D.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/TextureSlot.h"
#include "Scenes/IFrameTarget.h"
#include "Scenes/IWorldView.h"
#include "Scenes/SceneCanvas.h"
#include "Scenes/SceneWorlds.h"
#include "support/RecordingTextDrawer.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <utility>
#include <vector>

// El IFrameTarget de los tests de las escenas (CL5.4), en el lugar del Gl::GlFrameTarget de
// main.cpp: da un lienzo con un SpriteBatch de verdad, el RecordingTextDrawer y un mundo que anota,
// y guarda cada frame como una lista de lo que se dibujó, en el orden en que GL lo dibujaría: cada
// quad del lote (con su textura, su mezcla, su alpha test y sus vértices), cada texto (el quad de
// marca, con lo que pidió la escena), cada fin del 2D (flushSprites), cada pase 3D del mundo, con su
// pase, y cada fin del pase de los modelos (flushModels, CL6.3), con sus tramos y sus vértices. Lo
// del lote se anota al terminar un tramo: en un fin del 2D, en un pase 3D o en el Present. Un
// BeginFrame con el frame anterior abierto, un Present sin frame o con modelos sin su fin, hacen
// fallar el test.
namespace Client::Tests
{
struct FrameEvent
{
    enum class Kind : std::uint8_t
    {
        Quad,
        Text,
        Flush,
        World,
        Models,
    };
    Kind kind = Kind::Quad;
    // Quad y Text: el estado del tramo y los seis vértices del quad.
    std::optional<Render::TextureSlot> texture;
    Render::BlendMode blend = Render::BlendMode::Alpha;
    bool alphaTest = true;
    std::array<Render::SpriteVertex, 6> vertices{};
    // Text: lo que pidió la escena.
    RecordedText text;
    // World.
    Scenes::SceneWorld world{};
    float alpha = 0.0f;
    // World y Models: el pase.
    std::optional<Render::Pass3D> pass;
    // Models: los tramos y los vértices del lote.
    std::vector<Render::ModelDrawCommand> modelCommands;
    std::vector<Render::ModelVertex> modelVertices;
};

struct RecordedFrame
{
    Core::Duration clock{};
    bool presented = false;
    std::vector<FrameEvent> events;
};

class RecordingFrameTarget final : public Scenes::IFrameTarget
{
public:
    explicit RecordingFrameTarget(App::PixelSize screen = {.width = 640, .height = 480})
        : m_screen(screen), m_text(m_sprites), m_world([this](Scenes::SceneWorld world, const Render::Pass3D& pass,
                                                              float alpha) { RecordWorld(world, pass, alpha); })
    {
    }

    RecordingFrameTarget(const RecordingFrameTarget&) = delete;
    RecordingFrameTarget& operator=(const RecordingFrameTarget&) = delete;

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
        m_models.Begin(Render::Pass3D{}, false);
        m_synced = 0;
        m_canvas.emplace(Scenes::SceneCanvas{.sprites = m_sprites,
                                             .text = m_text,
                                             .world = m_world,
                                             .models = m_models,
                                             .flushSprites = [this] { Flush(); },
                                             .flushModels = [this] { FlushModels(); },
                                             .screen = m_screen,
                                             .frameClock = frameClock});
        return *m_canvas;
    }

    void Present() override
    {
        if (!FrameOpen())
        {
            ADD_FAILURE() << "Present sin BeginFrame";
            return;
        }
        Sync();
        if (!m_models.Vertices().empty())
        {
            ADD_FAILURE() << "Present con modelos sin flushModels";
        }
        frames.back().presented = true;
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

    // Los textos que pidieron las escenas, en orden, de todos los frames.
    [[nodiscard]] const std::vector<RecordedText>& Texts() const
    {
        return m_text.texts;
    }

    std::vector<RecordedFrame> frames;

private:
    // Un mundo que llama a `draw`.
    class WorldRecorder final : public Scenes::IWorldView
    {
    public:
        template <typename Draw> explicit WorldRecorder(Draw draw) : m_draw(std::move(draw)) {}

        void Draw(Scenes::SceneWorld world, const Render::Pass3D& pass, float alpha) override
        {
            m_draw(world, pass, alpha);
        }

    private:
        std::function<void(Scenes::SceneWorld, const Render::Pass3D&, float)> m_draw;
    };

    // Anota los quads del lote que no se anotaron todavía, en orden.
    void Sync()
    {
        const auto vertices = m_sprites.Vertices();
        for (const Render::DrawCommand& command : m_sprites.Commands())
        {
            for (std::size_t first = command.firstVertex; first < command.firstVertex + command.vertexCount; first += 6)
            {
                if (first < m_synced)
                {
                    continue;
                }
                FrameEvent event;
                event.texture = command.texture;
                event.blend = command.blend;
                event.alphaTest = command.alphaTest;
                for (std::size_t i = 0; i < event.vertices.size(); ++i)
                {
                    event.vertices[i] = vertices[first + i];
                }
                if (command.texture == TextMarkerSlot)
                {
                    event.kind = FrameEvent::Kind::Text;
                    if (m_textsSynced < m_text.texts.size())
                    {
                        event.text = m_text.texts[m_textsSynced];
                    }
                    ++m_textsSynced;
                }
                frames.back().events.push_back(event);
            }
        }
        m_synced = vertices.size();
    }

    // El fin del 2D: lo del lote se dibuja y el lote queda vacío, con su estado.
    void Flush()
    {
        if (!FrameOpen())
        {
            ADD_FAILURE() << "flushSprites sin frame";
            return;
        }
        Sync();
        FrameEvent flush;
        flush.kind = FrameEvent::Kind::Flush;
        frames.back().events.push_back(flush);
        m_sprites.Begin(m_screen);
        m_synced = 0;
    }

    // El fin del pase de los modelos: lo del lote se anota y el lote queda vacío, con el mismo pase.
    void FlushModels()
    {
        if (!FrameOpen())
        {
            ADD_FAILURE() << "flushModels sin frame";
            return;
        }
        Sync();
        FrameEvent event;
        event.kind = FrameEvent::Kind::Models;
        event.pass = m_models.Pass();
        event.modelCommands.assign(m_models.Commands().begin(), m_models.Commands().end());
        event.modelVertices.assign(m_models.Vertices().begin(), m_models.Vertices().end());
        frames.back().events.push_back(std::move(event));
        const auto state = m_models.State();
        m_models.Begin(m_models.Pass(), false);
        m_models.SetState(state);
    }

    void RecordWorld(Scenes::SceneWorld world, const Render::Pass3D& pass, float alpha)
    {
        if (!FrameOpen())
        {
            ADD_FAILURE() << "el pase 3D sin frame";
            return;
        }
        Sync();
        FrameEvent event;
        event.kind = FrameEvent::Kind::World;
        event.world = world;
        event.pass = pass;
        event.alpha = alpha;
        frames.back().events.push_back(event);
    }

    App::PixelSize m_screen;
    Render::SpriteBatch m_sprites;
    Render::ModelBatch m_models;
    RecordingTextDrawer m_text;
    WorldRecorder m_world;
    std::optional<Scenes::SceneCanvas> m_canvas;
    // Cuántos vértices del lote ya se anotaron, y cuántos textos.
    std::size_t m_synced = 0;
    std::size_t m_textsSynced = 0;
};
} // namespace Client::Tests
