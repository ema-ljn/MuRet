#include "Scenes/EventNpcWindows.h"

#include "App/WindowState.h"
#include "Core/Expected.h"
#include "Data/Text/TextTable.h"
#include "Input/InputState.h"
#include "Protocol/Connection/Logout.h"
#include "Protocol/Events.h"
#include "Protocol/Outgoing.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/LogicalAccess.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/FontSet.h"
#include "Scenes/ActorState.h"
#include "Scenes/GameChat.h"
#include "Scenes/GameHud.h"
#include "Scenes/GamePanels.h"
#include "Scenes/GameUi.h"
#include "Scenes/OwnInventory.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/GameDialogs.h"
#include "UI/GameText.h"
#include "UI/Widget.h"
#include "World/ItemState.h"

#include "support/RecordingTextDrawer.h"
#include "support/SameBytes.h"
#include "support/SceneDataFiles.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// CL9 hito 11, fase 2: las ventanas de los NPC de evento (0x5f58f0 y su paso 0x5e5920) contra el ASM de c32bbd5f, sin
// traza de GL (*estático*). Text.bmd es inventado, con los especificadores de los textos del original.
namespace
{
using Client::Render::Color;
using Client::Render::SpriteBatch;
using Client::Render::Models::CrtRandom;
using Client::Render::Models::OwnRecordCloseRandoms;
using Client::Render::Text::FontKind;
using Client::Scenes::GameHudContext;
using Client::Scenes::GameUiContext;
using Client::Scenes::WorldState;

constexpr std::uint32_t Seed = 0x2468ace1u;
constexpr Client::App::PixelSize Screen640{640, 480};
constexpr Color White{1.0f, 1.0f, 1.0f, 1.0f};

std::map<std::size_t, std::string> NpcTexts()
{
    std::map<std::size_t, std::string> texts{
        {39, "el cuadrado"},
        {645, "cuadrado %d (%d-%d)"},
        {646, "cuadrado %d (desde %d)"},
        {677, "sin invitacion"},
        {686, "muy fuerte"},
        {687, "muy debil"},
        {832, "el castillo espera a los que traen la capa"},
        {846, "el castillo"},
        {847, "puente %d (%d-%d)"},
        {848, "puente %d (desde %d)"},
        {854, "sin capa"},
    };
    for (std::size_t line = 0; line < 6; ++line)
        texts[670 + line] = "historia " + std::to_string(line);
    return texts;
}

struct DrawnQuad
{
    std::uint16_t slot = 0;
    Color color{};
    float x = 0.0f;
    float top = 0.0f;
};

// Los quads con textura del lote, sin las marcas de los textos.
std::vector<DrawnQuad> QuadsOf(const SpriteBatch& sprites)
{
    std::vector<DrawnQuad> quads;
    const auto vertices = sprites.Vertices();
    for (const Client::Render::DrawCommand& command : sprites.Commands())
    {
        if (!command.texture || command.texture == Client::Tests::TextMarkerSlot)
            continue;
        for (std::size_t quad = 0; quad < command.vertexCount / 6; ++quad)
        {
            const Client::Render::SpriteVertex& first = vertices[command.firstVertex + quad * 6];
            quads.push_back(
                DrawnQuad{.slot = command.texture->value, .color = first.color, .x = first.x, .top = 480.0f - first.y});
        }
    }
    return quads;
}

void ExpectColor(const Color& drawn, const Color& expected)
{
    EXPECT_FLOAT_EQ(drawn.red, expected.red);
    EXPECT_FLOAT_EQ(drawn.green, expected.green);
    EXPECT_FLOAT_EQ(drawn.blue, expected.blue);
}

CrtRandom Advanced(CrtRandom random, std::uint32_t count)
{
    random.Discard(count);
    return random;
}

class EventNpcWindowTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        sprites.Begin(Screen640);
    }

    Client::Scenes::WorldRuntime& Runtime()
    {
        return world->Runtime();
    }

    Client::Core::Expected<void, std::string> Draw(Client::UI::Point mouse, bool projecting = false)
    {
        Client::Scenes::WorldRuntime& runtime = Runtime();
        Client::Scenes::ActorState& hero = world->Actors()[runtime.hero];
        GameHudContext context{.world = *world,
                               .random = random,
                               .ownRecordDepth = runtime.ownRecordDepth,
                               .cellXDepth = hero.cellXAccesses,
                               .cellYDepth = hero.cellYAccesses,
                               .hud = runtime.hud,
                               .crywolfSandOverlay = runtime.crywolf.sandOverlay,
                               .sound = sound,
                               .projecting = projecting,
                               .sprites = sprites,
                               .text = text,
                               .texts = &texts,
                               .mouse = mouse};
        Client::Scenes::GamePanels panels(*world, random);
        return Client::Scenes::DrawTheEventNpcWindow(context, panels.Registry());
    }

    Client::Core::Expected<void, std::string> Step(std::int32_t x, std::int32_t y)
    {
        const Client::Scenes::GameChatContext chat{
            .world = *world, .random = random, .send = [this](Client::Protocol::Outgoing packet) {
                sent.push_back(std::move(packet));
            }};
        const GameUiContext context{.world = *world,
                                    .random = random,
                                    .input = input,
                                    .mouseX = x,
                                    .mouseY = y,
                                    .chat = &chat,
                                    .texts = &texts};
        Client::Scenes::GamePanels panels(*world, random);
        panels.LendTheWindowClose([this]() { sent.push_back(Client::Protocol::Connection::WriteWindowClose()); });
        return Client::Scenes::StepTheEventNpcWindow(context, panels.Registry());
    }

    void Open(std::uint8_t kind)
    {
        Runtime().panels.eventNpc = 1;
        Runtime().panels.eventNpcKind = kind;
        Runtime().panels.inventory = 1;
    }

    void PutItem(std::size_t cell, std::uint16_t type, std::uint32_t level)
    {
        Client::World::ItemState& item = world->Inventory().cells[cell];
        item.id = type;
        item.scalar1Low = level << 3;
        item.occupied = 1;
    }

    std::unique_ptr<WorldState> world = std::make_unique<WorldState>();
    CrtRandom random{Seed};
    std::optional<std::int32_t> sound;
    SpriteBatch sprites;
    Client::Tests::RecordingTextDrawer text{sprites};
    Client::Input::InputState input;
    Client::Data::Text::TextTable texts = Client::Tests::MakeTextTable(NpcTexts());
    std::vector<Client::Protocol::Outgoing> sent;
};

// 0x5583a0: de la columna 7 a la 0 y, en cada una, de la fila 7 a la 0; con +0x3c en 0 o con otro nivel no vale.
TEST_F(EventNpcWindowTest, FindsTheInventoryItemFromTheLastColumn)
{
    const Client::Scenes::OwnInventory& inventory = world->Inventory();
    EXPECT_EQ(Client::Scenes::FindInventoryItem(inventory, 0x1c13, 0), -1);
    PutItem(9, 0x1c13, 0);  // fila 1, columna 1
    PutItem(17, 0x1c13, 0); // fila 2, columna 1
    PutItem(2, 0x1c13, 3);  // fila 0, columna 2
    EXPECT_EQ(Client::Scenes::FindInventoryItem(inventory, 0x1c13, 0), 17);
    EXPECT_EQ(Client::Scenes::FindInventoryItem(inventory, 0x1c13, -1), 2);
    EXPECT_EQ(Client::Scenes::FindInventoryItem(inventory, 0x1c13, 3), 2);
    world->Inventory().cells[2].occupied = 0;
    EXPECT_EQ(Client::Scenes::FindInventoryItem(inventory, 0x1c13, 3), -1);
    EXPECT_EQ(Client::Scenes::FindInventoryItem(inventory, 0x1a12, -1), -1);
}

// Devil Square (0x1000): el fondo, los seis botones 0xf0 desde (285, 187) de a 40 (blanco con el mouse, si no
// (0,9, 0,8, 0,7)), los renglones con la tabla de la clase del registro propio (abierto y cerrado por fila: 6 × 3.848
// rand()), el título y los seis textos de abajo. Escribe la esquina y los niveles que lee el paso. En Lorencia y en
// Devias.
TEST_F(EventNpcWindowTest, TheDevilSquareWindowDrawsItsSquares)
{
    struct Case
    {
        std::int32_t map;
        std::uint8_t characterClass;
        std::array<std::string_view, 6> rows;
    };
    const std::array<Case, 2> cases{{{0,
                                      0,
                                      {"cuadrado 1 (15-130)", "cuadrado 2 (131-180)", "cuadrado 3 (181-230)",
                                       "cuadrado 4 (231-400)", "cuadrado 5 (281-330)", "cuadrado 6 (331-400)"}},
                                     {2,
                                      3,
                                      {"cuadrado 1 (15-110)", "cuadrado 2 (111-160)", "cuadrado 3 (161-210)",
                                       "cuadrado 4 (211-400)", "cuadrado 5 (261-310)", "cuadrado 6 (311-400)"}}}};
    for (const Case& probe : cases)
    {
        SCOPED_TRACE(probe.map);
        world = std::make_unique<WorldState>();
        sprites.Begin(Screen640);
        text.texts.clear();
        Runtime().map = probe.map;
        world->OwnCharacter().characterClass = probe.characterClass;
        Open(0);
        const CrtRandom expected = Advanced(random, 6u * OwnRecordCloseRandoms);
        const auto drawn = Draw({300, 232});
        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();

        const std::vector<DrawnQuad> quads = QuadsOf(sprites);
        ASSERT_EQ(quads.size(), 8u);
        EXPECT_EQ(quads[0].slot, 0x104);
        EXPECT_EQ(quads[1].slot, 0x105);
        for (std::size_t row = 0; row < 6; ++row)
        {
            SCOPED_TRACE(row);
            const DrawnQuad& button = quads[2 + row];
            EXPECT_EQ(button.slot, 0xf0);
            EXPECT_FLOAT_EQ(button.x, 285.0f);
            EXPECT_FLOAT_EQ(button.top, 187.0f + 40.0f * static_cast<float>(row));
            ExpectColor(button.color, row == 1 ? White : Color{0.9f, 0.8f, 0.7f, 1.0f});
        }
        ASSERT_EQ(text.texts.size(), 13u);
        for (std::size_t row = 0; row < 6; ++row)
        {
            SCOPED_TRACE(row);
            const Client::Tests::RecordedText& line = text.texts[row];
            EXPECT_EQ(line.text, probe.rows[row]);
            EXPECT_EQ(line.x, 297);
            EXPECT_EQ(line.y, 199 + 40 * static_cast<int>(row));
            EXPECT_TRUE(line.windowed);
            EXPECT_EQ(line.style.font, FontKind::Bold);
            EXPECT_EQ(line.style.colors.text, 0xc41432ffu);
            EXPECT_EQ(line.style.colors.background, 0x101000u);
        }
        EXPECT_EQ(text.texts[6].text, "el cuadrado");
        EXPECT_EQ(text.texts[6].x, 0x104 + 0x23);
        EXPECT_EQ(text.texts[6].y, 0xc);
        EXPECT_EQ(text.texts[6].style.font, FontKind::Bold);
        EXPECT_EQ(text.texts[6].style.colors.text, 0xffdcdcdcu);
        EXPECT_EQ(text.texts[6].style.colors.background, 0xff141414u);
        for (std::size_t line = 0; line < 6; ++line)
        {
            SCOPED_TRACE(line);
            EXPECT_EQ(text.texts[7 + line].text, "historia " + std::to_string(line));
            EXPECT_EQ(text.texts[7 + line].x, 0x104 + 0x1e);
            EXPECT_EQ(text.texts[7 + line].y, 0x41 + 20 * static_cast<int>(line));
            EXPECT_EQ(text.texts[7 + line].style.font, FontKind::Normal);
            EXPECT_EQ(text.texts[7 + line].style.colors.text, 0xffffffffu);
        }
        EXPECT_EQ(random.State(), expected.State());
        EXPECT_EQ(Runtime().ui.npcWindowX, 0x104);
        EXPECT_EQ(Runtime().events.devilSquareLevels[3][0], probe.characterClass == 3 ? 211 : 231);
        EXPECT_EQ(Runtime().events.devilSquareLevels[3][1], 400);
    }
}

// Con un ítem en el cursor (0x7879fbc), ningún botón se ilumina; un dibujo que proyecta no escribe los niveles ni la
// esquina.
TEST_F(EventNpcWindowTest, TheDevilSquareWindowWithAnItemOrProjecting)
{
    Open(0);
    Runtime().cursorItemCount = 1;
    ASSERT_TRUE(Draw({300, 232}, true).HasValue());
    for (const DrawnQuad& quad : QuadsOf(sprites))
    {
        if (quad.slot == 0xf0)
            ExpectColor(quad.color, Color{0.9f, 0.8f, 0.7f, 1.0f});
    }
    EXPECT_EQ(Runtime().ui.npcWindowX, 0);
    EXPECT_EQ(Runtime().events.devilSquareLevels[0][1], 0);
}

// Blood Castle (0x4000): siete botones desde (285, 170) de a 35, de 30 de alto: en gris (0,4, 0,4, 0,5) los que no
// son del nivel del registro propio (100), blanco el del mouse y (0,7, 0,7, 0,9) los demás; la tabla por la clase del
// héroe (+0x1d4), siete accesos al registro propio, el título y 832 en renglones de menos de 26 bytes.
TEST_F(EventNpcWindowTest, TheBloodCastleWindowDrawsItsBridges)
{
    for (const std::uint8_t characterClass : {std::uint8_t{0}, std::uint8_t{4}})
    {
        SCOPED_TRACE(static_cast<int>(characterClass));
        world = std::make_unique<WorldState>();
        sprites.Begin(Screen640);
        text.texts.clear();
        Runtime().map = 2;
        world->Actors()[Runtime().hero].characterClass = characterClass;
        world->OwnCharacter().level = 100;
        Open(1);
        const CrtRandom expected = Advanced(random, 7u * OwnRecordCloseRandoms);
        // El mouse en el segundo puente (170 + 35 ≤ 210 < 235).
        const auto drawn = Draw({300, 210});
        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();

        const std::vector<DrawnQuad> quads = QuadsOf(sprites);
        ASSERT_EQ(quads.size(), 9u);
        // Con la clase 0, el 100 es del puente 2 (81-130); con la 4, también (61-110).
        for (std::size_t row = 0; row < 7; ++row)
        {
            SCOPED_TRACE(row);
            const DrawnQuad& button = quads[2 + row];
            EXPECT_FLOAT_EQ(button.top, 170.0f + 35.0f * static_cast<float>(row));
            ExpectColor(button.color, row == 1 ? White : Color{0.4f, 0.4f, 0.5f, 1.0f});
        }
        ASSERT_GE(text.texts.size(), 8u);
        EXPECT_EQ(text.texts[0].text, characterClass == 0 ? "puente 1 (15-80)" : "puente 1 (10-60)");
        EXPECT_EQ(text.texts[0].x, 295);
        EXPECT_EQ(text.texts[0].y, 182);
        EXPECT_EQ(text.texts[0].style.colors.text, 0xdcffdcbeu);
        EXPECT_EQ(text.texts[6].text, characterClass == 0 ? "puente 7 (331-400)" : "puente 7 (311-400)");
        EXPECT_EQ(text.texts[7].text, "el castillo");
        const std::vector<std::string> story = Client::UI::SplitIntoLines(NpcTexts()[832], 7, 0x1a);
        ASSERT_EQ(text.texts.size(), 8u + story.size());
        for (std::size_t line = 0; line < story.size(); ++line)
        {
            EXPECT_EQ(text.texts[8 + line].text, story[line]);
            EXPECT_EQ(text.texts[8 + line].y, 0x40 + 20 * static_cast<int>(line));
        }
        EXPECT_EQ(random.State(), expected.State());
    }
}

// El paso de Devil Square: el clic sobre un cuadrado (285 ≤ x < 425), con el registro propio abierto (3.848 rand()
// al cerrarlo), el nivel contra el que dejó el dibujo y la invitación de nivel 0 o del cuadrado: el C1 05 90 con el
// lugar + 0x18. Sin clic, nada.
TEST_F(EventNpcWindowTest, TheDevilSquareStepSendsTheEntry)
{
    Open(0);
    world->OwnCharacter().level = 150;
    Runtime().events.devilSquareLevels[1] = {131, 180};
    PutItem(20, 0x1c13, 2);
    ASSERT_TRUE(Step(300, 232).HasValue());
    EXPECT_TRUE(sent.empty());

    Runtime().movement.pendingClick = true;
    const CrtRandom expected = Advanced(random, OwnRecordCloseRandoms);
    const auto stepped = Step(300, 232);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_FALSE(Runtime().movement.pendingClick);
    ASSERT_EQ(sent.size(), 1u);
    const Client::Protocol::Outgoing entry = Client::Protocol::Events::WriteDevilSquareEntry(1, 20 + 0x18);
    EXPECT_TRUE(Client::Tests::SameBytes(sent[0].packet.Bytes(), entry.packet.Bytes()));
    EXPECT_EQ(random.State(), expected.State());
    EXPECT_EQ(Runtime().ui.npcWindowX, 0x104);
}

// Fuera del nivel, 0x65cae0(0x4000000, 0) cierra el inventario y con él el panel 0x1000, que manda el C1 03 31
// (0x658a04), y avisa con 686 (muy alto) o 687 (muy bajo). Sin invitación, 677; con un ítem en el cursor, el clic se
// gasta sin nada más.
TEST_F(EventNpcWindowTest, TheDevilSquareStepWarns)
{
    for (const auto& [level, warning] : {std::pair<std::uint16_t, std::string_view>{200, "muy fuerte"},
                                         std::pair<std::uint16_t, std::string_view>{100, "muy debil"}})
    {
        SCOPED_TRACE(level);
        world = std::make_unique<WorldState>();
        sent.clear();
        Open(0);
        world->OwnCharacter().level = level;
        Runtime().events.devilSquareLevels[1] = {131, 180};
        Runtime().movement.pendingClick = true;
        const auto stepped = Step(300, 232);
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        ASSERT_EQ(sent.size(), 1u);
        EXPECT_TRUE(Client::Tests::SameBytes(sent[0].packet.Bytes(),
                                             Client::Protocol::Connection::WriteWindowClose().packet.Bytes()));
        EXPECT_EQ(Runtime().panels.eventNpc, 0);
        EXPECT_EQ(Runtime().panels.inventory, 0);
        EXPECT_EQ(Runtime().dialogs.current, Client::UI::MessageBoxDialog);
        EXPECT_EQ(std::string_view(Runtime().dialogBox.rows.data()), warning);
    }
    sent.clear();
    world = std::make_unique<WorldState>();
    Open(0);
    world->OwnCharacter().level = 150;
    Runtime().events.devilSquareLevels[1] = {131, 180};
    Runtime().movement.pendingClick = true;
    const auto stepped = Step(300, 232);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_TRUE(sent.empty());
    EXPECT_EQ(Runtime().dialogs.current, Client::UI::MessageBoxDialog);
    EXPECT_EQ(std::string_view(Runtime().dialogBox.rows.data()), "sin invitacion");

    world = std::make_unique<WorldState>();
    Open(0);
    Runtime().cursorItemCount = 1;
    Runtime().movement.pendingClick = true;
    ASSERT_TRUE(Step(300, 232).HasValue());
    EXPECT_FALSE(Runtime().movement.pendingClick);
    EXPECT_EQ(Runtime().dialogs.current, 0);
}

// El paso de Blood Castle: cada puente abre y cierra el registro propio hasta el elegido; un puente que no es del
// nivel no toma el clic. La capa 0x1a12 del puente, y para el 7 también la de nivel 6: el C1 05 9A con el lugar + 12.
// Sin capa, 854.
TEST_F(EventNpcWindowTest, TheBloodCastleStepSendsTheEntry)
{
    Open(1);
    world->OwnCharacter().level = 100;
    PutItem(5, 0x1a12, 2);
    Runtime().movement.pendingClick = true;
    // El puente 1 (15-80) no es del nivel: los siete accesos y el clic sigue.
    CrtRandom expected = Advanced(random, 7u * OwnRecordCloseRandoms);
    ASSERT_TRUE(Step(300, 180).HasValue());
    EXPECT_TRUE(Runtime().movement.pendingClick);
    EXPECT_TRUE(sent.empty());
    EXPECT_EQ(random.State(), expected.State());

    expected = Advanced(random, 2u * OwnRecordCloseRandoms);
    ASSERT_TRUE(Step(300, 210).HasValue());
    EXPECT_FALSE(Runtime().movement.pendingClick);
    ASSERT_EQ(sent.size(), 1u);
    EXPECT_TRUE(Client::Tests::SameBytes(sent[0].packet.Bytes(),
                                         Client::Protocol::Events::WriteBloodCastleEntry(2, 5 + 12).packet.Bytes()));
    EXPECT_EQ(random.State(), expected.State());

    sent.clear();
    world->OwnCharacter().level = 350;
    PutItem(5, 0x1a12, 6);
    Runtime().movement.pendingClick = true;
    ASSERT_TRUE(Step(300, 170 + 6 * 35 + 1).HasValue());
    ASSERT_EQ(sent.size(), 1u);
    EXPECT_TRUE(Client::Tests::SameBytes(sent[0].packet.Bytes(),
                                         Client::Protocol::Events::WriteBloodCastleEntry(7, 5 + 12).packet.Bytes()));

    sent.clear();
    world->Inventory().cells[5].occupied = 0;
    Runtime().movement.pendingClick = true;
    ASSERT_TRUE(Step(300, 170 + 6 * 35 + 1).HasValue());
    EXPECT_TRUE(sent.empty());
    EXPECT_EQ(std::string_view(Runtime().dialogBox.rows.data()), "sin capa");
}
} // namespace
