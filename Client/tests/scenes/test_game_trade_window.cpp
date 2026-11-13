#include "Scenes/GameInventoryHud.h"

#include "App/WindowState.h"
#include "Core/Expected.h"
#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Data/Models/PlayerModels.h"
#include "Data/Tables/ItemSetTables.h"
#include "Data/Tables/ItemTable.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Data/Text/TextTable.h"
#include "Network/Crypto/Xor32.h"
#include "Protocol/Inventory.h"
#include "Protocol/Outgoing.h"
#include "Protocol/Trade.h"
#include "Protocol/Viewport.h"
#include "Render/Models/Cloth.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/TextStyle.h"
#include "Scenes/ActorState.h"
#include "Scenes/GameHud.h"
#include "Scenes/GamePanels.h"
#include "Scenes/GuildMarks.h"
#include "Scenes/GuildState.h"
#include "Scenes/LoginModels.h"
#include "Scenes/SelectionState.h"
#include "Scenes/SocialEvents.h"
#include "Scenes/SocialRequests.h"
#include "Scenes/SocialState.h"
#include "Scenes/ViewportPlayers.h"
#include "Scenes/WorldEntry.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/Widget.h"
#include "World/GameCamera.h"
#include "World/WorldTerrain.h"

#include "support/ChatTexts.h"
#include "support/FakeTextureStore.h"
#include "support/GoldenDir.h"
#include "support/ItemFiles.h"
#include "support/ItemSetFiles.h"
#include "support/PlayerActors.h"
#include "support/RecordingSink.h"
#include "support/RecordingTextDrawer.h"
#include "support/SceneDataFiles.h"
#include "support/ViewportPackets.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// La ventana del comercio (0x5f3b40) y sus grillas (0x5f7610 y 0x5f7840; CL9 hito 7, fase 2) con el otro que trae el
// 0x12 (hito 1) al lado del héroe: el /trade, el 37 que abre la ventana, el 39 con un ítem y el zen de los dos, en
// Lorencia (0) y Devias (2). Los nombres, los textos y los ítems son inventados.
namespace
{
using Client::Data::Models::PlayerSkeletonModel;
using Client::Render::Color;
using Client::Render::SpriteBatch;
using Client::Render::Models::CrtRandom;
using Client::Render::Text::FontKind;
using Client::Scenes::ActorState;
using Client::Scenes::GameHudContext;
using Client::Tests::ViewportPlayerRecord;

constexpr std::uint32_t Seed = 0x7ade;
constexpr std::uint16_t HeroKey = 33;
constexpr std::uint16_t OtherKey = 0x0456;
constexpr std::size_t Chosen = 2;
constexpr Client::App::PixelSize Screen640{640, 480};
// 0x5f3b40 corre en (0x104, 0).
constexpr float Left = 260.0f;
// Un bastón de 1 × 3 (0x0a05): su modelo (0xc17) saca dos rand() sin el pase 3D, como en el inventario.
constexpr std::uint16_t StaffType = 0x0a05;
constexpr int StaffRandoms = 2;
// La poción (0x1c00) con 2 o más muestra la durabilidad.
constexpr std::uint16_t PotionType = 0x1c00;
constexpr std::uint16_t DigitSlot = 1;
// IsOpen(0x20): la entrada protegida del byte del comercio, un rand() al cerrarla.
constexpr int TradeQueryRandoms = 1;
// Interface\accept_box01/02, Money_01/02, Item_Money, exit_01, Trading_line y la flecha (CursorSitDown).
constexpr std::uint16_t OkOff = 0x122;
constexpr std::uint16_t OkOn = 0x123;
constexpr std::uint16_t Money = 0x110;
constexpr std::uint16_t MoneyPressed = 0x111;
constexpr std::uint16_t MoneyBox = 0x10f;
constexpr std::uint16_t Close = 0x118;
constexpr std::uint16_t Line = 0x117;
constexpr std::uint16_t Arrow = 9;
constexpr std::uint16_t EmptyBox = 0x115;
constexpr std::uint16_t FullBox = 0x116;

std::uint8_t Byte(int value)
{
    return static_cast<std::uint8_t>(value);
}

void Name(std::span<std::byte> name, std::string_view text)
{
    for (std::size_t at = 0; at < name.size(); ++at)
        name[at] = at < text.size() ? static_cast<std::byte>(text[at]) : std::byte{0};
}

Client::Protocol::Trade::Name NameOf(std::string_view text)
{
    Client::Protocol::Trade::Name name{};
    Name(name, text);
    return name;
}

// Los siete bytes de un ítem del 39 (0x5bc740): el tipo en el byte 0, el bit 8 en el 0x80 del 3 y los de arriba en
// el 5.
Client::Protocol::Inventory::ItemData ItemBytes(std::uint16_t type)
{
    return {static_cast<std::byte>(type & 0xff),
            std::byte{0},
            std::byte{20},
            static_cast<std::byte>((type >> 1) & 0x80),
            std::byte{0},
            static_cast<std::byte>((type >> 5) & 0xf0),
            std::byte{0}};
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

void ExpectQuad(const SpriteBatch& sprites, std::uint16_t slot, std::size_t index, float x, float y, float width,
                float height)
{
    SCOPED_TRACE(slot);
    SCOPED_TRACE(index);
    const auto quad = NthQuadOf(sprites, slot, index);
    ASSERT_TRUE(quad.has_value());
    constexpr float Tolerance = 1e-3f;
    EXPECT_NEAR(quad->first.x, x, Tolerance);
    EXPECT_NEAR(quad->first.y, 480.0f - y, Tolerance);
    EXPECT_NEAR(quad->third.x, x + width, Tolerance);
    EXPECT_NEAR(quad->third.y, 480.0f - y - height, Tolerance);
}

// El mundo del juego, en el heap.
struct Game
{
    Game()
    {
        models.Set(PlayerSkeletonModel, {}, Client::Tests::PlayerActionsSkeleton(), {});
        ActorState& hero = world.Actors()[0];
        hero = Client::Tests::UnarmedPlayer();
        hero.alive = true;
        hero.key = HeroKey;
        hero.byte88 = 1;
        Name(hero.name, "Tejon");
        world.Runtime().hero = 0;
        world.Runtime().drawingEnabled = true;
        Client::Scenes::ClearTheTradeForSelection(world.Runtime().social.trade);
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
    Client::Data::Tables::ItemTable items = Client::Tests::ItemTableOf({{StaffType, 1, 3, 0}, {PotionType, 1, 1, 0}});
    Client::Data::Tables::ItemSetTables sets =
        Client::Tests::ItemSetTablesFrom(std::vector<std::byte>(Client::Data::Tables::ItemSetTypeTableSize),
                                         std::vector<std::byte>(Client::Data::Tables::ItemSetOptionTableSize));
    Client::Tests::RecordingSink sink;
    Client::Core::Log log{sink, Client::Core::LogLevel::Debug};
};

class GameTradeWindowTest : public Client::Tests::GoldenDataTest, public ::testing::WithParamInterface<std::uint8_t>
{
protected:
    void SetUp() override
    {
        GoldenDataTest::SetUp();
        game = std::make_unique<Game>();
        const std::uint8_t map = GetParam();
        auto loaded = Client::World::LoadWorldTerrain(m_dataRoot, map);
        ASSERT_TRUE(loaded.HasValue()) << loaded.Error();
        game->world.Map().terrain = std::move(loaded).Value();
        game->world.Runtime().map = map;
        TheOtherArrives();
        sprites.Begin(Screen640);
    }

    // La primera fila libre de dos celdas desde (100, 100).
    [[nodiscard]] std::array<std::uint8_t, 2> FreeRow() const
    {
        const auto& cells = game->world.Map().terrain.attributes->cells;
        for (int y = 100; y < 200; ++y)
            for (int x = 100; x < 200; ++x)
                if (cells[Client::Data::Terrain::CellIndex(Byte(x), Byte(y))] == 0 &&
                    cells[Client::Data::Terrain::CellIndex(Byte(x + 1), Byte(y))] == 0)
                    return {Byte(x), Byte(y)};
        ADD_FAILURE() << "el mapa no tiene una fila libre";
        return {};
    }

    // El héroe en la primera celda y el 0x12 con Brezal en la de al lado.
    void TheOtherArrives()
    {
        const auto [x, y] = FreeRow();
        ActorState& hero = game->world.Actors()[0];
        hero.cellX = x;
        hero.cellY = y;
        const std::array<ViewportPlayerRecord, 1> records{{{.key = OtherKey,
                                                            .isNew = true,
                                                            .x = Byte(x + 1),
                                                            .y = y,
                                                            .name = "Brezal",
                                                            .targetX = Byte(x + 1),
                                                            .targetY = y}}};
        auto read = Client::Protocol::Viewport::ReadPlayers(Client::Tests::ViewportPlayersPacket(records));
        ASSERT_TRUE(read.HasValue());
        const auto applied = Client::Scenes::ApplyViewportPlayers(read.Value(), game->Resources());
        ASSERT_TRUE(applied.HasValue()) << applied.Error();
        ASSERT_EQ(game->world.Actors()[1].key, OtherKey);
    }

    // /trade al elegido, el 37 con el sí, un bastón del otro (39) y el zen de los dos (3B y 3A aceptado).
    void TradeWithTheOther()
    {
        Client::Scenes::WorldRuntime& runtime = game->world.Runtime();
        runtime.selectedTargets[Chosen] = 1;
        std::vector<Client::Protocol::Outgoing> sent;
        const auto asked = Client::Scenes::RequestATrade(
            {.world = game->world,
             .random = random,
             .texts = chatTexts,
             .send = [&sent](Client::Protocol::Outgoing packet) { sent.push_back(std::move(packet)); }});
        ASSERT_TRUE(asked.HasValue()) << asked.Error();
        ASSERT_EQ(sent.size(), 1U);
        const auto plain = Client::Network::Crypto::Xor32Decode(sent[0].packet);
        ASSERT_EQ(plain.Bytes().size(), 5U);
        EXPECT_EQ(plain.Bytes()[2], std::byte{0x36});
        EXPECT_EQ(plain.Bytes()[3], std::byte{0x04});
        EXPECT_EQ(plain.Bytes()[4], std::byte{0x56});

        const auto opened = Client::Scenes::ApplyTradeResponse({1, NameOf("Brezal"), 150, 0}, game->Resources());
        ASSERT_TRUE(opened.HasValue()) << opened.Error();
        ASSERT_EQ(runtime.panels.trade, 1);
        const auto added = Client::Scenes::ApplyTradeItemAdded({9, ItemBytes(StaffType)}, game->Resources());
        ASSERT_TRUE(added.HasValue()) << added.Error();
        Client::Scenes::ApplyTradeOtherMoney({2500}, game->world);
        runtime.social.trade.sentMoney = 1000;
        Client::Scenes::ApplyTradeMoneyResult({1}, game->world);
        random = CrtRandom(Seed);
    }

    GameHudContext Context()
    {
        Client::Scenes::WorldRuntime& runtime = game->world.Runtime();
        ActorState& hero = game->world.Actors()[runtime.hero];
        return GameHudContext{.world = game->world,
                              .random = random,
                              .ownRecordDepth = runtime.ownRecordDepth,
                              .cellXDepth = hero.cellXAccesses,
                              .cellYDepth = hero.cellYAccesses,
                              .hud = runtime.hud,
                              .crywolfSandOverlay = runtime.crywolf.sandOverlay,
                              .sound = sound,
                              .sprites = sprites,
                              .text = text,
                              .texts = &windowTexts,
                              .items = &game->items,
                              .worldClock = worldClock,
                              .mouse = mouse,
                              .leftDown = leftDown,
                              .guildMarks = withMarks ? &marks : nullptr};
    }

    Client::Core::Expected<void, std::string> Window()
    {
        Client::Scenes::GamePanels panels(game->world, random);
        GameHudContext context = Context();
        return Client::Scenes::DrawTheTradeWindow(context, panels.Registry());
    }

    Client::Core::Expected<void, std::string> GridItems()
    {
        Client::Scenes::GamePanels panels(game->world, random);
        GameHudContext context = Context();
        return Client::Scenes::DrawTheGridItems(context, panels.Registry());
    }

    Client::Core::Expected<void, std::string> GridNumbers()
    {
        Client::Scenes::GamePanels panels(game->world, random);
        GameHudContext context = Context();
        return Client::Scenes::DrawTheGridNumbers(context, panels.Registry());
    }

    std::unique_ptr<Game> game;
    CrtRandom random{Seed};
    std::optional<std::int32_t> sound;
    SpriteBatch sprites;
    Client::Tests::RecordingTextDrawer text{sprites};
    float worldClock = 0.0f;
    Client::UI::Point mouse{};
    bool leftDown = false;
    Client::Tests::FakeTextureStore store;
    Client::Scenes::GuildMarkTextures marks{store};
    bool withMarks = true;
    Client::Data::Text::TextTable chatTexts = Client::Tests::MakeTextTable(Client::Tests::ChatTexts());
    Client::Data::Text::TextTable windowTexts = Client::Tests::MakeTextTable({{224, "Zen"},
                                                                              {226, "Comercio"},
                                                                              {227, "dar zen"},
                                                                              {228, "listo"},
                                                                              {229, "cerrar"},
                                                                              {365, "mira bien"},
                                                                              {366, "antes de"},
                                                                              {367, "aceptar"},
                                                                              {368, "Nivel"},
                                                                              {369, "%d o mas"},
                                                                              {370, "ojo"}});
};

// Con el comercio cerrado, 0x5f3b40 solo consulta su byte.
TEST_P(GameTradeWindowTest, ClosedItDrawsNothing)
{
    ASSERT_TRUE(Window().HasValue());
    EXPECT_TRUE(SlotsOf(sprites).empty());
    EXPECT_TRUE(text.texts.empty());
    EXPECT_EQ(random.State(), Client::Tests::AfterDraws(Seed, TradeQueryRandoms).State());
}

// 0x5f3b40 con el comercio de Brezal abierto: el fondo en (260, 0) con la línea, las dos grillas (el bastón del otro
// llena tres casillas), los botones, las cajas del zen, la flecha y los textos en su orden. Solo el rand() de IsOpen.
TEST_P(GameTradeWindowTest, TheOpenTradeDrawsTheWindowOfTheOther)
{
    TradeWithTheOther();
    ASSERT_TRUE(Window().HasValue());

    const Client::Scenes::HudState& hud = game->world.Runtime().hud;
    EXPECT_EQ(hud.tradeX, 0x104);
    EXPECT_EQ(hud.tradeY, 0);
    std::vector<std::uint16_t> expected{0x104, 0x105, Line};
    for (std::size_t cell = 0; cell < 32; ++cell)
        expected.push_back(cell == 9 || cell == 17 || cell == 25 ? FullBox : EmptyBox);
    for (std::size_t cell = 0; cell < 32; ++cell)
        expected.push_back(EmptyBox);
    for (const std::uint16_t slot : {OkOff, Money, OkOff, Close, MoneyBox, MoneyBox, Arrow})
        expected.push_back(slot);
    EXPECT_EQ(SlotsOf(sprites), expected);
    ExpectQuad(sprites, Line, 0, Left, 225.0f, 190.0f, 10.0f);
    ExpectQuad(sprites, EmptyBox, 0, Left + 15.0f, 70.0f, 20.0f, 20.0f);
    ExpectQuad(sprites, FullBox, 0, Left + 15.0f + 20.0f, 90.0f, 20.0f, 20.0f);
    ExpectQuad(sprites, EmptyBox, 29, Left + 15.0f, 270.0f, 20.0f, 20.0f);
    ExpectQuad(sprites, OkOff, 0, Left + 140.0f, 185.0f, 24.0f, 24.0f);
    ExpectQuad(sprites, Money, 0, Left + 26.0f, 390.0f, 24.0f, 24.0f);
    ExpectQuad(sprites, OkOff, 1, Left + 97.0f, 390.0f, 24.0f, 24.0f);
    ExpectQuad(sprites, Close, 0, Left + 137.0f, 390.0f, 24.0f, 24.0f);
    ExpectQuad(sprites, MoneyBox, 0, Left + 50.0f, 160.0f, 113.0f, 18.0f);
    ExpectQuad(sprites, MoneyBox, 1, Left + 50.0f, 360.0f, 113.0f, 18.0f);
    // sen(0) = 0: la flecha en y + 28, en (1; 0,86; 0,58).
    ExpectQuad(sprites, Arrow, 0, Left + 140.0f, 28.0f, 15.0f, 24.0f);
    const auto arrow = NthQuadOf(sprites, Arrow);
    ASSERT_TRUE(arrow.has_value());
    EXPECT_EQ(arrow->color, (Color{1.0f, 0.86f, 0.58f, 1.0f}));
    EXPECT_EQ(NthQuadOf(sprites, OkOff)->color, (Color{1.0f, 1.0f, 1.0f, 1.0f}));

    struct Expected
    {
        std::string text;
        int x;
        int y;
        FontKind font;
        std::uint32_t color;
    };
    // El nivel 150 cae en el piso de 100, en verde; el aviso, con sen(0), en 275 recortado a 255.
    const std::vector<Expected> texts{{"Comercio", 260 + 0x23, 0xc, FontKind::Bold, 0xffdcdcdcu},
                                      {"Zen", 260 + 50 - 30, 162, FontKind::Bold, 0xff96dcffu},
                                      {"2,500", 260 + 50 + 10, 162, FontKind::Bold, 0xff96dcffu},
                                      {"Zen", 260 + 50 - 30, 362, FontKind::Bold, 0xff96dcffu},
                                      {"1,000", 260 + 50 + 10, 362, FontKind::Bold, 0xff96dcffu},
                                      {"Brezal", 260 + 0x14, 0x2d, FontKind::BigBold, 0xffffe6d2u},
                                      {"Nivel", 260 + 0x8c, 0x2d, FontKind::Bold, 0xff18c900u},
                                      {"100 o mas", 260 + 0x8c, 0x37, FontKind::Bold, 0xff18c900u},
                                      {"Tejon", 260 + 0x14, 0xfa, FontKind::Bold, 0xffffe6d2u},
                                      {"ojo", 260 + 0x14, 0xb9, FontKind::Bold, 0xff0000d2u},
                                      {"mira bien", 260 + 0x2d, 0xb9, FontKind::Bold, 0xff96dcffu},
                                      {"antes de", 260 + 0x14, 200, FontKind::Bold, 0xff96dcffu},
                                      {"aceptar", 260 + 0x14, 0xd7, FontKind::Bold, 0xff96dcffu}};
    ASSERT_EQ(text.texts.size(), texts.size());
    for (std::size_t at = 0; at < texts.size(); ++at)
    {
        SCOPED_TRACE(at);
        EXPECT_EQ(text.texts[at].text, texts[at].text);
        EXPECT_EQ(text.texts[at].x, texts[at].x);
        EXPECT_EQ(text.texts[at].y, texts[at].y);
        EXPECT_EQ(text.texts[at].style.font, texts[at].font);
        EXPECT_EQ(text.texts[at].style.colors.text, texts[at].color);
    }
    EXPECT_TRUE(text.texts[0].windowed);
    EXPECT_EQ(text.texts[0].style.colors.background, 0xff141414u);
    EXPECT_EQ(text.texts[1].style.colors.background, 0x80282828u);
    EXPECT_EQ(random.State(), Client::Tests::AfterDraws(Seed, TradeQueryRandoms).State());
}

// 0x5f3bf9..0x5f3c8a (CL9 hito 8): con el gremio del otro en la tabla (la clave del 37 en 0x6c9324), su marca va
// con 0x5f16a0(i, 0) en (260 − 32, 40), de 32 × 32, después de las grillas, y su nombre en (260, 30) antes del título,
// con la fuente y los colores que había. Con la clave 0 del 37 de los otros tests ("sin gremio") y las claves en −1
// (0x447ae0) no sale nada; sin las marcas, corta. Los gremios son inventados.
TEST_P(GameTradeWindowTest, TheOthersGuildDrawsItsMarkAndName)
{
    TradeWithTheOther();
    Client::Scenes::GuildState& guild = game->world.Runtime().guild;
    ASSERT_EQ(Client::Scenes::AddGuild(guild, 0x21), 0);
    ASSERT_EQ(Client::Scenes::AddGuild(guild, 0x33), 1);
    guild.table[0].guildName = {'O', 'm', 'b', 'u'};
    guild.table[1].guildName = {'T', 'a', 'l', 'a', 's'};
    guild.table[1].mark.fill(5);
    game->world.Runtime().social.trade.otherGuild = 0x33;
    ASSERT_TRUE(Window().HasValue());

    const std::uint16_t mark = Client::Scenes::GuildMarkTextures::SlotOf(1, false).value;
    const std::vector<std::uint16_t> slots = SlotsOf(sprites);
    ASSERT_GT(slots.size(), 67U);
    EXPECT_EQ(slots[67], mark);
    EXPECT_EQ(slots[68], OkOff);
    EXPECT_EQ(std::ranges::count(slots, mark), 1);
    ExpectQuad(sprites, mark, 0, Left - 32.0f, 40.0f, 32.0f, 32.0f);
    EXPECT_EQ(NthQuadOf(sprites, mark)->color, (Color{1.0f, 1.0f, 1.0f, 1.0f}));
    ASSERT_FALSE(text.texts.empty());
    EXPECT_EQ(text.texts[0].text, "Talas");
    EXPECT_EQ(text.texts[0].x, 260);
    EXPECT_EQ(text.texts[0].y, 30);
    EXPECT_EQ(text.texts[1].text, "Comercio");
    EXPECT_EQ(random.State(), Client::Tests::AfterDraws(Seed, TradeQueryRandoms).State());

    // Sin la tabla de las marcas, el lazo corta en 0x5f3c10.
    withMarks = false;
    const auto cut = Window();
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x5f3c10"), std::string::npos);
}

// Con 0x78abac4 y la espera (0x78abac8) sobre 0 los dos OK van en rojo, con la marca si los dos los dieron; el mouse
// sobre el zen con el izquierdo abajo pone Money_02 encima y su ayuda 13 arriba en la normal. Con el reloj, la flecha
// sube y baja y el aviso late.
TEST_P(GameTradeWindowTest, TheButtonsLightAndTheArrowMoves)
{
    TradeWithTheOther();
    Client::Scenes::TradeState& trade = game->world.Runtime().social.trade;
    trade.field78abac4 = 1;
    trade.okWait = 2;
    trade.otherOk = 1;
    trade.ownOk = 1;
    mouse = {.x = 260 + 26 + 23, .y = 390 + 23};
    leftDown = true;
    worldClock = 1000.0f;
    ASSERT_TRUE(Window().HasValue());

    const auto slots = SlotsOf(sprites);
    ASSERT_GE(slots.size(), 3U + 64U + 5U);
    EXPECT_EQ((std::vector<std::uint16_t>(slots.begin() + 67, slots.begin() + 72)),
              (std::vector<std::uint16_t>{OkOn, Money, MoneyPressed, OkOn, Close}));
    const Color red{1.0f, 0.0f, 0.0f, 1.0f};
    EXPECT_EQ(NthQuadOf(sprites, OkOn, 0)->color, red);
    EXPECT_EQ(NthQuadOf(sprites, OkOn, 1)->color, red);
    EXPECT_EQ(NthQuadOf(sprites, Close)->color, (Color{1.0f, 1.0f, 1.0f, 1.0f}));
    ASSERT_GE(text.texts.size(), 2U);
    EXPECT_EQ(text.texts[1].text, "dar zen");
    EXPECT_EQ(text.texts[1].x, 260 + 26);
    EXPECT_EQ(text.texts[1].y, 390 - 13);
    EXPECT_EQ(text.texts[1].style.font, FontKind::Normal);
    EXPECT_EQ(text.texts[1].style.colors.text, 0xffffffffu);
    EXPECT_EQ(text.texts[1].style.colors.background, 0xff000000u);

    const auto bob = static_cast<float>(std::sin(1000.0 * static_cast<double>(0.01f)) + 28.0);
    ExpectQuad(sprites, Arrow, 0, Left + 140.0f, bob, 15.0f, 24.0f);
    const double alpha = std::sin(1000.0 * static_cast<double>(0.005f)) * 200.0 + 275.0;
    const auto pulse = (static_cast<std::uint32_t>(static_cast<std::int32_t>(alpha)) << 24u) + 0xd2u;
    ASSERT_EQ(text.texts.size(), 14U);
    EXPECT_EQ(text.texts[10].text, "ojo");
    EXPECT_EQ(text.texts[10].style.colors.text, pulse);
    EXPECT_EQ(random.State(), Client::Tests::AfterDraws(Seed, TradeQueryRandoms).State());
}

// 0x5f76b4..0x5f7717: el bastón del otro en (x + 15 + 20, y + 70 + 20) saca los rand() de su modelo, después de los
// del inventario abierto; 0x5f798c..0x5f79f0: la poción propia con 5 muestra la cifra en la grilla de abajo.
TEST_P(GameTradeWindowTest, TheGridsDrawTheItemsOfBoth)
{
    TradeWithTheOther();
    Client::Scenes::TradeState& trade = game->world.Runtime().social.trade;
    ASSERT_TRUE(Window().HasValue());

    const CrtRandom beforeEmpty = random;
    const auto staff = trade.otherGrid;
    // La grilla propia sigue vacía (0x5bbe10).
    trade.otherGrid = trade.ownGrid;
    ASSERT_TRUE(GridItems().HasValue());
    CrtRandom empty = beforeEmpty;
    int inventoryDraws = 0;
    while (empty.State() != random.State() && inventoryDraws < 100000)
    {
        static_cast<void>(empty.Next());
        ++inventoryDraws;
    }
    ASSERT_LT(inventoryDraws, 100000);

    trade.otherGrid = staff;
    const CrtRandom before = random;
    ASSERT_TRUE(GridItems().HasValue());
    CrtRandom expected = before;
    for (int draw = 0; draw < inventoryDraws + StaffRandoms; ++draw)
        static_cast<void>(expected.Next());
    EXPECT_EQ(random.State(), expected.State());

    trade.ownGrid[3].id = PotionType;
    trade.ownGrid[3].durability = 5;
    trade.ownGrid[3].occupied = 1;
    sprites.Begin(Screen640);
    ASSERT_TRUE(GridNumbers().HasValue());
    EXPECT_EQ(SlotsOf(sprites), (std::vector<std::uint16_t>{DigitSlot}));
    const auto five = NthQuadOf(sprites, DigitSlot);
    ASSERT_TRUE(five.has_value());
    EXPECT_FLOAT_EQ(five->first.x, Left + 15.0f + 60.0f + 13.0f - 4.5f);
    EXPECT_FLOAT_EQ(five->first.y, 480.0f - 270.0f);
}

INSTANTIATE_TEST_SUITE_P(Maps, GameTradeWindowTest, ::testing::Values(std::uint8_t{0}, std::uint8_t{2}));
} // namespace
