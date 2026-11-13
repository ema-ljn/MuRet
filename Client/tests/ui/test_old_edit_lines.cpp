#include "UI/OldEditLines.h"

#include "App/WindowState.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Render/Text/TextStyle.h"
#include "Render/TextureSlot.h"
#include "UI/ChatInput.h"
#include "UI/OldControls.h"
#include "UI/OldWindowDraw.h"
#include "UI/OldWindowDrawParts.h"
#include "UI/OldWindowSteps.h"
#include "UI/OldWindows.h"
#include "support/RecordingTextDrawer.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// El EDIT de varios renglones (CL9 hito 10; 06 § HUD y ventanas, "Las cartas y el chat con un amigo"): +0xc4 y +0xd0
// de 0x467fe0, los renglones del EDIT de Windows (*inferido*, P-C217), su dibujo (0x468320 con +0xc4 y la barra
// 0x468970), su paso (0x468d50) y Enter (0x467b10). Los textos son inventados.
namespace
{
using Client::App::PixelSize;
using Client::Render::BlendMode;
using Client::Render::DrawCommand;
using Client::Render::SpriteBatch;
using Client::Render::SpriteVertex;
using Client::Render::TextureSlot;
using Client::Render::Text::FontKind;
using Client::Render::Text::ITextMeasure;
using Client::Render::Text::TextExtent;
using Client::Tests::RecordingTextDrawer;
using Client::Tests::TextMarkerSlot;
using Client::UI::ChatFocus;
using Client::UI::OldEdit;
using Client::UI::OldWindowDrawContext;
using Client::UI::OldWindowDrawView;
using Client::UI::OldWindowGlobals;
using Client::UI::OldWindowManager;
using Client::UI::OldWindowMeasure;
using Client::UI::OldWindowStepView;

constexpr PixelSize Screen640{640, 480};
constexpr PixelSize Screen800{800, 600};
constexpr TextureSlot ScrollbarSlot{0x649};

// 6 columnas por letra y 13 filas, como RecordingTextDrawer.
class LetterMeasure final : public ITextMeasure
{
public:
    [[nodiscard]] TextExtent Measure(FontKind /*font*/, std::string_view text) const override
    {
        return {static_cast<int>(text.size()) * RecordingTextDrawer::LetterWidth, RecordingTextDrawer::Height};
    }
};

// El cuerpo de la carta de 0x497f50: 0xee × 0x87.
OldEdit TheBody(PixelSize screen, std::size_t limit = 1000)
{
    OldEdit edit;
    edit.base.id = 7;
    edit.base.x = 60;
    edit.base.y = 100;
    edit.base.width = 0xee;
    edit.base.height = 0x87;
    edit.text = Client::UI::TextField(limit, false, Client::UI::TextField::Charset::Latin1);
    const LetterMeasure measure;
    Client::UI::MakeTheEditMultiline(edit, measure, FontKind::Normal, screen);
    return edit;
}

std::vector<std::string> LinesOf(const OldEdit& edit)
{
    const LetterMeasure measure;
    std::vector<std::string> lines;
    for (const std::string_view line : Client::UI::EditLines(edit, measure))
        lines.emplace_back(line);
    return lines;
}

// 0x467fe0: +0xd0 es ftol(0x87 · ry / 13); la ventana, ftol((0xee + ftol(−5 · rx)) · rx), y el área del texto 16
// menos.
TEST(OldEditLinesTest, TheBodyKnowsTheLinesThatFitAndItsTextWidth)
{
    const OldEdit at640 = TheBody(Screen640);
    EXPECT_EQ(at640.multiline, 1);
    EXPECT_EQ(at640.visibleLines, 10);
    EXPECT_EQ(at640.textWidth, 0xee - 5 - 16);
    // 800 × 600: rx = ry = 1,25; ftol(−6,25) = −6, ftol(232 · 1,25) = 290 y ftol(168,75 / 13) = 12.
    const OldEdit at800 = TheBody(Screen800);
    EXPECT_EQ(at800.visibleLines, 12);
    EXPECT_EQ(at800.textWidth, 290 - 16);
}

// Los renglones: "\r\n" corta; lo que no entra en 217 píxeles (36 letras) se corta en el último espacio que entra, los
// espacios de más quedan en el renglón, y una palabra más larga que el renglón se corta en la última letra que entra.
// Un EDIT de una línea es un renglón; uno vacío o que termina en "\r\n", con el renglón vacío del final.
TEST(OldEditLinesTest, LinesBreakAtCrLfAndWrapAtTheLastSpace)
{
    OldEdit edit = TheBody(Screen640);
    EXPECT_EQ(LinesOf(edit), std::vector<std::string>{""});
    edit.text.SetText("hola tero\r\n\r\nchau");
    EXPECT_EQ(LinesOf(edit), (std::vector<std::string>{"hola tero", "", "chau"}));
    edit.text.SetText(std::string(30, 'a') + " " + std::string(10, 'b') + "\r\n");
    EXPECT_EQ(LinesOf(edit), (std::vector<std::string>{std::string(30, 'a') + " ", std::string(10, 'b'), ""}));
    edit.text.SetText(std::string(36, 'c') + "   d");
    EXPECT_EQ(LinesOf(edit), (std::vector<std::string>{std::string(36, 'c') + "   ", "d"}));
    edit.text.SetText(std::string(80, 'e'));
    EXPECT_EQ(LinesOf(edit),
              (std::vector<std::string>{std::string(36, 'e'), std::string(36, 'e'), std::string(8, 'e')}));

    OldEdit single;
    single.text.Type("uno dos");
    EXPECT_EQ(LinesOf(single), std::vector<std::string>{"uno dos"});
}

// EM_SCROLL y EM_LINESCROLL: entre 0 y renglones − +0xd0; la página son los que entran. ES_AUTOVSCROLL pone el último
// renglón a la vista después de un cambio del texto. En un EDIT de una línea, nada.
TEST(OldEditLinesTest, TheScrollStaysBetweenTheFirstAndTheLastPage)
{
    OldEdit edit = TheBody(Screen640);
    Client::UI::ScrollTheEdit(edit, 25, Client::UI::EditLineDown);
    EXPECT_EQ(edit.firstLine, 1);
    Client::UI::ScrollTheEdit(edit, 25, Client::UI::EditPageDown);
    EXPECT_EQ(edit.firstLine, 11);
    Client::UI::ScrollTheEdit(edit, 25, Client::UI::EditPageDown);
    EXPECT_EQ(edit.firstLine, 15);
    Client::UI::ScrollTheEdit(edit, 25, Client::UI::EditPageUp);
    EXPECT_EQ(edit.firstLine, 5);
    Client::UI::ScrollTheEditLines(edit, 25, -10000);
    EXPECT_EQ(edit.firstLine, 0);
    Client::UI::ScrollTheEdit(edit, 6, Client::UI::EditLineDown);
    EXPECT_EQ(edit.firstLine, 0);

    for (int line = 0; line < 14; ++line)
    {
        Client::UI::TypeInTheEdit(edit, "x");
        Client::UI::BreakTheEditLine(edit);
    }
    Client::UI::FollowTheEditCaret(edit, 15);
    EXPECT_EQ(edit.firstLine, 5);
    EXPECT_FALSE(edit.followCaret);
    Client::UI::ScrollTheEditLines(edit, 15, -10000);
    Client::UI::FollowTheEditCaret(edit, 15);
    EXPECT_EQ(edit.firstLine, 0);

    OldEdit single;
    Client::UI::ScrollTheEdit(single, 1, Client::UI::EditLineDown);
    EXPECT_EQ(single.firstLine, 0);
}

// 0x467b10: cada WM_CHAR pone +0x80 en 0; Enter es "\r\n" en el de varios renglones si entra entero, y el retroceso lo
// borra entero; en el de una línea, Enter no escribe.
TEST(OldEditLinesTest, EnterBreaksTheLineOnlyInTheMultilineEdit)
{
    OldEdit edit = TheBody(Screen640, 5);
    edit.draws = 9;
    Client::UI::TypeInTheEdit(edit, "abc");
    EXPECT_EQ(edit.draws, 0);
    edit.draws = 9;
    Client::UI::BreakTheEditLine(edit);
    EXPECT_EQ(edit.draws, 0);
    EXPECT_EQ(edit.text.Value(), "abc\r\n");
    Client::UI::BreakTheEditLine(edit);
    EXPECT_EQ(edit.text.Value(), "abc\r\n");
    Client::UI::BackspaceInTheEdit(edit);
    EXPECT_EQ(edit.text.Value(), "abc");
    Client::UI::BackspaceInTheEdit(edit);
    EXPECT_EQ(edit.text.Value(), "ab");

    OldEdit single;
    single.text.Type("ab");
    Client::UI::BreakTheEditLine(single);
    EXPECT_EQ(single.text.Value(), "ab");
}

class OldEditTest : public ::testing::Test
{
protected:
    OldEditTest() : m_text(m_sprites) {}

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

    OldWindowStepView StepView(std::int32_t x, std::int32_t y) const
    {
        return OldWindowStepView{.mouseX = x, .mouseY = y, .measure = &m_measure};
    }

    void Step(OldEdit& edit, std::int32_t x, std::int32_t y)
    {
        Client::UI::Steps::StepTheEditMouse(m_manager, edit, Globals(), StepView(x, y));
    }

    OldWindowDrawContext Context()
    {
        return OldWindowDrawContext{.sprites = m_sprites, .text = m_text, .dcFont = m_dcFont, .textColors = m_colors};
    }

    void Draw(OldEdit& edit, PixelSize screen, bool focused, std::int32_t x = 0, std::int32_t y = 0)
    {
        m_sprites.Begin(screen);
        m_text.texts.clear();
        Client::UI::OldDraw::DrawTheOldEdit(Context(), edit, focused,
                                            OldWindowDrawView{.mouseX = x, .mouseY = y, .leftClick = m_leftClick});
    }

    const SpriteVertex& VertexOf(std::size_t command, std::size_t quad = 0, std::size_t corner = 0) const
    {
        return m_sprites.Vertices()[m_sprites.Commands()[command].firstVertex + quad * 6 + corner];
    }

    // El quad en 640 × 480 (las x y las y de los vértices, con la y hacia abajo, pasadas por la pantalla).
    void ExpectQuadAt(std::size_t command, std::size_t quad, float left, float top, float right, float bottom) const
    {
        const PixelSize screen = m_sprites.Screen();
        const float sx = static_cast<float>(screen.width) / 640.0f;
        const float sy = static_cast<float>(screen.height) / 480.0f;
        const SpriteVertex& first = VertexOf(command, quad, 0);
        const SpriteVertex& last = VertexOf(command, quad, 2);
        EXPECT_NEAR(first.x / sx, left, 1e-3f);
        EXPECT_NEAR((static_cast<float>(screen.height) - first.y) / sy, top, 1e-3f);
        EXPECT_NEAR(last.x / sx, right, 1e-3f);
        EXPECT_NEAR((static_cast<float>(screen.height) - last.y) / sy, bottom, 1e-3f);
    }

    LetterMeasure m_letters;
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
    std::int32_t m_mouseUpdateTime = 4;
    std::int32_t m_mouseUpdateTimeMax = 9;
    FontKind m_dcFont = FontKind::Normal;
    Client::Render::Text::TextColors m_colors{};
};

// 0x468320 con +0xc4 en 1 y 3 renglones (menos que +0xd0): el bitmap de todo el ancho y 3 · 13 de alto, los renglones
// de 13 en 13 y el cursor al final del último; después la barra (0x468970): las flechas en (x + w − 15, y − 4) y
// (x + w − 15, y + h − 9), los filos con la mezcla normal y el cursor, sin mezcla, de toda la pista (+0xd0 / 3 > 1).
TEST_F(OldEditTest, AMultilineEditDrawsItsLinesItsCaretAndItsBar)
{
    OldEdit edit = TheBody(Screen640);
    edit.text.SetText("uno\r\ndos\r\ntres");
    edit.draws = 0;

    Draw(edit, Screen640, true);

    const std::span<const DrawCommand> commands = m_sprites.Commands();
    ASSERT_EQ(commands.size(), 6u);
    // El fondo (+0xa0, negro) de 238 × 39 en (60, 100).
    EXPECT_FALSE(commands[0].texture.has_value());
    ExpectQuadAt(0, 0, 60.0f, 100.0f, 298.0f, 139.0f);
    EXPECT_EQ(commands[1].texture, TextMarkerSlot);
    ASSERT_EQ(m_text.texts.size(), 3u);
    EXPECT_EQ(m_text.texts[0].text, "uno");
    EXPECT_EQ(m_text.texts[1].y, 113);
    EXPECT_EQ(m_text.texts[2].text, "tres");
    EXPECT_EQ(m_text.texts[2].y, 126);
    // El cursor en la columna 4 · 6 + 1 del tercer renglón.
    EXPECT_FALSE(commands[2].texture.has_value());
    ExpectQuadAt(2, 0, 85.0f, 126.0f, 86.0f, 139.0f);
    // Las flechas.
    EXPECT_EQ(commands[3].texture, ScrollbarSlot);
    EXPECT_EQ(commands[3].vertexCount, 12u);
    ExpectQuadAt(3, 0, 283.0f, 96.0f, 296.0f, 109.0f);
    EXPECT_FLOAT_EQ(VertexOf(3, 0).v, 0.09375f);
    ExpectQuadAt(3, 1, 283.0f, 226.0f, 296.0f, 239.0f);
    EXPECT_FLOAT_EQ(VertexOf(3, 1).v, 0.5f);
    // Los filos de la pista de y + 9 a y + h − 9, en x + w − 15 y x + w − 3.
    EXPECT_EQ(commands[4].blend, BlendMode::Alpha);
    ExpectQuadAt(4, 0, 283.0f, 109.0f, 284.0f, 226.0f);
    ExpectQuadAt(4, 1, 295.0f, 109.0f, 296.0f, 226.0f);
    EXPECT_EQ(VertexOf(4).color, *Client::UI::OldWindowPalette(2, 1.0f));
    // El cursor de la barra, de 11 de ancho en x + w − 14, con sus puntas.
    EXPECT_EQ(commands[5].blend, BlendMode::Opaque);
    EXPECT_EQ(commands[5].vertexCount, 18u);
    ExpectQuadAt(5, 0, 284.0f, 109.0f, 295.0f, 226.0f);
    ExpectQuadAt(5, 1, 284.0f, 109.0f, 295.0f, 110.0f);
    ExpectQuadAt(5, 2, 284.0f, 225.0f, 295.0f, 226.0f);
    EXPECT_FLOAT_EQ(edit.barWidth, 13.0f);
    EXPECT_FLOAT_EQ(edit.thumbHeight, 117.0f);
    EXPECT_EQ(edit.draws, 1);
}

// Con 20 renglones y el tercero arriba: el bitmap de ftol(h · ry), los 10 renglones que entran desde el tercero, sin el
// cursor (el último no está a la vista), y el cursor de la barra de 117 · 10 / 20 en 109 + 117 · 3 / 20. La flecha de
// abajo, con el clic sobre ella, dada vuelta. En 800 × 600, lo mismo en la escala.
TEST_F(OldEditTest, AScrolledEditDrawsTheLinesInViewAndItsThumb)
{
    // El bitmap de ftol(238 · rx) × ftol(135 · ry) píxeles: en 800 × 600, 297 × 168.
    struct Case
    {
        PixelSize screen;
        float right;
        float bottom;
    };
    for (const Case& at : {Case{Screen640, 298.0f, 235.0f}, Case{Screen800, 297.6f, 234.4f}})
    {
        const PixelSize screen = at.screen;
        SCOPED_TRACE(screen.width);
        OldEdit edit = TheBody(screen);
        std::string text;
        for (int line = 0; line < 20; ++line)
            text += "renglon " + std::to_string(line) + (line < 19 ? "\r\n" : "");
        edit.text.SetText(text);
        edit.firstLine = 3;
        m_leftClick = true;

        Draw(edit, screen, true, 290, 230);

        const std::span<const DrawCommand> commands = m_sprites.Commands();
        ASSERT_EQ(commands.size(), 5u);
        ExpectQuadAt(0, 0, 60.0f, 100.0f, at.right, at.bottom);
        const auto shown = static_cast<std::size_t>(edit.visibleLines);
        ASSERT_EQ(m_text.texts.size(), shown);
        EXPECT_EQ(m_text.texts.front().text, "renglon 3");
        EXPECT_EQ(m_text.texts.back().text, "renglon " + std::to_string(2 + shown));
        EXPECT_EQ(commands[2].texture, ScrollbarSlot);
        EXPECT_FLOAT_EQ(VertexOf(2, 1, 0).u, 0.8125f);
        EXPECT_FLOAT_EQ(VertexOf(2, 1, 0).v, 0.5f);
        EXPECT_FLOAT_EQ(edit.thumbHeight, 117.0f * static_cast<float>(edit.visibleLines) / 20.0f);
        EXPECT_FLOAT_EQ(edit.thumbY, 109.0f + 117.0f * 3.0f / 20.0f);
    }
}

// 0x468d50 sobre la flecha de abajo: la primera vez corre un renglón con el 0x19 y empieza la cuenta, que sube hasta 15
// (sin padre) y desde 16 corre en cada paso; sin el clic, las cuentas vuelven a 0. El clic da el foco (EM_SETSEL(−2,
// −1): nada elegido), el mouse a la UI y 0x77f3b10 en 0 y 0x6c8dcc en 6.
TEST_F(OldEditTest, TheDownArrowScrollsWithTheSoundAndRepeats)
{
    OldEdit edit = TheBody(Screen640);
    std::string text;
    for (int line = 0; line < 30; ++line)
        text += "r\r\n";
    edit.text.SetText(text);
    edit.text.SelectAll();
    m_leftClick = true;

    Step(edit, 290, 230);

    EXPECT_EQ(edit.firstLine, 1);
    EXPECT_EQ(m_sound, std::optional<std::int32_t>(0x19));
    EXPECT_EQ(edit.arrowRepeat, 1);
    EXPECT_EQ(m_mouseOnUi, 1);
    EXPECT_EQ(m_focus, ChatFocus::OldWindow);
    EXPECT_EQ(m_manager.focusedEdit, 7);
    EXPECT_EQ(m_focusedList, 7);
    EXPECT_FALSE(edit.text.AllSelected());
    EXPECT_EQ(m_mouseUpdateTime, 0);
    EXPECT_EQ(m_mouseUpdateTimeMax, 6);
    m_sound.reset();
    for (int step = 0; step < 15; ++step)
        Step(edit, 290, 230);
    EXPECT_EQ(edit.firstLine, 1);
    EXPECT_EQ(edit.arrowRepeat, 16);
    Step(edit, 290, 230);
    EXPECT_EQ(edit.firstLine, 2);
    EXPECT_FALSE(m_sound.has_value());
    m_leftClick = false;
    Step(edit, 290, 230);
    EXPECT_EQ(edit.arrowRepeat, 0);
}

// Con la barra dibujada (+0xd4..+0xe4): el clic en la pista, debajo del cursor, pasa una página, sin sonido; el clic
// sobre el cursor lo agarra (el estado en 2, el mouse del EDIT y +0xe8) y el mouse lo lleva: EM_LINESCROLL a 0 y a
// ftol(31 · ((y − 109 + 0,5) / 117)); sin el clic, lo suelta.
TEST_F(OldEditTest, TheThumbIsDraggedAndTheTrackPages)
{
    OldEdit edit = TheBody(Screen640);
    std::string text;
    for (int line = 0; line < 30; ++line)
        text += "r\r\n";
    edit.text.SetText(text);
    Draw(edit, Screen640, false);
    ASSERT_FLOAT_EQ(edit.thumbY, 109.0f);
    ASSERT_FLOAT_EQ(edit.thumbHeight, 117.0f * 10.0f / 31.0f);
    m_leftClick = true;

    Step(edit, 290, 150);
    EXPECT_EQ(edit.firstLine, 10);
    EXPECT_EQ(edit.trackRepeat, 1);
    EXPECT_FALSE(m_sound.has_value());

    Draw(edit, Screen640, false);
    edit.trackRepeat = 0;
    const std::int32_t grabY = static_cast<std::int32_t>(edit.thumbY) + 2;
    Step(edit, 290, grabY);
    EXPECT_EQ(edit.base.state, 2);
    EXPECT_EQ(m_capture, 7);
    EXPECT_FLOAT_EQ(edit.dragOffset, static_cast<float>(grabY) - edit.thumbY);

    Step(edit, 0, grabY + 30);
    const float thumb = static_cast<float>(grabY + 30) - edit.dragOffset;
    EXPECT_FLOAT_EQ(edit.thumbY, thumb);
    EXPECT_EQ(edit.firstLine, static_cast<std::int32_t>(31.0 * ((static_cast<double>(thumb) - 109.0 + 0.5) / 117.0)));
    m_leftClick = false;
    Step(edit, 0, grabY + 30);
    EXPECT_EQ(edit.base.state, 0);
    EXPECT_EQ(m_capture, 0);
}

// En un EDIT de una línea (el del chat), el clic en el borde derecho también pasa por las flechas: EM_SCROLL no hace
// nada pero el 0x19 suena (Q-C1009); el cursor y la pista, sin 0x468970, son de 0 × 0.
TEST_F(OldEditTest, TheRightEdgeOfASingleLineEditSounds)
{
    OldEdit edit;
    edit.base.id = 3;
    edit.base.x = 20;
    edit.base.y = 40;
    edit.base.width = 0xb4;
    edit.base.height = 0xe;
    m_leftClick = true;

    Step(edit, 20 + 0xb4 - 10, 42);

    EXPECT_EQ(m_sound, std::optional<std::int32_t>(0x19));
    EXPECT_EQ(edit.firstLine, 0);
    EXPECT_EQ(edit.base.state, 0);
    EXPECT_EQ(m_manager.focusedEdit, 3);
}
} // namespace
