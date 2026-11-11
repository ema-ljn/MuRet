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
#include <string>
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

TEST_F(ActorConstructionTest, NewFacesKeepTheirClassAndHeadButStartAtActionZero)
{
    ActorState actor;
    actor.characterClass = 4;
    actor.headTurn.angles = {12.0f, 34.0f};
    actor.headTurn.target = {56.0f, 78.0f};

    const auto built = ConstructActor(actor, 0xa9, {}, Context(54), cloth);

    ASSERT_TRUE(built.HasValue()) << built.Error();
    EXPECT_TRUE(actor.alive);
    EXPECT_EQ(actor.type, 0xa9u);
    EXPECT_EQ(actor.characterClass, 4u);
    EXPECT_EQ(actor.animation.action, 0u);
    EXPECT_EQ(actor.animation.previousAction, 0u);
    EXPECT_EQ(actor.position.x, 50.0f);
    EXPECT_EQ(actor.position.y, 50.0f);
    EXPECT_EQ(actor.headTurn.angles, (glm::vec2{12.0f, 34.0f}));
    EXPECT_EQ(actor.headTurn.target, (glm::vec2{56.0f, 78.0f}));
    EXPECT_EQ(cloth.random.State(), Client::Tests::AfterDraws(Seed, 28).State());
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

// El estado de partida que lee 0x512a30 después: el equipo sin ítem, las acciones en 1, +0x3e3 en 0 y
// +0x358 en 0 (0x511c99).
TEST_F(ActorConstructionTest, LeavesThePlayerReadyToDress)
{
    ActorState actor = Client::Tests::UnarmedPlayer(3);
    actor.hands[0].model = 0x300;
    actor.hands[0].level = 7;
    actor.hands[0].flags1b = 1;
    actor.hands[0].flags1c = 2;
    actor.wings.animation.frame = 3.5f;
    actor.byte3e3 = 1;
    actor.byte358 = 1;
    actor.animation.action = 40;
    actor.alpha = 0.0f;

    ASSERT_TRUE(ConstructActor(actor, Player, ActorPlacement{133, 127, 0.0f}, Context(), cloth).HasValue());

    EXPECT_EQ(actor.hands[0].model, NoItemModel);
    EXPECT_EQ(actor.hands[0].level, 0);
    EXPECT_EQ(actor.hands[0].flags1b, 0);
    EXPECT_EQ(actor.hands[0].flags1c, 2);
    // 0x5126b2..0x5126d5: las manos de un 0x20a cuelgan de los huesos 33 y 42; lo que cuelga sigue con la
    // animación que tenía.
    EXPECT_EQ(actor.hands[0].bone, 0x21);
    EXPECT_EQ(actor.hands[1].bone, 0x2a);
    EXPECT_EQ(actor.wings.animation.frame, 3.5f);
    EXPECT_EQ(actor.head, NoItemModel);
    for (const auto& part : actor.body)
    {
        EXPECT_EQ(part.model, NoItemModel);
    }
    EXPECT_EQ(actor.wings.model, NoItemModel);
    EXPECT_EQ(actor.pet.model, NoItemModel);
    EXPECT_EQ(actor.byte3e3, 0);
    EXPECT_EQ(actor.byte358, 0);
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

// Lo que lee el paso de los personajes (0x508f00): sin verse (+0x16c, 0x511bd8) y sin morirse (+0x389,
// 0x511c05), aunque el lugar los tuviera de antes. Y lo que lee 0x4e9980 desde su cuerpo (0x4fd5f0): el alpha de a
// 0,05 (+0x16d, 0x511bdf) y la luz del blendMesh en 1,0 (+0x68, 0x512499).
TEST_F(ActorConstructionTest, LeavesThePlayerUnseenAndNotDying)
{
    ActorState actor = Client::Tests::UnarmedPlayer(3);
    actor.seen = true;
    actor.dying = 1;
    actor.alphaMode = 0;
    actor.blendMeshLight = 0.25f;

    ASSERT_TRUE(ConstructActor(actor, Player, ActorPlacement{133, 127, 0.0f}, Context(), cloth).HasValue());

    EXPECT_FALSE(actor.seen);
    EXPECT_EQ(actor.dying, 0);
    EXPECT_EQ(actor.alphaMode, 1);
    EXPECT_EQ(actor.blendMeshLight, 1.0f);
}

// Fuera del jugador y los NewFace, corta sin tocar el personaje ni gastar rand().
TEST_F(ActorConstructionTest, UnknownTypesFailBeforeChangingTheRecord)
{
    ActorState actor;

    const auto built = ConstructActor(actor, 0xad, ActorPlacement{133, 127, 0.0f}, Context(), cloth);

    ASSERT_FALSE(built.HasValue());
    EXPECT_NE(built.Error().find("0x511290"), std::string::npos);
    EXPECT_FALSE(actor.alive);
    EXPECT_EQ(actor.revision, 0u);
    EXPECT_EQ(cloth.random.State(), Seed);
}
} // namespace
