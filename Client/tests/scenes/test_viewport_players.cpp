#include "Scenes/ViewportPlayers.h"

#include "Core/Expected.h"
#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Core/PixelSize.h"
#include "Crt/CrtRandom.h"
#include "Tables/PlayerModels.h"
#include "Tables/ItemSetTables.h"
#include "Tables/ItemTable.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Data/Terrain/TerrainGrid.h"
#include "GameLogic/CharacterAppearance.h"
#include "Protocol/Movement/Movement.h"
#include "Protocol/Actors/Viewport.h"
#include "Models/Cloth.h"
#include "Models/LogicalAccess.h"
#include "Render/SpriteBatch.h"
#include "Scenes/ActionReply.h"
#include "Scenes/ActorConstruction.h"
#include "Scenes/ActorCreation.h"
#include "Scenes/ActorState.h"
#include "Scenes/ChatReceive.h"
#include "Scenes/DarkSpirit.h"
#include "Scenes/FaunaJointCreation.h"
#include "Scenes/GameDrawTail.h"
#include "Scenes/HelperTable.h"
#include "Scenes/FaunaModels.h"
#include "Scenes/LoginModels.h"
#include "Scenes/MovementReplies.h"
#include "Scenes/SelectionState.h"
#include "Scenes/ViewportPacketRing.h"
#include "Session/WorldEntry.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "Session/WorldTransitionSupport.h"
#include "World/GameCamera.h"
#include "World/ModelEffects.h"
#include "World/ParticleOwner.h"
#include "World/Particles.h"
#include "World/TerrainCulling.h"
#include "World/WorldTerrain.h"
#include "UI/ChatLog.h"
#include "UI/MessageArea.h"

#include "support/FakeTextureStore.h"
#include "support/GoldenDir.h"
#include "support/ItemSetFiles.h"
#include "support/PlayerActors.h"
#include "support/RecordingSink.h"
#include "support/RecordingTextDrawer.h"
#include "support/SceneDataFiles.h"
#include "support/ViewportPackets.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

// Los paquetes del viewport sobre el mundo (CL9 hito 1): el 0x12, el 0x14, el 0x45, el 0x25 y el 0xDF, armados con los
// structs del ASM (support/ViewportPackets.h), en Lorencia (0) y Devias (2). Los nombres son inventados.
namespace
{
using Client::Crt::CrtRandom;
using Client::Protocol::Actors::Players;
using Client::Scenes::ActorState;
using Client::Tables::PlayerSkeletonModel;
using Client::Tests::AfterDraws;
using Client::Tests::ViewportPlayerRecord;

constexpr std::uint32_t Seed = 0x5eed;
constexpr std::uint16_t HeroKey = 33;
// 0x511290 desde cero: las dos escrituras de la celda, sus cuatro lecturas y la del contador, 4 rand() cada una.
constexpr int ConstructionDraws = 28;
// Dos accesos a la celda protegida (+0x41c y +0x420).
constexpr int CellDraws = 8;
constexpr int FreshRecordDraws = ConstructionDraws + CellDraws + CellDraws;

// El mundo del juego, en el heap: WorldState pesa unos 350 KB.
struct Game
{
    Game()
    {
        // 0x612d90 y 0x612270 suben las texturas de los modelos que cargan por la tabla que liga el juego.
        world.FaunaResources().TextureTable().Bind(store);
        models.Set(PlayerSkeletonModel, {}, Client::Tests::PlayerActionsSkeleton(), {});
        ActorState& hero = world.Actors()[0];
        hero = Client::Tests::UnarmedPlayer();
        hero.alive = true;
        hero.key = HeroKey;
        world.Runtime().hero = 0;
    }
    Client::Scenes::WorldEntryResources Resources()
    {
        return {.world = world,
                .selection = selection,
                .dataRoot = dataRoot,
                .cloth = cloth,
                .camera = camera,
                .cameraInput = input,
                .models = models,
                .items = &items,
                .itemSetTables = &sets,
                .log = log};
    }
    // Antes que el mundo: FaunaModels suelta sus texturas al destruirse.
    Client::Tests::FakeTextureStore store;
    Client::Scenes::WorldState world;
    // El Data\ del laboratorio, para los Monster*.bmd que mira el alta de 0x5141e0; vacío fuera de los tests con datos.
    std::filesystem::path dataRoot;
    Client::Scenes::SelectionState selection{world};
    Client::World::GameCamera camera;
    Client::World::GameCameraInput input;
    Client::Models::ClothWorld cloth{.random = CrtRandom(Seed)};
    Client::Scenes::SingleModelLibrary models;
    // Item.bmd sin nada marcado, para 0x54efe0 desde 0x512a30.
    Client::Tables::ItemTable items = Client::Tests::ItemsMarking({});
    // Sin ningún conjunto: 0x40e1b0 (desde 0x512a30) recorre el equipo y no aplica nada.
    Client::Tables::ItemSetTables sets =
        Client::Tests::ItemSetTablesFrom(std::vector<std::byte>(Client::Tables::ItemSetTypeTableSize),
                                         std::vector<std::byte>(Client::Tables::ItemSetOptionTableSize));
    Client::Tests::RecordingSink sink;
    Client::Core::Log log{sink, Client::Core::LogLevel::Debug};
};

struct Cell
{
    std::uint8_t x = 0;
    std::uint8_t y = 0;
};

Players Read(std::span<const ViewportPlayerRecord> records)
{
    auto read = Client::Protocol::Actors::ReadPlayers(Client::Tests::ViewportPlayersPacket(records));
    EXPECT_TRUE(read.HasValue());
    return std::move(read).Value();
}

std::uint8_t Byte(int value)
{
    return static_cast<std::uint8_t>(value);
}

class ViewportPlayersTest : public Client::Tests::GoldenDataTest
{
protected:
    void Load(std::uint8_t map)
    {
        game = std::make_unique<Game>();
        auto loaded = Client::World::LoadWorldTerrain(m_dataRoot, map);
        ASSERT_TRUE(loaded.HasValue()) << loaded.Error();
        game->world.Map().terrain = std::move(loaded).Value();
        game->world.Runtime().map = map;
        game->dataRoot = m_dataRoot;
    }

    // La primera celda, desde (100, 100), con las cuatro de su derecha libres: sin paredes ni zona segura.
    [[nodiscard]] Cell FreeRow() const
    {
        const auto& cells = game->world.Map().terrain.attributes->cells;
        for (int y = 100; y < 200; ++y)
        {
            for (int x = 100; x < 200; ++x)
            {
                bool free = true;
                for (int step = 0; step < 5 && free; ++step)
                    free = cells[Client::Data::Terrain::CellIndex(Byte(x + step), Byte(y))] == 0;
                if (free)
                    return {Byte(x), Byte(y)};
            }
        }
        ADD_FAILURE() << "el mapa no tiene una fila libre";
        return {};
    }

    [[nodiscard]] ActorState& Actor(std::size_t slot)
    {
        return game->world.Actors()[slot];
    }

    std::unique_ptr<Game> game;
};

// 0x4c8f4b..0x4c905c: recién llegado, al centro de su celda, con el efecto 0x61f y el alpha en 0; el blanco del
// registro no cuenta. La cola guarda el paquete entero antes de los registros.
TEST_F(ViewportPlayersTest, ANewPlayerAppearsInTheCentreOfItsCell)
{
    for (const std::uint8_t map : {std::uint8_t{0}, std::uint8_t{2}})
    {
        SCOPED_TRACE(map);
        Load(map);
        const Cell cell = FreeRow();
        const std::array<ViewportPlayerRecord, 1> records{{{.key = 0x0456,
                                                            .isNew = true,
                                                            .x = cell.x,
                                                            .y = cell.y,
                                                            .classAndState = 0x20,
                                                            .name = "Hornero",
                                                            .targetX = Byte(cell.x + 3),
                                                            .targetY = cell.y,
                                                            .directionAndPk = 0x53}}};
        const Players players = Read(records);
        const auto applied = Client::Scenes::ApplyViewportPlayers(players, game->Resources());
        ASSERT_TRUE(applied.HasValue()) << applied.Error();

        const ActorState& actor = Actor(1);
        EXPECT_TRUE(actor.alive);
        EXPECT_EQ(actor.key, 0x0456);
        EXPECT_EQ(actor.type, PlayerSkeletonModel);
        // ((0x20 & 0x10) | (0x20 >> 4)) >> 1.
        EXPECT_EQ(actor.characterClass, 1);
        EXPECT_EQ(actor.byte88, 1);
        EXPECT_EQ(actor.pk, 3);
        EXPECT_EQ(actor.cellX, cell.x);
        EXPECT_EQ(actor.cellY, cell.y);
        EXPECT_EQ(actor.targetX, cell.x);
        EXPECT_EQ(actor.targetY, cell.y);
        EXPECT_EQ(actor.position.x, (static_cast<float>(cell.x) + 0.5f) * 100.0f);
        EXPECT_EQ(actor.position.y, (static_cast<float>(cell.y) + 0.5f) * 100.0f);
        // (5 − 1) · 45.
        EXPECT_EQ(actor.angles.z, 180.0f);
        EXPECT_EQ(actor.alpha, 0.0f);
        EXPECT_EQ(actor.movement, 0);
        EXPECT_EQ(std::to_integer<char>(actor.name[0]), 'H');
        EXPECT_EQ(std::to_integer<char>(actor.name[7]), '\0');
        EXPECT_EQ(actor.byte1e3, 0);
        const auto& effect = game->world.Map().effects.Effects()[0];
        EXPECT_TRUE(effect.alive);
        EXPECT_EQ(effect.type, Client::World::ArrivalEffect);
        EXPECT_TRUE(effect.hasOwner);
        EXPECT_EQ(effect.ownerContext, &actor);
        EXPECT_EQ(effect.disturbanceState, 0xffffu);
        EXPECT_EQ(game->cloth.random.State(), AfterDraws(Seed, FreshRecordDraws).State());
        // La copia del dispatcher (0x4e372e) la hace la sesión antes del lector; 0x4c8860 copia solo con el estado 1.
        EXPECT_EQ(game->world.Runtime().viewportPackets.count, 0);
    }
}

// 0x4c907c..0x4c91a3: el que ya estaba camina hasta +0x26/+0x27 desde su celda, leída la Y antes que la X.
TEST_F(ViewportPlayersTest, AnOldPlayerWalksTowardsItsTarget)
{
    for (const std::uint8_t map : {std::uint8_t{0}, std::uint8_t{2}})
    {
        SCOPED_TRACE(map);
        Load(map);
        const Cell cell = FreeRow();
        const std::array<ViewportPlayerRecord, 1> records{{{.key = 0x0457,
                                                            .x = cell.x,
                                                            .y = cell.y,
                                                            .classAndState = 0x00,
                                                            .name = "Carancho",
                                                            .targetX = Byte(cell.x + 3),
                                                            .targetY = cell.y,
                                                            .directionAndPk = 0x30}}};
        const auto applied = Client::Scenes::ApplyViewportPlayers(Read(records), game->Resources());
        ASSERT_TRUE(applied.HasValue()) << applied.Error();

        const ActorState& actor = Actor(1);
        EXPECT_EQ(actor.movement, 1);
        ASSERT_GT(actor.path.count, 1);
        EXPECT_EQ(actor.path.cells[0].x, cell.x);
        EXPECT_EQ(actor.path.cells[actor.path.count - 1].x, cell.x + 3);
        EXPECT_EQ(actor.path.cells[actor.path.count - 1].y, cell.y);
        EXPECT_EQ(actor.targetX, cell.x);
        EXPECT_EQ(actor.angles.z, 90.0f);
        EXPECT_EQ(actor.pk, 0);
        EXPECT_FALSE(game->world.Map().effects.Effects()[0].alive);
        EXPECT_EQ(game->cloth.random.State(), AfterDraws(Seed, FreshRecordDraws).State());
    }
}

// 0x4e670f..0x4e67a1 (CL9 hito 4; en el hito 1 cortaban): 0x400 y 0x10 apagan el 0x5ac/2 y /1 del que entra y crean
// otro con él de dueño, sin rand() y sin mirar si ya tenía el bit; después prenden el bit y siguen (el 0x80, aquí). Con
// la tabla llena, ninguno y sigue.
TEST_F(ViewportPlayersTest, TheGuardAndSwellFlagsLightTheirFlaresOnThePlayer)
{
    using Client::World::GuardFlareSubtype;
    using Client::World::SwellFlareSubtype;
    struct Case
    {
        std::uint32_t flags;
        // Los subtipos de los lugares 0 y 1 de la tabla al final (−1, muerto); el 0 empieza con un /2 viejo del que
        // entra.
        std::array<std::int32_t, 2> subtypes;
        // Si el del lugar 0 es el nuevo (el viejo se apagó y el alta tomó su lugar).
        bool renewed;
    };
    for (const std::uint8_t map : {std::uint8_t{0}, std::uint8_t{2}})
    {
        for (const Case& test :
             {Case{0x400u, {GuardFlareSubtype, -1}, true}, Case{0x10u, {GuardFlareSubtype, SwellFlareSubtype}, false},
              Case{0x410u, {GuardFlareSubtype, SwellFlareSubtype}, true}})
        {
            SCOPED_TRACE(testing::Message() << int{map} << " " << test.flags);
            Load(map);
            const Cell cell = FreeRow();
            auto& effects = game->world.Map().effects;
            // El que entra va al lugar 1 de los personajes.
            ASSERT_TRUE(effects.CreateSkillFlare({}, {}, {}, GuardFlareSubtype, &Actor(1)).has_value());
            const std::array<ViewportPlayerRecord, 1> records{{{.key = 0x0458,
                                                                .x = cell.x,
                                                                .y = cell.y,
                                                                .effects = test.flags | 0x80u,
                                                                .name = "Chingolo",
                                                                .targetX = cell.x,
                                                                .targetY = cell.y}}};
            const auto applied = Client::Scenes::ApplyViewportPlayers(Read(records), game->Resources());
            ASSERT_TRUE(applied.HasValue()) << applied.Error();

            const ActorState& actor = Actor(1);
            EXPECT_EQ(actor.flags78, test.flags | 0x80u);
            const auto live = effects.Effects();
            for (std::size_t slot = 0; slot < test.subtypes.size(); ++slot)
            {
                SCOPED_TRACE(slot);
                const auto& effect = live[slot];
                ASSERT_EQ(effect.alive, test.subtypes[slot] >= 0);
                if (!effect.alive)
                    continue;
                EXPECT_EQ(effect.type, Client::World::IcarusFlareEffect);
                EXPECT_EQ(effect.subtype, test.subtypes[slot]);
                EXPECT_EQ(effect.ownerContext, &actor);
                EXPECT_EQ(effect.life, 1000);
                const bool fresh = slot > 0 || test.renewed;
                // El nuevo, donde lo dejó 0x511290 (el registro viejo no lo mueve); el viejo, en el origen.
                EXPECT_EQ(effect.transform.position == actor.position, fresh);
            }
            EXPECT_FALSE(live[2].alive);
            EXPECT_EQ(game->cloth.random.State(), AfterDraws(Seed, FreshRecordDraws).State());
        }
    }
}

TEST_F(ViewportPlayersTest, TheFlaresOfTheFlagsWithAFullTableAreNoneAndTheRestFollows)
{
    for (const std::uint8_t map : {std::uint8_t{0}, std::uint8_t{2}})
    {
        SCOPED_TRACE(map);
        Load(map);
        const Cell cell = FreeRow();
        auto& effects = game->world.Map().effects;
        while (effects.Create(Client::World::ArrivalEffect, {}, {}, {}, 0.0f))
        {
        }
        const std::array<ViewportPlayerRecord, 1> records{
            {{.key = 0x0459, .x = cell.x, .y = cell.y, .effects = 0x410u | 0x1000000u, .name = "Chingolo"}}};
        const auto applied = Client::Scenes::ApplyViewportPlayers(Read(records), game->Resources());
        ASSERT_TRUE(applied.HasValue()) << applied.Error();
        EXPECT_EQ(Actor(1).flags78, 0x410u | 0x1000000u);
        for (const auto& effect : effects.Effects())
            EXPECT_EQ(effect.type, Client::World::ArrivalEffect);
        EXPECT_EQ(game->cloth.random.State(), AfterDraws(Seed, FreshRecordDraws).State());
    }
}

// 0x4c88d2 (0x576960 con "webzen", distinguiendo mayúsculas): el registro no hace nada, pero el paquete ya está en la
// cola.
TEST_F(ViewportPlayersTest, AWebzenNameIsSkippedAfterThePacketIsStored)
{
    Load(0);
    const Cell cell = FreeRow();
    const std::array<ViewportPlayerRecord, 3> records{{
        {.key = 0x0010, .isNew = true, .x = cell.x, .y = cell.y, .name = "elwebzen"},
        {.key = 0x0011, .isNew = true, .x = cell.x, .y = cell.y, .name = "Tero"},
        {.key = 0x0012, .isNew = true, .x = cell.x, .y = cell.y, .name = "WEBZEN"},
    }};
    const auto applied = Client::Scenes::ApplyViewportPlayers(Read(records), game->Resources());
    ASSERT_TRUE(applied.HasValue()) << applied.Error();
    EXPECT_EQ(Client::Scenes::ActorWithKey(game->world, 0x0010), Client::Scenes::CreatedActorSearch);
    EXPECT_EQ(Client::Scenes::ActorWithKey(game->world, 0x0011), 1u);
    EXPECT_EQ(Client::Scenes::ActorWithKey(game->world, 0x0012), 2u);
    EXPECT_EQ(game->world.Runtime().viewportPackets.count, 0);
    EXPECT_EQ(game->cloth.random.State(), AfterDraws(Seed, 2 * FreshRecordDraws).State());
}

// 0x4c88e2..0x4c89ed, 0x4c8a43..0x4c8ad4 y 0x4c9229..0x4c9246: el estado 1 conserva lo del que estaba (de +0x1f2 solo
// el byte de abajo, Q-C800), arranca el teletransporte (0x53d930, con el registro propio abierto y cerrado), guarda el
// paquete otra vez (Q-C801) y al final pisa +0x360 y +0x78 con lo de antes.
TEST_F(ViewportPlayersTest, TheTeleportStateKeepsWhatTheOldOneHad)
{
    Load(0);
    const Cell cell = FreeRow();
    ActorState& old = Actor(3);
    old.alive = true;
    old.key = 0x0222;
    old.type = PlayerSkeletonModel;
    old.word1f2 = 0x1234;
    old.guildStatus = 0x80;
    old.byte369 = 1;
    old.byte370 = 2;
    old.byte360 = 5;
    old.flags78 = 8;
    old.byte372 = 3;
    const std::array<ViewportPlayerRecord, 1> records{{{.key = 0x0222,
                                                        .isNew = true,
                                                        .x = cell.x,
                                                        .y = cell.y,
                                                        .classAndState = 0x21,
                                                        .effects = 1,
                                                        .name = "Benteveo",
                                                        .directionAndPk = 0x10}}};
    const auto applied = Client::Scenes::ApplyViewportPlayers(Read(records), game->Resources());
    ASSERT_TRUE(applied.HasValue()) << applied.Error();

    const ActorState& actor = Actor(3);
    EXPECT_FALSE(Actor(1).alive);
    EXPECT_EQ(actor.word1f2, 0x34);
    EXPECT_EQ(actor.guildStatus, 0x80);
    EXPECT_EQ(actor.byte369, 1);
    EXPECT_EQ(actor.byte370, 2);
    EXPECT_EQ(actor.byte372, 3);
    // 0x5139f0 lo dejó en 0 y 0x4e6380 prendió el bit 1: los pisa lo de antes.
    EXPECT_EQ(actor.byte360, 5);
    EXPECT_EQ(actor.flags78, 8u);
    EXPECT_EQ(actor.animation.action, 0x93);
    EXPECT_EQ(actor.byte80, 3);
    EXPECT_EQ(actor.alphaTarget, 1.0f);
    EXPECT_EQ(actor.alpha, 0.0f);
    EXPECT_EQ(game->world.Runtime().lastSoundId, 0x76);
    const auto effects = game->world.Map().effects.Effects();
    EXPECT_EQ(effects[0].type, Client::World::TeleportEffect);
    EXPECT_EQ(effects[1].type, Client::World::ArrivalEffect);
    EXPECT_FALSE(effects[0].hasOwner);
    EXPECT_EQ(effects[0].ownerContext, nullptr);
    EXPECT_TRUE(effects[1].hasOwner);
    EXPECT_EQ(effects[1].ownerContext, &actor);
    EXPECT_EQ(effects[1].disturbanceState, 0xffffu);
    // 0x4c8a43..0x4c8ad4: el estado 1 copia el paquete entero (Q-C801), además de la copia del dispatcher.
    const auto& ring = game->world.Runtime().viewportPackets;
    EXPECT_EQ(ring.count, 1);
    EXPECT_EQ(ring.sizes[0], 5 + 0x2c);
    EXPECT_EQ(std::to_integer<std::uint8_t>(ring.slots[0][3]), 0x12);
    EXPECT_EQ(game->cloth.random.State(),
              AfterDraws(Seed, FreshRecordDraws + Client::Models::OwnRecordCloseRandoms).State());
}

// 0x4c8a36: los estados 2 a 4 con su acción, y la del elfo (la clase & 7 en 2).
TEST_F(ViewportPlayersTest, TheStatesTwoToFourPutTheirActions)
{
    struct Case
    {
        std::uint8_t state;
        std::uint8_t others;
        std::uint8_t elf;
    };
    for (const Case tested : {Case{2, 0xc5, 0xc7}, Case{3, 0xcb, 0xcc}, Case{4, 0xc9, 0xca}})
    {
        SCOPED_TRACE(tested.state);
        Load(0);
        const Cell cell = FreeRow();
        const std::array<ViewportPlayerRecord, 2> records{{
            {.key = 0x0031, .isNew = true, .x = cell.x, .y = cell.y, .classAndState = tested.state, .name = "Mago"},
            {.key = 0x0032,
             .isNew = true,
             .x = cell.x,
             .y = cell.y,
             .classAndState = Byte(0x40 | tested.state),
             .name = "Arquera"},
        }};
        const auto applied = Client::Scenes::ApplyViewportPlayers(Read(records), game->Resources());
        ASSERT_TRUE(applied.HasValue()) << applied.Error();
        EXPECT_EQ(Actor(1).characterClass, 0);
        EXPECT_EQ(Actor(1).animation.action, tested.others);
        EXPECT_EQ(Actor(2).characterClass, 2);
        EXPECT_EQ(Actor(2).animation.action, tested.elf);
        EXPECT_EQ(game->world.Runtime().viewportPackets.count, 0);
    }
}

// 0x4c91aa..0x4c91e1: en Kalima (24 a 29 y 36), 0x530860(0x610, posición, posición, ángulos, 8, jugador, 20, −1, 0, 0,
// −1) con el jugador de dueño, sin rand(): nace en su posición con z = 300 y sus ángulos, vida 100, 20 puntos, ancho
// 20, mezcla 4. Después sigue: el equipo y el nombre. En Lorencia y Devias, ningún joint.
TEST_F(ViewportPlayersTest, InKalimaTheEnteringPlayerTakesTheJoint0x610Of8)
{
    for (const std::int32_t map : {0, 2, 24, 29, 36})
    {
        SCOPED_TRACE(map);
        Load(map == 2 ? std::uint8_t{2} : std::uint8_t{0});
        game->world.Runtime().map = map;
        const Cell cell = FreeRow();
        const std::array<ViewportPlayerRecord, 1> records{
            {{.key = 0x0040, .isNew = true, .x = cell.x, .y = cell.y, .name = "Hornero", .directionAndPk = 0x30}}};
        const auto applied = Client::Scenes::ApplyViewportPlayers(Read(records), game->Resources());
        ASSERT_TRUE(applied.HasValue()) << applied.Error();
        EXPECT_EQ(game->cloth.random.State(), AfterDraws(Seed, FreshRecordDraws).State());
        const ActorState& actor = Actor(1);
        EXPECT_EQ(actor.cellX, cell.x);
        EXPECT_EQ(std::to_integer<char>(actor.name[0]), 'H');

        const auto& joints = game->world.DrawTail().joints;
        const bool kalima = map != 0 && map != 2;
        const Client::Scenes::JointState& joint = joints[0];
        ASSERT_EQ(joint.alive, kalima);
        for (std::size_t i = 1; i < joints.size(); ++i)
            ASSERT_FALSE(joints[i].alive) << i;
        if (!kalima)
            continue;
        EXPECT_EQ(joint.type, 0x610);
        EXPECT_EQ(joint.subtype, 8);
        EXPECT_EQ(joint.ownerContext, &actor);
        EXPECT_EQ(joint.ownerKind, Client::Scenes::JointOwnerKind::Actor);
        EXPECT_EQ(joint.position, glm::vec3(actor.position.x, actor.position.y, 300.0f));
        EXPECT_EQ(joint.angles, actor.angles);
        EXPECT_EQ(joint.targetPosition, actor.position);
        EXPECT_EQ(joint.life, 100);
        EXPECT_EQ(joint.maximumCount, 20);
        EXPECT_EQ(joint.width, 20.0f);
        EXPECT_EQ(joint.count, -1);
        EXPECT_EQ(joint.key, 0);
        EXPECT_EQ(joint.blend, 4);
        EXPECT_EQ(joint.drawEnabled, 2);
        EXPECT_EQ(joint.category, 1);
        EXPECT_EQ(joint.light, glm::vec3(1.0f));
    }
}

// 0x53801e..0x538055: el paso del 610/8 lee del jugador el byte vivo y la posición (con z = 300); muerto, se apaga.
TEST_F(ViewportPlayersTest, TheKalimaJointFollowsItsPlayerAndDiesWithIt)
{
    Load(0);
    game->world.Runtime().map = 36;
    const Cell cell = FreeRow();
    const std::array<ViewportPlayerRecord, 1> records{
        {{.key = 0x0041, .isNew = true, .x = cell.x, .y = cell.y, .name = "Hornero"}}};
    ASSERT_TRUE(Client::Scenes::ApplyViewportPlayers(Read(records), game->Resources()).HasValue());
    auto& joints = game->world.DrawTail().joints;
    ActorState& actor = Actor(1);
    actor.position = {1234.0f, 5678.0f, 90.0f};
    ASSERT_TRUE(Client::Scenes::StepJoints(joints).HasValue());
    EXPECT_TRUE(joints[0].alive);
    EXPECT_EQ(joints[0].position, glm::vec3(1234.0f, 5678.0f, 300.0f));
    EXPECT_EQ(joints[0].life, 99);
    actor.alive = false;
    ASSERT_TRUE(Client::Scenes::StepJoints(joints).HasValue());
    EXPECT_FALSE(joints[0].alive);
}

// Con los 500 joints vivos, 0x530860 no crea nada y el 0x12 sigue: el equipo y el nombre, sin rand() de más.
TEST_F(ViewportPlayersTest, InKalimaWithTheJointsFullThePlayerStillEnters)
{
    Load(0);
    game->world.Runtime().map = 24;
    auto& joints = game->world.DrawTail().joints;
    for (auto& joint : joints)
    {
        joint.alive = true;
        joint.type = 0x617;
    }
    const Cell cell = FreeRow();
    const std::array<ViewportPlayerRecord, 1> records{
        {{.key = 0x0042, .isNew = true, .x = cell.x, .y = cell.y, .name = "Hornero"}}};
    const auto applied = Client::Scenes::ApplyViewportPlayers(Read(records), game->Resources());
    ASSERT_TRUE(applied.HasValue()) << applied.Error();
    EXPECT_EQ(game->cloth.random.State(), AfterDraws(Seed, FreshRecordDraws).State());
    EXPECT_EQ(std::to_integer<char>(Actor(1).name[0]), 'H');
    for (const auto& joint : joints)
        EXPECT_EQ(joint.type, 0x617);
}

// 0x4c8968 → 0x512870: con los 400 vivos, el alta da el 400 y el registro escribe en ese, el 401 del pool.
TEST_F(ViewportPlayersTest, AFullPoolWritesTheNoneRecord)
{
    Load(0);
    const Cell cell = FreeRow();
    for (std::size_t slot = 0; slot < Client::Scenes::CreatedActorSearch; ++slot)
    {
        Actor(slot).alive = true;
        Actor(slot).key = static_cast<std::uint16_t>(0x1000 + slot);
    }
    const std::array<ViewportPlayerRecord, 1> records{
        {{.key = 0x0040, .isNew = true, .x = cell.x, .y = cell.y, .name = "Hornero"}}};
    const auto applied = Client::Scenes::ApplyViewportPlayers(Read(records), game->Resources());
    ASSERT_TRUE(applied.HasValue()) << applied.Error();
    const ActorState& none = Actor(Client::Scenes::CreatedActorSearch);
    EXPECT_FALSE(none.alive);
    EXPECT_EQ(none.cellX, cell.x);
    EXPECT_EQ(none.byte88, 1);
    EXPECT_EQ(std::to_integer<char>(none.name[0]), 'H');
    EXPECT_EQ(game->cloth.random.State(), AfterDraws(Seed, CellDraws + CellDraws).State());
}

// 0x4e3736..0x4e377e: con diez guardados, el undécimo corre las ranuras 1..9 a 0..8 y entra en la 9.
TEST(ViewportPacketRingTest, TheEleventhPacketShiftsTheSlots)
{
    Client::Scenes::ViewportPacketRing ring;
    for (int packet = 0; packet < 11; ++packet)
    {
        std::array<std::byte, 6> bytes{};
        bytes[5] = std::byte{Byte(packet)};
        Client::Scenes::StoreViewportPacket(ring, bytes);
    }
    EXPECT_EQ(ring.count, 10);
    EXPECT_EQ(std::to_integer<int>(ring.slots[0][5]), 1);
    EXPECT_EQ(std::to_integer<int>(ring.slots[8][5]), 9);
    EXPECT_EQ(std::to_integer<int>(ring.slots[9][5]), 10);
    // Uno de más de 0x100 no entra, pero corre las ranuras igual.
    const std::array<std::byte, 0x101> large{};
    Client::Scenes::StoreViewportPacket(ring, large);
    EXPECT_EQ(ring.count, 9);
    EXPECT_EQ(std::to_integer<int>(ring.slots[0][5]), 2);
    EXPECT_EQ(std::to_integer<int>(ring.slots[8][5]), 10);
    // El de 0x100 justo entra (JG): la cola vuelve a diez.
    std::array<std::byte, 0x100> full{};
    full.back() = std::byte{0xbb};
    Client::Scenes::StoreViewportPacket(ring, full);
    EXPECT_EQ(ring.count, 10);
    EXPECT_EQ(ring.sizes[9], 0x100);
    EXPECT_EQ(ring.slots[9].back(), std::byte{0xbb});
}

// 0x4e3778..0x4e377e: con lugar, uno de más de 0x100 no corre nada ni toca lo guardado.
TEST(ViewportPacketRingTest, AnOversizedPacketDoesNotEraseAShorterRing)
{
    Client::Scenes::ViewportPacketRing ring;
    const std::array<std::byte, 3> teleport{std::byte{0xc1}, std::byte{3}, std::byte{0x1c}};
    Client::Scenes::StoreViewportPacket(ring, teleport);
    const std::array<std::byte, 0x101> large{};
    Client::Scenes::StoreViewportPacket(ring, large);
    EXPECT_EQ(ring.count, 1);
    EXPECT_EQ(ring.sizes[0], 3);
    EXPECT_EQ(ring.slots[0][2], std::byte{0x1c});
}

// 0x4ca780: la clave entera 0x13d apaga +4..+6 de la info del asedio, cualquier otra +0x1f..+0x21 (Q-C802); la baja va
// sin el bit 15 y apaga los ayudantes del que se va.
TEST(ViewportLeaveTest, TheLeaveRemovesWithoutTheNewBitAndClearsTheSiegeHalf)
{
    const auto game = std::make_unique<Game>();
    auto& world = game->world;
    ActorState& leaving = world.Actors()[2];
    leaving = Client::Tests::UnarmedPlayer();
    leaving.alive = true;
    leaving.key = 0x0123;
    leaving.darkSpirit.emplace();
    const auto helper = Client::Scenes::CreateHelper(world, 0xc6, leaving.position, 2);
    ASSERT_TRUE(helper.HasValue()) << helper.Error();
    ASSERT_TRUE(helper.Value().has_value());
    auto& info = world.Runtime().castleSiege.castleSiegeInfo;
    info.emplace();
    info->fill(0xff);

    Client::Scenes::ApplyViewportLeave(Client::Protocol::Actors::Leave{{0x8123, 0x013d}}, world);
    EXPECT_FALSE(leaving.alive);
    EXPECT_FALSE(leaving.darkSpirit.has_value());
    EXPECT_FALSE(world.Helpers()[*helper.Value()].alive);
    EXPECT_EQ((*info)[3], 0xff);
    EXPECT_EQ((*info)[4], 0);
    EXPECT_EQ((*info)[6], 0);
    EXPECT_EQ((*info)[7], 0xff);
    EXPECT_EQ((*info)[0x1e], 0xff);
    EXPECT_EQ((*info)[0x1f], 0);
    EXPECT_EQ((*info)[0x21], 0);
    EXPECT_EQ((*info)[0x22], 0xff);
    EXPECT_TRUE(world.Actors()[0].alive);
    // Sin la info del asedio, solo la baja.
    info.reset();
    world.Actors()[5].alive = true;
    world.Actors()[5].key = 0x0007;
    Client::Scenes::ApplyViewportLeave(Client::Protocol::Actors::Leave{{0x0007}}, world);
    EXPECT_FALSE(world.Actors()[5].alive);
}

// 0x4c92a0: "webzen" no hace nada; el otro da el alta de 0x5141e0 con su tipo (el 3: Monster10.bmd y el modelo 0x15b)
// y FinishTheTransformed le pone la clase del héroe leída antes. Los rand(): los 28 del alta, las dos celdas y el
// camino.
TEST_F(ViewportPlayersTest, TheTransformedIsCreatedByItsType)
{
    Load(0);
    const std::array<Client::Tests::TransformedRecordBytes, 1> hidden{{{.key = 0x0456, .name = "webzen"}}};
    auto read = Client::Protocol::Actors::ReadTransformed(Client::Tests::TransformedPacket(hidden));
    ASSERT_TRUE(read.HasValue());
    EXPECT_TRUE(Client::Scenes::ApplyTransformed(read.Value(), game->Resources()).HasValue());
    EXPECT_EQ(game->cloth.random.State(), CrtRandom(Seed).State());
    const Cell cell = FreeRow();
    Actor(0).characterClass = 3;
    const std::array<Client::Tests::TransformedRecordBytes, 1> shown{{{.key = 0x0456,
                                                                       .x = cell.x,
                                                                       .y = cell.y,
                                                                       .type = 3,
                                                                       .name = "Tatu",
                                                                       .targetX = cell.x,
                                                                       .targetY = cell.y,
                                                                       .directionAndPk = 0x31}}};
    read = Client::Protocol::Actors::ReadTransformed(Client::Tests::TransformedPacket(shown));
    ASSERT_TRUE(read.HasValue());
    const auto applied = Client::Scenes::ApplyTransformed(read.Value(), game->Resources());
    ASSERT_TRUE(applied.HasValue()) << applied.Error();
    const ActorState& actor = Actor(1);
    EXPECT_EQ(actor.type, 0x15b);
    EXPECT_EQ(actor.word376, 3);
    EXPECT_EQ(actor.key, 0x0456);
    EXPECT_EQ(actor.characterClass, 3);
    EXPECT_EQ(actor.pk, 1);
    EXPECT_EQ(actor.byte88, 1);
    EXPECT_EQ(actor.byte3e3, 1);
    EXPECT_EQ(std::to_integer<char>(actor.name[0]), 'T');
    EXPECT_EQ(actor.angles.z, 90.0f);
}

// 0x4c934c: el tipo 68 pide Monster50.bmd, que el Data del laboratorio no trae: el original cierra (P-C173).
TEST_F(ViewportPlayersTest, TheTransformedIntoTheMissingMonsterCuts)
{
    Load(0);
    const std::array<Client::Tests::TransformedRecordBytes, 1> shown{{{.key = 0x0456, .x = 130, .y = 130, .type = 68}}};
    const auto read = Client::Protocol::Actors::ReadTransformed(Client::Tests::TransformedPacket(shown));
    ASSERT_TRUE(read.HasValue());
    const auto applied = Client::Scenes::ApplyTransformed(read.Value(), game->Resources());
    ASSERT_FALSE(applied.HasValue());
    EXPECT_NE(applied.Error().find("Monster50.bmd"), std::string::npos) << applied.Error();
    EXPECT_FALSE(Actor(1).alive);
}

// 0x4c9356..0x4c975d sobre el personaje que dio el alta (que la hito 2 porta): con +0x376 en 7, la escala en 0,8; la
// clase del héroe, el PK, +0x88 y +0x3e3; el camino con la Y leída antes que la X, el nombre y el héroe vestido otra
// vez. El recién llegado no camina: con la posición y el alpha puestos, el círculo 0xec (0x4c965c) y el rayo 0x5d5/2
// (0x4c9674) con él de dueño, sin rand(), y después el nombre.
TEST_F(ViewportPlayersTest, FinishingTheTransformedWalksOrRaisesItsCircleWhenItArrives)
{
    Load(0);
    const Cell cell = FreeRow();
    const Client::Scenes::ActorConstructionContext construction{
        game->world.Map().terrain, 0, Client::World::CullingScene::Game, game->camera.fixedHeight};
    auto created = Client::Scenes::CreateActor(game->world, 0x0456, PlayerSkeletonModel, {cell.x, cell.y, 0.0f},
                                               construction, game->cloth);
    ASSERT_TRUE(created.HasValue()) << created.Error();
    ASSERT_EQ(created.Value(), 1u);
    Actor(1).word376 = 7;
    Client::Protocol::Actors::TransformedRecord record{.key = 0x0456,
                                                       .x = cell.x,
                                                       .y = cell.y,
                                                       .type = 0x0123,
                                                       .targetX = Byte(cell.x + 2),
                                                       .targetY = cell.y,
                                                       .directionAndPk = 0x31};
    record.name[0] = std::byte{'T'};
    const auto walked = Client::Scenes::FinishTheTransformed(1, record, 2, game->Resources());
    ASSERT_TRUE(walked.HasValue()) << walked.Error();
    const ActorState& actor = Actor(1);
    EXPECT_EQ(actor.scale, 0.8f);
    EXPECT_EQ(actor.characterClass, 2);
    EXPECT_EQ(actor.pk, 1);
    EXPECT_EQ(actor.byte88, 1);
    EXPECT_EQ(actor.byte3e3, 1);
    EXPECT_EQ(actor.angles.z, 90.0f);
    EXPECT_EQ(actor.movement, 1);
    EXPECT_EQ(std::to_integer<char>(actor.name[0]), 'T');

    record.isNew = true;
    record.name[0] = std::byte{'U'};
    const CrtRandom before = game->cloth.random;
    const auto arrived = Client::Scenes::FinishTheTransformed(1, record, 2, game->Resources());
    ASSERT_TRUE(arrived.HasValue()) << arrived.Error();
    EXPECT_EQ(std::to_integer<char>(actor.name[0]), 'U');
    const auto effects = std::as_const(game->world.Map().effects).Effects();
    const auto circle = std::ranges::find_if(effects, [](const Client::World::ModelEffect& effect)
                                             { return effect.alive && effect.type == 0xec; });
    ASSERT_NE(circle, effects.end());
    EXPECT_EQ(circle->subtype, 0);
    EXPECT_EQ(circle->ownerContext, &actor);
    EXPECT_EQ(circle->life, 30);
    EXPECT_EQ(circle->transform.position, actor.position);
    const auto& particles = std::as_const(game->world.Particles()).All();
    const auto bolt = std::ranges::find_if(particles, [](const Client::World::Particle& particle)
                                           { return particle.alive && particle.type == 0x5d5; });
    ASSERT_NE(bolt, particles.end());
    EXPECT_EQ(bolt->subtype, 2);
    EXPECT_EQ(bolt->typedOwner, (Client::World::ParticleOwner{Client::World::ParticleOwnerKind::Actor, 1}));
    EXPECT_EQ(actor.alpha, 0.0f);
    EXPECT_EQ(actor.position.x, (static_cast<float>(cell.x) + 0.5f) * 100.0f);
    CrtRandom expected = before;
    for (int draw = 0; draw < CellDraws + CellDraws; ++draw)
        static_cast<void>(expected.Next());
    // El círculo y el rayo no sacan rand(): después de las celdas sólo quedan los de 0x512a30 sobre el héroe publicado,
    // que se repiten desde ahí.
    const CrtRandom arrivedRandom = game->cloth.random;
    game->cloth.random = expected;
    const auto resources = game->Resources();
    ASSERT_TRUE(Client::Scenes::DressTransitionHero(Client::Scenes::PublishedHero(resources), resources).HasValue());
    EXPECT_EQ(arrivedRandom.State(), game->cloth.random.State());
}

// 0x4e68e1..0x4e68fd: el héroe recibe Text[1828] cada vez, aunque ya tenga la bandera; OR corre después del aviso.
TEST_F(ViewportPlayersTest, TheHeroesBlessingFlagShowsText1828OnEveryApplication)
{
    Load(0);
    const Cell cell = FreeRow();
    const auto texts = Client::Tests::MakeTextTable({{1828, "Bendicion"}});
    Client::Render::SpriteBatch sprites;
    Client::Tests::RecordingTextDrawer measure{sprites};
    auto resources = game->Resources();
    resources.texts = &texts;
    resources.textMeasure = &measure;
    resources.screen = [] { return Client::Core::PixelSize{640, 480}; };
    constexpr std::uint32_t BlessingFlag = 0x2000000;
    const Client::Protocol::Actors::TransformedRecord record{
        .key = HeroKey, .x = cell.x, .y = cell.y, .effects = BlessingFlag, .targetX = cell.x, .targetY = cell.y};
    for (std::size_t count = 1; count <= 2; ++count)
    {
        const auto applied = Client::Scenes::FinishTheTransformed(0, record, 0, resources);
        ASSERT_TRUE(applied.HasValue()) << applied.Error();
        const auto& entries = game->world.Runtime().chat.log.entries;
        ASSERT_EQ(entries.size(), count);
        EXPECT_TRUE(entries.front().name.empty());
        EXPECT_EQ(entries.front().text, "Bendicion");
        EXPECT_EQ(entries.front().type, 1);
        EXPECT_NE(Actor(0).flags78 & BlessingFlag, 0u);
    }
}

TEST_F(ViewportPlayersTest, TheBlessingFlagOnAnotherActorNeedsNoMessageResources)
{
    Load(0);
    const Cell cell = FreeRow();
    Actor(1) = Client::Tests::UnarmedPlayer(1);
    Actor(1).alive = true;
    constexpr std::uint32_t BlessingFlag = 0x2000000;
    const Client::Protocol::Actors::TransformedRecord record{
        .key = 0x456, .x = cell.x, .y = cell.y, .effects = BlessingFlag, .targetX = cell.x, .targetY = cell.y};
    const auto applied = Client::Scenes::FinishTheTransformed(1, record, 0, game->Resources());
    ASSERT_TRUE(applied.HasValue()) << applied.Error();
    EXPECT_EQ(Actor(1).flags78 & BlessingFlag, BlessingFlag);
    EXPECT_TRUE(game->world.Runtime().chat.log.entries.empty());
}

TEST_F(ViewportPlayersTest, AMissingBlessingTextKeepsThePrefixAndLeavesTheFinalFlagUnset)
{
    Load(0);
    const Cell cell = FreeRow();
    constexpr std::uint32_t BlessingFlag = 0x2000000;
    const Client::Protocol::Actors::TransformedRecord record{
        .key = HeroKey, .x = cell.x, .y = cell.y, .effects = BlessingFlag | 1u, .targetX = cell.x, .targetY = cell.y};
    const auto applied = Client::Scenes::FinishTheTransformed(0, record, 0, game->Resources());
    ASSERT_FALSE(applied.HasValue());
    EXPECT_NE(applied.Error().find("Text.bmd"), std::string::npos) << applied.Error();
    EXPECT_EQ(Actor(0).flags78 & 1u, 1u);
    EXPECT_EQ(Actor(0).flags78 & BlessingFlag, 0u);
    EXPECT_TRUE(game->world.Runtime().chat.log.entries.empty());
}

// Un Text[1828] vacío sigue pasando por 0x555190: envejece el registro y añade una fila al área vieja.
TEST_F(ViewportPlayersTest, AnEmptyBlessingTextStillUpdatesTheMessageAreaAndTheLogAge)
{
    Load(0);
    const Cell cell = FreeRow();
    const auto texts = Client::Tests::MakeTextTable({});
    Client::Render::SpriteBatch sprites;
    Client::Tests::RecordingTextDrawer measure{sprites};
    auto resources = game->Resources();
    resources.texts = &texts;
    resources.textMeasure = &measure;
    auto& chat = game->world.Runtime().chat;
    chat.log.entries.push_back({.name = "", .text = "previo"});
    const Client::Protocol::Actors::TransformedRecord record{
        .key = HeroKey, .x = cell.x, .y = cell.y, .effects = 0x2000000, .targetX = cell.x, .targetY = cell.y};

    const auto applied = Client::Scenes::FinishTheTransformed(0, record, 0, resources);

    ASSERT_TRUE(applied.HasValue()) << applied.Error();
    ASSERT_EQ(chat.log.entries.size(), 1u);
    EXPECT_EQ(chat.log.entries.front().age, 1u);
    ASSERT_EQ(chat.area.count, 1);
    EXPECT_TRUE(chat.area.rows[0].name.empty());
    EXPECT_TRUE(chat.area.rows[0].text.empty());
    EXPECT_EQ(chat.area.clock, Client::UI::MessageClockSteps);
}

// El 0x25 y el 0xDF sobre un jugador que ya está.
class ShownEquipmentTest : public ::testing::Test
{
protected:
    ShownEquipmentTest()
    {
        ActorState& actor = Other();
        actor = Client::Tests::UnarmedPlayer(1);
        actor.alive = true;
        actor.key = OtherKey;
    }
    ActorState& Other()
    {
        return game->world.Actors()[OtherSlot];
    }
    ActorState& Hero()
    {
        return game->world.Actors()[0];
    }
    Client::Core::Expected<void, std::string> Show(std::uint16_t key, const std::array<std::uint8_t, 7>& item)
    {
        auto read = Client::Protocol::Actors::ReadEquipmentShown(Client::Tests::EquipmentShownPacket(key, item));
        EXPECT_TRUE(read.HasValue());
        return Client::Scenes::ApplyEquipmentShown(read.Value(), game->Resources());
    }
    static constexpr std::uint16_t OtherKey = 0x0042;
    static constexpr std::size_t OtherSlot = 4;
    std::unique_ptr<Game> game = std::make_unique<Game>();
};

// 0x4c83ec..0x4c842c: la derecha con el modelo, el nivel (0x513340 del nibble; de 8 a 15, 0) y el excelente.
TEST_F(ShownEquipmentTest, TheRightHandTakesTheModelLevelAndExcellent)
{
    // El tipo 0x103: el byte 0 y el bit 0x80 del byte 3; el lugar 0 y el nivel 3 en el byte 1.
    ASSERT_TRUE(Show(OtherKey, {0x03, 0x03, 0, 0x85, 0x07, 0, 0}).HasValue());
    EXPECT_EQ(Other().hands[0].model, 0x103 + 0x212);
    EXPECT_EQ(Other().hands[0].level, Client::GameLogic::ItemLevelOf(3));
    EXPECT_EQ(Other().hands[0].flags1b, 5);
    ASSERT_TRUE(Show(OtherKey, {0x03, 0x09, 0, 0, 0, 0, 0}).HasValue());
    EXPECT_EQ(Other().hands[0].level, 0);
    // Vacía (0x1fff): el modelo en 0xffff y el excelente en 0; el nivel queda.
    Other().hands[0].level = 4;
    ASSERT_TRUE(Show(OtherKey, {0xff, 0x00, 0, 0x80, 0, 0xf0, 0}).HasValue());
    EXPECT_EQ(Other().hands[0].model, Client::Scenes::NoItemModel);
    EXPECT_EQ(Other().hands[0].flags1b, 0);
    EXPECT_EQ(Other().hands[0].level, 4);
    EXPECT_EQ(game->cloth.random.State(), CrtRandom(Seed).State());
}

// 0x4c8431..0x4c8457: vaciar la izquierda suelta el cuervo del héroe publicado, no el de este (Q-C803).
TEST_F(ShownEquipmentTest, EmptyingTheLeftHandReleasesThePublishedHerosRaven)
{
    Hero().darkSpirit.emplace();
    Other().darkSpirit.emplace();
    ASSERT_TRUE(Show(OtherKey, {0xff, 0x10, 0, 0x80, 0, 0xf0, 0}).HasValue());
    EXPECT_EQ(Other().hands[1].model, Client::Scenes::NoItemModel);
    EXPECT_FALSE(Hero().darkSpirit.has_value());
    EXPECT_TRUE(Other().darkSpirit.has_value());
}

// 0x4c848b..0x4c8667: el cuerpo con el set; vacío, la parte de la clase.
TEST_F(ShownEquipmentTest, TheBodyTakesTheSetOrTheClassPart)
{
    // La armadura (lugar 3) del tipo 0x1005: el byte 5 con 8 arriba.
    ASSERT_TRUE(Show(OtherKey, {0x05, 0x31, 0, 0x02, 0x05, 0x80, 0}).HasValue());
    EXPECT_EQ(Other().body[1].model, 0x1005 + 0x212);
    EXPECT_EQ(Other().body[1].level, Client::GameLogic::ItemLevelOf(1));
    EXPECT_EQ(Other().body[1].flags1b, 2);
    EXPECT_EQ(Other().body[1].flags1c, 5);
    ASSERT_TRUE(Show(OtherKey, {0xff, 0x30, 0, 0x80, 0, 0xf0, 0}).HasValue());
    EXPECT_EQ(Other().body[1].model,
              Client::GameLogic::ClassPartModel(Client::GameLogic::BodyPart{1}, Other().characterClass));
    EXPECT_EQ(Other().body[1].level, 0);
    EXPECT_EQ(Other().body[1].flags1c, 0);
    // Las alas (7) con el nivel en 0, sin mirar el nibble.
    ASSERT_TRUE(Show(OtherKey, {0x01, 0x7f, 0, 0, 0, 0xc0, 0}).HasValue());
    EXPECT_EQ(Other().wings.model, 0x1801 + 0x212);
    EXPECT_EQ(Other().wings.level, 0);
}

// 0x4c8711..0x4c87d4: la mascota con su ayudante; el 0x1c12 gasta sus tres rand() y Fenrir guarda el excelente.
TEST_F(ShownEquipmentTest, ThePetCreatesItsHelper)
{
    ASSERT_TRUE(Show(OtherKey, {0x00, 0x80, 0, 0, 0, 0xd0, 0}).HasValue());
    EXPECT_EQ(Other().pet.model, 0x1a00 + 0x212);
    const auto& helpers = game->world.Helpers();
    EXPECT_TRUE(helpers[0].alive);
    EXPECT_EQ(helpers[0].type, 0x1c12);
    EXPECT_EQ(helpers[0].rider, OtherSlot);
    EXPECT_EQ(game->cloth.random.State(), AfterDraws(Seed, 3).State());
    // Vacía: 0x443d40 apaga sus ayudantes.
    ASSERT_TRUE(Show(OtherKey, {0xff, 0x80, 0, 0x80, 0, 0xf0, 0}).HasValue());
    EXPECT_EQ(Other().pet.model, Client::Scenes::NoItemModel);
    EXPECT_FALSE(helpers[0].alive);
    // Fenrir (0x1a25) con el excelente 2: el ayudante 0x14c.
    ASSERT_TRUE(Show(OtherKey, {0x25, 0x80, 0, 0x02, 0, 0xd0, 0}).HasValue());
    EXPECT_EQ(Other().pet.flags1b, 2);
    EXPECT_EQ(helpers[0].type, 0x14c);
    EXPECT_EQ(game->cloth.random.State(), AfterDraws(Seed, 3).State());
}

// 0x4c838e → 0x5110d0 con la clave entera: con el bit 15, ninguno; escribe en el 400.
TEST_F(ShownEquipmentTest, TheWholeKeyWithTheNewBitFindsNobody)
{
    ASSERT_TRUE(Show(0x8000 | OtherKey, {0x03, 0x00, 0, 0, 0, 0, 0}).HasValue());
    EXPECT_NE(Other().hands[0].model, 0x3 + 0x212);
    EXPECT_EQ(game->world.Actors()[Client::Scenes::CreatedActorSearch].hands[0].model, 0x3 + 0x212);
}

// 0x4c6ca8..0x4c6dfe: la celda protegida, el blanco y +0x399.
TEST_F(ShownEquipmentTest, ThePositionWritesTheCells)
{
    const auto read = Client::Protocol::Actors::ReadPosition(Client::Tests::PositionPacket(OtherKey, 140, 141));
    ASSERT_TRUE(read.HasValue());
    ASSERT_TRUE(Client::Scenes::ApplyViewportPosition(read.Value(), game->Resources()).HasValue());
    EXPECT_EQ(Other().cellX, 140);
    EXPECT_EQ(Other().cellY, 141);
    EXPECT_EQ(Other().targetX, 140);
    EXPECT_EQ(Other().targetY, 141);
    EXPECT_EQ(Other().byte399, 1);
    EXPECT_EQ(game->cloth.random.State(), AfterDraws(Seed, CellDraws).State());
}

// 0x4c6c9d llama 0x51fe10(posición, 0): dos sorteos por intento, cinco por alta efectiva y ninguna 0x5ed.
TEST_F(ShownEquipmentTest, ThePushedTypeCreatesTwentySparksWithoutMovingItsCell)
{
    const auto read = Client::Protocol::Actors::ReadPosition(Client::Tests::PositionPacket(OtherKey, 140, 141));
    ASSERT_TRUE(read.HasValue());
    Other().type = 0xf1;
    Other().cellX = 138;
    Other().cellY = 143;
    Other().position = {12, 34, 56};
    const auto pushed = Client::Scenes::ApplyViewportPosition(read.Value(), game->Resources());
    ASSERT_TRUE(pushed.HasValue()) << pushed.Error();
    EXPECT_EQ(Other().floatE4, 20.0f);
    EXPECT_EQ(Other().floatD0, 800.0f);
    EXPECT_EQ(Other().floatCC, 800.0f);
    EXPECT_EQ(game->world.Runtime().lastSoundId, 0x1b);
    EXPECT_EQ(Other().cellX, 138);
    EXPECT_EQ(Other().cellY, 143);
    EXPECT_EQ(game->cloth.random.State(), AfterDraws(Seed, CellDraws + 20 * 7).State());
    const auto particles = game->world.Particles().All();
    for (std::size_t index = 0; index < particles.size(); ++index)
    {
        SCOPED_TRACE(index);
        EXPECT_EQ(particles[index].alive, index < 20);
        if (index >= 20)
            continue;
        EXPECT_EQ(particles[index].type, Client::World::FairySparkParticle);
        EXPECT_EQ(particles[index].subtype, 2);
        EXPECT_EQ(particles[index].position, glm::vec3(12, 34, 136));
        EXPECT_EQ(particles[index].light, glm::vec3(1));
        EXPECT_GE(particles[index].angles.x, 150.0f);
        EXPECT_LT(particles[index].angles.x, 210.0f);
    }
}

// 0x51fe4b/0x51fe6e preceden la búsqueda de slot 0x53fe70. Llenar el pool no corta los otros intentos.
TEST_F(ShownEquipmentTest, ThePushedTypeKeepsEveryAttemptWithAPartialOrFullPool)
{
    const auto read = Client::Protocol::Actors::ReadPosition(Client::Tests::PositionPacket(OtherKey, 140, 141));
    ASSERT_TRUE(read.HasValue());
    Other().type = 0xf1;
    Other().cellX = 138;
    Other().cellY = 143;
    for (const std::size_t free : {std::size_t{0}, std::size_t{1}, std::size_t{19}})
    {
        SCOPED_TRACE(free);
        auto particles = game->world.Particles().All();
        for (std::size_t index = 0; index < particles.size(); ++index)
        {
            particles[index].alive = index >= free;
            particles[index].life = 101;
            particles[index].position = {2, 4, 8};
        }
        game->cloth.random = CrtRandom(Seed);
        const auto pushed = Client::Scenes::ApplyViewportPosition(read.Value(), game->Resources());
        ASSERT_TRUE(pushed.HasValue()) << pushed.Error();
        const int draws = CellDraws + 40 + static_cast<int>(free) * 5;
        EXPECT_EQ(game->cloth.random.State(), AfterDraws(Seed, draws).State());
        for (std::size_t index = 0; index < particles.size(); ++index)
        {
            SCOPED_TRACE(index);
            EXPECT_TRUE(particles[index].alive);
            if (index < free)
            {
                EXPECT_EQ(particles[index].type, Client::World::FairySparkParticle);
                EXPECT_EQ(particles[index].subtype, 2);
            }
            else
            {
                EXPECT_EQ(particles[index].life, 101);
                EXPECT_EQ(particles[index].position, glm::vec3(2, 4, 8));
            }
        }
    }
}

// El D3 y el D7 de otro (0x4c6d60 y 0x4cab40) encuentran al que dio de alta el 0x12.
TEST_F(ViewportPlayersTest, TheWalkAndTheHitOfTheOthersFindTheCreatedPlayer)
{
    for (const std::uint8_t map : {std::uint8_t{0}, std::uint8_t{2}})
    {
        SCOPED_TRACE(map);
        Load(map);
        const Cell cell = FreeRow();
        const std::array<ViewportPlayerRecord, 1> records{
            {{.key = 0x0456, .isNew = true, .x = cell.x, .y = cell.y, .name = "Hornero"}}};
        auto applied = Client::Scenes::ApplyViewportPlayers(Read(records), game->Resources());
        ASSERT_TRUE(applied.HasValue()) << applied.Error();
        ActorState& actor = Actor(1);

        applied = Client::Scenes::ApplyWalkReply(
            Client::Protocol::Movement::Walk{0x0456, {Byte(cell.x + 3), cell.y}, 2}, HeroKey, game->Resources());
        ASSERT_TRUE(applied.HasValue()) << applied.Error();
        EXPECT_EQ(actor.targetX, cell.x + 3);
        EXPECT_EQ(actor.direction, 2);
        EXPECT_EQ(actor.movement, 1);

        applied = Client::Scenes::ApplyDamageReply(Client::Protocol::Movement::Damage{0x0456, true, 25, 0, false},
                                                   HeroKey, game->Resources());
        ASSERT_TRUE(applied.HasValue()) << applied.Error();
        EXPECT_EQ(actor.damage, 25);
        EXPECT_EQ(actor.movement, 0);
        EXPECT_TRUE(game->world.DrawTail().damage[0].alive);
    }
}

// El 18 con la acción 0x78 de un jugador que dio de alta el 0x12 (0x4cb750 → 0x4f4a10): el FFF codificado
// conserva1211 y pasa por el sonido aleatorio3c (4f5192); con ambas manosFFFF, solo cierra el registro propio.
// Con una espada de un grupo de 0 a 2 sin la marca +0x1e de Item.bmd en la
// derecha, la 0x23 o la 0x24 por +0x38f y el sonido rand() & 1 + 0x3c (0x4f5192). Después, +0x381 en 1, el frame en 0,
// el blanco con el lugar del héroe y [0x5767964] con el índice.
TEST_F(ViewportPlayersTest, ACreatedPlayerStrikesWithTheEighteen)
{
    constexpr int OwnRecordDraws = 0xf08;
    constexpr std::uint16_t Sword = 0x212;
    for (const std::uint8_t map : {std::uint8_t{0}, std::uint8_t{2}})
    {
        SCOPED_TRACE(map);
        Load(map);
        game->world.Runtime().heroSlot = 0;
        const Cell cell = FreeRow();
        const std::array<ViewportPlayerRecord, 1> records{
            {{.key = 0x0456, .isNew = true, .x = cell.x, .y = cell.y, .name = "Hornero"}}};
        auto applied = Client::Scenes::ApplyViewportPlayers(Read(records), game->Resources());
        ASSERT_TRUE(applied.HasValue()) << applied.Error();
        ActorState& actor = Actor(1);
        const std::uint32_t created = game->cloth.random.State();

        applied = Client::Scenes::ApplyActionReply(
            Client::Protocol::Movement::Action{.key = 0x0456, .direction = 5, .action = 0x78}, game->Resources(), {});
        ASSERT_TRUE(applied.HasValue()) << applied.Error();
        EXPECT_EQ(actor.animation.action, 0x22);
        EXPECT_EQ(actor.animation.frame, 0.0f);
        EXPECT_EQ(actor.angles.z, 180.0f);
        EXPECT_EQ(actor.byte381, 1);
        EXPECT_EQ(actor.attackCount, 1);
        EXPECT_EQ(actor.attackTarget, 0);
        EXPECT_EQ(game->world.Runtime().lastAttacker, 1);
        EXPECT_EQ(actor.hands[0].model, 0x1211);
        EXPECT_EQ(actor.hands[1].model, 0x1211);
        auto encoded = AfterDraws(created, OwnRecordDraws);
        const std::int32_t encodedSound = (encoded.Next() & 1) + 0x3c;
        EXPECT_EQ(game->world.Runtime().lastSoundId, std::optional<std::int32_t>(encodedSound));
        EXPECT_EQ(game->cloth.random.State(), encoded.State());

        // 4f5181/4f5186: únicamente ambos WORD enFFFF omiten el rand() y el sonido.
        actor.hands[0].model = Client::Scenes::NoItemModel;
        actor.hands[1].model = Client::Scenes::NoItemModel;
        game->world.Runtime().lastSoundId.reset();
        const auto unarmed = AfterDraws(encoded.State(), OwnRecordDraws);
        applied = Client::Scenes::ApplyActionReply(
            Client::Protocol::Movement::Action{.key = 0x0456, .direction = 5, .action = 0x78}, game->Resources(), {});
        ASSERT_TRUE(applied.HasValue()) << applied.Error();
        EXPECT_EQ(actor.animation.action, 0x22);
        EXPECT_FALSE(game->world.Runtime().lastSoundId.has_value());
        EXPECT_EQ(game->cloth.random.State(), unarmed.State());

        actor.hands[0].model = Sword;
        actor.animation.action = 0;
        auto expected = AfterDraws(game->cloth.random.State(), OwnRecordDraws);
        const std::int32_t sound = (expected.Next() & 1) + 0x3c;
        applied = Client::Scenes::ApplyActionReply(
            Client::Protocol::Movement::Action{.key = 0x0456, .direction = 1, .action = 0x79}, game->Resources(), {});
        ASSERT_TRUE(applied.HasValue()) << applied.Error();
        // +0x38f en 2 después de los dos ataques: (2 & 1) + 0x23.
        EXPECT_EQ(actor.animation.action, 0x23);
        EXPECT_EQ(actor.attackCount, 3);
        EXPECT_EQ(game->world.Runtime().lastSoundId, std::optional<std::int32_t>(sound));
        EXPECT_EQ(game->cloth.random.State(), expected.State());
    }
}
} // namespace
