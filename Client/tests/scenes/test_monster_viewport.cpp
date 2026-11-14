#include "Scenes/MonsterViewport.h"

#include "Core/Expected.h"
#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Data/Models/PlayerModels.h"
#include "Data/Tables/ItemSetTables.h"
#include "Data/Tables/ItemTable.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Data/Text/NpcNames.h"
#include "Data/Text/TextTable.h"
#include "Protocol/Viewport.h"
#include "Render/Models/Cloth.h"
#include "Scenes/ActorCreation.h"
#include "Scenes/ActorState.h"
#include "Scenes/ChatReceive.h"
#include "Scenes/FaunaModels.h"
#include "Scenes/LoginModels.h"
#include "Scenes/MonsterCreation.h"
#include "Scenes/MonsterKinds.h"
#include "Scenes/NpcNames.h"
#include "Scenes/SelectionState.h"
#include "Scenes/WorldEntry.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "World/GameCamera.h"
#include "World/WorldTerrain.h"

#include "support/FakeTextureStore.h"
#include "support/FileBytes.h"
#include "support/GoldenDir.h"
#include "support/ItemSetFiles.h"
#include "support/PlayerActors.h"
#include "support/RecordingSink.h"
#include "support/SceneDataFiles.h"
#include "support/ViewportPackets.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Los monstruos y los NPC del viewport (CL9 hito 2): el 0x13, el 0x1F y el 0x68 sobre el alta de 0x5141e0, con el Data
// del laboratorio (los Monster*.bmd que mira el alta y los mundos). Los registros van armados con los structs del ASM
// (support/ViewportPackets.h); los nombres de los dueños y el Text 485 son inventados.
namespace
{
using Client::Data::Models::PlayerSkeletonModel;
using Client::Render::Models::CrtRandom;
using Client::Scenes::ActorState;
using Client::Tests::AfterDraws;
using Client::Tests::MonsterRecordBytes;
using Client::Tests::ObjectRecordBytes;
using Client::Tests::SummonRecordBytes;

constexpr std::uint32_t Seed = 0x5eed;
constexpr std::uint16_t HeroKey = 33;
constexpr std::int32_t HeroSlot = 5;
// 0x511290 desde cero: las dos escrituras de la celda, sus cuatro lecturas y la del contador, 4 rand() cada una.
constexpr int ConstructionDraws = 28;
// Dos accesos a la celda protegida (+0x41c y +0x420).
constexpr int CellDraws = 8;
// El alta, las dos celdas del registro y las dos lecturas del camino.
constexpr int WalkingRecordDraws = ConstructionDraws + CellDraws + CellDraws;
// El alta y las dos celdas: el recién llegado no camina.
constexpr int ArrivingRecordDraws = ConstructionDraws + CellDraws;

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
        world.Runtime().heroSlot = HeroSlot;
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
                .log = log,
                .texts = texts};
    }
    // Antes que el mundo: FaunaModels suelta sus texturas al destruirse.
    Client::Tests::FakeTextureStore store;
    Client::Scenes::WorldState world;
    std::filesystem::path dataRoot;
    Client::Scenes::SelectionState selection{world};
    Client::World::GameCamera camera;
    Client::World::GameCameraInput input;
    Client::Render::Models::ClothWorld cloth{.random = CrtRandom(Seed)};
    Client::Scenes::SingleModelLibrary models;
    Client::Data::Tables::ItemTable items = Client::Tests::ItemsMarking({});
    Client::Data::Tables::ItemSetTables sets =
        Client::Tests::ItemSetTablesFrom(std::vector<std::byte>(Client::Data::Tables::ItemSetTypeTableSize),
                                         std::vector<std::byte>(Client::Data::Tables::ItemSetOptionTableSize));
    const Client::Data::Text::TextTable* texts = nullptr;
    Client::Tests::RecordingSink sink;
    Client::Core::Log log{sink, Client::Core::LogLevel::Debug};
};

struct Cell
{
    std::uint8_t x = 0;
    std::uint8_t y = 0;
};

std::uint8_t Byte(int value)
{
    return static_cast<std::uint8_t>(value);
}

float Real(std::uint32_t bits)
{
    return std::bit_cast<float>(bits);
}

Client::Protocol::Viewport::Monsters ReadMonsters(std::span<const MonsterRecordBytes> records)
{
    auto read = Client::Protocol::Viewport::ReadMonsters(Client::Tests::MonstersPacket(records));
    EXPECT_TRUE(read.HasValue());
    return std::move(read).Value();
}

Client::Protocol::Viewport::Summons ReadSummons(std::span<const SummonRecordBytes> records)
{
    auto read = Client::Protocol::Viewport::ReadSummons(Client::Tests::SummonsPacket(records));
    EXPECT_TRUE(read.HasValue());
    return std::move(read).Value();
}

Client::Protocol::Viewport::Objects ReadObjects(std::span<const ObjectRecordBytes> records)
{
    auto read = Client::Protocol::Viewport::ReadObjects(Client::Tests::ObjectsPacket(records));
    EXPECT_TRUE(read.HasValue());
    return std::move(read).Value();
}

class MonsterViewportTest : public Client::Tests::GoldenDataTest
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

    [[nodiscard]] Client::Core::Expected<void, std::string> Monsters(std::span<const MonsterRecordBytes> records)
    {
        return Client::Scenes::ApplyViewportMonsters(ReadMonsters(records), game->Resources());
    }

    [[nodiscard]] Client::Core::Expected<void, std::string> Summons(std::span<const SummonRecordBytes> records)
    {
        return Client::Scenes::ApplySummons(ReadSummons(records), game->Resources());
    }

    [[nodiscard]] Client::Core::Expected<void, std::string> Objects(std::span<const ObjectRecordBytes> records)
    {
        return Client::Scenes::ApplyViewportObjects(ReadObjects(records), game->Resources());
    }

    std::unique_ptr<Game> game;
};

// 0x4c9c60 con el caso del switch: en Lorencia el tipo 3 (0x515a1f) y en Devias el 19 (0x51595e) cargan su .bmd, dan
// de alta el modelo con su caja y sus manos, el nombre y la escala; la cola pone +0x376, +0x3a4 y la clase 2. Después,
// el ángulo por 45, las celdas, +0x42d y +0x93 del tipo alto y el camino. Los rand(): los 28 del alta, las dos celdas
// y las dos lecturas del camino.
TEST_F(MonsterViewportTest, TheMonstersOfLorenciaAndDeviasComeFromTheirCase)
{
    struct Case
    {
        std::uint8_t map;
        std::uint8_t type;
        std::uint16_t model;
        std::uint32_t scale;
        const char* name;
        glm::vec3 box;
    };
    const std::array<Case, 2> cases{{
        {0, 3, 0x15b, 0x3ecccccd, "\xb0\xc5\xb9\xcc", {50.0f, 50.0f, 80.0f}},
        {2, 19, 0x15e, 0x3f8ccccd, "\xbc\xb3\xc0\xce", {50.0f, 50.0f, 150.0f}},
    }};
    for (const Case& tested : cases)
    {
        SCOPED_TRACE(tested.map);
        Load(tested.map);
        const Cell cell = FreeRow();
        const std::array<MonsterRecordBytes, 1> records{{{.key = 0x0123,
                                                          .typeHigh = 0xb0,
                                                          .typeLow = tested.type,
                                                          .x = cell.x,
                                                          .y = cell.y,
                                                          .targetX = Byte(cell.x + 3),
                                                          .targetY = cell.y,
                                                          .direction = 0x35}}};
        const auto applied = Monsters(records);
        ASSERT_TRUE(applied.HasValue()) << applied.Error();

        const ActorState& actor = Actor(1);
        EXPECT_TRUE(actor.alive);
        EXPECT_EQ(actor.key, 0x0123);
        EXPECT_EQ(actor.type, tested.model);
        EXPECT_EQ(actor.word376, tested.type);
        EXPECT_EQ(actor.scale, Real(tested.scale));
        EXPECT_EQ(Client::Scenes::ActorText(actor), tested.name);
        EXPECT_EQ(actor.boundsMin, (glm::vec3{-60.0f, -60.0f, 0.0f}));
        EXPECT_EQ((glm::vec3{actor.boundsMaxX, actor.boundsMaxY, actor.boundsTop}), tested.box);
        EXPECT_EQ(actor.hands[0].bone, 0x21u);
        EXPECT_EQ(actor.hands[1].bone, 0x2au);
        EXPECT_EQ(actor.byte88, 2);
        EXPECT_EQ(actor.byte8, 0);
        EXPECT_EQ(actor.word3a4, HeroSlot);
        EXPECT_EQ(actor.characterClass, 0);
        // (3 − 1) · 45.
        EXPECT_EQ(actor.angles.z, 90.0f);
        EXPECT_EQ(actor.byte42d, 0x80);
        EXPECT_EQ(actor.byte93, 3);
        EXPECT_EQ(actor.cellX, cell.x);
        EXPECT_EQ(actor.cellY, cell.y);
        EXPECT_EQ(actor.targetX, cell.x);
        EXPECT_EQ(actor.movement, 1);
        EXPECT_EQ(actor.alpha, 1.0f);
        EXPECT_EQ(game->cloth.random.State(), AfterDraws(Seed, WalkingRecordDraws).State());
    }
}

// 0x51411f..0x51416b: con NpcName(%s).txt cargado, la cola copia a +0x1d9 el nombre del primer registro con el tipo
// (con su '\0': lo que queda del nombre del caso no se ve). Un tipo sin registro se queda con el del caso. Uno de 25
// letras deja su '\0' en el byte bajo del WORD de +0x1f2, como el strcpy sin tope del original (Q-C847); uno que
// llegaría a las partes de +0x218 corta antes de +0x376 (P-C352). Los nombres son inventados.
TEST_F(MonsterViewportTest, TheTailCopiesTheNameOfTheTable)
{
    const auto put = [](Client::Scenes::NpcName& record, std::uint16_t type, std::string_view name)
    {
        record.type = type;
        record.name = {};
        std::ranges::copy(name, record.name.begin());
    };
    struct Case
    {
        std::uint8_t map;
        std::uint8_t type;
        const char* caseName;
    };
    for (const Case& tested : {Case{0, 3, "\xb0\xc5\xb9\xcc"}, Case{2, 19, "\xbc\xb3\xc0\xce"}})
    {
        SCOPED_TRACE(tested.map);
        Load(tested.map);
        auto& names = game->world.Runtime().npcNames;
        // El 0x54ebb0 de la cola compara el word sin signo con el dword del tipo: gana el primero.
        put(names[4], tested.type, "Bicho");
        put(names[9], tested.type, "Otro bicho");
        const Cell cell = FreeRow();
        std::array<MonsterRecordBytes, 1> records{
            {{.key = 0x0123, .typeLow = tested.type, .x = cell.x, .y = cell.y, .targetX = cell.x, .targetY = cell.y}}};
        auto applied = Monsters(records);
        ASSERT_TRUE(applied.HasValue()) << applied.Error();
        EXPECT_EQ(Client::Scenes::ActorText(Actor(1)), "Bicho");
        EXPECT_EQ(Actor(1).word376, tested.type);

        names[4].type = 0;
        names[9].type = 0;
        records[0].key = 0x0124;
        applied = Monsters(records);
        ASSERT_TRUE(applied.HasValue()) << applied.Error();
        EXPECT_EQ(Client::Scenes::ActorText(Actor(2)), tested.caseName);

        // 24 letras y el '\0' llegan a +0x1f1; el WORD de +0x1f2 sigue con el 0xffff de 0x511290.
        put(names[4], tested.type, "Bicho de veinticuatro le");
        records[0].key = 0x0125;
        applied = Monsters(records);
        ASSERT_TRUE(applied.HasValue()) << applied.Error();
        EXPECT_EQ(Client::Scenes::ActorText(Actor(3)), "Bicho de veinticuatro le");
        EXPECT_EQ(Actor(3).word1f2, 0xffff);

        // Con una más, el '\0' cae en el byte bajo de +0x1f2; la clave de +0x1f4 no la toca.
        put(names[4], tested.type, "Bicho de veinticinco letr");
        records[0].key = 0x0126;
        applied = Monsters(records);
        ASSERT_TRUE(applied.HasValue()) << applied.Error();
        EXPECT_EQ(Client::Scenes::ActorText(Actor(4)), "Bicho de veinticinco letr");
        EXPECT_EQ(Actor(4).word1f2, 0xff00);
        EXPECT_EQ(Actor(4).key, 0x0126);
        EXPECT_EQ(Actor(4).word376, tested.type);

        // Sin '\0' en sus 0x34 bytes, 0x54ebb0 sigue por el tipo y el nombre del registro 5: 52 + 2 + 10 letras y el
        // '\0' son 65 bytes desde +0x1d9, que pasan de +0x217.
        names[4].type = tested.type;
        std::ranges::fill(names[4].name, 'x');
        put(names[5], 0x7979, "zzzzzzzzzz");
        records[0].key = 0x0127;
        applied = Monsters(records);
        ASSERT_FALSE(applied.HasValue());
        EXPECT_NE(applied.Error().find("0x514164"), std::string::npos) << applied.Error();
        EXPECT_NE(applied.Error().find("P-C352"), std::string::npos) << applied.Error();
        // +0x376 sigue con el 0xffff de 0x511290.
        EXPECT_EQ(Actor(5).word376, 0xffff);
        names[5].type = 0;
    }
}

// Los tres nombres de NpcName(Eng).txt de más de 24 letras: el strcpy de 0x51416b sigue por +0x1f2 (76 y 246, con su
// '\0' en el byte alto) y, con las 34 de 231, por la clave de +0x1f4 (que 0x512926 ya escribió), +0x1f6 y +0x1f7 (que
// MuRet no guarda), la cabeza de +0x1f8 y +0x1fa..+0x1fb (P-C352). El nombre de 231 no tiene '\0' en los 29 bytes que
// MuRet guarda seguidos.
TEST_F(MonsterViewportTest, TheLongNamesOfTheTableSpillIntoTheKey)
{
    const auto path = DataFile("Local\\NpcName(Eng).txt");
    ASSERT_TRUE(path.has_value());
    const std::optional<std::vector<char>> file = Client::Tests::ReadFileBytes(*path);
    ASSERT_TRUE(file.has_value());
    const auto names = Client::Data::Text::LoadNpcNames(std::as_bytes(std::span(*file)));
    ASSERT_TRUE(names.HasValue());
    for (const std::uint8_t map : {std::uint8_t{0}, std::uint8_t{2}})
    {
        SCOPED_TRACE(map);
        Load(map);
        game->world.Runtime().npcNames = Client::Scenes::NpcNameTableOf(names.Value());
        const Cell cell = FreeRow();
        const auto monster = [&cell](std::uint16_t key, std::uint8_t type)
        {
            return MonsterRecordBytes{
                .key = key, .typeLow = type, .x = cell.x, .y = cell.y, .targetX = cell.x, .targetY = cell.y};
        };
        const std::array<MonsterRecordBytes, 3> records{
            {monster(0x0130, 76), monster(0x0131, 246), monster(0x0132, 231)}};
        const auto applied = Monsters(records);
        ASSERT_TRUE(applied.HasValue()) << applied.Error();

        EXPECT_EQ(Client::Scenes::ActorText(Actor(1)), "Phoenix of Darkness shield");
        EXPECT_EQ(Actor(1).word1f2, 0x0064);
        EXPECT_EQ(Actor(1).key, 0x0130);
        EXPECT_EQ(Actor(1).word376, 76);
        EXPECT_EQ(Client::Scenes::ActorText(Actor(2)), "Zienna the Weapon Merchant");
        EXPECT_EQ(Actor(2).word1f2, 0x0074);
        EXPECT_EQ(Actor(2).key, 0x0131);

        // "Thompson Kenel the Weapon Merchant": " M" en +0x1f2, "er" en la clave, "ch" perdidas, "an" en la cabeza.
        const ActorState& merchant = Actor(3);
        EXPECT_EQ(merchant.word376, 231);
        EXPECT_EQ(Client::Scenes::ActorText(merchant), "Thompson Kenel the Weapon Mer");
        EXPECT_EQ(merchant.word1f2, 0x4d20);
        EXPECT_EQ(merchant.key, 0x7265);
        EXPECT_EQ(merchant.head, 0x6e61);
        // La clave ya no es la del registro: el original tampoco lo encuentra por ella.
        EXPECT_EQ(Client::Scenes::ActorWithKey(game->world, 0x0132), Client::Scenes::CreatedActorSearch);
    }
}

// 0x4c9acb..0x4c9af7: el NPC recién llegado (237 en Lorencia, 241 en Devias) queda con el alpha en 0 y no camina; la
// cola le pone la clase 4 (de 201 a 259).
TEST_F(MonsterViewportTest, ANewNpcStartsTransparentWithItsClass)
{
    struct Case
    {
        std::uint8_t map;
        std::uint8_t type;
        std::uint16_t model;
    };
    for (const Case& tested : {Case{0, 237, 0x1d9}, Case{2, 241, 0x1d5}})
    {
        SCOPED_TRACE(tested.map);
        Load(tested.map);
        const Cell cell = FreeRow();
        const std::array<MonsterRecordBytes, 1> records{{{.key = 0x0200,
                                                          .isNew = true,
                                                          .typeLow = tested.type,
                                                          .x = cell.x,
                                                          .y = cell.y,
                                                          .targetX = Byte(cell.x + 3),
                                                          .targetY = cell.y,
                                                          .direction = 0x10}}};
        const auto applied = Monsters(records);
        ASSERT_TRUE(applied.HasValue()) << applied.Error();
        const ActorState& actor = Actor(1);
        EXPECT_EQ(actor.type, tested.model);
        EXPECT_EQ(actor.byte88, 4);
        EXPECT_EQ(actor.alpha, 0.0f);
        EXPECT_EQ(actor.movement, 0);
        EXPECT_EQ(actor.angles.z, 0.0f);
        EXPECT_EQ(game->cloth.random.State(), AfterDraws(Seed, ArrivingRecordDraws).State());
    }
    EXPECT_EQ(Client::Scenes::ActorText(Actor(1)), "\xb8\xb6\xbd\xba\xc5\xcd");
}

// En Castle Siege (30) el 224 va por el creador de evento 0x42d070: su rand() (0x42d40a) sale después de los 28 del
// alta y antes de las celdas, y el subtipo es 10 más el resto.
TEST_F(MonsterViewportTest, TheCastleSiegeCreatorDrawsAfterTheCreation)
{
    ASSERT_EQ(Client::Scenes::MonsterCreationOf(224, 30).creator, 0x42d070u);
    for (const int wanted : {0, 1})
    {
        SCOPED_TRACE(wanted);
        Load(30);
        const std::uint32_t seed = Client::Tests::FirstSeedWhere(
            [wanted](CrtRandom& random)
            {
                random.Discard(ConstructionDraws);
                return random.Next() % 2 == wanted;
            });
        game->cloth.random = CrtRandom(seed);
        const Cell cell = FreeRow();
        const std::array<MonsterRecordBytes, 1> records{
            {{.key = 0x0300, .typeLow = 224, .x = cell.x, .y = cell.y, .targetX = cell.x, .targetY = cell.y}}};
        const auto applied = Monsters(records);
        ASSERT_TRUE(applied.HasValue()) << applied.Error();
        const ActorState& actor = Actor(1);
        EXPECT_EQ(actor.type, 0x1fe);
        EXPECT_EQ(actor.subtype, 10 + wanted);
        EXPECT_EQ(actor.dword394, 1);
        EXPECT_EQ(actor.scale, 1.0f);
        EXPECT_EQ(actor.hands[0].model, 0xffff);
        EXPECT_EQ(actor.hands[1].model, 0xffff);
        EXPECT_EQ(actor.byte88, 4);
        EXPECT_EQ(Client::Scenes::ActorText(actor), "\xb1\xd9\xc0\xa7\xba\xb4");
        EXPECT_EQ(game->cloth.random.State(), AfterDraws(seed, WalkingRecordDraws + 1).State());
    }
}

// 0x51443f, el 163 en Devil Square (9): el rand() del caso sale antes que los del alta y elige el .bmd, el modelo
// (0x199 o 0x19a, con la caja de 0x5122d4) y el arma.
TEST_F(MonsterViewportTest, TheRandomOfACaseCanGoBeforeTheCreation)
{
    for (const int wanted : {0, 1})
    {
        SCOPED_TRACE(wanted);
        Load(9);
        const std::uint32_t seed =
            Client::Tests::FirstSeedWhere([wanted](CrtRandom& random) { return random.Next() % 2 == wanted; });
        game->cloth.random = CrtRandom(seed);
        const Cell cell = FreeRow();
        const std::array<MonsterRecordBytes, 1> records{
            {{.key = 0x0400, .typeLow = 163, .x = cell.x, .y = cell.y, .targetX = cell.x, .targetY = cell.y}}};
        const auto applied = Monsters(records);
        ASSERT_TRUE(applied.HasValue()) << applied.Error();
        const ActorState& actor = Actor(1);
        EXPECT_EQ(actor.type, 0x199 + wanted);
        EXPECT_EQ(actor.scale, Real(0x3f666666));
        EXPECT_EQ(actor.hands[0].model, wanted == 0 ? 0xa25 : 0xc17);
        EXPECT_EQ(actor.hands[1].model, 0xffff);
        EXPECT_EQ(actor.boundsMaxX, 40.0f);
        EXPECT_EQ(actor.boundsTop, 120.0f);
        EXPECT_EQ(game->cloth.random.State(), AfterDraws(seed, 1 + WalkingRecordDraws).State());
    }
}

// En Crywolf (34), el 312 de 0x430c90 nombra tres huesos (0x401000); darlo de alta otra vez con la misma clave los
// reemplaza y la baja (0x510ff0 → 0x401040) los saca.
TEST_F(MonsterViewportTest, TheCrywolfStatueNamesItsBonesUntilItLeaves)
{
    Load(34);
    const Cell cell = FreeRow();
    const std::array<MonsterRecordBytes, 1> records{
        {{.key = 0x0500, .typeHigh = 1, .typeLow = 312 - 0x100, .x = cell.x, .y = cell.y}}};
    for (int pass = 0; pass < 2; ++pass)
    {
        const auto applied = Monsters(records);
        ASSERT_TRUE(applied.HasValue()) << applied.Error();
    }
    const ActorState& actor = Actor(1);
    EXPECT_EQ(actor.type, 0x1b2);
    EXPECT_EQ(actor.word376, 312);
    EXPECT_EQ(actor.scale, Real(0x3f99999a));
    const auto& bones = game->world.Runtime().namedBones;
    ASSERT_EQ(bones.size(), 3u);
    EXPECT_EQ(bones[0].actor, 1u);
    EXPECT_EQ(bones[0].name, "Monster96_Top");
    EXPECT_EQ(bones[0].bone, 27u);
    EXPECT_EQ(bones[1].name, "Monster96_Center");
    EXPECT_EQ(bones[1].bone, 28u);
    EXPECT_EQ(bones[2].name, "Monster96_Bottom");
    EXPECT_EQ(bones[2].bone, 29u);

    Client::Scenes::RemoveActor(game->world, 0x0500);
    EXPECT_TRUE(game->world.Runtime().namedBones.empty());
}

// El 348 de Crywolf (0x4313a2): el alta lo gira 180 y, con la X del argumento en 62 o 183, le escribe 90 en +0x24. El
// 0x13 lo gira otra vez con el paso de 0x4c9d01..0x4c9d36, que lee +0x420 y +0x41c sin abrir: lo que dejó el último
// cierre de 0x511290, cuatro rand() & 0xff que no dan nunca 0x5a, 0x3e ni 0xb7 (Q-C849), así que el paso es 45. Solo
// con la entrada abierta por otro (la cuenta en 1) la variable queda en claro y da 0 (la Y en 90) o −135 (la X en 62).
TEST_F(MonsterViewportTest, TheCrywolfAltarReadsItsClosedCell)
{
    Load(34);
    auto created = Client::Scenes::CreateMonster(348, 100, 100, 0x0600, game->Resources());
    ASSERT_TRUE(created.HasValue()) << created.Error();
    EXPECT_EQ(Actor(created.Value()).angles.z, 180.0f);
    created = Client::Scenes::CreateMonster(348, 62, 100, 0x0601, game->Resources());
    ASSERT_TRUE(created.HasValue()) << created.Error();
    EXPECT_EQ(Actor(created.Value()).angles.z, 90.0f);

    struct Case
    {
        std::uint16_t key;
        Cell cell;
        bool openY;
        bool openX;
        float angle;
    };
    for (const Case& tested :
         {Case{0x0602, {62, 100}, false, false, 90.0f}, Case{0x0602, {183, 100}, false, false, 90.0f},
          Case{0x0602, {100, 90}, false, false, 90.0f}, Case{0x0603, {100, 90}, true, false, 0.0f},
          Case{0x0604, {62, 100}, false, true, -270.0f}})
    {
        SCOPED_TRACE(tested.key);
        SCOPED_TRACE(tested.cell.x);
        const std::size_t slot = tested.key - 0x0602 + 3;
        if (tested.openY)
            Actor(slot).cellYAccesses = 1;
        if (tested.openX)
            Actor(slot).cellXAccesses = 1;
        const std::array<MonsterRecordBytes, 1> records{{{.key = tested.key,
                                                          .typeHigh = 1,
                                                          .typeLow = 348 - 0x100,
                                                          .x = tested.cell.x,
                                                          .y = tested.cell.y,
                                                          .targetX = tested.cell.x,
                                                          .targetY = tested.cell.y,
                                                          .direction = 0x30}}};
        const auto applied = Monsters(records);
        ASSERT_TRUE(applied.HasValue()) << applied.Error();
        EXPECT_EQ(Actor(slot).type, 0x1b5);
        EXPECT_EQ(Actor(slot).angles.z, tested.angle);
    }
}

// 0x612d90 y 0x612270 desde el alta (P-C176): en Lorencia el tipo 3 carga Monster10.bmd en el 0x15b con la acción 2 en
// 1,2 (0x612f4a); en Devias el 19, Monster13.bmd en el 0x15e con el hueso de la cabeza en 0x14 y sus sonidos
// (0x6133f4); el herrero (251) Smith01.bmd en el 0x1ce con sus acciones a 0,25; en Chaos Castle (18) el 162 lee las
// texturas de Monster71.bmd de la carpeta Npc (0x612e0d) y en Castle Siege (30) el 283 las de Monster75.bmd de la
// Object31 (0x612e2a), antes de cortar en su llegada (CL11). Cargar no saca rand() ni vuelve a leer lo que ya está.
TEST_F(MonsterViewportTest, TheCreationLoadsTheModelsOfItsCase)
{
    struct Case
    {
        std::uint8_t map;
        std::uint16_t type;
        std::uint16_t model;
        const char* folder;
        bool cuts;
    };
    const std::array<Case, 5> cases{{
        {0, 3, 0x15b, "Monster\\", false},
        {2, 19, 0x15e, "Monster\\", false},
        {0, 251, 0x1ce, "Npc\\", false},
        {18, 162, 0x198, "Npc\\", false},
        {30, 283, 0x19c, "Object31\\", true},
    }};
    for (const Case& tested : cases)
    {
        SCOPED_TRACE(tested.type);
        Load(tested.map);
        const Cell cell = FreeRow();
        const std::array<MonsterRecordBytes, 1> records{{{.key = 0x0123,
                                                          .typeHigh = Byte(tested.type >> 8),
                                                          .typeLow = Byte(tested.type),
                                                          .x = cell.x,
                                                          .y = cell.y,
                                                          .targetX = cell.x,
                                                          .targetY = cell.y}}};
        const auto applied = Monsters(records);
        ASSERT_EQ(applied.HasValue(), !tested.cuts) << (applied.HasValue() ? "" : applied.Error());
        auto& models = game->world.FaunaResources();
        const auto* model = models.Find(tested.model);
        ASSERT_NE(model, nullptr);
        ASSERT_FALSE(model->meshes.empty());
        EXPECT_EQ(models.MeshTextures(tested.model).size(), model->meshes.size());
        const auto requests = models.TextureTable().Slots().ToLoad();
        ASSERT_FALSE(requests.empty());
        EXPECT_TRUE(requests.front().path.starts_with(tested.folder)) << requests.front().path;
        EXPECT_GT(game->store.loadCalls, 0);
        if (!tested.cuts)
        {
            EXPECT_EQ(Actor(1).type, tested.model);
        }
        if (tested.model == 0x198)
        {
            // El 0x46 va por 0x613053: las acciones 3 y 4 en 0,5 y la 2 en 0,3; el hueso de la cabeza, 0x14.
            const auto* skeleton = models.SkeletonOf(0x198);
            ASSERT_NE(skeleton, nullptr);
            EXPECT_EQ(skeleton->actions[3].speed, 0.5f);
            EXPECT_EQ(skeleton->actions[2].speed, Real(0x3e99999a));
            EXPECT_EQ(skeleton->headBone, 0x14);
        }
    }

    Load(2);
    const Cell cell = FreeRow();
    std::array<MonsterRecordBytes, 1> records{
        {{.key = 0x0124, .typeLow = 19, .x = cell.x, .y = cell.y, .targetX = cell.x, .targetY = cell.y}}};
    ASSERT_TRUE(Monsters(records).HasValue());
    const auto& devias = game->world.FaunaResources();
    EXPECT_EQ(devias.SkeletonOf(0x15e)->actions[2].speed, Real(0x3e99999a));
    EXPECT_EQ(devias.SkeletonOf(0x15e)->headBone, 0x14);
    EXPECT_EQ(devias.SoundActions(0x15e)[0], 0x44);
    const int loads = game->store.loadCalls;
    records[0].key = 0x0125;
    ASSERT_TRUE(Monsters(records).HasValue());
    EXPECT_EQ(game->store.loadCalls, loads);
    EXPECT_EQ(game->cloth.random.State(), AfterDraws(Seed, 2 * WalkingRecordDraws).State());

    Load(0);
    const Cell lorencia = FreeRow();
    records[0] = {
        .key = 0x0126, .typeLow = 3, .x = lorencia.x, .y = lorencia.y, .targetX = lorencia.x, .targetY = lorencia.y};
    ASSERT_TRUE(Monsters(records).HasValue());
    EXPECT_EQ(game->world.FaunaResources().SkeletonOf(0x15b)->actions[2].speed, Real(0x3f99999a));
    records[0] = {
        .key = 0x0127, .typeLow = 251, .x = lorencia.x, .y = lorencia.y, .targetX = lorencia.x, .targetY = lorencia.y};
    ASSERT_TRUE(Monsters(records).HasValue());
    for (const auto& action : game->world.FaunaResources().SkeletonOf(0x1ce)->actions)
        EXPECT_EQ(action.speed, 0.25f);
}

// 0x51481a: el 68 pide Monster50.bmd, que el Data del laboratorio no trae; el original cierra (Q-C23) y MuRet corta
// antes del alta, sin rand().
TEST_F(MonsterViewportTest, TheMissingMonsterFileCutsBeforeTheCreation)
{
    Load(0);
    const std::array<MonsterRecordBytes, 1> records{{{.key = 0x0700, .typeLow = 68, .x = 130, .y = 130}}};
    const auto applied = Monsters(records);
    ASSERT_FALSE(applied.HasValue());
    EXPECT_NE(applied.Error().find("0x51481a"), std::string::npos) << applied.Error();
    EXPECT_NE(applied.Error().find("Monster50.bmd"), std::string::npos) << applied.Error();
    EXPECT_FALSE(Actor(1).alive);
    EXPECT_EQ(game->cloth.random.State(), CrtRandom(Seed).State());
}

// 0x4c9790: el 85 pide el sonido 0x173; el 21 la acción 1 (0x4c97ff) con las acciones de Monster15.bmd, que su caso
// cargó en el modelo 0x160 (0x612d90(0xe), P-C176), y el sonido 0x13. El 0x35 y el 0x154 cortan en 0x4c982b y 0x4c9883.
TEST_F(MonsterViewportTest, TheNewArrivalsAskForTheirSoundOrAction)
{
    Load(0);
    const Cell cell = FreeRow();
    std::array<MonsterRecordBytes, 1> records{
        {{.key = 0x0800, .isNew = true, .typeLow = 85, .x = cell.x, .y = cell.y}}};
    auto applied = Monsters(records);
    ASSERT_TRUE(applied.HasValue()) << applied.Error();
    EXPECT_EQ(Actor(1).type, 0x180);
    EXPECT_EQ(Actor(1).hands[1].model, 0xa15);
    EXPECT_EQ(game->world.Runtime().lastSoundId, 0x173);
    EXPECT_EQ(Actor(1).alpha, 1.0f);
    EXPECT_EQ(game->cloth.random.State(), AfterDraws(Seed, ArrivingRecordDraws).State());

    records[0] = {.key = 0x0801, .isNew = true, .typeLow = 21, .x = cell.x, .y = cell.y};
    applied = Monsters(records);
    ASSERT_TRUE(applied.HasValue()) << applied.Error();
    EXPECT_EQ(Actor(2).type, 0x160);
    ASSERT_NE(game->world.FaunaResources().SkeletonOf(0x160), nullptr);
    EXPECT_EQ(Actor(2).animation.action, 1);
    EXPECT_EQ(Actor(2).animation.previousAction, 1);
    EXPECT_EQ(Actor(2).alpha, 1.0f);
    EXPECT_EQ(game->world.Runtime().lastSoundId, 0x13);
    // Cargar no saca rand(): el alta y las celdas, como el 85.
    EXPECT_EQ(game->cloth.random.State(), AfterDraws(Seed, 2 * ArrivingRecordDraws).State());

    ActorState arriving = Actor(1);
    arriving.word376 = 0x35;
    auto arrived = Client::Scenes::ArriveAsNew(arriving, game->Resources());
    ASSERT_FALSE(arrived.HasValue());
    EXPECT_NE(arrived.Error().find("0x4c982b"), std::string::npos) << arrived.Error();
    arriving.word376 = 0x154;
    arrived = Client::Scenes::ArriveAsNew(arriving, game->Resources());
    ASSERT_FALSE(arrived.HasValue());
    EXPECT_NE(arrived.Error().find("0x4c9883"), std::string::npos) << arrived.Error();
}

// 0x4c9ed2..0x4c9f44 y 0x4e0b96..0x4e0c10: en Castle Siege, la puerta (+0x376 en 0x115, el 0x19b con la caja de
// 0x42d7f0) con +0x78 en 0 corta en 0x4c9f0f (el 0x13) o en 0x4e0bd1 (el 0x68); con otro estado sigue.
TEST_F(MonsterViewportTest, TheCastleSiegeGateCutsWhenItIsOpenOrUnmarked)
{
    Load(30);
    const Cell cell = FreeRow();
    std::array<MonsterRecordBytes, 1> records{{{.key = 0x0900,
                                                .typeHigh = 1,
                                                .typeLow = 0x15,
                                                .x = cell.x,
                                                .y = cell.y,
                                                .targetX = cell.x,
                                                .targetY = cell.y}}};
    auto applied = Monsters(records);
    ASSERT_FALSE(applied.HasValue());
    EXPECT_NE(applied.Error().find("0x4c9f0f"), std::string::npos) << applied.Error();
    const ActorState& gate = Actor(1);
    EXPECT_EQ(gate.type, 0x19b);
    EXPECT_EQ(gate.boundsMin, (glm::vec3{-140.0f, -140.0f, 0.0f}));
    EXPECT_EQ(gate.boundsTop, 300.0f);
    EXPECT_EQ(gate.byte38f, 0);
    EXPECT_EQ(gate.byte92, 0);

    records[0].effects = 1;
    applied = Monsters(records);
    EXPECT_TRUE(applied.HasValue()) << applied.Error();

    const std::array<ObjectRecordBytes, 1> objects{
        {{.kind = 2, .classOrTypeHigh = 1, .typeLow = 0x15, .key = 0x0901, .x = cell.x, .y = cell.y}}};
    applied = Objects(objects);
    ASSERT_FALSE(applied.HasValue());
    EXPECT_NE(applied.Error().find("0x4e0bd1"), std::string::npos) << applied.Error();
}

// 0x4ca0b0 con 0x517400: el 0x98 y el 0x9e (nivel tipo − 0x97, el 7 con el subtipo 1), +0x88 en 1, el PK de la
// dirección y, con 6 o más, el nivel en 1. El nombre es la cadena vacía de 0x6e57a4, sin el dueño.
TEST_F(MonsterViewportTest, TheSummonsTakeTheirLevelFromTheType)
{
    Load(0);
    const Cell cell = FreeRow();
    const std::array<SummonRecordBytes, 2> records{{{.key = 0x0a00,
                                                     .type = 0x98,
                                                     .x = cell.x,
                                                     .y = cell.y,
                                                     .targetX = cell.x,
                                                     .targetY = cell.y,
                                                     .directionAndPk = 0x33,
                                                     .owner = "Carpincho"},
                                                    {.key = 0x0a01,
                                                     .type = 0x9e,
                                                     .x = cell.x,
                                                     .y = cell.y,
                                                     .targetX = cell.x,
                                                     .targetY = cell.y,
                                                     .directionAndPk = 0x16,
                                                     .owner = "Carpincho"}}};
    const auto applied = Summons(records);
    ASSERT_TRUE(applied.HasValue()) << applied.Error();

    const ActorState& first = Actor(1);
    EXPECT_EQ(first.type, 0xc5);
    EXPECT_EQ(first.word376, 0x98);
    EXPECT_EQ(first.level, 1);
    EXPECT_EQ(first.subtype, 0);
    EXPECT_EQ(first.byte88, 1);
    EXPECT_EQ(first.pk, 3);
    EXPECT_EQ(first.angles.z, 90.0f);
    EXPECT_EQ(first.dword394, 1);
    EXPECT_EQ(first.dword64, -1);
    EXPECT_EQ(first.hands[0].model, 0xffff);
    EXPECT_EQ(first.animation.frame, 10.0f);
    EXPECT_EQ(Client::Scenes::ActorText(first), "");

    const ActorState& last = Actor(2);
    EXPECT_EQ(last.word376, 0x9e);
    EXPECT_EQ(last.subtype, 1);
    EXPECT_EQ(last.pk, 6);
    EXPECT_EQ(last.level, 1);
    EXPECT_EQ(game->cloth.random.State(), AfterDraws(Seed, WalkingRecordDraws + WalkingRecordDraws).State());
}

// 0x51747c: el invocado que llega corta después del nombre, antes de las celdas.
TEST_F(MonsterViewportTest, ANewSummonCutsAfterItsName)
{
    Load(0);
    const std::array<SummonRecordBytes, 1> records{{{.key = 0x0b00, .isNew = true, .type = 0x9a, .x = 130, .y = 130}}};
    const auto applied = Summons(records);
    ASSERT_FALSE(applied.HasValue());
    EXPECT_NE(applied.Error().find("0x51747c"), std::string::npos) << applied.Error();
    EXPECT_TRUE(Actor(1).alive);
    EXPECT_EQ(Actor(1).level, 3);
    EXPECT_EQ(game->cloth.random.State(), AfterDraws(Seed, ConstructionDraws).State());
}

// 0x4ca516..0x4ca5bf: fuera de 0x98..0x9e, el nombre es el del dueño (diez bytes), el Text 485 y el del monstruo; sin
// Text.bmd corta en 0x4ca570. Los strcat siguen por +0x1f2 y la clave de +0x1f4 como el original (Q-C847). El recién
// llegado corta en 0x4ca5f6.
TEST_F(MonsterViewportTest, TheOtherSummonsTakeTheOwnersName)
{
    Load(0);
    const Cell cell = FreeRow();
    std::array<SummonRecordBytes, 1> records{{{.key = 0x0c00,
                                               .type = 3,
                                               .x = cell.x,
                                               .y = cell.y,
                                               .targetX = cell.x,
                                               .targetY = cell.y,
                                               .directionAndPk = 0x10,
                                               .owner = "Carpincho"}}};
    auto applied = Summons(records);
    ASSERT_FALSE(applied.HasValue());
    EXPECT_NE(applied.Error().find("0x4ca570"), std::string::npos) << applied.Error();

    const Client::Data::Text::TextTable texts = Client::Tests::MakeTextTable({{485, " con su "}});
    game->texts = &texts;
    applied = Summons(records);
    ASSERT_TRUE(applied.HasValue()) << applied.Error();
    EXPECT_EQ(Client::Scenes::ActorText(Actor(1)), "Carpincho con su \xb0\xc5\xb9\xcc");
    EXPECT_EQ(Actor(1).byte88, 1);

    // Diez letras, " con su " y las nueve del caso del 20 (0x515949): 27, con el '\0' en el byte bajo de la clave. El
    // WORD de +0x1f2 queda con las dos últimas.
    records[0].key = 0x0c02;
    records[0].owner = "Carpinchos";
    records[0].type = 20;
    applied = Summons(records);
    ASSERT_TRUE(applied.HasValue()) << applied.Error();
    const ActorState& spilled = Actor(2);
    EXPECT_EQ(Client::Scenes::ActorText(spilled), "Carpinchos con su \xbc\xb3\xc0\xce \xb4\xeb\xc0\xe5");
    EXPECT_EQ(spilled.word1f2, 0xe5c0);
    EXPECT_EQ(spilled.key, 0x0c00);

    records[0] = {.key = 0x0c01, .isNew = true, .type = 3, .x = cell.x, .y = cell.y, .owner = "Carpincho"};
    applied = Summons(records);
    ASSERT_FALSE(applied.HasValue());
    EXPECT_NE(applied.Error().find("0x4ca5f6"), std::string::npos) << applied.Error();
    EXPECT_EQ(Actor(3).alpha, 0.0f);
}

// 0x51743f..0x517466 con NpcName(Eng).txt cargado: la cola deja "Magic stone of %s " del 152 al 157 (0x98..0x9d) y
// "Shadow Knight" en el 158 (0x9e), y wsprintfA pone el dueño en el "%s". Los 24 bytes del local van a +0x1d9: con
// menos de 24 letras, con su '\0'; con más, los 24 primeros sin él, y el nombre sigue en +0x1f1. Los dueños son
// inventados.
TEST_F(MonsterViewportTest, TheMagicStonesFormatTheOwnersName)
{
    const auto path = DataFile("Local\\NpcName(Eng).txt");
    ASSERT_TRUE(path.has_value());
    const std::optional<std::vector<char>> file = Client::Tests::ReadFileBytes(*path);
    ASSERT_TRUE(file.has_value());
    const auto names = Client::Data::Text::LoadNpcNames(std::as_bytes(std::span(*file)));
    ASSERT_TRUE(names.HasValue());
    for (const std::uint8_t map : {std::uint8_t{0}, std::uint8_t{2}})
    {
        SCOPED_TRACE(map);
        Load(map);
        game->world.Runtime().npcNames = Client::Scenes::NpcNameTableOf(names.Value());
        const Cell cell = FreeRow();
        const auto summon = [&cell](std::uint16_t key, std::uint8_t type, std::string_view owner)
        {
            return SummonRecordBytes{.key = key,
                                     .type = type,
                                     .x = cell.x,
                                     .y = cell.y,
                                     .targetX = cell.x,
                                     .targetY = cell.y,
                                     .directionAndPk = 0x10,
                                     .owner = owner};
        };
        const std::array<SummonRecordBytes, 3> records{
            {summon(0x0e00, 0x98, "Yacare"), summon(0x0e01, 0x9d, "Carpinchos"), summon(0x0e02, 0x9e, "Yacare")}};
        const auto applied = Summons(records);
        ASSERT_TRUE(applied.HasValue()) << applied.Error();

        EXPECT_EQ(Actor(1).word376, 0x98);
        EXPECT_EQ(Client::Scenes::ActorText(Actor(1)), "Magic stone of Yacare ");
        // "Magic stone of Carpinchos " tiene 26: los 24 primeros, sin '\0'; +0x1f1 sigue en 0 y corta la cadena.
        EXPECT_EQ(Client::Scenes::ActorText(Actor(2)), "Magic stone of Carpincho");
        EXPECT_EQ(Actor(2).nameSpill[13], std::byte{0});
        EXPECT_EQ(Actor(2).word1f2, 0xffff);
        EXPECT_EQ(Actor(2).key, 0x0e01);
        EXPECT_EQ(Actor(2).level, 6);
        EXPECT_EQ(Client::Scenes::ActorText(Actor(3)), "Shadow Knight");
        EXPECT_EQ(Actor(3).subtype, 1);
    }
}

// 0x51743f: wsprintfA con otro especificador, o un segundo "%s", lee de la pila argumentos que 0x517400 no pasa, y un
// dueño de diez letras sin '\0' detrás de menos de 14 bytes sigue en el registro del 1F dentro de los 24 bytes: cortan
// (P-C356). "%%" da '%'. Los nombres de la tabla y los dueños son inventados.
TEST_F(MonsterViewportTest, TheSummonFormatsThatReadMoreCut)
{
    for (const std::uint8_t map : {std::uint8_t{0}, std::uint8_t{2}})
    {
        SCOPED_TRACE(map);
        Load(map);
        auto& names = game->world.Runtime().npcNames;
        const auto put = [&names](std::size_t at, std::uint16_t type, std::string_view name)
        {
            names[at].type = type;
            names[at].name = {};
            std::ranges::copy(name, names[at].name.begin());
        };
        put(0, 0x98, "Piedra 100%% de %s");
        put(1, 0x99, "Piedra %d");
        put(2, 0x9a, "%s y %s");
        put(3, 0x9b, "%s");
        put(4, 0x9c, "Piedra de mago de %s");
        const Cell cell = FreeRow();
        std::array<SummonRecordBytes, 1> records{{{.key = 0x0f00,
                                                   .type = 0x98,
                                                   .x = cell.x,
                                                   .y = cell.y,
                                                   .targetX = cell.x,
                                                   .targetY = cell.y,
                                                   .owner = "Yacare"}}};
        auto applied = Summons(records);
        ASSERT_TRUE(applied.HasValue()) << applied.Error();
        EXPECT_EQ(Client::Scenes::ActorText(Actor(1)), "Piedra 100% de Yacare");

        for (const std::uint16_t type : {std::uint16_t{0x99}, std::uint16_t{0x9a}})
        {
            records[0].type = type;
            applied = Summons(records);
            ASSERT_FALSE(applied.HasValue());
            EXPECT_NE(applied.Error().find("0x51743f"), std::string::npos) << applied.Error();
            EXPECT_NE(applied.Error().find("P-C356"), std::string::npos) << applied.Error();
        }

        // Sin '\0' en los diez bytes del dueño: detrás de "%s" solo, corta; detrás de 18 bytes, los 24 son conocidos.
        records[0].owner = "Carpinchos";
        records[0].type = 0x9b;
        applied = Summons(records);
        ASSERT_FALSE(applied.HasValue());
        EXPECT_NE(applied.Error().find("P-C356"), std::string::npos) << applied.Error();
        records[0].type = 0x9c;
        applied = Summons(records);
        ASSERT_TRUE(applied.HasValue()) << applied.Error();
        EXPECT_EQ(Client::Scenes::ActorText(Actor(1)), "Piedra de mago de Carpin");
    }
}

// 0x4e0980: con 1, un jugador (0x20a) con la clase de +1, +0x88 en 0x80, +0x428 en 0x96, el ángulo rand() % 360 y el
// nombre "   "; con 2, el monstruo del tipo de +1 y +2 con el mismo ángulo; con 0, nada.
TEST_F(MonsterViewportTest, TheObjectsArePlayersOrMonstersByTheirKind)
{
    Load(0);
    const Cell cell = FreeRow();
    const std::array<ObjectRecordBytes, 3> records{{
        {.kind = 0, .typeLow = 3, .key = 0x0d00, .x = cell.x, .y = cell.y},
        {.kind = 1, .classOrTypeHigh = 0x20, .key = 0x0d01, .x = cell.x, .y = cell.y},
        {.kind = 2, .typeLow = 3, .key = 0x0d02, .x = cell.x, .y = cell.y},
    }};
    const auto applied = Objects(records);
    ASSERT_TRUE(applied.HasValue()) << applied.Error();

    CrtRandom expected = AfterDraws(Seed, ArrivingRecordDraws);
    const ActorState& player = Actor(1);
    EXPECT_EQ(player.key, 0x0d01);
    EXPECT_EQ(player.type, PlayerSkeletonModel);
    EXPECT_EQ(player.characterClass, 1);
    EXPECT_EQ(player.byte88, 0x80);
    EXPECT_EQ(player.dword428, 0x96);
    EXPECT_EQ(player.angles.z, static_cast<float>(expected.Next() % 360));
    EXPECT_EQ(Client::Scenes::ActorText(player), "   ");
    EXPECT_EQ(player.byte1e3, 0);

    expected.Discard(ArrivingRecordDraws);
    const ActorState& monster = Actor(2);
    EXPECT_EQ(monster.key, 0x0d02);
    EXPECT_EQ(monster.type, 0x15b);
    EXPECT_EQ(monster.byte88, 2);
    EXPECT_EQ(monster.dword428, 0x96);
    EXPECT_EQ(monster.angles.z, static_cast<float>(expected.Next() % 360));
    EXPECT_FALSE(Actor(3).alive);
    EXPECT_EQ(game->cloth.random.State(), expected.State());
}
} // namespace
