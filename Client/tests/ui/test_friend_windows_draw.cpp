#include "UI/FriendWindowsDraw.h"

#include "App/WindowState.h"
#include "Core/Log.h"
#include "Core/ILogSink.h"
#include "Core/Expected.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Render/Text/TextBox.h"
#include "Render/Text/TextCache.h"
#include "Render/Text/TextStyle.h"
#include "Render/TextureSlot.h"
#include "UI/ChatInput.h"
#include "UI/FriendModel.h"
#include "UI/FriendWindows.h"
#include "UI/GameUiState.h"
#include "UI/OldControls.h"
#include "UI/OldWindowBase.h"
#include "UI/OldWindowDraw.h"
#include "UI/OldWindowDrawParts.h"
#include "UI/OldWindows.h"
#include "Scenes/TextCacheDrawer.h"
#include "support/FakeTextureStore.h"
#include "support/RecordingSink.h"
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
// (0x4a2460), el botón del estado (0x463090), la pestaña de amigos (0x49cc20) o la de ventanas (0x49e7f0) con su lista
// (0x47a810, la barra 0x461890 y los renglones 0x461cd0 y 0x462a60), y la caja de agregar (0x4a3750) con su EDIT
// (0x468320). La ventana de amigos queda en (0x186, 0x106), de 250 × 170: el área de adentro en (395, 283) de 240 ×
// 144, las pestañas en (395, 304) de 240 × 125 y sus listas con el borde de abajo en 407, de 86 de alto. Los nombres
// y los textos son inventados.
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
constexpr std::uint8_t EnterKey = 0x0d;

class LetterMeasure final : public ITextMeasure
{
public:
    [[nodiscard]] TextExtent Measure(FontKind /*font*/, std::string_view text) const override
    {
        return {static_cast<int>(text.size()) * RecordingTextDrawer::LetterWidth, RecordingTextDrawer::Height};
    }
};

class PixelCaretRasterizer final : public Client::Render::Text::IGlyphRasterizer
{
public:
    [[nodiscard]] TextExtent Measure(FontKind /*font*/, std::string_view text) const override
    {
        int width = 0;
        for (const char character : text)
            width += Advance(character);
        return {width, 13};
    }

    void DrawInk(FontKind /*font*/, std::string_view text, int firstColumn,
                 Client::Render::Text::InkMask ink) const override
    {
        int glyphLeft = 0;
        for (const char character : text)
        {
            SetInk(ink, glyphLeft - firstColumn, 0);
            if (character == 'c')
            {
                for (int row = 3; row < 6; ++row)
                    SetInk(ink, glyphLeft + 1 - firstColumn, row);
            }
            glyphLeft += Advance(character);
        }
    }

private:
    static int Advance(char character)
    {
        return character == 'c' ? 5 : 6;
    }

    static void SetInk(Client::Render::Text::InkMask ink, int column, int row)
    {
        if (column >= 0 && column < Client::Render::Text::TextBoxWidth && row >= 0 &&
            row < Client::Render::Text::TextBoxHeight)
        {
            ink[static_cast<std::size_t>(row * Client::Render::Text::TextBoxWidth + column)] = 0xff;
        }
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
                                .doubleClick = m_doubleClick};
    }

    OldWindowStepView StepView()
    {
        return OldWindowStepView{.mouseX = 639,
                                 .mouseY = 479,
                                 .keyEdge = [this](std::uint8_t key)
                                 { return key == EnterKey && Client::UI::LatchTheKey(m_enterLatch, m_enterHeld); },
                                 .keyDown = {},
                                 .chatOpen = false,
                                 .texts = &m_texts,
                                 .measure = &m_measure,
                                 .send = [this](const FriendSend& send) { m_sent.push_back(send); },
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
        const OldWindowGlobals globals = Globals();
        const OldWindowStepView stepView = StepView();
        const OldWindowDrawView view{.mouseX = x,
                                     .mouseY = y,
                                     .leftClick = m_leftClick,
                                     .friendsOpen = true,
                                     .texts = &m_texts,
                                     .focusedList = m_focusedList,
                                     .focusedEdit = m_focusedEdit,
                                     .globals = m_withStepContext ? &globals : nullptr,
                                     .stepView = m_withStepContext ? &stepView : nullptr};
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

    bool m_withStepContext = false;
    bool m_enterHeld = false;
    std::int32_t m_enterLatch = 0;
    std::vector<FriendSend> m_sent;
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

// 0x4a2620 llama 0x45aa50: el paso con 1 atiende la cola funcional antes de recolocar; no difiere el comando.
TEST_F(FriendWindowsDrawTest, MovingTheWindowProcessesThePendingTabCommandDuringDrawing)
{
    m_withStepContext = true;
    OpenFriends();
    Friends().base.state = Client::UI::DraggedWindow;
    Client::UI::PostToTheWindow(m_manager, Friends().tabs[0].base.id, Client::UI::CommandMessage, 4, 0);

    const auto drawn = Draw();

    ASSERT_FALSE(drawn.HasValue());
    EXPECT_NE(drawn.Error().find("0x49d0b0(4)"), std::string::npos);
}

// 0x49d422 y 0x49d118: Name y Answer conservan orden, envían durante la pasada real y no vuelven al paso siguiente.
TEST_F(FriendWindowsDrawTest, DrawingSendsPendingNamesAndDeleteAnswersInOrderOnce)
{
    m_withStepContext = true;
    OpenFriends({{"Tero", 1}});
    Client::UI::SelectInTheList(FriendList(), 1, 1);
    Friends().base.state = Client::UI::DraggedWindow;
    Friends().tabs[0].base.queue.push_back(
        Client::UI::OldMessage{.type = Client::UI::NameMessage, .a = 0, .b = 1, .text = "Tordo"});
    Client::UI::PostToTheWindow(m_manager, Friends().tabs[0].base.id, Client::UI::AnswerMessage, 0, 1);

    ASSERT_TRUE(Draw().HasValue());

    EXPECT_EQ(m_sent,
              (std::vector<FriendSend>{{.kind = Client::UI::FriendSendKind::Add, .value = 0, .name = "Tordo"},
                                       {.kind = Client::UI::FriendSendKind::Delete, .value = 0, .name = "Tero"}}));
    EXPECT_TRUE(Friends().tabs[0].base.queue.empty());
    ASSERT_TRUE(Client::UI::StepTheOldWindows(m_manager, Globals(), StepView()).HasValue());
    EXPECT_EQ(m_sent.size(), 2u);
}

// 0x4a43eb consulta Enter incluso con arg1: la pregunta recién creada responde y la pestaña manda C3 en Draw.
TEST_F(FriendWindowsDrawTest, DrawingADeleteCommandReadsEnterAndKeepsTheManagersClosePending)
{
    m_withStepContext = true;
    OpenFriends({{"Tero", 1}});
    Client::UI::SelectInTheList(FriendList(), 1, 1);
    Friends().base.state = Client::UI::DraggedWindow;
    m_enterHeld = true;
    Client::UI::PostToTheWindow(m_manager, Friends().tabs[0].base.id, Client::UI::CommandMessage, 2, 0);

    ASSERT_TRUE(Draw().HasValue());

    ASSERT_EQ(m_sent.size(), 1u);
    EXPECT_EQ(m_sent[0], (FriendSend{.kind = Client::UI::FriendSendKind::Delete, .value = 0, .name = "Tero"}));
    EXPECT_EQ(m_enterLatch, 1);
    ASSERT_NE(m_manager.modal, 0);
    EXPECT_TRUE(m_manager.windows.contains(m_manager.modal));
    EXPECT_TRUE(
        std::ranges::any_of(m_manager.queue, [this](const Client::UI::OldMessage& message)
                            { return message.type == Client::UI::CloseMessage && message.a == m_manager.modal; }));
    ASSERT_TRUE(Draw().HasValue());
    EXPECT_EQ(m_sent.size(), 1u);
}

// 0x490390 avanza la lista después de dibujar: Command1 agrega la caja; el caret vacío queda recortado por el bitmap.
TEST_F(FriendWindowsDrawTest, DrawingAnAddCommandClipsTheNewEmptyEditCaretInTheSamePass)
{
    m_withStepContext = true;
    OpenFriends();
    Friends().base.state = Client::UI::DraggedWindow;
    Client::UI::PostToTheWindow(m_manager, Friends().tabs[0].base.id, Client::UI::CommandMessage, 1, 0);

    ASSERT_TRUE(Draw().HasValue());

    ASSERT_NE(m_manager.letter, 0);
    const OldEdit& edit = m_manager.windows.at(m_manager.letter).add.edit;
    EXPECT_EQ(m_focus, ChatFocus::OldWindow);
    EXPECT_EQ(m_manager.focusedEdit, edit.base.id);
    EXPECT_EQ(edit.draws, 1);
    const auto caret = std::ranges::find_if(
        m_sprites.Vertices(), [](const SpriteVertex& vertex)
        { return vertex.color == Color{200.0f / 255.0f, 200.0f / 255.0f, 200.0f / 255.0f, 1.0f}; });
    EXPECT_EQ(caret, m_sprites.Vertices().end());
}

TEST_F(FriendWindowsDrawTest, AFunctionalDrawMessageWithoutStepServicesIsAnExplicitError)
{
    OpenFriends();
    Friends().base.state = Client::UI::DraggedWindow;
    Client::UI::PostToTheWindow(m_manager, Friends().tabs[0].base.id, Client::UI::CommandMessage, 1, 0);

    const auto drawn = Draw();

    ASSERT_FALSE(drawn.HasValue());
    EXPECT_NE(drawn.Error().find("0x45aa50"), std::string::npos);
    ASSERT_FALSE(Friends().tabs[0].base.queue.empty());
    EXPECT_EQ(Friends().tabs[0].base.queue.front().type, Client::UI::CommandMessage);
}

// La pestaña de las cartas (0x4a0a30) es del hito 10: el dibujo corta con la dirección.
TEST_F(FriendWindowsDrawTest, TheLettersTabIsACut)
{
    OpenFriends();
    Friends().friends.currentTab = Client::UI::LettersTabNumber;

    const auto drawn = Client::UI::DrawTheFriendsWindow(m_manager, Friends(), Context(),
                                                        OldWindowDrawView{.friendsOpen = true, .texts = &m_texts});

    ASSERT_FALSE(drawn.HasValue());
    EXPECT_NE(drawn.Error().find("0x4a2620"), std::string::npos);
    EXPECT_NE(drawn.Error().find("0x4a0a30"), std::string::npos);
    // 0x490390 lo pasa para arriba.
    EXPECT_FALSE(Draw().HasValue());
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
    edit.text.SetCursor(2);
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
    ASSERT_GE(commands.size(), *black + 3);
    EXPECT_EQ(commands[*black].blend, BlendMode::Alpha);
    ExpectBox(QuadOf(*black, 0), 35.0f, 367.0f, 115.0f, 380.0f);
    ExpectBox(QuadOf(*black, 1), 35.0f, 367.0f, 62.0f, 381.0f);
    EXPECT_FLOAT_EQ(VertexOf(*black, 1).color.alpha, 0.0f);
    ASSERT_EQ(commands[*black + 1].texture, TextMarkerSlot);
    ASSERT_EQ(commands[*black + 1].vertexCount, 18u);
    ExpectBox(QuadOf(*black + 1, 0), 38.0f, 367.0f, 51.0f, 380.0f);
    ExpectBox(QuadOf(*black + 1, 1), 52.0f, 367.0f, 62.0f, 380.0f);
    ExpectBox(QuadOf(*black + 1, 2), 51.0f, 367.0f, 52.0f, 380.0f);
    EXPECT_EQ(commands[*black + 2].texture, TextureSlot{Client::UI::WindowTitleSlot});
    EXPECT_EQ(commands[*black + 2].blend, BlendMode::Opaque);
    EXPECT_EQ(edit.draws, 1);
    EXPECT_FLOAT_EQ(edit.caretHeight, 13.0f);
    const auto tero =
        std::find_if(m_text.texts.begin(), m_text.texts.end(), [](const auto& text) { return text.text == "Tero"; });
    ASSERT_NE(tero, m_text.texts.end());
    EXPECT_EQ(tero->x, 38);
    EXPECT_EQ(tero->y, 367);
    EXPECT_TRUE(tero->pixelPosition);
    EXPECT_EQ(tero->style.colors, (TextColors{.text = White, .background = 0}));
    EXPECT_EQ(std::ranges::count_if(m_text.texts, [](const auto& text) { return text.text == "Tero"; }), 3);
    EXPECT_EQ(tero->firstColumn, 0);
    EXPECT_EQ(tero->columns, 13);
    const auto trailing = std::find_if(m_text.texts.begin(), m_text.texts.end(),
                                       [](const auto& text) { return text.text == "Tero" && text.firstColumn == 14; });
    ASSERT_NE(trailing, m_text.texts.end());
    EXPECT_EQ(trailing->x, 52);
    EXPECT_EQ(trailing->columns, 10);
    const auto caret = std::find_if(m_text.texts.begin(), m_text.texts.end(),
                                    [](const auto& text) { return text.text == "Tero" && text.columns == 1; });
    ASSERT_NE(caret, m_text.texts.end());
    EXPECT_EQ(caret->x, 51);
    EXPECT_EQ(caret->firstColumn, 13);
    EXPECT_EQ(caret->style.colors, (TextColors{.text = edit.backgroundColor, .background = 0xffc8c8c8u}));
}

TEST(OldEditBitmapIntegrationTest, PixelCaretInvertsTheRasterizedGlyphColumn)
{
    using Client::App::PixelSize;
    using Client::Core::Log;
    using Client::Core::LogLevel;
    using Client::Render::SpriteBatch;
    using Client::Render::TextureSlot;
    using Client::Render::Text::TextCache;
    using Client::Scenes::TextCacheDrawer;
    using Client::Tests::FakeTextureStore;
    using Client::Tests::RecordingSink;

    SpriteBatch sprites;
    sprites.Begin(PixelSize{1024, 768});
    PixelCaretRasterizer rasterizer;
    FakeTextureStore textures;
    RecordingSink sink;
    Log log(sink, LogLevel::Debug);
    TextCache cache(rasterizer, textures);
    TextCacheDrawer text(cache, sprites, log);

    OldEdit edit;
    edit.base.x = 149;
    edit.base.y = 40;
    edit.base.width = 80;
    edit.base.height = 14;
    edit.text.SetText("abcd");
    edit.text.Select(0, 2);
    edit.textColor = 0xff123456u;
    edit.backgroundColor = 0x00000000u;
    FontKind dcFont = FontKind::Normal;
    TextColors textColors{};
    const OldWindowDrawContext context{.sprites = sprites, .text = text, .dcFont = dcFont, .textColors = textColors};

    Client::UI::OldDraw::DrawTheOldEdit(context, edit, true);

    const TextureSlot selectedSlot{static_cast<std::uint16_t>(Client::Render::FirstTextTextureSlot.value)};
    const auto selected = textures.Find(selectedSlot);
    ASSERT_TRUE(selected.has_value());
    const auto& selectedPixels = selected->texture.pixels;
    EXPECT_EQ(selectedPixels[0], 0x56);
    EXPECT_EQ(selectedPixels[1], 0x34);
    EXPECT_EQ(selectedPixels[2], 0x12);
    EXPECT_EQ(selectedPixels[3], 0xff);
    const std::size_t selectedBackground = 256u * 4u;
    EXPECT_EQ(selectedPixels[selectedBackground], 0x96);
    EXPECT_EQ(selectedPixels[selectedBackground + 1], 0x96);
    EXPECT_EQ(selectedPixels[selectedBackground + 2], 0x96);
    EXPECT_EQ(selectedPixels[selectedBackground + 3], 0xff);
    EXPECT_EQ(edit.textColor, 0xff123456u);

    const TextureSlot caretSlot{static_cast<std::uint16_t>(Client::Render::FirstTextTextureSlot.value + 3)};
    const auto caret = textures.Find(caretSlot);
    ASSERT_TRUE(caret.has_value());
    const auto& caretPixels = caret->texture.pixels;
    std::size_t transparentRows = 0;
    std::size_t grayRows = 0;
    for (int row = 0; row < 13; ++row)
    {
        const std::size_t pixel = static_cast<std::size_t>(row * 256 * 4);
        const bool glyphPixel = row >= 3 && row < 6;
        if (glyphPixel)
        {
            EXPECT_EQ(caretPixels[pixel], 0);
            EXPECT_EQ(caretPixels[pixel + 1], 0);
            EXPECT_EQ(caretPixels[pixel + 2], 0);
            EXPECT_EQ(caretPixels[pixel + 3], 0);
            ++transparentRows;
        }
        else
        {
            EXPECT_EQ(caretPixels[pixel], 0xc8);
            EXPECT_EQ(caretPixels[pixel + 1], 0xc8);
            EXPECT_EQ(caretPixels[pixel + 2], 0xc8);
            EXPECT_EQ(caretPixels[pixel + 3], 0xff);
            ++grayRows;
        }
    }
    EXPECT_EQ(transparentRows, 3u);
    EXPECT_EQ(grayRows, 10u);

    std::vector<const Client::Render::DrawCommand*> caretCommands;
    for (const Client::Render::DrawCommand& command : sprites.Commands())
    {
        if (command.texture == caretSlot)
            caretCommands.push_back(&command);
    }
    ASSERT_EQ(caretCommands.size(), 1u);
    const Client::Render::DrawCommand& caretCommand = *caretCommands.front();
    ASSERT_EQ(caretCommand.vertexCount, 6u);
    const auto vertices = sprites.Vertices();
    const Client::Render::SpriteVertex& topLeft = vertices[caretCommand.firstVertex];
    const Client::Render::SpriteVertex& bottomRight = vertices[caretCommand.firstVertex + 2];
    EXPECT_FLOAT_EQ(topLeft.x, 254.0f);
    EXPECT_FLOAT_EQ(bottomRight.x, 255.0f);
    EXPECT_FLOAT_EQ(768.0f - topLeft.y, 64.0f);
    EXPECT_FLOAT_EQ(768.0f - bottomRight.y, 77.0f);

    for (const Client::Render::DrawCommand& command : sprites.Commands())
    {
        if (!command.texture)
            continue;
        for (std::size_t first = command.firstVertex; first < command.firstVertex + command.vertexCount; first += 6)
        {
            const float left = vertices[first].x;
            const float right = vertices[first + 2].x;
            if (left <= 254.0f && right > 254.0f)
            {
                EXPECT_EQ(command.texture, std::optional<TextureSlot>{caretSlot});
            }
        }
    }
    EXPECT_TRUE(sink.lines.empty());
}

TEST_F(FriendWindowsDrawTest, TheEditDrawsTextAtItsPixelMarginAndCaretAtTheByteCursor)
{
    OldEdit edit;
    edit.base.x = 20;
    edit.base.y = 40;
    edit.base.width = 80;
    edit.base.height = 14;
    edit.text.SetText("abcd");
    edit.text.SetCursor(2);

    Client::UI::OldDraw::DrawTheOldEdit(Context(), edit, true);

    ASSERT_EQ(m_text.texts.size(), 3u);
    EXPECT_EQ(m_text.texts[0].text, "abcd");
    EXPECT_EQ(m_text.texts[1].text, "abcd");
    EXPECT_EQ(m_text.texts[2].text, "abcd");
    EXPECT_TRUE(m_text.texts[0].pixelPosition);
    EXPECT_TRUE(m_text.texts[1].pixelPosition);
    EXPECT_TRUE(m_text.texts[2].pixelPosition);
    EXPECT_EQ(m_text.texts[0].x, 23);
    EXPECT_EQ(m_text.texts[0].y, 40);
    EXPECT_EQ(m_text.texts[0].firstColumn, 0);
    EXPECT_EQ(m_text.texts[0].columns, 13);
    EXPECT_EQ(m_text.texts[1].x, 37);
    EXPECT_EQ(m_text.texts[1].firstColumn, 14);
    EXPECT_EQ(m_text.texts[1].columns, 10);
    EXPECT_EQ(m_text.texts[2].x, 36);
    EXPECT_EQ(m_text.texts[2].firstColumn, 13);
    EXPECT_EQ(m_text.texts[2].columns, 1);
    EXPECT_EQ(m_text.texts[2].style.colors, (TextColors{.text = Black, .background = 0xffc8c8c8u}));
    ASSERT_EQ(m_sprites.Commands().size(), 2u);
    ASSERT_EQ(m_sprites.Vertices().size(), 24u);
    ExpectBox(QuadOf(1, 2), 36.0f, 40.0f, 37.0f, 53.0f);
}

TEST_F(FriendWindowsDrawTest, TheEditClipsTheCaretAtTheEndOfText)
{
    OldEdit edit;
    edit.base.x = 20;
    edit.base.y = 40;
    edit.base.width = 80;
    edit.base.height = 14;
    edit.text.SetText("abcd");

    Client::UI::OldDraw::DrawTheOldEdit(Context(), edit, true);

    ASSERT_EQ(m_text.texts.size(), 1u);
    EXPECT_EQ(m_sprites.Vertices().size(), 12u);
}

TEST_F(FriendWindowsDrawTest, TheEditClipsTheCaretForEmptyText)
{
    OldEdit edit;
    edit.base.x = 20;
    edit.base.y = 40;
    edit.base.width = 80;
    edit.base.height = 14;

    Client::UI::OldDraw::DrawTheOldEdit(Context(), edit, true);

    EXPECT_TRUE(m_text.texts.empty());
    EXPECT_EQ(m_sprites.Vertices().size(), 6u);
}

TEST_F(FriendWindowsDrawTest, TheEditDrawsSelectionAndCaretFromFullTextColumns)
{
    OldEdit edit;
    edit.base.x = 20;
    edit.base.y = 40;
    edit.base.width = 80;
    edit.base.height = 14;
    edit.text.SetText("abcd");
    edit.text.Select(1, 3);

    Client::UI::OldDraw::DrawTheOldEdit(Context(), edit, true);

    ASSERT_EQ(m_text.texts.size(), 5u);
    EXPECT_EQ(m_text.texts[0].text, "abcd");
    EXPECT_EQ(m_text.texts[0].x, 23);
    EXPECT_EQ(m_text.texts[0].firstColumn, 0);
    EXPECT_EQ(m_text.texts[0].columns, 6);
    EXPECT_EQ(m_text.texts[0].style.colors, (TextColors{.text = White, .background = 0}));
    EXPECT_EQ(m_text.texts[1].text, "abcd");
    EXPECT_EQ(m_text.texts[1].x, 29);
    EXPECT_EQ(m_text.texts[1].firstColumn, 6);
    EXPECT_EQ(m_text.texts[1].columns, 12);
    EXPECT_EQ(m_text.texts[1].style.colors, (TextColors{.text = White, .background = 0xff969696u}));
    EXPECT_EQ(m_text.texts[2].text, "abcd");
    EXPECT_EQ(m_text.texts[2].x, 41);
    EXPECT_EQ(m_text.texts[2].firstColumn, 18);
    EXPECT_EQ(m_text.texts[2].columns, 1);
    EXPECT_EQ(m_text.texts[2].style.colors, (TextColors{.text = White, .background = 0}));
    EXPECT_EQ(m_text.texts[3].x, 43);
    EXPECT_EQ(m_text.texts[3].firstColumn, 20);
    EXPECT_EQ(m_text.texts[3].columns, 4);
    EXPECT_EQ(m_text.texts[3].style.colors, (TextColors{.text = White, .background = 0}));
    EXPECT_TRUE(m_text.texts[0].pixelPosition);
    EXPECT_TRUE(m_text.texts[1].pixelPosition);
    EXPECT_TRUE(m_text.texts[2].pixelPosition);
    EXPECT_EQ(m_text.texts[4].text, "abcd");
    EXPECT_EQ(m_text.texts[4].x, 42);
    EXPECT_EQ(m_text.texts[4].firstColumn, 19);
    EXPECT_EQ(m_text.texts[4].columns, 1);
    EXPECT_EQ(m_text.texts[4].style.colors, (TextColors{.text = Black, .background = 0xffc8c8c8u}));
    EXPECT_TRUE(m_text.texts[4].pixelPosition);
    ASSERT_EQ(m_sprites.Vertices().size(), 36u);
    ExpectBox(QuadOf(1, 4), 42.0f, 40.0f, 43.0f, 53.0f);
}

TEST_F(FriendWindowsDrawTest, TheEditSelectionUsesTheEditTextColor)
{
    OldEdit edit;
    edit.base.x = 20;
    edit.base.y = 40;
    edit.base.width = 80;
    edit.base.height = 14;
    edit.textColor = 0xff123456u;
    edit.text.SetText("abcd");
    edit.text.Select(1, 3);

    Client::UI::OldDraw::DrawTheOldEdit(Context(), edit, true);

    ASSERT_EQ(m_text.texts.size(), 5u);
    EXPECT_EQ(m_text.texts[0].style.colors.text, 0xff123456u);
    EXPECT_EQ(m_text.texts[1].style.colors.text, 0xff123456u);
    EXPECT_EQ(m_text.texts[1].style.colors.background, 0xff969696u);
    EXPECT_EQ(m_text.texts[2].style.colors.text, 0xff123456u);
    EXPECT_EQ(m_text.texts[3].style.colors.text, 0xff123456u);
    EXPECT_EQ(m_text.texts[4].style.colors.text, edit.backgroundColor);
}

TEST_F(FriendWindowsDrawTest, TheEditUsesTheActiveCaretForReverseSelection)
{
    OldEdit edit;
    edit.base.x = 20;
    edit.base.y = 40;
    edit.base.width = 80;
    edit.base.height = 14;
    edit.text.SetText("abcd");
    edit.text.Select(3, 1);

    Client::UI::OldDraw::DrawTheOldEdit(Context(), edit, true);

    ASSERT_EQ(m_text.texts.size(), 5u);
    EXPECT_EQ(m_text.texts[4].x, 30);
    EXPECT_EQ(m_text.texts[4].firstColumn, 7);
    EXPECT_EQ(m_text.texts[4].columns, 1);
    EXPECT_EQ(m_text.texts[4].style.colors, (TextColors{.text = Black, .background = 0xffc8c8c8u}));
    ExpectBox(QuadOf(1, 4), 30.0f, 40.0f, 31.0f, 53.0f);
}

TEST_F(FriendWindowsDrawTest, TheEditHidesSelectionWhenUnfocusedButKeepsItInTheModel)
{
    OldEdit edit;
    edit.base.x = 20;
    edit.base.y = 40;
    edit.base.width = 80;
    edit.base.height = 14;
    edit.text.SetText("abcd");
    edit.text.Select(1, 3);

    Client::UI::OldDraw::DrawTheOldEdit(Context(), edit, false);

    ASSERT_EQ(m_text.texts.size(), 1u);
    EXPECT_TRUE(edit.text.HasSelection());
    EXPECT_EQ(m_text.texts[0].text, "abcd");
    EXPECT_EQ(m_text.texts[0].style.colors, (TextColors{.text = White, .background = 0}));
    EXPECT_EQ(m_sprites.Vertices().size(), 12u);
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
    edit.text.SetCursor(1);
    const auto quads = [this] { return m_sprites.Vertices().size() / 6; };

    edit.draws = 11;
    Client::UI::OldDraw::DrawTheOldEdit(Context(), edit, true);
    // El bitmap, los dos tramos de texto y la columna de caret.
    EXPECT_EQ(quads(), 4u);
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

// 0x46854d: el WM_GETTEXT de GetWindowTextA pasa por la subclase (0x467b10), que con una flecha abajo pone +0x80 en 0
// después de que el dibujo eligió el cursor; el INC lo deja en 1, y el cursor queda prendido mientras siga abajo.
// Oculto, el EDIT sale antes (IsWindowVisible) y no cuenta.
TEST_F(FriendWindowsDrawTest, AHeldArrowKeepsTheEditCaretOn)
{
    OldEdit edit;
    edit.base.x = 20;
    edit.base.y = 40;
    edit.base.width = 80;
    edit.base.height = 14;
    edit.text.Type("ab");
    const auto quads = [this] { return m_sprites.Vertices().size() / 6; };
    OldWindowDrawContext held = Context();
    held.arrowHeld = true;

    edit.draws = 13;
    Client::UI::OldDraw::DrawTheOldEdit(held, edit, true);
    // El bitmap y el texto: el cursor se eligió con 13.
    EXPECT_EQ(quads(), 2u);
    EXPECT_EQ(edit.draws, 1);

    m_sprites.Begin(Screen640);
    Client::UI::OldDraw::DrawTheOldEdit(held, edit, true);
    EXPECT_EQ(quads(), 3u);
    EXPECT_EQ(edit.draws, 1);

    // Sin el foco el WM_GETTEXT llega igual.
    edit.draws = 20;
    Client::UI::OldDraw::DrawTheOldEdit(held, edit, false);
    EXPECT_EQ(edit.draws, 1);

    edit.draws = 20;
    edit.base.state = Client::UI::HiddenWindow;
    Client::UI::OldDraw::DrawTheOldEdit(held, edit, true);
    EXPECT_EQ(edit.draws, 20);
}

// 0x468584..0x4685ec, el EDIT con ES_PASSWORD (el número del diálogo 0x7d, CL9 hito 8): el texto es un '*' por byte,
// el bitmap mide los '*' en la negrita y el DC queda con la normal; el cursor, al final de los '*'.
TEST_F(FriendWindowsDrawTest, AMaskedEditDrawsStarsAndLeavesTheNormalFont)
{
    OldEdit edit = Client::UI::NumberFieldEdit();
    edit.base.state = 0;
    edit.base.x = 0xf3;
    edit.base.y = 0x62;
    edit.base.width = 0x46;
    edit.base.height = 0xe;
    edit.text.Type("4321");
    m_dcFont = FontKind::Bold;

    Client::UI::OldDraw::DrawTheOldEdit(Context(), edit, true);

    EXPECT_EQ(m_dcFont, FontKind::Normal);
    ASSERT_EQ(m_text.texts.size(), 1u);
    EXPECT_EQ(m_text.texts[0].text, "****");
    EXPECT_EQ(m_text.texts[0].x, 0xf3);
    EXPECT_EQ(m_text.texts[0].style.font, FontKind::Normal);
    EXPECT_EQ(edit.text.Value(), "4321");
    // El bitmap (ftol(1,5 + 1,5 + 24) = 27 de ancho), el texto y el cursor en x + 24 + 1.
    const std::span<const SpriteVertex> vertices = m_sprites.Vertices();
    ASSERT_EQ(vertices.size(), 3u * 6u);
    EXPECT_FLOAT_EQ(vertices[0].x, 243.0f);
    const float right = std::max({vertices[0].x, vertices[1].x, vertices[2].x, vertices[3].x});
    EXPECT_FLOAT_EQ(right, 243.0f + 27.0f);
    EXPECT_FLOAT_EQ(vertices[12].x, 243.0f + 25.0f);
    EXPECT_EQ(edit.draws, 1);
}
} // namespace
