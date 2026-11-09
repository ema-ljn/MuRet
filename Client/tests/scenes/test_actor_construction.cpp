#include "Scenes/ActorConstruction.h"

#include "Data/Models/PlayerModels.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Render/Models/Cloth.h"
#include "Scenes/ActorState.h"
#include "World/TerrainCulling.h"
#include "World/TerrainHeight.h"
#include "World/WorldTerrain.h"

#include "support/GoldenDir.h"
#include "support/PlayerActors.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>

namespace
{
using Client::Render::Models::ClothWorld;
using Client::Render::Models::CrtRandom;
using Client::Scenes::ActorConstructionContext;
using Client::Scenes::ActorPlacement;
using Client::Scenes::ActorState;
using Client::Scenes::ConstructActor;
using Client::Scenes::NoItemModel;

constexpr std::uint32_t Seed = 0xc0ffee;
constexpr std::uint16_t Player = Client::Data::Models::PlayerSkeletonModel;
constexpr std::uint16_t SafeBit = 1;
constexpr float FixedHeight = 1200.0f;

class ActorConstructionTest : public Client::Tests::GoldenDataTest
{
protected:
    void SetUp() override
    {
        GoldenDataTest::SetUp();
        auto loaded = Client::World::LoadWorldTerrain(m_dataRoot, 0);
        ASSERT_TRUE(loaded.HasValue()) << loaded.Error();
        terrain = std::move(loaded).Value();
    }

    [[nodiscard]] ActorConstructionContext Context(std::int32_t map = 0) const
    {
        return ActorConstructionContext{
            .terrain = terrain, .map = map, .scene = Client::World::CullingScene::Game, .fixedHeight = FixedHeight};
    }

    // La primera celda de Lorencia con el bit de la zona segura como `safe`.
    [[nodiscard]] std::pair<std::uint8_t, std::uint8_t> FirstCell(bool safe) const
    {
        for (std::size_t cell = 0; cell < Client::Data::Terrain::TerrainCellCount; ++cell)
        {
            if (((terrain.attributes->cells[cell] & SafeBit) != 0) == safe)
            {
                return {static_cast<std::uint8_t>(cell % Client::Data::Terrain::TerrainSide),
                        static_cast<std::uint8_t>(cell / Client::Data::Terrain::TerrainSide)};
            }
        }
        ADD_FAILURE() << "Lorencia no tiene esa celda";
        return {};
    }

    [[nodiscard]] double HeightAt(const glm::vec3& position) const
    {
        return Client::World::TerrainHeightAt(*terrain.heights, *terrain.attributes, position.x, position.y,
                                              FixedHeight);
    }

    Client::World::WorldTerrain terrain;
    ClothWorld cloth{.random = CrtRandom(Seed)};
};

// La celda del cable (133, 127): el centro en (13.350, 12.750), el ángulo en +0x24 y la Z del terreno.
TEST_F(ActorConstructionTest, PutsThePlayerInTheCentreOfItsCell)
{
    ActorState actor;

    ASSERT_TRUE(ConstructActor(actor, Player, ActorPlacement{133, 127, -45.0f}, Context(), cloth).HasValue());

    EXPECT_TRUE(actor.alive);
    EXPECT_EQ(actor.type, Player);
    EXPECT_EQ(actor.position.x, 13350.0f);
    EXPECT_EQ(actor.position.y, 12750.0f);
    EXPECT_EQ(actor.position.z, static_cast<float>(HeightAt(actor.position)));
    EXPECT_EQ(actor.angles, glm::vec3(0.0f, 0.0f, -45.0f));
    EXPECT_EQ(actor.cellX, 133);
    EXPECT_EQ(actor.cellY, 127);
    const std::size_t cell = Client::Data::Terrain::CellIndex(133, 127);
    EXPECT_EQ(actor.safeZone, terrain.attributes->cells[cell] & SafeBit);
    EXPECT_EQ(actor.revision, 1u);
}

// Siete accesos de la tabla de protección, cada uno con sus 4 rand() al cerrar desde cero; el del
// contador de pasos no gasta si la tela lo tiene abierto.
TEST_F(ActorConstructionTest, SpendsTwentyEightRandomsFromZero)
{
    ActorState actor;
    ASSERT_TRUE(ConstructActor(actor, Player, ActorPlacement{133, 127, 0.0f}, Context(), cloth).HasValue());
    EXPECT_EQ(cloth.random.State(), Client::Tests::AfterDraws(Seed, 28).State());
    EXPECT_EQ(actor.cellXAccesses, 0);
    EXPECT_EQ(actor.cellYAccesses, 0);

    ClothWorld counting{.random = CrtRandom(Seed), .logicSteps = 77, .openCounterAccesses = 1};
    ActorState other;
    ASSERT_TRUE(ConstructActor(other, Player, ActorPlacement{133, 127, 0.0f}, Context(), counting).HasValue());
    EXPECT_EQ(counting.random.State(), Client::Tests::AfterDraws(Seed, 24).State());
    EXPECT_EQ(other.stepStamp, 77);
}

// El word318 de antes en 0x1c15 suma 30 (90 en los mapas 8 y 10) fuera de la zona segura y con un
// mundo; después la mascota queda sin ítem.
TEST_F(ActorConstructionTest, TheOld1c15AddsThePetOffsetOutsideTheSafeZone)
{
    const auto [x, y] = FirstCell(false);
    for (const auto& [map, offset] : {std::pair{0, 30.0}, std::pair{8, 90.0}, std::pair{-1, 0.0}})
    {
        SCOPED_TRACE(map);
        ActorState actor;
        actor.previousHad1c15 = true;
        ASSERT_TRUE(ConstructActor(actor, Player, ActorPlacement{x, y, 0.0f}, Context(map), cloth).HasValue());
        EXPECT_EQ(actor.position.z, static_cast<float>(HeightAt(actor.position) + offset));
        EXPECT_FALSE(actor.previousHad1c15);
        EXPECT_EQ(actor.safeZone, 0);
    }

    const auto [safeX, safeY] = FirstCell(true);
    ActorState safe;
    safe.previousHad1c15 = true;
    ASSERT_TRUE(ConstructActor(safe, Player, ActorPlacement{safeX, safeY, 0.0f}, Context(), cloth).HasValue());
    EXPECT_EQ(safe.position.z, static_cast<float>(HeightAt(safe.position)));
    EXPECT_EQ(safe.safeZone, 1);
}

// El estado de partida que lee 0x512a30 después: el equipo sin ítem, las acciones en 1 y +0x3e3 en 0.
TEST_F(ActorConstructionTest, LeavesThePlayerReadyToDress)
{
    ActorState actor = Client::Tests::UnarmedPlayer(3);
    actor.hands[0] = {.model = 0x300, .level = 7, .flags1b = 1, .flags1c = 2};
    actor.byte3e3 = 1;
    actor.animation.action = 40;
    actor.alpha = 0.0f;

    ASSERT_TRUE(ConstructActor(actor, Player, ActorPlacement{133, 127, 0.0f}, Context(), cloth).HasValue());

    EXPECT_EQ(actor.hands[0].model, NoItemModel);
    EXPECT_EQ(actor.hands[0].level, 0);
    EXPECT_EQ(actor.hands[0].flags1b, 0);
    EXPECT_EQ(actor.hands[0].flags1c, 2);
    EXPECT_EQ(actor.head, NoItemModel);
    for (const auto& part : actor.body)
    {
        EXPECT_EQ(part.model, NoItemModel);
    }
    EXPECT_EQ(actor.wings.model, NoItemModel);
    EXPECT_EQ(actor.pet.model, NoItemModel);
    EXPECT_EQ(actor.byte3e3, 0);
    EXPECT_EQ(actor.characterClass, 0);
    EXPECT_EQ(actor.animation.action, 1);
    EXPECT_EQ(actor.animation.previousAction, 1);
    EXPECT_EQ(actor.scale, 0.9f);
    EXPECT_EQ(actor.alpha, 1.0f);
    EXPECT_EQ(actor.alphaTarget, 1.0f);
    EXPECT_EQ(actor.pk, 3);
    EXPECT_EQ(actor.byte88, 1);
    EXPECT_EQ(actor.word376, 0xffff);
}

// Los casos de los otros tipos no están leídos: corta sin tocar al personaje ni gastar rand().
TEST_F(ActorConstructionTest, OnlyThePlayerTypeIsConstructed)
{
    ActorState actor;

    const auto built = ConstructActor(actor, 0xa9, ActorPlacement{133, 127, 0.0f}, Context(), cloth);

    ASSERT_FALSE(built.HasValue());
    EXPECT_NE(built.Error().find("0x511290"), std::string::npos);
    EXPECT_FALSE(actor.alive);
    EXPECT_EQ(actor.revision, 0u);
    EXPECT_EQ(cloth.random.State(), Seed);
}
} // namespace
