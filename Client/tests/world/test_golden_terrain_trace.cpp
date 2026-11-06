#include "World/TerrainCells.h"
#include "World/GameCamera.h"
#include "World/TerrainCulling.h"
#include "World/WorldTerrain.h"

#include "Data/Textures/TextureSet.h"
#include "App/WindowState.h"
#include "Render/ModelBatch.h"
#include "Render/ModelVertex.h"
#include "Render/Pass3D.h"
#include "Render/TextureSlot.h"

#include "Scenes/SceneCameras.h"

#include "support/GlTrace.h"
#include "support/GoldenDir.h"

#include <gtest/gtest.h>

#include <glm/common.hpp>
#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_float4.hpp>
#include <glm/geometric.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Client::World::DrawTerrainCell contra el terreno de la traza de Lorencia (MURET_CLIENT_GL_TRACE_DIR,
// 06 § Terreno): cada fan de cuatro vértices del pase del terreno, con su geometría, sus UV, su
// textura, su alfa y su luz, contra lo que MuRet arma para esa celda con el Data\ de World1.
namespace
{
using Client::Render::ModelBatch;
using Client::Render::ModelBlend;
using Client::Render::ModelDrawCommand;
using Client::Tests::GlTraceBlock;
using Client::World::DrawTerrainCell;
using Client::World::TerrainView;
using Client::World::TerrainWater;

constexpr std::string_view LorenciaTrace = "lorencia-Frame_016215.txt";
constexpr std::uint8_t Lorencia = 0;
// En esa sesión, el tile i es la textura de GL 1351 + i (06 § Terreno); lo que no es un tile, en el
// pase de alfa, es el slot 0x122 (Q-C19).
constexpr std::uint32_t FirstTileName = 1351;
constexpr std::uint32_t TileCount = 14;
// La traza escribe seis decimales.
constexpr float Tolerance = 0.001f;
// La luz de un vértice coincide si cada canal está a menos del 1 % (06 § Terreno), donde el rojo pasa
// de 8 / 255.
constexpr float LightTolerance = 0.01f;
constexpr float MeasurableRed = 8.0f / 255.0f;

// Un fan del terreno: cuatro vértices en las esquinas de una celda de 100 × 100, con el estado de uno
// de los dos pases.
bool IsTerrainFan(const GlTraceBlock& block)
{
    if (block.mode != "GL_TRIANGLE_FAN" || block.vertices.size() != 4 || !block.state.depthTest)
    {
        return false;
    }
    const bool opaque = !block.state.blend && block.state.cullFace && !block.state.alphaTest;
    const bool alpha = block.state.blend && block.state.blendSource == "GL_SRC_ALPHA" && block.state.alphaTest &&
                       !block.state.cullFace;
    if (!opaque && !alpha)
    {
        return false;
    }
    const glm::vec3 origin = block.vertices[0].position;
    if (std::fmod(origin.x, 100.0f) != 0.0f || std::fmod(origin.y, 100.0f) != 0.0f)
    {
        return false;
    }
    const std::array<glm::vec2, 4> offsets{glm::vec2(0, 0), glm::vec2(100, 0), glm::vec2(100, 100), glm::vec2(0, 100)};
    for (std::size_t k = 0; k < 4; ++k)
    {
        const glm::vec3 p = block.vertices[k].position;
        if (p.x - origin.x != offsets[k].x || p.y - origin.y != offsets[k].y)
        {
            return false;
        }
    }
    return true;
}

// Los cuatro vértices de un tramo del lote: los del fan (0, 1, 2) y (0, 2, 3).
std::array<Client::Render::ModelVertex, 4> FanOf(const ModelBatch& batch, const ModelDrawCommand& command)
{
    const auto vertices = batch.Vertices().subspan(command.firstVertex, command.vertexCount);
    return {vertices[0], vertices[1], vertices[2], vertices[5]};
}

// Las celdas opacas de un frame de la traza, en su orden.
std::vector<std::pair<int, int>> TracedCells(const Client::Tests::GlTraceFrame& frame)
{
    std::vector<std::pair<int, int>> cells;
    for (const GlTraceBlock& block : frame.blocks)
    {
        if (IsTerrainFan(block) && !block.state.blend)
        {
            cells.emplace_back(static_cast<int>(block.vertices[0].position.x / 100.0f),
                               static_cast<int>(block.vertices[0].position.y / 100.0f));
        }
    }
    return cells;
}

// Las celdas que MuRet dibuja, en su orden: el primer pase de cada una.
std::vector<std::pair<int, int>> DrawnCells(const ModelBatch& batch)
{
    std::vector<std::pair<int, int>> cells;
    for (const ModelDrawCommand& command : batch.Commands())
    {
        if (command.state.blend != ModelBlend::Opaque)
        {
            continue;
        }
        for (std::size_t first = 0; first < command.vertexCount; first += 6)
        {
            const glm::vec3 origin = batch.Vertices()[command.firstVertex + first].position;
            cells.emplace_back(static_cast<int>(origin.x / 100.0f), static_cast<int>(origin.y / 100.0f));
        }
    }
    return cells;
}

// La primera vista de Lorencia usa esta cámara antes de los pases del terreno.
// Así el golden de cada celda también observa la cámara calculada.
void ExpectCameraInTrace(const Client::Tests::GlTraceFrame& frame, const Client::Render::Camera3D& camera)
{
    std::array<const Client::Tests::GlTraceMatrixCall*, 4> calls{};
    std::size_t count = 0;
    for (const auto& call : frame.matrices)
    {
        if (call.name == "glRotatef" || call.name == "glTranslatef")
        {
            calls[count++] = &call;
            if (count == calls.size())
                break;
        }
    }
    ASSERT_EQ(count, calls.size());
    const std::array<std::array<float, 4>, 3> rotations{{{camera.angles.y, 0.0f, 1.0f, 0.0f},
                                                         {camera.angles.x, 1.0f, 0.0f, 0.0f},
                                                         {camera.angles.z, 0.0f, 0.0f, 1.0f}}};
    for (std::size_t k = 0; k < rotations.size(); ++k)
    {
        ASSERT_EQ(calls[k]->name, "glRotatef");
        ASSERT_EQ(calls[k]->values.size(), rotations[k].size());
        for (std::size_t component = 0; component < rotations[k].size(); ++component)
            EXPECT_NEAR(rotations[k][component], calls[k]->values[component], Tolerance);
    }
    ASSERT_EQ(calls.back()->name, "glTranslatef");
    ASSERT_EQ(calls.back()->values.size(), 3u);
    for (int k = 0; k < 3; ++k)
        EXPECT_NEAR(-camera.position[k], calls.back()->values[static_cast<std::size_t>(k)], Tolerance);
}

class GoldenTerrainTrace : public Client::Tests::GoldenAndGlTraceTest
{
};

TEST_F(GoldenTerrainTrace, TheCellsOfLorenciaMatchTheTrace)
{
    const auto path = TraceFile(LorenciaTrace);
    if (!path)
    {
        return;
    }
    auto trace = Client::Tests::ReadGlTrace(*path);
    ASSERT_TRUE(trace.HasValue()) << trace.Error();
    ASSERT_FALSE(trace.Value().frames.empty());
    auto terrain = Client::World::LoadWorldTerrain(m_dataRoot, Lorencia);
    ASSERT_TRUE(terrain.HasValue()) << terrain.Error();

    Client::World::GameCamera cameraState;
    const Client::World::GameCameraInput cameraInput{.hero = {14350.0f, 12050.0f, 165.0f}, .cellAttribute = 0x01};
    const auto camera = Client::World::StepGameCamera(cameraState, cameraInput);
    ExpectCameraInTrace(trace.Value().frames.front(), camera);
    const auto worldPass = Client::Render::MakePass3D(Client::App::PixelSize{1024, 768}, {0, 0, 640, 432}, camera);
    TerrainView view{.mapping = terrain.Value().mapping.get(),
                     .heights = terrain.Value().heights.get(),
                     .attributes = terrain.Value().attributes.get(),
                     .lighting = terrain.Value().lighting.get()};
    std::vector<Client::World::WorldTileTexture> textures = Client::World::WorldTileTextures(Lorencia);
    textures.push_back(Client::World::AcceptBoxTexture());
    for (const Client::World::WorldTileTexture& texture : textures)
    {
        const std::array<Client::Data::Textures::TextureSetEntry, 1> entry{
            {{texture.path, texture.slot, texture.sampling}}};
        const auto loaded = Client::Data::Textures::LoadTextureSet(m_dataRoot, entry);
        if (loaded.HasValue())
        {
            const auto& t = loaded.Value().front().texture;
            view.tiles[static_cast<std::size_t>(texture.slot - Client::World::FirstTileSlot) % 256] = {
                static_cast<float>(t.textureWidth), static_cast<float>(t.textureHeight)};
        }
    }

    std::size_t opaque = 0;
    std::size_t alpha = 0;
    std::size_t acceptBox = 0;
    std::size_t matching = 0;
    std::size_t brighter = 0;
    std::size_t darker = 0;
    const auto& blocks = trace.Value().frames[0].blocks;
    for (std::size_t b = 0; b < blocks.size(); ++b)
    {
        if (!IsTerrainFan(blocks[b]) || blocks[b].state.blend)
        {
            continue;
        }
        // Un pase opaco, y el de alfa de su celda si viene enseguida.
        std::vector<const GlTraceBlock*> traced{&blocks[b]};
        if (b + 1 < blocks.size() && IsTerrainFan(blocks[b + 1]) && blocks[b + 1].state.blend &&
            blocks[b + 1].vertices[0].position == blocks[b].vertices[0].position)
        {
            traced.push_back(&blocks[b + 1]);
        }
        const auto x = static_cast<std::uint8_t>(blocks[b].vertices[0].position.x / 100.0f);
        const auto y = static_cast<std::uint8_t>(blocks[b].vertices[0].position.y / 100.0f);
        const std::string where =
            "celda (" + std::to_string(x) + ", " + std::to_string(y) + "), línea " + std::to_string(blocks[b].line);
        ModelBatch batch;
        batch.Begin(worldPass);
        DrawTerrainCell(batch, view, TerrainWater{}, x, y);
        ASSERT_EQ(batch.Commands().size(), traced.size()) << where;
        for (std::size_t pass = 0; pass < traced.size(); ++pass)
        {
            const GlTraceBlock& block = *traced[pass];
            const ModelDrawCommand& command = batch.Commands()[pass];
            const bool isAlpha = block.state.blend;
            (isAlpha ? alpha : opaque) += 1;
            EXPECT_EQ(command.state.blend, isAlpha ? ModelBlend::Alpha : ModelBlend::Opaque) << where;
            ASSERT_TRUE(command.state.texture.has_value()) << where;
            if (block.texture >= FirstTileName && block.texture < FirstTileName + TileCount)
            {
                EXPECT_EQ(command.state.texture->value, Client::World::FirstTileSlot + (block.texture - FirstTileName))
                    << where;
            }
            else
            {
                EXPECT_TRUE(isAlpha) << where;
                EXPECT_EQ(command.state.texture->value, 0x122) << where;
                ++acceptBox;
            }
            const auto ours = FanOf(batch, command);
            for (std::size_t k = 0; k < 4; ++k)
            {
                const auto& theirs = block.vertices[k];
                const std::string vertex = where + " pase " + std::to_string(pass) + " vértice " + std::to_string(k);
                EXPECT_LT(glm::distance(ours[k].position, theirs.position), Tolerance) << vertex;
                EXPECT_LT(glm::distance(ours[k].uv, theirs.texCoord), Tolerance) << vertex;
                EXPECT_NEAR(ours[k].color.alpha, theirs.color.a, Tolerance) << vertex;
                if (theirs.color.r <= MeasurableRed)
                {
                    continue;
                }
                const glm::vec3 light(ours[k].color.red, ours[k].color.green, ours[k].color.blue);
                const glm::vec3 traceLight(theirs.color);
                const glm::vec3 ratio = traceLight / glm::max(light, glm::vec3(1e-6f));
                if (std::abs(ratio.r - 1.0f) <= LightTolerance && std::abs(ratio.g - 1.0f) <= LightTolerance &&
                    std::abs(ratio.b - 1.0f) <= LightTolerance)
                {
                    ++matching;
                }
                else if (ratio.r > 1.0f && ratio.g > 1.0f && ratio.b > 1.0f)
                {
                    ++brighter;
                }
                else
                {
                    ++darker;
                    ADD_FAILURE() << vertex << ": la luz de la traza es más oscura que la de MuRet";
                }
            }
        }
    }
    // 06 § Terreno: 329 pases opacos y 141 de alfa, 3 con el slot 0x122; la luz coincide en 1.746 de los
    // 1.875 vértices con el rojo por encima de 8 / 255, y los otros 129 tienen luces dinámicas encima.
    EXPECT_EQ(opaque, 329u);
    EXPECT_EQ(alpha, 141u);
    EXPECT_EQ(acceptBox, 3u);
    EXPECT_EQ(matching, 1746u);
    EXPECT_EQ(brighter, 129u);
    EXPECT_EQ(darker, 0u);
}
// El recorte (CL7.3 A): con la cámara calculada desde el héroe en (14.350; 12.050;
// 165), el rango de 0x5fbb00, el trapecio de 0x5fb570 y el bit 0x08 dejan las 329 celdas de la traza,
// en su orden.
TEST_F(GoldenTerrainTrace, LorenciaDrawsTheCellsOfTheTraceInItsOrder)
{
    const auto path = TraceFile(LorenciaTrace);
    if (!path)
    {
        return;
    }
    auto trace = Client::Tests::ReadGlTrace(*path);
    ASSERT_TRUE(trace.HasValue()) << trace.Error();
    auto terrain = Client::World::LoadWorldTerrain(m_dataRoot, Lorencia);
    ASSERT_TRUE(terrain.HasValue()) << terrain.Error();
    Client::World::GameCamera cameraState;
    const Client::World::GameCameraInput cameraInput{.hero = {14350.0f, 12050.0f, 165.0f}, .cellAttribute = 0x01};
    const auto camera = Client::World::StepGameCamera(cameraState, cameraInput);
    ASSERT_FALSE(trace.Value().frames.empty());
    ExpectCameraInTrace(trace.Value().frames.front(), camera);
    const auto pass = Client::Render::MakePass3D(Client::App::PixelSize{1024, 768}, {0, 0, 640, 432}, camera);
    const auto range = Client::World::TerrainRangeOf(pass.view, camera);
    const auto polygon =
        Client::World::TerrainPolygonOf(Client::World::CullingScene::Game, Lorencia, cameraInput.hero, camera.angles.z);
    const TerrainView view{.mapping = terrain.Value().mapping.get(),
                           .heights = terrain.Value().heights.get(),
                           .attributes = terrain.Value().attributes.get(),
                           .lighting = terrain.Value().lighting.get()};
    ModelBatch batch;
    batch.Begin(pass);
    Client::World::DrawTerrain(batch, view, range, polygon, TerrainWater{});

    const auto traced = TracedCells(trace.Value().frames[0]);
    ASSERT_EQ(traced.size(), 329u);
    EXPECT_EQ(DrawnCells(batch), traced);
}

// Y la selección (el mapa 54, World55), donde el trapecio sí recorta: con SelectionCamera y el héroe
// en (12.975; 15.423; 333), las 5.109 celdas de su traza.
TEST_F(GoldenTerrainTrace, TheSelectionDrawsTheCellsOfItsTrace)
{
    const auto path = TraceFile("character-selection-Frame_003207.txt");
    if (!path)
    {
        return;
    }
    auto trace = Client::Tests::ReadGlTrace(*path);
    ASSERT_TRUE(trace.HasValue()) << trace.Error();
    constexpr std::uint8_t Selection = 54;
    auto terrain = Client::World::LoadWorldTerrain(m_dataRoot, Selection);
    ASSERT_TRUE(terrain.HasValue()) << terrain.Error();
    const auto camera = Client::Scenes::SelectionCamera();
    const auto pass =
        Client::Render::MakePass3D(Client::App::PixelSize{1024, 768}, Client::Scenes::WorldViewport, camera);
    const auto range = Client::World::TerrainRangeOf(pass.view, camera);
    const auto polygon = Client::World::TerrainPolygonOf(Client::World::CullingScene::CharacterSelect, Selection,
                                                         {12975.0f, 15423.0f, 333.0f}, camera.angles.z);
    const TerrainView view{.mapping = terrain.Value().mapping.get(),
                           .heights = terrain.Value().heights.get(),
                           .attributes = terrain.Value().attributes.get(),
                           .lighting = terrain.Value().lighting.get()};
    ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});
    Client::World::DrawTerrain(batch, view, range, polygon, TerrainWater{});

    const auto traced = TracedCells(trace.Value().frames[0]);
    ASSERT_EQ(traced.size(), 5109u);
    EXPECT_EQ(DrawnCells(batch), traced);
}

// El login no tiene traza: va con los datos solos (los del repo en CI) y no pide la carpeta de las trazas.
using GoldenTerrainLogin = Client::Tests::GoldenDataTest;

// Y el login (el mapa 55, World56), que no tiene traza: con LoginCamera y el héroe en (11.679;
// 14.191; 333), los bloques de x 0..128 e y 120..248 pasan todos y dibujan 3.244 celdas sin el bit
// 0x08, de la (90; 153) a la (104; 251), en el orden del código real emulado (la suma de control es
// la de scripts/login: s = s · 31 + x · 256 + y, módulo 2³²).
TEST_F(GoldenTerrainLogin, TheLoginDrawsTheCellsOfTheEmulatedCode)
{
    constexpr std::uint8_t Login = 55;
    auto terrain = Client::World::LoadWorldTerrain(m_dataRoot, Login);
    ASSERT_TRUE(terrain.HasValue()) << terrain.Error();
    const auto camera = Client::Scenes::LoginCamera();
    const auto pass =
        Client::Render::MakePass3D(Client::App::PixelSize{640, 480}, Client::Scenes::WorldViewport, camera);
    const auto range = Client::World::TerrainRangeOf(pass.view, camera);
    const auto polygon = Client::World::TerrainPolygonOf(Client::World::CullingScene::Login, Login,
                                                         Client::Scenes::LoginHeroPosition, camera.angles.z);
    const TerrainView view{.mapping = terrain.Value().mapping.get(),
                           .heights = terrain.Value().heights.get(),
                           .attributes = terrain.Value().attributes.get(),
                           .lighting = terrain.Value().lighting.get()};
    ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});
    Client::World::DrawTerrain(batch, view, range, polygon, TerrainWater{});

    const auto cells = DrawnCells(batch);
    ASSERT_EQ(cells.size(), 3244u);
    EXPECT_EQ(cells.front(), std::make_pair(90, 153));
    EXPECT_EQ(cells.back(), std::make_pair(104, 251));
    std::uint32_t checksum = 0;
    for (const auto& [x, y] : cells)
    {
        checksum = (checksum * 31u) + static_cast<std::uint32_t>((x * 256) + y);
    }
    EXPECT_EQ(checksum, 3022140673u);
}
} // namespace
