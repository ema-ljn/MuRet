#include "Scenes/SelectionState.h"

#include "App/CharacterEvent.h"
#include "Core/Expected.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Data/Terrain/TerrainHeights.h"
#include "Protocol/Character/CharacterList.h"
#include "Render/Models/Cloth.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "World/Particles.h"
#include "World/GameCamera.h"
#include "World/TerrainCulling.h"
#include "World/WorldTerrain.h"
#include "support/GoldenDir.h"
#include "support/TemporaryFolder.h"
#include "support/PlayerActors.h"

#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_float2.hpp>

#include <gtest/gtest.h>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace
{
using Client::App::CharacterCreated;
using Client::App::CharacterDeleted;
using Client::App::CharacterEvent;
using Client::App::CharacterListReceived;
using Client::Protocol::Character::CharacterEntry;
using Client::Scenes::SelectionState;
using Client::Scenes::WorldState;
using Client::World::CullingScene;

Client::World::WorldTerrain TerrainOf(float height, std::uint16_t attribute = 0)
{
    Client::World::WorldTerrain terrain;
    terrain.heights = std::make_unique<Client::Data::Terrain::TerrainHeights>();
    terrain.attributes = std::make_unique<Client::Data::Terrain::TerrainAttributes>();
    terrain.heights->cells.fill(height);
    terrain.attributes->cells.fill(attribute);
    return terrain;
}

CharacterEntry Entry(std::uint8_t slot = 0, bool pet = false)
{
    CharacterEntry entry;
    entry.slot = slot;
    entry.withoutEquipment = !pet;
    if (pet)
    {
        entry.equipment[4] = std::byte{3};
        entry.equipment[9] = std::byte{1};
    }
    return entry;
}

CharacterEvent List(std::initializer_list<CharacterEntry> entries)
{
    return CharacterListReceived{.list = {.characters = std::vector<CharacterEntry>(entries)}};
}

class SelectionStateTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_state.BeginSelection();
        m_state.RetainTerrain(55, TerrainOf(100.0f));
    }

    Client::Core::Expected<void, std::string> Apply(std::initializer_list<CharacterEvent> events)
    {
        return m_state.Apply(std::span(events.begin(), events.size()), m_camera, m_input);
    }

    WorldState m_world;
    SelectionState m_state{m_world};
    Client::World::GameCamera m_camera;
    Client::World::GameCameraInput m_input;
};

// Atrapa consultas con XY finales: sólo las esquinas de (50,50) dan 100.
TEST_F(SelectionStateTest, ConstructionReadsTheInitialCellBeforePlacingTheSlot)
{
    auto terrain = TerrainOf(900.0f);
    for (const std::size_t cell : {0u, 1u, 256u, 257u})
    {
        terrain.heights->cells[cell] = 100.0f;
    }
    m_state.RetainTerrain(55, std::move(terrain));
    ASSERT_TRUE(Apply({List({Entry()})}).HasValue());
    EXPECT_EQ(m_state.Slots()[0].position.x, 12880.0f);
    EXPECT_EQ(m_state.Slots()[0].position.y, 15046.0f);
    EXPECT_EQ(m_state.Slots()[0].position.z, 100.0f);
    EXPECT_EQ(m_input.hero.x, 12880.0f);
    EXPECT_EQ(m_input.hero.y, 15046.0f);
    EXPECT_EQ(m_input.hero.z, 100.0f);
    EXPECT_EQ(m_state.Slots()[0].revision, 1u);
}

// 0x4c20a0 → 0x517270: el mismo registro 0x20a que avanza 0x508f00; el constructor
// conserva los cuatro campos de cabeza que no escribe 0x511290.
TEST_F(SelectionStateTest, TheListConstructsTheCompletePlayerAndKeepsItsHead)
{
    auto& actor = m_world.Actors()[3];
    actor.headTurn.angles = {17.0f, 23.0f};
    actor.headTurn.target = {31.0f, 47.0f};
    CharacterEntry entry = Entry(3);
    entry.classByte = 0x40; // Elf: 0x4f2250 elige la acción 2 sin armas.
    entry.flags = 0x11;

    ASSERT_TRUE(Apply({List({entry})}).HasValue());

    EXPECT_TRUE(actor.alive);
    EXPECT_EQ(actor.type, 0x20au);
    EXPECT_EQ(actor.key, 3u);
    EXPECT_EQ(actor.characterClass, 2u);
    EXPECT_EQ(actor.skin, 0u);
    EXPECT_EQ(actor.body[0].model, 0x2214u);
    EXPECT_EQ(actor.hands[0].model, 0xffffu);
    EXPECT_EQ(actor.hands[1].model, 0xffffu);
    EXPECT_EQ(actor.wings.model, 0xffffu);
    EXPECT_EQ(actor.pet.model, 0xffffu);
    EXPECT_FLOAT_EQ(actor.angles.z, 25.0f);
    EXPECT_FLOAT_EQ(actor.alpha, 1.0f);
    EXPECT_EQ(actor.pk, 3u);
    EXPECT_EQ(actor.byte88, 1u);
    EXPECT_EQ(actor.controlCode, 0x11u);
    EXPECT_EQ(actor.headTurn.angles, (glm::vec2{17.0f, 23.0f}));
    EXPECT_EQ(actor.headTurn.target, (glm::vec2{31.0f, 47.0f}));
}

TEST_F(SelectionStateTest, TheNativeListConstructorUsesTheSharedRandomBeforeApplyingEquipment)
{
    constexpr std::uint32_t Seed = 0x12345678;
    Client::Render::Models::ClothWorld cloth{.random = Client::Render::Models::CrtRandom(Seed)};
    const auto skeleton = Client::Tests::PlayerActionsSkeleton();
    CharacterEntry entry = Entry(3);
    entry.classByte = 0x40;
    entry.level = 123;
    const std::array<CharacterEvent, 1> events{List({entry})};

    ASSERT_TRUE(m_state.Apply(events, m_camera, m_input, &cloth, &skeleton).HasValue());

    EXPECT_EQ(m_world.Actors()[3].animation.action, 2u);
    EXPECT_EQ(m_world.Actors()[3].level, 123u);
    EXPECT_EQ(cloth.random.State(), Client::Tests::AfterDraws(Seed, 29).State());
}

TEST_F(SelectionStateTest, TheRavenConstructorPrecedesTheGuardianAndKeepsTheListIdentity)
{
    constexpr std::uint32_t Seed = 0x12345678;
    Client::Render::Models::ClothWorld cloth{.random = Client::Render::Models::CrtRandom(Seed)};
    const auto skeleton = Client::Tests::PlayerActionsSkeleton();
    CharacterEntry entry;
    entry.slot = 4;
    entry.classByte = 0x80;
    entry.flags = 0x11;
    entry.name[0] = std::byte{'X'};
    entry.level = 321;
    entry.guildStatus = 7;
    entry.equipment[1] = std::byte{0x05};
    entry.equipment[12] = std::byte{0xa0};
    const std::array<CharacterEvent, 1> events{List({entry})};

    ASSERT_TRUE(m_state.Apply(events, m_camera, m_input, &cloth, &skeleton).HasValue());

    const auto& actor = m_world.Actors()[4];
    ASSERT_TRUE(actor.darkSpirit.has_value());
    EXPECT_EQ(actor.hands[1].model, 0x1c17u);
    EXPECT_EQ(actor.name, entry.name);
    EXPECT_EQ(actor.level, 321u);
    EXPECT_EQ(actor.guildStatus, 7u);
    EXPECT_EQ(actor.controlCode, 0x11u);
    EXPECT_EQ(actor.animation.action, 1u);
    EXPECT_EQ(cloth.random.State(), Client::Tests::AfterDraws(Seed, 44).State());
    EXPECT_EQ(m_world.Helpers()[0].type, 0x1c12u);
}

TEST_F(SelectionStateTest, ALaterPetOverrideKeepsTheHelperCreatedByTheEarlierPacketBits)
{
    constexpr std::uint32_t Seed = 0x12345678;
    Client::Render::Models::ClothWorld cloth{.random = Client::Render::Models::CrtRandom(Seed)};
    const auto skeleton = Client::Tests::PlayerActionsSkeleton();
    CharacterEntry entry;
    entry.equipment[11] = std::byte{1};
    const std::array<CharacterEvent, 1> events{List({entry})};

    ASSERT_TRUE(m_state.Apply(events, m_camera, m_input, &cloth, &skeleton).HasValue());

    EXPECT_EQ(m_world.Actors()[0].pet.model, 0x1c16u);
    EXPECT_TRUE(m_world.Helpers()[0].alive);
    EXPECT_EQ(m_world.Helpers()[0].type, 0x1c12u);
    EXPECT_TRUE(m_world.Helpers()[1].alive);
    EXPECT_EQ(m_world.Helpers()[1].type, 0xbfu);
    EXPECT_EQ(cloth.random.State(), Client::Tests::AfterDraws(Seed, 32).State());
}

TEST_F(SelectionStateTest, BssSlotsAndAnEmptyPollPreserveTheCurrentHero)
{
    m_input.hero = {7.0f, 8.0f, 9.0f};
    ASSERT_TRUE(Apply({}).HasValue());
    for (const auto& slot : m_state.Slots())
    {
        EXPECT_EQ(slot.position.x, 0.0f);
        EXPECT_EQ(slot.position.y, 0.0f);
        EXPECT_EQ(slot.position.z, 0.0f);
        EXPECT_EQ(slot.revision, 0u);
        EXPECT_FALSE(slot.previousHad1c15);
    }
    EXPECT_EQ(m_input.hero.z, 9.0f);
}

TEST_F(SelectionStateTest, OnlyTheRecordsPresentInTheListConstruct)
{
    m_input.hero = {7.0f, 8.0f, 9.0f};
    ASSERT_TRUE(Apply({List({Entry(4)})}).HasValue());
    EXPECT_EQ(m_state.Slots()[0].revision, 0u);
    EXPECT_EQ(m_state.Slots()[4].revision, 1u);
    EXPECT_EQ(m_state.Slots()[4].position.x, 13349.6f);
    EXPECT_EQ(m_state.Slots()[4].position.y, 15527.0f);
    EXPECT_EQ(m_state.Slots()[4].position.z, 100.0f);
    EXPECT_EQ(m_input.hero.z, 9.0f);
}

TEST_F(SelectionStateTest, AnEmptyListIsReceivedWithoutAConstructor)
{
    ASSERT_TRUE(Apply({List({})}).HasValue());
    EXPECT_TRUE(m_state.HasReceivedList());
    EXPECT_FALSE(m_state.IsInitialized());
    EXPECT_EQ(m_state.Map(), 55);
    EXPECT_EQ(m_state.Slots()[0].revision, 0u);
}

TEST_F(SelectionStateTest, EveryIdenticalReceptionConstructsAgainUsingThePreviousPet)
{
    ASSERT_TRUE(Apply({List({Entry(0, true)}), List({Entry(0, true)})}).HasValue());
    EXPECT_EQ(m_state.Slots()[0].revision, 2u);
    EXPECT_EQ(m_state.Slots()[0].position.z, 130.0f);
    EXPECT_TRUE(m_state.Slots()[0].previousHad1c15);
}

TEST_F(SelectionStateTest, DuplicateSlotsAreAppliedInRecordOrder)
{
    ASSERT_TRUE(Apply({List({Entry(0, true), Entry(0)})}).HasValue());
    EXPECT_EQ(m_state.Slots()[0].revision, 2u);
    EXPECT_EQ(m_state.Slots()[0].position.z, 130.0f);
    EXPECT_FALSE(m_state.Slots()[0].previousHad1c15);
}

TEST_F(SelectionStateTest, ACreationConsumesTheOldPetAndThenClearsItsEquipment)
{
    ASSERT_TRUE(Apply({List({Entry(0, true)}), CharacterCreated{Entry()}, CharacterCreated{Entry()}}).HasValue());
    EXPECT_EQ(m_state.Slots()[0].revision, 3u);
    EXPECT_EQ(m_state.Slots()[0].position.z, 100.0f);
    EXPECT_FALSE(m_state.Slots()[0].previousHad1c15);
}

TEST_F(SelectionStateTest, TheOffsetUsesTheExactWorldGuardAndWorldEightOrTen)
{
    struct Case
    {
        int map;
        float expected;
    };
    for (const Case test : {Case{-1, 100.0f}, Case{-2, 130.0f}, Case{8, 190.0f}, Case{10, 190.0f}, Case{55, 130.0f}})
    {
        SCOPED_TRACE(test.map);
        WorldState world;
        SelectionState state(world);
        state.BeginSelection();
        state.RetainTerrain(test.map, TerrainOf(100.0f));
        const std::array<CharacterEvent, 2> events{List({Entry(0, true)}), CharacterCreated{Entry()}};
        ASSERT_TRUE(state.Apply(events, m_camera, m_input).HasValue());
        EXPECT_EQ(state.Slots()[0].position.z, test.expected);
    }
}

TEST_F(SelectionStateTest, NewSafetyComesFromAttributeZeroAndCancelsTheOldPetOffset)
{
    ASSERT_TRUE(Apply({List({Entry(0, true)})}).HasValue());
    auto terrain = TerrainOf(200.0f);
    terrain.attributes->cells[0] = 1;
    m_state.RetainTerrain(8, std::move(terrain));
    ASSERT_TRUE(Apply({CharacterCreated{Entry()}}).HasValue());
    EXPECT_EQ(m_state.Slots()[0].position.z, 200.0f);
}

TEST_F(SelectionStateTest, HeightIsSkippedOnlyInScenesZeroOneAndThree)
{
    for (const std::uint8_t scene : {std::uint8_t{0}, std::uint8_t{1}, std::uint8_t{3}})
    {
        SCOPED_TRACE(static_cast<int>(scene));
        m_state.SetScene(static_cast<CullingScene>(scene));
        ASSERT_TRUE(Apply({CharacterCreated{Entry(0, true)}}).HasValue());
        ASSERT_TRUE(Apply({CharacterCreated{Entry()}}).HasValue());
        EXPECT_EQ(m_state.Slots()[0].position.z, 30.0f);
    }
    for (const CullingScene scene : {CullingScene::Login, CullingScene::CharacterSelect, CullingScene::Game})
    {
        m_state.SetScene(scene);
        ASSERT_TRUE(Apply({CharacterCreated{Entry()}}).HasValue());
        EXPECT_EQ(m_state.Slots()[0].position.z, 100.0f);
    }
}

TEST_F(SelectionStateTest, FixedHeightIsLiveAndIsNotResetToItsPeInitializer)
{
    m_camera.fixedHeight = 87.25f;
    m_state.RetainTerrain(55, TerrainOf(400.0f, 0x40));
    ASSERT_TRUE(Apply({List({Entry(0, true)}), CharacterCreated{Entry()}}).HasValue());
    EXPECT_EQ(m_state.Slots()[0].position.z, 117.25f);
    EXPECT_EQ(m_camera.fixedHeight, 87.25f);
}

TEST_F(SelectionStateTest, TheBranchWithoutAnOffsetPreservesNegativeZero)
{
    m_camera.fixedHeight = -0.0f;
    m_state.RetainTerrain(55, TerrainOf(400.0f, 0x40));
    ASSERT_TRUE(Apply({List({Entry()})}).HasValue());
    EXPECT_EQ(std::bit_cast<std::uint32_t>(m_state.Slots()[0].position.z), 0x80000000u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(m_input.hero.z), 0x80000000u);
}

TEST_F(SelectionStateTest, TheHeightIsStoredAsFloatOnlyAfterAddingThePetOffset)
{
    ASSERT_TRUE(Apply({List({Entry(0, true)})}).HasValue());
    auto terrain = TerrainOf(1.00000095367431640625f);
    terrain.heights->cells[0] = 1.00000107288360595703125f;
    m_state.RetainTerrain(55, std::move(terrain));
    ASSERT_TRUE(Apply({CharacterCreated{Entry()}}).HasValue());
    // h=1+2^-20+2^-25; h+30 redondea a 31+2^-19. Redondear h primero daría 31.
    EXPECT_EQ(std::bit_cast<std::uint32_t>(m_state.Slots()[0].position.z), 0x41f80001u);
}

TEST_F(SelectionStateTest, DrawingWritesAllFiveHeightsAndLeavesTheirPositionsAndHistory)
{
    ASSERT_TRUE(Apply({List({Entry(1, true)})}).HasValue());
    m_input.hero = {7.0f, 8.0f, 9.0f};
    m_state.DrawSelection(m_input);
    for (const auto& slot : m_state.Slots())
    {
        EXPECT_EQ(slot.position.z, 163.0f);
        EXPECT_EQ(slot.light, glm::vec3(0.0f));
    }
    EXPECT_EQ(m_state.Slots()[0].position.x, 0.0f);
    EXPECT_EQ(m_state.Slots()[1].position.x, 12863.0f);
    EXPECT_EQ(m_state.Slots()[1].position.y, 15251.0f);
    EXPECT_TRUE(m_state.Slots()[1].previousHad1c15);
    EXPECT_EQ(m_state.Slots()[1].revision, 1u);
    EXPECT_EQ(m_input.hero.x, 7.0f);
    EXPECT_EQ(m_input.hero.y, 8.0f);
    EXPECT_EQ(m_input.hero.z, 163.0f);
}

// Q-C213: después de un F1 02 con 1 el héroe publicado es el lugar que sorteó el juego. Uno de los cinco
// lleva la cámara; fuera de ellos, la construcción no la mueve y el dibujo no le pone la Z de la selección.
TEST_F(SelectionStateTest, TheCameraFollowsAHeroPublishedByTheGame)
{
    m_world.Runtime().hero = 4;
    m_input.hero = {7.0f, 8.0f, 9.0f};
    ASSERT_TRUE(Apply({List({Entry(0), Entry(4)})}).HasValue());
    EXPECT_EQ(m_input.hero, m_state.Slots()[4].position);
    m_state.DrawSelection(m_input);
    EXPECT_EQ(m_input.hero.z, 163.0f);

    m_world.Runtime().hero = 237;
    m_input.hero = {7.0f, 8.0f, 9.0f};
    ASSERT_TRUE(Apply({List({Entry(0), Entry(4)})}).HasValue());
    EXPECT_EQ(m_input.hero, (glm::vec3{7.0f, 8.0f, 9.0f}));
    m_state.DrawSelection(m_input);
    EXPECT_EQ(m_input.hero.z, 9.0f);
    EXPECT_EQ(m_state.Slots()[0].position.z, 163.0f);
}

TEST_F(SelectionStateTest, ReconstructingAfterDrawingReplacesTheRenderedHeight)
{
    ASSERT_TRUE(Apply({List({Entry(0, true)})}).HasValue());
    m_state.DrawSelection(m_input);
    ASSERT_TRUE(Apply({CharacterCreated{Entry()}}).HasValue());
    EXPECT_EQ(m_input.hero.z, 130.0f);
    EXPECT_EQ(m_state.Slots()[0].position.z, 130.0f);
    ASSERT_TRUE(Apply({}).HasValue());
    EXPECT_EQ(m_input.hero.z, 130.0f);
}

TEST_F(SelectionStateTest, DeletionAndReentryPreserveTheSlotHistoryAndCoordinates)
{
    ASSERT_TRUE(Apply({List({Entry(1, true)}), CharacterDeleted{1}}).HasValue());
    m_state.BeginSelection();
    EXPECT_FALSE(m_state.HasReceivedList());
    EXPECT_EQ(m_state.Slots()[1].position.z, 100.0f);
    EXPECT_EQ(m_state.Slots()[1].revision, 1u);
    EXPECT_TRUE(m_state.Slots()[1].previousHad1c15);
    ASSERT_TRUE(Apply({List({Entry(1)})}).HasValue());
    EXPECT_EQ(m_state.Slots()[1].position.z, 130.0f);
}

TEST_F(SelectionStateTest, EnteringLoginClearsOnlyTheSecondSceneActorPet)
{
    ASSERT_TRUE(Apply({List({Entry(0, true), Entry(1, true), Entry(2, true)})}).HasValue());
    m_state.EnterLogin();
    EXPECT_TRUE(m_state.Slots()[0].previousHad1c15);
    EXPECT_TRUE(m_state.Slots()[1].previousHad1c15);
    EXPECT_FALSE(m_state.Slots()[2].previousHad1c15);
    EXPECT_EQ(m_state.Slots()[0].position.z, 100.0f);
    EXPECT_EQ(m_input.hero.z, 100.0f);
    EXPECT_EQ(m_state.Slots()[2].revision, 1u);
}

// 0x4c20a0 construye en chars[lugar]: los cinco lugares son los registros 0 a 4 del pool durable, sin
// otra copia, y los demás registros no se tocan.
TEST_F(SelectionStateTest, TheFiveSlotsAreTheFirstRecordsOfTheDurablePool)
{
    ASSERT_TRUE(Apply({List({Entry(0, true), Entry(4)})}).HasValue());
    for (std::size_t slot = 0; slot < Client::Scenes::SelectionActorCount; ++slot)
    {
        EXPECT_EQ(&m_state.Slots()[slot], &m_world.Actors()[slot]);
    }
    EXPECT_TRUE(m_world.Actors()[0].previousHad1c15);
    EXPECT_EQ(m_world.Actors()[0].revision, 1u);
    EXPECT_EQ(m_world.Actors()[4].revision, 1u);
    EXPECT_EQ(m_world.Actors()[4].position.x, 13349.6f);
    for (const auto& actor : m_world.Actors().subspan<Client::Scenes::SelectionActorCount>())
    {
        EXPECT_EQ(actor.revision, 0u);
        EXPECT_EQ(actor.position.x, 0.0f);
        EXPECT_EQ(actor.position.z, 0.0f);
    }
    // La creación sin equipo (Q-C103) deja en el registro el word318 que leerá el próximo constructor.
    ASSERT_TRUE(Apply({CharacterCreated{Entry()}}).HasValue());
    EXPECT_FALSE(m_world.Actors()[0].previousHad1c15);
    EXPECT_EQ(m_world.Actors()[0].revision, 2u);
}

// Quien llama a 0x61a560 escribe antes el mundo en curso (DAT_006c9dd0, 05 "La carpeta de cada
// mapa"): las tablas lo dejan en el WorldRuntime del pool, no en una copia de la selección.
TEST_F(SelectionStateTest, TheTablesWriteTheLiveMapOfTheWorldState)
{
    EXPECT_EQ(m_world.Runtime().map, 55);
    m_state.BorrowTerrain(8, nullptr);
    EXPECT_EQ(m_world.Runtime().map, 8);
    EXPECT_EQ(m_state.Map(), 8);
    m_state.RetainTerrain(54, TerrainOf(100.0f));
    EXPECT_EQ(m_world.Runtime().map, 54);
    EXPECT_EQ(m_state.Map(), 54);
}

TEST_F(SelectionStateTest, MissingTablesFailExplicitlyEvenWhenTheSceneSkipsHeight)
{
    WorldState world;
    SelectionState state(world);
    state.SetScene(CullingScene::Other);
    const std::array<CharacterEvent, 1> events{List({Entry()})};
    const auto applied = state.Apply(events, m_camera, m_input);
    ASSERT_FALSE(applied.HasValue());
    EXPECT_FALSE(applied.Error().empty());
    EXPECT_EQ(state.Slots()[0].revision, 0u);
    EXPECT_TRUE(state.Failure().has_value());
    state.BeginSelection();
    EXPECT_TRUE(state.Failure().has_value());
}

TEST_F(SelectionStateTest, MissingTablesKeepTheNativeConstructorRandomsAndPartialRecord)
{
    constexpr std::uint32_t Seed = 0x12345678;
    Client::Render::Models::ClothWorld cloth{.random = Client::Render::Models::CrtRandom(Seed)};
    m_state.BorrowTerrain(55, nullptr);
    const std::array<CharacterEvent, 1> events{List({Entry(3)})};

    const auto applied = m_state.Apply(events, m_camera, m_input, &cloth);

    ASSERT_FALSE(applied.HasValue());
    EXPECT_NE(applied.Error().find("0x511b9f"), std::string::npos);
    EXPECT_EQ(cloth.random.State(), Client::Tests::AfterDraws(Seed, 16).State());
    EXPECT_TRUE(m_world.Actors()[3].alive);
    EXPECT_EQ(m_world.Actors()[3].type, 0x20au);
    EXPECT_EQ(m_world.Actors()[3].revision, 0u);
}

TEST_F(SelectionStateTest, BorrowingTheOwnedTerrainDoesNotDestroyItsTables)
{
    const auto* terrain = m_state.Terrain();
    ASSERT_NE(terrain, nullptr);
    m_state.BorrowTerrain(8, terrain);
    EXPECT_EQ(m_state.Terrain(), terrain);
    ASSERT_TRUE(Apply({List({Entry()})}).HasValue());
    EXPECT_EQ(m_input.hero.z, 100.0f);
}

TEST_F(SelectionStateTest, BorrowedTablesStayLiveUntilOwnershipIsTransferred)
{
    auto terrain = TerrainOf(200.0f);
    m_state.BorrowTerrain(55, &terrain);
    terrain.heights->cells.fill(300.0f);
    ASSERT_TRUE(Apply({List({Entry()})}).HasValue());
    EXPECT_EQ(m_input.hero.z, 300.0f);
    m_state.RetainTerrain(55, std::move(terrain));
    ASSERT_TRUE(Apply({CharacterCreated{Entry()}}).HasValue());
    EXPECT_EQ(m_input.hero.z, 300.0f);
}

TEST_F(SelectionStateTest, AnEmptyListRefreshesTheLiveLogicalZeroAttributeWithoutMovingHero)
{
    m_input.cellAttribute = 7;
    m_input.hero = {7.0f, 8.0f, 9.0f};
    m_state.RetainTerrain(55, TerrainOf(200.0f, 0x140));
    ASSERT_TRUE(Apply({List({})}).HasValue());
    EXPECT_EQ(m_input.cellAttribute, 0x40u);
    EXPECT_EQ(m_input.hero.z, 9.0f);
}

TEST_F(SelectionStateTest, RefreshReadsBorrowedAttributeChangesAndPreservesInputWhenTablesAreMissing)
{
    auto terrain = TerrainOf(200.0f, 0x140);
    m_state.BorrowTerrain(55, &terrain);
    m_state.RefreshCameraAttribute(m_input);
    EXPECT_EQ(m_input.cellAttribute, 0x40u);
    terrain.attributes->cells[0] = 0x104;
    m_state.RefreshCameraAttribute(m_input);
    EXPECT_EQ(m_input.cellAttribute, 4u);
    m_state.BorrowTerrain(55, nullptr);
    m_state.RefreshCameraAttribute(m_input);
    EXPECT_EQ(m_input.cellAttribute, 4u);
}

TEST_F(SelectionStateTest, InitializationWaitsForAListAndReturnsLoadFailures)
{
    const Client::Tests::TemporaryFolder empty("selection-state");
    ASSERT_TRUE(m_state.InitializeSelection(empty.Path(), m_input).HasValue());
    EXPECT_FALSE(m_state.IsInitialized());
    EXPECT_EQ(m_state.Map(), 55);
    ASSERT_TRUE(Apply({List({})}).HasValue());
    const auto initialized = m_state.InitializeSelection(empty.Path(), m_input);
    ASSERT_FALSE(initialized.HasValue());
    EXPECT_FALSE(initialized.Error().empty());
    EXPECT_FALSE(m_state.IsInitialized());
    EXPECT_EQ(m_state.Map(), 55);
    ASSERT_TRUE(m_state.Failure().has_value());
    const std::array<CharacterEvent, 1> missing{CharacterCreated{Entry()}};
    m_state.BorrowTerrain(55, nullptr);
    EXPECT_FALSE(m_state.Apply(missing, m_camera, m_input).HasValue());
    EXPECT_EQ(*m_state.Failure(), initialized.Error());
}

// 0x633f30: el mundo cargado lo marca la carga del 54, antes de cargarlo (0x633f4e), y no la entrada a
// la escena 4 (BeginSelection, 0x6342cb). Sin la lista no se carga ni se marca.
TEST_F(SelectionStateTest, TheWorld54LoadMarksTheLoadedWorld)
{
    EXPECT_FALSE(m_world.Runtime().drawingEnabled);
    const Client::Tests::TemporaryFolder empty("selection-state-drawing");
    ASSERT_TRUE(m_state.InitializeSelection(empty.Path(), m_input).HasValue());
    EXPECT_FALSE(m_world.Runtime().drawingEnabled);
    ASSERT_TRUE(Apply({List({})}).HasValue());

    // La carpeta vacía hace fallar la carga, que va después de la marca.
    EXPECT_FALSE(m_state.InitializeSelection(empty.Path(), m_input).HasValue());

    EXPECT_TRUE(m_world.Runtime().drawingEnabled);
}

TEST_F(SelectionStateTest, TheWorld54LoadClearsLiveEffectsBeforeAFileFailure)
{
    auto effects = m_world.Map().effects.Effects();
    effects[0].alive = true;
    effects[0].seen = true;
    const Client::Tests::TemporaryFolder empty("selection-effects-history");
    ASSERT_TRUE(m_state.InitializeSelection(empty.Path(), m_input).HasValue());
    EXPECT_TRUE(effects[0].alive);
    ASSERT_TRUE(Apply({List({})}).HasValue());
    EXPECT_FALSE(m_state.InitializeSelection(empty.Path(), m_input).HasValue());
    EXPECT_FALSE(effects[0].alive);
    EXPECT_TRUE(effects[0].seen);
}

TEST_F(SelectionStateTest, TheWorld54LoadClearsParticlesBeforeAFileFailureAndRetainsWind)
{
    auto& particles = m_world.Particles();
    particles.All()[0].alive = true;
    particles.Wind().wind = {3.0f, 4.0f};
    const Client::Tests::TemporaryFolder empty("selection-particle-history");
    ASSERT_TRUE(m_state.InitializeSelection(empty.Path(), m_input).HasValue());
    EXPECT_TRUE(particles.Any());
    ASSERT_TRUE(Apply({List({})}).HasValue());
    EXPECT_FALSE(m_state.InitializeSelection(empty.Path(), m_input).HasValue());
    EXPECT_FALSE(particles.Any());
    EXPECT_EQ(particles.Wind().wind, glm::vec2(3.0f, 4.0f));
}

class GoldenSelectionStateTest : public Client::Tests::GoldenDataTest
{
};

TEST_F(GoldenSelectionStateTest, TheFirstListUsesLoginTablesAndOnlyTheNextConstructionUsesWorld54)
{
    WorldState world;
    SelectionState state(world);
    Client::World::GameCamera camera;
    Client::World::GameCameraInput input;
    state.BeginSelection();
    state.RetainTerrain(55, TerrainOf(987.0f));
    const std::array<CharacterEvent, 1> events{List({Entry()})};
    ASSERT_TRUE(state.Apply(events, camera, input).HasValue());
    EXPECT_EQ(input.hero.z, 987.0f);
    const auto initialized = state.InitializeSelection(m_dataRoot, input);
    ASSERT_TRUE(initialized.HasValue()) << initialized.Error();
    ASSERT_TRUE(state.IsInitialized());
    EXPECT_EQ(state.Map(), 54);
    // 0x633f20 deja el 54 como mundo en curso del WorldState (06, "Al entrar").
    EXPECT_EQ(world.Runtime().map, 54);
    EXPECT_EQ(input.map, 54u);
    EXPECT_EQ(input.hero.z, 987.0f);
    EXPECT_EQ(state.Slots()[0].revision, 1u);
    ASSERT_TRUE(state.InitializeSelection(m_dataRoot / "missing", input).HasValue());
    ASSERT_TRUE(state.Apply(events, camera, input).HasValue());
    EXPECT_NE(input.hero.z, 987.0f);
    EXPECT_EQ(state.Slots()[0].revision, 2u);
}

TEST_F(GoldenSelectionStateTest, AnEmptyListLoads54WithoutInventingHeroConstruction)
{
    WorldState world;
    SelectionState state(world);
    Client::World::GameCamera camera;
    Client::World::GameCameraInput input;
    input.hero = {7.0f, 8.0f, 9.0f};
    state.BeginSelection();
    state.RetainTerrain(55, TerrainOf(900.0f, 0x40));
    const std::array<CharacterEvent, 1> events{List({})};
    ASSERT_TRUE(state.Apply(events, camera, input).HasValue());
    EXPECT_EQ(input.cellAttribute, 0x40u);
    const auto initialized = state.InitializeSelection(m_dataRoot, input);
    ASSERT_TRUE(initialized.HasValue()) << initialized.Error();
    EXPECT_TRUE(state.IsInitialized());
    EXPECT_EQ(input.hero.z, 9.0f);
    EXPECT_EQ(state.Slots()[0].revision, 0u);
    EXPECT_EQ(state.Slots()[0].position.z, 0.0f);
    EXPECT_NE(input.cellAttribute, 0x40u);
    // El atributo 0 del World55 del repo es 0x0c, distinto del 0x40 del login sintético.
    EXPECT_EQ(input.cellAttribute, 0x0cu);
}
} // namespace
