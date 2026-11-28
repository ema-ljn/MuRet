#include "Core/Expected.h"
#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Core/PixelSize.h"
#include "Crt/CrtRandom.h"
#include "Tables/PlayerModels.h"
#include "Tables/ItemSetTables.h"
#include "Tables/ItemTable.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Data/Terrain/TerrainHeights.h"
#include "Network/Xor32Packet.h"
#include "Protocol/Items/FloorItems.h"
#include "Protocol/Items/Inventory.h"
#include "Models/Cloth.h"
#include "Models/LogicalAccess.h"
#include "Models/RayPicking.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/TextStyle.h"
#include "Kernel/ActorState.h"
#include "Actors/Drawing/CharacterDrawing.h"
#include "Items/FloorItemActions.h"
#include "Items/FloorItemNames.h"
#include "Items/FloorItemPackets.h"
#include "Kernel/DrawTailState.h"
#include "Hud/GameHover.h"
#include "Hud/GameHud.h"
#include "Items/InventoryMoves.h"
#include "FrontEnd/LoginModels.h"
#include "Monsters/NpcNames.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Kernel/OwnInventory.h"
#include "Session/ReturnToSelection.h"
#include "FrontEnd/SelectionState.h"
#include "Session/ModuleStates.h"
#include "Session/WorldEntry.h"
#include "Kernel/WorldMap.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Ui/GameDialogs.h"
#include "Camera/GameCamera.h"
#include "Kernel/ItemState.h"
#include "Effects/ModelEffects.h"
#include "Terrain/TerrainPickingSegment.h"
#include "Audio/SoundId.h"

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

#include <algorithm>
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

// The floor items (CL9 milestone 5; 06 § Los ítems del piso): the incoming 20, 21, 22, 23 and 2C, picking up
// (0x5650c0, the space bar), the picking search (0x59e2e0), the room in the grid (0x5c9d20) and the name (0x5ba790).
// Item.bmd and Text.bmd are synthetic, with made-up names and texts.
namespace
{
using Client::Core::Expected;
using Client::Crt::CrtRandom;
using Client::Kernel::FloorItemState;
using Client::Kernel::WorldState;
using Client::Network::Xor32Decode;
using Client::Protocol::Items::FloorItemEntry;
using Client::Protocol::Items::FloorItemsCreate;
using Client::Protocol::Items::ItemData;
using Client::Scenes::WorldEntryResources;
using Client::Tests::Bytes;
using Client::Tests::SameBytes;
namespace Scenes = Client::Scenes;

constexpr std::uint32_t Seed = 0x2468aceu;
constexpr int Own = Client::Models::OwnRecordCloseRandoms;
constexpr int SendRandoms = 2;
constexpr std::size_t HeroSlot = 42;
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

// Synthetic Item.bmd: each type's name (+0), width (+0x24) and height (+0x25).
Client::Tables::ItemTable NamedItems(std::initializer_list<NamedItem> named)
{
    using Client::Tables::ItemRecordSize;
    std::vector<std::byte> plain(Client::Tables::ItemTableSize);
    for (const NamedItem& item : named)
    {
        const std::size_t base = std::size_t{item.type} * ItemRecordSize;
        for (std::size_t i = 0; i < item.name.size(); ++i)
            plain[base + i] = static_cast<std::byte>(item.name[i]);
        plain[base + 0x24] = std::byte{item.width};
        plain[base + 0x25] = std::byte{item.height};
    }
    const std::vector<std::byte> encrypted = Client::Tests::EncryptRecords(std::move(plain), ItemRecordSize);
    return Client::Tables::ItemTable(std::span<const std::byte>(encrypted));
}

// An item's seven bytes (0x54b3a0 in reverse).
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

Client::Tables::ItemSetTables NoSets()
{
    return Client::Tests::ItemSetTablesFrom(
        std::vector<std::byte>(Client::Tables::ItemSetTypeTableSize, std::byte{0xff}),
        std::vector<std::byte>(Client::Tables::ItemSetOptionTableSize, std::byte{0xff}));
}

CrtRandom Advanced(CrtRandom from, std::size_t count)
{
    from.Discard(static_cast<std::uint32_t>(count));
    return from;
}

// Per test: the world, the flat terrain at `Ground`, Player.bmd for 0x512a30, the texts and the message meter. On the
// heap, because of WorldState's size.
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
                                              {1412, "sumados"},  {1900, "comando"},     {1903, "restados"},
                                              {103, "Anillo"}}))
    {
        sprites.Begin(Client::Core::PixelSize{640, 480});
        models.Set(Client::Tables::PlayerSkeletonModel, {}, Client::Tests::PlayerActionsSkeleton(), {});
        world.OwnCharacter().ClearEquipmentForEntry();
        world.Inventory().ClearForInventoryReply();
        Client::Kernel::WorldRuntime& runtime = world.Runtime();
        runtime.map = map;
        runtime.hero = HeroSlot;
        Client::Kernel::ActorState& hero = world.Actors()[HeroSlot];
        hero = Client::Tests::UnarmedPlayer();
        hero.alive = true;
        auto& terrain = states.terrain.current;
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
                                   .screen = [] { return Client::Core::PixelSize{640, 480}; },
                                   .texts = &texts,
                                   .states = &states};
    }

    [[nodiscard]] Client::Items::InventoryMoveContext Moves()
    {
        return Client::Items::InventoryMoveContext{.world = world, .random = cloth.random, .services = services};
    }

    FloorItemState& Item(std::size_t index)
    {
        return world.DrawTail().floorItems[index];
    }

    WorldState world;
    Client::Session::ModuleStates states;
    Client::FrontEnd::SelectionState selection;
    Client::Camera::GameCamera camera;
    Client::Camera::GameCameraInput cameraInput;
    Client::Models::ClothWorld cloth;
    Client::FrontEnd::SingleModelLibrary models;
    Client::Tables::ItemTable items = NamedItems({{Sword, "Espada inventada", 1, 3},
                                                  {Potion, "Pocion inventada"},
                                                  {Zen, "Oro"},
                                                  {Jewel, "Joya inventada"},
                                                  {BoxOfLuck, "Caja inventada"}});
    Client::Tables::ItemSetTables sets = NoSets();
    Client::Tests::RecordingSink sink;
    Client::Core::Log log{sink, Client::Core::LogLevel::Debug};
    Client::Render::SpriteBatch sprites;
    Client::Tests::RecordingTextDrawer text;
    Client::Tables::TextTable texts;
    Client::Items::InventoryServices services;
};

std::unique_ptr<Stage> StageIn(std::int32_t map)
{
    return std::make_unique<Stage>(map);
}

FloorItemsCreate ThreeItems()
{
    return FloorItemsCreate{
        .items = {
            // The just dropped zen in 5: 0x01 0x02 in b1 and b2, 0x05 in b4.
            FloorItemEntry{.key = 0x8005, .x = 130, .y = 120, .item = ItemBytes(Zen, 1, 2, 0, 5)},
            FloorItemEntry{.key = 7, .x = 10, .y = 20, .item = ItemBytes(Potion, 0, 1)},
            // A +3 sword with options at key 1005, which goes to 0.
            FloorItemEntry{.key = 1005, .x = 1, .y = 2, .item = ItemBytes(Sword, 3 << 3, 30, 0x01, 0x05)},
        }};
}

// 0x4d0ba0 → 0x608350: each entry at (cell + 0.5) · 100, the type in +4, the model, the value and the options; the
// just dropped one 180 above the terrain with the speed 20 and the zen's sound; the others on the terrain. The rand()
// are 0x607b60's for each model. The same in Lorencia, Devias and Icarus (10, which only changes 0x608a00's draw).
TEST(FloorItemsSceneTest, TheCreateFillsEachEntryInEveryMap)
{
    for (const std::int32_t map : {0, 2, 10})
    {
        auto stage = StageIn(map);
        const CrtRandom expected =
            Advanced(stage->cloth.random, Client::Actors::Drawing::HangingRandomCount(Zen + FirstModel) +
                                              Client::Actors::Drawing::HangingRandomCount(Potion + FirstModel) +
                                              Client::Actors::Drawing::HangingRandomCount(Sword + FirstModel));
        const auto created = Client::Items::ApplyFloorItemsCreate(ThreeItems(), stage->Resources());
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
        // 0x6083a5: only the just dropped one sounds (CL10).
        EXPECT_EQ(stage->world.Runtime().lastSoundId, Client::Audio::SoundId{0x1f});
    }
}

// 0x608614..0x608722: the silver medal (the 0x1c0b of level 5, model 0x2242) and the gold one (level 6, 0x2243) just
// dropped create the crack 0x100 (0x6086ce) and the 0xfe (0x6086f1) with 0x518cf0: 3 above the terrain at the
// requested position, angles at 0, light 1, subtype 0, the medal as owner and size 0x37 (life 35, scale 0.55f), with no
// rand(); the medal stays at 3 with the speed 50. The one already on the floor, on the terrain and without cracks. The
// same in Lorencia, Devias (2, where the GS 0.97 drops the silver one) and Lost Tower (4, the gold one).
TEST(FloorItemsSceneTest, TheDroppedMedalsCrackTheGroundInEveryMap)
{
    constexpr std::int16_t SilverMedal = 0x2242;
    constexpr std::int16_t GoldMedal = 0x2243;
    const float scale = static_cast<float>(55.0 * static_cast<double>(0.01f));
    for (const std::int32_t map : {0, 2, 4})
    {
        SCOPED_TRACE(map);
        auto stage = StageIn(map);
        const CrtRandom expected =
            Advanced(stage->cloth.random, (2 * Client::Actors::Drawing::HangingRandomCount(SilverMedal)) +
                                              Client::Actors::Drawing::HangingRandomCount(GoldMedal));
        const FloorItemsCreate create{
            .items = {FloorItemEntry{.key = 0x8003, .x = 30, .y = 40, .item = ItemBytes(BoxOfLuck, 5 << 3, 1)},
                      FloorItemEntry{.key = 0x8004, .x = 50, .y = 60, .item = ItemBytes(BoxOfLuck, 6 << 3, 1)},
                      FloorItemEntry{.key = 6, .x = 70, .y = 80, .item = ItemBytes(BoxOfLuck, 5 << 3, 1)}}};
        const auto created = Client::Items::ApplyFloorItemsCreate(create, stage->Resources());
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
            const Client::Effects::ModelEffect& effect = effects[slot];
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
            EXPECT_EQ(Client::Effects::OwnerTypeOf(effect.owner),
                      std::optional<std::uint16_t>(static_cast<std::uint16_t>(owner.type)));
            EXPECT_EQ(effect.owner.angles, &owner.angles);
        }
        for (std::size_t slot = cracks.size(); slot < effects.size(); ++slot)
            EXPECT_FALSE(effects[slot].alive) << slot;
    }
}

// With only one free slot in the effect table, 0x518cf0 creates the 0x100 and not the 0xfe; with the table full,
// none. The medal is the same, at 3 with the speed 50, and there are no extra rand().
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
        const CrtRandom expected =
            Advanced(stage->cloth.random, Client::Actors::Drawing::HangingRandomCount(GoldMedal));
        const FloorItemsCreate create{
            .items = {FloorItemEntry{.key = 0x8009, .x = 12, .y = 34, .item = ItemBytes(BoxOfLuck, 6 << 3, 1)}}};
        const auto created = Client::Items::ApplyFloorItemsCreate(create, stage->Resources());
        ASSERT_TRUE(created.HasValue()) << created.Error();
        EXPECT_EQ(stage->cloth.random.State(), expected.State());
        const FloorItemState& gold = stage->Item(9);
        EXPECT_EQ(gold.type, GoldMedal);
        EXPECT_FLOAT_EQ(gold.position.z, Ground + 3.0f);
        EXPECT_FLOAT_EQ(gold.dropSpeed, 50.0f);
        const Client::Effects::ModelEffect& last = effects.back();
        EXPECT_TRUE(last.alive);
        EXPECT_EQ(last.type, free == 1 ? std::int16_t{0x100} : Busy);
        for (std::size_t slot = 0; slot + 1 < effects.size(); ++slot)
            EXPECT_EQ(effects[slot].type, Busy) << slot;
    }
}

// 0x4e3849: each key turns off its entry, without the 20's mask: those of 1000 or more, 0.
TEST(FloorItemsSceneTest, TheDeleteTurnsOffEachKey)
{
    auto stage = StageIn(0);
    ASSERT_TRUE(Client::Items::ApplyFloorItemsCreate(ThreeItems(), stage->Resources()).HasValue());
    Client::Items::ApplyFloorItemsDelete(Client::Protocol::Items::FloorItemsDelete{.keys = {5, 1500}}, stage->world);
    EXPECT_FALSE(stage->Item(5).alive);
    EXPECT_FALSE(stage->Item(0).alive);
    EXPECT_TRUE(stage->Item(7).alive);
}

// 0x59e2e0: no rand(); the live ones in view with the vertex pass's light 0.2, and the first the ray touches with 1.5
// and without the pass. The ones not in view are not touched.
TEST(FloorItemsSceneTest, ThePickingSearchLightsTheItemsAndTakesTheFirstHit)
{
    auto items = std::make_unique<std::array<FloorItemState, 1000>>();
    const auto put = [&](std::size_t index, bool seen, float x)
    {
        FloorItemState& item = (*items)[index];
        item.alive = true;
        item.seen = seen;
        item.pickBox = Client::Models::Box{.min = {x - 30.0f, -30.0f, -30.0f}, .max = {x + 30.0f, 30.0f, 30.0f}};
    };
    put(3, false, 0.0f);
    put(4, true, 500.0f);
    put(8, true, 0.0f);
    put(9, true, 0.0f);
    const Client::Terrain::TerrainPickingSegment ray{.origin = {0.0f, 0.0f, 1000.0f}, .end = {0.0f, 0.0f, -1000.0f}};

    EXPECT_EQ(Client::Items::PickTheFloorItem(*items, ray), 8);
    EXPECT_FALSE((*items)[8].vertexLight);
    EXPECT_EQ((*items)[8].light, glm::vec3(1.5f));
    EXPECT_TRUE((*items)[9].vertexLight);
    EXPECT_EQ((*items)[9].light, glm::vec3(0.2f));
    EXPECT_TRUE((*items)[4].vertexLight);
    EXPECT_FALSE((*items)[3].vertexLight);

    const Client::Terrain::TerrainPickingSegment away{.origin = {2000.0f, 0.0f, 1000.0f},
                                                      .end = {2000.0f, 0.0f, -1000.0f}};
    EXPECT_EQ(Client::Items::PickTheFloorItem(*items, away), -1);
}

// 0x5c9d20.
TEST(FloorItemsSceneTest, TheRoomInTheGridLooksForEmptyCells)
{
    auto stage = StageIn(0);
    auto& cells = stage->world.Inventory().cells;
    for (auto& cell : cells)
        cell.id = Potion;
    constexpr auto PotionType = static_cast<std::int16_t>(Potion);
    constexpr auto SwordType = static_cast<std::int16_t>(Sword);
    EXPECT_FALSE(Client::Items::HasRoomFor(&stage->items, cells, PotionType).Value());
    cells[63].id = Client::Kernel::EmptyOwnItemId;
    EXPECT_TRUE(Client::Items::HasRoomFor(&stage->items, cells, PotionType).Value());
    EXPECT_FALSE(Client::Items::HasRoomFor(&stage->items, cells, SwordType).Value());
    cells[47].id = Client::Kernel::EmptyOwnItemId;
    cells[55].id = Client::Kernel::EmptyOwnItemId;
    EXPECT_TRUE(Client::Items::HasRoomFor(&stage->items, cells, SwordType).Value());
}

// Case 1 of 0x5650c0 with the hero within 150 of the target cell's center: the mouse wait at 6, the request in
// 0x6c82e8 and the C1 05 22 with the index, in C3. With the request set, it does not request another. Far away,
// nothing.
TEST(FloorItemsSceneTest, ArrivingPicksUpTheChosenItemByteByByte)
{
    for (const std::int32_t map : {0, 2, 10})
    {
        auto stage = StageIn(map);
        ASSERT_TRUE(Client::Items::ApplyFloorItemsCreate(ThreeItems(), stage->Resources()).HasValue());
        std::vector<std::uint16_t> sent;
        stage->services.sendPickUp = [&sent](std::uint16_t index)
        {
            sent.push_back(index);
            return Expected<void, std::string>{};
        };
        stage->services.items = &stage->items;
        Client::Kernel::WorldRuntime& runtime = stage->world.Runtime();
        runtime.floor.chosen = 7;
        runtime.movement.targetX = 10;
        runtime.movement.targetY = 20;

        ASSERT_TRUE(Client::Items::PickUpTheFloorItem(stage->Moves(), {1300.0f, 2050.0f, 0.0f},
                                                      stage->states.terrain.current, 1200.0f)
                        .HasValue());
        EXPECT_TRUE(sent.empty());

        const CrtRandom expected = Advanced(stage->cloth.random, SendRandoms);
        ASSERT_TRUE(Client::Items::PickUpTheFloorItem(stage->Moves(), {1100.0f, 2100.0f, 0.0f},
                                                      stage->states.terrain.current, 1200.0f)
                        .HasValue());
        EXPECT_EQ(stage->cloth.random.State(), expected.State());
        ASSERT_EQ(sent.size(), 1u);
        const auto packet = Client::Protocol::Items::WritePickUp(sent.front());
        EXPECT_TRUE(SameBytes(Xor32Decode(packet.packet).Bytes(), Bytes({0xc1, 0x05, 0x22, 0x00, 0x07})));
        EXPECT_EQ(runtime.floor.pendingPickUp, 7);
        EXPECT_EQ(runtime.mouse.updateTimeMax, 6);

        runtime.floor.chosen = 5;
        ASSERT_TRUE(Client::Items::PickUpTheFloorItem(stage->Moves(), {1100.0f, 2100.0f, 0.0f},
                                                      stage->states.terrain.current, 1200.0f)
                        .HasValue());
        EXPECT_EQ(sent.size(), 1u);
    }
}

// 0x566e0a..0x566e76: with no room in the grid, notice 375 of type 1, the item 3 above the terrain and its drop at 50.
TEST(FloorItemsSceneTest, WithoutRoomTheItemJumpsAndWarns)
{
    auto stage = StageIn(2);
    ASSERT_TRUE(Client::Items::ApplyFloorItemsCreate(ThreeItems(), stage->Resources()).HasValue());
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
    Client::Kernel::WorldRuntime& runtime = stage->world.Runtime();
    runtime.floor.chosen = 0;
    runtime.floor.fromSpace = 1;
    const auto before = stage->cloth.random.State();
    ASSERT_TRUE(
        Client::Items::PickUpTheFloorItem(stage->Moves(), {}, stage->states.terrain.current, 1200.0f).HasValue());
    EXPECT_EQ(stage->cloth.random.State(), before);
    EXPECT_EQ(lines, std::vector<std::string>{"no hay lugar"});
    EXPECT_FLOAT_EQ(stage->Item(0).position.z, Ground + 3.0f);
    EXPECT_FLOAT_EQ(stage->Item(0).dropSpeed, 50.0f);
    EXPECT_EQ(runtime.floor.pendingPickUp, -1);
}

// 0x5ac928..0x5ac9c2: the space bar picks the first live item in view within 300 of the hero and requests it without
// checking the target; then the hero's +0x379 and 0x77f3b40 go back to 0.
TEST(FloorItemsSceneTest, TheSpaceBarPicksUpTheNearestSeenItem)
{
    auto stage = StageIn(0);
    ASSERT_TRUE(Client::Items::ApplyFloorItemsCreate(ThreeItems(), stage->Resources()).HasValue());
    std::vector<std::uint16_t> sent;
    stage->services.sendPickUp = [&sent](std::uint16_t index)
    {
        sent.push_back(index);
        return Expected<void, std::string>{};
    };
    stage->services.items = &stage->items;
    Client::Kernel::ActorState& hero = stage->world.Actors()[HeroSlot];
    hero.position = {1050.0f, 2050.0f + 299.0f, Ground};
    stage->Item(7).seen = false;
    ASSERT_TRUE(
        Client::Items::PickUpTheNearestFloorItem(stage->Moves(), stage->states.terrain.current, 1200.0f).HasValue());
    EXPECT_TRUE(sent.empty());

    stage->Item(7).seen = true;
    ASSERT_TRUE(
        Client::Items::PickUpTheNearestFloorItem(stage->Moves(), stage->states.terrain.current, 1200.0f).HasValue());
    EXPECT_EQ(sent, std::vector<std::uint16_t>{7});
    EXPECT_EQ(stage->world.Runtime().floor.chosen, 7);
    EXPECT_EQ(stage->world.Runtime().floor.fromSpace, 0);
    EXPECT_EQ(hero.movementType, 0);

    stage->world.Runtime().floor.pendingPickUp = -1;
    hero.position.y = 2050.0f + 300.0f;
    ASSERT_TRUE(
        Client::Items::PickUpTheNearestFloorItem(stage->Moves(), stage->states.terrain.current, 1200.0f).HasValue());
    EXPECT_EQ(sent.size(), 1u);
}

// 0x4d0cc0: with 0xfe, the zen in the own record (its close) and, with the picked one alive, "%d %s %s"; the sound by
// the packet's bytes and the request at −1. With 0xff, only the request. With 0xfd, the picked one's name (0x5b1c70).
TEST(FloorItemsSceneTest, ThePickUpResultTakesTheZenOrNamesTheItem)
{
    auto stage = StageIn(0);
    ASSERT_TRUE(Client::Items::ApplyFloorItemsCreate(ThreeItems(), stage->Resources()).HasValue());
    Client::Kernel::WorldRuntime& runtime = stage->world.Runtime();
    runtime.floor.pendingPickUp = 5;
    runtime.floor.chosen = 5;
    const CrtRandom expected = Advanced(stage->cloth.random, Own);
    ASSERT_TRUE(Client::Items::ApplyPickUpResult({.result = 0xfe, .zen = 123456}, stage->Resources()).HasValue());
    EXPECT_EQ(stage->cloth.random.State(), expected.State());
    EXPECT_EQ(stage->world.OwnCharacter().zen, 123456u);
    ASSERT_EQ(runtime.chat.log.entries.size(), 1u);
    EXPECT_EQ(runtime.chat.log.entries[0].text, "66053 zen levantado");
    EXPECT_EQ(runtime.lastSoundId, Client::Audio::SoundId{0x1d});
    EXPECT_EQ(runtime.floor.pendingPickUp, -1);

    runtime.floor.pendingPickUp = 5;
    ASSERT_TRUE(Client::Items::ApplyPickUpResult({.result = 0xff}, stage->Resources()).HasValue());
    EXPECT_EQ(runtime.floor.pendingPickUp, -1);
    EXPECT_EQ(runtime.chat.log.entries.size(), 1u);

    runtime.floor.chosen = 0;
    ASSERT_TRUE(
        Client::Items::ApplyPickUpResult({.result = 0xfd, .item = ItemBytes(Jewel)}, stage->Resources()).HasValue());
    ASSERT_EQ(runtime.chat.log.entries.size(), 2u);
    // The log keeps the last one first.
    EXPECT_EQ(runtime.chat.log.entries[0].text, "Espada inventada +3 levantado");
    EXPECT_EQ(runtime.lastSoundId, Client::Audio::SoundId{0x45});
}

// 0x4d10d0: refused, the cursor item returns (0x5be150); dropped from the grid, it leaves it. Both leave [0x6c82ec] at
// −1.
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
    stage->world.Runtime().mouse.pendingClick = true;
    ASSERT_TRUE(Client::Items::PickUpFromTheGrid(stage->Moves(), 10).HasValue());
    stage->world.Runtime().floor.pendingDrop = 22;

    const auto rejected = Client::Items::ApplyDropResult({.result = 0, .slot = 22}, stage->Resources());
    ASSERT_TRUE(rejected.HasValue()) << rejected.Error();
    EXPECT_EQ(stage->world.Runtime().cursorItemCount, 0);
    EXPECT_EQ(cell.id, Potion);
    EXPECT_EQ(stage->world.Runtime().floor.pendingDrop, -1);

    stage->world.Runtime().mouse.pendingClick = true;
    ASSERT_TRUE(Client::Items::PickUpFromTheGrid(stage->Moves(), 10).HasValue());
    const auto dropped = Client::Items::ApplyDropResult({.result = 1, .slot = 22}, stage->Resources());
    ASSERT_TRUE(dropped.HasValue()) << dropped.Error();
    EXPECT_EQ(stage->world.Runtime().cursorItemCount, 0);
    EXPECT_EQ(stage->world.Runtime().cursorItem.occupied, 0u);
    EXPECT_EQ(cell.id, Client::Kernel::EmptyOwnItemId);
}

// 0x4de500: with 0, the stat and +0x44 go up inside the own record, with their notice; with 3, the stat goes down and
// +0x68 and +0x48 go up; with 1, notice 378. Always 0x5767954 at 0.
TEST(FloorItemsSceneTest, TheStatFruitChangesTheStatAndWarns)
{
    auto stage = StageIn(0);
    Client::Kernel::OwnCharacterRecord& own = stage->world.OwnCharacter();
    own.vitality = 40;
    stage->world.Runtime().ui.field5767954 = 10;
    const CrtRandom expected = Advanced(stage->cloth.random, Own);
    ASSERT_TRUE(Client::Items::ApplyStatFruit({.result = 0, .points = 3, .stat = 1}, stage->world, stage->cloth.random,
                                              &stage->texts)
                    .HasValue());
    EXPECT_EQ(stage->cloth.random.State(), expected.State());
    EXPECT_EQ(own.vitality, 43);
    EXPECT_EQ(own.addedPoints, 3);
    EXPECT_EQ(Client::Ui::DialogRow(stage->world.Runtime().dialogBox, 0), "vitalidad 3 sumados");
    EXPECT_EQ(stage->world.Runtime().ui.field5767954, 0);

    ASSERT_TRUE(Client::Items::ApplyStatFruit({.result = 3, .points = 2, .stat = 3}, stage->world, stage->cloth.random,
                                              &stage->texts)
                    .HasValue());
    EXPECT_EQ(own.levelUpPoints, 2);

    stage->world.Runtime().ui.field5767954 = 10;
    ASSERT_TRUE(
        Client::Items::ApplyStatFruit({.result = 1}, stage->world, stage->cloth.random, &stage->texts).HasValue());
    EXPECT_EQ(stage->world.Runtime().ui.field5767954, 0);
}

// 07 Q-C1240: of a short 22, 23 or 2C, only what the original writes on all its paths. 0x4d10b9 releases the request;
// 0x4d13d1 and 0x4d13e5, the dropped slot, and with pkt[3] other than 0, 0x4d13c7 empties the cursor; 0x4deb0e and
// 0x4de500's other exits, the use wait. Without them, nothing is picked up or used again.
TEST(FloorItemsSceneTest, TheShortRepliesReleaseOnlyTheirWaits)
{
    auto stage = StageIn(0);
    Client::Kernel::WorldRuntime& runtime = stage->world.Runtime();
    runtime.floor.pendingPickUp = 5;
    runtime.ui.field5767954 = 10;
    Client::Items::ApplyShortPickUpResult(stage->world);
    EXPECT_EQ(runtime.floor.pendingPickUp, -1);
    EXPECT_EQ(runtime.ui.field5767954, 10);
    Client::Items::ApplyShortStatFruit(stage->world);
    EXPECT_EQ(runtime.ui.field5767954, 0);

    for (const std::optional<std::uint8_t> result :
         {std::optional<std::uint8_t>{}, std::optional<std::uint8_t>{std::uint8_t{0}}})
    {
        runtime.cursorItemCount = 1;
        runtime.cursorItem.occupied = 1;
        runtime.floor.pendingDrop = 22;
        Client::Items::ApplyShortDropResult({.result = result}, stage->world);
        EXPECT_EQ(runtime.floor.pendingDrop, -1);
        EXPECT_EQ(runtime.cursorItemCount, 1);
        EXPECT_EQ(runtime.cursorItem.occupied, 1u);
    }
    runtime.floor.pendingDrop = 22;
    Client::Items::ApplyShortDropResult({.result = std::uint8_t{1}}, stage->world);
    EXPECT_EQ(runtime.floor.pendingDrop, -1);
    EXPECT_EQ(runtime.cursorItemCount, 0);
    EXPECT_EQ(runtime.cursorItem.occupied, 0u);
}

// 07 Q-C1240: the GameServer 0.97's C3 08 22 FE (the zen when buying, selling or repairing) carries the zen. 0x4d0cc0
// writes it with the record open (its close) and, with the picked one alive, the notice "%d %s %s", as with the whole
// packet, and releases the request. What does not come out is 0x4d1051's sound, which reads the type from pkt[4..10]
// and here falls outside the packet.
TEST(FloorItemsSceneTest, TheShortZenOfThe22WritesTheZenWithoutTheSound)
{
    auto stage = StageIn(0);
    ASSERT_TRUE(Client::Items::ApplyFloorItemsCreate(ThreeItems(), stage->Resources()).HasValue());
    Client::Kernel::WorldRuntime& runtime = stage->world.Runtime();
    runtime.floor.pendingPickUp = 5;
    runtime.floor.chosen = 5;
    // The just dropped items' sound.
    runtime.lastSoundId.reset();
    const CrtRandom expected = Advanced(stage->cloth.random, Own);
    ASSERT_TRUE(Client::Items::ApplyShortZenPickUp({.zen = 123456}, stage->Resources()).HasValue());
    EXPECT_EQ(stage->cloth.random.State(), expected.State());
    EXPECT_EQ(stage->world.OwnCharacter().zen, 123456u);
    ASSERT_EQ(runtime.chat.log.entries.size(), 1u);
    EXPECT_EQ(runtime.chat.log.entries[0].text, "66053 zen levantado");
    EXPECT_FALSE(runtime.lastSoundId.has_value());
    EXPECT_EQ(runtime.floor.pendingPickUp, -1);

    // With the picked one without an item (0x77f4714 at 0), the zen without the notice.
    runtime.floor.chosen = 3;
    runtime.floor.pendingPickUp = 5;
    ASSERT_TRUE(Client::Items::ApplyShortZenPickUp({.zen = 1000}, stage->Resources()).HasValue());
    EXPECT_EQ(stage->world.OwnCharacter().zen, 1000u);
    EXPECT_EQ(runtime.chat.log.entries.size(), 1u);
    EXPECT_EQ(runtime.floor.pendingPickUp, -1);
}

// 0x4c24cf: the return to selection releases the C1 22 request and 0x4c24c9 the use wait.
TEST(FloorItemsSceneTest, TheReturnToSelectionClearsThePickUpRequest)
{
    auto stage = StageIn(0);
    stage->world.Runtime().floor.pendingPickUp = 7;
    stage->world.Runtime().ui.field5767954 = 10;
    ASSERT_TRUE(Scenes::ApplyReturnToSelection(stage->Resources()).HasValue());
    EXPECT_EQ(stage->world.Runtime().floor.pendingPickUp, -1);
    EXPECT_EQ(stage->world.Runtime().ui.field5767954, 0);
}

// The floor name (0x5ba790) on a GameHudContext with synthetic Item.bmd and Text.bmd.
class FloorItemNameTest : public ::testing::Test
{
protected:
    FloorItemNameTest() : m_stage(StageIn(0)) {}

    Client::Hud::GameHudContext Context()
    {
        Stage& stage = *m_stage;
        return Client::Hud::GameHudContext{.world = stage.world,
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
        Client::Hud::GameHudContext context = Context();
        return Client::Items::DrawTheFloorItemName(context, item, atTheItem);
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
    std::optional<Client::Audio::SoundId> m_sound;
    Client::Kernel::HudState m_hud{};
};

void ExpectColor(Client::Render::Color color, float red, float green, float blue)
{
    EXPECT_FLOAT_EQ(color.red, red);
    EXPECT_FLOAT_EQ(color.green, green);
    EXPECT_FLOAT_EQ(color.blue, blue);
}

// 0x5ba7d5: zen, "%s %d" with the value, in gold; at the mouse, centered with half the width and 15 higher. The text
// colors stay at (0xffc8e6ff, 0xff000000).
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

// 0x5bb7bc: the jewels in bold and gold; with Alt, at the item's projection.
TEST_F(FloorItemNameTest, TheJewelsGoInBoldAtTheItem)
{
    ASSERT_TRUE(Draw(Item(Jewel, 0), true).HasValue());
    EXPECT_EQ(Last().text, "Joya inventada");
    EXPECT_EQ(Last().x, 100 - 14 * 6 / 2);
    EXPECT_EQ(Last().y, 80 - 15);
    EXPECT_EQ(Last().style.font, Client::Render::Text::FontKind::Bold);
    ExpectColor(LastColor(), 1.0f, 0.8f, 0.1f);
}

// 0x5bb449..0x5bb72e: the others, with the color of the level or the options, " +level" and the skill, option and
// luck texts.
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

    // 0x5bb6c5: the excellent option 0x40 also adds the option text.
    ASSERT_TRUE(Draw(Item(Sword, 0, 0x40)).HasValue());
    EXPECT_EQ(Last().text, "Espada inventada +opcion");
}

// 0x5bace0: the box of luck of level 8 to 12 (model 0x2244), "%s +%d" with text 115 and the level − 7. 0x5ba94c: the
// 0x1c0c's (0x1e1e) of level 0 to 2 with its text; with another level, the original draws the stack.
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

// 0x5bb2eb..0x5bb338: the transformation ring on the floor (model 0x1c1c): the level's monster at 0x6c9338 and the
// first record of 0x7681eb8 with that type, "%s %s" with its name and the 103. With none, the buffer stays unwritten
// (0x5bb30f, Q-C1242).
TEST_F(FloorItemNameTest, TheTransformationRingNamesItsFirstMonster)
{
    Client::Monsters::NpcNameTable& names = m_stage->world.Runtime().npcNames;
    names[3].type = 7;
    std::ranges::copy(std::string_view("Gigante A"), names[3].name.begin());
    names[10].type = 7;
    std::ranges::copy(std::string_view("Gigante B"), names[10].name.begin());
    ASSERT_TRUE(Draw(Item(0x1a0a, 1 << 3)).HasValue());
    EXPECT_EQ(Last().text, "Gigante A Anillo");
    const auto unwritten = Draw(Item(0x1a0a, 2 << 3));
    ASSERT_FALSE(unwritten.HasValue());
    EXPECT_NE(unwritten.Error().find("0x5bb30f"), std::string::npos) << unwritten.Error();
}

// 0x60848b..0x608491: the box of luck of level 13 writes model 0x2240 and goes on into the case below, which
// overwrites it with 0x223f; the name is 0x223f's (the 106), and 0x2240's level 13 (0x5bac43, the 117) is not reached
// from the floor (07 Q-C1243).
TEST_F(FloorItemNameTest, TheBoxOfLevelThirteenTakesTheModelOfTheNextCase)
{
    const FloorItemsCreate create{
        .items = {FloorItemEntry{.key = 3, .x = 1, .y = 2, .item = ItemBytes(BoxOfLuck, 13 << 3, 1)}}};
    const auto created = Client::Items::ApplyFloorItemsCreate(create, m_stage->Resources());
    ASSERT_TRUE(created.HasValue()) << created.Error();
    const FloorItemState& box = m_stage->Item(3);
    EXPECT_EQ(box.type, 0x223f);
    ASSERT_TRUE(Draw(box).HasValue());
    EXPECT_EQ(Last().text, "caja dos");
}

// 0x5bbd31..0x5bbde7: the hover names the picked one at the mouse and, with Alt, the other live ones in view at their
// projection.
TEST_F(FloorItemNameTest, TheHoverNamesThePickedItemAndAltTheOthers)
{
    auto tail = std::make_unique<Client::Kernel::GameDrawTailState>();
    tail->floorItems[3] = Item(Potion, 0);
    tail->floorItems[4] = Item(Jewel, 0);
    std::array<std::int32_t, 5> targets{3, -1, -1, -1, -1};
    std::int32_t latch = 0;
    std::uint8_t names = 0;
    Client::Hud::GameHudContext context = Context();
    Client::Hud::GameHoverContext hover{.world = m_stage->world,
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
    ASSERT_TRUE(Client::Hud::DrawTheHover(hover).HasValue());
    EXPECT_EQ(m_stage->cloth.random.State(), before);
    ASSERT_EQ(m_stage->text.texts.size(), 2u);
    EXPECT_EQ(m_stage->text.texts[0].text, "Pocion inventada");
    EXPECT_EQ(m_stage->text.texts[0].y, 200 - 15);
    EXPECT_EQ(m_stage->text.texts[1].text, "Joya inventada");
    EXPECT_EQ(m_stage->text.texts[1].y, 80 - 15);
}
} // namespace
