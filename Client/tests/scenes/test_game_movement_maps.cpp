#include "Scenes/GameMovement.h"

#include "Core/Expected.h"
#include "Data/Tables/SkillTable.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Data/Terrain/TerrainHeights.h"
#include "Data/Terrain/TerrainMapping.h"
#include "GameLogic/Path.h"
#include "GameLogic/PathCell.h"
#include "Network/Crypto/Xor32.h"
#include "Network/Packet.h"
#include "Protocol/Movement.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/LogicalAccess.h"
#include "Render/Models/Skeleton.h"
#include "Scenes/ActorState.h"
#include "Scenes/CharacterStep.h"
#include "Scenes/GameMovementState.h"
#include "Scenes/MapLoad.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/PlayerActionSpeeds.h"
#include "Scenes/SceneId.h"
#include "Scenes/WorldMap.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "World/TerrainCulling.h"
#include "World/TerrainHeight.h"
#include "World/TerrainWalls.h"
#include "World/WorldTerrain.h"
#include "support/FileBytes.h"
#include "support/GoldenDir.h"
#include "support/PlayerActors.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

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

// H2 T16: matriz del consumidor MoveHero con terreno contractual explícito y Skill.bmd real.
// Los nombres KernelOnly evitan confundir una llamada aislada con entrada/1C/regen/render nativos.
// La grilla plana vale 75, está abierta salvo donde el test crea un corredor/borde; Player sólo
// presta 205 acciones sin geometría. El picker presta una celda explícita: no valida rayos ni GL.
namespace
{
using Client::Core::Expected;
using Client::GameLogic::PathCell;
using Client::Protocol::Movement::Cell;
using Client::Scenes::ActorState;
using Client::Scenes::HeroMovementContext;
using Client::Scenes::StepHeroMovement;

struct MapCase
{
    std::uint8_t map;
    const char* family;
};

constexpr std::array<MapCase, 37> MovementMaps{{
    {0, "Lorencia"},
    {1, "Dungeon"},
    {2, "Devias"},
    {3, "Noria"},
    {4, "LostTower"},
    {5, "MissingWorld6"},
    {6, "Arena"},
    {7, "AtlansBubblePoolBoundary"},
    {8, "Tarkan"},
    {9, "DevilSquareEventBoundary"},
    {10, "Icarus"},
    {11, "BloodCastleEventBoundary"},
    {12, "BloodCastleEventBoundary"},
    {13, "BloodCastleEventBoundary"},
    {14, "BloodCastleEventBoundary"},
    {15, "BloodCastleEventBoundary"},
    {16, "BloodCastleEventBoundary"},
    {17, "BloodCastleEventBoundary"},
    {18, "ChaosCastleCellReportBoundary"},
    {19, "ChaosCastleCellReportBoundary"},
    {20, "ChaosCastleCellReportBoundary"},
    {21, "ChaosCastleCellReportBoundary"},
    {22, "ChaosCastleCellReportBoundary"},
    {23, "ChaosCastleCellReportBoundary"},
    {24, "KalimaLoadBoundary"},
    {25, "KalimaLoadBoundary"},
    {26, "KalimaLoadBoundary"},
    {27, "KalimaLoadBoundary"},
    {28, "KalimaLoadBoundary"},
    {29, "KalimaLoadBoundary"},
    {30, "SiegeLoadBoundary"},
    {31, "LorenDeep"},
    {32, "BeforeAliasToNine"},
    {33, "Aida"},
    {34, "CrywolfLoadBoundary"},
    {35, "MissingWorld36"},
    {36, "KalimaLoadBoundary"},
}};

constexpr float FlatHeight = 75.0f;
constexpr std::uint32_t Seed = 0x1616;
constexpr int MaximumMovementSteps = 256;
// Dos cierres de espera, tres lecturas de celda en el clic y el sufijo propio/Skill real.
constexpr int CommonPrefixRandoms = 2 * Client::Render::Models::SmallLogicalCloseRandoms +
                                    Client::Render::Models::OwnRecordCloseRandoms +
                                    static_cast<int>(Client::Data::Tables::SkillRecordSize);
constexpr int ClickPrefixRandoms = CommonPrefixRandoms + Client::Render::Models::OwnRecordCloseRandoms +
                                   3 * Client::Render::Models::SmallLogicalCloseRandoms + 2;
// Walk ordinario saca1; el inicio de MovePath escribe X/Y con dos cierres más.
constexpr int FirstMovingPrefixRandoms = CommonPrefixRandoms + 1 + 2 * Client::Render::Models::SmallLogicalCloseRandoms;
constexpr float CellSize = 100.0f;
constexpr float CellCentre = 50.0f;

bool IsChaosCastle(std::uint8_t map)
{
    return map >= 18 && map <= 23;
}

bool IsKalima(std::uint8_t map)
{
    return (map >= 24 && map <= 29) || map == 36;
}

std::string_view CharacterFrontier(std::uint8_t map)
{
    if (map == 30)
    {
        return "0x412140";
    }
    if (IsChaosCastle(map))
    {
        return "0x4fbe10";
    }
    if (map == 7)
    {
        // La burbuja de Atlans (0x506d0a) con el reloj en 0 pide el pool de partículas, que este paso no da.
        return "0x506d77";
    }
    if (map == 9)
    {
        return "0x506d8b";
    }
    return IsKalima(map) ? "0x5087fc" : "";
}

class GameMovementMapMatrix : public Client::Tests::GoldenDataTest, public ::testing::WithParamInterface<MapCase>
{
protected:
    void SetUp() override
    {
        GoldenDataTest::SetUp();
        const auto path = DataFile("Local\\Skill.bmd");
        ASSERT_TRUE(path.has_value());
        const auto bytes = Client::Tests::ReadFileBytes(*path);
        ASSERT_TRUE(bytes.has_value());
        auto skills = Client::Data::Tables::LoadSkillTable(std::as_bytes(std::span(*bytes)));
        ASSERT_TRUE(skills.HasValue());
        m_skills = std::move(skills).Value();
        m_world.Runtime().map = GetParam().map;
        m_world.Runtime().hero = 0;
        m_world.Runtime().terrainPolygon.everything = true;
        m_world.OwnCharacter().ClearEquipmentForEntry();
        auto& terrain = m_world.Map().terrain;
        terrain.attributes = std::make_unique<Client::Data::Terrain::TerrainAttributes>();
        terrain.heights = std::make_unique<Client::Data::Terrain::TerrainHeights>();
        terrain.heights->cells.fill(FlatHeight);
        terrain.mapping = std::make_unique<Client::Data::Terrain::TerrainMapping>();
        m_skeleton = Client::Tests::PlayerActionsSkeleton();
        for (auto& action : m_skeleton.actions)
        {
            action.keyCount = 20;
        }
        PutAt({10, 20});
    }

    ActorState& Hero()
    {
        return m_world.Actors()[0];
    }

    void PutAt(PathCell cell)
    {
        Hero() = Client::Tests::UnarmedPlayer();
        Hero().alive = true;
        // Estado construido, opaco y quieto: evita las guardas nativas de alfa/acción del clic.
        Hero().alpha = Hero().alphaTarget = 1.0f;
        Hero().animation.action = Hero().animation.previousAction = 1;
        Hero().byte88 = 1;
        Hero().baseMovementSpeed = 10;
        Hero().cellX = Hero().targetX = cell.x;
        Hero().cellY = Hero().targetY = cell.y;
        Hero().position = {static_cast<float>(cell.x) * CellSize + CellCentre,
                           static_cast<float>(cell.y) * CellSize + CellCentre, FlatHeight};
        Hero().key = 0x123;
        Hero().revision = 7;
    }

    Expected<void, std::string> Step(bool click = false)
    {
        HeroMovementContext context{
            .world = m_world, .random = m_random, .playerSkeleton = &m_skeleton, .skills = &*m_skills};
        context.input.leftPressed = click;
        context.input.worldTime = static_cast<float>(m_ticks) * 40.0f;
        context.input.ticksMs = m_ticks++ * 40;
        context.pickTarget = [&]() -> Expected<std::optional<PathCell>, std::string> { return m_target; };
        context.sendWalk = [&](std::span<const Cell> cells, Cell target, float yaw) -> Expected<void, std::string>
        {
            const auto packet = Client::Protocol::Movement::WriteWalk(cells, target, yaw);
            if (!packet.HasValue())
            {
                return Client::Core::Unexpected(std::string("El callback recibió un camino D3 inválido"));
            }
            const auto decoded = Client::Network::Crypto::Xor32Decode(packet.Value().packet);
            m_packets.emplace_back(decoded.Bytes().begin(), decoded.Bytes().end());
            m_sentPath.assign(cells.begin(), cells.end());
            m_sentTarget = target;
            return {};
        };
        return StepHeroMovement(context);
    }

    Expected<void, std::string> Click(PathCell target)
    {
        m_target = target;
        auto& movement = m_world.Runtime().movement;
        movement.mouseUpdateTime = movement.mouseUpdateTimeMax;
        return Step(true);
    }

    Expected<void, std::string> Finish()
    {
        for (int step = 0; step < MaximumMovementSteps && Hero().movement != 0; ++step)
        {
            const auto moved = Step();
            if (!moved.HasValue())
            {
                return moved;
            }
        }
        if (Hero().movement != 0)
        {
            return Client::Core::Unexpected(std::string("El fixture no llegó en 256 pasos"));
        }
        return {};
    }

    Client::Scenes::WorldState m_world;
    Client::Render::Models::CrtRandom m_random{Seed};
    Client::Render::Models::Skeleton m_skeleton;
    std::optional<Client::Data::Tables::SkillTable> m_skills;
    std::optional<PathCell> m_target;
    std::vector<std::vector<std::byte>> m_packets;
    std::vector<Cell> m_sentPath;
    Cell m_sentTarget{};
    std::uint32_t m_ticks = 0;
};

TEST_P(GameMovementMapMatrix, StraightClickPublishesD3BeforeMovingAndRetainsTheSameActor)
{
    ActorState* const actor = &Hero();
    m_world.Runtime().movement.heldSince = 73.0f;
    const auto clicked = Click({13, 20});
    ASSERT_TRUE(clicked.HasValue()) << clicked.Error();
    ASSERT_EQ(m_packets.size(), 1U);
    EXPECT_EQ(m_packets[0][0], std::byte{0xc1});
    EXPECT_EQ(m_packets[0][2], std::byte{0xd3});
    ASSERT_EQ(m_sentPath.size(), 4U);
    EXPECT_EQ(m_sentPath.front().x, 10);
    EXPECT_EQ(m_sentPath.front().y, 20);
    EXPECT_EQ(m_sentTarget.x, 13);
    EXPECT_EQ(m_sentTarget.y, 20);
    EXPECT_FLOAT_EQ(Hero().position.x, 1050.0f);
    EXPECT_EQ(Hero().movement, 1);
    EXPECT_EQ(m_random.State(), Client::Tests::AfterDraws(Seed, ClickPrefixRandoms).State());
    const auto moved = Step();
    ASSERT_TRUE(moved.HasValue()) << moved.Error();
    EXPECT_FLOAT_EQ(Hero().position.x, IsChaosCastle(GetParam().map) ? 1065.0f : 1062.0f);
    EXPECT_FLOAT_EQ(Hero().position.y, 2050.0f);
    EXPECT_FLOAT_EQ(Hero().position.z, FlatHeight);
    EXPECT_EQ(Hero().cellX, 11);
    EXPECT_EQ(m_random.State(), Client::Tests::AfterDraws(Seed, ClickPrefixRandoms + FirstMovingPrefixRandoms).State());
    const auto arrived = Finish();
    ASSERT_TRUE(arrived.HasValue()) << arrived.Error();
    EXPECT_EQ(&Hero(), actor);
    EXPECT_EQ(Hero().key, 0x123);
    EXPECT_EQ(Hero().revision, 7U);
    EXPECT_FLOAT_EQ(Hero().position.x, 1350.0f);
    EXPECT_FLOAT_EQ(Hero().position.y, 2050.0f);
    EXPECT_EQ(Hero().cellX, 13);
    EXPECT_EQ(Hero().cellY, 20);
    EXPECT_EQ(m_packets.size(), 1U);
    EXPECT_EQ(m_world.Runtime().movement.targetX, 13U);
    EXPECT_EQ(m_world.Runtime().movement.targetY, 20U);
    EXPECT_GT(m_world.Runtime().movement.heldSince, 73.0f);
    EXPECT_EQ(m_world.Runtime().movement.arrivalYaw, 90);
    EXPECT_EQ(m_world.Runtime().ownRecordDepth, 0);
    EXPECT_EQ(m_world.Runtime().warpDepth, 0);
    EXPECT_EQ(Hero().cellXAccesses, 0);
    EXPECT_EQ(Hero().cellYAccesses, 0);
    const auto skill = m_skills->Record(0);
    ASSERT_TRUE(skill.has_value());
    EXPECT_FLOAT_EQ(m_world.Runtime().movement.selectedSkillRange,
                    static_cast<float>(std::to_integer<std::uint8_t>((*skill)[0x26])));
    EXPECT_FLOAT_EQ(m_world.ActionSpeeds().Speed(13, 99.0f), 0.33f);
    EXPECT_FLOAT_EQ(m_skeleton.actions[13].speed, 0.0f);
}

TEST_P(GameMovementMapMatrix, AForcedCornerPreservesTheCorridorInTheStoredPath)
{
    auto& walls = m_world.Map().terrain.attributes->cells;
    walls.fill(Client::World::BlockedWallBit);
    for (std::uint8_t x = 10; x <= 14; ++x)
    {
        walls[Client::Data::Terrain::CellIndex(x, 20)] = 0;
    }
    for (std::uint8_t y = 20; y <= 24; ++y)
    {
        walls[Client::Data::Terrain::CellIndex(14, y)] = 0;
    }
    const auto clicked = Click({14, 24});
    ASSERT_TRUE(clicked.HasValue()) << clicked.Error();
    ASSERT_EQ(m_packets.size(), 1U);
    ASSERT_GE(m_sentPath.size(), 7U);
    for (const Cell cell : m_sentPath)
    {
        EXPECT_EQ(walls[Client::Data::Terrain::CellIndex(cell.x, cell.y)], 0);
    }
    bool headedEast = false;
    bool turnedNorth = false;
    for (int step = 0; step < MaximumMovementSteps && Hero().movement != 0; ++step)
    {
        const auto moved = Step();
        ASSERT_TRUE(moved.HasValue()) << moved.Error();
        headedEast = headedEast || Hero().angles.z == 90.0f;
        turnedNorth = turnedNorth || Hero().angles.z > 100.0f;
        EXPECT_EQ(walls[Client::Data::Terrain::CellIndex(Hero().cellX, Hero().cellY)], 0);
    }
    EXPECT_TRUE(headedEast);
    EXPECT_TRUE(turnedNorth);
    EXPECT_EQ(Hero().movement, 0);
    EXPECT_EQ(Hero().cellX, 14);
    EXPECT_EQ(Hero().cellY, 24);
    EXPECT_FLOAT_EQ(Hero().position.x, 1450.0f);
    EXPECT_FLOAT_EQ(Hero().position.y, 2450.0f);
    EXPECT_EQ(m_packets.size(), 1U);
}

TEST_P(GameMovementMapMatrix, ARouteAtTheUpperBorderDoesNotWrapTheLogicalCell)
{
    PutAt({254, 254});
    const auto clicked = Click({255, 255});
    ASSERT_TRUE(clicked.HasValue()) << clicked.Error();
    ASSERT_EQ(m_packets.size(), 1U);
    for (int step = 0; step < MaximumMovementSteps && Hero().movement != 0; ++step)
    {
        const auto moved = Step();
        ASSERT_TRUE(moved.HasValue()) << moved.Error();
        EXPECT_GE(Hero().cellX, 254);
        EXPECT_GE(Hero().cellY, 254);
        EXPECT_GE(Hero().position.x, 25450.0f);
        EXPECT_GE(Hero().position.y, 25450.0f);
        EXPECT_FLOAT_EQ(Hero().position.z, FlatHeight);
    }
    EXPECT_EQ(Hero().movement, 0);
    EXPECT_EQ(Hero().cellX, 255);
    EXPECT_EQ(Hero().cellY, 255);
    EXPECT_FLOAT_EQ(Hero().position.x, 25550.0f);
    EXPECT_FLOAT_EQ(Hero().position.y, 25550.0f);
}

TEST_P(GameMovementMapMatrix, AZeroStartAxisRetainsTheOldPathAndSendsNothing)
{
    PutAt({0, 20});
    Hero().path.count = 1;
    Hero().path.cells[0] = {9, 9};
    const auto clicked = Click({1, 20});
    ASSERT_TRUE(clicked.HasValue()) << clicked.Error();
    EXPECT_TRUE(m_packets.empty());
    EXPECT_EQ(Hero().movement, 0);
    EXPECT_EQ(Hero().path.count, 1);
    EXPECT_EQ(Hero().path.cells[0].x, 9);
    EXPECT_FLOAT_EQ(Hero().position.x, CellCentre);
    EXPECT_EQ(m_world.Runtime().movement.targetX, 1U);
    EXPECT_FALSE(m_world.Runtime().movement.pendingClick);
}

TEST_P(GameMovementMapMatrix, SafeZoneResetsRunBeforeTheChaosCastleSpeedOverride)
{
    Hero().safeZone = 0x80;
    Hero().run = 39;
    m_world.Map().terrain.attributes->cells.fill(Client::World::SafeZoneWallBit);
    const auto clicked = Click({13, 20});
    ASSERT_TRUE(clicked.HasValue()) << clicked.Error();
    const auto moved = Step();
    ASSERT_TRUE(moved.HasValue()) << moved.Error();
    EXPECT_EQ(Hero().run, IsChaosCastle(GetParam().map) ? 40 : 0);
    EXPECT_EQ(Hero().animation.action, 13);
    EXPECT_FLOAT_EQ(Hero().position.x, IsChaosCastle(GetParam().map) ? 1065.0f : 1062.0f);
    EXPECT_EQ(Hero().safeZone, 0x80);
}

TEST_P(GameMovementMapMatrix, TheLogicalSafeCellIsPublishedUnlessTheBodyStopsEarlier)
{
    m_world.Map().terrain.attributes->cells[Client::Data::Terrain::CellIndex(11, 20)] = Client::World::SafeZoneWallBit;
    const auto clicked = Click({13, 20});
    ASSERT_TRUE(clicked.HasValue()) << clicked.Error();
    const auto moved = Step();
    ASSERT_TRUE(moved.HasValue()) << moved.Error();
    ASSERT_EQ(Hero().cellX, 11);
    ASSERT_LT(Hero().position.x, 1100.0f);
    const auto stepped =
        Client::Scenes::StepCharacters({.world = m_world, .random = m_random, .playerSkeleton = &m_skeleton});
    const auto frontier = CharacterFrontier(GetParam().map);
    if (frontier.empty())
    {
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    }
    else
    {
        ASSERT_FALSE(stepped.HasValue());
        EXPECT_NE(stepped.Error().find(frontier), std::string::npos) << stepped.Error();
    }
    const bool beforeSafeZone = GetParam().map == 30 || IsChaosCastle(GetParam().map);
    EXPECT_EQ(Hero().safeZone, beforeSafeZone ? 0 : 1);
}

INSTANTIATE_TEST_SUITE_P(KernelOnly, GameMovementMapMatrix, ::testing::ValuesIn(MovementMaps),
                         [](const ::testing::TestParamInfo<MapCase>& mapInfo)
                         { return "Map" + std::to_string(mapInfo.param.map) + mapInfo.param.family; });

using GameMovementMapEvidenceTest = Client::Tests::GoldenDataTest;

TEST_F(GameMovementMapEvidenceTest, EntryFrontiersStaySeparateFromTheSyntheticWalkingMatrix)
{
    struct Boundary
    {
        std::uint8_t map;
        std::string_view message;
    };
    constexpr std::array<Boundary, 5> Boundaries{{
        {5, "World6"},
        {35, "World36"},
        {30, "0x42a760"},
        {34, "0x42fe50"},
        {55, "0x6188ae"},
    }};
    for (const auto boundary : Boundaries)
    {
        SCOPED_TRACE(boundary.map);
        Client::Scenes::WorldState world;
        world.Runtime().map = boundary.map;
        Client::Render::Models::CrtRandom random(Seed);
        const auto loaded =
            Client::Scenes::LoadMap(boundary.map, {m_dataRoot, world, Client::Scenes::SceneId::Game, 5, random});
        ASSERT_FALSE(loaded.HasValue());
        EXPECT_NE(loaded.Error().find(boundary.message), std::string::npos) << loaded.Error();
    }
}

TEST_F(GameMovementMapEvidenceTest, TheAliasAndEventFamiliesStillRequireTheirEventObjects)
{
    Client::Scenes::WorldState world;
    world.Runtime().map = 32;
    Client::Render::Models::CrtRandom random(Seed);
    const auto loaded = Client::Scenes::LoadMap(32, {m_dataRoot, world, Client::Scenes::SceneId::Game, 5, random});
    ASSERT_TRUE(loaded.HasValue()) << loaded.Error();
    EXPECT_EQ(world.Runtime().map, 9);
    for (std::int32_t map = 9; map <= 23; ++map)
    {
        if (map == 10)
        {
            continue;
        }
        const auto event = Client::Scenes::CheckEventMap(map);
        ASSERT_FALSE(event.HasValue());
        EXPECT_NE(event.Error().find("0x447d70"), std::string::npos);
    }
}

TEST_F(GameMovementMapEvidenceTest, TheTwoSceneMapsExposeTerrainHeightsWithoutClaimingHeroMovement)
{
    for (const std::uint8_t map : std::array<std::uint8_t, 2>{54, 55})
    {
        SCOPED_TRACE(map);
        const auto loaded = Client::World::LoadWorldTerrain(m_dataRoot, map);
        ASSERT_TRUE(loaded.HasValue()) << loaded.Error();
        const auto& terrain = loaded.Value();
        ASSERT_NE(terrain.attributes, nullptr);
        ASSERT_NE(terrain.heights, nullptr);
        const double height = Client::World::TerrainHeightAt(*terrain.heights, *terrain.attributes, 10050.0f, 10050.0f);
        EXPECT_TRUE(std::isfinite(height));
        EXPECT_GE(height, 0.0);
        // 54/55 corren otras escenas: no se llama StepHeroMovement aquí.
    }
}

TEST(GameMovementMapHeightTest, TheCastleFixedHeightContractDoesNotClaimItsLoaderIsImplemented)
{
    auto heights = std::make_unique<Client::Data::Terrain::TerrainHeights>();
    heights->cells.fill(FlatHeight);
    auto attributes = std::make_unique<Client::Data::Terrain::TerrainAttributes>();
    constexpr std::uint16_t FixedHeightBit = 0x40;
    attributes->cells[Client::Data::Terrain::CellIndex(10, 20)] = FixedHeightBit;
    EXPECT_DOUBLE_EQ(Client::World::TerrainHeightAt(*heights, *attributes, 1050.0f, 2050.0f, 1201.0f), 1201.0);
    EXPECT_DOUBLE_EQ(Client::World::TerrainHeightAt(*heights, *attributes, 1150.0f, 2050.0f, 1201.0f), FlatHeight);
}
} // namespace
