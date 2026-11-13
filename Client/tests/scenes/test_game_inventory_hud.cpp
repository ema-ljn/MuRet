#include "Scenes/GameInventoryHud.h"

#include "App/WindowState.h"
#include "Core/Expected.h"
#include "Data/Models/Model.h"
#include "Render/ModelBatch.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/LogicalAccess.h"
#include "Render/Models/Skeleton.h"
#include "Render/Pass3D.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/TextureSlot.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/TextStyle.h"
#include "Scenes/ActorState.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/FaunaDrawing.h"
#include "Scenes/GameData.h"
#include "Scenes/GameHud.h"
#include "Scenes/GameItemSpecialDrawing.h"
#include "Scenes/GameProjection.h"
#include "Scenes/GamePanels.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/OwnInventory.h"
#include "Scenes/UiItemDrawing.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/GameText.h"
#include "UI/Widget.h"
#include "World/ItemState.h"
#include "support/GoldenDir.h"
#include "support/RecordingTextDrawer.h"

#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_int4.hpp>
#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// El inventario en el HUD (CL8 hito 4): la ventana de 0x5f2340, los ítems de la UI (0x5e1480: sin los modelos, sus
// rand(); con unos inventados, el pase de 0x5ac120), las cifras de 0x5f7840 y el ítem del cursor de 0x5f7b10. Los
// ítems y el zen son inventados.
namespace
{
using Client::Render::Color;
using Client::Render::SpriteBatch;
using Client::Render::Models::CrtRandom;
using Client::Render::Models::OwnRecordCloseRandoms;
using Client::Render::Models::SmallLogicalCloseRandoms;
using Client::Scenes::GameHudContext;
using Client::Scenes::WorldState;
using Client::World::ItemState;

constexpr std::uint32_t Seed = 0x13579bdfu;
constexpr Client::App::PixelSize Screen640{640, 480};
constexpr int Own = OwnRecordCloseRandoms;
// La ventana: los cierres de los lugares (0x5e2460), del zen y del nivel, la consulta del comercio del botón de
// cerrar y las del comercio y la tienda de NPC de reparar y de la tienda personal.
constexpr int WindowDraws = 3 * Own + 5;
// El HUD sin ventanas, como en GameHudTest.
constexpr int HudBaseDraws = 19 * Own + 2 * SmallLogicalCloseRandoms + 9;
// El ancho del mundo (0x5bb8d0) con el inventario abierto: las consultas de la tienda y del comercio.
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

// El vértice 0 (arriba a la izquierda, con la y de GL hacia arriba) y el 2 del quad `index` con la textura `slot`.
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

// Los modelos inventados del pase de la UI: un triángulo con el vértice 0 en el origen de su hueso, que cae en la
// posición del objeto.
class UiModels final : public Client::Scenes::IModelLibrary
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
        Client::Render::Models::SkeletonBone bone;
        bone.actions = {{Client::Render::Models::BoneKey{}}};
        skeleton.bones = {bone};
        skeleton.actions = {Client::Render::Models::ActionInfo{.keyCount = 1}};
    }
    const Client::Data::Models::Model* Find(std::uint16_t) const override
    {
        return &model;
    }
    const Client::Render::Models::Skeleton* SkeletonOf(std::uint16_t) const override
    {
        return &skeleton;
    }
    std::span<const Client::Render::TextureSlot> MeshTextures(std::uint16_t) const override
    {
        return textures;
    }
    Client::Data::Models::Model model;
    Client::Render::Models::Skeleton skeleton;
    std::array<Client::Render::TextureSlot, 1> textures{Client::Render::TextureSlot{0x400}};
};

class GameInventoryHudTest : public Client::Tests::GoldenDataTest
{
protected:
    void SetUp() override
    {
        GoldenDataTest::SetUp();
        ASSERT_FALSE(HasFatalFailure());
        auto loaded = Client::Scenes::LoadGameData(m_dataRoot);
        ASSERT_TRUE(loaded.HasValue()) << loaded.Error();
        data = std::move(loaded).Value();
        // Como lo deja la entrada al mundo y un F3 10 sin ítems.
        Client::Scenes::OwnCharacterRecord& own = world.OwnCharacter();
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
        Client::Scenes::WorldRuntime& runtime = world.Runtime();
        Client::Scenes::ActorState& hero = world.Actors()[runtime.hero];
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
        Client::Scenes::GamePanels panels(world, random);
        GameHudContext context = Context();
        return Client::Scenes::DrawTheInventory(context, panels.Registry());
    }

    Client::Core::Expected<void, std::string> GridItems()
    {
        Client::Scenes::GamePanels panels(world, random);
        GameHudContext context = Context();
        return Client::Scenes::DrawTheGridItems(context, panels.Registry());
    }

    Client::Core::Expected<void, std::string> GridNumbers()
    {
        Client::Scenes::GamePanels panels(world, random);
        GameHudContext context = Context();
        return Client::Scenes::DrawTheGridNumbers(context, panels.Registry());
    }

    Client::Core::Expected<void, std::string> UiItem(std::int32_t type, std::uint32_t level, std::uint8_t excellent,
                                                     std::uint8_t set)
    {
        GameHudContext context = Context();
        return Client::Scenes::DrawUiItem(context, Client::Scenes::UiItemBox{}, type, level, excellent, set, false);
    }

    // Un ítem de `width` × `height` desde la casilla `origin`, como lo deja 0x5bc740: el tipo en todas y +0x3c en 1
    // solo en la de arriba a la izquierda.
    void PutItem(std::size_t origin, std::uint16_t type, std::size_t width = 1, std::size_t height = 1,
                 std::uint8_t durability = 0, std::uint32_t level = 0)
    {
        for (std::size_t y = 0; y < height; ++y)
        {
            for (std::size_t x = 0; x < width; ++x)
            {
                ItemState& item = world.Inventory().cells[origin + y * Client::Scenes::InventoryColumns + x];
                item.id = type;
                item.durability = durability;
                item.scalar1Low = level;
                item.occupied = x == 0 && y == 0 ? 1u : 0u;
            }
        }
    }

    // El pase de la UI de `screen` con los modelos inventados; los dos fines de lote se anotan en `events`.
    Client::Scenes::UiItemResources UiItems(Client::App::PixelSize screen)
    {
        special.random = &random;
        special.worldClock = worldClock;
        itemContext.special = &special;
        itemContext.selections = &world.Runtime().itemRender.selections;
        auto pass = Client::Scenes::MakeUiItemPass(screen);
        EXPECT_TRUE(pass.HasValue());
        return Client::Scenes::UiItemResources{.batch = models,
                                               .models = uiModels,
                                               .poseCarry = poseCarry,
                                               .itemContext = itemContext,
                                               .flushSprites = [this] { events.emplace_back("2d"); },
                                               .flushModels = [this] { events.emplace_back("3d"); },
                                               .pass = pass.HasValue() ? pass.Value() : Client::Scenes::UiItemPass{}};
    }

    // Dónde posa 0x5e0720 el ítem de la caja `box` en `screen`, con los ángulos de `angles`.
    glm::vec3 PlacedAt(Client::App::PixelSize screen, std::int32_t type, std::uint32_t level,
                       const Client::Scenes::UiItemBox& box, bool always, glm::vec3& angles)
    {
        const auto pass = Client::Scenes::MakeUiItemPass(screen);
        const auto anchor = Client::Scenes::UiItemAnchor(type, level, box);
        const auto call = Client::Scenes::UiItemCallOf(type, level);
        if (!pass.HasValue() || !anchor.HasValue() || !call)
        {
            ADD_FAILURE() << "sin el punto del ítem " << type;
            return {};
        }
        const auto point =
            Client::Scenes::UnprojectGamePoint(pass.Value().projection, anchor.Value().x, anchor.Value().y);
        const auto placed =
            Client::Scenes::PlaceUiItem(*call, point, always, always, worldClock, &data.items->table, angles);
        if (!placed.HasValue())
        {
            ADD_FAILURE() << placed.Error();
            return {};
        }
        return placed.Value().position;
    }

    // La posición del vértice 0 de cada triángulo del lote de los modelos.
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
        return std::string(Client::UI::TextAt(&*data.text, index).value_or("?"));
    }

    WorldState world;
    CrtRandom random{Seed};
    std::optional<std::int32_t> sound;
    SpriteBatch sprites;
    Client::Tests::RecordingTextDrawer text{sprites};
    float worldClock = 0.0f;
    Client::UI::Point mouse{};
    Client::Scenes::GameData data;
    UiModels uiModels;
    Client::Render::ModelBatch models;
    Client::Scenes::FaunaDrawCarry poseCarry;
    Client::Scenes::ItemSpecialDrawContext special;
    Client::Scenes::ItemDrawContext itemContext;
    std::vector<std::string> events;
};

// 0x5f2340 sin personaje ni party: la ventana en (0x1c2, 0), los doce fondos del equipo (el casco con la clase 0),
// las 64 casillas vacías, el fondo del zen, cerrar y el cartel del set; reparar y la tienda personal no, con el
// nivel en 0. Lo mismo en Lorencia y en Devias: la ventana no mira el mapa.
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
    // El fondo de arriba: 190 × 256 desde (450, 0); la primera casilla en (465, 200); el zen en (500; 367,7).
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
    // El título (Text 223) en una caja de 120 desde 485, en negrita y en blanco sobre gris; "Zen" (224) en (470, 370)
    // y la cifra en (510, 370), con su color; el cartel del set (989) dos veces, centrado en 546 y 545.
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

// 0x5f240b..0x5f2459: el título va en el modo 2 en una caja de W · 120 / 640 píxeles desde x + 0x23, con el texto en
// la columna (caja − cx) / 2 y el fondo en toda la caja (P-C70 b): 120 en 640 × 480, 192 en 1.024 × 768 y 300 en
// 1.600 × 1.200, que TextCache parte en dos cajas (TextCacheTest.AModeTwoBoxPast256ColumnsGoesInBoxesOf256). Con la
// party abierta, desde 0x104 + 0x23. Lo mismo en Lorencia y en Devias.
TEST_F(GameInventoryHudTest, TheTitleFillsItsWholeBoxInThreeResolutions)
{
    struct Case
    {
        Client::App::PixelSize screen;
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

// Con el personaje o la party abiertos, la ventana va en x = 0x104.
TEST_F(GameInventoryHudTest, TheWindowMovesLeftWithThePartyOpen)
{
    world.Runtime().panels.party = 1;

    ASSERT_TRUE(Window().HasValue());

    EXPECT_EQ(world.Runtime().hud.inventoryX, 0x104);
    const auto back = NthQuadOf(sprites, 0x104);
    ASSERT_TRUE(back.has_value());
    EXPECT_FLOAT_EQ(back->first.x, 260.0f);
}

// 0x5f26a7..0x5f27ef: el zen con comas y su color por cuánto es.
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

// Reparar desde el nivel 50 (con 0x78abb0c, la otra textura) y la tienda personal desde el 6; el casco no tiene fondo
// con la clase 3.
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

// Con el mouse sobre cerrar, su ayuda (Text 225) en (_ftol(x + 20), _ftol(395) − 13).
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

// Cada lugar del equipo y cada casilla toma el color del aviso de su ítem (0x5e22d0): el 8, roto, en rojo.
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

// 0x5f7610 con el inventario: el registro abierto alrededor del equipo, los rand() de 0x607b60 por el modelo (el 0xe
// es el 0x220, uno; el 0xa05 y el 0xc0e, el 0xc17 y el 0xe20, dos; el 0xa07, el 0xc19, uno), los dos de 0x609640 del
// 0xc0f (0xe21) y las consultas del comercio y de la tienda. Las casillas que no son la de arriba a la izquierda no
// cuentan, y el 0x1a13 con el nivel en 3 no dibuja nada.
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

// El pase del set (0x60cc29): con el set en 1 o 2, sin excelente y con sin(0,001 · T) · 0,5 + 0,4 en 0,01 o menos, un
// rand() % 100 en el +0xc0 del modelo.
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

// Las alas 0x1806 (el modelo 0x1a18) cortan antes de sus uniones; las otras grillas, con su dirección.
TEST_F(GameInventoryHudTest, WhatBelongsToOtherWavesStopsWithItsAddress)
{
    PutItem(0, 0x1806, 2, 2);
    auto wings = GridItems();
    ASSERT_FALSE(wings.HasValue());
    EXPECT_NE(wings.Error().find("0x60c3b6"), std::string::npos) << wings.Error();

    world.Inventory().ClearForInventoryReply();
    world.Runtime().ownRecordDepth = 0;
    world.Runtime().panels.npcShop = 1;
    auto shop = GridItems();
    ASSERT_FALSE(shop.HasValue());
    EXPECT_NE(shop.Error().find("0x5f769b"), std::string::npos) << shop.Error();
    auto shopNumbers = GridNumbers();
    ASSERT_FALSE(shopNumbers.HasValue());
    EXPECT_NE(shopNumbers.Error().find("0x5f7973"), std::string::npos) << shopNumbers.Error();
}

// 0x5f7840: las pociones con 2 o más (9 × 10, 13 a la derecha) y los paquetes de joyas con ((nivel + 1) · 5) · 2
// (7 × 8, 14 a la derecha), en (1; 0,9; 0,7) y con la mezcla de alfa.
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

// 0x5f77c6 y 0x5f7a9e: con la caja del caos abierta (y el inventario cerrado), su grilla de 8 × 4 desde la esquina
// de la ventana del NPC más (15, 110): los rand() de los modelos (el 0xe uno, el 0xc0e dos) después de las consultas
// de la tienda y del comercio, y las cifras de la poción con 3. En Lorencia, Devias y Noria.
TEST_F(GameInventoryHudTest, TheChaosBoxGridDrawsFromTheNpcWindowCorner)
{
    for (const std::int32_t map : {0, 2, 3})
    {
        SCOPED_TRACE(map);
        auto& runtime = world.Runtime();
        runtime.map = map;
        runtime.panels.inventory = 0;
        runtime.panels.chaosBox = 1;
        runtime.hud.npcWindowX = 0x104;
        runtime.hud.npcWindowY = 0;
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

// 0x44ad40: con 0x724580 en 3, la caja del entrenador (0x723c80) en (275, 110), sin mirar la ventana del NPC, y sus
// cifras (el 0x8000 abierto basta para 0x5f7840). En otro modo, nada.
TEST_F(GameInventoryHudTest, TheTrainerBoxGridDrawsAtItsFixedCorner)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        auto& runtime = world.Runtime();
        runtime.map = map;
        runtime.panels.inventory = 0;
        runtime.panels.delegated.trainer = 3;
        runtime.hud.npcWindowX = 0x40;
        runtime.hud.npcWindowY = 0x20;
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

        // Con el modo 2, solo los rand() de los IsOpen de la tienda y del comercio.
        runtime.panels.delegated.trainer = 2;
        const CrtRandom protectedOnly = Advanced(random, 2);
        ASSERT_TRUE(GridItems().HasValue());
        EXPECT_EQ(random.State(), protectedOnly.State());
    }
}

// 0x5f7b10: el ítem del cursor saca los rand() de su modelo; sin ítem, nada.
TEST_F(GameInventoryHudTest, TheCursorItemSpendsTheRandomsOfItsModel)
{
    world.Runtime().cursorItem.id = 0x0c0e;
    const CrtRandom unchanged = random;
    {
        GameHudContext context = Context();
        ASSERT_TRUE(Client::Scenes::DrawTheCursorItem(context).HasValue());
    }
    EXPECT_EQ(random.State(), unchanged.State());

    world.Runtime().cursorItemCount = 1;
    const CrtRandom expected = Advanced(random, 2);
    GameHudContext context = Context();
    ASSERT_TRUE(Client::Scenes::DrawTheCursorItem(context).HasValue());
    EXPECT_EQ(random.State(), expected.State());
}

// El HUD entero con el inventario abierto, en Lorencia y en Devias: el estado base más la ventana, el cierre del
// registro alrededor del equipo de 0x5e2fc0 y las dos consultas que el ancho del mundo (0x5bb8d0) hace con el
// inventario abierto (la tienda y el comercio), en los avisos de durabilidad y en la party cerrada (0x5f0be0, que con
// su IsOpen(4) en 0 salta a 0x5f12d0 y lo llama).
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
        const auto drawn = Client::Scenes::DrawTheHud(context);

        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
        EXPECT_EQ(random.State(), expected.State());
        EXPECT_EQ(world.Runtime().ownRecordDepth, 0);
    }
}

// 0x5f7610 con los modelos (P-C70): el equipo y la grilla en sus cajas, en Lorencia y en Devias, en 640 × 480 y en
// 1.024 × 768. El primer ítem cierra el 2D y abre el pase de la UI, cada ítem va donde lo posa 0x5e0720 desde el
// punto de su caja (el arma en (0x1c2 + 15, 89), de 40 × 60; la grilla desde (0x1c2 + 15, 200), cada uno del tamaño
// de Item.bmd: 1 × 3, 2 × 3 y 1 × 4 casillas), y los rand() son los mismos que sin dibujar.
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
        for (const Client::App::PixelSize screen : {Screen640, Client::App::PixelSize{1024, 768}})
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
            Client::Scenes::UiItemResources resources = UiItems(screen);
            {
                Client::Scenes::GamePanels panels(world, random);
                GameHudContext context = Context();
                context.uiItems = &resources;
                const auto drawn = Client::Scenes::DrawTheGridItems(context, panels.Registry());
                ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
            }
            Client::Scenes::CloseUiItemPass(resources);

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

// 0x5f7b10 con los modelos: el ítem del cursor centrado en el mouse, a 0,07 de la cámara y girando con el reloj
// (el último argumento de 0x5e1480 en 1).
TEST_F(GameInventoryHudTest, TheCursorItemDrawsCenteredOnTheMouse)
{
    world.Runtime().cursorItem.id = 0x0c0e;
    world.Runtime().cursorItemCount = 1;
    mouse = {300, 200};
    worldClock = 4321.0f;
    for (const Client::App::PixelSize screen : {Screen640, Client::App::PixelSize{1024, 768}})
    {
        SCOPED_TRACE(screen.width);
        models.Begin(Client::Render::Pass3D{});
        events.clear();
        Client::Scenes::UiItemResources resources = UiItems(screen);
        {
            GameHudContext context = Context();
            context.uiItems = &resources;
            const auto drawn = Client::Scenes::DrawTheCursorItem(context);
            ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
        }
        Client::Scenes::CloseUiItemPass(resources);

        EXPECT_EQ(events, (std::vector<std::string>{"2d", "3d"}));
        glm::vec3 angles{};
        const glm::vec3 expected =
            PlacedAt(screen, 0x0c0e, 0, {.x = 280.0f, .y = 170.0f, .width = 40.0f, .height = 60.0f}, true, angles);
        EXPECT_EQ(TriangleOrigins(), std::vector<glm::vec3>{expected});
    }
}

// El HUD entero con el inventario abierto y los modelos, en Lorencia y en Devias: los mismos rand() que sin dibujar,
// y el pase de la UI que abre el arma y cierra el final de 0x5ac120.
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
        Client::Scenes::UiItemResources resources = UiItems(Screen640);

        GameHudContext context = Context();
        context.uiItems = &resources;
        const auto drawn = Client::Scenes::DrawTheHud(context);

        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
        EXPECT_EQ(random.State(), expected.State());
        EXPECT_EQ(events, (std::vector<std::string>{"2d", "3d"}));
        EXPECT_FALSE(resources.open);
        EXPECT_EQ(TriangleOrigins().size(), 1u);
    }
}
} // namespace
