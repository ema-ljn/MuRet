#include "UI/FriendWindowsDraw.h"

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
#include "UI/FriendWindows.h"
#include "UI/LetterModel.h"
#include "UI/LetterWindows.h"
#include "UI/OldControls.h"
#include "UI/OldWindowBase.h"
#include "UI/OldWindowDraw.h"
#include "UI/OldWindowDrawParts.h"
#include "UI/OldWindows.h"
#include "support/RecordingTextDrawer.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

// El dibujo de la ventana de amigos (CL9 hito 9; 06 § HUD y ventanas, "Los amigos"): 0x4a2620 con los encabezados
// (0x4a2460), el botón del estado (0x463090), la pestaña de amigos (0x49cc20), la de las cartas (0x4a0a30, CL9 hito 10)
// o la de ventanas (0x49e7f0) con su lista (0x47a810, la barra 0x461890 y los renglones 0x461cd0, 0x4641f0 y 0x462a60),
// y la caja de agregar (0x4a3750) con su EDIT (0x468320). La ventana de amigos queda en (0x186, 0x106), de 250 × 170:
// el área de adentro en (395, 283) de 240 × 144, las pestañas en (395, 304) de 240 × 125 y sus listas con el borde de
// abajo en 407, de 86 de alto. Los nombres y los textos son inventados.
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
using Client::UI::OldEdit;
using Client::UI::OldList;
using Client::UI::OldWindow;
using Client::UI::OldWindowDrawContext;
using Client::UI::OldWindowDrawView;
using Client::UI::OldWindowGlobals;
using Client::UI::OldWindowManager;
using Client::UI::OldWindowMeasure;
using Client::UI::OldWindowStepView;
using Client::UI::OldWindowTexts;

constexpr PixelSize Screen640{640, 480};
constexpr TextureSlot ScrollbarSlot{0x649};
constexpr TextureSlot CheckSlot{0x64a};
constexpr std::uint32_t Dim = 0xffc8dce6u;
constexpr std::uint32_t White = 0xffffffffu;
constexpr std::uint32_t Black = 0xff000000u;

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

// Un quad en 640 × 480 con la y hacia abajo: el vértice 0 es el de arriba a la izquierda y el 2 el de abajo a la
// derecha.
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

// El color de 0x491960 con el alfa en 1.
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

// Un amigo del modelo: un agregado y no un std::pair, que con un literal int en el estado convertiría a std::uint8_t
// adentro de <utility> (C4244 con /WX en MSVC).
struct NewFriend
{
    std::string name;
    std::uint8_t state = 0;
};

// Un texto: lo que dice, dónde y de qué color.
using ExpectedText = std::tuple<std::string, int, int, std::uint32_t>;

class FriendWindowsDrawTest : public ::testing::Test
{
protected:
    FriendWindowsDrawTest() : m_text(m_sprites)
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

    OldWindowStepView StepView()
    {
        return OldWindowStepView{.mouseX = 639,
                                 .mouseY = 479,
                                 .keyEdge = [](std::uint8_t /*key*/) { return false; },
                                 .keyDown = {},
                                 .chatOpen = false,
                                 .texts = &m_texts,
                                 .measure = &m_measure,
                                 .send = [](const FriendSend& /*send*/) {},
                                 .whisperTo = {},
                                 .openFriends = {}};
    }

    // 0x491080 la primera vez, con los amigos en el modelo y copiados a la pestaña (0x49ddd0).
    void OpenFriends(const std::vector<NewFriend>& friends = {})
    {
        const auto opened = Client::UI::OpenTheFriendsWindow(m_manager, Client::UI::FriendsWindowX,
                                                             Client::UI::FriendsWindowY, Globals(), StepView());
        ASSERT_TRUE(opened.HasValue()) << opened.Error();
        for (const NewFriend& entry : friends)
            Client::UI::AddAFriend(m_manager.friendModel, entry.name, 0, entry.state);
        Client::UI::RefreshTheFriendTab(m_manager);
    }

    OldWindow& Friends()
    {
        return m_manager.windows.at(m_manager.friends);
    }

    OldList& FriendList()
    {
        return Friends().tabs[Client::UI::FriendTabNumber].tab.list;
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
                                     .focusedEdit = m_focusedEdit};
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
            EXPECT_EQ(text.style.colors, (TextColors{.text = std::get<3>(expected[i]), .background = 0}));
            EXPECT_EQ(text.style.font, FontKind::Normal);
        }
    }

    LetterMeasure m_letters;
    const OldWindowTexts m_texts{.notice = "Aviso", .question = "Pregunta", .yes = "Si", .no = "No", .lookup = TextOf};
    const OldWindowMeasure m_measure{.measure = m_letters, .dcFont = FontKind::Normal, .screen = Screen640};
    OldWindowManager m_manager;
    SpriteBatch m_sprites;
    RecordingTextDrawer m_text;
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
    FontKind m_dcFont = FontKind::Bold;
    TextColors m_colors{};
};

// La pestaña de amigos con dos amigos y el segundo elegido, dibujada por 0x4a2620 antes del marco: el negro de abajo,
// la barra de la lista sin mezcla (0x49cc20 la deja), los renglones de abajo hacia arriba (el elegido con su franja y
// en negro), los cuatro botones, la franja de las columnas con sus rayas y sus títulos; después la franja negra de
// los encabezados, los tres encabezados, sus títulos, el del estado y su casilla.
TEST_F(FriendWindowsDrawTest, TheFriendTabIsDrawnLikeTheOriginal)
{
    OpenFriends({{"Zorzal", 2}, {"Hornero", 0xff}});
    Client::UI::SelectInTheList(FriendList(), 2, 2);

    const auto drawn = Draw();
    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();

    const TextureSlot button{Client::UI::WindowButtonSlot};
    ExpectCommands({
        {std::nullopt, BlendMode::Alpha, 2},   // la sombra del marco y el negro debajo de la lista
        {ScrollbarSlot, BlendMode::Opaque, 2}, // las flechas
        {std::nullopt, BlendMode::Opaque, 2},  // los filos de la pista
        {ScrollbarSlot, BlendMode::Opaque, 3}, // el cursor
        {TextMarkerSlot, BlendMode::Alpha, 2}, // "Zorzal" y su lugar
        {std::nullopt, BlendMode::Alpha, 1},   // la franja del elegido
        {TextMarkerSlot, BlendMode::Alpha, 2}, // "Hornero" y su lugar
        {button, BlendMode::Alpha, 1},
        {TextMarkerSlot, BlendMode::Alpha, 1},
        {button, BlendMode::Alpha, 1},
        {TextMarkerSlot, BlendMode::Alpha, 1},
        {button, BlendMode::Alpha, 1},
        {TextMarkerSlot, BlendMode::Alpha, 1},
        {button, BlendMode::Alpha, 1},
        {TextMarkerSlot, BlendMode::Alpha, 1},
        {std::nullopt, BlendMode::Alpha, 5},   // las rayas, la franja de las columnas, la raya del medio y su marca
        {TextMarkerSlot, BlendMode::Alpha, 2}, // los títulos de las columnas
        {std::nullopt, BlendMode::Alpha, 14},  // la franja de los encabezados, los tres y la raya
        {TextMarkerSlot, BlendMode::Alpha, 4}, // los tres títulos y el del estado
        {std::nullopt, BlendMode::Opaque, 4},  // la casilla
        // El marco sigue con la mezcla normal que dejó 0x463090.
        {TextureSlot{Client::UI::WindowTitleSlot}, BlendMode::Alpha, 1},
    });

    // 0x49cc20: el 7 en (395, 304 + 86 + 0x12) de 240 × (125 − 86 − 18).
    ExpectBox(QuadOf(0, 1), 395.0f, 408.0f, 635.0f, 429.0f);
    EXPECT_EQ(VertexOf(0, 1).color, PaletteOf(7));
    // 0x461890: las flechas de 13 × 13 en (x + w − 12, y − h − 1) y (x + w − 12, y − 12).
    ExpectBox(QuadOf(1, 0), 623.0f, 320.0f, 636.0f, 333.0f);
    EXPECT_FLOAT_EQ(VertexOf(1, 0).u, 0.0f);
    EXPECT_FLOAT_EQ(VertexOf(1, 0).v, 0.09375f);
    ExpectBox(QuadOf(1, 1), 623.0f, 395.0f, 636.0f, 408.0f);
    EXPECT_FLOAT_EQ(VertexOf(1, 1).v, 0.5f);
    // Con menos amigos que renglones, el cursor es toda la pista, con sus puntas de 1.
    ExpectBox(QuadOf(2, 0), 623.0f, 333.0f, 624.0f, 395.0f);
    ExpectBox(QuadOf(2, 1), 635.0f, 333.0f, 636.0f, 395.0f);
    EXPECT_EQ(VertexOf(2).color, PaletteOf(2));
    ExpectBox(QuadOf(3, 0), 624.0f, 333.0f, 635.0f, 395.0f);
    EXPECT_FLOAT_EQ(VertexOf(3, 0).v, 0.03125f);
    ExpectBox(QuadOf(3, 1), 624.0f, 333.0f, 635.0f, 334.0f);
    ExpectBox(QuadOf(3, 2), 624.0f, 394.0f, 635.0f, 395.0f);
    EXPECT_FLOAT_EQ(VertexOf(3, 2).v, 0.0625f);
    // 0x461cd0: la franja del elegido, 3 más arriba que su renglón, de w − 13 + 1 × 13, en 0,7 porque 0x491080 le
    // dio las flechas del teclado a la lista de amigos.
    EXPECT_EQ(m_focusedList, FriendList().base.id);
    ExpectBox(QuadOf(5), 395.0f, 321.0f, 623.0f, 334.0f);
    EXPECT_FLOAT_EQ(VertexOf(5).color.alpha, 0.7f);
    // 0x49cc20: la raya del 2 en y + 0x10, la franja del 5 de 16, la raya sobre los botones, la del medio en x + 68 (el
    // ancho de la columna del nombre) de 125 − 22 − 17 y su marca del 14, de 10, en y + 3.
    ExpectBox(QuadOf(15, 0), 395.0f, 320.0f, 635.0f, 321.0f);
    ExpectBox(QuadOf(15, 1), 395.0f, 304.0f, 635.0f, 320.0f);
    EXPECT_EQ(VertexOf(15, 1).color, PaletteOf(5));
    ExpectBox(QuadOf(15, 2), 395.0f, 407.0f, 635.0f, 408.0f);
    ExpectBox(QuadOf(15, 3), 463.0f, 321.0f, 464.0f, 407.0f);
    EXPECT_EQ(VertexOf(15, 3).color, PaletteOf(2));
    ExpectBox(QuadOf(15, 4), 463.0f, 307.0f, 464.0f, 317.0f);
    EXPECT_EQ(VertexOf(15, 4).color, PaletteOf(14));
    // 0x4a2620: el 7 de 20 en (395, 283); 0x4a2460(395, 285, 0x35, 0x13, 3, 0): el de la vista con sus filos de
    // arriba, la izquierda y la derecha y el relleno del 5, los otros con los de arriba, la derecha y abajo, 1 más
    // abajo, y el relleno del 6; la raya del 2 desde x + 0x9f, 0x12 debajo.
    ExpectBox(QuadOf(17, 0), 395.0f, 283.0f, 635.0f, 303.0f);
    ExpectBox(QuadOf(17, 1), 395.0f, 285.0f, 448.0f, 286.0f);
    ExpectBox(QuadOf(17, 2), 394.0f, 285.0f, 395.0f, 304.0f);
    ExpectBox(QuadOf(17, 3), 447.0f, 285.0f, 448.0f, 304.0f);
    ExpectBox(QuadOf(17, 4), 395.0f, 286.0f, 447.0f, 304.0f);
    EXPECT_EQ(VertexOf(17, 4).color, PaletteOf(5));
    ExpectBox(QuadOf(17, 5), 448.0f, 286.0f, 501.0f, 287.0f);
    ExpectBox(QuadOf(17, 6), 500.0f, 286.0f, 501.0f, 304.0f);
    ExpectBox(QuadOf(17, 7), 448.0f, 303.0f, 501.0f, 304.0f);
    ExpectBox(QuadOf(17, 8), 448.0f, 287.0f, 500.0f, 303.0f);
    EXPECT_EQ(VertexOf(17, 8).color, PaletteOf(6));
    ExpectBox(QuadOf(17, 9), 501.0f, 286.0f, 554.0f, 287.0f);
    ExpectBox(QuadOf(17, 13), 554.0f, 303.0f, 635.0f, 304.0f);
    // 0x463090: la casilla de 9 × 9 del 0, 14 + 1 a la izquierda del texto del estado y 1 más arriba.
    ExpectBox(QuadOf(19, 0), 588.0f, 287.0f, 597.0f, 288.0f);
    ExpectBox(QuadOf(19, 1), 588.0f, 295.0f, 597.0f, 296.0f);
    ExpectBox(QuadOf(19, 2), 588.0f, 287.0f, 589.0f, 296.0f);
    ExpectBox(QuadOf(19, 3), 596.0f, 287.0f, 597.0f, 296.0f);
    EXPECT_EQ(VertexOf(19).color, PaletteOf(0));

    // Los renglones de abajo hacia arriba: el primero del deque abajo. El lugar, 4 después de la columna del nombre:
    // el servidor 2 con el formato 1042 y fuera de línea con el 1039.
    ExpectTexts(0, {
                       {"Zorzal", 398, 337, Dim},
                       {"t1042", 470, 337, Dim},
                       {"Hornero", 398, 324, Black},
                       {"t1039", 470, 324, Black},
                   });
    // Los títulos de las columnas en (x + 4, y + 3), el del orden en blanco; los de los encabezados centrados en su
    // lugar ((52 − 30 + 0,5) · 0,5 y (24 − 13 + 0,5) · 0,5), el de la vista en blanco; el del estado contra el borde.
    ExpectTexts(8, {
                       {"t1021", 399, 307, White},
                       {"t1022", 467, 307, Dim},
                       {"t1032", 406, 288, White},
                       {"t1034", 460, 288, Dim},
                       {"t1033", 513, 288, Dim},
                       {"t1035", 603, 288, Dim},
                   });
    EXPECT_EQ(m_text.texts.size(), 15u);
    EXPECT_EQ(m_dcFont, FontKind::Normal);
}

// 0x49cf3c..0x49d099: el título de la columna bajo el mouse y el del orden van en blanco, y después de cada uno el
// color vuelve a 0xffc8dce6.
TEST_F(FriendWindowsDrawTest, TheColumnTitleUnderTheMouseAndTheSortedOneAreWhite)
{
    OpenFriends({{"Zorzal", 2}});
    m_manager.friendModel.sortMode = Client::UI::ByStateSort;

    ASSERT_TRUE(Draw(400, 310).HasValue());

    // El renglón y su lugar, los cuatro botones y los dos títulos.
    ExpectTexts(6, {{"t1021", 399, 307, White}, {"t1022", 467, 307, White}});
}

// Con la lista corrida y el clic sobre la flecha de arriba: la flecha dada vuelta y el cursor de la barra en su lugar
// (+0x110 y +0x118 de 0x47b9c0), con sus puntas.
TEST_F(FriendWindowsDrawTest, TheBarArrowTurnsOverWithTheClickAndTheThumbFollowsTheScroll)
{
    OpenFriends({{"Ana", 0},
                 {"Bruno", 0},
                 {"Carla", 0},
                 {"Dario", 0},
                 {"Elsa", 0},
                 {"Fabio", 0},
                 {"Gala", 0},
                 {"Hugo", 0},
                 {"Irene", 0},
                 {"Juan", 0}});
    FriendList().scroll = 2;
    m_leftClick = true;

    ASSERT_TRUE(Draw(625, 322).HasValue());

    // La de arriba desde (0,8125, 0,90625) con −0,8125 × −0,40625; la de abajo, sin el mouse, como siempre.
    EXPECT_FLOAT_EQ(VertexOf(1, 0, 0).u, 0.8125f);
    EXPECT_FLOAT_EQ(VertexOf(1, 0, 0).v, 0.90625f);
    EXPECT_FLOAT_EQ(VertexOf(1, 0, 2).u, 0.0f);
    EXPECT_FLOAT_EQ(VertexOf(1, 0, 2).v, 0.5f);
    EXPECT_FLOAT_EQ(VertexOf(1, 1, 0).u, 0.0f);
    EXPECT_FLOAT_EQ(VertexOf(1, 1, 0).v, 0.5f);
    const OldList& list = FriendList();
    ASSERT_LT(list.thumbHeight, list.barBottom - list.barTop);
    ExpectBox(QuadOf(3, 0), 624.0f, list.thumbY, 635.0f, list.thumbY + list.thumbHeight);
    ExpectBox(QuadOf(3, 1), 624.0f, list.thumbY, 635.0f, list.thumbY + 1.0f);
    ExpectBox(QuadOf(3, 2), 624.0f, list.thumbY + list.thumbHeight - 1.0f, 635.0f, list.thumbY + list.thumbHeight);
    // Seis renglones desde el tercero del deque.
    ASSERT_GE(m_text.texts.size(), 12u);
    EXPECT_EQ(m_text.texts[0].text, Friends().tabs[0].tab.friends[2].name);
    EXPECT_EQ(m_text.texts[10].text, Friends().tabs[0].tab.friends[7].name);
}

// Con +0xe0 en 1 (salió de línea), la casilla lleva la marca de win_check.tga (0x64a) de 5 × 5 en (x + 2, y + 2).
TEST_F(FriendWindowsDrawTest, TheStateBoxIsMarkedOffline)
{
    OpenFriends();
    m_manager.offlineSent = 1;

    ASSERT_TRUE(Draw().HasValue());

    const std::span<const DrawCommand> commands = m_sprites.Commands();
    const auto mark = std::find_if(commands.begin(), commands.end(),
                                   [](const DrawCommand& command) { return command.texture == CheckSlot; });
    ASSERT_NE(mark, commands.end());
    const auto at = static_cast<std::size_t>(mark - commands.begin());
    EXPECT_EQ(mark->blend, BlendMode::Alpha);
    ExpectBox(QuadOf(at), 590.0f, 289.0f, 595.0f, 294.0f);
    EXPECT_FLOAT_EQ(VertexOf(at, 0, 2).u, 0.625f);
}

// 0x49e7f0: la pestaña de las ventanas no vuelve a la mezcla opaca después del negro, dibuja el título de cada caja en
// x + 8, su botón, las rayas sin la del medio y el título 1026 en (x + 8, y + 3). La franja del elegido va en 0,3:
// las flechas del teclado son de la lista de amigos.
TEST_F(FriendWindowsDrawTest, TheWindowsTabDrawsTheBoxTitles)
{
    OpenFriends();
    const auto opened = Client::UI::CreateTheOldWindow(
        m_manager,
        Client::UI::OldWindowRequest{
            .kind = Client::UI::NoticeCase, .x = -1, .y = -1, .data = "aviso uno", .aux = 0, .flags = 0},
        m_texts, m_measure);
    ASSERT_TRUE(opened.HasValue()) << opened.Error();
    Friends().friends.currentTab = Client::UI::WindowsTabNumber;
    Friends().friends.hoverTab = Client::UI::WindowsTabNumber;

    ASSERT_TRUE(Draw().HasValue());

    ExpectCommands({
        {std::nullopt, BlendMode::Alpha, 2}, // la sombra y el negro debajo de la lista
        {ScrollbarSlot, BlendMode::Alpha, 2},
        {std::nullopt, BlendMode::Alpha, 2},
        {ScrollbarSlot, BlendMode::Alpha, 3},
        {std::nullopt, BlendMode::Alpha, 1},   // la franja del elegido
        {TextMarkerSlot, BlendMode::Alpha, 1}, // "aviso uno"
        {TextureSlot{Client::UI::WindowButtonSlot}, BlendMode::Alpha, 1},
        {TextMarkerSlot, BlendMode::Alpha, 1},
        {std::nullopt, BlendMode::Alpha, 3},   // las dos rayas y la franja de la columna
        {TextMarkerSlot, BlendMode::Alpha, 1}, // "t1026"
        {std::nullopt, BlendMode::Alpha, 14},
    });
    EXPECT_FLOAT_EQ(VertexOf(4).color.alpha, 0.3f);
    // El encabezado de la vista es el tercero: sus filos desde 501 y el relleno del 5.
    ExpectBox(QuadOf(10, 9), 501.0f, 285.0f, 554.0f, 286.0f);
    ExpectBox(QuadOf(10, 12), 501.0f, 286.0f, 553.0f, 304.0f);
    EXPECT_EQ(VertexOf(10, 12).color, PaletteOf(5));
    ExpectTexts(0, {{"aviso uno", 403, 324, Black}});
    ExpectTexts(2, {
                       {"t1026", 403, 307, Dim},
                       {"t1032", 406, 288, Dim},
                       {"t1034", 460, 288, Dim},
                       {"t1033", 513, 288, White},
                   });
}

// Sin el foco en la de la vista, el título del encabezado bajo el mouse (+0xb4) también va en blanco.
TEST_F(FriendWindowsDrawTest, TheHeaderUnderTheMouseIsWhite)
{
    OpenFriends();
    Friends().friends.hoverTab = Client::UI::WindowsTabNumber;

    ASSERT_TRUE(Draw().HasValue());

    ExpectTexts(6, {
                       {"t1032", 406, 288, White},
                       {"t1034", 460, 288, Dim},
                       {"t1033", 513, 288, White},
                   });
}

// La pestaña de las cartas (0x4a0a30, CL9 hito 10) con dos cartas, la primera marcada y sin leer, la segunda leída y
// elegida, todas marcadas y el orden por la fecha: el negro de abajo, la barra de la lista sin mezcla, los renglones
// (0x4641f0) de abajo hacia arriba con su casilla, su sobre y sus tres textos en cajas, la franja de las columnas con
// sus tres rayas y sus marcas, la casilla de todas y el sobre, los títulos (el del orden en blanco) y los cuatro
// botones; después, lo de la ventana de amigos.
TEST_F(FriendWindowsDrawTest, TheLettersTabIsDrawnLikeTheOriginal)
{
    OpenFriends();
    Client::UI::AddALetter(m_manager.letterModel, 1, "Tordo", "Asado", "2026/11/13", "21:30", 0);
    Client::UI::AddALetter(m_manager.letterModel, 2, "Calandria", "Mate", "2026/11/12", "08:15", 1);
    Client::UI::RefreshTheLettersTab(m_manager);
    OldWindow& tab = Friends().tabs[Client::UI::LettersTabNumber];
    ASSERT_EQ(tab.tab.letters.size(), 2u);
    tab.tab.letters[0].selected = 1;
    tab.tab.allChecked = 1;
    Client::UI::SelectInTheList(tab.tab.list, 2, 2);
    m_manager.letterModel.sortMode = Client::UI::ByDateLetters;
    Friends().friends.currentTab = Client::UI::LettersTabNumber;
    // 0x463190: la columna de la marca de 0x19 y las de los textos de 5 letras, + 8.
    ASSERT_EQ(tab.tab.list.headers, 0x19);
    ASSERT_EQ(tab.tab.list.nameWidth, 38);
    ASSERT_EQ(tab.tab.list.placeWidth, 38);
    ASSERT_EQ(tab.tab.list.subjectWidth, 38);

    const auto drawn = Draw();
    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();

    const TextureSlot button{Client::UI::WindowButtonSlot};
    const TextureSlot mail{0x64b};
    std::vector<ExpectedCommand> expected{
        {std::nullopt, BlendMode::Alpha, 2},   // la sombra del marco y el negro debajo de la lista
        {ScrollbarSlot, BlendMode::Opaque, 2}, // la barra
        {std::nullopt, BlendMode::Opaque, 2},
        {ScrollbarSlot, BlendMode::Opaque, 3},
        {std::nullopt, BlendMode::Opaque, 4}, // "Tordo": la casilla, marcada, el sobre y los textos
        {CheckSlot, BlendMode::Alpha, 1},
        {mail, BlendMode::Alpha, 1},
        {TextMarkerSlot, BlendMode::Alpha, 3},
        {std::nullopt, BlendMode::Alpha, 1}, // "Calandria", elegida: la franja
        {std::nullopt, BlendMode::Opaque, 4},
        {mail, BlendMode::Alpha, 1},
        {TextMarkerSlot, BlendMode::Alpha, 3},
        {std::nullopt, BlendMode::Alpha, 9},  // la franja de las columnas, las tres rayas y las tres marcas
        {std::nullopt, BlendMode::Opaque, 4}, // la casilla de todas, marcada, y el sobre
        {CheckSlot, BlendMode::Alpha, 1},
        {mail, BlendMode::Alpha, 1},
        {TextMarkerSlot, BlendMode::Alpha, 3}, // los títulos de las columnas
    };
    for (std::size_t at = 0; at < 4; ++at)
    {
        expected.push_back({button, BlendMode::Alpha, 1});
        expected.push_back({TextMarkerSlot, BlendMode::Alpha, 1});
    }
    expected.push_back({std::nullopt, BlendMode::Alpha, 14});
    expected.push_back({TextMarkerSlot, BlendMode::Alpha, 4});
    expected.push_back({std::nullopt, BlendMode::Opaque, 4});
    expected.push_back({TextureSlot{Client::UI::WindowTitleSlot}, BlendMode::Alpha, 1});
    ExpectCommands(expected);

    // 0x4a0ac1: el 7 en (395, 304 + 86 + 0x12) de 240 × (125 − 86 − 18).
    ExpectBox(QuadOf(0, 1), 395.0f, 408.0f, 635.0f, 429.0f);
    // 0x4641f0: la casilla en (x + 1, y − 1) y el sobre de 13 × 9 en (x + 0xb, y − 1); el de la leída, de 13 × 10 en
    // y − 2 y desde 0,28125 de la textura.
    ExpectBox(QuadOf(4, 0), 396.0f, 336.0f, 405.0f, 337.0f);
    ExpectBox(QuadOf(6), 406.0f, 336.0f, 419.0f, 345.0f);
    EXPECT_FLOAT_EQ(VertexOf(6, 0, 0).v, 0.0f);
    EXPECT_FLOAT_EQ(VertexOf(6, 0, 2).u, 0.8125f);
    EXPECT_FLOAT_EQ(VertexOf(6, 0, 2).v, 0.28125f);
    ExpectBox(QuadOf(8), 395.0f, 321.0f, 623.0f, 334.0f);
    ExpectBox(QuadOf(9, 0), 396.0f, 323.0f, 405.0f, 324.0f);
    ExpectBox(QuadOf(10), 406.0f, 322.0f, 419.0f, 332.0f);
    EXPECT_FLOAT_EQ(VertexOf(10, 0, 0).v, 0.28125f);
    EXPECT_FLOAT_EQ(VertexOf(10, 0, 2).v, 0.59375f);
    // 0x4a0c53..0x4a0e70: las rayas en x + 0x19, x + 0x19 + 38 y x + 0x19 + 76, de 125 − 22 − 17 desde y + 0x11, y sus
    // marcas del 14, de 10, en y + 3.
    ExpectBox(QuadOf(12, 3), 420.0f, 321.0f, 421.0f, 407.0f);
    ExpectBox(QuadOf(12, 4), 458.0f, 321.0f, 459.0f, 407.0f);
    ExpectBox(QuadOf(12, 5), 496.0f, 321.0f, 497.0f, 407.0f);
    EXPECT_EQ(VertexOf(12, 5).color, PaletteOf(2));
    ExpectBox(QuadOf(12, 6), 420.0f, 307.0f, 421.0f, 317.0f);
    ExpectBox(QuadOf(12, 8), 496.0f, 307.0f, 497.0f, 317.0f);
    EXPECT_EQ(VertexOf(12, 6).color, PaletteOf(14));
    // 0x4a0e91..0x4a0efd: la casilla de todas en (x + 1, y + 3) y el sobre en (x + 0xb, y + 3).
    ExpectBox(QuadOf(13, 0), 396.0f, 307.0f, 405.0f, 308.0f);
    ExpectBox(QuadOf(15), 406.0f, 307.0f, 419.0f, 316.0f);

    // El nombre 4 después de la marca en una caja de 38 − 4, la fecha sin "20" centrada en la de 38 (48 columnas, desde
    // la 5) y el asunto 4 después de la fecha, en lo que queda hasta la barra: 240 − 13 − 101 − 4.
    ExpectTexts(0, {
                       {"Tordo", 424, 337, Dim},
                       {"26/11/13", 458, 337, Dim},
                       {"Asado", 500, 337, Dim},
                       {"Calandria", 424, 324, Black},
                       {"26/11/12", 458, 324, Black},
                       {"Mate", 500, 324, Black},
                       {"t1028", 424, 307, Dim},
                       {"t1029", 462, 307, White},
                       {"t1030", 500, 307, Dim},
                   });
    const std::vector<std::tuple<int, int>> boxes{{0, 34}, {5, 38}, {0, 122}, {0, 34}, {5, 38}, {0, 122}};
    for (std::size_t at = 0; at < boxes.size(); ++at)
    {
        SCOPED_TRACE(at);
        EXPECT_TRUE(m_text.texts[at].windowed);
        EXPECT_EQ(m_text.texts[at].firstColumn, std::get<0>(boxes[at]));
        EXPECT_EQ(m_text.texts[at].columns, std::get<1>(boxes[at]));
    }
}

// En 800 × 600 las cajas de los textos de un renglón van en píxeles: ftol(38 · 1,25 − 4), ftol(38 · 1,25) y
// ftol(122 · 1,25). El título bajo el mouse va en blanco.
TEST_F(FriendWindowsDrawTest, TheLetterBoxesAreInPixels)
{
    OpenFriends();
    Client::UI::AddALetter(m_manager.letterModel, 1, "Tordo", "Asado", "2026/11/13", "21:30", 0);
    Client::UI::RefreshTheLettersTab(m_manager);
    Friends().friends.currentTab = Client::UI::LettersTabNumber;
    m_sprites.Begin(PixelSize{800, 600});

    Client::UI::DrawTheFriendsWindow(
        m_manager, Friends(), Context(),
        OldWindowDrawView{.mouseX = 500, .mouseY = 310, .friendsOpen = true, .texts = &m_texts});

    ASSERT_GE(m_text.texts.size(), 6u);
    EXPECT_EQ(m_text.texts[0].columns, 43);
    // (47 − 48) / 2 trunca hacia el 0: la fecha desde su columna 0, con sus 48.
    EXPECT_EQ(m_text.texts[1].firstColumn, 0);
    EXPECT_EQ(m_text.texts[1].columns, 48);
    EXPECT_EQ(m_text.texts[2].columns, 152);
    // El mouse en (500, 310): sobre la columna del asunto, desde x + 0x19 + 76 de 38 × 0x13.
    ExpectTexts(3, {{"t1028", 424, 307, Dim}, {"t1029", 462, 307, Dim}, {"t1030", 500, 307, White}});
}

// La caja de agregar (0x4a3750): los dos botones y, con la mezcla normal, el EDIT (0x468320): con +0x4c & 8 el negro
// de w × el alto de "1", el bitmap de 1,5 + 1,5 + cx de ancho con el fondo en 0, el texto y, con el foco, el cursor
// en la columna después del texto. Termina sin mezcla, y la barra del título del marco sale así.
TEST_F(FriendWindowsDrawTest, TheAddBoxDrawsItsEditWithTheCaret)
{
    OpenFriends();
    ASSERT_TRUE(Client::UI::StepTheOldWindows(m_manager, Globals(), StepView()).HasValue());
    Client::UI::PostToTheWindow(m_manager, Friends().tabs[0].base.id, Client::UI::CommandMessage, 1, 0);
    ASSERT_TRUE(Client::UI::StepTheOldWindows(m_manager, Globals(), StepView()).HasValue());
    ASSERT_NE(m_manager.letter, 0);
    OldEdit& edit = m_manager.windows.at(m_manager.letter).add.edit;
    ASSERT_EQ(edit.base.x, 35);
    ASSERT_EQ(edit.base.y, 367);
    edit.text.Type("Tero");
    m_focusedEdit = edit.base.id;

    ASSERT_TRUE(Draw().HasValue());

    const std::span<const DrawCommand> commands = m_sprites.Commands();
    std::optional<std::size_t> black;
    for (std::size_t i = 0; i + 1 < commands.size(); ++i)
    {
        if (!commands[i].texture && commands[i].vertexCount == 12 &&
            VertexOf(i).color == Color{.red = 0.0f, .green = 0.0f, .blue = 0.0f, .alpha = 1.0f} &&
            commands[i + 1].texture == TextMarkerSlot)
            black = i;
    }
    ASSERT_TRUE(black.has_value());
    ASSERT_GE(commands.size(), *black + 4);
    EXPECT_EQ(commands[*black].blend, BlendMode::Alpha);
    ExpectBox(QuadOf(*black, 0), 35.0f, 367.0f, 115.0f, 380.0f);
    ExpectBox(QuadOf(*black, 1), 35.0f, 367.0f, 62.0f, 381.0f);
    EXPECT_FLOAT_EQ(VertexOf(*black, 1).color.alpha, 0.0f);
    ExpectBox(QuadOf(*black + 2), 60.0f, 367.0f, 61.0f, 380.0f);
    EXPECT_FLOAT_EQ(VertexOf(*black + 2).color.red, 200.0f / 255.0f);
    EXPECT_EQ(commands[*black + 3].texture, TextureSlot{Client::UI::WindowTitleSlot});
    EXPECT_EQ(commands[*black + 3].blend, BlendMode::Opaque);
    EXPECT_EQ(edit.draws, 1);
    EXPECT_FLOAT_EQ(edit.caretHeight, 13.0f);
    const auto tero =
        std::find_if(m_text.texts.begin(), m_text.texts.end(), [](const auto& text) { return text.text == "Tero"; });
    ASSERT_NE(tero, m_text.texts.end());
    EXPECT_EQ(tero->x, 35);
    EXPECT_EQ(tero->y, 367);
    EXPECT_EQ(tero->style.colors, (TextColors{.text = White, .background = 0}));
}

// 0x4684bc..0x4684e4: el cursor se ve con el foco de Windows y +0x80 % 24 < 12; oculto (la vtable + 4 con el estado
// 3), el EDIT no dibuja ni cuenta.
TEST_F(FriendWindowsDrawTest, TheEditCaretBlinksAndAHiddenEditDrawsNothing)
{
    OldEdit edit;
    edit.base.x = 20;
    edit.base.y = 40;
    edit.base.width = 80;
    edit.base.height = 14;
    edit.text.Type("ab");
    const auto quads = [this] { return m_sprites.Vertices().size() / 6; };

    edit.draws = 11;
    Client::UI::OldDraw::DrawTheOldEdit(Context(), edit, true);
    // El bitmap, el texto y el cursor.
    EXPECT_EQ(quads(), 3u);
    EXPECT_EQ(edit.draws, 12);

    m_sprites.Begin(Screen640);
    Client::UI::OldDraw::DrawTheOldEdit(Context(), edit, true);
    EXPECT_EQ(quads(), 2u);
    EXPECT_EQ(edit.draws, 13);

    m_sprites.Begin(Screen640);
    edit.draws = 24;
    Client::UI::OldDraw::DrawTheOldEdit(Context(), edit, false);
    EXPECT_EQ(quads(), 2u);

    m_sprites.Begin(Screen640);
    edit.base.state = Client::UI::HiddenWindow;
    Client::UI::OldDraw::DrawTheOldEdit(Context(), edit, true);
    EXPECT_EQ(quads(), 0u);
    EXPECT_EQ(edit.draws, 25);
}
} // namespace
