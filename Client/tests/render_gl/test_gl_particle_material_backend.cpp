#include "Render/ModelBatchParticleBackend.h"

#include "Core/Expected.h"
#include "Core/Log.h"
#include "Core/ILogSink.h"
#include "Data/Textures/Texture.h"
#include "Render/Gl/GlTexture.h"
#include <glm/ext/vector_float3.hpp>
#include <array>
#include "Platform/GlLoader.h"
#include "Render/Gl/GlModelRenderer.h"
#include "Render/Gl/TextureTable.h"
#include "Render/ModelBatch.h"
#include "Render/ModelBatchParticleState.h"
#include "Render/ModelVertex.h"
#include "Render/SpriteVertex.h"
#include "Render/Pass3D.h"
#include "Scenes/CharacterDrawing.h"
#include "Data/Models/Model.h"
#include "Render/Models/Skeleton.h"
#include "Render/TextureSlot.h"
#include "support/GlDrawTest.h"
#include "support/RecordingSink.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstddef>
#include <glm/ext/vector_float2.hpp>
#include <functional>
#include <span>
#include <optional>
#include <string>
#include <utility>
#include <system_error>
#include <vector>

namespace
{
class MaterialModels : public Client::Scenes::IModelLibrary
{
public:
    const Client::Data::Models::Model* Find(std::uint16_t) const override
    {
        return nullptr;
    }
    const Client::Render::Models::Skeleton* SkeletonOf(std::uint16_t) const override
    {
        return nullptr;
    }
    std::span<const Client::Render::TextureSlot> MeshTextures(std::uint16_t) const override
    {
        return {};
    }
};
// Spy limitado al sink de depthMask: registra argumentos y sigue al GL real del contexto.
const Client::Platform::GlFunctions* s_depthForward = nullptr;
std::vector<GLboolean> s_depthCalls;
void APIENTRY RecordDepthMask(GLboolean value)
{
    s_depthCalls.push_back(value);
    s_depthForward->depthMask(value);
}

const Client::Platform::GlFunctions* s_stateForward = nullptr;
std::vector<std::pair<GLint, GLint>> s_uniformCalls;
int s_drawCalls = 0;
void APIENTRY RecordUniform(GLint location, GLint value)
{
    s_uniformCalls.emplace_back(location, value);
    s_stateForward->uniform1i(location, value);
}
void APIENTRY RecordDraw(GLenum mode, GLint first, GLsizei count)
{
    ++s_drawCalls;
    s_stateForward->drawArrays(mode, first, count);
}

class GlParticleMaterialBackendTest : public Client::Tests::GlDrawTest
{
protected:
    std::optional<GLint> ReadUniform(const char* name)
    {
        PFNGLGETUNIFORMIVPROC read = nullptr;
        std::string error;
        if (!Client::Platform::LoadGlFunction(read, "glGetUniformiv", error))
            return std::nullopt;
        GLint program = 0;
        Gl().getIntegerv(GL_CURRENT_PROGRAM, &program);
        const GLint location = Gl().getUniformLocation(static_cast<GLuint>(program), name);
        if (location < 0)
            return std::nullopt;
        GLint value = -1;
        read(static_cast<GLuint>(program), location, &value);
        return value;
    }
};

// Detecta que el sink ejecute tail/helpers prematuramente o ignore comandos sin geometría.
TEST_F(GlParticleMaterialBackendTest, PrefixOnlyChangesBlendInRealGl)
{
    Client::Render::ModelBatch batch;
    batch.Begin(Client::Tests::TestPass({64, 64}));
    MaterialModels models;
    Client::Render::ModelBatchParticleBackend backend(std::ref(batch), std::cref(models),
                                                      Client::Render::ModelBatchParticleState::ForModelPass());
    ASSERT_TRUE(backend.SubtractivePrefix().HasValue());
    auto renderer = Client::Render::Gl::GlModelRenderer::Create(Gl());
    ASSERT_TRUE(renderer.HasValue());
    Client::Render::Gl::TextureTable textures(Gl());
    Client::Tests::RecordingSink sink;
    Client::Core::Log log(sink, Client::Core::LogLevel::Debug);
    renderer.Value().Draw(batch, textures, log);
    GLint value = 0;
    Gl().getIntegerv(GL_BLEND_SRC_RGB, &value);
    EXPECT_EQ(value, GL_ZERO);
    Gl().getIntegerv(GL_BLEND_DST_RGB, &value);
    EXPECT_EQ(value, GL_ONE_MINUS_SRC_COLOR);
    Gl().getIntegerv(GL_DEPTH_WRITEMASK, &value);
    EXPECT_EQ(value, GL_TRUE);
    Gl().getIntegerv(GL_CULL_FACE, &value);
    EXPECT_EQ(value, GL_TRUE);
    EXPECT_EQ(Gl().getError(), static_cast<GLenum>(GL_NO_ERROR));
}

// Detecta alpha raw0 que fuerce profundidad: el verde lejano debe poder sobrescribir al rojo.
TEST_F(GlParticleMaterialBackendTest, InheritedDisabledWriteAllowsLaterFarGeometry)
{
    Client::Render::ModelBatch batch;
    batch.Begin(Client::Tests::TestPass({64, 64}));
    MaterialModels models;
    Client::Render::ModelBatchParticleBackend backend(std::ref(batch), std::cref(models),
                                                      Client::Render::ModelBatchParticleState::ForModelPass());
    ASSERT_TRUE(backend.Additive().HasValue());
    ASSERT_TRUE(backend.AlphaInheritDepth().HasValue());
    Client::Tests::AddSquare(batch, batch.State(), 5, {1, 0, 0, 1});
    Client::Tests::AddSquare(batch, {}, 20, {0, 1, 0, 1});
    auto renderer = Client::Render::Gl::GlModelRenderer::Create(Gl());
    ASSERT_TRUE(renderer.HasValue());
    Client::Render::Gl::TextureTable textures(Gl());
    Client::Tests::RecordingSink sink;
    Client::Core::Log log(sink, Client::Core::LogLevel::Debug);
    renderer.Value().Draw(batch, textures, log);
    const auto cells = Cells(1, 1);
    ASSERT_TRUE(cells[0].has_value());
    EXPECT_EQ(cells[0]->red, 0);
    EXPECT_EQ(cells[0]->green, 255);
    EXPECT_EQ(Gl().getError(), static_cast<GLenum>(GL_NO_ERROR));
}

// Detecta usar BlendedShadow para raw0: ese perfil no descarta alpha<=0,25.
TEST_F(GlParticleMaterialBackendTest, RawZeroStillDiscardsLowAlphaFragments)
{
    Client::Render::ModelBatch batch;
    batch.Begin(Client::Tests::TestPass({64, 64}));
    MaterialModels models;
    Client::Render::ModelBatchParticleBackend backend(std::ref(batch), std::cref(models),
                                                      Client::Render::ModelBatchParticleState::ForModelPass());
    ASSERT_TRUE(backend.Additive().HasValue());
    ASSERT_TRUE(backend.AlphaInheritDepth().HasValue());
    Client::Tests::AddSquare(batch, batch.State(), 5, {1, 0, 0, 0.2f});
    auto renderer = Client::Render::Gl::GlModelRenderer::Create(Gl());
    ASSERT_TRUE(renderer.HasValue());
    Client::Render::Gl::TextureTable textures(Gl());
    Client::Tests::RecordingSink sink;
    Client::Core::Log log(sink, Client::Core::LogLevel::Debug);
    renderer.Value().Draw(batch, textures, log);
    const auto cells = Cells(1, 1);
    ASSERT_TRUE(cells[0].has_value());
    EXPECT_EQ(cells[0]->red, 0);
    EXPECT_EQ(cells[0]->alpha, 0);
    EXPECT_EQ(Gl().getError(), static_cast<GLenum>(GL_NO_ERROR));
}

// ZERO/ONE_MINUS_SRC_COLOR sobre blanco: 255*(1-c), con tolerancia de un byte por canal.
TEST_F(GlParticleMaterialBackendTest, SubtractiveChangesRealFramebufferChannels)
{
    Client::Render::ModelBatch batch;
    batch.Begin(Client::Tests::TestPass({64, 64}));
    Client::Tests::AddSquare(batch, {}, 20, {1, 1, 1, 1});
    MaterialModels models;
    Client::Render::ModelBatchParticleBackend backend(std::ref(batch), std::cref(models),
                                                      Client::Render::ModelBatchParticleState::ForModelPass());
    ASSERT_TRUE(backend.SubtractivePrefix().HasValue());
    ASSERT_TRUE(backend.Helper6208f0().HasValue());
    ASSERT_TRUE(backend.Helper6208b0().HasValue());
    ASSERT_TRUE(backend.SubtractiveTail().HasValue());
    Client::Tests::AddSquare(batch, batch.State(), 5, {0.25f, 0.5f, 0.75f, 1});
    auto renderer = Client::Render::Gl::GlModelRenderer::Create(Gl());
    ASSERT_TRUE(renderer.HasValue());
    Client::Render::Gl::TextureTable textures(Gl());
    Client::Tests::RecordingSink sink;
    Client::Core::Log log(sink, Client::Core::LogLevel::Debug);
    renderer.Value().Draw(batch, textures, log);
    const auto cells = Cells(1, 1);
    ASSERT_TRUE(cells[0].has_value());
    EXPECT_NEAR(cells[0]->red, 191, 1);
    EXPECT_NEAR(cells[0]->green, 128, 1);
    EXPECT_NEAR(cells[0]->blue, 64, 1);
    EXPECT_EQ(Gl().getError(), static_cast<GLenum>(GL_NO_ERROR));
}

// Recorre binder raw negativo, material tardío, Begin/End reales y textura real del shader.
TEST_F(GlParticleMaterialBackendTest, BackendQuadUsesExistingRendererAndRealTexture)
{
    Client::Data::Textures::Texture pixels{
        .width = 2,
        .height = 2,
        .textureWidth = 2,
        .textureHeight = 2,
        .format = Client::Data::Textures::PixelFormat::Rgba,
        .pixels = {200, 40, 80, 255, 20, 160, 240, 255, 60, 220, 120, 255, 240, 100, 200, 255}};
    auto texture = Client::Render::Gl::GlTexture::Upload(Gl(), pixels, {});
    ASSERT_TRUE(texture.HasValue());
    Client::Render::ModelBatch batch;
    batch.Begin(Client::Tests::TestPass({64, 64}));
    MaterialModels models;
    Client::Render::ModelBatchParticleBackend backend(std::ref(batch), std::cref(models),
                                                      Client::Render::ModelBatchParticleState::ForModelPass());
    ASSERT_TRUE(backend.BindTexture(-static_cast<std::int32_t>(texture.Value().Name())).HasValue());
    ASSERT_TRUE(backend.Additive().HasValue());
    ASSERT_TRUE(backend.AlphaInheritDepth().HasValue());
    ASSERT_TRUE(backend.Color4({0.5f, 0.75f, 0.25f, 1}).HasValue());
    ASSERT_TRUE(backend.Begin().HasValue());
    const std::array<glm::vec3, 4> corners{glm::vec3{-5, -5, -5}, glm::vec3{5, -5, -5}, glm::vec3{5, 5, -5},
                                           glm::vec3{-5, 5, -5}};
    const std::array<glm::vec2, 4> uv{glm::vec2{0, 0}, glm::vec2{1, 0}, glm::vec2{1, 1}, glm::vec2{0, 1}};
    for (std::size_t i = 0; i < corners.size(); ++i)
    {
        ASSERT_TRUE(backend.TexCoord(uv[i]).HasValue());
        ASSERT_TRUE(backend.Vertex(corners[i]).HasValue());
    }
    ASSERT_TRUE(backend.End().HasValue());
    auto renderer = Client::Render::Gl::GlModelRenderer::Create(Gl());
    ASSERT_TRUE(renderer.HasValue());
    Client::Render::Gl::TextureTable textures(Gl());
    Client::Tests::RecordingSink sink;
    Client::Core::Log log(sink, Client::Core::LogLevel::Debug);
    renderer.Value().Draw(batch, textures, log);
    const auto cells = Cells(2, 2);
    // Filas de pantalla: superior primero; bytes de textura: inferior primero.
    const std::array<std::array<int, 3>, 4> expected{std::array{30, 165, 30}, std::array{120, 75, 50},
                                                     std::array{100, 30, 20}, std::array{10, 120, 60}};
    for (std::size_t i = 0; i < expected.size(); ++i)
    {
        ASSERT_TRUE(cells[i].has_value());
        EXPECT_NEAR(cells[i]->red, expected[i][0], 1);
        EXPECT_NEAR(cells[i]->green, expected[i][1], 1);
        EXPECT_NEAR(cells[i]->blue, expected[i][2], 1);
    }
    EXPECT_EQ(Gl().getError(), static_cast<GLenum>(GL_NO_ERROR));
}

TEST_F(GlParticleMaterialBackendTest, HelperWritesDepthMaskOnceAndHitsDoNotRepeatSink)
{
    Client::Render::ModelBatch batch;
    batch.Begin(Client::Tests::TestPass({64, 64}));
    MaterialModels models;
    Client::Render::ModelBatchParticleBackend backend(std::ref(batch), std::cref(models),
                                                      Client::Render::ModelBatchParticleState::ForModelPass());
    ASSERT_TRUE(backend.SubtractivePrefix().HasValue());
    ASSERT_TRUE(backend.Helper6208b0().HasValue());
    ASSERT_TRUE(backend.Helper6208b0().HasValue());
    auto functions = Gl();
    functions.depthMask = RecordDepthMask;
    auto renderer = Client::Render::Gl::GlModelRenderer::Create(functions);
    ASSERT_TRUE(renderer.HasValue());
    Client::Render::Gl::TextureTable textures(Gl());
    Client::Tests::RecordingSink sink;
    Client::Core::Log log(sink, Client::Core::LogLevel::Debug);
    s_depthForward = &Gl();
    s_depthCalls.clear();
    renderer.Value().Draw(batch, textures, log);
    s_depthForward = nullptr;
    const std::vector<GLboolean> expected{GL_TRUE, GL_FALSE}; // BeginPass, luego6208b0; sin tail.
    EXPECT_EQ(s_depthCalls, expected);
    EXPECT_EQ(Gl().getError(), static_cast<GLenum>(GL_NO_ERROR));
}

// Sin Draw que pueda reparar Binding: el nombre que dejó Upload se cambia a0 antes del sink.
TEST_F(GlParticleMaterialBackendTest, BindingOnlyRestoresPoisonedRealGlBinding)
{
    Client::Data::Textures::Texture pixels{.width = 1,
                                           .height = 1,
                                           .textureWidth = 1,
                                           .textureHeight = 1,
                                           .format = Client::Data::Textures::PixelFormat::Rgba,
                                           .pixels = {23, 91, 177, 255}};
    auto texture = Client::Render::Gl::GlTexture::Upload(Gl(), pixels, {});
    ASSERT_TRUE(texture.HasValue());
    Client::Render::ModelBatch batch;
    batch.Begin(Client::Tests::TestPass({64, 64}));
    MaterialModels models;
    Client::Render::ModelBatchParticleBackend backend(std::ref(batch), std::cref(models),
                                                      Client::Render::ModelBatchParticleState::ForModelPass());
    const auto serial = batch.StateSerial();
    ASSERT_TRUE(backend.BindTexture(-static_cast<std::int32_t>(texture.Value().Name())).HasValue());
    EXPECT_EQ(batch.StateSerial(), serial + 1);
    ASSERT_EQ(batch.Commands().size(), 1);
    EXPECT_EQ(batch.Commands()[0].operation, Client::Render::ModelStateOperation::Binding);
    EXPECT_TRUE(batch.Vertices().empty());
    auto renderer = Client::Render::Gl::GlModelRenderer::Create(Gl());
    ASSERT_TRUE(renderer.HasValue());
    Client::Render::Gl::TextureTable textures(Gl());
    Client::Tests::RecordingSink sink;
    Client::Core::Log log(sink, Client::Core::LogLevel::Debug);
    Gl().bindTexture(GL_TEXTURE_2D, 0);
    GLint before = -1;
    Gl().getIntegerv(GL_TEXTURE_BINDING_2D, &before);
    ASSERT_EQ(before, 0);
    renderer.Value().Draw(batch, textures, log);
    GLint after = -1;
    Gl().getIntegerv(GL_TEXTURE_BINDING_2D, &after);
    EXPECT_EQ(after, static_cast<GLint>(texture.Value().Name()));
    EXPECT_EQ(Gl().getError(), static_cast<GLenum>(GL_NO_ERROR));
}

void ClipRectangle(Client::Render::ModelBatch& batch, Client::Render::ModelDrawState state, float left, float right,
                   Client::Render::Color color)
{
    const std::array<Client::Render::ModelVertex, 4> v{Client::Render::ModelVertex{{left, -0.25f, 0}, {0, 0}, color},
                                                       Client::Render::ModelVertex{{right, -0.25f, 0}, {0, 0}, color},
                                                       Client::Render::ModelVertex{{right, 0.25f, 0}, {0, 0}, color},
                                                       Client::Render::ModelVertex{{left, 0.25f, 0}, {0, 0}, color}};
    batch.SetState(state);
    batch.Triangle({v[0], v[1], v[2]});
    batch.Triangle({v[0], v[2], v[3]});
}

// Clip→píxel: (x+1)*32. View traslada x+0,5 sólo a World: [20,28], Eye[4,12], World[52,60].
TEST_F(GlParticleMaterialBackendTest, NonidentityViewSeparatesWorldEyeWorldAcrossPartialCommands)
{
    Client::Render::Pass3D pass;
    pass.viewport = {0, 0, 64, 64};
    pass.view[3][0] = 0.5f;
    Client::Render::ModelBatch batch;
    batch.Begin(pass);
    Client::Render::ModelDrawState world;
    world.depthTest = false;
    ClipRectangle(batch, world, -0.875f, -0.625f, {1, 0, 0, 1});
    MaterialModels models;
    Client::Render::ModelBatchParticleBackend backend(std::ref(batch), std::cref(models),
                                                      Client::Render::ModelBatchParticleState::ForModelPass());
    ASSERT_TRUE(backend.AlphaInheritDepth().HasValue());
    auto eye = batch.State();
    eye.space = Client::Render::ModelCoordinateSpace::Eye;
    ClipRectangle(batch, eye, -0.875f, -0.625f, {0, 1, 0, 1});
    ASSERT_TRUE(backend.Helper6208b0().HasValue());
    auto lastWorld = batch.State();
    lastWorld.space = Client::Render::ModelCoordinateSpace::World;
    ClipRectangle(batch, lastWorld, 0.125f, 0.375f, {0, 0, 1, 1});
    auto renderer = Client::Render::Gl::GlModelRenderer::Create(Gl());
    ASSERT_TRUE(renderer.HasValue());
    Client::Render::Gl::TextureTable textures(Gl());
    Client::Tests::RecordingSink sink;
    Client::Core::Log log(sink, Client::Core::LogLevel::Debug);
    renderer.Value().Draw(batch, textures, log);
    const auto pixels = m_framebuffer->ReadPixels();
    EXPECT_EQ(pixels[32 * 64 + 8], (Client::Render::Gl::Rgba8{0, 255, 0, 255}));
    EXPECT_EQ(pixels[32 * 64 + 24], (Client::Render::Gl::Rgba8{255, 0, 0, 255}));
    EXPECT_EQ(pixels[32 * 64 + 56], (Client::Render::Gl::Rgba8{0, 0, 255, 255}));
    EXPECT_EQ(pixels[32 * 64 + 40], (Client::Render::Gl::Rgba8{0, 0, 0, 0}));
    EXPECT_EQ(Gl().getError(), static_cast<GLenum>(GL_NO_ERROR));
}

TEST_F(GlParticleMaterialBackendTest, AlphaAndTextureStateOnlyReachRealShaderWithoutDraw)
{
    Client::Render::ModelBatch batch;
    batch.Begin(Client::Tests::TestPass({64, 64}));
    MaterialModels models;
    Client::Render::ModelBatchParticleBackend backend(std::ref(batch), std::cref(models),
                                                      Client::Render::ModelBatchParticleState::ForModelPass());
    ASSERT_TRUE(backend.AlphaInheritDepth().HasValue());
    EXPECT_TRUE(batch.Vertices().empty());
    auto renderer = Client::Render::Gl::GlModelRenderer::Create(Gl());
    ASSERT_TRUE(renderer.HasValue());
    Client::Render::Gl::TextureTable textures(Gl());
    Client::Tests::RecordingSink sink;
    Client::Core::Log log(sink, Client::Core::LogLevel::Debug);
    renderer.Value().Draw(batch, textures, log);
    EXPECT_EQ(ReadUniform("alphaTest"), 1);
    EXPECT_EQ(ReadUniform("textured"), 1);
    EXPECT_EQ(Gl().getError(), static_cast<GLenum>(GL_NO_ERROR));
}

TEST_F(GlParticleMaterialBackendTest, SubtractiveFogErrorStillExecutesReachedStateOnlyTail)
{
    Client::Render::ModelBatch batch;
    batch.Begin(Client::Tests::TestPass({64, 64}));
    auto initialDraw = batch.State();
    initialDraw.alphaTest = true;
    batch.SetState(initialDraw);
    auto initial = Client::Render::ModelBatchParticleState::ForModelPass();
    initial.alphaTest = 0x80;
    initial.fogPolicy = 7;
    MaterialModels models;
    Client::Render::ModelBatchParticleBackend backend(std::ref(batch), std::cref(models), initial);
    ASSERT_TRUE(backend.SubtractivePrefix().HasValue());
    ASSERT_TRUE(backend.Helper6208f0().HasValue());
    ASSERT_TRUE(backend.Helper6208b0().HasValue());
    const auto result = backend.SubtractiveTail();
    ASSERT_FALSE(result.HasValue());
    EXPECT_EQ(result.Error(), std::make_error_code(std::errc::function_not_supported));
    ASSERT_EQ(batch.Commands().size(), 5);
    EXPECT_TRUE(batch.Vertices().empty());
    auto functions = Gl();
    functions.uniform1i = RecordUniform;
    functions.drawArrays = RecordDraw;
    auto renderer = Client::Render::Gl::GlModelRenderer::Create(functions);
    ASSERT_TRUE(renderer.HasValue());
    Client::Render::Gl::TextureTable textures(Gl());
    Client::Tests::RecordingSink sink;
    Client::Core::Log log(sink, Client::Core::LogLevel::Debug);
    s_stateForward = &Gl();
    s_uniformCalls.clear();
    s_drawCalls = 0;
    renderer.Value().Draw(batch, textures, log);
    s_stateForward = nullptr;
    GLint program = 0;
    Gl().getIntegerv(GL_CURRENT_PROGRAM, &program);
    const GLint alphaLocation = Gl().getUniformLocation(static_cast<GLuint>(program), "alphaTest");
    const GLint textureLocation = Gl().getUniformLocation(static_cast<GLuint>(program), "textured");
    const std::vector<std::pair<GLint, GLint>> expected{{alphaLocation, 0}, {textureLocation, 1}};
    EXPECT_EQ(s_uniformCalls, expected);
    EXPECT_EQ(s_drawCalls, 0);
    EXPECT_EQ(ReadUniform("alphaTest"), 0);
    EXPECT_EQ(ReadUniform("textured"), 1);
    GLint write = -1;
    Gl().getIntegerv(GL_DEPTH_WRITEMASK, &write);
    EXPECT_EQ(write, GL_FALSE);
    Gl().getIntegerv(GL_CULL_FACE, &write);
    EXPECT_EQ(write, GL_FALSE);
    EXPECT_EQ(Gl().getError(), static_cast<GLenum>(GL_NO_ERROR));
}
} // namespace
