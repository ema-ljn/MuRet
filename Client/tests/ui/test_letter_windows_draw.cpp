#include "UI/LetterWindowsDraw.h"

#include "App/WindowState.h"
#include "Core/Expected.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Render/Text/TextStyle.h"
#include "Render/TextureSlot.h"
#include "UI/ChatInput.h"
#include "UI/FriendModel.h"
#include "UI/LetterWindows.h"
#include "UI/OldControls.h"
#include "UI/OldWindowBase.h"
#include "UI/OldWindowDraw.h"
#include "UI/OldWindows.h"
#include "support/RecordingTextDrawer.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

// El dibujo del chat con un amigo y de las ventanas de una carta (CL9 hito 10; 06 § HUD y ventanas, "Las cartas y el
// chat con un amigo"): 0x493fc0 con sus renglones (0x45f9c0, 0x460550), los miembros y los de invitar (0x47a810 con
// 0x461cd0) y sus rayas (0x491ba0). El chat queda en (100, 80), de 250 × 170: el área de adentro en (105, 101) de
// 240 × 144, los renglones con el borde de abajo en 229 y 128 de alto, el EDIT en (107, 233) y el botón de invitar en
// (291, 231). Los nombres y los textos son inventados.
namespace
{
using Client::App::PixelSize;
using Client::Render::BlendMode;
using Client::Render::Color;
using Client::Render::DrawCommand;
using Client::Render::SpriteBatch;
using Client::Render::SpriteVertex;
using Client::Render::TextureSlot;
using Client::Render::Text::FontKind;
using Client::Render::Text::ITextMeasure;
using Client::Render::Text::TextColors;
using Client::Render::Text::TextExtent;
using Client::Tests::RecordingTextDrawer;
using Client::Tests::TextMarkerSlot;
using Client::UI::ChatFocus;
using Client::UI::FriendSend;
using Client::UI::OldChatPart;
using Client::UI::OldPortraitPart;
using Client::UI::OldWindow;
using Client::UI::OldWindowDrawContext;
using Client::UI::OldWindowDrawView;
using Client::UI::OldWindowGlobals;
using Client::UI::OldWindowManager;
using Client::UI::OldWindowMeasure;
using Client::UI::OldWindowStepView;
using Client::UI::OldWindowTexts;
using Client::UI::PortraitCallKind;

constexpr PixelSize Screen640{640, 480};
constexpr TextureSlot ScrollbarSlot{0x649};
constexpr TextureSlot ButtonSlot{Client::UI::WindowButtonSlot};
constexpr std::uint32_t Dim = 0xffc8dce6u;
constexpr std::string_view Hero = "Heroico";
constexpr std::int32_t ChatX = 100;
constexpr std::int32_t ChatY = 80;

class LetterMeasure final : public ITextMeasure
{
public:
    [[nodiscard]] TextExtent Measure(FontKind /*font*/, std::string_view text) const override
    {
        return {static_cast<int>(text.size()) * RecordingTextDrawer::LetterWidth, RecordingTextDrawer::Height};
    }
};

// Cada texto es "t<índice>".
std::string TextOf(std::size_t index)
{
    return "t" + std::to_string(index);
}

// Un quad en 640 × 480 con la y hacia abajo.
struct Box
{
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;
};

void ExpectBox(const Box& box, float left, float top, float right, float bottom)
{
    EXPECT_NEAR(box.left, left, 1e-4f);
    EXPECT_NEAR(box.top, top, 1e-4f);
    EXPECT_NEAR(box.right, right, 1e-4f);
    EXPECT_NEAR(box.bottom, bottom, 1e-4f);
}

Color PaletteOf(std::int32_t index)
{
    return *Client::UI::OldWindowPalette(index, 1.0f);
}

struct ExpectedCommand
{
    std::optional<TextureSlot> texture;
    BlendMode blend;
    std::size_t quads;
};

// Un miembro de la sala: un agregado y no un std::pair (C4244 con un literal int en un std::uint8_t).
struct NewMember
{
    std::string name;
    std::uint8_t index = 0;
};

// Un renglón del chat: quién (0xff, nadie), el texto y el tipo.
struct NewLine
{
    std::uint8_t speaker = 0xff;
    std::string text;
    std::int32_t kind = 0;
};

// Un texto: lo que dice, dónde y sus colores.
using ExpectedText = std::tuple<std::string, int, int, TextColors>;

class LetterWindowsDrawTest : public ::testing::Test
{
protected:
    LetterWindowsDrawTest() : m_text(m_sprites)
    {
        m_sprites.Begin(Screen640);
    }

    OldWindowGlobals Globals()
    {
        return OldWindowGlobals{.leftHeld = m_leftHeld,
                                .leftClick = m_leftClick,
                                .releasedClick = m_released,
                                .mouseOnUi = m_mouseOnUi,
                                .hovered = m_hovered,
                                .capture = m_capture,
                                .pressedButton = m_pressedButton,
                                .keyboardFocus = m_focus,
                                .sound = m_sound,
                                .focusedList = m_focusedList,
                                .wheel = m_wheel,
                                .rightClick = m_rightClick,
                                .doubleClick = m_doubleClick,
                                .mouseUpdateTime = m_mouseUpdateTime,
                                .mouseUpdateTimeMax = m_mouseUpdateTimeMax};
    }

    // El paso con un retrato de prueba que arma el personaje sin sacar rand().
    OldWindowStepView StepView()
    {
        OldWindowStepView view{.mouseX = 639,
                               .mouseY = 479,
                               .keyEdge = [](std::uint8_t /*key*/) { return false; },
                               .keyDown = {},
                               .chatOpen = false,
                               .texts = &m_texts,
                               .measure = &m_measure,
                               .send = [](const FriendSend& /*send*/) {},
                               .whisperTo = {},
                               .openFriends = {}};
        view.portrait = [](OldPortraitPart& portrait, PortraitCallKind kind,
                           std::int32_t /*arg*/) -> Client::Core::Expected<void, std::string>
        {
            if (kind == PortraitCallKind::Build)
                portrait.built = 1;
            return {};
        };
        view.heroName = std::string(Hero);
        return view;
    }

    // 0x48ee60 con el 1 en (100, 80): el héroe y los miembros (0x493c90) y los renglones (0x493f80).
    OldWindow& OpenAChat(const std::vector<NewMember>& members, const std::vector<NewLine>& lines)
    {
        const OldWindowGlobals globals = Globals();
        const OldWindowStepView view = StepView();
        const auto opened = Client::UI::CreateTheOldWindow(
            m_manager,
            Client::UI::OldWindowRequest{
                .kind = Client::UI::ChatCase, .x = ChatX, .y = ChatY, .data = "t994", .aux = 0, .flags = 0},
            m_texts, m_measure, &globals, &view);
        EXPECT_TRUE(opened.HasValue()) << opened.Error();
        m_chat = opened.HasValue() ? opened.Value() : 0;
        OldWindow& chat = Chat();
        static_cast<void>(Client::UI::AddAChatMember(m_manager, chat, Hero, 0, 0, m_texts, Hero));
        for (const NewMember& member : members)
            static_cast<void>(Client::UI::AddAChatMember(m_manager, chat, member.name, member.index, 0, m_texts, Hero));
        for (const NewLine& line : lines)
            Client::UI::AddAChatLine(chat, line.speaker, line.text, line.kind, 0);
        return chat;
    }

    OldWindow& Chat()
    {
        return m_manager.windows.at(m_chat);
    }

    // El paso de las ventanas, con la cola del administrador.
    void Step()
    {
        const auto stepped = Client::UI::StepTheOldWindows(m_manager, Globals(), StepView());
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    }

    OldWindowDrawContext Context()
    {
        return OldWindowDrawContext{.sprites = m_sprites, .text = m_text, .dcFont = m_dcFont, .textColors = m_colors};
    }

    // 0x490390 con la ventana de amigos abierta.
    Client::Core::Expected<void, std::string> Draw(std::int32_t x = 0, std::int32_t y = 0)
    {
        const OldWindowDrawView view{.mouseX = x,
                                     .mouseY = y,
                                     .leftClick = m_leftClick,
                                     .friendsOpen = true,
                                     .texts = &m_texts,
                                     .focusedList = m_focusedList,
                                     .focusedEdit = m_focusedEdit,
                                     .chatPlacement = m_placement};
        return Client::UI::DrawTheOldWindows(m_manager, Context(), view);
    }

    const SpriteVertex& VertexOf(std::size_t command, std::size_t quad = 0, std::size_t corner = 0) const
    {
        return m_sprites.Vertices()[m_sprites.Commands()[command].firstVertex + quad * 6 + corner];
    }

    Box QuadOf(std::size_t command, std::size_t quad = 0) const
    {
        const SpriteVertex& first = VertexOf(command, quad, 0);
        const SpriteVertex& last = VertexOf(command, quad, 2);
        return Box{.left = first.x, .top = 480.0f - first.y, .right = last.x, .bottom = 480.0f - last.y};
    }

    void ExpectCommands(const std::vector<ExpectedCommand>& expected) const
    {
        const std::span<const DrawCommand> commands = m_sprites.Commands();
        ASSERT_GE(commands.size(), expected.size());
        for (std::size_t i = 0; i < expected.size(); ++i)
        {
            SCOPED_TRACE(i);
            EXPECT_EQ(commands[i].texture, expected[i].texture);
            EXPECT_EQ(commands[i].blend, expected[i].blend);
            EXPECT_EQ(commands[i].vertexCount, expected[i].quads * 6);
        }
    }

    void ExpectTexts(std::size_t from, const std::vector<ExpectedText>& expected) const
    {
        ASSERT_GE(m_text.texts.size(), from + expected.size());
        for (std::size_t i = 0; i < expected.size(); ++i)
        {
            SCOPED_TRACE(from + i);
            const auto& text = m_text.texts[from + i];
            EXPECT_EQ(text.text, std::get<0>(expected[i]));
            EXPECT_EQ(text.x, std::get<1>(expected[i]));
            EXPECT_EQ(text.y, std::get<2>(expected[i]));
            EXPECT_EQ(text.style.colors, std::get<3>(expected[i]));
            EXPECT_EQ(text.style.font, FontKind::Normal);
        }
    }

    LetterMeasure m_letters;
    const OldWindowTexts m_texts{.notice = "Aviso", .question = "Pregunta", .yes = "Si", .no = "No", .lookup = TextOf};
    const OldWindowMeasure m_measure{.measure = m_letters, .dcFont = FontKind::Normal, .screen = Screen640};
    OldWindowManager m_manager;
    SpriteBatch m_sprites;
    RecordingTextDrawer m_text;
    std::int32_t m_chat = 0;
    bool m_leftHeld = false;
    bool m_leftClick = false;
    bool m_released = false;
    std::uint8_t m_mouseOnUi = 0;
    std::int32_t m_hovered = 0;
    std::int32_t m_capture = 0;
    std::int32_t m_pressedButton = 0;
    ChatFocus m_focus = ChatFocus::Game;
    std::optional<std::int32_t> m_sound;
    std::int32_t m_focusedList = 0;
    std::int32_t m_wheel = 0;
    bool m_rightClick = false;
    bool m_doubleClick = false;
    std::int32_t m_mouseUpdateTime = 0;
    std::int32_t m_mouseUpdateTimeMax = 6;
    std::int32_t m_focusedEdit = 0;
    std::int32_t m_placement = 0;
    FontKind m_dcFont = FontKind::Bold;
    TextColors m_colors{};
};

// Con dos miembros (el héroe y un amigo) y sin la lista de invitar: la sombra del marco, la barra de los renglones,
// los renglones de abajo hacia arriba (el último abajo: el del sistema sin nombre y el del amigo con el suyo), la raya
// del 2 y el negro de abajo, el botón de invitar y el EDIT sin mezcla (0x45b2e0 la saca), y el marco.
TEST_F(LetterWindowsDrawTest, TheChatDrawsItsLinesItsBottomItsButtonAndItsEdit)
{
    OldWindow& chat = OpenAChat({{"Tordo", 1}}, {{1, "hola che", 3}, {0xff, "se sumo alguien", 1}});
    const OldChatPart& part = chat.chat;
    ASSERT_EQ(part.lines.base.x, 105);
    ASSERT_EQ(part.lines.base.y, 229);
    ASSERT_EQ(part.lines.base.width, 240);
    ASSERT_EQ(part.lines.base.height, 128);
    ASSERT_EQ(part.edit.base.x, 107);
    ASSERT_EQ(part.edit.base.y, 233);
    ASSERT_EQ(part.inviteButton.base.x, 291);
    ASSERT_EQ(part.inviteButton.base.y, 231);
    m_focusedEdit = part.edit.base.id;

    const auto drawn = Draw();
    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();

    ExpectCommands({
        {std::nullopt, BlendMode::Alpha, 1},   // la sombra del marco
        {ScrollbarSlot, BlendMode::Alpha, 2},  // las flechas de los renglones
        {std::nullopt, BlendMode::Alpha, 2},   // los filos de la pista
        {ScrollbarSlot, BlendMode::Alpha, 3},  // el cursor
        {TextMarkerSlot, BlendMode::Alpha, 3}, // "se sumo alguien", "Tordo: " y "hola che"
        {std::nullopt, BlendMode::Alpha, 2},   // la raya y el negro de abajo
        {ButtonSlot, BlendMode::Alpha, 1},
        {TextMarkerSlot, BlendMode::Alpha, 1}, // "t993"
        {std::nullopt, BlendMode::Opaque, 2},  // el bitmap del EDIT y el cursor, sin mezcla
        {TextureSlot{Client::UI::WindowTitleSlot}, BlendMode::Opaque, 1},
    });
    // 0x45fac0: las flechas en (x + w − 12, y − h − 1) y (x + w − 12, y − 12).
    ExpectBox(QuadOf(1, 0), 333.0f, 100.0f, 346.0f, 113.0f);
    ExpectBox(QuadOf(1, 1), 333.0f, 217.0f, 346.0f, 230.0f);
    // 0x4941ae..0x494281: la raya del 2 en (105, 144 + 101 − 16) de 240 × 1 y el 7 en (105, 230) de 240 × 15.
    ExpectBox(QuadOf(5, 0), 105.0f, 229.0f, 345.0f, 230.0f);
    EXPECT_EQ(VertexOf(5, 0).color, PaletteOf(2));
    ExpectBox(QuadOf(5, 1), 105.0f, 230.0f, 345.0f, 245.0f);
    EXPECT_EQ(VertexOf(5, 1).color, PaletteOf(7));
    ExpectBox(QuadOf(6), 291.0f, 231.0f, 344.0f, 244.0f);
    // El EDIT, en su lugar.
    EXPECT_FLOAT_EQ(QuadOf(8).left, 107.0f);
    EXPECT_FLOAT_EQ(QuadOf(8).top, 233.0f);

    // 0x460550: el renglón 0 en y − 0x10 y el 1, 13 más arriba; el nombre en x + 8 con ": " y el texto después de su
    // ancho (7 letras de 6); el sistema (1) en 0xff46a5d2, el nombre de un miembro (3) en 0xffefdccd y su texto en
    // 0xffc8dce6, todos sin fondo.
    ExpectTexts(0, {
                       {"se sumo alguien", 113, 213, TextColors{.text = 0xff46a5d2u, .background = 0}},
                       {"Tordo: ", 113, 200, TextColors{.text = 0xffefdccdu, .background = 0}},
                       {"hola che", 155, 200, TextColors{.text = Dim, .background = 0}},
                   });
    EXPECT_EQ(m_dcFont, FontKind::Normal);
}

// Con la lista de invitar (+0xb0 en 2, el 10 con 1 de 0x4944c0): los renglones 0xa0 más angostos, los miembros y los
// de invitar con sus barras y la franja del elegido, la raya de 0x491ba0 a 0xa0 del borde derecho (con la mezcla
// normal) y la de 0x50, y el botón de mandar la invitación después del EDIT.
TEST_F(LetterWindowsDrawTest, TheInvitesShowTheMembersTheirSeparatorsAndTheSendButton)
{
    Client::UI::AddAFriend(m_manager.friendModel, "Zorzal", 0, 3);
    Client::UI::AddAFriend(m_manager.friendModel, "Calandria", 0, 1);
    OpenAChat({{"Tordo", 1}}, {});
    Client::UI::PostToTheWindow(m_manager, m_chat, Client::UI::CommandMessage, 1, 0);
    Step();
    const OldChatPart& part = Chat().chat;
    ASSERT_EQ(part.mode, 2);
    ASSERT_EQ(part.invitable.size(), 2u);
    // 0x4944c0 la hace 0x50 más ancha: el área de adentro, de 320.
    ASSERT_EQ(Chat().base.width, 0x14a);
    ASSERT_EQ(part.lines.base.width, 160);
    ASSERT_EQ(part.users.base.x, 270);
    ASSERT_EQ(part.invite.base.x, 350);

    ASSERT_TRUE(Draw().HasValue());

    ExpectCommands({
        {std::nullopt, BlendMode::Alpha, 1},
        {ScrollbarSlot, BlendMode::Alpha, 2}, // los renglones, sin ninguno
        {std::nullopt, BlendMode::Alpha, 2},
        {ScrollbarSlot, BlendMode::Alpha, 5}, // su cursor y las flechas de los miembros
        {std::nullopt, BlendMode::Alpha, 2},
        {ScrollbarSlot, BlendMode::Alpha, 3},
        {TextMarkerSlot, BlendMode::Alpha, 1}, // "Heroico"
        {std::nullopt, BlendMode::Alpha, 1},   // la franja de "Tordo", el último (el 0xe de 0x4944c0)
        {TextMarkerSlot, BlendMode::Alpha, 1},
        {ScrollbarSlot, BlendMode::Opaque, 2}, // los de invitar: 0x461cd0 deja la mezcla opaca
        {std::nullopt, BlendMode::Opaque, 2},
        {ScrollbarSlot, BlendMode::Opaque, 3},
        {TextMarkerSlot, BlendMode::Alpha, 1},
        {std::nullopt, BlendMode::Alpha, 1},
        {TextMarkerSlot, BlendMode::Alpha, 1},
        {std::nullopt, BlendMode::Alpha, 8}, // las dos rayas, la de abajo y el negro
        {ButtonSlot, BlendMode::Alpha, 1},
        {TextMarkerSlot, BlendMode::Alpha, 1}, // "t996"
        {std::nullopt, BlendMode::Opaque, 1},  // el EDIT, sin el foco
        {ButtonSlot, BlendMode::Alpha, 1},
        {TextMarkerSlot, BlendMode::Alpha, 1}, // "t993"
        {TextureSlot{Client::UI::WindowTitleSlot}, BlendMode::Opaque, 1},
    });
    // 0x491ba0(105 + 320 − 0xa0, 101, 144 − 16): los filos del 2 en x y x + 4 y el medio del 1 en x + 1 de 3.
    ExpectBox(QuadOf(15, 0), 265.0f, 101.0f, 266.0f, 229.0f);
    EXPECT_EQ(VertexOf(15, 0).color, PaletteOf(2));
    ExpectBox(QuadOf(15, 1), 269.0f, 101.0f, 270.0f, 229.0f);
    ExpectBox(QuadOf(15, 2), 266.0f, 101.0f, 269.0f, 229.0f);
    EXPECT_EQ(VertexOf(15, 2).color, PaletteOf(1));
    // Y a 0x50 del borde, y la raya de abajo de todo el ancho.
    ExpectBox(QuadOf(15, 3), 345.0f, 101.0f, 346.0f, 229.0f);
    ExpectBox(QuadOf(15, 5), 346.0f, 101.0f, 349.0f, 229.0f);
    ExpectBox(QuadOf(15, 6), 105.0f, 229.0f, 425.0f, 230.0f);
    // El botón de mandar en x + w − 0x4a, 0xe sobre el borde de abajo.
    ExpectBox(QuadOf(19), 351.0f, 231.0f, 424.0f, 244.0f);
    // 0x461cd0: los miembros desde x + 3, del primero del deque abajo; sin +0x130, sin el lugar.
    ExpectTexts(0, {
                       {"Heroico", 273, 117, TextColors{.text = Dim, .background = 0}},
                       {"Tordo", 273, 104, TextColors{.text = 0xff000000u, .background = 0}},
                       {"Zorzal", 353, 117, TextColors{.text = Dim, .background = 0}},
                       {"Calandria", 353, 104, TextColors{.text = 0xff000000u, .background = 0}},
                   });
}

// Con tres miembros los renglones se hacen 0x50 más angostos y se ven los miembros con la raya de 0x50; sin la de
// invitar, ni ella ni su raya ni el botón de mandar.
TEST_F(LetterWindowsDrawTest, ThreeMembersShowTheMembersWithoutTheInvites)
{
    OpenAChat({{"Tordo", 1}, {"Zorzal", 2}}, {});
    ASSERT_EQ(Chat().chat.lines.base.width, 160);

    ASSERT_TRUE(Draw().HasValue());

    ExpectCommands({
        {std::nullopt, BlendMode::Alpha, 1},
        {ScrollbarSlot, BlendMode::Alpha, 2},
        {std::nullopt, BlendMode::Alpha, 2},
        {ScrollbarSlot, BlendMode::Alpha, 5},
        {std::nullopt, BlendMode::Alpha, 2},
        {ScrollbarSlot, BlendMode::Alpha, 3},
        {TextMarkerSlot, BlendMode::Alpha, 3}, // los tres miembros, sin elegido
        {std::nullopt, BlendMode::Opaque, 3},  // la raya de 0x50 sin mezcla (la deja 0x461cd0)
        {std::nullopt, BlendMode::Alpha, 2},   // la de abajo y el negro
    });
    ExpectBox(QuadOf(7, 0), 265.0f, 101.0f, 266.0f, 229.0f);
}

// 0x460550: los colores del nombre y del texto por el tipo (+0x110), sin caso para 6 o más (quedan los de antes); con
// 0x6c7a88 en 1, un fondo negro del texto queda en 0, y el del nombre no.
TEST_F(LetterWindowsDrawTest, TheChatRowColorsGoByTheKind)
{
    OpenAChat({{"Tordo", 1}}, {{1, "a", 7}, {1, "b", 5}, {1, "c", 4}, {1, "d", 2}, {1, "e", 0}});
    m_colors = TextColors{.text = 0xff123456u, .background = 0xff000000u};
    m_placement = 1;

    ASSERT_TRUE(Draw().HasValue());

    ExpectTexts(0, {
                       {"Tordo: ", 113, 213, TextColors{.text = 0xff000000u, .background = 0x32c8ffu}},
                       {"e", 155, 213, TextColors{.text = 0xff000000u, .background = 0x32c8ffu}},
                       {"Tordo: ", 113, 200, TextColors{.text = 0xff001effu, .background = 0}},
                       {"d", 155, 200, TextColors{.text = 0xff001effu, .background = 0}},
                       {"Tordo: ", 113, 187, TextColors{.text = 0xff000000u, .background = 0xffc800u}},
                       {"c", 155, 187, TextColors{.text = 0xff000000u, .background = 0xffc800u}},
                       {"Tordo: ", 113, 174, TextColors{.text = 0xff000000u, .background = 0x96ff00u}},
                       {"b", 155, 174, TextColors{.text = 0xff000000u, .background = 0x96ff00u}},
                   });

    // El 7 primero (el último renglón): sus colores son los que haya.
    m_sprites.Begin(Screen640);
    m_text.texts.clear();
    m_colors = TextColors{.text = 0xff123456u, .background = 0xff000000u};
    Chat().chat.lineRows.resize(1);
    ASSERT_TRUE(Draw().HasValue());
    ExpectTexts(0, {
                       {"Tordo: ", 113, 213, TextColors{.text = 0xff123456u, .background = 0xff000000u}},
                       {"a", 155, 213, TextColors{.text = 0xff123456u, .background = 0}},
                   });
}

// En 800 × 600 el ancho del nombre es ftol(cx / rx): 7 letras de 6 sobre 1,25.
TEST_F(LetterWindowsDrawTest, TheNameWidthIsIn640)
{
    OpenAChat({{"Tordo", 1}}, {{1, "hola", 3}});
    m_sprites.Begin(PixelSize{800, 600});

    ASSERT_TRUE(Draw().HasValue());

    ExpectTexts(0, {
                       {"Tordo: ", 113, 213, TextColors{.text = 0xffefdccdu, .background = 0}},
                       {"hola", 113 + 33, 213, TextColors{.text = Dim, .background = 0}},
                   });
}

// Con más renglones que los que entran y la lista corrida, el primero que se ve es el del corrimiento contando desde el
// último (el deque de 0x460130 tiene el último primero).
TEST_F(LetterWindowsDrawTest, TheScrolledLinesStartFromTheLast)
{
    std::vector<NewLine> lines;
    for (char letter = 'a'; letter <= 'l'; ++letter)
        lines.push_back(NewLine{.speaker = 0xff, .text = std::string(1, letter), .kind = 1});
    OldWindow& chat = OpenAChat({{"Tordo", 1}}, lines);
    const std::int32_t rows = chat.chat.lines.rows;
    ASSERT_LT(rows, 12);
    chat.chat.lines.scroll = 2;

    ASSERT_TRUE(Draw().HasValue());

    ASSERT_GE(m_text.texts.size(), static_cast<std::size_t>(rows));
    EXPECT_EQ(m_text.texts[0].text, "j");
    EXPECT_EQ(m_text.texts[0].y, 213);
    EXPECT_EQ(m_text.texts[1].text, "i");
    EXPECT_EQ(m_text.texts[static_cast<std::size_t>(rows) - 1].text,
              std::string(1, static_cast<char>('j' - (rows - 1))));
}

// Arrastrado (el estado en 4), el dibujo (0x493fc0, sin el marco) vuelve a ubicar a sus hijas con el 8 antes de
// dibujarlas; cambiando de tamaño (1), también el 9 a las tres listas.
TEST_F(LetterWindowsDrawTest, TheDraggedChatPlacesItsChildrenBeforeTheDraw)
{
    OldWindow& chat = OpenAChat({{"Tordo", 1}}, {});
    chat.base.x += 10;
    chat.base.y += 20;
    chat.base.state = Client::UI::DraggedWindow;

    Client::UI::DrawTheChatWindow(m_manager, chat, Context(), OldWindowDrawView{.texts = &m_texts});

    const OldChatPart& part = chat.chat;
    EXPECT_EQ(part.lines.base.x, 115);
    EXPECT_EQ(part.lines.base.y, 249);
    EXPECT_EQ(part.users.base.x, 280);
    EXPECT_EQ(part.invite.base.y, 249);
    EXPECT_EQ(part.inviteButton.base.x, 301);
    EXPECT_EQ(part.sendButton.base.y, 251);
    EXPECT_EQ(part.edit.base.x, 117);
    // El 9 no: el alto de los renglones queda.
    EXPECT_EQ(part.lines.base.height, 128);

    chat.base.height += 26;
    chat.base.state = Client::UI::ResizedWindow;
    m_sprites.Begin(Screen640);
    ASSERT_TRUE(Draw().HasValue());
    EXPECT_EQ(part.lines.base.height, 154);
    EXPECT_EQ(part.users.base.height, 154);
    EXPECT_EQ(part.invite.base.height, 154);
    EXPECT_EQ(part.lines.base.y, 275);
}
} // namespace
