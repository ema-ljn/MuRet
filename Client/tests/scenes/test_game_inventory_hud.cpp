#include "Models/IModelLibrary.h"
#include "Hud/GameInventoryHud.h"

#include "Core/Expected.h"
#include "Core/PixelSize.h"
#include "Crt/CrtRandom.h"
#include "Data/Models/Model.h"
#include "Render/ModelBatch.h"
#include "Math/HalfLifeMath.h"
#include "Models/LogicalAccess.h"
#include "Models/Skeleton.h"
#include "Models/ModelId.h"
#include "Render/Pass3D.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/TextureSlot.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/TextStyle.h"
#include "Kernel/ActorState.h"
#include "Actors/Drawing/CharacterDrawing.h"
#include "Events/EventChipWindow.h"
#include "Kernel/PoseBuffer.h"
#include "Tables/GameData.h"
#include "Hud/GameHud.h"
#include "Actors/Drawing/GameItemSpecialDrawing.h"
#include "Scenes/GameProjection.h"
#include "Hud/GamePanels.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Kernel/OwnInventory.h"
#include "Items/UiItemDrawing.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Ui/GameText.h"
#include "Ui/Widget.h"
#include "Kernel/ItemState.h"
#include "Particles/Sprites.h"
#include "Audio/SoundId.h"
#include "support/GoldenDir.h"
#include "support/RecordingTextDrawer.h"
#include "Kernel/JointRequest.h"
#include "Social/SocialState.h"
#include "Ui/GameUiState.h"
#include "Actors/Drawing/GameItemRenderState.h"

#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_int4.hpp>
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// The inventory in the HUD (CL8 milestone 4): the window of 0x5f2340, the UI items (0x5e1480: without the models,
// their rand(); with made-up ones, the pass of 0x5ac120), the numbers of 0x5f7840 and the cursor item of 0x5f7b10. The
// items and the zen are made up.
namespace
{
using Client::Crt::CrtRandom;
using Client::Kernel::ItemState;
using Client::Kernel::WorldState;
using Client::Models::OwnRecordCloseRandoms;
using Client::Models::SmallLogicalCloseRandoms;
using Client::Render::Color;
using Client::Render::SpriteBatch;
using Client::Ui::GameHudContext;

constexpr std::uint32_t Seed = 0x13579bdfu;
constexpr Client::Core::PixelSize Screen640{640, 480};
constexpr int Own = OwnRecordCloseRandoms;
// The window: the closes of the slots (0x5e2460), of the zen and of the level, the close button's trade query and the
// trade and NPC shop queries of repair and of the personal shop.
constexpr int WindowDraws = 3 * Own + 5;
// The HUD with no windows, as in GameHudTest.
constexpr int HudBaseDraws = 19 * Own + 2 * SmallLogicalCloseRandoms + 9;
// The world's width (0x5bb8d0) with the inventory open: the shop and trade queries.
constexpr int WorldWidthDraws = 2;
constexpr std::uint16_t DigitSlot = 1;

CrtRandom Advanced(CrtRandom random, int count)
{
    for (int i = 0; i < count; ++i)
        static_cast<void>(random.Next());
    return random;
}

std::vector<std::uint16_t> SlotsOf(const SpriteBatch& sprites)
{
    std::vector<std::uint16_t> slots;
    for (const Client::Render::DrawCommand& command : sprites.Commands())
    {
        if (!command.texture || command.texture == Client::Tests::TextMarkerSlot)
            continue;
        for (std::size_t quad = 0; quad < command.vertexCount / 6; ++quad)
            slots.push_back(command.texture->value);
    }
    return slots;
}

// Vertex 0 (top left, with GL's y going up) and vertex 2 of quad `index` with texture `slot`.
struct Corners
{
    Client::Render::SpriteVertex first;
    Client::Render::SpriteVertex third;
    Color color;
};

std::optional<Corners> NthQuadOf(const SpriteBatch& sprites, std::uint16_t slot, std::size_t index = 0)
{
    const auto vertices = sprites.Vertices();
    std::size_t seen = 0;
    for (const Client::Render::DrawCommand& command : sprites.Commands())
    {
        if (!command.texture || command.texture->value != slot)
            continue;
        for (std::size_t quad = 0; quad < command.vertexCount / 6; ++quad)
        {
            const std::size_t first = command.firstVertex + quad * 6;
            if (seen++ == index)
                return Corners{vertices[first], vertices[first + 2], vertices[first].color};
        }
    }
    return std::nullopt;
}

// The UI pass's made-up models: a triangle with vertex 0 at its bone's origin, which falls on the object's position.
class UiModels final : public Client::Models::IModelLibrary
{
public:
    UiModels()
    {
        Client::Data::Models::Mesh mesh;
        mesh.vertices = {{0, {0, 0, 0}}, {0, {1, 0, 0}}, {0, {0, 1, 0}}};
        mesh.normals = {{0, {0, 0, 1}, 0}};
        mesh.texCoords = {{0, 0}};
        mesh.triangles = {{3, {0, 1, 2, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}}};
        mesh.textureName = "plain.jpg";
        model.meshes = {mesh};
        Client::Models::SkeletonBone bone;
        bone.actions = {{Client::Models::BoneKey{}}};
        skeleton.bones = {bone};
        skeleton.actions = {Client::Models::ActionInfo{.keyCount = 1}};
    }
    const Client::Data::Models::Model* Find(Client::Models::ModelId) const override
    {
        return &model;
    }
    const Client::Models::Skeleton* SkeletonOf(Client::Models::ModelId) const override
    {
        return &skeleton;
    }
    std::span<const Client::Render::TextureSlot> MeshTextures(Client::Models::ModelId) const override
    {
        return textures;
    }
    Client::Data::Models::Model model;
    Client::Models::Skeleton skeleton;
    std::array<Client::Render::TextureSlot, 1> textures{Client::Render::TextureSlot{0x400}};
};

class GameInventoryHudTest : public Client::Tests::GoldenDataTest
{
protected:
    void SetUp() override
    {
        GoldenDataTest::SetUp();
        ASSERT_FALSE(HasFatalFailure());
        auto loaded = Client::Tables::LoadGameData(m_dataRoot);
        ASSERT_TRUE(loaded.HasValue()) << loaded.Error();
        data = std::move(loaded).Value();
        // As world entry and an F3 10 with no items leave it.
        Client::Kernel::OwnCharacterRecord& own = world.OwnCharacter();
        own.ClearEquipmentForEntry();
        own.life = 100;
        own.maxLife = 100;
        own.mana = 100;
        own.maxMana = 100;
        world.Inventory().ClearForInventoryReply();
        world.Runtime().panels.inventory = 1;
        sprites.Begin(Screen640);
    }

    GameHudContext Context()
    {
        Client::Kernel::WorldRuntime& runtime = world.Runtime();
        Client::Kernel::ActorState& hero = world.Hero();
        return GameHudContext{.world = world,
                              .random = random,
                              .ownRecordDepth = runtime.ownRecordDepth,
                              .cellXDepth = hero.cellXAccesses,
                              .cellYDepth = hero.cellYAccesses,
                              .hud = runtime.hud,
                              .crywolfSandOverlay = runtime.crywolf.sandOverlay,
                              .sound = sound,
                              .sprites = sprites,
                              .text = text,
                              .texts = &*data.text,
                              .items = &data.items->table,
                              .skills = &*data.skills,
                              .worldClock = worldClock,
                              .mouse = mouse};
    }

    Client::Core::Expected<void, std::string> Window()
    {
        Client::Hud::GamePanels panels(world, random);
        GameHudContext context = Context();
        return Client::Hud::DrawTheInventory(context, panels.Registry());
    }

    Client::Core::Expected<void, std::string> GridItems()
    {
        Client::Hud::GamePanels panels(world, random);
        GameHudContext context = Context();
        return Client::Hud::DrawTheGridItems(context, panels.Registry());
    }

    Client::Core::Expected<void, std::string> GridNumbers()
    {
        Client::Hud::GamePanels panels(world, random);
        GameHudContext context = Context();
        return Client::Hud::DrawTheGridNumbers(context, panels.Registry());
    }

    Client::Core::Expected<void, std::string> UiItem(std::int32_t type, std::uint32_t level, std::uint8_t excellent,
                                                     std::uint8_t set)
    {
        GameHudContext context = Context();
        return Client::Hud::DrawUiItem(context, Client::Items::UiItemBox{}, type, level, excellent, set, false);
    }

    // An item of `width` × `height` from cell `origin`, as 0x5bc740 leaves it: the type in all of them and +0x3c at 1
    // only in the top left one.
    void PutItem(std::size_t origin, std::uint16_t type, std::size_t width = 1, std::size_t height = 1,
                 std::uint8_t durability = 0, std::uint32_t level = 0)
    {
        for (std::size_t y = 0; y < height; ++y)
        {
            for (std::size_t x = 0; x < width; ++x)
            {
                ItemState& item = world.Inventory().cells[origin + y * Client::Kernel::InventoryColumns + x];
                item.id = type;
                item.durability = durability;
                item.scalar1Low = level;
                item.occupied = x == 0 && y == 0 ? 1u : 0u;
            }
        }
    }

    // The UI pass of `screen` with the made-up models; the two batch ends are noted in `events`.
    Client::Items::UiItemResources UiItems(Client::Core::PixelSize screen)
    {
        special.random = &random;
        special.worldClock = worldClock;
        itemContext.special = &special;
        itemContext.selections = &world.Runtime().itemRender.selections;
        auto pass = Client::Items::MakeUiItemPass(screen);
        EXPECT_TRUE(pass.HasValue());
        return Client::Items::UiItemResources{.batch = models,
                                              .models = uiModels,
                                              .poseCarry = poseCarry,
                                              .itemContext = itemContext,
                                              .flushSprites = [this] { events.emplace_back("2d"); },
                                              .flushModels = [this] { events.emplace_back("3d"); },
                                              .pass = pass.HasValue() ? pass.Value() : Client::Items::UiItemPass{}};
    }

    // Where 0x5e0720 poses the item of box `box` in `screen`, with the angles of `angles`.
    glm::vec3 PlacedAt(Client::Core::PixelSize screen, std::int32_t type, std::uint32_t level,
                       const Client::Items::UiItemBox& box, bool always, glm::vec3& angles)
    {
        const auto pass = Client::Items::MakeUiItemPass(screen);
        const auto anchor = Client::Items::UiItemAnchor(type, level, box);
        const auto call = Client::Items::UiItemCallOf(type, level);
        if (!pass.HasValue() || !anchor.HasValue() || !call)
        {
            ADD_FAILURE() << "sin el punto del ítem " << type;
            return {};
        }
        const auto point =
            Client::Scenes::UnprojectGamePoint(pass.Value().projection, anchor.Value().x, anchor.Value().y);
        const auto placed =
            Client::Items::PlaceUiItem(*call, point, always, always, worldClock, &data.items->table, angles);
        if (!placed.HasValue())
        {
            ADD_FAILURE() << placed.Error();
            return {};
        }
        return placed.Value().position;
    }

    std::vector<glm::vec3> TriangleOrigins() const
    {
        std::vector<glm::vec3> origins;
        const auto vertices = models.Vertices();
        for (std::size_t index = 0; index + 2 < vertices.size(); index += 3)
            origins.push_back(vertices[index].position);
        return origins;
    }

    std::string TextAt(std::size_t index) const
    {
        return std::string(Client::Ui::TextAt(&*data.text, index).value_or("?"));
    }

    WorldState world;
    CrtRandom random{Seed};
    std::optional<Client::Audio::SoundId> sound;
    SpriteBatch sprites;
    Client::Tests::RecordingTextDrawer text{sprites};
    float worldClock = 0.0f;
    Client::Ui::Point mouse{};
    Client::Tables::GameData data;
    UiModels uiModels;
    Client::Render::ModelBatch models;
    Client::Kernel::PoseBuffer poseCarry;
    Client::Actors::Drawing::ItemSpecialDrawContext special;
    Client::Actors::Drawing::ItemDrawContext itemContext;
    std::vector<std::string> events;
};

// 0x5f2340 with no character or party: the window at (0x1c2, 0), the twelve equipment backgrounds (the helm with class
// 0), the 64 empty cells, the zen background, close and the set banner; not repair or the personal shop, with the
// level at 0. The same in Lorencia and in Devias: the window does not look at the map.
TEST_F(GameInventoryHudTest, TheWindowDrawsItsQuadsAndSpendsItsRandomsInLorenciaAndDevias)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        world.Runtime().map = map;
        sprites.Begin(Screen640);
        text.texts.clear();
        random = CrtRandom{Seed};
        const CrtRandom expected = Advanced(random, WindowDraws);

        const auto drawn = Window();

        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
        EXPECT_EQ(random.State(), expected.State());
        EXPECT_EQ(world.Runtime().ownRecordDepth, 0);
        std::vector<std::uint16_t> slots{0x104, 0x105, 0x113, 0x112, 0x107, 0x108, 0x109,
                                         0x10a, 0x114, 0x10b, 0x10c, 0x10d, 0x10e, 0x10e};
        slots.insert(slots.end(), 64, std::uint16_t{0x115});
        slots.insert(slots.end(), {0x10f, 0x118, 0x652});
        EXPECT_EQ(SlotsOf(sprites), slots);
        EXPECT_EQ(world.Runtime().hud.inventoryX, 0x1c2);
        EXPECT_EQ(world.Runtime().hud.inventoryY, 0);
    }
    // The top background: 190 × 256 from (450, 0); the first cell at (465, 200); the zen at (500, 367.7).
    const auto back = NthQuadOf(sprites, 0x104);
    ASSERT_TRUE(back.has_value());
    EXPECT_FLOAT_EQ(back->first.x, 450.0f);
    EXPECT_FLOAT_EQ(back->first.y, 480.0f);
    EXPECT_FLOAT_EQ(back->third.x, 640.0f);
    EXPECT_FLOAT_EQ(back->third.y, 224.0f);
    const auto box = NthQuadOf(sprites, 0x115);
    ASSERT_TRUE(box.has_value());
    EXPECT_FLOAT_EQ(box->first.x, 465.0f);
    EXPECT_FLOAT_EQ(box->first.y, 280.0f);
    const auto zen = NthQuadOf(sprites, 0x10f);
    ASSERT_TRUE(zen.has_value());
    EXPECT_FLOAT_EQ(zen->first.x, 500.0f);
    EXPECT_NEAR(zen->first.y, 480.0f - 367.7f, 1e-4f);
    // The title (Text 223) in a box of 120 from 485, bold and white on gray; "Zen" (224) at (470, 370) and the number
    // at (510, 370), with its color; the set banner (989) twice, centered at 546 and 545.
    ASSERT_EQ(text.texts.size(), 5u);
    const std::string title = TextAt(223);
    EXPECT_EQ(text.texts[0].text, title);
    const int titleWidth = static_cast<int>(title.size()) * 6;
    EXPECT_EQ(text.texts[0].x, 485);
    EXPECT_EQ(text.texts[0].y, 12);
    EXPECT_TRUE(text.texts[0].windowed);
    EXPECT_EQ(text.texts[0].firstColumn, -((120 - titleWidth) / 2));
    EXPECT_EQ(text.texts[0].columns, titleWidth + 2 * ((120 - titleWidth) / 2));
    EXPECT_EQ(text.texts[0].style.font, Client::Render::Text::FontKind::Bold);
    EXPECT_EQ(text.texts[0].style.colors.background, 0xff141414u);
    EXPECT_EQ(text.texts[1].text, TextAt(224));
    EXPECT_EQ(text.texts[1].x, 470);
    EXPECT_EQ(text.texts[1].y, 370);
    EXPECT_EQ(text.texts[1].style.colors.text, 0xff96dcffu);
    EXPECT_EQ(text.texts[1].style.colors.background, 0xff282828u);
    EXPECT_EQ(text.texts[2].text, "0");
    EXPECT_EQ(text.texts[2].x, 510);
    const std::string set = "[" + TextAt(989) + "]";
    EXPECT_EQ(text.texts[3].text, set);
    EXPECT_EQ(text.texts[3].x, 0x1c2 + 0x60 - static_cast<int>(set.size()) * 6 / 2);
    EXPECT_EQ(text.texts[3].y, 25);
    EXPECT_EQ(text.texts[3].style.colors.text, 0xff000000u);
    EXPECT_EQ(text.texts[4].x, 0x1c2 + 0x5f - static_cast<int>(set.size()) * 6 / 2);
    EXPECT_EQ(text.texts[4].y, 24);
    EXPECT_EQ(text.texts[4].style.colors.text, 0xffffffffu);
}

// 0x5f240b..0x5f2459: the title goes in mode 2 in a box of W · 120 / 640 pixels from x + 0x23, with the text in column
// (box − cx) / 2 and the background over the whole box (P-C70 b): 120 in 640 × 480, 192 in 1024 × 768 and 300 in
// 1600 × 1200, which TextCache splits into two boxes (TextCacheTest.AModeTwoBoxPast256ColumnsGoesInBoxesOf256). With
// the party open, from 0x104 + 0x23. The same in Lorencia and in Devias.
TEST_F(GameInventoryHudTest, TheTitleFillsItsWholeBoxInThreeResolutions)
{
    struct Case
    {
        Client::Core::PixelSize screen;
        int box = 0;
        std::uint8_t party = 0;
    };
    constexpr std::array<Case, 4> Cases{{
        {.screen = Screen640, .box = 120, .party = 0},
        {.screen = {1024, 768}, .box = 192, .party = 0},
        {.screen = {1600, 1200}, .box = 300, .party = 0},
        {.screen = {1600, 1200}, .box = 300, .party = 1},
    }};
    const int titleWidth = static_cast<int>(TextAt(223).size()) * Client::Tests::RecordingTextDrawer::LetterWidth;
    for (const std::int32_t map : {0, 2})
    {
        for (const Case& probe : Cases)
        {
            SCOPED_TRACE(testing::Message()
                         << "mapa " << map << ", " << probe.screen.width << " de ancho, party " << int{probe.party});
            world.Runtime().map = map;
            world.Runtime().panels.party = probe.party;
            sprites.Begin(probe.screen);
            text.texts.clear();

            ASSERT_TRUE(Window().HasValue());

            ASSERT_FALSE(text.texts.empty());
            const int column = (probe.box - titleWidth) / 2;
            EXPECT_EQ(text.texts[0].x, probe.party != 0 ? 0x104 + 0x23 : 0x1c2 + 0x23);
            EXPECT_EQ(text.texts[0].y, 12);
            EXPECT_TRUE(text.texts[0].windowed);
            EXPECT_EQ(text.texts[0].firstColumn, -column);
            EXPECT_EQ(text.texts[0].columns, titleWidth + 2 * column);
        }
    }
}

// With the character or the party open, the window goes at x = 0x104.
TEST_F(GameInventoryHudTest, TheWindowMovesLeftWithThePartyOpen)
{
    world.Runtime().panels.party = 1;

    ASSERT_TRUE(Window().HasValue());

    EXPECT_EQ(world.Runtime().hud.inventoryX, 0x104);
    const auto back = NthQuadOf(sprites, 0x104);
    ASSERT_TRUE(back.has_value());
    EXPECT_FLOAT_EQ(back->first.x, 260.0f);
}

// 0x5f26a7..0x5f27ef: the zen's color goes by how much it is.
TEST_F(GameInventoryHudTest, TheZenGetsItsCommasAndItsColor)
{
    struct Case
    {
        std::uint32_t zen;
        std::string_view shown;
        std::uint32_t color;
    };
    const std::array<Case, 6> cases{{
        {999, "999", 0xff96dcffu},
        {1000, "1,000", 0xff96dcffu},
        {123456, "123,456", 0xff18c900u},
        {1234567, "1,234,567", 0xff0096ffu},
        {12345678, "12,345,678", 0xff0000ffu},
        {1234567890, "1,234,567,890", 0xff0000ffu},
    }};
    for (const Case& probe : cases)
    {
        SCOPED_TRACE(probe.zen);
        world.OwnCharacter().zen = probe.zen;
        text.texts.clear();
        ASSERT_TRUE(Window().HasValue());
        ASSERT_GE(text.texts.size(), 3u);
        EXPECT_EQ(text.texts[2].text, probe.shown);
        EXPECT_EQ(text.texts[2].style.colors.text, probe.color);
    }
}

// Repair from level 50 (with 0x78abb0c, the other texture) and the personal shop from 6; the helm has no background
// with class 3.
TEST_F(GameInventoryHudTest, TheFootButtonsFollowTheLevel)
{
    struct Case
    {
        std::uint16_t level;
        std::int32_t repairMode;
        std::vector<std::uint16_t> buttons;
    };
    const std::array<Case, 4> cases{{
        {5, 0, {0x118, 0x652}},
        {6, 0, {0x118, 0x657, 0x652}},
        {50, 0, {0x118, 0x11e, 0x657, 0x652}},
        {50, 1, {0x118, 0x11f, 0x657, 0x652}},
    }};
    for (const Case& probe : cases)
    {
        SCOPED_TRACE(probe.level);
        world.OwnCharacter().level = probe.level;
        world.Runtime().panels.repairMode = probe.repairMode;
        sprites.Begin(Screen640);
        ASSERT_TRUE(Window().HasValue());
        const std::vector<std::uint16_t> slots = SlotsOf(sprites);
        const std::vector<std::uint16_t> tail(slots.begin() + 14 + 64 + 1, slots.end());
        EXPECT_EQ(tail, probe.buttons);
    }
    world.OwnCharacter().characterClass = 3;
    sprites.Begin(Screen640);
    ASSERT_TRUE(Window().HasValue());
    EXPECT_FALSE(NthQuadOf(sprites, 0x107).has_value());
}

// With the mouse over close, its help (Text 225) at (_ftol(x + 20), _ftol(395) − 13).
TEST_F(GameInventoryHudTest, TheCloseButtonShowsItsHelp)
{
    mouse = {0x1c2 + 20 + 5, 395 + 5};

    ASSERT_TRUE(Window().HasValue());

    ASSERT_EQ(text.texts.size(), 6u);
    EXPECT_EQ(text.texts[3].text, TextAt(225));
    EXPECT_EQ(text.texts[3].x, 0x1c2 + 20);
    EXPECT_EQ(text.texts[3].y, 395 - 13);
    EXPECT_EQ(text.texts[3].style.font, Client::Render::Text::FontKind::Normal);
}

// Each equipment slot and each cell takes the color of its item's warning (0x5e22d0): the 8, broken, in red.
TEST_F(GameInventoryHudTest, TheSlotsTakeTheColorOfTheirWarning)
{
    world.OwnCharacter().equipment[8].warning = 8;
    world.Inventory().cells[0].warning = 2;

    ASSERT_TRUE(Window().HasValue());

    const auto helper = NthQuadOf(sprites, 0x113);
    ASSERT_TRUE(helper.has_value());
    EXPECT_EQ(helper->color, (Color{1.0f, 0.0f, 0.0f, 1.0f}));
    const auto cell = NthQuadOf(sprites, 0x115);
    ASSERT_TRUE(cell.has_value());
    EXPECT_EQ(cell->color, (Color{0.6f, 0.7f, 1.0f, 1.0f}));
}

// 0x5f7610 with the inventory: the record opened around the equipment, the rand() of 0x607b60 per model (the 0xe is
// the 0x220, one; the 0xa05 and the 0xc0e, the 0xc17 and the 0xe20, two; the 0xa07, the 0xc19, one), the two of
// 0x609640 of the 0xc0f (0xe21) and the trade and shop queries. Cells other than the top left one do not count, and
// the 0x1a13 with the level at 3 draws nothing.
TEST_F(GameInventoryHudTest, TheGridItemsSpendTheRandomsOfTheirModels)
{
    PutItem(0, 0x000e, 1, 3);
    PutItem(1, 0x0c0e, 2, 2);
    PutItem(3, 0x0c0f, 2, 2);
    PutItem(5, 0x0a05, 1, 3);
    PutItem(6, 0x1a13, 1, 1, 0, 3u << 3u);
    ItemState& weapon = world.OwnCharacter().equipment[0];
    weapon.id = 0x0a07;
    weapon.occupied = 1;
    const CrtRandom expected = Advanced(random, Own + 1 + 2 + 2 + 2 + 1 + 2);

    const auto drawn = GridItems();

    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    EXPECT_EQ(random.State(), expected.State());
    EXPECT_EQ(world.Runtime().ownRecordDepth, 0);
}

// The set pass (0x60cc29): with the set at 1 or 2, without excellent and with sin(0.001 · T) · 0.5 + 0.4 at 0.01 or
// less, one rand() % 100 in the model's +0xc0.
TEST_F(GameInventoryHudTest, TheSetPassDrawsItsRandomWhenThePulseIsOff)
{
    worldClock = 4712.0f;
    CrtRandom copy = random;
    const int selected = copy.Next() % 100;

    ASSERT_TRUE(UiItem(0x0001, 0, 0, 1).HasValue());

    EXPECT_EQ(random.State(), copy.State());
    EXPECT_EQ(world.Runtime().itemRender.selections.at(0x213), selected);
    const CrtRandom before = random;
    ASSERT_TRUE(UiItem(0x0001, 0, 1, 1).HasValue());
    ASSERT_TRUE(UiItem(0x0001, 0, 0, 3).HasValue());
    worldClock = 0.0f;
    ASSERT_TRUE(UiItem(0x0001, 0, 0, 2).HasValue());
    EXPECT_EQ(random.State(), before.State());
}

// The wings 0x1806 (the model 0x1a18) stop before their joints, with their address. Without the models MuRet does not
// have the bones of the 20 joints of the 0x1a18 (0x60c3b6), which the next step uses with their rand(): without them
// it stops.
TEST_F(GameInventoryHudTest, WithoutModelsTheWingsOfDarknessStopWithTheirAddress)
{
    PutItem(0, 0x1806, 2, 2);
    auto wings = GridItems();
    ASSERT_FALSE(wings.HasValue());
    EXPECT_NE(wings.Error().find("0x60c3b6"), std::string::npos) << wings.Error();
}

// 0x60c3b6..0x60c598 with the models: the 0x1806 (item (12, 6), Wing of Darkness, the model 0x1a18 of the Potion
// Girl's shop) hangs from its local object, the one on the stack of 0x5e0720, ten pairs of joints (0x614/0xe and
// 0x613/4) and ten sprites 0x62b, without rand() (07 Q-C1821).
TEST_F(GameInventoryHudTest, TheWingsOfDarknessHangTheirJointsFromTheUiLocalObject)
{
    PutItem(0, 0x1806, 2, 2);
    uiModels.skeleton.bones.resize(31, uiModels.skeleton.bones[0]);
    auto glow = std::make_unique<Client::Particles::FrameSprites>();
    special.sprites = glow.get();
    std::vector<Client::Kernel::JointRequest> requests;
    models.Begin(Client::Render::Pass3D{});
    Client::Items::UiItemResources resources = UiItems(Screen640);
    itemContext.originalFrame = true;
    itemContext.poseBones = [this]() { return std::span<const Client::Math::BoneTransform>(poseCarry.bones); };
    itemContext.createJoint =
        [&requests](const Client::Kernel::JointRequest& request) -> Client::Core::Expected<void, std::string>
    {
        requests.push_back(request);
        return {};
    };
    const CrtRandom unchanged = random;
    {
        GameHudContext context = Context();
        context.uiItems = &resources;
        const auto drawn = Client::Hud::DrawUiItem(context, {.x = 465.0f, .y = 200.0f, .width = 40.0f, .height = 40.0f},
                                                   0x1806, 0, 0, 0, false);
        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    }
    Client::Items::CloseUiItemPass(resources);
    EXPECT_EQ(random.State(), unchanged.State());
    ASSERT_EQ(requests.size(), 20u);
    for (std::size_t index = 0; index < requests.size(); ++index)
    {
        EXPECT_EQ(requests[index].type, index % 2 == 0 ? 0x614 : 0x613) << index;
        EXPECT_EQ(requests[index].subtype, index % 2 == 0 ? 14 : 4) << index;
        EXPECT_EQ(requests[index].ownerContext, Client::Items::UiItemLocalObject()) << index;
    }
    EXPECT_EQ(std::ranges::count_if(glow->Sprites(), [](const auto& sprite) { return sprite.alive; }), 10);
}

// 0x5f769b and 0x5f7756 (CL9 milestone 6): the shop grid and the warehouse grid (0x78a7070, 8 × 15) go from the NPC
// window's corner plus (15, 50); the shop with the rand() of its protected byte. A 0xc0e draws the two of its model in
// either.
TEST_F(GameInventoryHudTest, TheShopAndWarehouseGridsSpendTheRandomsOfTheirItems)
{
    world.Runtime().ui.npcWindowX = 0x104;
    for (const bool shop : {true, false})
    {
        SCOPED_TRACE(shop ? "la tienda" : "el almacén");
        world.Runtime().panels.npcShop = shop ? 1 : 0;
        world.Runtime().panels.warehouse = shop ? 0 : 1;
        world.Runtime().npcWindows.grid = {};
        const CrtRandom before = random;
        ASSERT_TRUE(GridItems().HasValue());
        const CrtRandom empty = random;

        ItemState& item = world.Runtime().npcWindows.grid[2 * 8 + 1];
        item.id = 0x0c0e;
        item.occupied = 1;
        random = before;
        const auto drawn = GridItems();

        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
        EXPECT_EQ(random.State(), Advanced(empty, 2).State());
    }
}

// 0x5f7973 and 0x5f7a2e: the numbers of the shop and the warehouse, in the cell (column · 20 + x + 15, row · 20 + y +
// 50) from the NPC corner.
TEST_F(GameInventoryHudTest, TheShopAndWarehouseNumbersStartAtTheNpcCorner)
{
    world.Runtime().panels.inventory = 0;
    world.Runtime().ui.npcWindowX = 0x104;
    world.Runtime().ui.npcWindowY = 0;
    ItemState& potion = world.Runtime().npcWindows.grid[3 * 8 + 2];
    potion.id = 0x1c00;
    potion.durability = 3;
    potion.occupied = 1;
    for (const bool shop : {true, false})
    {
        SCOPED_TRACE(shop ? "la tienda" : "el almacén");
        world.Runtime().panels.npcShop = shop ? 1 : 0;
        world.Runtime().panels.warehouse = shop ? 0 : 1;
        sprites.Begin(Screen640);

        ASSERT_TRUE(GridNumbers().HasValue());

        EXPECT_EQ(SlotsOf(sprites), (std::vector<std::uint16_t>{DigitSlot}));
        const auto three = NthQuadOf(sprites, DigitSlot, 0);
        ASSERT_TRUE(three.has_value());
        EXPECT_FLOAT_EQ(three->first.x, 260.0f + 15.0f + 40.0f + 13.0f - 4.5f);
        EXPECT_FLOAT_EQ(three->third.y, 480.0f - 50.0f - 60.0f - 10.0f);
    }
}

// 0x5f7840: the potions with 2 or more (9 × 10, 13 to the right) and the jewel bundles with ((level + 1) · 5) · 2
// (7 × 8, 14 to the right), in (1, 0.9, 0.7) and with alpha blending.
TEST_F(GameInventoryHudTest, TheNumbersShowThePotionsAndTheJewelBundles)
{
    world.Runtime().hud.inventoryX = 0x1c2;
    PutItem(0, 0x1c00, 1, 1, 3);
    PutItem(1, 0x181e, 1, 1, 0, 2u << 3u);
    PutItem(2, 0x1c00, 1, 1, 1);
    PutItem(3, 0x000e, 1, 3, 5);

    ASSERT_TRUE(GridNumbers().HasValue());

    EXPECT_EQ(SlotsOf(sprites), (std::vector<std::uint16_t>{DigitSlot, DigitSlot, DigitSlot}));
    const auto three = NthQuadOf(sprites, DigitSlot, 0);
    ASSERT_TRUE(three.has_value());
    EXPECT_FLOAT_EQ(three->first.x, 465.0f + 13.0f - 4.5f);
    EXPECT_FLOAT_EQ(three->third.y, 480.0f - 210.0f);
    EXPECT_EQ(three->color, (Color{1.0f, 0.9f, 0.7f, 1.0f}));
    const auto thirty = NthQuadOf(sprites, DigitSlot, 1);
    ASSERT_TRUE(thirty.has_value());
    EXPECT_FLOAT_EQ(thirty->first.x, 485.0f + 14.0f - 7.0f);
    EXPECT_FLOAT_EQ(thirty->third.y, 480.0f - 208.0f);
}

// 0x5f77c6 and 0x5f7a9e: with the chaos box open (and the inventory closed), its 8 × 4 grid from the NPC window's
// corner plus (15, 110): the models' rand() (the 0xe one, the 0xc0e two) after the shop and trade queries, and the
// numbers of the potion with 3. In Lorencia, Devias and Noria.
TEST_F(GameInventoryHudTest, TheChaosBoxGridDrawsFromTheNpcWindowCorner)
{
    for (const std::int32_t map : {0, 2, 3})
    {
        SCOPED_TRACE(map);
        auto& runtime = world.Runtime();
        runtime.map = map;
        runtime.panels.inventory = 0;
        runtime.panels.chaosBox = 1;
        runtime.ui.npcWindowX = 0x104;
        runtime.ui.npcWindowY = 0;
        auto& box = runtime.npcWindows.chaosBox;
        for (ItemState& cell : box)
            cell = ItemState{.id = 0xffff};
        box[0] = ItemState{.id = 0x000e, .occupied = 1};
        box[8] = ItemState{.id = 0x000e};
        box[2] = ItemState{.id = 0x0c0e, .occupied = 1};
        box[9] = ItemState{.id = 0x1c00, .durability = 3, .occupied = 1};
        const CrtRandom expected = Advanced(random, 2 + 1 + 2);
        const auto drawn = GridItems();
        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
        EXPECT_EQ(random.State(), expected.State());

        sprites.Begin(Screen640);
        ASSERT_TRUE(GridNumbers().HasValue());
        const auto three = NthQuadOf(sprites, DigitSlot, 0);
        ASSERT_TRUE(three.has_value());
        EXPECT_FLOAT_EQ(three->first.x, 0x104 + 15.0f + 20.0f + 13.0f - 4.5f);
        EXPECT_FLOAT_EQ(three->third.y, 480.0f - 140.0f);
        EXPECT_FALSE(NthQuadOf(sprites, DigitSlot, 1).has_value());
    }
}

// 0x44ad40: with 0x724580 at 3, the trainer's box (0x723c80) at (275, 110), without looking at the NPC window, and its
// numbers (the 0x8000 open is enough for 0x5f7840). In another mode, nothing.
TEST_F(GameInventoryHudTest, TheTrainerBoxGridDrawsAtItsFixedCorner)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        auto& runtime = world.Runtime();
        runtime.map = map;
        runtime.panels.inventory = 0;
        runtime.panels.delegated.trainer = 3;
        runtime.ui.npcWindowX = 0x40;
        runtime.ui.npcWindowY = 0x20;
        auto& box = runtime.npcWindows.trainerBox;
        for (ItemState& cell : box)
            cell = ItemState{.id = 0xffff};
        box[0] = ItemState{.id = 0x000e, .occupied = 1};
        box[8] = ItemState{.id = 0x000e};
        box[2] = ItemState{.id = 0x0c0e, .occupied = 1};
        box[9] = ItemState{.id = 0x1c00, .durability = 3, .occupied = 1};
        const CrtRandom expected = Advanced(random, 2 + 1 + 2);
        const auto drawn = GridItems();
        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
        EXPECT_EQ(random.State(), expected.State());

        sprites.Begin(Screen640);
        ASSERT_TRUE(GridNumbers().HasValue());
        const auto three = NthQuadOf(sprites, DigitSlot, 0);
        ASSERT_TRUE(three.has_value());
        EXPECT_FLOAT_EQ(three->first.x, 275.0f + 20.0f + 13.0f - 4.5f);
        EXPECT_FLOAT_EQ(three->third.y, 480.0f - 140.0f);
        EXPECT_FALSE(NthQuadOf(sprites, DigitSlot, 1).has_value());

        // With mode 2, only the rand() of the shop and trade IsOpen.
        runtime.panels.delegated.trainer = 2;
        const CrtRandom protectedOnly = Advanced(random, 2);
        ASSERT_TRUE(GridItems().HasValue());
        EXPECT_EQ(random.State(), protectedOnly.State());
    }
}

// 0x5f7b10: the cursor item draws the rand() of its model; without an item, nothing.
TEST_F(GameInventoryHudTest, TheCursorItemSpendsTheRandomsOfItsModel)
{
    world.Runtime().cursorItem.id = 0x0c0e;
    const CrtRandom unchanged = random;
    {
        GameHudContext context = Context();
        ASSERT_TRUE(Client::Hud::DrawTheCursorItem(context).HasValue());
    }
    EXPECT_EQ(random.State(), unchanged.State());

    world.Runtime().cursorItemCount = 1;
    const CrtRandom expected = Advanced(random, 2);
    GameHudContext context = Context();
    ASSERT_TRUE(Client::Hud::DrawTheCursorItem(context).HasValue());
    EXPECT_EQ(random.State(), expected.State());
}

// 0x633116..0x63315d and 0x6331fc..0x633220: 0x633110 draws the cursor item with the 0x94 or the 0x98 and notice 1
// (selling something valuable), and the dialogs' one (0x78a59c8) with the 0x96, the 0xa8, the 0x99 and the 0x94 with
// notice 5, which goes through the 0x96. The mix's 0x98 (notice 2) and the other notices, nothing.
TEST(DialogItemTest, TheDialogAndItsWarningChooseTheItem)
{
    using Client::Hud::DialogItemOf;
    using Source = Client::Hud::DialogItemSource;
    EXPECT_EQ(DialogItemOf(0x98, 1), Source::Cursor);
    EXPECT_EQ(DialogItemOf(0x94, 1), Source::Cursor);
    for (const std::int32_t warning : {0, 2, 3, 4, 5, 6})
        EXPECT_EQ(DialogItemOf(0x98, warning), Source::None) << warning;
    for (const std::int32_t warning : {0, 2, 3, 4, 6})
        EXPECT_EQ(DialogItemOf(0x94, warning), Source::None) << warning;
    EXPECT_EQ(DialogItemOf(0x94, 5), Source::Dialog);
    for (const std::int32_t dialog : {0x96, 0xa8, 0x99})
    {
        for (const std::int32_t warning : {0, 1, 2, 5})
            EXPECT_EQ(DialogItemOf(dialog, warning), Source::Dialog) << dialog << " " << warning;
    }
    EXPECT_EQ(DialogItemOf(0, 1), Source::None);
    EXPECT_EQ(DialogItemOf(0x97, 1), Source::None);
}

// 0x633110 without models: the rand() of 0x5e0720 of the item it chooses (the 0x0c0e, the model 0xe20: two in
// 0x607b60). With the sell dialog it takes the cursor item without looking at 0x7879fbc; with the 0x99, the dialogs'
// one; with the mix, nothing.
TEST_F(GameInventoryHudTest, TheDialogItemSpendsTheRandomsOfTheItemItShows)
{
    Client::Kernel::WorldRuntime& runtime = world.Runtime();
    runtime.cursorItem.id = 0x0c0e;
    runtime.cursorItemCount = 0;
    runtime.dialogs.current = 0x98;
    runtime.social.trade.warningKind = 2;
    const CrtRandom unchanged = random;
    {
        GameHudContext context = Context();
        ASSERT_TRUE(Client::Hud::DrawTheDialogItem(context, 2000.0f).HasValue());
    }
    EXPECT_EQ(random.State(), unchanged.State());

    runtime.social.trade.warningKind = 1;
    const CrtRandom sold = Advanced(random, 2);
    {
        GameHudContext context = Context();
        ASSERT_TRUE(Client::Hud::DrawTheDialogItem(context, 2000.0f).HasValue());
    }
    EXPECT_EQ(random.State(), sold.State());

    runtime.cursorItem.id = 0xffff;
    runtime.dialogItem.id = 0x0c0e;
    runtime.dialogs.current = 0x99;
    runtime.social.trade.warningKind = 0;
    const CrtRandom priced = Advanced(random, 2);
    GameHudContext context = Context();
    ASSERT_TRUE(Client::Hud::DrawTheDialogItem(context, 2000.0f).HasValue());
    EXPECT_EQ(random.State(), priced.State());
}

// 0x633110 with the models: its own pass (the projection of 1°, W / H, 20 and the world's far of 0x6d5d44, which the
// anchor does not use) and 0x5e1480(245, 115, 40, 60, …, 1). The box is fixed: the 0x000e is 1 × 3 in Item.bmd and
// goes in the 40 × 60 one. The earlier 2D stays below and the UI items pass goes back to its far of 2000.
TEST_F(GameInventoryHudTest, TheDialogItemDrawsInItsFixedBox)
{
    Client::Kernel::WorldRuntime& runtime = world.Runtime();
    runtime.cursorItem.id = 0x000e;
    runtime.dialogs.current = 0x98;
    runtime.social.trade.warningKind = 1;
    mouse = {260, 140};
    worldClock = 4321.0f;
    for (const Client::Core::PixelSize screen : {Screen640, Client::Core::PixelSize{1024, 768}})
    {
        SCOPED_TRACE(screen.width);
        models.Begin(Client::Render::Pass3D{});
        events.clear();
        Client::Items::UiItemResources resources = UiItems(screen);
        {
            GameHudContext context = Context();
            context.uiItems = &resources;
            const auto drawn = Client::Hud::DrawTheDialogItem(context, 3200.0f);
            ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
        }

        EXPECT_EQ(events, (std::vector<std::string>{"2d", "3d"}));
        EXPECT_FALSE(resources.open);
        EXPECT_EQ(resources.pass.pass.farPlane, 2000.0f);
        glm::vec3 angles{};
        const glm::vec3 expected =
            PlacedAt(screen, 0x000e, 0, {.x = 245.0f, .y = 115.0f, .width = 40.0f, .height = 60.0f}, true, angles);
        EXPECT_EQ(TriangleOrigins(), std::vector<glm::vec3>{expected});
    }
}

// 0x5ac1e9..0x5ac204: 0x5ac120 draws the cursor item only with 0x78abb14 at 0 and 0x78aba60 other than 2. With the
// sell dialog open (notice 1) or the own shop's price dialog (2), it does not draw it: 0x633110 draws the sell one in
// the dialog. With the whole HUD, the rand() of the 0x0c0e do not come out.
TEST_F(GameInventoryHudTest, TheCursorItemWaitsWhileItsDialogIsOpen)
{
    Client::Kernel::WorldRuntime& runtime = world.Runtime();
    runtime.cursorItem.id = 0x0c0e;
    runtime.cursorItemCount = 1;
    struct Case
    {
        std::int32_t warning = 0;
        std::int32_t shopDialog = 0;
        int cursorRandoms = 0;
    };
    for (const Case tested : {Case{0, 0, 2}, Case{1, 0, 0}, Case{0, 2, 0}, Case{0, 1, 2}})
    {
        SCOPED_TRACE(tested.warning * 10 + tested.shopDialog);
        runtime.social.trade.warningKind = tested.warning;
        runtime.social.personalShop.dialogMode = tested.shopDialog;
        random = CrtRandom{Seed};
        sprites.Begin(Screen640);
        const CrtRandom expected =
            Advanced(random, HudBaseDraws + WindowDraws + Own + 2 * WorldWidthDraws + tested.cursorRandoms);
        GameHudContext context = Context();
        const auto drawn = Client::Hud::DrawTheHud(context);
        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
        EXPECT_EQ(random.State(), expected.State());
    }
}

// The base state plus the window, the record close around the equipment of 0x5e2fc0 and the two queries the world's
// width (0x5bb8d0) makes with the inventory open (the shop and the trade), in the durability notices and in the closed
// party (0x5f0be0, which with its IsOpen(4) at 0 jumps to 0x5f12d0 and calls it).
TEST_F(GameInventoryHudTest, TheHudWithTheInventoryOpenSpendsItsRandomsInLorenciaAndDevias)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        world.Runtime().map = map;
        random = CrtRandom{Seed};
        sprites.Begin(Screen640);
        const CrtRandom expected = Advanced(random, HudBaseDraws + WindowDraws + Own + 2 * WorldWidthDraws);

        GameHudContext context = Context();
        const auto drawn = Client::Hud::DrawTheHud(context);

        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
        EXPECT_EQ(random.State(), expected.State());
        EXPECT_EQ(world.Runtime().ownRecordDepth, 0);
    }
}

// 0x5f7610 with the models (P-C70): the equipment and the grid in their boxes, in Lorencia and in Devias, in 640 × 480
// and in 1024 × 768. The first item closes the 2D and opens the UI pass, each item goes where 0x5e0720 poses it from
// its box's point (the weapon at (0x1c2 + 15, 89), of 40 × 60; the grid from (0x1c2 + 15, 200), each of Item.bmd's
// size: 1 × 3, 2 × 3 and 1 × 4 cells), and the rand() are the same as without drawing.
TEST_F(GameInventoryHudTest, TheGridItemsDrawInTheUiPassInTwoMapsAndTwoResolutions)
{
    world.Runtime().hud.inventoryX = 0x1c2;
    PutItem(0, 0x000e, 1, 3);
    PutItem(1, 0x0c0e, 2, 3);
    PutItem(5, 0x0a05, 1, 4);
    ItemState& weapon = world.OwnCharacter().equipment[0];
    weapon.id = 0x0a07;
    weapon.occupied = 1;
    for (const std::int32_t map : {0, 2})
    {
        for (const Client::Core::PixelSize screen : {Screen640, Client::Core::PixelSize{1024, 768}})
        {
            SCOPED_TRACE(testing::Message() << map << " " << screen.width);
            world.Runtime().map = map;
            random = CrtRandom{Seed};
            ASSERT_TRUE(GridItems().HasValue());
            const CrtRandom withoutModels = random;

            random = CrtRandom{Seed};
            world.Runtime().hud.uiItemAngles = {};
            models.Begin(Client::Render::Pass3D{});
            events.clear();
            Client::Items::UiItemResources resources = UiItems(screen);
            {
                Client::Hud::GamePanels panels(world, random);
                GameHudContext context = Context();
                context.uiItems = &resources;
                const auto drawn = Client::Hud::DrawTheGridItems(context, panels.Registry());
                ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
            }
            Client::Items::CloseUiItemPass(resources);

            EXPECT_EQ(random.State(), withoutModels.State());
            EXPECT_EQ(events, (std::vector<std::string>{"2d", "3d"}));
            EXPECT_EQ(models.Pass().viewport, glm::ivec4(0, 0, screen.width, screen.height));
            glm::vec3 angles{};
            const std::vector<glm::vec3> expected{
                PlacedAt(screen, 0x0a07, 0, {.x = 465.0f, .y = 89.0f, .width = 40.0f, .height = 60.0f}, false, angles),
                PlacedAt(screen, 0x000e, 0, {.x = 465.0f, .y = 200.0f, .width = 20.0f, .height = 60.0f}, false, angles),
                PlacedAt(screen, 0x0c0e, 0, {.x = 485.0f, .y = 200.0f, .width = 40.0f, .height = 60.0f}, false, angles),
                PlacedAt(screen, 0x0a05, 0, {.x = 565.0f, .y = 200.0f, .width = 20.0f, .height = 80.0f}, false, angles),
            };
            EXPECT_EQ(TriangleOrigins(), expected);
            EXPECT_EQ(world.Runtime().hud.uiItemAngles, angles);
        }
    }
}

// 0x5f7b10 with the models: the cursor item centered on the mouse, at 0.07 from the camera and turning with the clock
// (the last argument of 0x5e1480 at 1).
TEST_F(GameInventoryHudTest, TheCursorItemDrawsCenteredOnTheMouse)
{
    world.Runtime().cursorItem.id = 0x0c0e;
    world.Runtime().cursorItemCount = 1;
    mouse = {300, 200};
    worldClock = 4321.0f;
    for (const Client::Core::PixelSize screen : {Screen640, Client::Core::PixelSize{1024, 768}})
    {
        SCOPED_TRACE(screen.width);
        models.Begin(Client::Render::Pass3D{});
        events.clear();
        Client::Items::UiItemResources resources = UiItems(screen);
        {
            GameHudContext context = Context();
            context.uiItems = &resources;
            const auto drawn = Client::Hud::DrawTheCursorItem(context);
            ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
        }
        Client::Items::CloseUiItemPass(resources);

        EXPECT_EQ(events, (std::vector<std::string>{"2d", "3d"}));
        glm::vec3 angles{};
        const glm::vec3 expected =
            PlacedAt(screen, 0x0c0e, 0, {.x = 280.0f, .y = 170.0f, .width = 40.0f, .height = 60.0f}, true, angles);
        EXPECT_EQ(TriangleOrigins(), std::vector<glm::vec3>{expected});
    }
}

// CL9 milestone 11, 0x5f70a0: the models of the window of 0x78abb00 with Item.bmd's size: the 0x1c15 (level 0, or with
// +4 at 0xf with 2 and 4; with 5, type 0) at (540, 298) and at (540, 333), and with 4 only the lower one. The same,
// with the same rand(), as those calls to 0x5e1480 one by one. Rena's glow leaves its unlit mesh published for the
// following drawings: each case draws once before. In Lorencia and in Devias.
TEST_F(GameInventoryHudTest, TheChipWindowDrawsItsModels)
{
    struct Case
    {
        std::int32_t window;
        std::int32_t type;
        std::uint32_t level;
        bool upper;
    };
    // A UI pass with `draw` and what it left in the models' batch.
    const auto pass = [this](const auto& draw)
    {
        auto glow = std::make_unique<Client::Particles::FrameSprites>();
        special.sprites = glow.get();
        world.Runtime().hud.uiItemAngles = {};
        models.Begin(Client::Render::Pass3D{});
        Client::Items::UiItemResources resources = UiItems(Screen640);
        GameHudContext context = Context();
        context.uiItems = &resources;
        const auto drawn = draw(context);
        EXPECT_TRUE(drawn.HasValue()) << drawn.Error();
        Client::Items::CloseUiItemPass(resources);
        return TriangleOrigins();
    };
    for (const std::int32_t map : {0, 2})
    {
        for (const Case probe :
             {Case{1, 0x1c15, 0, true}, Case{2, 0x1c15, 0xf, true}, Case{4, 0x1c15, 0xf, false}, Case{5, 0, 0, true}})
        {
            SCOPED_TRACE(testing::Message() << map << " " << probe.window);
            world.Runtime().map = map;
            world.Runtime().panels.field78abb00 = probe.window;
            const auto chips = [](GameHudContext& context) { return Client::Events::DrawTheEventChipItems(context); };
            static_cast<void>(pass(chips));

            random = CrtRandom{Seed};
            const std::vector<glm::vec3> drawn = pass(chips);
            const CrtRandom afterChips = random;

            random = CrtRandom{Seed};
            const std::vector<glm::vec3> expected = pass(
                [&probe](GameHudContext& context) -> Client::Core::Expected<void, std::string>
                {
                    auto box = Client::Hud::ItemSizeOf(context, probe.type, "test");
                    if (!box.HasValue())
                        return Client::Core::Unexpected(box.Error());
                    box.Value().x = 540.0f;
                    if (probe.upper)
                    {
                        box.Value().y = 298.0f;
                        if (auto upper =
                                Client::Hud::DrawUiItem(context, box.Value(), probe.type, probe.level, 0, 0, false);
                            !upper.HasValue())
                            return upper;
                    }
                    box.Value().y = 333.0f;
                    return Client::Hud::DrawUiItem(context, box.Value(), probe.type, probe.level, 0, 0, false);
                });
            EXPECT_EQ(random.State(), afterChips.State());
            ASSERT_FALSE(drawn.empty());
            EXPECT_EQ(drawn, expected);
        }
    }
}

// The whole HUD with the inventory open and the models: the same rand() as without drawing, and the UI pass that the
// weapon opens and the end of 0x5ac120 closes.
TEST_F(GameInventoryHudTest, TheHudDrawsTheUiItemsBetweenItsTwoDimensionalParts)
{
    ItemState& weapon = world.OwnCharacter().equipment[0];
    weapon.id = 0x0a07;
    weapon.occupied = 1;
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        world.Runtime().map = map;
        random = CrtRandom{Seed};
        sprites.Begin(Screen640);
        const CrtRandom expected = Advanced(random, HudBaseDraws + WindowDraws + Own + 2 * WorldWidthDraws + 1);
        models.Begin(Client::Render::Pass3D{});
        events.clear();
        Client::Items::UiItemResources resources = UiItems(Screen640);

        GameHudContext context = Context();
        context.uiItems = &resources;
        const auto drawn = Client::Hud::DrawTheHud(context);

        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
        EXPECT_EQ(random.State(), expected.State());
        EXPECT_EQ(events, (std::vector<std::string>{"2d", "3d"}));
        EXPECT_FALSE(resources.open);
        EXPECT_EQ(TriangleOrigins().size(), 1u);
    }
}
} // namespace
