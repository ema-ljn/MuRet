#include "Scenes/GameLeaves.h"

#include "Core/Expected.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Data/Terrain/TerrainHeights.h"
#include "GameLogic/MovementSpeed.h"
#include "Render/Models/Cloth.h"
#include "Scenes/ActorState.h"
#include "World/ILeafWaveEvaluator.h"
#include "World/LeafWeather.h"
#include "World/Leaves.h"
#include "World/Particles.h"

#include <glm/ext/vector_float3.hpp>
#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <array>
#include <memory>
#include <string>
#include <system_error>

namespace
{
using Client::Render::Models::CrtRandom;

std::uint32_t StateAfter(std::size_t draws)
{
    std::uint32_t state = 1;
    for (std::size_t i = 0; i < draws; ++i)
        state = state * 214013u + 2531011u;
    return state;
}

std::uint32_t SeedStateAfter(std::uint32_t seed, std::size_t draws)
{
    for (std::size_t i = 0; i < draws; ++i)
        seed = seed * 214013u + 2531011u;
    return seed;
}

class GameLeavesTest : public testing::Test
{
protected:
    std::unique_ptr<Client::Data::Terrain::TerrainHeights> m_heights =
        std::make_unique<Client::Data::Terrain::TerrainHeights>();
    std::unique_ptr<Client::Data::Terrain::TerrainAttributes> m_attributes =
        std::make_unique<Client::Data::Terrain::TerrainAttributes>();
    Client::World::Leaves m_leaves;
    Client::World::LeafWeather m_weather{.firstWave = 30};
    glm::vec3 m_hero{1000, 2000, 100};
    float m_cameraY = 0;
    std::int32_t m_map = 0;
    std::int32_t m_layer = 0;
    float m_clock = 0;
    CrtRandom m_random;
    Client::Scenes::ActorState m_cloudHero;
    Client::GameLogic::MovementSpeedSharedState m_movementSpeed;
    std::int32_t m_crywolfState = 2;
    Client::Scenes::GameLeafContext m_context{m_leaves,  m_weather, *m_heights, *m_attributes, m_hero,
                                              m_cameraY, m_map,     m_layer,    m_random,      m_clock};

    void PrepareCloud(std::int32_t map, std::uint32_t seed, Client::World::Particles* particles)
    {
        m_map = map;
        m_hero = {10000, 15000, 100};
        m_random = CrtRandom(seed);
        m_cloudHero = Client::Scenes::ActorState{};
        m_movementSpeed = {};
        m_context.cloudHero = &m_cloudHero;
        m_context.movementSpeed = &m_movementSpeed;
        m_context.crywolfState = &m_crywolfState;
        m_context.particles = particles;
        for (auto& leaf : m_leaves.All())
        {
            leaf.alive = true;
            leaf.position = {0, 0, 0};
            leaf.light = glm::vec3(1);
        }
        m_leaves.All()[0].alive = false;
    }
};

TEST_F(GameLeavesTest, TheTavernSkipsWeatherAndEveryPhysicalSlot)
{
    m_layer = 4;
    m_weather.target = 80;
    ASSERT_TRUE(Client::Scenes::StepGameLeaves(m_context).HasValue());
    EXPECT_EQ(m_weather.current, 0);
    EXPECT_EQ(m_weather.phase, 0);
    EXPECT_FALSE(m_leaves.Any());
    EXPECT_EQ(m_random.State(), 1u);
}

TEST_F(GameLeavesTest, DeviasRoofsAndMapsOutsideTheCallerSetSkipTheWholePass)
{
    for (const std::int32_t layer : {3, 10, 11})
    {
        m_map = 2;
        m_layer = layer;
        ASSERT_TRUE(Client::Scenes::StepGameLeaves(m_context).HasValue());
        EXPECT_FALSE(m_leaves.Any());
        EXPECT_EQ(m_weather.phase, 0);
        EXPECT_EQ(m_random.State(), 1u);
    }
    m_layer = 0;
    for (const std::int32_t map : {1, 4, 8, 11, 17, 24, 29, 32, 35, 55})
    {
        m_map = map;
        ASSERT_TRUE(Client::Scenes::StepGameLeaves(m_context).HasValue());
        EXPECT_FALSE(m_leaves.Any());
        EXPECT_EQ(m_weather.phase, 0);
        EXPECT_EQ(m_random.State(), 1u);
    }
}

TEST_F(GameLeavesTest, LorenciaCreatesEightyLeavesWithNineRandomCallsAndRetainsAnglesAndScale)
{
    auto& first = m_leaves.All()[0];
    first.scale = 17;
    first.angles = {1, 2, 3};
    m_weather.target = 100;
    ASSERT_TRUE(Client::Scenes::StepGameLeaves(m_context).HasValue());
    EXPECT_EQ(m_weather.current, 1);
    EXPECT_EQ(m_weather.firstWave, 30);
    EXPECT_EQ(m_weather.phase, 20);
    EXPECT_EQ(m_random.State(), StateAfter(80 * 9));
    EXPECT_EQ(first.type, 100);
    EXPECT_EQ(first.position, (glm::vec3{241, 1767, 184}));
    EXPECT_EQ(first.start, first.position);
    EXPECT_EQ(first.light, glm::vec3(1));
    EXPECT_EQ(first.scale, 17);
    EXPECT_EQ(first.angles, (glm::vec3{1, 2, 3}));
    EXPECT_TRUE(m_leaves.All()[79].alive);
    EXPECT_FALSE(m_leaves.All()[80].alive);
}

TEST_F(GameLeavesTest, LorenciaLivingLeavesFlutterWithSixCallsEvenBelowTheTerrain)
{
    for (auto& leaf : m_leaves.All())
    {
        leaf.alive = true;
        leaf.type = 100;
        leaf.position = m_hero;
        leaf.position.z = -10;
        leaf.start = leaf.position;
    }
    ASSERT_TRUE(Client::Scenes::StepGameLeaves(m_context).HasValue());
    EXPECT_EQ(m_random.State(), StateAfter(80 * 6));
    EXPECT_LT(m_leaves.All()[0].position.z, 0);
    EXPECT_NE(m_leaves.All()[0].angles, glm::vec3(0));
    EXPECT_EQ(m_leaves.All()[80].position, (glm::vec3{1000, 2000, -10}));
}

TEST_F(GameLeavesTest, DeviasCreatesSnowWithFiveCallsAndPreservesStartAndUnusedFields)
{
    m_map = 2;
    m_leaves.All()[0].start = {11, 12, 13};
    m_leaves.All()[0].spin = {21, 22, 23};
    ASSERT_TRUE(Client::Scenes::StepGameLeaves(m_context).HasValue());
    const auto& leaf = m_leaves.All()[0];
    EXPECT_EQ(m_random.State(), StateAfter(80 * 5));
    EXPECT_EQ(leaf.type, 100);
    EXPECT_EQ(leaf.scale, 5);
    // R3=6334: 6334 % 1400 - 500 + 2000 = 2234 (52fd92..52fdae).
    EXPECT_EQ(leaf.position, (glm::vec3{1067, 2234, 400}));
    EXPECT_EQ(leaf.angles, (glm::vec3{-30, 0, 0}));
    EXPECT_EQ(leaf.start, (glm::vec3{11, 12, 13}));
    EXPECT_EQ(leaf.spin, (glm::vec3{21, 22, 23}));
}

TEST_F(GameLeavesTest, DeviasFallbackFadesOnTheGroundWithoutRandomCalls)
{
    m_map = 2;
    for (auto& leaf : m_leaves.All())
    {
        leaf.alive = true;
        leaf.position = {1000, 2000, -1};
        leaf.light = glm::vec3(0.05f);
    }
    ASSERT_TRUE(Client::Scenes::StepGameLeaves(m_context).HasValue());
    EXPECT_EQ(m_random.State(), 1u);
    EXPECT_EQ(m_leaves.All()[0].position.z, 0);
    EXPECT_FALSE(m_leaves.All()[0].alive);
    EXPECT_TRUE(m_leaves.All()[80].alive);
}

TEST_F(GameLeavesTest, NoriaAndAtlansUseTheNineCallCreatorAndGenericAirStep)
{
    for (const std::int32_t map : {3, 7})
    {
        m_map = map;
        m_random = CrtRandom{};
        m_leaves.Clear();
        ASSERT_TRUE(Client::Scenes::StepGameLeaves(m_context).HasValue());
        EXPECT_EQ(m_random.State(), StateAfter(80 * 9));
        EXPECT_EQ(m_leaves.All()[0].type, 100);
        EXPECT_EQ(m_leaves.All()[0].position, (glm::vec3{241, 1767, 184}));
        ASSERT_TRUE(Client::Scenes::StepGameLeaves(m_context).HasValue());
        EXPECT_EQ(m_random.State(), StateAfter(80 * 12));
    }
}

TEST_F(GameLeavesTest, MapsWithoutACreatorKeepTheOriginalStaleFieldsAndMapThirtyUsesFortySlots)
{
    m_map = 30;
    m_leaves.All()[0].type = 777;
    m_leaves.All()[0].scale = 9;
    ASSERT_TRUE(Client::Scenes::StepGameLeaves(m_context).HasValue());
    EXPECT_EQ(m_leaves.All()[0].type, 777);
    EXPECT_EQ(m_leaves.All()[0].scale, 9);
    EXPECT_EQ(m_random.State(), 1u);
    EXPECT_TRUE(m_leaves.All()[39].alive);
    EXPECT_FALSE(m_leaves.All()[40].alive);
}

TEST_F(GameLeavesTest, MapsThirtyThreeAndFiftyFourKeepTheOriginalEmptyCreatorContinuation)
{
    for (const std::int32_t map : {33, 54})
    {
        m_map = map;
        m_random = CrtRandom{};
        m_leaves.Clear();
        auto& leaf = m_leaves.All()[0];
        leaf.type = 777;
        leaf.scale = 9;
        leaf.position = {1000, 2000, 0};
        ASSERT_TRUE(Client::Scenes::StepGameLeaves(m_context).HasValue());
        EXPECT_EQ(m_random.State(), 1u);
        EXPECT_TRUE(m_leaves.All()[79].alive);
        EXPECT_FALSE(m_leaves.All()[80].alive);
        EXPECT_EQ(leaf.type, 777);
        EXPECT_EQ(leaf.scale, 9);
        EXPECT_EQ(leaf.position, (glm::vec3{1000, 2000, 0}));
        EXPECT_EQ(leaf.light, glm::vec3(1));
        ASSERT_TRUE(Client::Scenes::StepGameLeaves(m_context).HasValue());
        EXPECT_EQ(leaf.position.z, 0);
        EXPECT_FLOAT_EQ(leaf.light.x, 0.95f);
        EXPECT_EQ(m_random.State(), 1u);
    }
}

TEST_F(GameLeavesTest, IcarusForcesRainTargetAndOnlyBirthsTheCurrentIntensityPrefix)
{
    m_map = 10;
    ASSERT_TRUE(Client::Scenes::StepGameLeaves(m_context).HasValue());
    EXPECT_EQ(m_weather.target, 100);
    EXPECT_EQ(m_weather.current, 1);
    EXPECT_EQ(m_random.State(), StateAfter(2 * 4));
    EXPECT_EQ(m_leaves.All()[0].type, 102);
    EXPECT_EQ(m_leaves.All()[2].type, 0);
    EXPECT_TRUE(m_leaves.All()[79].alive);
}

TEST_F(GameLeavesTest, RainSplashRequiresAParticleBindingAfterMovingAndKillingTheCurrentLeaf)
{
    for (auto& leaf : m_leaves.All())
    {
        leaf.alive = true;
        leaf.type = 102;
        leaf.position = {1000, 2000, 1};
        leaf.velocity = {2, 3, -2};
    }
    const auto result = Client::Scenes::StepGameLeaves(m_context);
    ASSERT_FALSE(result.HasValue());
    EXPECT_NE(result.Error().find("530231"), std::string::npos);
    EXPECT_FALSE(m_leaves.All()[0].alive);
    EXPECT_EQ(m_leaves.All()[0].position, (glm::vec3{1002, 2003, 10}));
    EXPECT_EQ(m_leaves.All()[1].position, (glm::vec3{1000, 2000, 1}));
    EXPECT_EQ(m_weather.phase, 20);
}

TEST_F(GameLeavesTest, RainSplashCreatesARealParticleAndConsumesItsCreationRandomValue)
{
    Client::World::Particles particles;
    m_context.particles = &particles;
    auto& leaf = m_leaves.All()[0];
    leaf.alive = true;
    leaf.type = 102;
    leaf.position = {1000, 2000, 1};
    leaf.velocity.z = -2;
    ASSERT_TRUE(Client::Scenes::StepGameLeaves(m_context).HasValue());
    EXPECT_TRUE(particles.All()[0].alive);
    EXPECT_EQ(particles.All()[0].type, 103);
    EXPECT_EQ(particles.All()[0].position, (glm::vec3{1000, 2000, 10}));
    EXPECT_EQ(m_random.State(), StateAfter(1 + 79 * 9));
}

TEST_F(GameLeavesTest, MapNineCreatesTwoHundredRainSlotsWithSixCallsEach)
{
    m_map = 9;
    ASSERT_TRUE(Client::Scenes::StepGameLeaves(m_context).HasValue());
    EXPECT_TRUE(m_leaves.All()[199].alive);
    EXPECT_EQ(m_leaves.All()[199].type, 102);
    EXPECT_EQ(m_random.State(), StateAfter(200 * 6));
}

TEST_F(GameLeavesTest, EventRainCreatesEightySlotsWithSevenCallsAndAForcedTarget)
{
    m_map = 18;
    ASSERT_TRUE(Client::Scenes::StepGameLeaves(m_context).HasValue());
    EXPECT_EQ(m_weather.target, 100);
    EXPECT_EQ(m_weather.current, 1);
    EXPECT_EQ(m_leaves.All()[0].spin.x, 1);
    EXPECT_GE(m_leaves.All()[0].spin.y, 30);
    EXPECT_EQ(m_random.State(), StateAfter(80 * 7));
}

TEST_F(GameLeavesTest, IcarusUsesTwentyAsTheSpeedBase)
{
    m_map = 10;
    ASSERT_TRUE(Client::Scenes::StepGameLeaves(m_context).HasValue());
    // R4=26500; 26500 % 24 + 20 = 24; seno de -30° por esa velocidad.
    EXPECT_NEAR(m_leaves.All()[0].velocity.y, -12, 0.00001);
}

// 0x5f82c0 multiplica por el −0,01f de 0x6ba110: (1.000; −2.000) da la fila 20 y la columna −10, y 0x7d0d348
// − 5.110 · 2 queda antes de la tabla.
TEST_F(GameLeavesTest, EventRainCannotInventAWrappedAttributeCell)
{
    m_map = 18;
    auto& leaf = m_leaves.All()[0];
    leaf.alive = true;
    leaf.position = {1000, -2000, -1};
    const auto result = Client::Scenes::StepGameLeaves(m_context);
    ASSERT_FALSE(result.HasValue());
    EXPECT_NE(result.Error().find("530128"), std::string::npos);
    EXPECT_TRUE(leaf.alive);
    EXPECT_EQ(leaf.position.z, -1);
    EXPECT_FALSE(m_leaves.All()[1].alive);
    EXPECT_EQ(m_random.State(), 1u);
}

TEST_F(GameLeavesTest, CrywolfUsesTheSuppliedStateForRainCount)
{
    m_map = 34;
    const auto missing = Client::Scenes::StepGameLeaves(m_context);
    ASSERT_FALSE(missing.HasValue());
    EXPECT_NE(missing.Error().find("5304f6"), std::string::npos);
    EXPECT_EQ(m_weather.phase, 0);
    std::int32_t state = 1;
    m_context.crywolfState = &state;
    ASSERT_TRUE(Client::Scenes::StepGameLeaves(m_context).HasValue());
    EXPECT_TRUE(m_leaves.All()[59].alive);
    EXPECT_FALSE(m_leaves.All()[60].alive);
    EXPECT_EQ(m_random.State(), StateAfter(60 * 6));
}

TEST_F(GameLeavesTest, MapThirtyOneOutsideItsRectangleKeepsAnEmptyCreatorWithoutRandomCalls)
{
    m_map = 31;
    ASSERT_TRUE(Client::Scenes::StepGameLeaves(m_context).HasValue());
    EXPECT_TRUE(m_leaves.All()[79].alive);
    EXPECT_FALSE(m_leaves.All()[80].alive);
    EXPECT_EQ(m_random.State(), 1u);
}

TEST_F(GameLeavesTest, CloudCreatorsEmitRealParticlesAndKeepTheirDifferentHeightsAndRandomPrefixes)
{
    for (const std::int32_t map : {31, 34})
    {
        m_map = map;
        m_hero = {10000, 15000, 100};
        m_random = CrtRandom(613);
        m_context.cloudHero = &m_cloudHero;
        m_context.movementSpeed = &m_movementSpeed;
        m_context.crywolfState = &m_crywolfState;
        Client::World::Particles particles;
        m_context.particles = &particles;
        for (auto& leaf : m_leaves.All())
        {
            leaf.alive = true;
            leaf.position = {0, 0, 0};
            leaf.light = glm::vec3(1);
        }
        m_leaves.All()[0].alive = false;
        ASSERT_TRUE(Client::Scenes::StepGameLeaves(m_context).HasValue());
        EXPECT_FALSE(m_leaves.All()[0].alive);
        const auto& particle = particles.All()[0];
        EXPECT_TRUE(particle.alive);
        EXPECT_EQ(particle.type, 0x622);
        EXPECT_EQ(particle.subtype, 8);
        // Create622/8 suma sus propios R2/R3/R4 a la posición: no es un recorder sin efectos.
        EXPECT_EQ(particle.position, (map == 31 ? glm::vec3{10617, 15088, -12} : glm::vec3{10481, 15004, 398}));
        EXPECT_EQ(particle.light, (map == 31 ? glm::vec3{0.05f, 0.05f, 0.1f} : glm::vec3(0.07f)));
        EXPECT_EQ(m_random.State(), SeedStateAfter(613, map == 31 ? 12 : 11));
    }
}

TEST_F(GameLeavesTest, AllEightCloudDirectionsPreserveTheOriginalOffsetTableAndParticleRandomOrder)
{
    constexpr std::array<std::uint32_t, 8> Seeds{71, 567, 264, 117, 613, 310, 16, 365};
    const std::array<glm::vec3, 8> map31Positions{{{10249, 15421, -6},
                                                   {10581, 14776, -5},
                                                   {9662, 15493, 0},
                                                   {9730, 14880, -4},
                                                   {10617, 15088, -12},
                                                   {9714, 14816, 12},
                                                   {10146, 15520, -1},
                                                   {10048, 14678, 2}}};
    const std::array<glm::vec3, 8> crywolfPositions{{{10233, 15153, 385},
                                                     {10114, 15022, 394},
                                                     {10065, 15076, 386},
                                                     {9530, 15060, 394},
                                                     {10481, 15004, 398},
                                                     {9628, 15203, 386},
                                                     {9839, 15499, 397},
                                                     {10011, 14770, 386}}};
    for (const std::int32_t map : {31, 34})
    {
        for (std::size_t direction = 0; direction < Seeds.size(); ++direction)
        {
            Client::World::Particles particles;
            PrepareCloud(map, Seeds[direction], &particles);
            ASSERT_TRUE(Client::Scenes::StepGameLeaves(m_context).HasValue());
            EXPECT_FALSE(m_leaves.All()[0].alive);
            EXPECT_TRUE(particles.All()[0].alive);
            EXPECT_EQ(particles.All()[0].position, map == 31 ? map31Positions[direction] : crywolfPositions[direction]);
            const std::size_t draws = (direction < 4 ? 13u : 12u) - (map == 34 ? 1u : 0u);
            EXPECT_EQ(m_random.State(), SeedStateAfter(Seeds[direction], draws));
        }
    }
}

TEST_F(GameLeavesTest, MovingCloudCreatorsRunTheRealSharedMovementSpeedWithoutAStationaryCoin)
{
    for (const std::int32_t map : {31, 34})
    {
        Client::World::Particles particles;
        PrepareCloud(map, 613, &particles);
        m_cloudHero.movement = 1;
        m_cloudHero.type = 0x20a;
        m_cloudHero.byte88 = 1;
        m_cloudHero.pet.model = 0x1c37;
        m_movementSpeed.itemCounterBits = 123;
        ASSERT_TRUE(Client::Scenes::StepGameLeaves(m_context).HasValue());
        EXPECT_EQ(m_movementSpeed.itemLatch, 1u);
        EXPECT_EQ(m_movementSpeed.itemCounterBits, 0u);
        EXPECT_EQ(particles.All()[0].position, (map == 31 ? glm::vec3{10681, 14329, 6} : glm::vec3{10628, 14393, 394}));
        EXPECT_EQ(m_random.State(), SeedStateAfter(613, map == 31 ? 11 : 10));
    }
}

TEST_F(GameLeavesTest, MissingCloudBindingsKeepTheReachedRandomPrefixAndTheLeafAlive)
{
    PrepareCloud(34, 613, nullptr);
    m_context.cloudHero = nullptr;
    const auto missingHero = Client::Scenes::StepGameLeaves(m_context);
    ASSERT_FALSE(missingHero.HasValue());
    EXPECT_NE(missingHero.Error().find("435848"), std::string::npos);
    EXPECT_EQ(m_random.State(), SeedStateAfter(613, 3));
    EXPECT_TRUE(m_leaves.All()[0].alive);
    PrepareCloud(34, 613, nullptr);
    m_cloudHero.movement = 1;
    m_context.movementSpeed = nullptr;
    const auto missingSpeed = Client::Scenes::StepGameLeaves(m_context);
    ASSERT_FALSE(missingSpeed.HasValue());
    EXPECT_NE(missingSpeed.Error().find("43586c"), std::string::npos);
    EXPECT_EQ(m_random.State(), SeedStateAfter(613, 3));
    EXPECT_TRUE(m_leaves.All()[0].alive);
    PrepareCloud(31, 613, nullptr);
    const auto missingParticles = Client::Scenes::StepGameLeaves(m_context);
    ASSERT_FALSE(missingParticles.HasValue());
    EXPECT_NE(missingParticles.Error().find("443cd1"), std::string::npos);
    EXPECT_EQ(m_random.State(), SeedStateAfter(613, 5));
    EXPECT_TRUE(m_leaves.All()[0].alive);
}

TEST_F(GameLeavesTest, AFullParticlePoolStillKillsTheCloudLeafWithoutCreationRandomCalls)
{
    Client::World::Particles particles;
    for (auto& particle : particles.All())
        particle.alive = true;
    PrepareCloud(31, 613, &particles);
    ASSERT_TRUE(Client::Scenes::StepGameLeaves(m_context).HasValue());
    EXPECT_FALSE(m_leaves.All()[0].alive);
    EXPECT_EQ(m_random.State(), SeedStateAfter(613, 5));
}

TEST_F(GameLeavesTest, AStationaryCloudCoinCanKillTheLeafWithoutCreatingAParticle)
{
    Client::World::Particles particles;
    PrepareCloud(31, 466, &particles);
    ASSERT_TRUE(Client::Scenes::StepGameLeaves(m_context).HasValue());
    EXPECT_FALSE(m_leaves.All()[0].alive);
    EXPECT_FALSE(particles.Any());
    EXPECT_EQ(m_random.State(), SeedStateAfter(466, 4));
}

TEST_F(GameLeavesTest, EventRainHonorsTheAttributeBitBeforeConsumingSplashRandom)
{
    m_map = 18;
    for (auto& leaf : m_leaves.All())
    {
        leaf.alive = true;
        leaf.position = {0, 0, 1};
    }
    m_leaves.All()[0].position.z = -1;
    m_attributes->cells[0] = 8;
    ASSERT_TRUE(Client::Scenes::StepGameLeaves(m_context).HasValue());
    EXPECT_TRUE(m_leaves.All()[0].alive);
    EXPECT_EQ(m_leaves.All()[0].position.z, -1);
    EXPECT_EQ(m_random.State(), 1u);
}

// Con el −0,01f, (1.050; 2.050) es la celda (10; 20): el bit 8 de esa celda la frena, como el de la (0; 0).
TEST_F(GameLeavesTest, EventRainReadsTheAttributeOfTheCellItFallsOn)
{
    m_map = 18;
    for (auto& other : m_leaves.All())
    {
        other.alive = true;
        other.position = {0, 0, 1};
    }
    auto& leaf = m_leaves.All()[0];
    leaf.position = {1050, 2050, -1};
    m_attributes->cells[20 * 256 + 10] = 8;
    ASSERT_TRUE(Client::Scenes::StepGameLeaves(m_context).HasValue());
    EXPECT_TRUE(leaf.alive);
    EXPECT_EQ(leaf.position.z, -1);
    EXPECT_EQ(m_random.State(), 1u);
}

TEST_F(GameLeavesTest, MissingParticleBindingReportsTheReachedType68CallAfterItsRandomChoice)
{
    for (const std::int32_t map : {9, 18})
    {
        m_map = map;
        m_random = CrtRandom{};
        m_leaves.Clear();
        auto& leaf = m_leaves.All()[0];
        leaf.alive = true;
        leaf.position = {0, 0, -1};
        const auto result = Client::Scenes::StepGameLeaves(m_context);
        ASSERT_FALSE(result.HasValue());
        EXPECT_NE(result.Error().find(map == 9 ? "530098" : "530187"), std::string::npos);
        EXPECT_FALSE(leaf.alive);
        EXPECT_EQ(leaf.position.z, 10);
        EXPECT_EQ(m_random.State(), StateAfter(1));
        EXPECT_FALSE(m_leaves.All()[1].alive);
    }
}

class FailedWave final : public Client::World::ILeafWaveEvaluator
{
public:
    Client::Core::Expected<void, std::error_code> Evaluate(Client::World::LeafWave, float) override
    {
        return Client::Core::Unexpected(std::make_error_code(std::errc::io_error));
    }
    Client::Core::Expected<std::int32_t, std::error_code> Truncate() override
    {
        ADD_FAILURE() << "No debe convertir después del evaluador fallido";
        return 0;
    }
};

TEST_F(GameLeavesTest, WaveFailurePreservesOnlyTheWeatherIntensityPrefix)
{
    FailedWave failed;
    m_context.waveEvaluator = &failed;
    m_weather.target = 5;
    m_weather.current = 9;
    m_weather.firstWave = 71;
    m_weather.secondWave = 72;
    m_weather.phase = 73;
    const auto result = Client::Scenes::StepGameLeaves(m_context);
    ASSERT_FALSE(result.HasValue());
    EXPECT_NE(result.Error().find("53052b"), std::string::npos);
    EXPECT_EQ(m_weather.current, 8);
    EXPECT_EQ(m_weather.firstWave, 71);
    EXPECT_EQ(m_weather.secondWave, 72);
    EXPECT_EQ(m_weather.phase, 73);
    EXPECT_FALSE(m_leaves.Any());
    EXPECT_EQ(m_random.State(), 1u);
}
} // namespace
