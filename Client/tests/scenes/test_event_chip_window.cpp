#include "Scenes/EventChipWindow.h"

#include "App/WindowState.h"
#include "Core/Expected.h"
#include "Data/Text/TextTable.h"
#include "Input/InputState.h"
#include "Network/Crypto/Xor32.h"
#include "Protocol/Events.h"
#include "Protocol/Outgoing.h"
#include "Render/Models/LogicalAccess.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/FontSet.h"
#include "Scenes/ActorState.h"
#include "Scenes/EventGlobals.h"
#include "Scenes/GameChat.h"
#include "Scenes/GameHud.h"
#include "Scenes/GameMovementState.h"
#include "Scenes/GamePanels.h"
#include "Scenes/GameUi.h"
#include "Scenes/OwnInventory.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/ChatInput.h"
#include "UI/GameDialogs.h"
#include "UI/OldControls.h"
#include "UI/PanelRegistry.h"
#include "UI/Widget.h"
#include "World/ItemState.h"

#include "support/RecordingTextDrawer.h"
#include "support/SameBytes.h"
#include "support/SceneDataFiles.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// CL9 hito 11, fase 4: la ventana de 0x78abb00 (0x5f6550, su paso 0x5e6520 y la entrada del número de serie) contra
// el ASM de c32bbd5f, sin traza de GL (*estático*). Text.bmd es inventado; el nombre del NPC 236 también.
namespace
{
using Client::Render::Color;
using Client::Render::SpriteBatch;
using Client::Render::Models::CrtRandom;
using Client::Render::Text::FontKind;
using Client::Scenes::GameHudContext;
using Client::Scenes::GameUiContext;
using Client::Scenes::WorldState;

constexpr std::uint32_t Seed = 0x13579bdfu;
constexpr Client::App::PixelSize Screen640{640, 480};
constexpr std::string_view InventedNpc = "arquero inventado";

std::map<std::size_t, std::string> ChipTexts()
{
    std::map<std::size_t, std::string> texts{
        {243, "registrar rena"},   {245, "renas"},     {246, "registradas"}, {706, "muto b"},
        {818, "registrar piedra"}, {819, "piedras"},   {820, "entregadas"},  {893, "serie a"},
        {894, "serie b"},          {895, "confirmar"}, {896, "sin lugar"},   {903, "pedir numero"},
        {916, "serie c"},          {917, "serie d"},   {1002, "cerrar"},
    };
    for (std::size_t line = 0; line < 4; ++line)
    {
        texts[700 + line] = "rena " + std::to_string(line);
        texts[882 + line] = "piedra " + std::to_string(line);
    }
    texts[891] = "historia 0";
    texts[892] = "historia 1";
    texts[897] = "historia 2";
    texts[898] = "historia 3";
    texts[900] = "muto a";
    texts[901] = "muto c";
    texts[902] = "muto d";
    return texts;
}

struct DrawnQuad
{
    std::uint16_t slot = 0;
    Color color{};
    float x = 0.0f;
    float top = 0.0f;
};

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

// Cuántos rand() hay de `from` a `to` (hasta 100.000).
std::uint32_t DrawsBetween(CrtRandom from, std::uint32_t to)
{
    for (std::uint32_t draws = 0; draws < 100000; ++draws)
    {
        if (from.State() == to)
            return draws;
        from.Discard(1);
    }
    ADD_FAILURE() << "más de 100.000 rand()";
    return 0;
}

// El x de 0x623690 con la medida falsa (6 columnas por letra) en 640 × 480.
int Centered(int x, std::string_view text)
{
    return x - static_cast<int>(text.size()) * Client::Tests::RecordingTextDrawer::LetterWidth / 2;
}

std::vector<std::byte> Decoded(const Client::Protocol::Outgoing& packet)
{
    const auto plain = Client::Network::Crypto::Xor32Decode(packet.packet);
    const auto bytes = plain.Bytes();
    return {bytes.begin(), bytes.end()};
}

std::vector<std::byte> Bytes(std::initializer_list<int> values)
{
    std::vector<std::byte> bytes;
    for (const int value : values)
        bytes.push_back(static_cast<std::byte>(value));
    return bytes;
}

class EventChipWindowTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        Reset(0);
    }

    // Un mundo nuevo en `map`, con el inventario vacío (0x5bc740) y abierto.
    void Reset(std::int32_t map)
    {
        world = std::make_unique<WorldState>();
        world->Inventory().ClearForInventoryReply();
        Runtime().map = map;
        Runtime().panels.inventory = 1;
        sprites.Begin(Screen640);
        text.texts.clear();
        sent.clear();
    }

    Client::Scenes::WorldRuntime& Runtime()
    {
        return world->Runtime();
    }

    Client::Core::Expected<void, std::string> Draw(Client::UI::Point mouse, bool projecting = false,
                                                   bool withNames = true)
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
                               .worldClock = worldClock,
                               .mouse = mouse};
        if (withNames)
        {
            context.npcName = [](std::uint16_t npc) -> std::optional<std::string_view>
            {
                if (npc != 236)
                    return std::nullopt;
                return InventedNpc;
            };
        }
        return Client::Scenes::DrawTheEventChipWindow(context);
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
        return Client::Scenes::StepTheEventChipWindow(context, panels.Registry());
    }

    // El estado de rand() después de 0x65cae0(0x4000000, 0) con los paneles de ahora.
    std::uint32_t AfterClosingTheInventory()
    {
        auto other = std::make_unique<WorldState>();
        other->Runtime().panels = Runtime().panels;
        CrtRandom expected = random;
        Client::Scenes::GamePanels panels(*other, expected);
        EXPECT_TRUE(panels.Registry().Close(Client::UI::InventoryPanel).HasValue());
        return expected.State();
    }

    void PutItem(std::size_t cell, std::uint16_t type, std::uint32_t level)
    {
        Client::World::ItemState& item = world->Inventory().cells[cell];
        item.id = type;
        item.scalar1Low = level << 3;
        item.occupied = 1;
    }

    std::unique_ptr<WorldState> world;
    CrtRandom random{Seed};
    std::optional<std::int32_t> sound;
    float worldClock = 0.0f;
    SpriteBatch sprites;
    Client::Tests::RecordingTextDrawer text{sprites};
    Client::Input::InputState input;
    Client::Data::Text::TextTable texts = Client::Tests::MakeTextTable(ChipTexts());
    std::vector<Client::Protocol::Outgoing> sent;
};

// ----- El paso (0x5e6520).

// Con 4: soltar el clic (0x7d366ec) sobre el botón de 0x1e5..0x25d × 0xdc..0xf4 manda el C1 04 98 01 y apaga el
// soltado; el mouse en [0x1c2, 0x280) × [0, 0x1b1) es de la UI. Sin soltar, nada. No saca rand(). En Lorencia y en
// Devias.
TEST_F(EventChipWindowTest, TheMutoButtonAsksForTheNumber)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        Reset(map);
        Runtime().panels.field78abb00 = Client::Scenes::MutoNumberWindow;
        const std::uint32_t before = random.State();
        ASSERT_TRUE(Step(0x1e5, 0xdc).HasValue());
        EXPECT_TRUE(sent.empty());
        EXPECT_EQ(Runtime().ui.mouseOnUi, 1);

        Runtime().movement.releasedClick = true;
        const auto stepped = Step(0x25c, 0xf3);
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        ASSERT_EQ(sent.size(), 1u);
        EXPECT_TRUE(Client::Tests::SameBytes(sent[0].packet.Bytes(), Bytes({0xC1, 0x04, 0x98, 0x10})));
        EXPECT_TRUE(Client::Tests::SameBytes(Decoded(sent[0]), Bytes({0xC1, 0x04, 0x98, 0x01})));
        EXPECT_FALSE(Runtime().movement.releasedClick);
        EXPECT_EQ(random.State(), before);

        // Fuera del botón (0xf4), el soltado se apaga igual al final (0x5e74cc).
        sent.clear();
        Runtime().movement.releasedClick = true;
        Runtime().ui.mouseOnUi = 0;
        ASSERT_TRUE(Step(0x1e5, 0xf4).HasValue());
        EXPECT_TRUE(sent.empty());
        EXPECT_FALSE(Runtime().movement.releasedClick);
        // 0x1b1 ya no es de la UI.
        Runtime().ui.mouseOnUi = 0;
        ASSERT_TRUE(Step(0x1e5, 0x1b1).HasValue());
        EXPECT_EQ(Runtime().ui.mouseOnUi, 0);
    }
}

// Con 1, 2 u otro que no sea 3 ni 4: el soltado sobre 0x1e5..0x25d × 0xe7..0xfd se apaga y, si 0x5583a0 encuentra el
// 0x1c15 de nivel 1 (con 2) o 0 (con los otros), sale el C1 05 95 con 0x78abb00 − 1 y el lugar crudo. Sin el ítem,
// nada. No saca rand(). En Lorencia y en Devias.
TEST_F(EventChipWindowTest, TheChipButtonRegistersTheRenaOrTheStone)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        Reset(map);
        PutItem(31, 0x1c15, 0);
        PutItem(5, 0x1c15, 1);
        const std::uint32_t before = random.State();

        Runtime().panels.field78abb00 = Client::Scenes::RenaChipWindow;
        Runtime().movement.releasedClick = true;
        ASSERT_TRUE(Step(0x1e5, 0xe7).HasValue());
        ASSERT_EQ(sent.size(), 1u);
        EXPECT_TRUE(Client::Tests::SameBytes(sent[0].packet.Bytes(), Bytes({0xC1, 0x05, 0x95, 0x1C, 0xBF})));
        EXPECT_TRUE(Client::Tests::SameBytes(Decoded(sent[0]), Bytes({0xC1, 0x05, 0x95, 0x00, 0x1F})));
        EXPECT_FALSE(Runtime().movement.releasedClick);

        Runtime().panels.field78abb00 = Client::Scenes::StoneChipWindow;
        Runtime().movement.releasedClick = true;
        ASSERT_TRUE(Step(0x25c, 0xfc).HasValue());
        ASSERT_EQ(sent.size(), 2u);
        EXPECT_TRUE(Client::Tests::SameBytes(sent[1].packet.Bytes(), Bytes({0xC1, 0x05, 0x95, 0x1D, 0xA4})));
        EXPECT_TRUE(Client::Tests::SameBytes(Decoded(sent[1]), Bytes({0xC1, 0x05, 0x95, 0x01, 0x05})));

        // Con 5 busca la Rena de nivel 0 y manda el tipo 4.
        Runtime().panels.field78abb00 = 5;
        Runtime().movement.releasedClick = true;
        ASSERT_TRUE(Step(0x1e5, 0xe7).HasValue());
        ASSERT_EQ(sent.size(), 3u);
        EXPECT_TRUE(Client::Tests::SameBytes(Decoded(sent[2]), Bytes({0xC1, 0x05, 0x95, 0x04, 0x1F})));
        EXPECT_EQ(random.State(), before);

        // Sin la Rena, el soltado se gasta sin enviar.
        world->Inventory().cells[31].occupied = 0;
        Runtime().panels.field78abb00 = Client::Scenes::RenaChipWindow;
        Runtime().movement.releasedClick = true;
        ASSERT_TRUE(Step(0x1e5, 0xe7).HasValue());
        EXPECT_EQ(sent.size(), 3u);
        EXPECT_FALSE(Runtime().movement.releasedClick);
    }
}

// Con 3, el botón de confirmar en (ftol(0x1c2 + 35), ftol(esquina + 290)) de 120 × 22: con alguna casilla libre
// (0x558740(−1, −1)) el C1 12 9D con los 12 bytes del EDIT de [0x5763504] en tres pedazos de 4 y un 0; un texto más
// corto deja ceros donde el original deja la pila (P-C247). Después, el izquierdo suelto (0x7d36874).
TEST_F(EventChipWindowTest, TheSerialButtonSendsTheNumber)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        Reset(map);
        Runtime().panels.field78abb00 = Client::Scenes::SerialNumberWindow;
        Runtime().chat.input.dialogEdit.text.SetText("AB12CD34EF56");
        Runtime().movement.releasedClick = true;
        Runtime().movement.leftHeld = true;
        const std::uint32_t before = random.State();
        const auto stepped = Step(485, 290);
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        ASSERT_EQ(sent.size(), 1u);
        EXPECT_TRUE(Client::Tests::SameBytes(sent[0].packet.Bytes(),
                                             Bytes({0xC1, 0x12, 0x9D, 0x55, 0xAB, 0x28, 0x85, 0xF6, 0x96, 0x7A, 0xB7,
                                                    0x35, 0x7C, 0x64, 0x1B, 0x73, 0xCF, 0x04})));
        EXPECT_FALSE(Runtime().movement.leftHeld);
        EXPECT_FALSE(Runtime().movement.releasedClick);
        EXPECT_EQ(random.State(), before);

        Runtime().chat.input.dialogEdit.text.SetText("Q7X");
        Runtime().movement.releasedClick = true;
        ASSERT_TRUE(Step(485 + 119, 290 + 21).HasValue());
        ASSERT_EQ(sent.size(), 2u);
        EXPECT_TRUE(Client::Tests::SameBytes(
            Decoded(sent[1]), Bytes({0xC1, 0x12, 0x9D, 'Q', '7', 'X', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0})));

        // Fuera del botón (y + 22), nada, y el izquierdo sigue.
        Runtime().movement.releasedClick = true;
        Runtime().movement.leftHeld = true;
        ASSERT_TRUE(Step(485, 290 + 22).HasValue());
        EXPECT_EQ(sent.size(), 2u);
        EXPECT_TRUE(Runtime().movement.leftHeld);
    }
}

// Sin casillas libres, 0x5e6d22 abre el aviso 896 y no manda nada; el izquierdo queda suelto igual. Más de 12 bytes en
// el EDIT (que solo pondría un SetText que el juego no hace) pisarían la pila del paso: corta.
TEST_F(EventChipWindowTest, TheSerialButtonWarnsWithAFullInventory)
{
    for (Client::World::ItemState& item : world->Inventory().cells)
    {
        item.id = 0x0001;
        item.occupied = 1;
    }
    Runtime().panels.field78abb00 = Client::Scenes::SerialNumberWindow;
    Runtime().chat.input.dialogEdit.text.SetText("AB12");
    Runtime().movement.releasedClick = true;
    Runtime().movement.leftHeld = true;
    ASSERT_TRUE(Step(500, 300).HasValue());
    EXPECT_TRUE(sent.empty());
    EXPECT_EQ(Runtime().dialogs.current, Client::UI::MessageBoxDialog);
    EXPECT_EQ(std::string_view(Runtime().dialogBox.rows.data()), "sin lugar");
    EXPECT_FALSE(Runtime().movement.leftHeld);

    Reset(0);
    Runtime().panels.field78abb00 = Client::Scenes::SerialNumberWindow;
    Runtime().chat.input.dialogEdit.text.SetText("ABCDEFGHIJKLM");
    Runtime().movement.releasedClick = true;
    const auto cut = Step(500, 300);
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x5e6d68"), std::string::npos) << cut.Error();
}

// 0x5e7255..0x5e74c6: el clic (0x7d366d4) en el botón de cerrar (0x19, 0x18b de la esquina, 24 × 24) apaga el clic,
// pone 0x77f3b10 en 0 y 0x6c8dcc en 6, manda el C1 03 97 en claro, 0x78abb00 en 0, cierra el inventario (con los
// rand() de 0x65cae0), vacía los buffers del chat, lo cierra, apaga 0x77f3b6b y le da el foco a la ventana del juego;
// el texto del EDIT de [0x5763504] queda. En Lorencia y en Devias, con cada tipo.
TEST_F(EventChipWindowTest, TheCloseButtonLeavesTheWindow)
{
    for (const std::int32_t map : {0, 2})
    {
        for (const std::int32_t window : {1, 2, 3, 4})
        {
            SCOPED_TRACE(map * 10 + window);
            Reset(map);
            Runtime().panels.field78abb00 = window;
            Runtime().panels.field78abb04 = 9;
            Runtime().hud.inventoryX = 0x1c2;
            Runtime().hud.inventoryY = 0;
            Runtime().events.serialEntry = window == Client::Scenes::SerialNumberWindow;
            Client::UI::ChatInputState& chat = Runtime().chat.input;
            chat.focus = Client::UI::ChatFocus::Dialog;
            chat.dialogEdit.text.SetText("AB12");
            chat.messageBuffer[0] = 'm';
            chat.messageLength = 1;
            Runtime().ui.chatOpen = true;
            Runtime().movement.pendingClick = true;
            Runtime().movement.mouseUpdateTime = 3;
            const std::uint32_t closed = AfterClosingTheInventory();

            const auto stepped = Step(0x1c2 + 0x19 + 23, 0x18b + 23);
            ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
            ASSERT_EQ(sent.size(), 1u);
            EXPECT_TRUE(Client::Tests::SameBytes(sent[0].packet.Bytes(),
                                                 Client::Protocol::Events::WriteEventWindowLeave().packet.Bytes()));
            EXPECT_EQ(random.State(), closed);
            EXPECT_EQ(Runtime().panels.field78abb00, 0);
            EXPECT_EQ(Runtime().panels.field78abb04, 0);
            EXPECT_EQ(Runtime().panels.inventory, 0);
            EXPECT_FALSE(Runtime().movement.pendingClick);
            EXPECT_EQ(Runtime().movement.mouseUpdateTime, 0);
            EXPECT_EQ(Runtime().movement.mouseUpdateTimeMax, 6);
            EXPECT_FALSE(Runtime().ui.chatOpen);
            EXPECT_FALSE(Runtime().events.serialEntry);
            EXPECT_EQ(chat.focus, Client::UI::ChatFocus::Game);
            EXPECT_EQ(chat.messageBuffer[0], '\0');
            EXPECT_EQ(chat.messageLength, 0);
            EXPECT_EQ(chat.dialogEdit.text.Value(), "AB12");
        }
    }
}

// El cierre saca los rand() de las lecturas protegidas de 0x65cae0 (el comercio y la tienda del NPC, 0x7d531f8 y
// 0x7d53710): dos con el inventario abierto.
TEST_F(EventChipWindowTest, TheCloseButtonDrawsTheInventoryCloseRandoms)
{
    Runtime().panels.field78abb00 = Client::Scenes::RenaChipWindow;
    Runtime().movement.pendingClick = true;
    const CrtRandom before = random;
    ASSERT_TRUE(Step(0x19, 0x18b).HasValue());
    EXPECT_EQ(DrawsBetween(before, random.State()), 2u);
}

// ----- La entrada del número de serie (0x4dd8ce..0x4dd920 y 0x56b6a9..0x56b72a).

// 0x4dd8ce..0x4dd920: el número de serie en cero, 0x553570(0) (el campo elegido y el buffer del mensaje, no el del
// susurro), el chat cerrado, un solo campo para el Tab y 0x77f3b6b en 1. 0x56b6a9..0x56b72a: el campo elegido en 0, el
// buffer del mensaje vacío (el del susurro no), el EDIT vacío, el chat cerrado y 0x77f3b6b en 0, sin tocar el foco.
TEST_F(EventChipWindowTest, TheSerialEntryOpensAndCloses)
{
    Client::UI::ChatInputState& chat = Runtime().chat.input;
    Runtime().events.serial[0] = std::byte{0x41};
    Runtime().ui.chatOpen = true;
    chat.messageBuffer[0] = 'm';
    chat.messageLength = 1;
    chat.whisperBuffer[0] = 'w';
    chat.fieldCount = 2;
    Client::Scenes::OpenTheSerialEntry(Runtime());
    EXPECT_EQ(Runtime().events.serial[0], std::byte{0});
    EXPECT_FALSE(Runtime().ui.chatOpen);
    EXPECT_EQ(chat.messageBuffer[0], '\0');
    EXPECT_EQ(chat.whisperBuffer[0], 'w');
    EXPECT_EQ(chat.fieldCount, 1);
    EXPECT_TRUE(Runtime().events.serialEntry);

    chat.dialogEdit.text.SetText("AB12");
    chat.focus = Client::UI::ChatFocus::Dialog;
    chat.field = 1;
    chat.messageBuffer[0] = 'm';
    chat.whisperBuffer[0] = 'w';
    Runtime().ui.chatOpen = true;
    Client::Scenes::CloseTheSerialEntry(Runtime());
    EXPECT_EQ(chat.field, Client::UI::MessageField);
    EXPECT_EQ(chat.messageBuffer[0], '\0');
    EXPECT_EQ(chat.whisperBuffer[0], 'w');
    EXPECT_EQ(chat.dialogEdit.text.Value(), "");
    EXPECT_EQ(chat.focus, Client::UI::ChatFocus::Dialog);
    EXPECT_FALSE(Runtime().ui.chatOpen);
    EXPECT_FALSE(Runtime().events.serialEntry);
}

// La subclase 0x467b10 con +0x4c en 2: cifras y letras, las minúsculas en mayúscula; nada más.
TEST(EventChipEditTest, TheEditFilterTakesDigitsAndLetters)
{
    using Client::UI::FilteredByTheEdit;
    EXPECT_EQ(FilteredByTheEdit(2, 'a'), std::optional<char>('A'));
    EXPECT_EQ(FilteredByTheEdit(2, 'Z'), std::optional<char>('Z'));
    EXPECT_EQ(FilteredByTheEdit(2, '7'), std::optional<char>('7'));
    EXPECT_EQ(FilteredByTheEdit(2, '-'), std::nullopt);
    EXPECT_EQ(FilteredByTheEdit(2, static_cast<char>(0xb0)), std::nullopt);
    EXPECT_EQ(FilteredByTheEdit(1, 'a'), std::nullopt);
    EXPECT_EQ(FilteredByTheEdit(1, '3'), std::optional<char>('3'));
    EXPECT_EQ(FilteredByTheEdit(0, '-'), std::optional<char>('-'));
    EXPECT_EQ(FilteredByTheEdit(0, static_cast<char>(0x1b)), std::nullopt);
    EXPECT_EQ(FilteredByTheEdit(2, static_cast<char>(0x1a)), std::nullopt);

    Client::UI::OldEdit edit;
    edit.filter = Client::UI::UppercaseLettersAndDigitsEdit;
    edit.text.SetLimit(12);
    Client::UI::TypeInTheEdit(edit, "ab-12 cd34ef56gh");
    EXPECT_EQ(edit.text.Value(), "AB12CD34EF56");
}

// ----- El dibujo (0x5f6550).

// 0x5f65df: el título es el NPC 236 de la tabla de NpcName(%s).txt, que MuRet no carga: sin ella, corta.
TEST_F(EventChipWindowTest, TheTitleNeedsTheNpcNames)
{
    Runtime().panels.field78abb00 = Client::Scenes::RenaChipWindow;
    const auto drawn = Draw({0, 0}, false, false);
    ASSERT_FALSE(drawn.HasValue());
    EXPECT_NE(drawn.Error().find("0x5f65df"), std::string::npos) << drawn.Error();
}

// Con 1: la esquina en (0x1c2, 0), el fondo, el título, el botón 0xf0 en (485, 231) blanco y con el texto 243 en
// negrita (hay Renas), el botón de cerrar, los cuatro textos 700..703 desde y 100 de a 15, el rótulo 245 y "x2" de las
// dos Renas de nivel 0, el 246 y "x7" de 0x78abb04. No saca rand(). En Lorencia y en Devias.
TEST_F(EventChipWindowTest, TheRenaWindowDrawsItsCounts)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        Reset(map);
        PutItem(3, 0x1c15, 0);
        PutItem(4, 0x1c15, 0);
        PutItem(5, 0x1c15, 1);
        Runtime().panels.field78abb00 = Client::Scenes::RenaChipWindow;
        Runtime().panels.field78abb04 = 7;
        const std::uint32_t before = random.State();
        const auto drawn = Draw({0, 0});
        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
        EXPECT_EQ(random.State(), before);
        EXPECT_EQ(Runtime().hud.inventoryX, 0x1c2);
        EXPECT_EQ(Runtime().hud.inventoryY, 0);

        const std::vector<DrawnQuad> quads = QuadsOf(sprites);
        ASSERT_EQ(quads.size(), 4u);
        EXPECT_EQ(quads[0].slot, 0x104);
        EXPECT_FLOAT_EQ(quads[0].x, 450.0f);
        EXPECT_EQ(quads[1].slot, 0x105);
        EXPECT_EQ(quads[2].slot, 0xf0);
        EXPECT_FLOAT_EQ(quads[2].x, 485.0f);
        EXPECT_FLOAT_EQ(quads[2].top, 231.0f);
        EXPECT_FLOAT_EQ(quads[2].color.red, 1.0f);
        EXPECT_EQ(quads[3].slot, 0x118);
        EXPECT_FLOAT_EQ(quads[3].x, 475.0f);
        EXPECT_FLOAT_EQ(quads[3].top, 395.0f);

        const auto& lines = text.texts;
        ASSERT_EQ(lines.size(), 10u);
        EXPECT_EQ(lines[0].text, InventedNpc);
        EXPECT_EQ(lines[0].x, 0x1c2 + 0x23);
        EXPECT_EQ(lines[0].y, 0xc);
        EXPECT_EQ(lines[1].text, "registrar rena");
        EXPECT_EQ(lines[1].x, Centered(0x221, "registrar rena"));
        EXPECT_EQ(lines[1].y, 0xec);
        EXPECT_EQ(lines[1].style.font, FontKind::Bold);
        EXPECT_EQ(lines[1].style.colors.text, 0xffd2e6ffu);
        for (std::size_t line = 0; line < 4; ++line)
        {
            SCOPED_TRACE(line);
            const std::string expected = "rena " + std::to_string(line);
            EXPECT_EQ(lines[2 + line].text, expected);
            EXPECT_EQ(lines[2 + line].x, Centered(0x1c2 + 95, expected));
            EXPECT_EQ(lines[2 + line].y, 100 + 15 * static_cast<int>(line));
            EXPECT_EQ(lines[2 + line].style.font, FontKind::Normal);
        }
        EXPECT_EQ(lines[6].text, "renas");
        EXPECT_EQ(lines[6].x, 0x1e5);
        EXPECT_EQ(lines[6].y, 0x122);
        EXPECT_EQ(lines[6].style.font, FontKind::Bold);
        EXPECT_EQ(lines[7].text, "x2");
        EXPECT_EQ(lines[7].x, 0x230);
        EXPECT_EQ(lines[7].y, 300);
        EXPECT_EQ(lines[7].style.font, FontKind::BigBold);
        EXPECT_EQ(lines[7].style.colors.text, 0xff0096d4u);
        EXPECT_EQ(lines[8].text, "registradas");
        EXPECT_EQ(lines[8].y, 0x145);
        EXPECT_EQ(lines[8].style.colors.text, 0xffd2e6ffu);
        EXPECT_EQ(lines[9].text, "x7");
        EXPECT_EQ(lines[9].y, 335);
    }
}

// Con 2 y sin piedras de nivel 1: el botón en gris (0,4) y su texto 818 con la fuente normal, y "x0".
TEST_F(EventChipWindowTest, TheStoneWindowWithoutStonesIsGrey)
{
    PutItem(3, 0x1c15, 0);
    Runtime().panels.field78abb00 = Client::Scenes::StoneChipWindow;
    ASSERT_TRUE(Draw({0, 0}).HasValue());
    const std::vector<DrawnQuad> quads = QuadsOf(sprites);
    ASSERT_GE(quads.size(), 3u);
    EXPECT_FLOAT_EQ(quads[2].color.red, 0.4f);
    ASSERT_GE(text.texts.size(), 8u);
    EXPECT_EQ(text.texts[1].text, "registrar piedra");
    EXPECT_EQ(text.texts[1].style.font, FontKind::Normal);
    EXPECT_EQ(text.texts[2].text, "piedra 0");
    EXPECT_EQ(text.texts[6].text, "piedras");
    EXPECT_EQ(text.texts[7].text, "x0");
}

// Con el botón apretado (0x7d366d4) bajo el mouse y el soltado (0x7d366ec), el dibujo apaga el clic y el izquierdo;
// uno que proyecta no.
TEST_F(EventChipWindowTest, ThePressedButtonTakesTheClick)
{
    Runtime().panels.field78abb00 = Client::Scenes::MutoNumberWindow;
    Runtime().movement.pendingClick = true;
    Runtime().movement.releasedClick = true;
    Runtime().movement.leftHeld = true;
    ASSERT_TRUE(Draw({0x1e5, 0xdc}, true).HasValue());
    EXPECT_TRUE(Runtime().movement.pendingClick);
    EXPECT_TRUE(Runtime().movement.leftHeld);
    ASSERT_TRUE(Draw({0x1e5, 0xdc}).HasValue());
    EXPECT_FALSE(Runtime().movement.pendingClick);
    EXPECT_FALSE(Runtime().movement.leftHeld);
    const std::vector<DrawnQuad> quads = QuadsOf(sprites);
    bool pressed = false;
    for (const DrawnQuad& quad : quads)
        pressed = pressed || (quad.slot == 0xf0 && quad.top == 220.0f && quad.color.red == 0.4f);
    EXPECT_TRUE(pressed);
}

// Con 3: los textos 893, 916, 917 (en 0xff67bfdf) y 894 (en negrita) sobre la entrada, el marco 0x10f, el EDIT de
// [0x5763504] reconfigurado (filtro 2, fondo 0, tope 12, 150 × 14 en (495, 265)) con el foco, el botón 895 40 más
// abajo, los textos 891, 892, 897 y 898 desde y 70 y, con el resultado del 9D, su sombra y él en negrita 0x104 más
// abajo. Sin cuentas. Uno que proyecta no toca el EDIT ni el foco.
TEST_F(EventChipWindowTest, TheSerialWindowDrawsTheEntry)
{
    Runtime().panels.field78abb00 = Client::Scenes::SerialNumberWindow;
    const std::string_view result = "R3SULT0";
    for (std::size_t at = 0; at < result.size(); ++at)
        Runtime().events.serial[at] = static_cast<std::byte>(result[at]);
    ASSERT_TRUE(Draw({0, 0}, true).HasValue());
    EXPECT_EQ(Runtime().chat.input.focus, Client::UI::ChatFocus::Game);
    EXPECT_EQ(Runtime().chat.input.dialogEdit.filter, 0);

    sprites.Begin(Screen640);
    text.texts.clear();
    const std::uint32_t before = random.State();
    const auto drawn = Draw({0, 0});
    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    EXPECT_EQ(random.State(), before);
    const Client::UI::OldEdit& edit = Runtime().chat.input.dialogEdit;
    EXPECT_EQ(Runtime().chat.input.focus, Client::UI::ChatFocus::Dialog);
    EXPECT_EQ(edit.filter, Client::UI::UppercaseLettersAndDigitsEdit);
    EXPECT_EQ(edit.backgroundColor, 0u);
    EXPECT_EQ(edit.base.x, 495);
    EXPECT_EQ(edit.base.y, 265);
    EXPECT_EQ(edit.base.width, 150);
    EXPECT_EQ(edit.base.height, 14);

    std::vector<std::string> said;
    for (const Client::Tests::RecordedText& line : text.texts)
        said.push_back(line.text);
    const std::vector<std::string> expected{std::string(InventedNpc),
                                            "serie a",
                                            "serie c",
                                            "serie d",
                                            "serie b",
                                            "confirmar",
                                            "historia 0",
                                            "historia 1",
                                            "historia 2",
                                            "historia 3",
                                            "R3SULT0",
                                            "R3SULT0"};
    ASSERT_EQ(said, expected);
    const auto& lines = text.texts;
    EXPECT_EQ(lines[1].x, Centered(541, "serie a"));
    EXPECT_EQ(lines[1].y, 250 - 0x28);
    EXPECT_EQ(lines[2].y, 250 - 0xf);
    EXPECT_EQ(lines[2].style.colors.text, 0xff67bfdfu);
    EXPECT_EQ(lines[3].y, 250 - 5);
    EXPECT_EQ(lines[4].y, 250 - 0x19);
    EXPECT_EQ(lines[4].style.font, FontKind::Bold);
    EXPECT_EQ(lines[5].x, Centered(545, "confirmar"));
    EXPECT_EQ(lines[5].y, 295);
    EXPECT_EQ(lines[6].x, Centered(545, "historia 0"));
    EXPECT_EQ(lines[6].y, 70);
    EXPECT_EQ(lines[9].y, 70 + 45);
    EXPECT_EQ(lines[10].x, Centered(546, "R3SULT0"));
    EXPECT_EQ(lines[10].y, 70 + 0x104 + 1);
    EXPECT_EQ(lines[10].style.font, FontKind::Bold);
    EXPECT_EQ(lines[11].y, 70 + 0x104);
    EXPECT_EQ(lines[11].style.colors.text, 0xff67d2ffu);

    bool frame = false;
    for (const DrawnQuad& quad : QuadsOf(sprites))
        frame = frame || (quad.slot == 0x10f && quad.x == 488.0f && quad.top == 260.0f);
    EXPECT_TRUE(frame);
}

// Con 4: el botón 903 en y 220 (texto en 0xe1), los textos 900, 706, 901 y 902 desde y 70 y las dos cuentas sin
// rótulos.
TEST_F(EventChipWindowTest, TheMutoWindowDrawsItsButton)
{
    Runtime().panels.field78abb00 = Client::Scenes::MutoNumberWindow;
    Runtime().panels.field78abb04 = 3;
    ASSERT_TRUE(Draw({0, 0}).HasValue());
    std::vector<std::string> said;
    for (const Client::Tests::RecordedText& line : text.texts)
        said.push_back(line.text);
    const std::vector<std::string> expected{
        std::string(InventedNpc), "pedir numero", "muto a", "muto b", "muto c", "muto d", "x0", "x3"};
    ASSERT_EQ(said, expected);
    EXPECT_EQ(text.texts[1].y, 0xe1);
    EXPECT_EQ(text.texts[2].y, 70);
    EXPECT_EQ(text.texts[6].y, 300);
    EXPECT_EQ(text.texts[7].y, 335);
}
} // namespace
