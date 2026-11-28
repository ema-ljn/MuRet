#include "Network/Xor32Packet.h"
#include "Guild/SecondGuildWindow.h"

#include "Core/Expected.h"
#include "Core/PixelSize.h"
#include "Crt/CrtRandom.h"
#include "Tables/TextTable.h"
#include "Protocol/Guild/Guild.h"
#include "Protocol/Outgoing.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/FontSet.h"
#include "Render/TextureSlot.h"
#include "Kernel/MouseState.h"
#include "Kernel/ActorState.h"
#include "Chat/GameChat.h"
#include "Ui/GameHudContext.h"
#include "Hud/GamePanels.h"
#include "Guild/GuildMarks.h"
#include "Guild/GuildPanels.h"
#include "Guild/GuildReceive.h"
#include "Guild/GuildState.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Chat/ChatInput.h"
#include "Chat/ChatLog.h"
#include "Ui/Dialogs.h"
#include "Ui/MessageArea.h"
#include "Ui/OldWindowBase.h"
#include "Ui/PanelDialog.h"
#include "Ui/PanelRegistry.h"
#include "Audio/SoundId.h"

#include "support/FakeTextureStore.h"
#include "support/RecordingTextDrawer.h"
#include "support/SceneDataFiles.h"
#include "Ui/GameUiState.h"
#include "Chat/ChatRuntime.h"

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

// Guild window 2 (CL9 milestone 8, phase 3): 0x654900 with its step (0x45aa80(0) and 0x656df0), its draw (0x656e70),
// the four pages (0x655d70, 0x654f80, 0x655c10 and 0x6552b0, and their draws), the mark editor (0x6535c0 and
// 0x653750) and the creation end to end: the 54 that opens it, Create, the name and the mark, and the C1 55. In
// Lorencia (0) and Devias (2, the Guild Master's). The names and the texts are invented.
namespace
{
using Client::Crt::CrtRandom;
using Client::Guild::GuildMarkTextures;
using Client::Guild::GuildState;
using Client::Ui::GameHudContext;
using Client::Kernel::WorldState;
using Client::Render::BlendMode;
using Client::Render::SpriteBatch;
using Client::Render::SpriteVertex;

constexpr std::uint32_t Seed = 11;
constexpr Client::Core::PixelSize Screen640{640, 480};
constexpr int NoTexture = -1;
constexpr int Marker = -2;
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
    const auto plain = Client::Network::Xor32Decode(sent.packet);
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

// The window's texts, invented.
Client::Tables::TextTable WindowTexts()
{
    const std::map<std::size_t, std::string> texts{
        {180, "Gremio"},        {181, "Armas uno?"},  {182, "nombre"},   {183, "Elegi un color"}, {184, "y pinta"},
        {228, "Listo"},         {457, "Operador"},    {458, "Servidor"}, {1002, "Cerrar"},        {1269, "Todavia no"},
        {1303, "Armar gremio"}, {1304, "Otra marca"}, {1305, "Sigue"},   {1306, "Vuelve"}};
    auto table = Client::Tables::LoadTextTable(Client::Tests::TextTableBytes(texts));
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

    Client::Kernel::WorldRuntime& Runtime()
    {
        return m_world->Runtime();
    }

    Client::Kernel::ActorState& Hero()
    {
        return m_world->Hero();
    }

    GuildState& Guild()
    {
        return Runtime().guild;
    }

    Client::Chat::ChatInputState& Input()
    {
        return Runtime().chat.input;
    }

    Client::Guild::GuildSender Sender()
    {
        return [this](Client::Protocol::Outgoing packet) { m_sent.push_back(std::move(packet)); };
    }

    // What the 54 does (0x4e4279): opens panel 0x40000.
    void OpenIt()
    {
        Client::Hud::GamePanels panels(*m_world, m_random);
        panels.LendTheGuild(*m_world, Sender());
        auto opened = panels.Registry().Open(Client::Ui::SecondGuildPanel);
        ASSERT_TRUE(opened.HasValue()) << opened.Error();
        ASSERT_TRUE(opened.Value());
    }

    Client::Core::Expected<void, std::string> Step()
    {
        return Client::Guild::StepTheSecondGuildWindow(Client::Guild::SecondGuildStepContext{.world = *m_world,
                                                                                             .mouseX = m_mouseX,
                                                                                             .mouseY = m_mouseY,
                                                                                             .now = Now,
                                                                                             .screen = Screen640,
                                                                                             .measure = &m_text,
                                                                                             .texts = &m_texts,
                                                                                             .filterName = m_filterName,
                                                                                             .filter = m_filter,
                                                                                             .send = Sender()});
    }

    Client::Core::Expected<void, std::string> Draw(bool withMarks = true)
    {
        Client::Kernel::WorldRuntime& runtime = Runtime();
        Client::Kernel::ActorState& hero = Hero();
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
        return Client::Guild::DrawTheSecondGuildWindow(context);
    }

    // A scene frame: the UI step and the draw, with the batch empty.
    void Frame()
    {
        auto stepped = Step();
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        m_sprites.Begin(Screen640);
        m_text.texts.clear();
        auto drawn = Draw();
        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    }

    // 0x45b200: the edge over the button and, the next frame, the release over it.
    void Click(std::size_t button)
    {
        const Client::Ui::OldWindowBase& base = Guild().secondWindow.buttons[button].base;
        m_mouseX = base.x + base.width / 2;
        m_mouseY = base.y + base.height / 2;
        Client::Kernel::MouseState& mouse = Runtime().mouse;
        mouse.pendingClick = true;
        mouse.leftHeld = true;
        Frame();
        mouse.pendingClick = false;
        mouse.leftHeld = false;
        mouse.releasedClick = true;
        Frame();
        mouse.releasedClick = false;
    }

    // The left button down over the mark grid's square (row, column).
    void Paint(std::int32_t row, std::int32_t column)
    {
        m_mouseX = WindowX + 0x32 + column * 15 + 7;
        m_mouseY = WindowY + 0x64 + row * 15 + 7;
        Runtime().mouse.leftHeld = true;
        Frame();
        Runtime().mouse.leftHeld = false;
    }

    // The click's edge over the palette's color (row, column).
    void Choose(std::int32_t row, std::int32_t column)
    {
        m_mouseX = WindowX + 0xf + column * 20 + 10;
        m_mouseY = WindowY + 0x104 + row * 20 + 10;
        Runtime().mouse.pendingClick = true;
        Frame();
    }

    std::unique_ptr<WorldState> m_world = std::make_unique<WorldState>();
    CrtRandom m_random{Seed};
    Client::Tables::TextTable m_texts = WindowTexts();
    std::vector<std::string> m_filterName{"Malo"};
    std::vector<std::string> m_filter{"feo"};
    std::vector<Client::Protocol::Outgoing> m_sent;
    std::optional<Client::Audio::SoundId> m_sound{};
    std::int32_t m_mouseX = 0;
    std::int32_t m_mouseY = 300;
    SpriteBatch m_sprites;
    Client::Tests::RecordingTextDrawer m_text;
    Client::Tests::FakeTextureStore m_store;
    GuildMarkTextures m_marks{m_store};
};

// 0x654900: the window at (0x1c2, 0), 0xbe × 0x5a, page 0, and the 100 × 20 and 50 × 18 buttons hanging from it with
// commands 1, 2, 5 and 4.
TEST_P(SecondGuildWindowTest, TheWindowStartsOnTheQuestion)
{
    const Client::Guild::SecondGuildWindowState& window = Guild().secondWindow;
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

// 0x656e70 and 0x6563e0 without a guild: the opaque background, the title "Gremio" in bold centered in 120, the
// question at (x + 0x19, y + 0x3c), Create shown and the mark off at x + 0x2d, 0x1e and 0x36 lower, and the close
// button.
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
    const Client::Guild::SecondGuildWindowState& window = Guild().secondWindow;
    EXPECT_EQ(window.buttons[0].base.x, WindowX + 0x2d);
    EXPECT_EQ(window.buttons[0].base.y, 0x3c + 0x1e);
    EXPECT_EQ(window.buttons[0].base.state, Client::Ui::ShownWindow);
    EXPECT_EQ(window.buttons[1].base.y, 0x3c + 0x1e + 0x18);
    EXPECT_EQ(window.buttons[1].base.state, Client::Ui::DisabledWindow);
    EXPECT_EQ(Draws(m_random), 0);
}

// With a guild (+0x368 other than 0xff), its name and the score centered in the 140 box (0x5542f0 with 2), Create off
// and the mark shown; with the mouse over the close button, its help.
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
    EXPECT_EQ(Guild().secondWindow.buttons[0].base.state, Client::Ui::DisabledWindow);
    EXPECT_EQ(Guild().secondWindow.buttons[1].base.state, Client::Ui::ShownWindow);
}

// 0x655d70: Create builds a guild (both at 1, the creation field on) and sends C1 04 54 01.
TEST_P(SecondGuildWindowTest, CreateGoesToTheNamePageAndAnswersYes)
{
    OpenIt();
    ASSERT_TRUE(Draw().HasValue());
    Click(Client::Guild::SecondGuildCreateButton);
    EXPECT_EQ(Guild().createKind, 1);
    EXPECT_EQ(Guild().createStep, 1);
    EXPECT_TRUE(Input().dialogFieldOn);
    ASSERT_EQ(m_sent.size(), 1u);
    EXPECT_EQ(PlainOf(m_sent[0]), BytesOf({0xc1, 0x04, 0x54, 0x01}));
    // 0x45b200: the button's sound, 0x19.
    EXPECT_EQ(Runtime().lastSoundId, Client::Audio::SoundId{0x19});
}

// 0x6560b3: the mark opens the panels' dialog with 1269 and Accept, and stores it in +0x2a0.
TEST_P(SecondGuildWindowTest, TheMarkButtonOpensTheNotEnabledDialog)
{
    Hero().guildStatus = 0x80;
    Hero().word1f2 = 4;
    OpenIt();
    ASSERT_TRUE(Draw().HasValue());
    Click(Client::Guild::SecondGuildMarkButton);
    const Client::Ui::PanelDialogState& dialog = Runtime().panels.delegated.panelDialog;
    ASSERT_NE(Client::Ui::PanelDialogHandle(dialog), 0u);
    EXPECT_EQ(Guild().secondWindow.dialog2a0, Client::Ui::PanelDialogHandle(dialog));
    EXPECT_EQ(Guild().createStep, 0);
    EXPECT_TRUE(m_sent.empty());
}

// 0x6560b3 with the dialog open (a second click: 0x657930 gives the dialog's step and then the window's): 0x6600d0
// gives the notice of 0x6d69a8 in the chat (0x555190 with type 1) and 0, which goes to +0x2a0; the open one stays.
// Without stopping.
TEST_P(SecondGuildWindowTest, ASecondClickOnTheMarkGetsTheBusyNotice)
{
    Hero().guildStatus = 0x80;
    Hero().word1f2 = 4;
    OpenIt();
    ASSERT_TRUE(Draw().HasValue());
    Click(Client::Guild::SecondGuildMarkButton);
    const Client::Ui::PanelDialogState& dialog = Runtime().panels.delegated.panelDialog;
    ASSERT_EQ(Guild().secondWindow.dialog2a0, Now);
    ASSERT_TRUE(Runtime().chat.log.entries.empty());

    Click(Client::Guild::SecondGuildMarkButton);
    constexpr std::array<std::uint8_t, 26> Binary{0xc0, 0xcc, 0xb9, 0xcc, 0x20, 0xc6, 0xcb, 0xbe, 0xf7,
                                                  0xc3, 0xa2, 0xc0, 0xcc, 0x20, 0xb6, 0xb0, 0x20, 0xc0,
                                                  0xd6, 0xbd, 0xc0, 0xb4, 0xcf, 0xb4, 0xd9, 0x2e};
    std::string notice;
    for (const std::uint8_t byte : Binary)
        notice.push_back(static_cast<char>(byte));
    ASSERT_EQ(Runtime().chat.log.entries.size(), 1U);
    EXPECT_EQ(Runtime().chat.log.entries.front().text, notice);
    EXPECT_EQ(Runtime().chat.log.entries.front().type, Client::Ui::SystemMessage);
    EXPECT_EQ(Guild().secondWindow.dialog2a0, 0U);
    EXPECT_EQ(dialog.opened, Now);
    EXPECT_EQ(dialog.lines[0], "Todavia no");
    EXPECT_TRUE(m_sent.empty());
}

// 0x6560c3..0x656124: the edge over the close button (0x18 × 0x18 at x + 0x19, y + 0x18b) turns it off, leaves
// 0x77f3b10 at 0 and 0x6c8dcc at 6, sends 54 00, plays 0x19 and closes with 0x6570b0, which sends another 54 00
// (Q-C1289).
TEST_P(SecondGuildWindowTest, TheCloseButtonAnswersNoTwiceAndClosesTheWindow)
{
    OpenIt();
    Runtime().mouse.updateTime = 3;
    m_mouseX = WindowX + 0x19 + 5;
    m_mouseY = 0x18b + 5;
    Runtime().mouse.pendingClick = true;
    ASSERT_TRUE(Step().HasValue());
    EXPECT_FALSE(Runtime().mouse.pendingClick);
    EXPECT_EQ(Runtime().mouse.updateTime, 0);
    EXPECT_EQ(Runtime().mouse.updateTimeMax, 6);
    EXPECT_EQ(Runtime().panels.delegated.secondGuild, 0);
    ASSERT_EQ(m_sent.size(), 2u);
    EXPECT_EQ(PlainOf(m_sent[0]), BytesOf({0xc1, 0x04, 0x54, 0x00}));
    EXPECT_EQ(PlainOf(m_sent[1]), BytesOf({0xc1, 0x04, 0x54, 0x00}));
    // 0x41ed10(0x19) and then 0x6570b0's 0x1c.
    EXPECT_EQ(Runtime().lastSoundId, Client::Audio::SoundId{0x1c});
}

// 0x45aa80(0): with the mouse over the window and another one that grabbed the mouse, it does not take its step; with
// nobody, it takes it and, with the mouse within its 0x1b1 height, turns 0x775857c on.
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
    EXPECT_EQ(Runtime().ui.hovered, Client::Guild::SecondGuildWindowId);
    EXPECT_EQ(Runtime().ui.mouseOnUi, 1);
}

// 0x6535c0: the hero's angle at 112.5; the left button paints with the chosen color, the right one erases, and the
// edge over the palette chooses (row · 8 + column) and plays 0x19.
TEST_P(SecondGuildWindowTest, TheEditorPaintsErasesAndChoosesColors)
{
    OpenIt();
    Guild().createKind = 1;
    Guild().createStep = 1;
    Choose(1, 3);
    EXPECT_EQ(Guild().markColor, 11);
    EXPECT_FALSE(Runtime().mouse.pendingClick);
    EXPECT_EQ(Runtime().lastSoundId, Client::Audio::SoundId{0x19});
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

// 0x655130 and 0x653750: the name box, "nombre" in bold, what was typed at (x + 0x3f, y + 0x3c), the grid of 13 at
// (x + 51, y + 101) every 15 with its 0x116 frame, the palette of 18 at (x + 16, y + 261) every 20, the chosen color of
// 23 at (x + 16, y + 231), the two rows and Back and Next.
TEST_P(SecondGuildWindowTest, TheNamePageDrawsTheFieldAndTheEditor)
{
    OpenIt();
    Guild().createKind = 1;
    Guild().createStep = 1;
    Guild().markColor = 6;
    Guild().table[Creating].mark[9] = 4;
    Runtime().ui.focusedList = 3;
    ASSERT_TRUE(Draw().HasValue());
    EXPECT_EQ(Input().dialogEdit.base.state, Client::Ui::ShownWindow);
    EXPECT_EQ(Input().focus, Client::Chat::ChatFocus::Dialog);
    // 0x655232: the EDIT's id in 0x5762ec0, so the arrows no longer move another list.
    EXPECT_EQ(Runtime().ui.focusedList, Client::Chat::DialogFieldId);
    Client::Chat::TypeInTheChat(Input(), "Ceibo");
    m_sprites.Begin(Screen640);
    m_text.texts.clear();
    ASSERT_TRUE(Draw().HasValue());
    const std::vector<DrawnQuad> quads = QuadsOf(m_sprites);
    ASSERT_EQ(quads.size(), 175u);
    ExpectQuadAt(quads[3], 0x10f, Left + 0x3a, 56.0f, 110.0f, 18.0f);
    // 0x468320, after "nombre" (4): the EDIT's box on +0xa0's background (0), as high as the EDIT (0xe), and the text
    // (6). Each WM_CHAR of "Ceibo" sets +0x80 to 0 (the subclass, 0x467b9d), and the second draw's cursor (+0x80 at 0,
    // on) goes at column 3 + cx + 1 of a bitmap of ftol(3 + cx): at the end of the text it is clipped (the XP edit of
    // Ui::OldDraw::DrawTheOldEdit).
    constexpr float Tolerance = 1e-3f;
    EXPECT_EQ(quads[4].slot, Marker);
    EXPECT_EQ(quads[5].slot, NoTexture);
    EXPECT_NEAR(quads[5].first.x, Left + 0x3f, Tolerance);
    EXPECT_NEAR(quads[5].first.y, 480.0f - 0x3c, Tolerance);
    EXPECT_NEAR(quads[5].third.y, 480.0f - 0x3c - 0xe, Tolerance);
    EXPECT_EQ(quads[6].slot, Marker);
    EXPECT_EQ(Input().dialogEdit.draws, 1);
    // The grid starts at 7: its frame and color 0's square (cell 9 is color 4).
    ExpectQuadAt(quads[7], 0x116, Left + 50.0f, 100.0f, 15.0f, 15.0f);
    ExpectQuadAt(quads[8], SwatchSlot(0), Left + 51.0f, 101.0f, 13.0f, 13.0f);
    ExpectQuadAt(quads[7 + 2 * 9 + 1], SwatchSlot(4), Left + 51.0f + 15.0f, 101.0f + 15.0f, 13.0f, 13.0f);
    ExpectQuadAt(quads[7 + 128 + 1], SwatchSlot(0), Left + 16.0f, 261.0f, 18.0f, 18.0f);
    ExpectQuadAt(quads[7 + 128 + 2 * 15 + 1], SwatchSlot(15), Left + 16.0f + 140.0f, 281.0f, 18.0f, 18.0f);
    ExpectQuadAt(quads[7 + 160 + 1], SwatchSlot(6), Left + 16.0f, 231.0f, 23.0f, 23.0f);
    const auto& texts = m_text.texts;
    ASSERT_EQ(texts.size(), 7u);
    EXPECT_EQ(texts[0].text, "Armar gremio");
    EXPECT_EQ(texts[1].text, "nombre");
    EXPECT_EQ(texts[1].style.font, Client::Render::Text::FontKind::Bold);
    EXPECT_EQ(texts[2].text, "Ceibo");
    EXPECT_EQ(texts[2].x, WindowX + 0x3f + 3);
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
    // 0x5f1bb0 with 0: black with the gray diagonals.
    const auto black = m_store.Find(Client::Render::TextureSlot{static_cast<std::uint16_t>(SwatchSlot(0))});
    ASSERT_TRUE(black.has_value());
    EXPECT_EQ(black->texture.pixels[0], 0x80);
    EXPECT_EQ(black->texture.pixels[4], 0x00);
    EXPECT_EQ(black->texture.pixels[9 * 4], 0x80);
    EXPECT_EQ(black->texture.pixels[7 * 4], 0x80);
    EXPECT_EQ(black->texture.pixels[4 * 4 + 3], 0xff);
}

// 0x654f80: Next with a filtered name (0x72), a bad byte (0x7a), a short one (0x79) or the empty mark (0x7e) gives
// the notice and stays; the EDIT is emptied and hidden anyway.
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
        Input().dialogEdit.text.SetText({});
        Client::Chat::TypeInTheChat(Input(), name);
        Runtime().dialogs.current = 0;
        Click(Client::Guild::SecondGuildNextButton);
        EXPECT_EQ(Runtime().dialogs.current, notice);
        EXPECT_EQ(Guild().createStep, 1);
        EXPECT_TRUE(Input().dialogEdit.text.Value().empty());
    }
}

// 0x654f80, Back: the hero without a guild, the message buffer and its length at 0 and the question.
TEST_P(SecondGuildWindowTest, BackFromTheNameReturnsToTheQuestion)
{
    OpenIt();
    Guild().createKind = 1;
    Guild().createStep = 1;
    Hero().word1f2 = 1999;
    Client::Chat::CopyIntoTheBuffer(Input().messageBuffer, "Molle");
    Input().messageLength = 5;
    ASSERT_TRUE(Draw().HasValue());
    Click(Client::Guild::SecondGuildBackButton);
    EXPECT_EQ(Guild().createKind, 0);
    EXPECT_EQ(Guild().createStep, 0);
    EXPECT_EQ(Hero().word1f2, 0xffff);
    EXPECT_EQ(Input().messageBuffer[0], '\0');
    EXPECT_EQ(Input().messageLength, 0);
    EXPECT_EQ(Input().dialogEdit.base.state, Client::Ui::HiddenWindow);
}

// 0x655c10 (the mark alone, Q-C1288): Next with the empty mark gives notice 0x7e; with a color, the confirmation; on
// the confirmation, Next sends only the two 54 00.
TEST_P(SecondGuildWindowTest, TheMarkOnlyPagesSendNoCreate)
{
    OpenIt();
    Hero().guildStatus = 0x80;
    Hero().word1f2 = 4;
    Guild().createKind = 2;
    Guild().createStep = 2;
    ASSERT_TRUE(Draw().HasValue());
    Click(Client::Guild::SecondGuildNextButton);
    EXPECT_EQ(Runtime().dialogs.current, 0x7e);
    EXPECT_EQ(Guild().createStep, 2);
    Runtime().dialogs.current = 0;
    Guild().table[Creating].mark[0] = 3;
    Click(Client::Guild::SecondGuildNextButton);
    EXPECT_EQ(Guild().createStep, 3);
    Click(Client::Guild::SecondGuildBackButton);
    EXPECT_EQ(Guild().createStep, 2);
    Click(Client::Guild::SecondGuildNextButton);
    Click(Client::Guild::SecondGuildNextButton);
    ASSERT_EQ(m_sent.size(), 2u);
    EXPECT_EQ(PlainOf(m_sent[0]), BytesOf({0xc1, 0x04, 0x54, 0x00}));
    EXPECT_EQ(Runtime().panels.delegated.secondGuild, 0);
}

// 0x655ac0: the name frame (0x117 above and below, 0x104 on the sides), "nombre : <name>" centered in 140 at
// (x + 0x19, y + 0x47), the 53 frame and the new 48 mark at (x + 0x48, y + 0x6b).
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

// Without the marks table, the name page stops; without Text.bmd, the title.
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
    EXPECT_FALSE(Client::Guild::DrawTheSecondGuildWindow(context).HasValue());
}

// The creation end to end: the 54 opens the window; Create sends 54 01; the typed name (the chat does not take the
// Enter: 0x4b5115), the mark painted with two colors, Next to the confirmation and Next again: C1 2C 55 00, the name
// in 8 bytes and the 64 nibbles two by two (the first of the pair high), then 54 00 and 0x6570b0's 54 00. The window
// is left closed, the creation field off, and the 56 with 1 empties the message buffer.
TEST_P(SecondGuildWindowTest, CreatesAGuildFromTheQuestionToTheCreatePacket)
{
    Client::Guild::GuildReceiveContext receive{
        .world = *m_world, .random = m_random, .measure = &m_text, .texts = &m_texts, .now = Now, .send = Sender()};
    ASSERT_TRUE(Client::Guild::ApplyGuildPacket(Client::Protocol::Guild::MasterQuestion{}, receive).HasValue());
    ASSERT_EQ(Runtime().panels.delegated.secondGuild, 1);
    // The first frame: the step with the buttons not yet placed and the draw that places them.
    Frame();
    Click(Client::Guild::SecondGuildCreateButton);
    ASSERT_EQ(Guild().createStep, 1);
    ASSERT_EQ(m_sent.size(), 1u);
    EXPECT_EQ(Input().focus, Client::Chat::ChatFocus::Dialog);

    // The name page's frames: the EDIT counts its draws (0x468320) and the WM_CHAR resets them to 0 (0x467b9d).
    Frame();
    Frame();
    EXPECT_GE(Input().dialogEdit.draws, 2);
    Client::Chat::GameChatContext chat{.world = *m_world, .random = m_random, .texts = &m_texts};
    ASSERT_TRUE(Client::Chat::TypeInTheGameChat(chat, "Ceibo\r").HasValue());
    EXPECT_EQ(Input().dialogEdit.draws, 0);
    EXPECT_EQ(Input().dialogEdit.text.Value(), "Ceibo");
    EXPECT_TRUE(Runtime().ui.enterPressed);
    EXPECT_FALSE(Runtime().ui.chatOpen);

    Choose(0, 4);
    for (std::int32_t column = 0; column < 8; ++column)
        Paint(0, column);
    Choose(1, 4);
    Paint(7, 7);
    ASSERT_EQ(Guild().table[Creating].mark[0], 4);
    ASSERT_EQ(Guild().table[Creating].mark[63], 12);

    Click(Client::Guild::SecondGuildNextButton);
    ASSERT_EQ(Guild().createStep, 3);
    EXPECT_EQ(Runtime().dialogs.current, 0);
    EXPECT_EQ(Input().dialogEdit.base.state, Client::Ui::HiddenWindow);
    EXPECT_EQ(std::string_view(Guild().table[Creating].guildName.data()), "Ceibo");
    EXPECT_EQ(Client::Chat::CStringOf(Input().messageBuffer), "Ceibo");
    Click(Client::Guild::SecondGuildNextButton);

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
        Client::Guild::ApplyGuildPacket(Client::Protocol::Guild::CreateResult{.result = 1}, receive).HasValue());
    EXPECT_EQ(Input().messageBuffer[0], '\0');
    EXPECT_EQ(m_sent.size(), 4u);
}

INSTANTIATE_TEST_SUITE_P(Maps, SecondGuildWindowTest, ::testing::Values(0, 2));
} // namespace
