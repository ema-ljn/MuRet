#include "Scenes/ViewportPlayers.h"

#include "Core/Expected.h"
#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Data/Models/PlayerModels.h"
#include "Data/Tables/ItemSetTables.h"
#include "Data/Tables/ItemTable.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Data/Terrain/TerrainGrid.h"
#include "GameLogic/CharacterAppearance.h"
#include "Protocol/Movement.h"
#include "Protocol/Viewport.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/LogicalAccess.h"
#include "Scenes/ActorConstruction.h"
#include "Scenes/ActorCreation.h"
#include "Scenes/ActorState.h"
#include "Scenes/ChatReceive.h"
#include "Scenes/DarkSpirit.h"
#include "Scenes/GameDrawTail.h"
#include "Scenes/HelperTable.h"
#include "Scenes/LoginModels.h"
#include "Scenes/MovementReplies.h"
#include "Scenes/SelectionState.h"
#include "Scenes/ViewportPacketRing.h"
#include "Scenes/WorldEntry.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "World/GameCamera.h"
#include "World/ModelEffects.h"
#include "World/TerrainCulling.h"
#include "World/WorldTerrain.h"

#include "support/GoldenDir.h"
#include "support/ItemSetFiles.h"
#include "support/PlayerActors.h"
#include "support/RecordingSink.h"
#include "support/ViewportPackets.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

// Los paquetes del viewport sobre el mundo (CL9 hito 1): el 0x12, el 0x14, el 0x45, el 0x25 y el 0xDF, armados con los
// structs del ASM (support/ViewportPackets.h), en Lorencia (0) y Devias (2). Los nombres son inventados.
namespace
{
using Client::Data::Models::PlayerSkeletonModel;
using Client::Protocol::Viewport::Players;
using Client::Render::Models::CrtRandom;
using Client::Scenes::ActorState;
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
                .dataRoot = {},
                .cloth = cloth,
                .camera = camera,
                .cameraInput = input,
                .models = models,
                .items = &items,
                .itemSetTables = &sets,
                .log = log};
    }
    Client::Scenes::WorldState world;
    Client::Scenes::SelectionState selection{world};
    Client::World::GameCamera camera;
    Client::World::GameCameraInput input;
    Client::Render::Models::ClothWorld cloth{.random = CrtRandom(Seed)};
    Client::Scenes::SingleModelLibrary models;
    // Item.bmd sin nada marcado, para 0x54efe0 desde 0x512a30.
    Client::Data::Tables::ItemTable items = Client::Tests::ItemsMarking({});
    // Sin ningún conjunto: 0x40e1b0 (desde 0x512a30) recorre el equipo y no aplica nada.
    Client::Data::Tables::ItemSetTables sets =
        Client::Tests::ItemSetTablesFrom(std::vector<std::byte>(Client::Data::Tables::ItemSetTypeTableSize),
                                         std::vector<std::byte>(Client::Data::Tables::ItemSetOptionTableSize));
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
    auto read = Client::Protocol::Viewport::ReadPlayers(Client::Tests::ViewportPlayersPacket(records));
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
        EXPECT_EQ(game->cloth.random.State(), AfterDraws(Seed, FreshRecordDraws).State());
        const auto& ring = game->world.Runtime().viewportPackets;
        EXPECT_EQ(ring.count, 1);
        EXPECT_EQ(ring.sizes[0], 5 + 0x2c);
        EXPECT_EQ(std::to_integer<std::uint8_t>(ring.slots[0][3]), 0x12);
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
    EXPECT_EQ(game->world.Runtime().viewportPackets.count, 1);
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
    EXPECT_EQ(game->world.Runtime().viewportPackets.count, 2);
    EXPECT_EQ(game->cloth.random.State(),
              AfterDraws(Seed, FreshRecordDraws + Client::Render::Models::OwnRecordCloseRandoms).State());
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
        EXPECT_EQ(game->world.Runtime().viewportPackets.count, 1);
    }
}

// 0x4c91aa..0x4c91e1: en Kalima (24 a 29 y 36) corta antes del equipo y del nombre, con la celda ya escrita.
TEST_F(ViewportPlayersTest, KalimaCutsBeforeTheEquipment)
{
    for (const std::int32_t map : {24, 29, 36})
    {
        SCOPED_TRACE(map);
        Load(0);
        game->world.Runtime().map = map;
        const Cell cell = FreeRow();
        const std::array<ViewportPlayerRecord, 1> records{
            {{.key = 0x0040, .isNew = true, .x = cell.x, .y = cell.y, .name = "Hornero"}}};
        const auto applied = Client::Scenes::ApplyViewportPlayers(Read(records), game->Resources());
        ASSERT_FALSE(applied.HasValue());
        EXPECT_NE(applied.Error().find("0x4c91dc"), std::string::npos) << applied.Error();
        EXPECT_EQ(Actor(1).cellX, cell.x);
        EXPECT_EQ(std::to_integer<char>(Actor(1).name[0]), '\0');
    }
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
    auto& info = world.Runtime().castleSiegeInfo;
    info.emplace();
    info->fill(0xff);

    Client::Scenes::ApplyViewportLeave(Client::Protocol::Viewport::Leave{{0x8123, 0x013d}}, world);
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
    Client::Scenes::ApplyViewportLeave(Client::Protocol::Viewport::Leave{{0x0007}}, world);
    EXPECT_FALSE(world.Actors()[5].alive);
}

// 0x4c92a0: el primer registro sin "webzen" corta en el alta de 0x5141e0 (hito 2).
TEST(ViewportTransformedTest, TheTransformedCutsAtItsCreation)
{
    const auto game = std::make_unique<Game>();
    const std::array<Client::Tests::TransformedRecordBytes, 1> hidden{{{.key = 0x0456, .name = "webzen"}}};
    auto read = Client::Protocol::Viewport::ReadTransformed(Client::Tests::TransformedPacket(hidden));
    ASSERT_TRUE(read.HasValue());
    EXPECT_TRUE(Client::Scenes::ApplyTransformed(read.Value(), game->Resources()).HasValue());
    const std::array<Client::Tests::TransformedRecordBytes, 1> shown{{{.key = 0x0456, .type = 0x0123, .name = "Tatu"}}};
    read = Client::Protocol::Viewport::ReadTransformed(Client::Tests::TransformedPacket(shown));
    ASSERT_TRUE(read.HasValue());
    const auto applied = Client::Scenes::ApplyTransformed(read.Value(), game->Resources());
    ASSERT_FALSE(applied.HasValue());
    EXPECT_NE(applied.Error().find("0x4c934c"), std::string::npos) << applied.Error();
    EXPECT_EQ(game->cloth.random.State(), CrtRandom(Seed).State());
}

// 0x4c9356..0x4c975d sobre el personaje que dio el alta (que la hito 2 porta): con +0x376 en 7, la escala en 0,8; la
// clase del héroe, el PK, +0x88 y +0x3e3; el camino con la Y leída antes que la X, el nombre y el héroe vestido otra
// vez. El recién llegado corta en 0x4c965c, con la posición y el alpha ya puestos.
TEST_F(ViewportPlayersTest, FinishingTheTransformedWalksOrCutsWhenItArrives)
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
    Client::Protocol::Viewport::TransformedRecord record{.key = 0x0456,
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
    const CrtRandom before = game->cloth.random;
    const auto arrived = Client::Scenes::FinishTheTransformed(1, record, 2, game->Resources());
    ASSERT_FALSE(arrived.HasValue());
    EXPECT_NE(arrived.Error().find("0x4c965c"), std::string::npos) << arrived.Error();
    EXPECT_EQ(actor.alpha, 0.0f);
    EXPECT_EQ(actor.position.x, (static_cast<float>(cell.x) + 0.5f) * 100.0f);
    CrtRandom expected = before;
    for (int draw = 0; draw < CellDraws + CellDraws; ++draw)
        static_cast<void>(expected.Next());
    EXPECT_EQ(game->cloth.random.State(), expected.State());
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
        auto read = Client::Protocol::Viewport::ReadEquipmentShown(Client::Tests::EquipmentShownPacket(key, item));
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

// 0x4c6ca8..0x4c6dfe: la celda protegida, el blanco y +0x399; el tipo 0xf1 corta.
TEST_F(ShownEquipmentTest, ThePositionWritesTheCellsAndTheTypeF1Cuts)
{
    const auto read = Client::Protocol::Viewport::ReadPosition(Client::Tests::PositionPacket(OtherKey, 140, 141));
    ASSERT_TRUE(read.HasValue());
    ASSERT_TRUE(Client::Scenes::ApplyViewportPosition(read.Value(), game->Resources()).HasValue());
    EXPECT_EQ(Other().cellX, 140);
    EXPECT_EQ(Other().cellY, 141);
    EXPECT_EQ(Other().targetX, 140);
    EXPECT_EQ(Other().targetY, 141);
    EXPECT_EQ(Other().byte399, 1);
    EXPECT_EQ(game->cloth.random.State(), AfterDraws(Seed, CellDraws).State());
    Other().type = 0xf1;
    const auto pushed = Client::Scenes::ApplyViewportPosition(read.Value(), game->Resources());
    ASSERT_FALSE(pushed.HasValue());
    EXPECT_NE(pushed.Error().find("0x4c6b80"), std::string::npos) << pushed.Error();
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
} // namespace
