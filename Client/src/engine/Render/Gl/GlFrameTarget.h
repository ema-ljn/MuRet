#pragma once

#include "Core/Expected.h"
#include "Core/IClock.h"
#include "Core/Log.h"
#include "Core/PixelSize.h"
#include "OpenGLApi.h"
#include "Platform/Sdl/GlLoader.h"
#include "Render/Gl/GlModelRenderer.h"
#include "Render/Gl/GlSpriteRenderer.h"
#include "Render/Gl/TextureTable.h"
#include "Render/ModelBatch.h"
#include "Render/SpriteBatch.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Render/Frame/IFrameTarget.h"
#include "Render/Frame/IWorldView.h"
#include "Render/Frame/SceneCanvas.h"
#include "Render/Frame/SceneText.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>

// BeginFrame clears to black, like the login's glClearColor(0, 0, 0, 1) and 0x65f200's color and depth glClear.
// Needs a live context and leaves no GL errors: the texture table uploads with a clean glGetError.
namespace Client::Render::Gl
{
class OPENGL_API GlFrameTarget final : public Frame::IFrameTarget
{
public:
    // The table and the log must outlive it.
    [[nodiscard]] static Core::Expected<std::unique_ptr<GlFrameTarget>, std::string>
    Create(const Platform::Sdl::GlFunctions& gl, TextureTable& textures, Frame::RasterizerFactory fonts,
           std::function<Core::PixelSize()> screen, std::function<void()> present, Core::Log& log);

    GlFrameTarget(const GlFrameTarget&) = delete;
    GlFrameTarget& operator=(const GlFrameTarget&) = delete;
    GlFrameTarget(GlFrameTarget&&) = delete;
    GlFrameTarget& operator=(GlFrameTarget&&) = delete;
    ~GlFrameTarget() override = default;

    Frame::SceneCanvas& BeginFrame(Core::Duration frameClock) override;
    void Present() override;

    // The text DC's GetTextExtentPointA outside drawing (the step and the network): SceneText's, with the last frame's
    // fonts. Lives as long as the frame target.
    [[nodiscard]] const Render::Text::ITextMeasure& TextMeasure() const;

private:
    GlFrameTarget(const Platform::Sdl::GlFunctions& gl, TextureTable& textures, GlSpriteRenderer renderer,
                  GlModelRenderer models, std::function<Core::PixelSize()> screen, std::function<void()> present,
                  Core::Log& log);

    // Draws the batch to the framebuffer and empties it, keeping its state.
    void FlushSprites();
    // Draws the 3D pass batch to the framebuffer and empties it, keeping the same pass.
    void FlushModels();

    const Platform::Sdl::GlFunctions* m_gl;
    TextureTable& m_textures;
    GlSpriteRenderer m_renderer;
    GlModelRenderer m_modelRenderer;
    std::function<Core::PixelSize()> m_screen;
    std::function<void()> m_present;
    Core::Log& m_log;
    SpriteBatch m_sprites;
    ModelBatch m_models;
    // After the batch, which its drawer holds. Not before the table, which belongs to main.cpp.
    std::unique_ptr<Frame::SceneText> m_text;
    Frame::EmptyWorldView m_world;
    std::optional<Frame::SceneCanvas> m_canvas;
};
} // namespace Client::Render::Gl
