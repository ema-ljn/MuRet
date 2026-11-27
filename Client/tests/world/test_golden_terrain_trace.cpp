#include "Terrain/TerrainCells.h"
#include "Camera/GameCamera.h"
#include "Terrain/TerrainCulling.h"
#include "Terrain/WorldTerrain.h"

#include "Core/PixelSize.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Tables/ItemTable.h"
#include "Tables/SkillTable.h"
#include "Tables/TextTable.h"
#include "Data/Textures/TextureSet.h"
#include "Render/ModelBatch.h"
#include "Render/ModelVertex.h"
#include "Render/Pass3D.h"
#include "Render/TextureSlot.h"

#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Models/Cloth.h"
#include "Kernel/ActorState.h"
#include "Scenes/GameScene.h"
#include "Camera/SceneCameras.h"
#include "Scenes/SceneContext.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"

#include "support/FakeTextureStore.h"
#include "support/FileBytes.h"
#include "support/GlTrace.h"
#include "support/GoldenDir.h"
#include "support/RecordingFrameTarget.h"
#include "support/RecordingSink.h"

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
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Client::Terrain::DrawTerrainCell against the terrain of the Lorencia trace (MU_CLIENT_GL_TRACE_DIR, 06 § Terreno):
// each four-vertex fan of the terrain pass, with its geometry, UVs, texture, alpha and light, against what MuRet
// builds for that cell with the Data\ of World1.
namespace
{
using Client::Render::ModelBatch;
using Client::Render::ModelBlend;
using Client::Render::ModelDrawCommand;
using Client::Terrain::DrawTerrainCell;
using Client::Terrain::TerrainView;
using Client::Terrain::TerrainWater;
using Client::Tests::GlTraceBlock;

constexpr std::string_view LorenciaTrace = "lorencia-Frame_016215.txt";
constexpr std::uint8_t Lorencia = 0;
// In that session tile i is GL texture 1351 + i (06 § Terreno); what is not a tile, in the alpha pass, is slot 0x122
// (Q-C19).
constexpr std::uint32_t FirstTileName = 1351;
constexpr std::uint32_t TileCount = 14;
// The trace writes six decimals.
constexpr float Tolerance = 0.001f;
// A vertex's light matches if each channel is within 1 % (06 § Terreno), where red is above 8 / 255.
constexpr float LightTolerance = 0.01f;
constexpr float MeasurableRed = 8.0f / 255.0f;

// A terrain fan: four vertices at the corners of a 100 × 100 cell, with the state of one of the two passes.
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

// The four vertices of a span of the batch: those of the fan (0, 1, 2) and (0, 2, 3).
std::array<Client::Render::ModelVertex, 4> FanOf(const ModelBatch& batch, const ModelDrawCommand& command)
{
    const auto vertices = batch.Vertices().subspan(command.firstVertex, command.vertexCount);
    return {vertices[0], vertices[1], vertices[2], vertices[5]};
}

// The opaque cells of a trace frame, in their order.
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

// The cells MuRet draws, in their order: the first pass of each.
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

// The first view of Lorencia uses this camera before the terrain passes, so each cell's golden also checks the
// computed camera.
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
    auto terrain = Client::Terrain::LoadWorldTerrain(m_dataRoot, Lorencia);
    ASSERT_TRUE(terrain.HasValue()) << terrain.Error();

    Client::Camera::GameCamera cameraState;
    const Client::Camera::GameCameraInput cameraInput{.hero = {14350.0f, 12050.0f, 165.0f}, .cellAttribute = 0x01};
    const auto camera = Client::Camera::StepGameCamera(cameraState, cameraInput);
    ExpectCameraInTrace(trace.Value().frames.front(), camera);
    const auto worldPass = Client::Render::MakePass3D(Client::Core::PixelSize{1024, 768}, {0, 0, 640, 432}, camera);
    TerrainView view{.mapping = terrain.Value().mapping.get(),
                     .heights = terrain.Value().heights.get(),
                     .attributes = terrain.Value().attributes.get(),
                     .lighting = terrain.Value().lighting.get()};
    std::vector<Client::Terrain::WorldTileTexture> textures = Client::Terrain::WorldTileTextures(Lorencia);
    textures.push_back(Client::Terrain::AcceptBoxTexture());
    for (const Client::Terrain::WorldTileTexture& texture : textures)
    {
        const std::array<Client::Data::Textures::TextureSetEntry, 1> entry{
            {{texture.path, texture.slot, texture.sampling}}};
        const auto loaded = Client::Data::Textures::LoadTextureSet(m_dataRoot, entry);
        if (loaded.HasValue())
        {
            const auto& t = loaded.Value().front().texture;
            view.tiles[static_cast<std::size_t>(texture.slot - Client::Terrain::FirstTileSlot) % 256] = {
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
        // An opaque pass, and its cell's alpha pass if it comes right after.
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
                EXPECT_EQ(command.state.texture->value,
                          Client::Terrain::FirstTileSlot + (block.texture - FirstTileName))
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
    // 06 § Terreno: 329 opaque passes and 141 alpha passes, 3 with slot 0x122; the light matches on 1,746 of the 1,875
    // vertices with red above 8 / 255, and the other 129 have dynamic lights on top.
    EXPECT_EQ(opaque, 329u);
    EXPECT_EQ(alpha, 141u);
    EXPECT_EQ(acceptBox, 3u);
    EXPECT_EQ(matching, 1746u);
    EXPECT_EQ(brighter, 129u);
    EXPECT_EQ(darker, 0u);
}
// The culling: with the camera computed from the hero at (14350, 12050, 165), the range of 0x5fbb00, the trapezoid of
// 0x5fb570 and bit 0x08 leave the trace's 329 cells, in its order.
TEST_F(GoldenTerrainTrace, LorenciaDrawsTheCellsOfTheTraceInItsOrder)
{
    const auto path = TraceFile(LorenciaTrace);
    if (!path)
    {
        return;
    }
    auto trace = Client::Tests::ReadGlTrace(*path);
    ASSERT_TRUE(trace.HasValue()) << trace.Error();
    auto terrain = Client::Terrain::LoadWorldTerrain(m_dataRoot, Lorencia);
    ASSERT_TRUE(terrain.HasValue()) << terrain.Error();
    Client::Camera::GameCamera cameraState;
    const Client::Camera::GameCameraInput cameraInput{.hero = {14350.0f, 12050.0f, 165.0f}, .cellAttribute = 0x01};
    const auto camera = Client::Camera::StepGameCamera(cameraState, cameraInput);
    ASSERT_FALSE(trace.Value().frames.empty());
    ExpectCameraInTrace(trace.Value().frames.front(), camera);
    const auto pass = Client::Render::MakePass3D(Client::Core::PixelSize{1024, 768}, {0, 0, 640, 432}, camera);
    const auto range = Client::Terrain::TerrainRangeOf(pass.view, camera);
    const auto polygon = Client::Terrain::TerrainPolygonOf(Client::Terrain::CullingScene::Game, Lorencia,
                                                           cameraInput.hero, camera.angles.z);
    const TerrainView view{.mapping = terrain.Value().mapping.get(),
                           .heights = terrain.Value().heights.get(),
                           .attributes = terrain.Value().attributes.get(),
                           .lighting = terrain.Value().lighting.get()};
    ModelBatch batch;
    batch.Begin(pass);
    Client::Terrain::DrawTerrain(batch, view, range, polygon, TerrainWater{});

    const auto traced = TracedCells(trace.Value().frames[0]);
    ASSERT_EQ(traced.size(), 329u);
    EXPECT_EQ(DrawnCells(batch), traced);
}

// A terrain cell, by its corner.
struct TerrainCell
{
    int x = 0;
    int y = 0;
};

// The cells of the opaque pass at the end of a recorded frame's model pass, in their order.
std::vector<TerrainCell> OpaqueCellsOf(const Client::Tests::FrameEvent& models)
{
    std::vector<TerrainCell> cells;
    for (const ModelDrawCommand& command : models.modelCommands)
    {
        if (command.state.blend != ModelBlend::Opaque)
        {
            continue;
        }
        for (std::size_t first = 0; first < command.vertexCount; first += 6)
        {
            const glm::vec3 origin = models.modelVertices[command.firstVertex + first].position;
            cells.push_back(TerrainCell{static_cast<int>(origin.x / 100.0f), static_cast<int>(origin.y / 100.0f)});
        }
    }
    return cells;
}

// The same from the game scene: Lorencia loaded in the WorldState, the hero published at cell (143, 120) with the
// trace's position and the counter at 30. Its first frame reads the cell through the protection table, builds the
// trace's camera and draws the 329 cells in their order and the trace's 25 grass quads (06 § El pasto). The marks,
// objects, effects and fauna are empty; the hero, without a step that sees it, is not drawn (0x510d00). The empty tail
// goes on to the HUD, with its real tables; the terrain is in the first batch, before the tail's passes. The trace's
// 120 object blocks are not compared yet (08 P-C53).
TEST_F(GoldenTerrainTrace, TheGameSceneDrawsTheCellsOfLorencia)
{
    const auto path = TraceFile(LorenciaTrace);
    if (!path)
    {
        return;
    }
    auto trace = Client::Tests::ReadGlTrace(*path);
    ASSERT_TRUE(trace.HasValue()) << trace.Error();
    ASSERT_FALSE(trace.Value().frames.empty());
    auto terrain = Client::Terrain::LoadWorldTerrain(m_dataRoot, Lorencia);
    ASSERT_TRUE(terrain.HasValue()) << terrain.Error();
    const auto itemPath = DataFile("Local\\Item.bmd");
    const auto skillPath = DataFile("Local\\Skill.bmd");
    const auto textPath = DataFile("Local\\Text.bmd");
    ASSERT_TRUE(itemPath && skillPath && textPath);
    const auto itemFile = Client::Tests::ReadFileBytes(*itemPath);
    const auto skillFile = Client::Tests::ReadFileBytes(*skillPath);
    const auto textFile = Client::Tests::ReadFileBytes(*textPath);
    ASSERT_TRUE(itemFile && skillFile && textFile);
    const auto items = Client::Tables::LoadItemTable(std::as_bytes(std::span(*itemFile)));
    const auto skills = Client::Tables::LoadSkillTable(std::as_bytes(std::span(*skillFile)));
    const auto texts = Client::Tables::LoadTextTable(std::as_bytes(std::span(*textFile)));
    ASSERT_TRUE(items.HasValue());
    ASSERT_TRUE(skills.HasValue());
    ASSERT_TRUE(texts.HasValue());
    Client::Tests::RecordingSink sink;
    Client::Core::Log log(sink, Client::Core::LogLevel::Debug);
    Client::Tests::FakeTextureStore store;
    const auto worldOwner = std::make_unique<Client::Kernel::WorldState>();
    Client::Kernel::WorldState& world = *worldOwner;
    world.Map().terrain = std::move(terrain).Value();
    // The grass offset 0x5f8300 fills on load: the u does not change which cells carry it.
    world.Map().grassOffsets.assign(Client::Data::Terrain::TerrainCellCount, 0.0f);
    constexpr std::size_t HeroSlot = 7;
    Client::Kernel::ActorState& hero = world.Actors()[HeroSlot];
    hero.alive = true;
    hero.cellX = 143;
    hero.cellY = 120;
    hero.position = {14350.0f, 12050.0f, 165.0f};
    world.Runtime().map = Lorencia;
    world.Runtime().hero = HeroSlot;
    world.Runtime().drawingEnabled = true;
    world.Runtime().warpCounter = Client::Kernel::WaitDrawThreshold;
    Client::Models::ClothWorld cloth;
    Client::Camera::GameCamera cameraState;
    Client::Camera::GameCameraInput cameraInput;
    Client::Scenes::GameScene scene(Client::Scenes::SceneContext{.dataRoot = m_dataRoot,
                                                                 .textures = store,
                                                                 .log = log,
                                                                 .cloth = &cloth,
                                                                 .camera = &cameraState,
                                                                 .cameraInput = &cameraInput,
                                                                 .world = &world},
                                    nullptr, nullptr, &items.Value().table, nullptr, nullptr, &skills.Value(),
                                    &texts.Value());
    ASSERT_TRUE(scene.Enter().HasValue());
    Client::Tests::RecordingFrameTarget target(Client::Core::PixelSize{1024, 768});

    scene.PrepareFrame();
    scene.Render(target.BeginFrame({}), 0.0f);
    target.Present();

    ASSERT_EQ(scene.Failure(), std::nullopt) << scene.Failure().value_or("");
    const Client::Tests::FrameEvent* models = nullptr;
    for (const Client::Tests::FrameEvent& event : target.frames.at(0).events)
    {
        if (event.kind == Client::Tests::FrameEvent::Kind::Models)
        {
            models = &event;
            break;
        }
    }
    ASSERT_NE(models, nullptr);
    ASSERT_TRUE(models->pass.has_value());
    Client::Camera::GameCamera fresh;
    const auto camera = Client::Camera::StepGameCamera(
        fresh, Client::Camera::GameCameraInput{.hero = hero.position, .cellAttribute = cameraInput.cellAttribute});
    ExpectCameraInTrace(trace.Value().frames.front(), camera);
    EXPECT_EQ(models->pass->view,
              Client::Render::MakePass3D(Client::Core::PixelSize{1024, 768}, {0, 0, 640, 432}, camera).view);
    const auto traced = TracedCells(trace.Value().frames[0]);
    const std::vector<TerrainCell> drawn = OpaqueCellsOf(*models);
    ASSERT_EQ(traced.size(), 329u);
    ASSERT_EQ(drawn.size(), traced.size());
    for (std::size_t k = 0; k < drawn.size(); ++k)
    {
        EXPECT_EQ(drawn[k].x, traced[k].first) << k;
        EXPECT_EQ(drawn[k].y, traced[k].second) << k;
    }
    std::size_t grassVertices = 0;
    for (const ModelDrawCommand& command : models->modelCommands)
    {
        grassVertices += command.state.texture == Client::Render::TextureSlot{0x32} ? command.vertexCount : 0;
    }
    EXPECT_EQ(grassVertices, 25u * 6u);
}

// And the selection (map 54, World55), where the trapezoid does cull: with SelectionCamera and the hero at (12975,
// 15423, 333), the 5,109 cells of its trace.
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
    auto terrain = Client::Terrain::LoadWorldTerrain(m_dataRoot, Selection);
    ASSERT_TRUE(terrain.HasValue()) << terrain.Error();
    const auto camera = Client::Camera::SelectionCamera();
    const auto pass =
        Client::Render::MakePass3D(Client::Core::PixelSize{1024, 768}, Client::Camera::WorldViewport, camera);
    const auto range = Client::Terrain::TerrainRangeOf(pass.view, camera);
    const auto polygon = Client::Terrain::TerrainPolygonOf(Client::Terrain::CullingScene::CharacterSelect, Selection,
                                                           {12975.0f, 15423.0f, 333.0f}, camera.angles.z);
    const TerrainView view{.mapping = terrain.Value().mapping.get(),
                           .heights = terrain.Value().heights.get(),
                           .attributes = terrain.Value().attributes.get(),
                           .lighting = terrain.Value().lighting.get()};
    ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});
    Client::Terrain::DrawTerrain(batch, view, range, polygon, TerrainWater{});

    const auto traced = TracedCells(trace.Value().frames[0]);
    ASSERT_EQ(traced.size(), 5109u);
    EXPECT_EQ(DrawnCells(batch), traced);
}

// The login has no trace: it goes with the data alone (the repo's in CI) and does not ask for the traces folder.
using GoldenTerrainLogin = Client::Tests::GoldenDataTest;

// And the login (map 55, World56), which has no trace: with LoginCamera and the hero at (11679, 14191, 333), the
// blocks of x 0..128 and y 120..248 all pass and draw 3,244 cells without bit 0x08, from (90, 153) to (104, 251), in
// the order of the emulated real code (the checksum is that of scripts/login: s = s · 31 + x · 256 + y, modulo 2³²).
TEST_F(GoldenTerrainLogin, TheLoginDrawsTheCellsOfTheEmulatedCode)
{
    constexpr std::uint8_t Login = 55;
    auto terrain = Client::Terrain::LoadWorldTerrain(m_dataRoot, Login);
    ASSERT_TRUE(terrain.HasValue()) << terrain.Error();
    const auto camera = Client::Camera::LoginCamera();
    const auto pass =
        Client::Render::MakePass3D(Client::Core::PixelSize{640, 480}, Client::Camera::WorldViewport, camera);
    const auto range = Client::Terrain::TerrainRangeOf(pass.view, camera);
    const auto polygon = Client::Terrain::TerrainPolygonOf(Client::Terrain::CullingScene::Login, Login,
                                                           Client::Camera::LoginHeroPosition, camera.angles.z);
    const TerrainView view{.mapping = terrain.Value().mapping.get(),
                           .heights = terrain.Value().heights.get(),
                           .attributes = terrain.Value().attributes.get(),
                           .lighting = terrain.Value().lighting.get()};
    ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});
    Client::Terrain::DrawTerrain(batch, view, range, polygon, TerrainWater{});

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
