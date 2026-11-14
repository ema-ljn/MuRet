#include "App/WindowState.h"
#include "Core/Expected.h"
#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Data/Models/PlayerModels.h"
#include "Data/Tables/ItemSetTables.h"
#include "Data/Tables/ItemTable.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Data/Terrain/TerrainHeights.h"
#include "Network/Crypto/Xor32.h"
#include "Protocol/FloorItems.h"
#include "Protocol/Inventory.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/LogicalAccess.h"
#include "Render/Models/RayPicking.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/TextStyle.h"
#include "Scenes/ActorState.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/FloorItemActions.h"
#include "Scenes/FloorItemNames.h"
#include "Scenes/FloorItemPackets.h"
#include "Scenes/GameDrawTail.h"
#include "Scenes/GameHover.h"
#include "Scenes/GameHud.h"
#include "Scenes/InventoryMoves.h"
#include "Scenes/LoginModels.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/OwnInventory.h"
#include "Scenes/ReturnToSelection.h"
#include "Scenes/SelectionState.h"
#include "Scenes/WorldEntry.h"
#include "Scenes/WorldMap.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/GameDialogs.h"
#include "World/GameCamera.h"
#include "World/ItemState.h"
#include "World/ModelEffects.h"
#include "World/TerrainPickingSegment.h"

#include "support/Bytes.h"
#include "support/ItemSetFiles.h"
#include "support/PlayerActors.h"
#include "support/RecordingSink.h"
#include "support/RecordingTextDrawer.h"
#include "support/SameBytes.h"
#include "support/SceneDataFiles.h"
#include "support/TableFiles.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Los ítems del piso (CL9 hito 5; 06 § Los ítems del piso): el 20, el 21, el 22, el 23 y el 2C que llegan, levantar
// (0x5650c0, la barra), la búsqueda del picking (0x59e2e0), el lugar en la grilla (0x5c9d20) y el nombre (0x5ba790).
// Item.bmd y Text.bmd son sintéticos, con nombres y textos inventados.
namespace
{
using Client::Core::Expected;
using Client::Network::Crypto::Xor32Decode;
using Client::Protocol::FloorItems::FloorItemEntry;
using Client::Protocol::FloorItems::FloorItemsCreate;
using Client::Protocol::Inventory::ItemData;
using Client::Render::Models::CrtRandom;
using Client::Scenes::FloorItemState;
using Client::Scenes::WorldEntryResources;
using Client::Scenes::WorldState;
using Client::Tests::Bytes;
using Client::Tests::SameBytes;
namespace Scenes = Client::Scenes;

constexpr std::uint32_t Seed = 0x2468aceu;
constexpr int Own = Client::Render::Models::OwnRecordCloseRandoms;
constexpr int SendRandoms = 2;
constexpr std::size_t HeroSlot = 42;
// La altura de todo el terreno sintético.
constexpr float Ground = 170.0f;

constexpr std::uint16_t Sword = 0x0001;
constexpr std::uint16_t Potion = 0x1c00;
constexpr std::uint16_t Zen = 0x1c0f;
constexpr std::uint16_t Jewel = 0x1c0d;
constexpr std::uint16_t BoxOfLuck = 0x1c0b;
constexpr std::uint16_t FirstModel = 0x212;

struct NamedItem
{
    std::uint16_t type = 0;
    std::string_view name;
    std::uint8_t width = 1;
    std::uint8_t height = 1;
};

// Item.bmd sintético: el nombre (+0), el ancho (+0x24) y el alto (+0x25) de cada tipo.
Client::Data::Tables::ItemTable NamedItems(std::initializer_list<NamedItem> named)
{
    using Client::Data::Tables::ItemRecordSize;
    std::vector<std::byte> plain(Client::Data::Tables::ItemTableSize);
    for (const NamedItem& item : named)
    {
        const std::size_t base = std::size_t{item.type} * ItemRecordSize;
        for (std::size_t i = 0; i < item.name.size(); ++i)
            plain[base + i] = static_cast<std::byte>(item.name[i]);
        plain[base + 0x24] = std::byte{item.width};
        plain[base + 0x25] = std::byte{item.height};
    }
    const std::vector<std::byte> encrypted = Client::Tests::EncryptRecords(std::move(plain), ItemRecordSize);
    return Client::Data::Tables::ItemTable(std::span<const std::byte>(encrypted));
}

// Los siete bytes de un ítem (0x54b3a0 al revés).
ItemData ItemBytes(std::uint16_t type, std::uint8_t byte1 = 0, std::uint8_t durability = 20, std::uint8_t byte3 = 0,
                   std::uint8_t byte4 = 0)
{
    return {static_cast<std::byte>(type & 0xff),
            std::byte{byte1},
            std::byte{durability},
            static_cast<std::byte>(byte3 | ((type >> 1) & 0x80)),
            std::byte{byte4},
            static_cast<std::byte>((type >> 5) & 0xf0),
            std::byte{0}};
}

Client::Data::Tables::ItemSetTables NoSets()
{
    return Client::Tests::ItemSetTablesFrom(
        std::vector<std::byte>(Client::Data::Tables::ItemSetTypeTableSize, std::byte{0xff}),
        std::vector<std::byte>(Client::Data::Tables::ItemSetOptionTableSize, std::byte{0xff}));
}

CrtRandom Advanced(CrtRandom from, std::size_t count)
{
    from.Discard(static_cast<std::uint32_t>(count));
    return from;
}

// Lo que dura una prueba: el mundo, el terreno plano en `Ground`, Player.bmd para 0x512a30, los textos y el medidor
// de los mensajes. En el heap, por el tamaño de WorldState.
struct Stage
{
    explicit Stage(std::int32_t map)
        : selection(world), cloth{.random = CrtRandom(Seed)}, text(sprites),
          texts(Client::Tests::MakeTextTable({{224, "zen"},       {375, "no hay lugar"}, {918, "levantado"},
                                              {100, "caja A"},    {101, "caja B"},       {104, "caja C"},
                                              {106, "caja dos"},  {117, "caja trece"},   {115, "caja de la suerte"},
                                              {166, "fuerza"},    {167, "agilidad"},     {168, "energia"},
                                              {169, "vitalidad"}, {176, " +habilidad"},  {177, " +opcion"},
                                              {178, " +suerte"},  {378, "no se puede"},  {379, "%s %d %s"},
                                              {1412, "sumados"},  {1900, "comando"},     {1903, "restados"}}))
    {
        sprites.Begin(Client::App::PixelSize{640, 480});
        models.Set(Client::Data::Models::PlayerSkeletonModel, {}, Client::Tests::PlayerActionsSkeleton(), {});
        world.OwnCharacter().ClearEquipmentForEntry();
        world.Inventory().ClearForInventoryReply();
        Scenes::WorldRuntime& runtime = world.Runtime();
        runtime.map = map;
        runtime.hero = HeroSlot;
        Scenes::ActorState& hero = world.Actors()[HeroSlot];
        hero = Client::Tests::UnarmedPlayer();
        hero.alive = true;
        auto& terrain = world.Map().terrain;
        terrain.attributes = std::make_unique<Client::Data::Terrain::TerrainAttributes>();
        terrain.heights = std::make_unique<Client::Data::Terrain::TerrainHeights>();
        terrain.heights->cells.fill(Ground);
    }

    [[nodiscard]] WorldEntryResources Resources()
    {
        return WorldEntryResources{.world = world,
                                   .selection = selection,
                                   .dataRoot = {},
                                   .cloth = cloth,
                                   .camera = camera,
                                   .cameraInput = cameraInput,
                                   .models = models,
                                   .items = &items,
                                   .itemSetTables = &sets,
                                   .log = log,
                                   .textMeasure = &text,
                                   .screen = [] { return Client::App::PixelSize{640, 480}; },
                                   .texts = &texts};
    }

    [[nodiscard]] Scenes::InventoryMoveContext Moves()
    {
        return Scenes::InventoryMoveContext{.world = world, .random = cloth.random, .services = services};
    }

    FloorItemState& Item(std::size_t index)
    {
        return world.DrawTail().floorItems[index];
    }

    WorldState world;
    Scenes::SelectionState selection;
    Client::World::GameCamera camera;
    Client::World::GameCameraInput cameraInput;
    Client::Render::Models::ClothWorld cloth;
    Scenes::SingleModelLibrary models;
    Client::Data::Tables::ItemTable items = NamedItems({{Sword, "Espada inventada", 1, 3},
                                                        {Potion, "Pocion inventada"},
                                                        {Zen, "Oro"},
                                                        {Jewel, "Joya inventada"},
                                                        {BoxOfLuck, "Caja inventada"}});
    Client::Data::Tables::ItemSetTables sets = NoSets();
    Client::Tests::RecordingSink sink;
    Client::Core::Log log{sink, Client::Core::LogLevel::Debug};
    Client::Render::SpriteBatch sprites;
    Client::Tests::RecordingTextDrawer text;
    Client::Data::Text::TextTable texts;
    Scenes::InventoryServices services;
};

std::unique_ptr<Stage> StageIn(std::int32_t map)
{
    return std::make_unique<Stage>(map);
}

FloorItemsCreate ThreeItems()
{
    return FloorItemsCreate{
        .items = {
            // El zen recién caído en la 5: 0x01 0x02 en b1 y b2, 0x05 en b4.
            FloorItemEntry{.key = 0x8005, .x = 130, .y = 120, .item = ItemBytes(Zen, 1, 2, 0, 5)},
            // Una poción en el piso en la 7.
            FloorItemEntry{.key = 7, .x = 10, .y = 20, .item = ItemBytes(Potion, 0, 1)},
            // Una espada +3 con opciones en la clave 1005, que va a la 0.
            FloorItemEntry{.key = 1005, .x = 1, .y = 2, .item = ItemBytes(Sword, 3 << 3, 30, 0x01, 0x05)},
        }};
}

// 0x4d0ba0 → 0x608350: cada entrada en (celda + 0,5) · 100, el tipo en +4, el modelo, el valor y las opciones; el
// recién caído 180 por encima del terreno con la velocidad 20 y el sonido del zen; los demás en el terreno. Los rand()
// son los de 0x607b60 de cada modelo. Igual en Lorencia, Devias e Icarus (el 10, que solo cambia el dibujo de
// 0x608a00).
TEST(FloorItemsSceneTest, TheCreateFillsEachEntryInEveryMap)
{
    for (const std::int32_t map : {0, 2, 10})
    {
        auto stage = StageIn(map);
        const CrtRandom expected = Advanced(stage->cloth.random, Scenes::HangingRandomCount(Zen + FirstModel) +
                                                                     Scenes::HangingRandomCount(Potion + FirstModel) +
                                                                     Scenes::HangingRandomCount(Sword + FirstModel));
        const auto created = Scenes::ApplyFloorItemsCreate(ThreeItems(), stage->Resources());
        ASSERT_TRUE(created.HasValue()) << created.Error();
        EXPECT_EQ(stage->cloth.random.State(), expected.State()) << map;

        const FloorItemState& zen = stage->Item(5);
        EXPECT_TRUE(zen.alive);
        EXPECT_EQ(zen.itemType, static_cast<std::int16_t>(Zen));
        EXPECT_EQ(zen.type, static_cast<std::int16_t>(Zen + FirstModel));
        EXPECT_EQ(zen.value, 0x10205);
        EXPECT_FLOAT_EQ(zen.position.x, 13050.0f);
        EXPECT_FLOAT_EQ(zen.position.y, 12050.0f);
        EXPECT_FLOAT_EQ(zen.position.z, Ground + 180.0f);
        EXPECT_FLOAT_EQ(zen.dropSpeed, 20.0f);

        const FloorItemState& potion = stage->Item(7);
        EXPECT_TRUE(potion.alive);
        EXPECT_EQ(potion.type, static_cast<std::int16_t>(Potion + FirstModel));
        EXPECT_FLOAT_EQ(potion.position.x, 1050.0f);
        EXPECT_FLOAT_EQ(potion.position.z, Ground);
        EXPECT_FLOAT_EQ(potion.dropSpeed, 0.0f);

        const FloorItemState& sword = stage->Item(0);
        EXPECT_TRUE(sword.alive);
        EXPECT_EQ(sword.itemType, static_cast<std::int16_t>(Sword));
        EXPECT_EQ(sword.value, 3 << 3);
        EXPECT_EQ(sword.durability, 30);
        EXPECT_EQ(sword.excellent, 0x01);
        EXPECT_EQ(sword.ancient, 0x05);
        EXPECT_EQ(sword.boundingMin, glm::vec3(-30.0f));
        EXPECT_EQ(sword.boundingMax, glm::vec3(30.0f));
        // 0x6083a5: solo el recién caído suena (CL10).
        EXPECT_EQ(stage->world.Runtime().lastSoundId, 0x1f);
    }
}

// 0x608614..0x608722: la medalla de plata (el 0x1c0b de nivel 5, el modelo 0x2242) y la de oro (nivel 6, 0x2243)
// recién caídas crean la grieta 0x100 (0x6086ce) y la 0xfe (0x6086f1) con 0x518cf0: a 3 sobre el terreno en la
// posición pedida, ángulos en 0, luz 1, subtipo 0, la medalla de dueño y el tamaño 0x37 (vida 35, escala 0,55f), sin
// rand(); la medalla queda a 3 con la velocidad 50. La que ya estaba en el piso, en el terreno y sin grietas. Igual en
// Lorencia, Devias (el 2, donde el GS 0.97 suelta la de plata) y Lost Tower (el 4, la de oro).
TEST(FloorItemsSceneTest, TheDroppedMedalsCrackTheGroundInEveryMap)
{
    constexpr std::int16_t SilverMedal = 0x2242;
    constexpr std::int16_t GoldMedal = 0x2243;
    const float scale = static_cast<float>(55.0 * static_cast<double>(0.01f));
    for (const std::int32_t map : {0, 2, 4})
    {
        SCOPED_TRACE(map);
        auto stage = StageIn(map);
        const CrtRandom expected = Advanced(stage->cloth.random, (2 * Scenes::HangingRandomCount(SilverMedal)) +
                                                                     Scenes::HangingRandomCount(GoldMedal));
        const FloorItemsCreate create{
            .items = {FloorItemEntry{.key = 0x8003, .x = 30, .y = 40, .item = ItemBytes(BoxOfLuck, 5 << 3, 1)},
                      FloorItemEntry{.key = 0x8004, .x = 50, .y = 60, .item = ItemBytes(BoxOfLuck, 6 << 3, 1)},
                      FloorItemEntry{.key = 6, .x = 70, .y = 80, .item = ItemBytes(BoxOfLuck, 5 << 3, 1)}}};
        const auto created = Scenes::ApplyFloorItemsCreate(create, stage->Resources());
        ASSERT_TRUE(created.HasValue()) << created.Error();
        EXPECT_EQ(stage->cloth.random.State(), expected.State());

        const FloorItemState& silver = stage->Item(3);
        const FloorItemState& gold = stage->Item(4);
        const FloorItemState& lying = stage->Item(6);
        EXPECT_EQ(silver.type, SilverMedal);
        EXPECT_EQ(gold.type, GoldMedal);
        EXPECT_EQ(lying.type, SilverMedal);
        EXPECT_FLOAT_EQ(silver.position.z, Ground + 3.0f);
        EXPECT_FLOAT_EQ(silver.dropSpeed, 50.0f);
        EXPECT_FLOAT_EQ(gold.position.z, Ground + 3.0f);
        EXPECT_FLOAT_EQ(gold.dropSpeed, 50.0f);
        EXPECT_FLOAT_EQ(lying.position.z, Ground);
        EXPECT_FLOAT_EQ(lying.dropSpeed, 0.0f);

        struct Crack
        {
            std::int16_t type = 0;
            const FloorItemState* owner = nullptr;
        };
        const std::array<Crack, 4> cracks{Crack{0x100, &silver}, Crack{0xfe, &silver}, Crack{0x100, &gold},
                                          Crack{0xfe, &gold}};
        const auto effects = stage->world.Map().effects.Effects();
        for (std::size_t slot = 0; slot < cracks.size(); ++slot)
        {
            SCOPED_TRACE(slot);
            const Client::World::ModelEffect& effect = effects[slot];
            const FloorItemState& owner = *cracks[slot].owner;
            EXPECT_TRUE(effect.alive);
            EXPECT_EQ(effect.type, cracks[slot].type);
            EXPECT_EQ(effect.subtype, 0);
            EXPECT_EQ(effect.life, 35);
            EXPECT_FLOAT_EQ(effect.transform.scale, scale);
            EXPECT_EQ(effect.disturbanceState, 0x37u);
            EXPECT_FLOAT_EQ(effect.transform.position.x, owner.position.x);
            EXPECT_FLOAT_EQ(effect.transform.position.y, owner.position.y);
            EXPECT_FLOAT_EQ(effect.transform.position.z, Ground + 3.0f);
            EXPECT_EQ(effect.transform.angles, glm::vec3(0.0f));
            EXPECT_EQ(effect.light, glm::vec3(1.0f));
            EXPECT_TRUE(effect.hasOwner);
            EXPECT_EQ(Client::World::OwnerTypeOf(effect.owner),
                      std::optional<std::uint16_t>(static_cast<std::uint16_t>(owner.type)));
            EXPECT_EQ(effect.owner.angles, &owner.angles);
        }
        for (std::size_t slot = cracks.size(); slot < effects.size(); ++slot)
            EXPECT_FALSE(effects[slot].alive) << slot;
    }
}

// Con un solo lugar libre en la tabla de efectos, 0x518cf0 crea la 0x100 y no la 0xfe; con la tabla llena, ninguna.
// La medalla sigue igual, a 3 con la velocidad 50, y no hay rand() de más.
TEST(FloorItemsSceneTest, WithTheEffectsFullTheDroppedMedalStillFalls)
{
    constexpr std::int16_t GoldMedal = 0x2243;
    constexpr std::int16_t Busy = 0x5ac;
    for (const std::size_t free : {std::size_t{1}, std::size_t{0}})
    {
        SCOPED_TRACE(free);
        auto stage = StageIn(2);
        const auto effects = stage->world.Map().effects.Effects();
        for (std::size_t slot = 0; slot + free < effects.size(); ++slot)
        {
            effects[slot].alive = true;
            effects[slot].type = Busy;
        }
        const CrtRandom expected = Advanced(stage->cloth.random, Scenes::HangingRandomCount(GoldMedal));
        const FloorItemsCreate create{
            .items = {FloorItemEntry{.key = 0x8009, .x = 12, .y = 34, .item = ItemBytes(BoxOfLuck, 6 << 3, 1)}}};
        const auto created = Scenes::ApplyFloorItemsCreate(create, stage->Resources());
        ASSERT_TRUE(created.HasValue()) << created.Error();
        EXPECT_EQ(stage->cloth.random.State(), expected.State());
        const FloorItemState& gold = stage->Item(9);
        EXPECT_EQ(gold.type, GoldMedal);
        EXPECT_FLOAT_EQ(gold.position.z, Ground + 3.0f);
        EXPECT_FLOAT_EQ(gold.dropSpeed, 50.0f);
        const Client::World::ModelEffect& last = effects.back();
        EXPECT_TRUE(last.alive);
        EXPECT_EQ(last.type, free == 1 ? std::int16_t{0x100} : Busy);
        for (std::size_t slot = 0; slot + 1 < effects.size(); ++slot)
            EXPECT_EQ(effects[slot].type, Busy) << slot;
    }
}

// 0x4e3849: cada clave apaga su entrada, sin la máscara del 20: las de 1000 o más, la 0.
TEST(FloorItemsSceneTest, TheDeleteTurnsOffEachKey)
{
    auto stage = StageIn(0);
    ASSERT_TRUE(Scenes::ApplyFloorItemsCreate(ThreeItems(), stage->Resources()).HasValue());
    Scenes::ApplyFloorItemsDelete(Client::Protocol::FloorItems::FloorItemsDelete{.keys = {5, 1500}}, stage->world);
    EXPECT_FALSE(stage->Item(5).alive);
    EXPECT_FALSE(stage->Item(0).alive);
    EXPECT_TRUE(stage->Item(7).alive);
}

// 0x59e2e0: sin rand(); los vivos y a la vista con la luz 0,2 del pase de vértices, y el primero que toca el rayo con
// 1,5 y sin el pase. Los que no están a la vista no se tocan.
TEST(FloorItemsSceneTest, ThePickingSearchLightsTheItemsAndTakesTheFirstHit)
{
    auto items = std::make_unique<std::array<FloorItemState, 1000>>();
    const auto put = [&](std::size_t index, bool seen, float x)
    {
        FloorItemState& item = (*items)[index];
        item.alive = true;
        item.seen = seen;
        item.pickBox =
            Client::Render::Models::Box{.min = {x - 30.0f, -30.0f, -30.0f}, .max = {x + 30.0f, 30.0f, 30.0f}};
    };
    put(3, false, 0.0f);
    put(4, true, 500.0f);
    put(8, true, 0.0f);
    put(9, true, 0.0f);
    const Client::World::TerrainPickingSegment ray{.origin = {0.0f, 0.0f, 1000.0f}, .end = {0.0f, 0.0f, -1000.0f}};

    EXPECT_EQ(Scenes::PickTheFloorItem(*items, ray), 8);
    EXPECT_FALSE((*items)[8].vertexLight);
    EXPECT_EQ((*items)[8].light, glm::vec3(1.5f));
    EXPECT_TRUE((*items)[9].vertexLight);
    EXPECT_EQ((*items)[9].light, glm::vec3(0.2f));
    EXPECT_TRUE((*items)[4].vertexLight);
    EXPECT_FALSE((*items)[3].vertexLight);

    const Client::World::TerrainPickingSegment away{.origin = {2000.0f, 0.0f, 1000.0f},
                                                    .end = {2000.0f, 0.0f, -1000.0f}};
    EXPECT_EQ(Scenes::PickTheFloorItem(*items, away), -1);
}

// 0x5c9d20: entra si hay algún lugar con todas sus casillas vacías.
TEST(FloorItemsSceneTest, TheRoomInTheGridLooksForEmptyCells)
{
    auto stage = StageIn(0);
    auto& cells = stage->world.Inventory().cells;
    for (auto& cell : cells)
        cell.id = Potion;
    constexpr auto PotionType = static_cast<std::int16_t>(Potion);
    constexpr auto SwordType = static_cast<std::int16_t>(Sword);
    EXPECT_FALSE(Scenes::HasRoomFor(&stage->items, cells, PotionType).Value());
    cells[63].id = Scenes::EmptyOwnItemId;
    EXPECT_TRUE(Scenes::HasRoomFor(&stage->items, cells, PotionType).Value());
    EXPECT_FALSE(Scenes::HasRoomFor(&stage->items, cells, SwordType).Value());
    cells[47].id = Scenes::EmptyOwnItemId;
    cells[55].id = Scenes::EmptyOwnItemId;
    EXPECT_TRUE(Scenes::HasRoomFor(&stage->items, cells, SwordType).Value());
}

// El caso 1 de 0x5650c0 con el héroe a menos de 150 del centro de la celda del destino: la espera del mouse en 6, el
// pedido en 0x6c82e8 y el C1 05 22 con el índice, en C3. Con el pedido puesto, no pide otro. Lejos, nada.
TEST(FloorItemsSceneTest, ArrivingPicksUpTheChosenItemByteByByte)
{
    for (const std::int32_t map : {0, 2, 10})
    {
        auto stage = StageIn(map);
        ASSERT_TRUE(Scenes::ApplyFloorItemsCreate(ThreeItems(), stage->Resources()).HasValue());
        std::vector<std::uint16_t> sent;
        stage->services.sendPickUp = [&sent](std::uint16_t index)
        {
            sent.push_back(index);
            return Expected<void, std::string>{};
        };
        stage->services.items = &stage->items;
        Scenes::WorldRuntime& runtime = stage->world.Runtime();
        runtime.floor.chosen = 7;
        runtime.movement.targetX = 10;
        runtime.movement.targetY = 20;

        ASSERT_TRUE(Scenes::PickUpTheFloorItem(stage->Moves(), {1300.0f, 2050.0f, 0.0f}, 1200.0f).HasValue());
        EXPECT_TRUE(sent.empty());

        const CrtRandom expected = Advanced(stage->cloth.random, SendRandoms);
        ASSERT_TRUE(Scenes::PickUpTheFloorItem(stage->Moves(), {1100.0f, 2100.0f, 0.0f}, 1200.0f).HasValue());
        EXPECT_EQ(stage->cloth.random.State(), expected.State());
        ASSERT_EQ(sent.size(), 1u);
        const auto packet = Client::Protocol::FloorItems::WritePickUp(sent.front());
        EXPECT_TRUE(SameBytes(Xor32Decode(packet.packet).Bytes(), Bytes({0xc1, 0x05, 0x22, 0x00, 0x07})));
        EXPECT_EQ(runtime.floor.pendingPickUp, 7);
        EXPECT_EQ(runtime.movement.mouseUpdateTimeMax, 6);

        runtime.floor.chosen = 5;
        ASSERT_TRUE(Scenes::PickUpTheFloorItem(stage->Moves(), {1100.0f, 2100.0f, 0.0f}, 1200.0f).HasValue());
        EXPECT_EQ(sent.size(), 1u);
    }
}

// 0x566e0a..0x566e76: sin lugar en la grilla, el aviso 375 del tipo 1, el ítem 3 sobre el terreno y su caída en 50.
TEST(FloorItemsSceneTest, WithoutRoomTheItemJumpsAndWarns)
{
    auto stage = StageIn(2);
    ASSERT_TRUE(Scenes::ApplyFloorItemsCreate(ThreeItems(), stage->Resources()).HasValue());
    for (auto& cell : stage->world.Inventory().cells)
        cell.id = Potion;
    std::vector<std::string> lines;
    stage->services.items = &stage->items;
    stage->services.texts = &stage->texts;
    stage->services.chatLine = [&lines](std::string_view line, std::int32_t type)
    {
        EXPECT_EQ(type, 1);
        lines.emplace_back(line);
        return Expected<void, std::string>{};
    };
    Scenes::WorldRuntime& runtime = stage->world.Runtime();
    runtime.floor.chosen = 0;
    runtime.floor.fromSpace = 1;
    const auto before = stage->cloth.random.State();
    ASSERT_TRUE(Scenes::PickUpTheFloorItem(stage->Moves(), {}, 1200.0f).HasValue());
    EXPECT_EQ(stage->cloth.random.State(), before);
    EXPECT_EQ(lines, std::vector<std::string>{"no hay lugar"});
    EXPECT_FLOAT_EQ(stage->Item(0).position.z, Ground + 3.0f);
    EXPECT_FLOAT_EQ(stage->Item(0).dropSpeed, 50.0f);
    EXPECT_EQ(runtime.floor.pendingPickUp, -1);
}

// 0x5ac928..0x5ac9c2: la barra elige el primer ítem vivo y a la vista a menos de 300 del héroe y lo pide sin mirar el
// destino; después +0x379 del héroe y 0x77f3b40 vuelven a 0.
TEST(FloorItemsSceneTest, TheSpaceBarPicksUpTheNearestSeenItem)
{
    auto stage = StageIn(0);
    ASSERT_TRUE(Scenes::ApplyFloorItemsCreate(ThreeItems(), stage->Resources()).HasValue());
    std::vector<std::uint16_t> sent;
    stage->services.sendPickUp = [&sent](std::uint16_t index)
    {
        sent.push_back(index);
        return Expected<void, std::string>{};
    };
    stage->services.items = &stage->items;
    Scenes::ActorState& hero = stage->world.Actors()[HeroSlot];
    hero.position = {1050.0f, 2050.0f + 299.0f, Ground};
    stage->Item(7).seen = false;
    ASSERT_TRUE(Scenes::PickUpTheNearestFloorItem(stage->Moves(), 1200.0f).HasValue());
    EXPECT_TRUE(sent.empty());

    stage->Item(7).seen = true;
    ASSERT_TRUE(Scenes::PickUpTheNearestFloorItem(stage->Moves(), 1200.0f).HasValue());
    EXPECT_EQ(sent, std::vector<std::uint16_t>{7});
    EXPECT_EQ(stage->world.Runtime().floor.chosen, 7);
    EXPECT_EQ(stage->world.Runtime().floor.fromSpace, 0);
    EXPECT_EQ(hero.movementType, 0);

    stage->world.Runtime().floor.pendingPickUp = -1;
    hero.position.y = 2050.0f + 300.0f;
    ASSERT_TRUE(Scenes::PickUpTheNearestFloorItem(stage->Moves(), 1200.0f).HasValue());
    EXPECT_EQ(sent.size(), 1u);
}

// 0x4d0cc0: con 0xfe, el zen en el registro propio (su cierre) y, con el elegido vivo, "%d %s %s"; el sonido por los
// bytes del paquete y el pedido en −1. Con 0xff, solo el pedido. Con 0xfd, el nombre del elegido (0x5b1c70).
TEST(FloorItemsSceneTest, ThePickUpResultTakesTheZenOrNamesTheItem)
{
    auto stage = StageIn(0);
    ASSERT_TRUE(Scenes::ApplyFloorItemsCreate(ThreeItems(), stage->Resources()).HasValue());
    Scenes::WorldRuntime& runtime = stage->world.Runtime();
    runtime.floor.pendingPickUp = 5;
    runtime.floor.chosen = 5;
    const CrtRandom expected = Advanced(stage->cloth.random, Own);
    ASSERT_TRUE(Scenes::ApplyPickUpResult({.result = 0xfe, .zen = 123456}, stage->Resources()).HasValue());
    EXPECT_EQ(stage->cloth.random.State(), expected.State());
    EXPECT_EQ(stage->world.OwnCharacter().zen, 123456u);
    ASSERT_EQ(runtime.chat.log.entries.size(), 1u);
    EXPECT_EQ(runtime.chat.log.entries[0].text, "66053 zen levantado");
    EXPECT_EQ(runtime.lastSoundId, 0x1d);
    EXPECT_EQ(runtime.floor.pendingPickUp, -1);

    runtime.floor.pendingPickUp = 5;
    ASSERT_TRUE(Scenes::ApplyPickUpResult({.result = 0xff}, stage->Resources()).HasValue());
    EXPECT_EQ(runtime.floor.pendingPickUp, -1);
    EXPECT_EQ(runtime.chat.log.entries.size(), 1u);

    runtime.floor.chosen = 0;
    ASSERT_TRUE(Scenes::ApplyPickUpResult({.result = 0xfd, .item = ItemBytes(Jewel)}, stage->Resources()).HasValue());
    ASSERT_EQ(runtime.chat.log.entries.size(), 2u);
    // El registro guarda el último primero.
    EXPECT_EQ(runtime.chat.log.entries[0].text, "Espada inventada +3 levantado");
    EXPECT_EQ(runtime.lastSoundId, 0x45);
}

// 0x4d10d0: rechazado, el ítem del cursor vuelve (0x5be150); tirado desde la grilla, sale de ella. Los dos dejan
// [0x6c82ec] en −1.
TEST(FloorItemsSceneTest, TheDropResultReturnsOrRemovesTheCursorItem)
{
    auto stage = StageIn(0);
    stage->services.items = &stage->items;
    stage->services.dressTheHero = [] { return Expected<void, std::string>{}; };
    auto& cell = stage->world.Inventory().cells[10];
    cell.id = Potion;
    cell.gridColumn = 2;
    cell.gridRow = 1;
    cell.occupied = 1;
    stage->world.Runtime().movement.pendingClick = true;
    ASSERT_TRUE(Scenes::PickUpFromTheGrid(stage->Moves(), 10).HasValue());
    stage->world.Runtime().floor.pendingDrop = 22;

    const auto rejected = Scenes::ApplyDropResult({.result = 0, .slot = 22}, stage->Resources());
    ASSERT_TRUE(rejected.HasValue()) << rejected.Error();
    EXPECT_EQ(stage->world.Runtime().cursorItemCount, 0);
    EXPECT_EQ(cell.id, Potion);
    EXPECT_EQ(stage->world.Runtime().floor.pendingDrop, -1);

    stage->world.Runtime().movement.pendingClick = true;
    ASSERT_TRUE(Scenes::PickUpFromTheGrid(stage->Moves(), 10).HasValue());
    const auto dropped = Scenes::ApplyDropResult({.result = 1, .slot = 22}, stage->Resources());
    ASSERT_TRUE(dropped.HasValue()) << dropped.Error();
    EXPECT_EQ(stage->world.Runtime().cursorItemCount, 0);
    EXPECT_EQ(stage->world.Runtime().cursorItem.occupied, 0u);
    EXPECT_EQ(cell.id, Scenes::EmptyOwnItemId);
}

// 0x4de500: con 0, el stat y +0x44 suben dentro del registro propio, con su aviso; con 3, el stat baja y +0x68 y +0x48
// suben; con 1, el aviso 378. Siempre 0x5767954 en 0.
TEST(FloorItemsSceneTest, TheStatFruitChangesTheStatAndWarns)
{
    auto stage = StageIn(0);
    Scenes::OwnCharacterRecord& own = stage->world.OwnCharacter();
    own.vitality = 40;
    stage->world.Runtime().ui.field5767954 = 10;
    const CrtRandom expected = Advanced(stage->cloth.random, Own);
    ASSERT_TRUE(
        Scenes::ApplyStatFruit({.result = 0, .points = 3, .stat = 1}, stage->world, stage->cloth.random, &stage->texts)
            .HasValue());
    EXPECT_EQ(stage->cloth.random.State(), expected.State());
    EXPECT_EQ(own.vitality, 43);
    EXPECT_EQ(own.addedPoints, 3);
    EXPECT_EQ(Client::UI::DialogRow(stage->world.Runtime().dialogBox, 0), "vitalidad 3 sumados");
    EXPECT_EQ(stage->world.Runtime().ui.field5767954, 0);

    ASSERT_TRUE(
        Scenes::ApplyStatFruit({.result = 3, .points = 2, .stat = 3}, stage->world, stage->cloth.random, &stage->texts)
            .HasValue());
    EXPECT_EQ(own.levelUpPoints, 2);

    stage->world.Runtime().ui.field5767954 = 10;
    ASSERT_TRUE(Scenes::ApplyStatFruit({.result = 1}, stage->world, stage->cloth.random, &stage->texts).HasValue());
    EXPECT_EQ(stage->world.Runtime().ui.field5767954, 0);
}

// 07 Q-C1240: de un 22, un 23 o un 2C cortos, solo lo que el original escribe en todos sus caminos. 0x4d10b9 suelta el
// pedido; 0x4d13d1 y 0x4d13e5, el lugar tirado, y con pkt[3] distinto de 0, 0x4d13c7 vacía el cursor; 0x4deb0e y las
// otras salidas de 0x4de500, la espera del uso. Sin ellos, no se vuelve a levantar ni a usar nada.
TEST(FloorItemsSceneTest, TheShortRepliesReleaseOnlyTheirWaits)
{
    auto stage = StageIn(0);
    Scenes::WorldRuntime& runtime = stage->world.Runtime();
    runtime.floor.pendingPickUp = 5;
    runtime.ui.field5767954 = 10;
    Scenes::ApplyShortPickUpResult(stage->world);
    EXPECT_EQ(runtime.floor.pendingPickUp, -1);
    EXPECT_EQ(runtime.ui.field5767954, 10);
    Scenes::ApplyShortStatFruit(stage->world);
    EXPECT_EQ(runtime.ui.field5767954, 0);

    for (const std::optional<std::uint8_t> result :
         {std::optional<std::uint8_t>{}, std::optional<std::uint8_t>{std::uint8_t{0}}})
    {
        runtime.cursorItemCount = 1;
        runtime.cursorItem.occupied = 1;
        runtime.floor.pendingDrop = 22;
        Scenes::ApplyShortDropResult({.result = result}, stage->world);
        EXPECT_EQ(runtime.floor.pendingDrop, -1);
        EXPECT_EQ(runtime.cursorItemCount, 1);
        EXPECT_EQ(runtime.cursorItem.occupied, 1u);
    }
    runtime.floor.pendingDrop = 22;
    Scenes::ApplyShortDropResult({.result = std::uint8_t{1}}, stage->world);
    EXPECT_EQ(runtime.floor.pendingDrop, -1);
    EXPECT_EQ(runtime.cursorItemCount, 0);
    EXPECT_EQ(runtime.cursorItem.occupied, 0u);
}

// 0x4c24cf: la vuelta a la selección suelta el pedido del C1 22 y 0x4c24c9 la espera del uso.
TEST(FloorItemsSceneTest, TheReturnToSelectionClearsThePickUpRequest)
{
    auto stage = StageIn(0);
    stage->world.Runtime().floor.pendingPickUp = 7;
    stage->world.Runtime().ui.field5767954 = 10;
    Scenes::ApplyReturnToSelection(stage->Resources());
    EXPECT_EQ(stage->world.Runtime().floor.pendingPickUp, -1);
    EXPECT_EQ(stage->world.Runtime().ui.field5767954, 0);
}

// El nombre del piso (0x5ba790) sobre un GameHudContext con Item.bmd y Text.bmd sintéticos.
class FloorItemNameTest : public ::testing::Test
{
protected:
    FloorItemNameTest() : m_stage(StageIn(0)) {}

    Scenes::GameHudContext Context()
    {
        Stage& stage = *m_stage;
        return Scenes::GameHudContext{.world = stage.world,
                                      .random = stage.cloth.random,
                                      .ownRecordDepth = m_own,
                                      .cellXDepth = m_cellX,
                                      .cellYDepth = m_cellY,
                                      .hud = m_hud,
                                      .crywolfSandOverlay = m_sand,
                                      .sound = m_sound,
                                      .sprites = stage.sprites,
                                      .text = stage.text,
                                      .texts = &stage.texts,
                                      .items = &stage.items,
                                      .setOptions = &stage.sets.options,
                                      .setTypes = &stage.sets.types,
                                      .mouse = {300, 200}};
    }

    Expected<void, std::string> Draw(const FloorItemState& item, bool atTheItem = false)
    {
        Scenes::GameHudContext context = Context();
        return Scenes::DrawTheFloorItemName(context, item, atTheItem);
    }

    [[nodiscard]] static FloorItemState Item(std::uint16_t type, std::int32_t value, std::uint8_t excellent = 0)
    {
        FloorItemState item;
        item.alive = true;
        item.seen = true;
        item.itemType = static_cast<std::int16_t>(type);
        item.type = static_cast<std::int16_t>(type + FirstModel);
        item.value = value;
        item.excellent = excellent;
        item.screenX = 100;
        item.screenY = 80;
        return item;
    }

    [[nodiscard]] const Client::Tests::RecordedText& Last() const
    {
        return m_stage->text.texts.back();
    }

    [[nodiscard]] Client::Render::Color LastColor() const
    {
        return m_stage->sprites.Vertices().back().color;
    }

    std::unique_ptr<Stage> m_stage;
    std::uint8_t m_own = 0;
    std::uint8_t m_cellX = 0;
    std::uint8_t m_cellY = 0;
    std::uint8_t m_sand = 0;
    std::optional<std::int32_t> m_sound;
    Scenes::HudState m_hud{};
};

void ExpectColor(Client::Render::Color color, float red, float green, float blue)
{
    EXPECT_FLOAT_EQ(color.red, red);
    EXPECT_FLOAT_EQ(color.green, green);
    EXPECT_FLOAT_EQ(color.blue, blue);
}

// 0x5ba7d5: el zen, "%s %d" con el valor, en dorado; en el mouse, centrado con la mitad del ancho y 15 más arriba. Los
// colores del texto quedan en (0xffc8e6ff, 0xff000000).
TEST_F(FloorItemNameTest, TheZenGoesWithItsValueAtTheMouse)
{
    ASSERT_TRUE(Draw(Item(Zen, 66053)).HasValue());
    EXPECT_EQ(Last().text, "Oro 66053");
    EXPECT_EQ(Last().x, 300 - 9 * 6 / 2);
    EXPECT_EQ(Last().y, 200 - 15);
    EXPECT_EQ(Last().style.font, Client::Render::Text::FontKind::Normal);
    EXPECT_EQ(Last().style.colors.text, 0xffffffffu);
    EXPECT_EQ(Last().style.colors.background, 0xff000000u);
    ExpectColor(LastColor(), 1.0f, 0.8f, 0.1f);
    EXPECT_EQ(m_hud.textColors.text, 0xffc8e6ffu);
}

// 0x5bb7bc: las joyas en negrita y dorado; con Alt, en la proyección del ítem.
TEST_F(FloorItemNameTest, TheJewelsGoInBoldAtTheItem)
{
    ASSERT_TRUE(Draw(Item(Jewel, 0), true).HasValue());
    EXPECT_EQ(Last().text, "Joya inventada");
    EXPECT_EQ(Last().x, 100 - 14 * 6 / 2);
    EXPECT_EQ(Last().y, 80 - 15);
    EXPECT_EQ(Last().style.font, Client::Render::Text::FontKind::Bold);
    ExpectColor(LastColor(), 1.0f, 0.8f, 0.1f);
}

// 0x5bb449..0x5bb72e: los demás, con el color del nivel o de las opciones, " +nivel" y los textos de la skill, la
// opción y la suerte.
TEST_F(FloorItemNameTest, TheOthersGoWithTheColorOfTheirLevelAndTheirOptions)
{
    ASSERT_TRUE(Draw(Item(Sword, 0)).HasValue());
    EXPECT_EQ(Last().text, "Espada inventada");
    ExpectColor(LastColor(), 0.7f, 0.7f, 0.7f);

    ASSERT_TRUE(Draw(Item(Sword, 2 << 3)).HasValue());
    EXPECT_EQ(Last().text, "Espada inventada +2");
    ExpectColor(LastColor(), 0.9f, 0.9f, 0.9f);

    ASSERT_TRUE(Draw(Item(Sword, 4 << 3)).HasValue());
    ExpectColor(LastColor(), 1.0f, 0.5f, 0.2f);

    ASSERT_TRUE(Draw(Item(Sword, (3 << 3) | 0x80 | 0x04 | 0x01)).HasValue());
    EXPECT_EQ(Last().text, "Espada inventada +3 +habilidad +opcion +suerte");
    ExpectColor(LastColor(), 0.4f, 0.7f, 1.0f);

    ASSERT_TRUE(Draw(Item(Sword, 9 << 3)).HasValue());
    ExpectColor(LastColor(), 1.0f, 0.8f, 0.1f);

    ASSERT_TRUE(Draw(Item(Sword, 1 << 3, 0x02)).HasValue());
    ExpectColor(LastColor(), 0.1f, 1.0f, 0.5f);

    // 0x5bb6c5: la opción excelente 0x40 también agrega el texto de la opción.
    ASSERT_TRUE(Draw(Item(Sword, 0, 0x40)).HasValue());
    EXPECT_EQ(Last().text, "Espada inventada +opcion");
}

// 0x5bace0: la caja de la suerte de nivel 8 a 12 (el modelo 0x2244), "%s +%d" con el texto 115 y el nivel − 7.
// 0x5ba94c: la del 0x1c0c (0x1e1e) de nivel 0 a 2 con su texto; con otro nivel, el original dibuja la pila.
TEST_F(FloorItemNameTest, TheBoxesGoWithTheirTexts)
{
    FloorItemState box = Item(BoxOfLuck, 9 << 3);
    box.type = 0x2244;
    ASSERT_TRUE(Draw(box).HasValue());
    EXPECT_EQ(Last().text, "caja de la suerte +2");

    FloorItemState chest = Item(0x1c0c, 2 << 3);
    ASSERT_TRUE(Draw(chest).HasValue());
    EXPECT_EQ(Last().text, "caja C");
    chest.value = 3 << 3;
    const auto unwritten = Draw(chest);
    ASSERT_FALSE(unwritten.HasValue());
    EXPECT_NE(unwritten.Error().find("Q-C1242"), std::string::npos) << unwritten.Error();
}

// 0x60848b..0x608491: la caja de la suerte de nivel 13 escribe el modelo 0x2240 y sigue en el caso de abajo, que lo
// pisa con 0x223f; el nombre es el de 0x223f (el 106), y el nivel 13 de 0x2240 (0x5bac43, el 117) no se alcanza desde
// el piso (07 Q-C1243).
TEST_F(FloorItemNameTest, TheBoxOfLevelThirteenTakesTheModelOfTheNextCase)
{
    const FloorItemsCreate create{
        .items = {FloorItemEntry{.key = 3, .x = 1, .y = 2, .item = ItemBytes(BoxOfLuck, 13 << 3, 1)}}};
    const auto created = Scenes::ApplyFloorItemsCreate(create, m_stage->Resources());
    ASSERT_TRUE(created.HasValue()) << created.Error();
    const FloorItemState& box = m_stage->Item(3);
    EXPECT_EQ(box.type, 0x223f);
    ASSERT_TRUE(Draw(box).HasValue());
    EXPECT_EQ(Last().text, "caja dos");
}

// 0x5bbd31..0x5bbde7: el hover nombra al elegido en el mouse y, con Alt, a los demás vivos y a la vista en su
// proyección.
TEST_F(FloorItemNameTest, TheHoverNamesThePickedItemAndAltTheOthers)
{
    auto tail = std::make_unique<Scenes::GameDrawTailState>();
    tail->floorItems[3] = Item(Potion, 0);
    tail->floorItems[4] = Item(Jewel, 0);
    std::array<std::int32_t, 5> targets{3, -1, -1, -1, -1};
    std::int32_t latch = 0;
    std::uint8_t names = 0;
    Scenes::GameHudContext context = Context();
    Scenes::GameHoverContext hover{.world = m_stage->world,
                                   .tail = *tail,
                                   .random = m_stage->cloth.random,
                                   .targets = targets,
                                   .hud = m_hud,
                                   .altLatch = latch,
                                   .altNames = names,
                                   .altDown = true,
                                   .sprites = m_stage->sprites,
                                   .text = m_stage->text,
                                   .names = &context};
    const auto before = m_stage->cloth.random.State();
    ASSERT_TRUE(Scenes::DrawTheHover(hover).HasValue());
    EXPECT_EQ(m_stage->cloth.random.State(), before);
    ASSERT_EQ(m_stage->text.texts.size(), 2u);
    EXPECT_EQ(m_stage->text.texts[0].text, "Pocion inventada");
    EXPECT_EQ(m_stage->text.texts[0].y, 200 - 15);
    EXPECT_EQ(m_stage->text.texts[1].text, "Joya inventada");
    EXPECT_EQ(m_stage->text.texts[1].y, 80 - 15);
}
} // namespace
