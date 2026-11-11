#include "World/KalimaWorld.h"
#include "World/KalimaResources.h"
#include "World/KalimaWaves.h"
#include "Data/Textures/Sampling.h"
#include "Data/Textures/Texture.h"
#include "Render/ModelBatch.h"
#include "Render/ModelVertex.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/LogicalAccess.h"
#include "Render/Pass3D.h"
#include "Render/SpriteVertex.h"
#include "Render/TextureSlot.h"
#include "support/GoldenDir.h"
#include "support/TemporaryFolder.h"
#include <gtest/gtest.h>
#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <type_traits>
#include <string>
#include <utility>
#include <vector>
#include "Core/Expected.h"

namespace
{
using Client::Render::ModelBatch;
using Client::Render::TextureSlot;
using Client::Render::Models::CrtRandom;
using Client::World::KalimaFrameContext;
using Client::World::KalimaWorld;

TEST(KalimaWorldTest, MapMembershipDoesNotCreateAnOwner)
{
    for (std::int32_t map = -1; map <= 55; ++map)
        EXPECT_EQ(Client::World::IsKalimaMap(map), (map >= 24 && map <= 29) || map == 36) << map;
    EXPECT_FALSE(std::is_copy_constructible_v<KalimaWorld>);
    EXPECT_LT(sizeof(KalimaWorld), 64u);
}
TEST(KalimaWorldTest, InitializesAllFourPlanesAndTheSelectedSurface)
{
    for (const std::int32_t map : {24, 25, 26, 27, 28, 29, 36})
    {
        KalimaWorld world(map);
        EXPECT_EQ(world.Map(), map);
        EXPECT_FALSE(world.Waves().selected);
        for (const auto& plane : world.Waves().planes)
            EXPECT_TRUE(std::ranges::all_of(plane, [](auto value) { return value == 0; }));
        EXPECT_EQ(world.Height(6400, 6400), 175.0f);
        EXPECT_EQ(world.Height(-10, -30), 175.0f);
        EXPECT_FALSE(world.Height(12850, 6400));
    }
}
TEST(KalimaWorldTest, TogglesBeforeTheRealWaveKernelAndConsumesNoRandomOutsideTheGate)
{
    KalimaWorld world(24);
    CrtRandom random(17);
    ASSERT_TRUE(world.Disturb(64, 64, 4, 1000000, random).HasValue());
    auto expected = std::make_unique<Client::World::KalimaWaveState>(world.Waves());
    EXPECT_NE(expected->planes[0][128 * 256 + 128], 0u);
    expected->selected = true;
    Client::World::UpdateKalimaWaves(*expected);
    const auto state = random.State();
    ASSERT_TRUE(world.Update(KalimaFrameContext{1, 0, 64, 64, random}).HasValue());
    EXPECT_EQ(random.State(), state);
    EXPECT_TRUE(world.Waves().selected);
    EXPECT_EQ(world.Waves().planes, expected->planes);
}
TEST(KalimaWorldTest, FortiethGateConsumesExactlyOneCrtDrawAndDisturbsBeforeToggle)
{
    KalimaWorld world(36);
    CrtRandom random(1);
    CrtRandom expected(1);
    static_cast<void>(expected.Next());
    ASSERT_TRUE(world.Update(KalimaFrameContext{40, 1000, 64, 64, random}).HasValue());
    EXPECT_EQ(random.State(), expected.State());
    EXPECT_TRUE(world.Waves().selected);
    EXPECT_TRUE(std::ranges::all_of(world.Waves().planes[0], [](auto value) { return value == 0; }));
    EXPECT_TRUE(std::ranges::all_of(world.Waves().planes[1], [](auto value) { return value == 0; }));
    EXPECT_EQ(world.Height(6400, 6400), 177.625f);
}
TEST(KalimaWorldTest, DrawsBothRealModesWithCheckerboardTrianglesAndNoSimulationStep)
{
    KalimaWorld world(24);
    ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});
    ASSERT_TRUE(world.Draw(batch, 64, 64).HasValue());
    ASSERT_EQ(batch.Vertices().size(), 2u * 41u * 41u * 6u);
    ASSERT_EQ(batch.Commands().size(), 2u);
    EXPECT_EQ(batch.Commands()[0].state.texture, TextureSlot{0x23});
    EXPECT_EQ(batch.Commands()[0].state.blend, Client::Render::ModelBlend::Alpha);
    EXPECT_EQ(batch.Commands()[1].state.texture, TextureSlot{0x24});
    EXPECT_EQ(batch.Commands()[1].state.blend, Client::Render::ModelBlend::Additive);
    EXPECT_EQ(batch.Vertices()[0].position.x, 5150.0f);
    EXPECT_EQ(batch.Vertices()[0].position.y, 5650.0f);
    EXPECT_EQ(batch.Vertices()[0].position.z, 350.0f);
    EXPECT_EQ(batch.Vertices()[1].position.x, 5200.0f);
    EXPECT_EQ(batch.Vertices()[1].position.y, 5650.0f);
    EXPECT_EQ(batch.Vertices()[2].position.x, 5200.0f);
    EXPECT_EQ(batch.Vertices()[2].position.y, 5700.0f);
    EXPECT_FLOAT_EQ(batch.Vertices()[0].uv.x, 0.1f);
    EXPECT_FLOAT_EQ(batch.Vertices()[0].uv.y, 1.0f);
    EXPECT_EQ(batch.Vertices()[0].color, (Client::Render::Color{0.2f, 0.5f, 0.65f, 1}));
    EXPECT_EQ(batch.Vertices()[10086].color, (Client::Render::Color{0, 0, 0, 1}));
    // Columna impar: la otra diagonal corta por arriba-derecha / abajo-izquierda.
    EXPECT_EQ(batch.Vertices()[8].position.x, 5200.0f);
    EXPECT_EQ(batch.Vertices()[8].position.y, 5700.0f);
    EXPECT_FALSE(world.Waves().selected);
    for (const auto& vertex : batch.Vertices())
        EXPECT_EQ(vertex.position.z, 350.0f);
}
TEST(KalimaWorldTest, HighlightKeepsTheUnspilledLightForBlueOnTheUpdatedSurface)
{
    KalimaWorld world(24);
    CrtRandom random;
    ASSERT_TRUE(world.Update(KalimaFrameContext{1, 1000, 64, 64, random}).HasValue());
    ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});
    ASSERT_TRUE(world.Draw(batch, 64, 64).HasValue());
    ASSERT_EQ(batch.Commands().size(), 2u);
    const auto& highlight = batch.Commands()[1];
    ASSERT_EQ(highlight.state.blend, Client::Render::ModelBlend::Additive);
    const auto vertices = batch.Vertices().subspan(highlight.firstVertex, highlight.vertexCount);
    const auto vertex = std::ranges::find_if(vertices, [](const Client::Render::ModelVertex& candidate)
                                             { return candidate.position.x == 5800 && candidate.position.y == 5650; });
    ASSERT_NE(vertex, vertices.end());
    ASSERT_EQ(vertex->position.z, 341.75f);
    // 41d257 conserva la luz x87 para el azul; rojo/verde recargan su copia float.
    EXPECT_EQ(std::bit_cast<std::uint32_t>(vertex->color.red), 0xbda71640u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(vertex->color.green), 0xbe50dbd0u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(vertex->color.blue), 0xbe7aa15fu);
}
TEST(KalimaWorldTest, PerturbationKeepsTheSignedShiftStrictDiskAndInteriorBorders)
{
    KalimaWorld world(24);
    CrtRandom random(1);
    const auto initialRandom = random.State();
    ASSERT_TRUE(world.Disturb(64, 64, 3, -1000000, random).HasValue());
    const auto& plane = world.Waves().planes[0];
    EXPECT_EQ(plane[128 * 256 + 128], 0xfffffffcU);
    EXPECT_EQ(plane[128 * 256 + 131], 0u); // distancia² == radio² queda afuera.
    EXPECT_EQ(random.State(), initialRandom);
    ASSERT_TRUE(world.Disturb(0, 0, 20, 1000000000, random).HasValue());
    for (std::size_t index = 0; index < Client::World::KalimaWaveSide; ++index)
    {
        EXPECT_EQ(plane[index], 0u);
        EXPECT_EQ(plane[index * Client::World::KalimaWaveSide], 0u);
    }
    EXPECT_NE(plane[257], 0u);
}

TEST(KalimaWorldTest, NativeGateConsumesAnExtraDrawWhenThePerturbationCenterIsNegative)
{
    KalimaWorld world(24);
    CrtRandom random(1);
    CrtRandom expected(1);
    ASSERT_LT(expected.Next() % 30 - 15, 0);
    static_cast<void>(expected.Next());
    ASSERT_TRUE(world.Update(KalimaFrameContext{0, 0, 0, 0, random}).HasValue());
    EXPECT_EQ(random.State(), expected.State());
}

TEST(KalimaWorldTest, PeriodicFieldsPersistOutsideTheNextHeroWindowAndSurviveProjection)
{
    KalimaWorld world(24);
    CrtRandom random;
    ASSERT_TRUE(world.Update(KalimaFrameContext{1, 1000, 64, 64, random}).HasValue());
    EXPECT_EQ(world.Height(6400, 6400), 177.625f);
    auto projected = world.CloneForFrame();
    ASSERT_TRUE(projected->Update(KalimaFrameContext{2, 2000, 255, 255, random}).HasValue());
    EXPECT_EQ(projected->Height(6400, 6400), 177.625f);
    EXPECT_EQ(world.Height(6400, 6400), 177.625f);
    ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});
    ASSERT_TRUE(projected->Draw(batch, 255, 255).HasValue());
    EXPECT_EQ(batch.Vertices().size(), 20172u);
    for (const auto& vertex : batch.Vertices())
        EXPECT_EQ(vertex.position.z, 350);
}

TEST(KalimaWorldTest, WaterReenablesTextureWithoutResettingTheOtherCameraAxes)
{
    KalimaWorld world(24);
    ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});
    auto state = batch.State();
    state.texture2d = false;
    state.depthTest = false;
    batch.SetState(state);
    ASSERT_TRUE(world.Draw(batch, 64, 64).HasValue());
    for (const auto& command : batch.Commands())
    {
        EXPECT_EQ(command.state.texture2d, true);
        EXPECT_FALSE(command.state.depthTest);
        EXPECT_EQ(command.state.cullEnabled, false);
    }
}

TEST(KalimaWorldTest, ExplicitProjectionCopiesAllStateAndHasIndependentLifetime)
{
    auto world = std::make_unique<KalimaWorld>(24);
    CrtRandom random;
    ASSERT_TRUE(world->Disturb(64, 64, 4, 1000000, random).HasValue());
    auto projected = world->CloneForFrame();
    EXPECT_EQ(projected->Waves().planes, world->Waves().planes);
    ASSERT_TRUE(projected->Update(KalimaFrameContext{1, 1000, 64, 64, random}).HasValue());
    EXPECT_FALSE(world->Waves().selected);
    EXPECT_TRUE(projected->Waves().selected);
    world.reset();
    EXPECT_TRUE(projected->Height(6400, 6400).has_value());
}
TEST(KalimaWorldTest, ReentryStartsCleanAndRejectsUndefinedCoordinates)
{
    KalimaWorld first(24);
    CrtRandom random;
    ASSERT_TRUE(first.Disturb(64, 64, 4, 1000000, random).HasValue());
    KalimaWorld second(29);
    EXPECT_EQ(second.Height(6400, 6400), 175.0f);
    EXPECT_FALSE(second.Height(std::numeric_limits<float>::infinity(), 0));
    EXPECT_FALSE(second.Disturb(64, 64, 0, 1000, random).HasValue());
}
TEST(KalimaWorldTest, ReadsAndClosesTheHeroAtTheNativePointsAroundTheConditionalRandom)
{
    KalimaWorld world(24);
    CrtRandom random(1);
    CrtRandom expected(1);
    static_cast<void>(expected.Next());
    std::vector<std::pair<std::string, std::uint32_t>> reads;
    KalimaFrameContext context{40, 1000, 0, 0, random};
    context.readHeroX = [&]() -> Client::Core::Expected<std::int32_t, std::string>
    {
        reads.emplace_back("X", random.State());
        return 64;
    };
    context.readHeroY = [&]() -> Client::Core::Expected<std::int32_t, std::string>
    {
        reads.emplace_back("Y", random.State());
        return 64;
    };
    ASSERT_TRUE(world.Update(context).HasValue());
    const std::vector<std::pair<std::string, std::uint32_t>> ordered{
        {"X", 1}, {"Y", expected.State()}, {"X", expected.State()}, {"Y", expected.State()}};
    EXPECT_EQ(reads, ordered);
    EXPECT_EQ(random.State(), expected.State());
    EXPECT_EQ(world.Height(6400, 6400), 177.625f);
}

TEST(KalimaWorldTest, RealLogicalHeroReadersCloseBeforeTheGateAndKeepAllFourNativeCloseCosts)
{
    KalimaWorld world(24);
    CrtRandom random(1);
    CrtRandom expected(1);
    const auto close = [&]()
    {
        for (std::uint16_t draw = 0; draw < Client::Render::Models::SmallLogicalCloseRandoms; ++draw)
            static_cast<void>(expected.Next());
    };
    close();
    const auto xState = expected.State();
    const auto center = expected.Next() % 30 - 15;
    close();
    const auto yState = expected.State();
    if (center < 0)
        static_cast<void>(expected.Next());
    close();
    close();
    std::uint8_t xDepth = 0;
    std::uint8_t yDepth = 0;
    std::vector<std::pair<std::string, std::uint32_t>> reads;
    const auto read = [&](bool x) -> Client::Core::Expected<std::int32_t, std::string>
    {
        {
            const Client::Render::Models::LogicalAccess access(x ? xDepth : yDepth, random,
                                                               Client::Render::Models::SmallLogicalCloseRandoms);
        }
        reads.emplace_back(x ? "X" : "Y", random.State());
        return x ? 0 : 64;
    };
    KalimaFrameContext context{40, 1000, 64, 64, random};
    context.readHeroX = [&] { return read(true); };
    context.readHeroY = [&] { return read(false); };
    ASSERT_TRUE(world.Update(context).HasValue());
    ASSERT_EQ(reads.size(), 4u);
    EXPECT_EQ(reads[0].second, xState);
    EXPECT_EQ(reads[1].second, yState);
    EXPECT_EQ(random.State(), expected.State());
    EXPECT_EQ(xDepth, 0);
    EXPECT_EQ(yDepth, 0);
    EXPECT_EQ(world.Height(6400, 6400), 175.0f);
}

TEST(KalimaWorldTest, AFailedConditionalYReadKeepsItsRandomButDoesNotToggleThePlanes)
{
    KalimaWorld world(24);
    CrtRandom random(1);
    CrtRandom expected(1);
    static_cast<void>(expected.Next());
    KalimaFrameContext context{40, 1000, 0, 0, random};
    context.readHeroX = []() -> Client::Core::Expected<std::int32_t, std::string> { return 64; };
    context.readHeroY = []() -> Client::Core::Expected<std::int32_t, std::string>
    { return Client::Core::Unexpected(std::string("Y lógico no disponible")); };
    const auto updated = world.Update(context);
    ASSERT_FALSE(updated.HasValue());
    EXPECT_EQ(updated.Error(), "Y lógico no disponible");
    EXPECT_EQ(random.State(), expected.State());
    EXPECT_FALSE(world.Waves().selected);
    EXPECT_EQ(world.Height(6400, 6400), 175.0f);
}

TEST(KalimaWorldTest, AFailedPeriodicReadKeepsTheAlreadyToggledAndUpdatedWaveKernel)
{
    KalimaWorld world(24);
    CrtRandom random(1);
    ASSERT_TRUE(world.Disturb(64, 64, 3, 1000000, random).HasValue());
    auto expected = std::make_unique<Client::World::KalimaWaveState>(world.Waves());
    expected->selected = true;
    Client::World::UpdateKalimaWaves(*expected);
    KalimaFrameContext context{1, 1000, 64, 64, random};
    context.readHeroX = []() -> Client::Core::Expected<std::int32_t, std::string>
    { return Client::Core::Unexpected(std::string("X periódico no disponible")); };
    context.readHeroY = []() -> Client::Core::Expected<std::int32_t, std::string>
    {
        ADD_FAILURE() << "Y no debe leerse tras fallar X";
        return 64;
    };
    EXPECT_FALSE(world.Update(context).HasValue());
    EXPECT_EQ(random.State(), 1u);
    EXPECT_TRUE(world.Waves().selected);
    EXPECT_EQ(world.Waves().planes, expected->planes);
}

TEST(KalimaResourcesTest, LoadsTheRealObject25FilesIntoNativeSlotsAndSampling)
{
    const auto set = Client::World::KalimaTextureSet();
    ASSERT_EQ(set.size(), 2u);
    EXPECT_EQ(set[0].path, "Object25\\water1.tga");
    EXPECT_EQ(set[0].slot, 0x23);
    EXPECT_EQ(set[0].sampling.filter, Client::Data::Textures::Filter::Linear);
    EXPECT_EQ(set[0].sampling.wrap, Client::Data::Textures::Wrap::Repeat);
    EXPECT_EQ(set[1].path, "Object25\\water2.jpg");
    EXPECT_EQ(set[1].slot, 0x24);
    EXPECT_EQ(set[1].sampling.filter, Client::Data::Textures::Filter::Nearest);
    EXPECT_EQ(set[1].sampling.wrap, Client::Data::Textures::Wrap::Clamp);
    const auto loaded = Client::World::LoadKalimaTextures(Client::Tests::RepoDataDir());
    ASSERT_TRUE(loaded.HasValue()) << loaded.Error();
    ASSERT_EQ(loaded.Value().size(), 2u);
    EXPECT_EQ(loaded.Value()[0].texture.format, Client::Data::Textures::PixelFormat::Rgba);
    EXPECT_EQ(loaded.Value()[1].texture.format, Client::Data::Textures::PixelFormat::Rgb);
    EXPECT_GT(loaded.Value()[0].texture.pixels.size(), 0u);
    EXPECT_GT(loaded.Value()[1].texture.pixels.size(), 0u);
}
TEST(KalimaResourcesTest, MissingFilesAreAnErrorBeforeAnyUpload)
{
    Client::Tests::TemporaryFolder empty("kalima-empty");
    EXPECT_FALSE(Client::World::LoadKalimaTextures(empty.Path()).HasValue());
}
} // namespace
