#include "Scenes/SecondGuildWindow.h"

#include "App/WindowState.h"
#include "Core/Expected.h"
#include "Data/Text/TextTable.h"
#include "Network/Crypto/Xor32.h"
#include "Protocol/Guild.h"
#include "Protocol/Outgoing.h"
#include "Render/Models/Cloth.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/FontSet.h"
#include "Render/TextureSlot.h"
#include "Scenes/ActorState.h"
#include "Scenes/GameChat.h"
#include "Scenes/GameHud.h"
#include "Scenes/GameMovementState.h"
#include "Scenes/GamePanels.h"
#include "Scenes/GuildMarks.h"
#include "Scenes/GuildPanels.h"
#include "Scenes/GuildReceive.h"
#include "Scenes/GuildState.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/ChatInput.h"
#include "UI/Dialogs.h"
#include "UI/OldWindowBase.h"
#include "UI/PanelDialog.h"
#include "UI/PanelRegistry.h"

#include "support/FakeTextureStore.h"
#include "support/RecordingTextDrawer.h"
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

// La ventana 2 del gremio (CL9 hito 8, fase 3): 0x654900 con su paso (0x45aa80(0) y 0x656df0), su dibujo (0x656e70),
// las cuatro páginas (0x655d70, 0x654f80, 0x655c10 y 0x6552b0, y sus dibujos), el editor de la marca (0x6535c0 y
// 0x653750) y el alta de punta a punta: el 54 que la abre, Crear, el nombre y la marca, y el C1 55. En Lorencia (0) y
// en Devias (2, la del Guild Master). Los nombres y los textos son inventados.
namespace
{
using Client::Render::BlendMode;
using Client::Render::SpriteBatch;
using Client::Render::SpriteVertex;
using Client::Render::Models::CrtRandom;
using Client::Scenes::GameHudContext;
using Client::Scenes::GuildMarkTextures;
using Client::Scenes::GuildState;
using Client::Scenes::WorldState;

constexpr std::uint32_t Seed = 11;
constexpr Client::App::PixelSize Screen640{640, 480};
constexpr int NoTexture = -1;
constexpr int Marker = -2;
// La ventana en (0x1c2, 0).
constexpr std::int32_t WindowX = 0x1c2;
constexpr std::int32_t WindowY = 0;
constexpr float Left = 450.0f;
constexpr std::size_t Creating = 1999;
constexpr std::uint32_t Now = 5000;

struct DrawnQuad
{
    int slot = NoTexture;
    BlendMode blend = BlendMode::Alpha;
    SpriteVertex first{};
    SpriteVertex third{};
};

std::vector<DrawnQuad> QuadsOf(const SpriteBatch& sprites)
{
    std::vector<DrawnQuad> quads;
    const auto vertices = sprites.Vertices();
    for (const Client::Render::DrawCommand& command : sprites.Commands())
    {
        int slot = NoTexture;
        if (command.texture == Client::Tests::TextMarkerSlot)
            slot = Marker;
        else if (command.texture)
            slot = command.texture->value;
        for (std::size_t quad = 0; quad < command.vertexCount / 6; ++quad)
        {
            const std::size_t first = command.firstVertex + quad * 6;
            quads.push_back(DrawnQuad{
                .slot = slot, .blend = command.blend, .first = vertices[first], .third = vertices[first + 2]});
        }
    }
    return quads;
}

void ExpectQuadAt(const DrawnQuad& quad, int slot, float x, float y, float width, float height)
{
    constexpr float Tolerance = 1e-3f;
    EXPECT_EQ(quad.slot, slot);
    EXPECT_NEAR(quad.first.x, x, Tolerance);
    EXPECT_NEAR(quad.first.y, 480.0f - y, Tolerance);
    EXPECT_NEAR(quad.third.x, x + width, Tolerance);
    EXPECT_NEAR(quad.third.y, 480.0f - y - height, Tolerance);
}

int SwatchSlot(int color)
{
    return Client::Render::FirstGuildSwatchTextureSlot.value + color;
}

int NewMarkSlot()
{
    return GuildMarkTextures::SlotOf(static_cast<std::int32_t>(Creating), true).value;
}

std::vector<std::byte> PlainOf(const Client::Protocol::Outgoing& sent)
{
    const auto plain = Client::Network::Crypto::Xor32Decode(sent.packet);
    return {plain.Bytes().begin(), plain.Bytes().end()};
}

std::vector<std::byte> BytesOf(std::initializer_list<int> values)
{
    std::vector<std::byte> bytes;
    for (const int value : values)
        bytes.push_back(static_cast<std::byte>(value));
    return bytes;
}

template <std::size_t Size> void Fill(std::array<char, Size>& target, std::string_view text)
{
    target = {};
    for (std::size_t i = 0; i < text.size() && i + 1 < Size; ++i)
        target[i] = text[i];
}

int Draws(const CrtRandom& random)
{
    CrtRandom reference(Seed);
    for (int draws = 0; draws <= 1000; ++draws)
    {
        if (reference.State() == random.State())
            return draws;
        static_cast<void>(reference.Next());
    }
    return -1;
}

// Los textos de la ventana, inventados.
Client::Data::Text::TextTable WindowTexts()
{
    const std::map<std::size_t, std::string> texts{
        {180, "Gremio"},        {181, "Armas uno?"},  {182, "nombre"},   {183, "Elegi un color"}, {184, "y pinta"},
        {228, "Listo"},         {457, "Operador"},    {458, "Servidor"}, {1002, "Cerrar"},        {1269, "Todavia no"},
        {1303, "Armar gremio"}, {1304, "Otra marca"}, {1305, "Sigue"},   {1306, "Vuelve"}};
    auto table = Client::Data::Text::LoadTextTable(Client::Tests::TextTableBytes(texts));
    EXPECT_TRUE(table.HasValue());
    return std::move(table).Value();
}

class SecondGuildWindowTest : public ::testing::TestWithParam<std::int32_t>
{
protected:
    SecondGuildWindowTest() : m_text(m_sprites)
    {
        m_sprites.Begin(Screen640);
        Runtime().map = GetParam();
        Hero().guildStatus = 0xff;
        Hero().word1f2 = 0xffff;
    }

    Client::Scenes::WorldRuntime& Runtime()
    {
        return m_world->Runtime();
    }

    Client::Scenes::ActorState& Hero()
    {
        return m_world->Actors()[Runtime().hero];
    }

    GuildState& Guild()
    {
        return Runtime().guild;
    }

    Client::UI::ChatInputState& Input()
    {
        return Runtime().chat.input;
    }

    Client::Scenes::GuildSender Sender()
    {
        return [this](Client::Protocol::Outgoing packet) { m_sent.push_back(std::move(packet)); };
    }

    // Lo que hace el 54 (0x4e4279): abre el panel 0x40000.
    void OpenIt()
    {
        Client::Scenes::GamePanels panels(*m_world, m_random);
        panels.LendTheGuild(*m_world, Sender());
        auto opened = panels.Registry().Open(Client::UI::SecondGuildPanel);
        ASSERT_TRUE(opened.HasValue()) << opened.Error();
        ASSERT_TRUE(opened.Value());
    }

    Client::Core::Expected<void, std::string> Step()
    {
        return Client::Scenes::StepTheSecondGuildWindow(
            Client::Scenes::SecondGuildStepContext{.world = *m_world,
                                                   .mouseX = m_mouseX,
                                                   .mouseY = m_mouseY,
                                                   .now = Now,
                                                   .measure = &m_text,
                                                   .texts = &m_texts,
                                                   .filterName = m_filterName,
                                                   .filter = m_filter,
                                                   .send = Sender()});
    }

    Client::Core::Expected<void, std::string> Draw(bool withMarks = true)
    {
        Client::Scenes::WorldRuntime& runtime = Runtime();
        Client::Scenes::ActorState& hero = Hero();
        GameHudContext context{.world = *m_world,
                               .random = m_random,
                               .ownRecordDepth = runtime.ownRecordDepth,
                               .cellXDepth = hero.cellXAccesses,
                               .cellYDepth = hero.cellYAccesses,
                               .hud = runtime.hud,
                               .crywolfSandOverlay = runtime.crywolf.sandOverlay,
                               .sound = m_sound,
                               .sprites = m_sprites,
                               .text = m_text,
                               .texts = &m_texts,
                               .mouse = {m_mouseX, m_mouseY},
                               .guildMarks = withMarks ? &m_marks : nullptr};
        return Client::Scenes::DrawTheSecondGuildWindow(context);
    }

    // Un cuadro de la escena: el paso de la UI y el dibujo, con el lote vacío.
    void Frame()
    {
        auto stepped = Step();
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        m_sprites.Begin(Screen640);
        m_text.texts.clear();
        auto drawn = Draw();
        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    }

    // 0x45b200: el flanco sobre el botón y, el cuadro que sigue, soltarlo encima.
    void Click(std::size_t button)
    {
        const Client::UI::OldWindowBase& base = Guild().secondWindow.buttons[button].base;
        m_mouseX = base.x + base.width / 2;
        m_mouseY = base.y + base.height / 2;
        Client::Scenes::GameMovementState& movement = Runtime().movement;
        movement.pendingClick = true;
        movement.leftHeld = true;
        Frame();
        movement.pendingClick = false;
        movement.leftHeld = false;
        movement.releasedClick = true;
        Frame();
        movement.releasedClick = false;
    }

    // El izquierdo abajo sobre el cuadro (fila, columna) de la grilla de la marca.
    void Paint(std::int32_t row, std::int32_t column)
    {
        m_mouseX = WindowX + 0x32 + column * 15 + 7;
        m_mouseY = WindowY + 0x64 + row * 15 + 7;
        Runtime().movement.leftHeld = true;
        Frame();
        Runtime().movement.leftHeld = false;
    }

    // El flanco del clic sobre el color (fila, columna) de la paleta.
    void Choose(std::int32_t row, std::int32_t column)
    {
        m_mouseX = WindowX + 0xf + column * 20 + 10;
        m_mouseY = WindowY + 0x104 + row * 20 + 10;
        Runtime().movement.pendingClick = true;
        Frame();
    }

    std::unique_ptr<WorldState> m_world = std::make_unique<WorldState>();
    CrtRandom m_random{Seed};
    Client::Data::Text::TextTable m_texts = WindowTexts();
    std::vector<std::string> m_filterName{"Malo"};
    std::vector<std::string> m_filter{"feo"};
    std::vector<Client::Protocol::Outgoing> m_sent;
    std::optional<std::int32_t> m_sound{};
    std::int32_t m_mouseX = 0;
    std::int32_t m_mouseY = 300;
    SpriteBatch m_sprites;
    Client::Tests::RecordingTextDrawer m_text;
    Client::Tests::FakeTextureStore m_store;
    GuildMarkTextures m_marks{m_store};
};

// 0x654900: la ventana en (0x1c2, 0) de 0xbe × 0x5a, la página 0, y los botones de 100 × 20 y de 50 × 18 colgados de
// ella con los comandos 1, 2, 5 y 4.
TEST_P(SecondGuildWindowTest, TheWindowStartsOnTheQuestion)
{
    const Client::Scenes::SecondGuildWindowState& window = Guild().secondWindow;
    EXPECT_EQ(window.base.x, WindowX);
    EXPECT_EQ(window.base.y, WindowY);
    EXPECT_EQ(window.base.width, 0xbe);
    EXPECT_EQ(window.base.height, 0x5a);
    EXPECT_EQ(Guild().createKind, 0);
    EXPECT_EQ(Guild().createStep, 0);
    const std::array<std::int32_t, 4> commands{1, 2, 5, 4};
    for (std::size_t button = 0; button < commands.size(); ++button)
    {
        EXPECT_EQ(window.buttons[button].command, commands[button]);
        EXPECT_EQ(window.buttons[button].base.parent, window.base.id);
    }
    EXPECT_EQ(window.buttons[2].base.x, WindowX + 0x61);
    EXPECT_EQ(window.buttons[3].base.x, WindowX + 0x2d);
    EXPECT_EQ(window.buttons[3].base.y, 0x168);
}

// 0x656e70 y 0x6563e0 sin gremio: el fondo opaco, el título "Gremio" en negrita centrado en 120, la pregunta en
// (x + 0x19, y + 0x3c), Crear a la vista y la marca apagada en x + 0x2d, 0x1e y 0x36 más abajo, y el botón de cerrar.
TEST_P(SecondGuildWindowTest, TheQuestionPageDrawsTheQuestionAndPlacesTheButtons)
{
    ASSERT_TRUE(Draw().HasValue());
    const std::vector<DrawnQuad> quads = QuadsOf(m_sprites);
    ASSERT_EQ(quads.size(), 9u);
    ExpectQuadAt(quads[0], 0x104, 450.0f, 0.0f, 190.0f, 256.0f);
    EXPECT_EQ(quads[0].blend, BlendMode::Opaque);
    ExpectQuadAt(quads[1], 0x105, 450.0f, 256.0f, 190.0f, 177.0f);
    ExpectQuadAt(quads[8], 0x118, 475.0f, 395.0f, 24.0f, 24.0f);
    const auto& texts = m_text.texts;
    ASSERT_EQ(texts.size(), 4u);
    EXPECT_EQ(texts[0].text, "Gremio");
    EXPECT_EQ(texts[0].style.font, Client::Render::Text::FontKind::Bold);
    EXPECT_EQ(texts[0].style.colors.text, 0xffdcdcdcu);
    EXPECT_EQ(texts[0].style.colors.background, 0xff141414u);
    EXPECT_EQ(texts[1].text, "Armas uno?");
    EXPECT_EQ(texts[1].x, WindowX + 0x19);
    EXPECT_EQ(texts[1].y, 0x3c);
    EXPECT_EQ(texts[1].style.colors.text, 0xffe6e6e6u);
    EXPECT_EQ(texts[2].text, "Armar gremio");
    EXPECT_EQ(texts[3].text, "Otra marca");
    const Client::Scenes::SecondGuildWindowState& window = Guild().secondWindow;
    EXPECT_EQ(window.buttons[0].base.x, WindowX + 0x2d);
    EXPECT_EQ(window.buttons[0].base.y, 0x3c + 0x1e);
    EXPECT_EQ(window.buttons[0].base.state, Client::UI::ShownWindow);
    EXPECT_EQ(window.buttons[1].base.y, 0x3c + 0x1e + 0x18);
    EXPECT_EQ(window.buttons[1].base.state, Client::UI::DisabledWindow);
    EXPECT_EQ(Draws(m_random), 0);
}

// Con gremio (+0x368 distinto de 0xff), su nombre y el puntaje centrados en la caja de 140 (0x5542f0 con 2), Crear
// apagado y la marca a la vista; con el mouse sobre el botón de cerrar, su ayuda.
TEST_P(SecondGuildWindowTest, WithAGuildTheQuestionPageShowsTheGuildAndTheScore)
{
    Hero().guildStatus = 0x80;
    Hero().word1f2 = 4;
    Fill(Guild().table[4].guildName, "Talas");
    Guild().score = 17;
    m_mouseX = 480;
    m_mouseY = 400;
    ASSERT_TRUE(Draw().HasValue());
    const auto& texts = m_text.texts;
    ASSERT_EQ(texts.size(), 5u);
    EXPECT_EQ(texts[1].text, "Talas ( Score:17 )");
    EXPECT_TRUE(texts[1].windowed);
    EXPECT_EQ(texts[1].x, WindowX + 0x19);
    EXPECT_EQ(texts[1].columns, 140);
    EXPECT_EQ(texts[4].text, "Cerrar");
    EXPECT_EQ(Guild().secondWindow.buttons[0].base.state, Client::UI::DisabledWindow);
    EXPECT_EQ(Guild().secondWindow.buttons[1].base.state, Client::UI::ShownWindow);
}

// 0x655d70: Crear arma un gremio (las dos en 1, el campo del alta prendido) y manda C1 04 54 01.
TEST_P(SecondGuildWindowTest, CreateGoesToTheNamePageAndAnswersYes)
{
    OpenIt();
    ASSERT_TRUE(Draw().HasValue());
    Click(Client::Scenes::SecondGuildCreateButton);
    EXPECT_EQ(Guild().createKind, 1);
    EXPECT_EQ(Guild().createStep, 1);
    EXPECT_TRUE(Input().dialogFieldOn);
    ASSERT_EQ(m_sent.size(), 1u);
    EXPECT_EQ(PlainOf(m_sent[0]), BytesOf({0xc1, 0x04, 0x54, 0x01}));
    // 0x45b200: el sonido del botón, 0x19.
    EXPECT_EQ(Runtime().lastSoundId, std::optional<std::int32_t>(0x19));
}

// 0x6560b3: la marca abre el diálogo de los paneles con el 1269 y Aceptar, y lo guarda en +0x2a0.
TEST_P(SecondGuildWindowTest, TheMarkButtonOpensTheNotEnabledDialog)
{
    Hero().guildStatus = 0x80;
    Hero().word1f2 = 4;
    OpenIt();
    ASSERT_TRUE(Draw().HasValue());
    Click(Client::Scenes::SecondGuildMarkButton);
    const Client::UI::PanelDialogState& dialog = Runtime().panels.delegated.panelDialog;
    ASSERT_NE(Client::UI::PanelDialogHandle(dialog), 0u);
    EXPECT_EQ(Guild().secondWindow.dialog2a0, Client::UI::PanelDialogHandle(dialog));
    EXPECT_EQ(Guild().createStep, 0);
    EXPECT_TRUE(m_sent.empty());
}

// 0x6560c3..0x656124: el flanco sobre el botón de cerrar (0x18 × 0x18 en x + 0x19, y + 0x18b) lo apaga, deja
// 0x77f3b10 en 0 y 0x6c8dcc en 6, manda 54 00, suena el 0x19 y cierra con 0x6570b0, que manda otro 54 00 (Q-C1289).
TEST_P(SecondGuildWindowTest, TheCloseButtonAnswersNoTwiceAndClosesTheWindow)
{
    OpenIt();
    Runtime().movement.mouseUpdateTime = 3;
    m_mouseX = WindowX + 0x19 + 5;
    m_mouseY = 0x18b + 5;
    Runtime().movement.pendingClick = true;
    ASSERT_TRUE(Step().HasValue());
    EXPECT_FALSE(Runtime().movement.pendingClick);
    EXPECT_EQ(Runtime().movement.mouseUpdateTime, 0);
    EXPECT_EQ(Runtime().movement.mouseUpdateTimeMax, 6);
    EXPECT_EQ(Runtime().panels.delegated.secondGuild, 0);
    ASSERT_EQ(m_sent.size(), 2u);
    EXPECT_EQ(PlainOf(m_sent[0]), BytesOf({0xc1, 0x04, 0x54, 0x00}));
    EXPECT_EQ(PlainOf(m_sent[1]), BytesOf({0xc1, 0x04, 0x54, 0x00}));
    // 0x41ed10(0x19) y después el 0x1c de 0x6570b0.
    EXPECT_EQ(Runtime().lastSoundId, std::optional<std::int32_t>(0x1c));
}

// 0x45aa80(0): con el mouse sobre la ventana y otra que agarró el mouse, no da su paso; sin nadie, la toma y, con el
// mouse en su alto de 0x1b1, prende 0x775857c.
TEST_P(SecondGuildWindowTest, TheGateSkipsTheStepWhenAnotherWindowHoldsTheMouse)
{
    OpenIt();
    m_mouseX = WindowX + 10;
    m_mouseY = 10;
    Runtime().ui.capture = 42;
    Runtime().ui.mouseOnUi = 0;
    ASSERT_TRUE(Step().HasValue());
    EXPECT_EQ(Runtime().ui.hovered, 0);
    EXPECT_EQ(Runtime().ui.mouseOnUi, 0);
    Runtime().ui.capture = 0;
    ASSERT_TRUE(Step().HasValue());
    EXPECT_EQ(Runtime().ui.hovered, Client::Scenes::SecondGuildWindowId);
    EXPECT_EQ(Runtime().ui.mouseOnUi, 1);
}

// 0x6535c0: el ángulo del héroe en 112,5; el izquierdo pinta con el color elegido, el derecho borra, y el flanco
// sobre la paleta elige (fila · 8 + columna) y suena el 0x19.
TEST_P(SecondGuildWindowTest, TheEditorPaintsErasesAndChoosesColors)
{
    OpenIt();
    Guild().createKind = 1;
    Guild().createStep = 1;
    Choose(1, 3);
    EXPECT_EQ(Guild().markColor, 11);
    EXPECT_FALSE(Runtime().movement.pendingClick);
    EXPECT_EQ(Runtime().lastSoundId, std::optional<std::int32_t>(0x19));
    EXPECT_FLOAT_EQ(Hero().angles.z, 112.5f);
    Paint(2, 5);
    EXPECT_EQ(Guild().table[Creating].mark[2 * 8 + 5], 11);
    m_mouseX = WindowX + 0x32 + 5 * 15;
    m_mouseY = 0x64 + 2 * 15;
    Runtime().ui.rightDown = true;
    ASSERT_TRUE(Step().HasValue());
    Runtime().ui.rightDown = false;
    EXPECT_EQ(Guild().table[Creating].mark[2 * 8 + 5], 0);
}

// 0x655130 y 0x653750: la caja del nombre, "nombre" en negrita, lo tecleado en (x + 0x3f, y + 0x3c), la grilla de
// 13 en (x + 51, y + 101) de a 15 con su marco 0x116, la paleta de 18 en (x + 16, y + 261) de a 20, el color elegido
// de 23 en (x + 16, y + 231), los dos renglones y Regreso y Próximo.
TEST_P(SecondGuildWindowTest, TheNamePageDrawsTheFieldAndTheEditor)
{
    OpenIt();
    Guild().createKind = 1;
    Guild().createStep = 1;
    Guild().markColor = 6;
    Guild().table[Creating].mark[9] = 4;
    ASSERT_TRUE(Draw().HasValue());
    EXPECT_TRUE(Input().dialogFieldShown);
    EXPECT_EQ(Input().focus, Client::UI::ChatFocus::DialogField);
    Client::UI::TypeInTheChat(Input(), "Ceibo");
    m_sprites.Begin(Screen640);
    m_text.texts.clear();
    ASSERT_TRUE(Draw().HasValue());
    const std::vector<DrawnQuad> quads = QuadsOf(m_sprites);
    ASSERT_EQ(quads.size(), 176u);
    ExpectQuadAt(quads[3], 0x10f, Left + 0x3a, 56.0f, 110.0f, 18.0f);
    // 0x468320, después de "nombre" (4): la caja del EDIT en el fondo de +0xa0 (0), alta como el EDIT (0xe), el texto
    // (6) y el cursor del segundo dibujo (+0x80 en 1), una columna al final del texto.
    constexpr float Tolerance = 1e-3f;
    EXPECT_EQ(quads[4].slot, Marker);
    EXPECT_EQ(quads[5].slot, NoTexture);
    EXPECT_NEAR(quads[5].first.x, Left + 0x3f, Tolerance);
    EXPECT_NEAR(quads[5].first.y, 480.0f - 0x3c, Tolerance);
    EXPECT_NEAR(quads[5].third.y, 480.0f - 0x3c - 0xe, Tolerance);
    EXPECT_EQ(quads[6].slot, Marker);
    EXPECT_EQ(quads[7].slot, NoTexture);
    EXPECT_NEAR(quads[7].third.x - quads[7].first.x, 1.0f, Tolerance);
    EXPECT_LT(quads[7].first.x, quads[5].third.x);
    EXPECT_EQ(Input().dialogField.draws, 2);
    // La grilla empieza en el 8: su marco y el cuadrado del 0 (el 9 es el 4).
    ExpectQuadAt(quads[8], 0x116, Left + 50.0f, 100.0f, 15.0f, 15.0f);
    ExpectQuadAt(quads[9], SwatchSlot(0), Left + 51.0f, 101.0f, 13.0f, 13.0f);
    ExpectQuadAt(quads[8 + 2 * 9 + 1], SwatchSlot(4), Left + 51.0f + 15.0f, 101.0f + 15.0f, 13.0f, 13.0f);
    ExpectQuadAt(quads[8 + 128 + 1], SwatchSlot(0), Left + 16.0f, 261.0f, 18.0f, 18.0f);
    ExpectQuadAt(quads[8 + 128 + 2 * 15 + 1], SwatchSlot(15), Left + 16.0f + 140.0f, 281.0f, 18.0f, 18.0f);
    ExpectQuadAt(quads[8 + 160 + 1], SwatchSlot(6), Left + 16.0f, 231.0f, 23.0f, 23.0f);
    const auto& texts = m_text.texts;
    ASSERT_EQ(texts.size(), 7u);
    EXPECT_EQ(texts[0].text, "Armar gremio");
    EXPECT_EQ(texts[1].text, "nombre");
    EXPECT_EQ(texts[1].style.font, Client::Render::Text::FontKind::Bold);
    EXPECT_EQ(texts[2].text, "Ceibo");
    EXPECT_EQ(texts[2].x, WindowX + 0x3f);
    EXPECT_EQ(texts[2].y, 0x3c);
    EXPECT_EQ(texts[2].style.font, Client::Render::Text::FontKind::Normal);
    EXPECT_EQ(texts[2].style.colors.text, 0xffffffffu);
    EXPECT_EQ(texts[2].style.colors.background, 0u);
    EXPECT_EQ(texts[3].text, "Elegi un color");
    EXPECT_EQ(texts[3].x, WindowX + 0x32);
    EXPECT_EQ(texts[3].y, 0xe6);
    EXPECT_EQ(texts[3].style.font, Client::Render::Text::FontKind::Normal);
    EXPECT_EQ(texts[4].y, 0xf5);
    EXPECT_EQ(texts[5].text, "Vuelve");
    EXPECT_EQ(texts[6].text, "Sigue");
    // 0x5f1bb0 con el 0: negro con las diagonales grises.
    const auto black = m_store.Find(Client::Render::TextureSlot{static_cast<std::uint16_t>(SwatchSlot(0))});
    ASSERT_TRUE(black.has_value());
    EXPECT_EQ(black->texture.pixels[0], 0x80);
    EXPECT_EQ(black->texture.pixels[4], 0x00);
    EXPECT_EQ(black->texture.pixels[9 * 4], 0x80);
    EXPECT_EQ(black->texture.pixels[7 * 4], 0x80);
    EXPECT_EQ(black->texture.pixels[4 * 4 + 3], 0xff);
}

// 0x654f80: Próximo con un nombre filtrado (0x72), un byte malo (0x7a), corto (0x79) o la marca vacía (0x7e) avisa y se
// queda; el EDIT queda vacío y oculto igual.
TEST_P(SecondGuildWindowTest, TheNameChecksOpenTheirNotices)
{
    OpenIt();
    Guild().createKind = 1;
    Guild().createStep = 1;
    const std::array<std::pair<std::string_view, std::int32_t>, 4> cases{{
        {"Malon", 0x72},
        {"Ca\xc2\xa5on", 0x7a},
        {"Tio", 0x79},
        {"Ombu", 0x7e},
    }};
    for (const auto& [name, notice] : cases)
    {
        SCOPED_TRACE(name);
        ASSERT_TRUE(Draw().HasValue());
        Input().dialogField.text.SetText({});
        Client::UI::TypeInTheChat(Input(), name);
        Runtime().dialogs.current = 0;
        Click(Client::Scenes::SecondGuildNextButton);
        EXPECT_EQ(Runtime().dialogs.current, notice);
        EXPECT_EQ(Guild().createStep, 1);
        EXPECT_TRUE(Input().dialogField.text.Value().empty());
    }
}

// 0x654f80, Regreso: el héroe sin gremio, el buffer del mensaje y su largo en 0 y la pregunta.
TEST_P(SecondGuildWindowTest, BackFromTheNameReturnsToTheQuestion)
{
    OpenIt();
    Guild().createKind = 1;
    Guild().createStep = 1;
    Hero().word1f2 = 1999;
    Client::UI::CopyIntoTheBuffer(Input().messageBuffer, "Molle");
    Input().messageLength = 5;
    ASSERT_TRUE(Draw().HasValue());
    Click(Client::Scenes::SecondGuildBackButton);
    EXPECT_EQ(Guild().createKind, 0);
    EXPECT_EQ(Guild().createStep, 0);
    EXPECT_EQ(Hero().word1f2, 0xffff);
    EXPECT_EQ(Input().messageBuffer[0], '\0');
    EXPECT_EQ(Input().messageLength, 0);
    EXPECT_FALSE(Input().dialogFieldShown);
}

// 0x655c10 (la marca sola, Q-C1288): Próximo con la marca vacía avisa 0x7e; con color, la confirmación; en la
// confirmación, Próximo manda solo los dos 54 00.
TEST_P(SecondGuildWindowTest, TheMarkOnlyPagesSendNoCreate)
{
    OpenIt();
    Hero().guildStatus = 0x80;
    Hero().word1f2 = 4;
    Guild().createKind = 2;
    Guild().createStep = 2;
    ASSERT_TRUE(Draw().HasValue());
    Click(Client::Scenes::SecondGuildNextButton);
    EXPECT_EQ(Runtime().dialogs.current, 0x7e);
    EXPECT_EQ(Guild().createStep, 2);
    Runtime().dialogs.current = 0;
    Guild().table[Creating].mark[0] = 3;
    Click(Client::Scenes::SecondGuildNextButton);
    EXPECT_EQ(Guild().createStep, 3);
    Click(Client::Scenes::SecondGuildBackButton);
    EXPECT_EQ(Guild().createStep, 2);
    Click(Client::Scenes::SecondGuildNextButton);
    Click(Client::Scenes::SecondGuildNextButton);
    ASSERT_EQ(m_sent.size(), 2u);
    EXPECT_EQ(PlainOf(m_sent[0]), BytesOf({0xc1, 0x04, 0x54, 0x00}));
    EXPECT_EQ(Runtime().panels.delegated.secondGuild, 0);
}

// 0x655ac0: el marco del nombre (0x117 arriba y abajo, 0x104 a los costados), "nombre : <nombre>" centrado en 140 en
// (x + 0x19, y + 0x47), el marco de 53 y la marca nueva de 48 en (x + 0x48, y + 0x6b).
TEST_P(SecondGuildWindowTest, TheConfirmationDrawsTheNameAndTheMark)
{
    Guild().createKind = 1;
    Guild().createStep = 3;
    Fill(Guild().table[Creating].guildName, "Ceibo");
    Guild().table[Creating].mark.fill(12);
    ASSERT_TRUE(Draw().HasValue());
    const std::vector<DrawnQuad> quads = QuadsOf(m_sprites);
    ASSERT_EQ(quads.size(), 17u);
    ExpectQuadAt(quads[3], 0x117, Left + 0x19, 65.0f, 140.0f, 2.0f);
    ExpectQuadAt(quads[4], 0x117, Left + 0x19, 85.0f, 141.0f, 2.0f);
    ExpectQuadAt(quads[5], 0x104, Left + 0x19, 65.0f, 2.0f, 20.0f);
    ExpectQuadAt(quads[6], 0x104, Left + 0x19 + 140, 65.0f, 2.0f, 20.0f);
    ExpectQuadAt(quads[8], 0x117, Left + 0x45, 104.0f, 53.0f, 2.0f);
    ExpectQuadAt(quads[12], NewMarkSlot(), Left + 0x48, 107.0f, 48.0f, 48.0f);
    EXPECT_FLOAT_EQ(quads[3].first.u, 0.0390625f);
    EXPECT_FLOAT_EQ(quads[5].first.u, 0.00390625f);
    const auto& texts = m_text.texts;
    ASSERT_EQ(texts.size(), 4u);
    EXPECT_EQ(texts[0].text, "Listo");
    EXPECT_EQ(texts[1].text, "nombre : Ceibo");
    EXPECT_EQ(texts[1].y, 0x47);
    EXPECT_EQ(texts[1].columns, 140);
    EXPECT_EQ(texts[1].style.font, Client::Render::Text::FontKind::Normal);
    EXPECT_EQ(texts[1].style.colors.background, 0u);
}

// Sin la tabla de las marcas, la página del nombre corta; sin Text.bmd, el título.
TEST_P(SecondGuildWindowTest, WithoutTheMarksOrTheTextsTheDrawIsCut)
{
    Guild().createStep = 1;
    EXPECT_FALSE(Draw(false).HasValue());
    Guild().createStep = 0;
    GameHudContext context{.world = *m_world,
                           .random = m_random,
                           .ownRecordDepth = Runtime().ownRecordDepth,
                           .cellXDepth = Hero().cellXAccesses,
                           .cellYDepth = Hero().cellYAccesses,
                           .hud = Runtime().hud,
                           .crywolfSandOverlay = Runtime().crywolf.sandOverlay,
                           .sound = m_sound,
                           .sprites = m_sprites,
                           .text = m_text};
    EXPECT_FALSE(Client::Scenes::DrawTheSecondGuildWindow(context).HasValue());
}

// El alta de punta a punta: el 54 abre la ventana; Crear manda 54 01; el nombre tecleado (el Enter no lo toma el
// chat: 0x4b5115), la marca pintada con dos colores, Próximo a la confirmación y Próximo otra vez: C1 2C 55 00, el
// nombre en 8 bytes y los 64 nibbles de a dos (el par primero arriba), después 54 00 y el 54 00 de 0x6570b0. La
// ventana queda cerrada, el campo del alta apagado, y el 56 con 1 vacía el buffer del mensaje.
TEST_P(SecondGuildWindowTest, CreatesAGuildFromTheQuestionToTheCreatePacket)
{
    Client::Scenes::GuildReceiveContext receive{
        .world = *m_world, .random = m_random, .measure = &m_text, .texts = &m_texts, .now = Now, .send = Sender()};
    ASSERT_TRUE(Client::Scenes::ApplyGuildPacket(Client::Protocol::Guild::MasterQuestion{}, receive).HasValue());
    ASSERT_EQ(Runtime().panels.delegated.secondGuild, 1);
    // El primer cuadro: el paso con los botones sin ubicar y el dibujo que los ubica.
    Frame();
    Click(Client::Scenes::SecondGuildCreateButton);
    ASSERT_EQ(Guild().createStep, 1);
    ASSERT_EQ(m_sent.size(), 1u);
    EXPECT_EQ(Input().focus, Client::UI::ChatFocus::DialogField);

    // Los cuadros de la página del nombre: el EDIT cuenta sus dibujos (0x468320) y el WM_CHAR los vuelve a 0
    // (0x467b9d).
    Frame();
    Frame();
    EXPECT_GE(Input().dialogField.draws, 2);
    Client::Scenes::GameChatContext chat{.world = *m_world, .random = m_random, .texts = &m_texts};
    ASSERT_TRUE(Client::Scenes::TypeInTheGameChat(chat, "Ceibo\r").HasValue());
    EXPECT_EQ(Input().dialogField.draws, 0);
    EXPECT_EQ(Input().dialogField.text.Value(), "Ceibo");
    EXPECT_TRUE(Runtime().ui.enterPressed);
    EXPECT_FALSE(Runtime().ui.chatOpen);

    Choose(0, 4);
    for (std::int32_t column = 0; column < 8; ++column)
        Paint(0, column);
    Choose(1, 4);
    Paint(7, 7);
    ASSERT_EQ(Guild().table[Creating].mark[0], 4);
    ASSERT_EQ(Guild().table[Creating].mark[63], 12);

    Click(Client::Scenes::SecondGuildNextButton);
    ASSERT_EQ(Guild().createStep, 3);
    EXPECT_EQ(Runtime().dialogs.current, 0);
    EXPECT_FALSE(Input().dialogFieldShown);
    EXPECT_EQ(std::string_view(Guild().table[Creating].guildName.data()), "Ceibo");
    EXPECT_EQ(Client::UI::CStringOf(Input().messageBuffer), "Ceibo");
    Click(Client::Scenes::SecondGuildNextButton);

    ASSERT_EQ(m_sent.size(), 4u);
    std::vector<std::byte> create = BytesOf({0xc1, 0x2c, 0x55, 0x00, 'C', 'e', 'i', 'b', 'o', 0, 0, 0});
    create.push_back(std::byte{0x44});
    create.push_back(std::byte{0x44});
    create.push_back(std::byte{0x44});
    create.push_back(std::byte{0x44});
    for (int byte = 4; byte < 31; ++byte)
        create.push_back(std::byte{0x00});
    create.push_back(std::byte{0x0c});
    EXPECT_EQ(PlainOf(m_sent[1]), create);
    EXPECT_EQ(PlainOf(m_sent[2]), BytesOf({0xc1, 0x04, 0x54, 0x00}));
    EXPECT_EQ(PlainOf(m_sent[3]), BytesOf({0xc1, 0x04, 0x54, 0x00}));
    EXPECT_EQ(Runtime().panels.delegated.secondGuild, 0);
    EXPECT_FALSE(Input().dialogFieldOn);
    EXPECT_EQ(Guild().createKind, 0);
    EXPECT_EQ(Guild().createStep, 0);

    ASSERT_TRUE(
        Client::Scenes::ApplyGuildPacket(Client::Protocol::Guild::CreateResult{.result = 1}, receive).HasValue());
    EXPECT_EQ(Input().messageBuffer[0], '\0');
    EXPECT_EQ(m_sent.size(), 4u);
}

INSTANTIATE_TEST_SUITE_P(Maps, SecondGuildWindowTest, ::testing::Values(0, 2));
} // namespace
