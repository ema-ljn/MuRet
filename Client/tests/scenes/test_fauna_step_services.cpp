#include "Scenes/FaunaStepServices.h"

#include "Data/Terrain/TerrainAttributes.h"
#include "Data/Terrain/TerrainHeights.h"
#include "Render/Models/Cloth.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "World/TerrainLighting.h"
#include "World/WorldTerrain.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <memory>
#include <type_traits>

namespace
{
using Client::Scenes::FaunaEnvironmentAccess;
using Client::Scenes::FaunaEnvironmentError;
class FaunaEnvironmentAccessTest : public testing::Test
{
protected:
    Client::Scenes::WorldState m_world;
    Client::Render::Models::ClothWorld m_cloth;
    Client::World::WorldTerrain m_terrain;
    std::unique_ptr<Client::World::TerrainLighting> m_light = std::make_unique<Client::World::TerrainLighting>();
    std::int32_t m_mode = 25;
    std::uint8_t m_flag = 0x81;
    std::int32_t m_scene = 5;
    float m_scale = -0.01f;
    float m_fixedHeight = 1201.0f;
    FaunaEnvironmentAccess m_access{
        m_world, m_cloth, m_terrain, *m_light, {m_mode, m_flag, m_scene, m_scale, m_fixedHeight}};
    void SetUp() override
    {
        m_terrain.attributes = std::make_unique<Client::Data::Terrain::TerrainAttributes>();
        m_terrain.heights = std::make_unique<Client::Data::Terrain::TerrainHeights>();
    }
};
static_assert(!std::is_default_constructible_v<FaunaEnvironmentAccess>);
static_assert(!std::is_copy_constructible_v<FaunaEnvironmentAccess>);
static_assert(!std::is_move_constructible_v<FaunaEnvironmentAccess>);
} // namespace

// Un RNG propio o una copia del owner rompe la secuencia intercalada.
TEST_F(FaunaEnvironmentAccessTest, ConsumesTheClothRandomSequence)
{
    EXPECT_EQ(m_access.Random(), 41);
    EXPECT_EQ(m_cloth.random.Next(), 18467);
    EXPECT_EQ(m_access.Random(), 6334);
    EXPECT_EQ(m_cloth.random.State(), 0x18be873aU);
}

TEST_F(FaunaEnvironmentAccessTest, RereadsPublishedWorldTimeAndMapWithoutSampling)
{
    EXPECT_FALSE(m_access.WorldTime().HasValue());
    EXPECT_EQ(m_access.WorldTime().Error(), FaunaEnvironmentError::MissingWorldTime);
    m_world.Runtime().worldTime = 1234.5f;
    m_world.Runtime().map = 7;
    EXPECT_EQ(m_access.WorldTime().Value(), 1234.5f);
    EXPECT_EQ(m_access.Map(), 7);
    m_world.Runtime().worldTime = 9.0f;
    m_world.Runtime().map = 8;
    EXPECT_EQ(m_access.WorldTime().Value(), 9.0f);
    EXPECT_EQ(m_access.Map(), 8);
    m_world.Runtime().worldTime.reset();
    EXPECT_FALSE(m_access.WorldTime().HasValue());
}

TEST_F(FaunaEnvironmentAccessTest, HeroBorrowRemainsTheOriginalActorWhenIndexChanges)
{
    m_world.Runtime().hero = 3;
    m_world.Actors()[3].position = {10, 20, 30};
    const auto borrowed = m_access.ActorPosition();
    ASSERT_TRUE(borrowed.HasValue());
    EXPECT_EQ(&borrowed.Value().get(), &m_world.Actors()[3].position);
    m_world.Runtime().hero = 4;
    m_world.Actors()[3].position.x = 77;
    EXPECT_EQ(borrowed.Value().get().x, 77);
    EXPECT_EQ(&m_access.ActorPosition().Value().get(), &m_world.Actors()[4].position);
    m_world.Runtime().hero = Client::Scenes::ActorWindowCount;
    EXPECT_EQ(m_access.ActorPosition().Error(), FaunaEnvironmentError::InvalidHeroIndex);
    EXPECT_EQ(borrowed.Value().get().y, 20);
}

TEST_F(FaunaEnvironmentAccessTest, RequiresExternalInputsAndReadsTheirCurrentValues)
{
    EXPECT_EQ(m_access.Mode(), 25);
    EXPECT_EQ(&m_access.HeroFlag(), &m_flag);
    m_mode = -9;
    m_flag = 0xff;
    EXPECT_EQ(m_access.Mode(), -9);
    EXPECT_EQ(m_access.HeroFlag(), 0xff);
}

TEST_F(FaunaEnvironmentAccessTest, IndexesWrappedCellsSeparatelyFromSubtractiveQuery)
{
    EXPECT_EQ(m_access.TileIndex(-1, 257), 511U);
    m_terrain.attributes->cells[511] = 0xabcd;
    const auto cell = m_access.TileAttributes(511);
    ASSERT_TRUE(cell.HasValue());
    EXPECT_EQ(&cell.Value().get(), &m_terrain.attributes->cells[511]);
    m_terrain.attributes->cells[511] = 0x1234;
    EXPECT_EQ(cell.Value().get(), 0x1234);
    EXPECT_EQ(m_access.TileAttributes(65536).Error(), FaunaEnvironmentError::InvalidCellIndex);
    m_terrain.attributes->cells[770] = 0xab08;
    EXPECT_EQ(m_access.QueryAttributes(250, 350).Value(), 0x07d0ab08U);
    m_scale = 1.0f;
    EXPECT_EQ(m_access.QueryAttributes(1, 0).Error(), FaunaEnvironmentError::UnrepresentedQueryAddress);
    m_scale = 0.0f;
    m_terrain.attributes->cells[0] = 0xfedc;
    EXPECT_EQ(m_access.QueryAttributes(250, 350).Value(), 0x07d0fedcU);
    m_terrain.attributes.reset();
    EXPECT_EQ(m_access.QueryAttributes(0, 0).Error(), FaunaEnvironmentError::MissingAttributes);
    EXPECT_EQ(m_access.TileAttributes(0).Error(), FaunaEnvironmentError::MissingAttributes);
}

TEST_F(FaunaEnvironmentAccessTest, HeightUsesLiveTerrainAndSceneGuard)
{
    m_terrain.heights->cells.fill(80.0f);
    EXPECT_EQ(m_access.Height(150, 150).Value(), 80.0);
    m_terrain.heights->cells.fill(90.0f);
    EXPECT_EQ(m_access.Height(150, 150).Value(), 90.0);
    m_terrain.attributes->cells[257] = 0x40;
    EXPECT_EQ(m_access.Height(150, 150).Value(), 1201.0);
    m_fixedHeight = 1202;
    EXPECT_EQ(m_access.Height(150, 150).Value(), 1202.0);
    m_terrain.heights.reset();
    EXPECT_EQ(m_access.Height(150, 150).Error(), FaunaEnvironmentError::MissingHeights);
    m_terrain.attributes.reset();
    for (const std::int32_t scene : {0, 1, 3})
    {
        m_scene = scene;
        EXPECT_EQ(m_access.Height(150, 150).Value(), 0.0);
    }
    m_scene = 5;
    EXPECT_EQ(m_access.Height(150, 150).Error(), FaunaEnvironmentError::MissingAttributes);
}

TEST_F(FaunaEnvironmentAccessTest, RadialPrefixUpdatesOnlyTheBorrowedDynamicBuffer)
{
    m_light->cells.fill({1, 1, 1});
    m_terrain.lighting = std::make_unique<Client::World::TerrainLighting>();
    m_terrain.lighting->cells.fill({4, 4, 4});
    ASSERT_TRUE(m_access.PrefixLight(0, 0).HasValue());
    EXPECT_FLOAT_EQ(m_light->cells[0][0], 0.7f);
    EXPECT_FLOAT_EQ(m_light->cells[0][1], 0.7f);
    EXPECT_FLOAT_EQ(m_light->cells[0][2], 0.8f);
    EXPECT_FLOAT_EQ(m_light->cells[255][0], 0.71875f);
    EXPECT_EQ(m_light->cells[16][0], 1.0f);
    EXPECT_EQ(m_terrain.lighting->cells[0][0], 4.0f);
    const auto failure = m_access.PrefixLight(std::numeric_limits<float>::infinity(), 0);
    EXPECT_EQ(failure.Error(), FaunaEnvironmentError::InvalidLightCoordinates);
    // Un fallo posterior no revierte la luz ni los draws ya consumidos.
    EXPECT_FLOAT_EQ(m_light->cells[0][0], 0.7f);
    EXPECT_EQ(m_access.Random(), 41);
    m_scale = 1.0f;
    EXPECT_FALSE(m_access.QueryAttributes(1, 0).HasValue());
    EXPECT_EQ(m_cloth.random.Next(), 18467);
}

TEST_F(FaunaEnvironmentAccessTest, HeightPreservesCornerInterpolationAndNegativeGate)
{
    m_terrain.heights->cells[0] = 1;
    m_terrain.heights->cells[1] = 3;
    m_terrain.heights->cells[256] = 5;
    m_terrain.heights->cells[257] = 7;
    EXPECT_EQ(m_access.Height(50, 50).Value(), 4.0);
    EXPECT_EQ(m_access.Height(-1, 50).Value(), 0.0);
    m_scene = 2;
    EXPECT_EQ(m_access.Height(50, 50).Value(), 4.0);
}

TEST_F(FaunaEnvironmentAccessTest, InvalidRadialCoordinatesLeaveAnEarlierPrefixIntact)
{
    m_light->cells.fill({0.1f, 0.1f, 0.1f});
    ASSERT_TRUE(m_access.PrefixLight(0, 0).HasValue());
    EXPECT_EQ(m_light->cells[0][0], 0.0f);
    const float previous = m_light->cells[255][0];
    for (const float x : {std::numeric_limits<float>::max(), std::numeric_limits<float>::quiet_NaN(),
                          -std::numeric_limits<float>::infinity()})
    {
        EXPECT_EQ(m_access.PrefixLight(x, 0).Error(), FaunaEnvironmentError::InvalidLightCoordinates);
        EXPECT_EQ(m_light->cells[255][0], previous);
    }
}

TEST_F(FaunaEnvironmentAccessTest, RejectsTheShadedBaseAsDynamicLightOwner)
{
    m_terrain.lighting = std::make_unique<Client::World::TerrainLighting>();
    m_terrain.lighting->cells.fill({1, 1, 1});
    FaunaEnvironmentAccess wrongOwner{
        m_world, m_cloth, m_terrain, *m_terrain.lighting, {m_mode, m_flag, m_scene, m_scale, m_fixedHeight}};
    const auto result = wrongOwner.PrefixLight(0, 0);
    ASSERT_FALSE(result.HasValue());
    EXPECT_EQ(m_terrain.lighting->cells[0][0], 1.0f);
    EXPECT_EQ(m_access.Random(), 41);
}
