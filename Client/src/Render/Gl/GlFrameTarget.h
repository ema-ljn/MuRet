#pragma once

#include "App/WindowState.h"
#include "Core/Expected.h"
#include "Core/IClock.h"
#include "Core/Log.h"
#include "Platform/GlLoader.h"
#include "Render/Gl/GlModelRenderer.h"
#include "Render/Gl/GlSpriteRenderer.h"
#include "Render/Gl/TextureTable.h"
#include "Render/ModelBatch.h"
#include "Render/SpriteBatch.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Scenes/IFrameTarget.h"
#include "Scenes/IWorldView.h"
#include "Scenes/SceneCanvas.h"
#include "Scenes/SceneText.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>

// El IFrameTarget de main.cpp (CL5.4): el frame de una escena en GL. BeginFrame limpia el
// framebuffer en negro, como el glClearColor(0, 0, 0, 1) del login y el glClear de color y
// profundidad de 0x65f200, y da el lienzo con los dos lotes vacíos; el fin del 2D (flushSprites), el
// del pase 3D (flushModels, CL6.3) y el Present dibujan lo juntado con el GlSpriteRenderer, el
// GlModelRenderer y la TextureTable de las escenas, y el Present además presenta (el swap de la
// ventana). El texto es el de SceneText, con las fuentes del ancho
// de cada frame, y el mundo, el que no dibuja (EmptyWorldView) hasta la CL7.
//
// Necesita el contexto vivo y no deja errores de GL: la tabla de texturas sube con glGetError
// limpio.
namespace Client::Render::Gl
{
class GlFrameTarget final : public Scenes::IFrameTarget
{
public:
    // `screen` da el área de dibujo de cada frame, y `present` presenta. La tabla y el log tienen
    // que vivir más. El error, si un renderer no compila o las fuentes del ancho de ahora no abren.
    [[nodiscard]] static Core::Expected<std::unique_ptr<GlFrameTarget>, std::string>
    Create(const Platform::GlFunctions& gl, TextureTable& textures, Scenes::RasterizerFactory fonts,
           std::function<App::PixelSize()> screen, std::function<void()> present, Core::Log& log);

    GlFrameTarget(const GlFrameTarget&) = delete;
    GlFrameTarget& operator=(const GlFrameTarget&) = delete;
    GlFrameTarget(GlFrameTarget&&) = delete;
    GlFrameTarget& operator=(GlFrameTarget&&) = delete;
    ~GlFrameTarget() override = default;

    Scenes::SceneCanvas& BeginFrame(Core::Duration frameClock) override;
    void Present() override;

    // El GetTextExtentPointA del DC de los textos fuera del dibujo (el paso y la red): el de SceneText, con las
    // fuentes del ancho del último frame. Vive lo que el frame.
    [[nodiscard]] const Render::Text::ITextMeasure& TextMeasure() const;

private:
    GlFrameTarget(const Platform::GlFunctions& gl, TextureTable& textures, GlSpriteRenderer renderer,
                  GlModelRenderer models, std::function<App::PixelSize()> screen, std::function<void()> present,
                  Core::Log& log);

    // Lo que juntó el lote, al framebuffer, y el lote vacío con su estado.
    void FlushSprites();
    // Lo que juntó el lote del pase 3D, al framebuffer, y el lote vacío con el mismo pase.
    void FlushModels();

    const Platform::GlFunctions* m_gl;
    TextureTable& m_textures;
    GlSpriteRenderer m_renderer;
    GlModelRenderer m_modelRenderer;
    std::function<App::PixelSize()> m_screen;
    std::function<void()> m_present;
    Core::Log& m_log;
    SpriteBatch m_sprites;
    ModelBatch m_models;
    // Después del lote: su drawer lo guarda. Antes de la tabla no, que es de main.cpp.
    std::unique_ptr<Scenes::SceneText> m_text;
    Scenes::EmptyWorldView m_world;
    std::optional<Scenes::SceneCanvas> m_canvas;
};
} // namespace Client::Render::Gl
