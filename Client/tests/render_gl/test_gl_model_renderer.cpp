#include "Render/Gl/GlModelRenderer.h"

#include "App/WindowState.h"
#include "Core/Expected.h"
#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Data/Textures/Texture.h"
#include "Data/Textures/TextureSet.h"
#include "Platform/GlLoader.h"
#include "Render/Gl/GlFramebuffer.h"
#include "Render/Gl/GlSpriteRenderer.h"
#include "Render/Gl/TextureTable.h"
#include "Render/ModelBatch.h"
#include "Render/ModelVertex.h"
#include "Render/OriginalQuad.h"
#include "Render/Pass3D.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/TextureSlot.h"

#include "support/GlDrawTest.h"
#include "support/RecordingSink.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// Client::Render::Gl::GlModelRenderer: el ModelBatch de un pase en GL, con el estado de 0x620db0
// (el viewport, la perspectiva, el depth test con GL_LEQUAL, el culling y la profundidad escrita) y
// el de la mezcla de cada tramo (06 § El modelo en el binario). La cámara y el cuadrado son los de
// GlDrawTest.h: el cuadrado llena el framebuffer de 64 × 64.
namespace
{
using Client::App::PixelSize;
using Client::Data::Textures::LoadedTexture;
using Client::Data::Textures::PixelFormat;
using Client::Data::Textures::Texture;
using Client::Render::Camera3D;
using Client::Render::Color;
using Client::Render::MakePass3D;
using Client::Render::ModelBatch;
using Client::Render::ModelBlend;
using Client::Render::ModelDrawState;
using Client::Render::ModelVertex;
using Client::Render::OriginalQuad;
using Client::Render::Pass3D;
using Client::Render::SpriteBatch;
using Client::Render::TextureSlot;
using Client::Render::ViewportRect;
using Client::Render::Gl::GlModelRenderer;
using Client::Render::Gl::GlSpriteRenderer;
using Client::Render::Gl::Rgba8;
using Client::Render::Gl::TextureTable;
using Client::Tests::FramebufferSide;
using Client::Tests::TestPass;

constexpr Rgba8 Transparent{0, 0, 0, 0};
constexpr Rgba8 Red{255, 0, 0, 255};
constexpr Rgba8 Green{0, 255, 0, 255};

constexpr Color RedColor{1.0f, 0.0f, 0.0f, 1.0f};
constexpr Color GreenColor{0.0f, 1.0f, 0.0f, 1.0f};

// Un téxel blanco y uno gris al 50 %. Ninguno es de una escena del original.
constexpr TextureSlot WhiteSlot{0x1};
constexpr TextureSlot GraySlot{0x2};
// Uno que la tabla no tiene.
constexpr TextureSlot MissingSlot{0x2a};

constexpr PixelSize Screen{FramebufferSide, FramebufferSide};

LoadedTexture Rgba(TextureSlot slot, std::vector<std::uint8_t> pixel)
{
    return {slot.value,
            {},
            Texture{.width = 1,
                    .height = 1,
                    .textureWidth = 1,
                    .textureHeight = 1,
                    .format = PixelFormat::Rgba,
                    .pixels = std::move(pixel)}};
}

// Si la celda es `expected`, con uno de tolerancia por canal: la mezcla en 8 bits redondea como
// quiere el driver.
void ExpectNear(const std::optional<Rgba8>& cell, Rgba8 expected)
{
    ASSERT_TRUE(cell.has_value());
    const auto withinOne = [](std::uint8_t actual, std::uint8_t wanted) { return std::abs(actual - wanted) <= 1; };
    EXPECT_TRUE(withinOne(cell->red, expected.red) && withinOne(cell->green, expected.green) &&
                withinOne(cell->blue, expected.blue) && withinOne(cell->alpha, expected.alpha))
        << ::testing::PrintToString(*cell) << " contra " << ::testing::PrintToString(expected);
}

class GlModelRendererTest : public Client::Tests::GlDrawTest
{
protected:
    void SetUp() override
    {
        GlDrawTest::SetUp();
        if (IsSkipped() || HasFatalFailure())
        {
            return;
        }
        Client::Core::Expected<GlModelRenderer, std::string> renderer = GlModelRenderer::Create(Gl());
        ASSERT_TRUE(renderer.HasValue()) << renderer.Error();
        m_models.emplace(std::move(renderer).Value());
        m_textures.emplace(Gl());
        const std::array<LoadedTexture, 2> textures{Rgba(WhiteSlot, {255, 255, 255, 255}),
                                                    Rgba(GraySlot, {128, 128, 128, 255})};
        const Client::Core::Expected<void, std::string> loaded = m_textures->Load(textures);
        ASSERT_TRUE(loaded.HasValue()) << loaded.Error();
        m_batch.Begin(TestPass(Screen));
    }

    // El cuadrado de GlDrawTest.h en el lote.
    void AddSquare(const ModelDrawState& state, float depth, Color color, bool backFacing = false)
    {
        Client::Tests::AddSquare(m_batch, state, depth, color, backFacing);
    }

    void DrawBatch()
    {
        m_models->Draw(m_batch, *m_textures, m_log);
        ASSERT_EQ(Gl().getError(), static_cast<GLenum>(GL_NO_ERROR));
    }

    // La celda de una grilla de 2 × 2 sobre el framebuffer, con la fila de arriba primero.
    [[nodiscard]] std::optional<Rgba8> Cell(int index) const
    {
        return Cells(2, 2)[static_cast<std::size_t>(index)];
    }

    std::optional<GlModelRenderer> m_models;
    std::optional<TextureTable> m_textures;
    ModelBatch m_batch;
    Client::Tests::RecordingSink m_sink;
    Client::Core::Log m_log{m_sink, Client::Core::LogLevel::Debug};
};

// Con el depth test de 0x620db0 queda el más cerca en cada píxel, aunque llegue primero: el rojo a
// 5 tapa al verde a 20 dibujado después.
TEST_F(GlModelRendererTest, DrawsATriangleWithDepth)
{
    AddSquare({WhiteSlot, ModelBlend::Opaque, true}, 5.0f, RedColor);
    AddSquare({WhiteSlot, ModelBlend::Opaque, true}, 20.0f, GreenColor);

    DrawBatch();

    for (int cell = 0; cell < 4; ++cell)
    {
        ExpectNear(Cell(cell), Red);
    }
}

// La bandera 0x2000: sin depth test, lo que va después tapa.
TEST_F(GlModelRendererTest, TheDepthTestCanBeOff)
{
    AddSquare({WhiteSlot, ModelBlend::Opaque, true}, 5.0f, RedColor);
    AddSquare({WhiteSlot, ModelBlend::Opaque, false}, 20.0f, GreenColor);

    DrawBatch();

    ExpectNear(Cell(0), Green);
}

// 0x620980 deja el culling prendido: la cara de atrás no sale. 0x620a00(1) lo apaga.
TEST_F(GlModelRendererTest, CullsTheBackFaces)
{
    AddSquare({WhiteSlot, ModelBlend::Opaque, true}, 10.0f, RedColor, true);
    DrawBatch();
    EXPECT_EQ(Cell(0), Transparent);

    m_batch.Begin(TestPass(Screen));
    AddSquare({WhiteSlot, ModelBlend::Alpha, true}, 10.0f, GreenColor, true);
    DrawBatch();
    ExpectNear(Cell(0), Green);
}

// 0x620a90 no escribe la profundidad: el verde opaco a 20, dibujado después, no queda tapado por el
// rojo aditivo a 5, y lo pisa.
TEST_F(GlModelRendererTest, TheAdditiveDoesNotWriteTheDepth)
{
    AddSquare({WhiteSlot, ModelBlend::Additive, true}, 5.0f, RedColor);
    AddSquare({WhiteSlot, ModelBlend::Opaque, true}, 20.0f, GreenColor);

    DrawBatch();

    ExpectNear(Cell(0), Green);
}

// La sombra (0x620980 y la máscara apagada de 0x4eff40) no mezcla ni escribe la profundidad: lo que
// va detrás y después se ve.
TEST_F(GlModelRendererTest, TheShadowDoesNotWriteTheDepth)
{
    AddSquare({std::nullopt, ModelBlend::Shadow, true}, 5.0f, RedColor);
    AddSquare({WhiteSlot, ModelBlend::Opaque, true}, 20.0f, GreenColor);

    DrawBatch();

    ExpectNear(Cell(0), Green);
}

// La sombra de un objeto del mundo (0x6002e0 con el caballo: 0x620a00(1) y la máscara apagada de
// 0x4eff40) mezcla con el alfa del vértice: el negro al 0,7 sobre el blanco deja 0,3.
TEST_F(GlModelRendererTest, TheBlendedShadowMixesWithItsAlpha)
{
    AddSquare({WhiteSlot, ModelBlend::Opaque, true}, 20.0f, Color{1.0f, 1.0f, 1.0f, 1.0f});
    AddSquare({std::nullopt, ModelBlend::BlendedShadow, true}, 5.0f, Color{0.0f, 0.0f, 0.0f, 0.7f});

    DrawBatch();

    ExpectNear(Cell(0), Rgba8{77, 77, 77, 201});
}

// Y no escribe la profundidad: el verde opaco a 20, dibujado después, la pisa.
TEST_F(GlModelRendererTest, TheBlendedShadowDoesNotWriteTheDepth)
{
    AddSquare({std::nullopt, ModelBlend::BlendedShadow, true}, 5.0f, Color{0.0f, 0.0f, 0.0f, 0.7f});
    AddSquare({WhiteSlot, ModelBlend::Opaque, true}, 20.0f, GreenColor);

    DrawBatch();

    ExpectNear(Cell(0), Green);
}

// Y sin alpha test: 0x4eff40 lo apaga (0x620910(0)), y la sombra del mapa 7 va en 0,2. El negro al 0,2
// sobre el blanco deja 0,8.
TEST_F(GlModelRendererTest, TheBlendedShadowHasNoAlphaTest)
{
    AddSquare({WhiteSlot, ModelBlend::Opaque, true}, 20.0f, Color{1.0f, 1.0f, 1.0f, 1.0f});
    AddSquare({std::nullopt, ModelBlend::BlendedShadow, true}, 5.0f, Color{0.0f, 0.0f, 0.0f, 0.2f});

    DrawBatch();

    ExpectNear(Cell(0), Rgba8{204, 204, 204, 214});
}

// Ni recorta las caras de atrás (0x620a00 apaga el culling): una de atrás, roja al 0,7 sobre el vacío,
// se ve.
TEST_F(GlModelRendererTest, TheBlendedShadowDoesNotCull)
{
    AddSquare({std::nullopt, ModelBlend::BlendedShadow, true}, 10.0f, Color{1.0f, 0.0f, 0.0f, 0.7f}, true);

    DrawBatch();

    ExpectNear(Cell(0), Rgba8{179, 0, 0, 125});
}

// La opaca no mezcla: un alfa de 0,5 se escribe tal cual, sin el fondo.
TEST_F(GlModelRendererTest, TheOpaqueIgnoresTheAlpha)
{
    AddSquare({WhiteSlot, ModelBlend::Opaque, true}, 10.0f, Color{1.0f, 0.0f, 0.0f, 0.5f});

    DrawBatch();

    ExpectNear(Cell(0), Rgba8{255, 0, 0, 128});
}

// La función fija recorta el color del vértice a [0, 1] antes de la textura: 1,34 por el gris al
// 50 % da 128, no 171.
TEST_F(GlModelRendererTest, ClampsTheColorToOne)
{
    AddSquare({GraySlot, ModelBlend::Opaque, true}, 10.0f, Color{1.34f, 1.34f, 1.34f, 1.0f});
    DrawBatch();
    ExpectNear(Cell(0), Rgba8{128, 128, 128, 255});

    m_batch.Begin(TestPass(Screen));
    AddSquare({std::nullopt, ModelBlend::Opaque, true}, 10.0f, Color{1.34f, 0.0f, 0.0f, 1.0f});
    DrawBatch();
    ExpectNear(Cell(0), Red);
}

// El glAlphaFunc(GL_GREATER, 0,25) de 0x620a00: un alfa de 0,25 no pasa, uno de 0,3 sí.
TEST_F(GlModelRendererTest, TheAlphaTestCutsAtAQuarter)
{
    AddSquare({WhiteSlot, ModelBlend::Alpha, true}, 10.0f, Color{1.0f, 1.0f, 1.0f, 0.25f});
    DrawBatch();
    EXPECT_EQ(Cell(0), Transparent);

    m_batch.Begin(TestPass(Screen));
    AddSquare({WhiteSlot, ModelBlend::Alpha, true}, 10.0f, Color{1.0f, 1.0f, 1.0f, 0.3f});
    DrawBatch();
    EXPECT_NE(Cell(0), Transparent);
}

// El viewport es el del pase: el rectángulo de arriba a la izquierda de 640 × 480 llena solo esa
// cuarta parte.
TEST_F(GlModelRendererTest, UsesTheViewportOfThePass)
{
    m_batch.Begin(TestPass(Screen, ViewportRect{0, 0, 320, 240}));
    AddSquare({WhiteSlot, ModelBlend::Opaque, true}, 10.0f, RedColor);

    DrawBatch();

    ExpectNear(Cell(0), Red);
    EXPECT_EQ(Cell(1), Transparent);
    EXPECT_EQ(Cell(2), Transparent);
    EXPECT_EQ(Cell(3), Transparent);
}

// El 2D que sigue al pase apaga el depth test y el culling, como 0x6219b0 → 0x620870: el quad del
// SpriteBatch sale entero aunque el pase dejó escrita una profundidad más cerca que la suya.
TEST_F(GlModelRendererTest, TheTwoDAfterTurnsTheDepthOff)
{
    AddSquare({WhiteSlot, ModelBlend::Opaque, true}, 1.1f, RedColor);
    DrawBatch();
    Client::Core::Expected<GlSpriteRenderer, std::string> sprites = GlSpriteRenderer::Create(Gl());
    ASSERT_TRUE(sprites.HasValue()) << sprites.Error();
    SpriteBatch batch;
    batch.Begin(Screen);
    batch.SetColor(Color{0.0f, 1.0f, 0.0f, 1.0f});
    batch.Quad(OriginalQuad{.slot = WhiteSlot,
                            .x = 0.0f,
                            .y = 0.0f,
                            .width = static_cast<float>(FramebufferSide),
                            .height = static_cast<float>(FramebufferSide)});

    sprites.Value().Draw(batch, *m_textures, m_log);
    ASSERT_EQ(Gl().getError(), static_cast<GLenum>(GL_NO_ERROR));

    for (int cell = 0; cell < 4; ++cell)
    {
        ExpectNear(Cell(cell), Green);
    }
}

// Q-C29: un slot que nunca tuvo textura ata la textura por defecto, y la malla sale sin textura,
// con el color de sus vértices. El slot sale una vez en el log como aviso.
TEST_F(GlModelRendererTest, AMissingSlotDrawsUntexturedWithAWarning)
{
    AddSquare({MissingSlot, ModelBlend::Opaque, true}, 10.0f, GreenColor);

    DrawBatch();
    DrawBatch();

    ExpectNear(Cell(0), Green);
    std::size_t warnings = 0;
    for (const std::string& line : m_sink.lines)
    {
        warnings += line.find("0x2a") != std::string::npos ? 1u : 0u;
    }
    EXPECT_EQ(warnings, 1u);
}
} // namespace
