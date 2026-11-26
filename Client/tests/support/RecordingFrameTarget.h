#pragma once

#include "Core/IClock.h"
#include "Core/PixelSize.h"
#include "Render/Frame/SceneWorld.h"
#include "Render/ModelBatch.h"
#include "Render/ModelVertex.h"
#include "Render/Pass3D.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/TextureSlot.h"
#include "Render/Frame/IFrameTarget.h"
#include "Render/Frame/IWorldView.h"
#include "Render/Frame/SceneCanvas.h"
#include "support/RecordingTextDrawer.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <utility>
#include <vector>

// The scene tests' IFrameTarget, in place of main.cpp's Gl::GlFrameTarget: it gives a canvas with a real SpriteBatch,
// the RecordingTextDrawer and a recording world, and stores each frame as a list of what was drawn, in the order GL
// would draw it: each batch quad (with its texture, blending, alpha test and vertices), each text (the marker quad,
// with what the scene asked for), each 2D end (flushSprites), each world 3D pass, with its pass, and each end of the
// models pass (flushModels), with its runs and vertices. The batch is recorded when a run ends: at a 2D end, at a 3D
// pass or at Present. A BeginFrame with the previous frame open, or a Present without a frame or with models without
// their end, fail the test.
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
    // Quad and Text: the run's state and the quad's six vertices.
    std::optional<Render::TextureSlot> texture;
    Render::BlendMode blend = Render::BlendMode::Alpha;
    bool alphaTest = true;
    std::array<Render::SpriteVertex, 6> vertices{};
    // Text: what the scene asked for.
    RecordedText text;
    // World.
    Render::Frame::SceneWorld world{};
    float alpha = 0.0f;
    // World and Models: the pass.
    std::optional<Render::Pass3D> pass;
    // Models: the batch's runs and vertices.
    std::vector<Render::ModelDrawCommand> modelCommands;
    std::vector<Render::ModelVertex> modelVertices;
};

struct RecordedFrame
{
    Core::Duration clock{};
    bool presented = false;
    std::vector<FrameEvent> events;
};

class RecordingFrameTarget final : public Render::Frame::IFrameTarget
{
public:
    explicit RecordingFrameTarget(Core::PixelSize screen = {.width = 640, .height = 480})
        : m_screen(screen), m_text(m_sprites),
          m_world([this](Render::Frame::SceneWorld world, const Render::Pass3D& pass, float alpha)
                  { RecordWorld(world, pass, alpha); })
    {
    }

    RecordingFrameTarget(const RecordingFrameTarget&) = delete;
    RecordingFrameTarget& operator=(const RecordingFrameTarget&) = delete;

    Render::Frame::SceneCanvas& BeginFrame(Core::Duration frameClock) override
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
        m_canvas.emplace(Render::Frame::SceneCanvas{.sprites = m_sprites,
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

    // Whether a frame is between its BeginFrame and its Present.
    [[nodiscard]] bool FrameOpen() const
    {
        return !frames.empty() && !frames.back().presented;
    }

    // The drawing area of the following frames, as when the window changes.
    void SetScreen(Core::PixelSize screen)
    {
        m_screen = screen;
    }

    // The texts the scenes asked for, in order, from all frames.
    [[nodiscard]] const std::vector<RecordedText>& Texts() const
    {
        return m_text.texts;
    }

    std::vector<RecordedFrame> frames;

private:
    // A world that calls `draw`.
    class WorldRecorder final : public Render::Frame::IWorldView
    {
    public:
        template <typename Draw> explicit WorldRecorder(Draw draw) : m_draw(std::move(draw)) {}

        void Draw(Render::Frame::SceneWorld world, const Render::Pass3D& pass, float alpha) override
        {
            m_draw(world, pass, alpha);
        }

    private:
        std::function<void(Render::Frame::SceneWorld, const Render::Pass3D&, float)> m_draw;
    };

    // Records the batch quads not recorded yet, in order.
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

    // The end of the 2D: the batch is drawn and left empty, with its state.
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

    // The end of the models pass: the batch is recorded and left empty, with the same pass.
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

    void RecordWorld(Render::Frame::SceneWorld world, const Render::Pass3D& pass, float alpha)
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

    Core::PixelSize m_screen;
    Render::SpriteBatch m_sprites;
    Render::ModelBatch m_models;
    RecordingTextDrawer m_text;
    WorldRecorder m_world;
    std::optional<Render::Frame::SceneCanvas> m_canvas;
    // How many batch vertices were already recorded, and how many texts.
    std::size_t m_synced = 0;
    std::size_t m_textsSynced = 0;
};
} // namespace Client::Tests
