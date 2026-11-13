#include "Scenes/GameDialogDrawing.h"

#include "App/WindowState.h"
#include "Core/Expected.h"
#include "Data/Text/DialogTable.h"
#include "Data/Text/TextTable.h"
#include "Render/Models/Cloth.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/TextStyle.h"
#include "Scenes/EventMapObject.h"
#include "Scenes/GameHud.h"
#include "Scenes/GameMovementState.h"
#include "Scenes/NpcWindowState.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/Dialogs.h"
#include "UI/GameDialogs.h"
#include "UI/GameUiState.h"
#include "UI/Widget.h"

#include "support/RecordingTextDrawer.h"
#include "support/SceneDataFiles.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string_view>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

// Los diálogos del juego en el dibujo (0x62db60, CL8 hito 6): el menú de Esc, las opciones con la barra del volumen y
// el brillo, las cajas con sus botones, sus renglones y las flechas del 0x8c, el OK genérico y los cortes. En 640 ×
// 480, con textos inventados y el mundo en Lorencia (0) y en Devias (2).
namespace
{
using Client::Render::BlendMode;
using Client::Render::Color;
using Client::Render::SpriteBatch;
using Client::Scenes::GameHudContext;
using Client::Scenes::WorldState;

constexpr Client::App::PixelSize Screen640{640, 480};
constexpr std::int32_t Lorencia = 0;
constexpr std::int32_t Devias = 2;
constexpr std::int32_t Noria = 3;
// RecordingTextDrawer: 6 columnas por letra.
constexpr int LetterWidth = Client::Tests::RecordingTextDrawer::LetterWidth;

// Un quad del lote: la textura, la mezcla, el color y la esquina de arriba a la izquierda en 640 × 480.
struct DrawnQuad
{
    std::optional<std::uint16_t> slot;
    BlendMode blend = BlendMode::Alpha;
    Color color{};
    float x = 0.0f;
    float y = 0.0f;
};

std::vector<DrawnQuad> QuadsOf(const SpriteBatch& sprites)
{
    std::vector<DrawnQuad> quads;
    const auto vertices = sprites.Vertices();
    for (const Client::Render::DrawCommand& command : sprites.Commands())
    {
        if (command.texture == Client::Tests::TextMarkerSlot)
            continue;
        for (std::size_t quad = 0; quad < command.vertexCount / 6; ++quad)
        {
            const auto& first = vertices[command.firstVertex + quad * 6];
            quads.push_back(
                DrawnQuad{.slot = command.texture ? std::optional<std::uint16_t>{command.texture->value} : std::nullopt,
                          .blend = command.blend,
                          .color = first.color,
                          .x = first.x,
                          .y = 480.0f - first.y});
        }
    }
    return quads;
}

// x = 0x140 − (cx / 2) de un texto centrado en 640 × 480.
int CenteredX(const std::string& text)
{
    return 0x140 - static_cast<int>(text.size()) * LetterWidth / 2;
}

class DrawingDialogs
{
public:
    explicit DrawingDialogs(std::int32_t map)
    {
        world.Runtime().map = map;
        sprites.Begin(Screen640);
    }

    Client::Core::Expected<void, std::string> Draw(bool projecting = false)
    {
        Client::Scenes::WorldRuntime& runtime = world.Runtime();
        GameHudContext context{.world = world,
                               .random = random,
                               .ownRecordDepth = runtime.ownRecordDepth,
                               .cellXDepth = cellDepth,
                               .cellYDepth = cellDepth,
                               .hud = runtime.hud,
                               .crywolfSandOverlay = runtime.crywolf.sandOverlay,
                               .sound = sound,
                               .projecting = projecting,
                               .sprites = sprites,
                               .text = text,
                               .texts = &texts,
                               .mouse = mouse};
        return Client::Scenes::DrawTheGameDialogs(context);
    }

    [[nodiscard]] std::vector<std::string> Texts() const
    {
        std::vector<std::string> drawn;
        for (const Client::Tests::RecordedText& recorded : text.texts)
            drawn.push_back(recorded.text);
        return drawn;
    }

    WorldState world;
    Client::Render::Models::CrtRandom random;
    std::uint8_t cellDepth = 0;
    std::optional<std::int32_t> sound;
    SpriteBatch sprites;
    Client::Tests::RecordingTextDrawer text{sprites};
    Client::UI::Point mouse{};
    Client::Data::Text::TextTable texts = Client::Tests::MakeTextTable({
        {381, "salir"},
        {382, "otro servidor"},
        {383, "otro personaje"},
        {384, "cerrar"},
        {385, "opciones"},
        {386, "ataque"},
        {387, "aviso"},
        {388, "listo"},
        {389, "volumen"},
        {401, "uno"},
        {402, "sin conexion"},
        {405, "primero"},
        {406, "segundo"},
        {409, "fin"},
        {442, "iguales"},
        {445, "distinta"},
        {919, "carteles"},
        {1840, "brillo"},
    });
};

// 0x62db60 sin diálogo: sin mezcla y en blanco, los colores del texto y la fuente, y la mezcla normal al final.
TEST(GameDialogDrawingTest, WithoutADialogItOnlyLeavesTheState)
{
    DrawingDialogs tested(Lorencia);
    tested.world.Runtime().hud.textColors = {.text = 0xff00ff00u, .background = 0x80000000u};
    tested.sprites.SetColor({0.5f, 0.5f, 0.5f, 1.0f});

    ASSERT_TRUE(tested.Draw().HasValue());
    EXPECT_TRUE(tested.sprites.Vertices().empty());
    EXPECT_EQ(tested.sprites.Blend(), BlendMode::Alpha);
    EXPECT_EQ(tested.world.Runtime().hud.textColors.text, 0xffffffffu);
    EXPECT_EQ(tested.world.Runtime().hud.textColors.background, 0u);
    EXPECT_EQ(tested.world.Runtime().hud.dcFont, Client::Render::Text::FontKind::Normal);
}

// 0x62dbcf: los 5 botones, el del mouse otra vez en aditiva con (0,8; 0,6; 0,4), y los textos centrados.
TEST(GameDialogDrawingTest, TheEscapeMenuDrawsItsButtonsAndItsTexts)
{
    for (const std::int32_t map : {Lorencia, Devias})
    {
        DrawingDialogs tested(map);
        tested.world.Runtime().dialogs.current = Client::UI::EscapeMenuDialog;
        tested.mouse = {0x110, 95};

        ASSERT_TRUE(tested.Draw().HasValue());
        const std::vector<DrawnQuad> quads = QuadsOf(tested.sprites);
        ASSERT_EQ(quads.size(), 6u);
        for (const std::size_t at : {0u, 1u, 3u, 4u, 5u})
        {
            EXPECT_EQ(quads[at].slot, 0xf0);
            EXPECT_EQ(quads[at].blend, BlendMode::Opaque);
            EXPECT_FLOAT_EQ(quads[at].x, 260.0f);
        }
        EXPECT_FLOAT_EQ(quads[1].y, 90.0f);
        EXPECT_EQ(quads[2].blend, BlendMode::Additive);
        EXPECT_FLOAT_EQ(quads[2].color.green, 0.6f);
        EXPECT_FLOAT_EQ(quads[2].y, 90.0f);
        EXPECT_FLOAT_EQ(quads[5].y, 180.0f);

        const std::vector<std::string> expected{"salir", "otro servidor", "otro personaje", "opciones", "cerrar"};
        EXPECT_EQ(tested.Texts(), expected);
        const auto& texts = tested.text.texts;
        EXPECT_EQ(texts[3].x, CenteredX("opciones"));
        EXPECT_EQ(texts[3].y, 0x9b);
        EXPECT_EQ(texts[4].y, 0xb9);
        // 0x554180 deja el tamaño del último texto.
        EXPECT_EQ(tested.world.Runtime().hud.arrowsTextWidth, 6 * LetterWidth);
        EXPECT_EQ(tested.sprites.Blend(), BlendMode::Alpha);
    }
}

// 0x62df3f: las filas, las llaves con dos espacios, la barra del volumen con su esquina para el paso y el brillo.
TEST(GameDialogDrawingTest, TheOptionsDrawTheVolumeBarAndTheGlow)
{
    for (const std::int32_t map : {Lorencia, Devias})
    {
        DrawingDialogs tested(map);
        Client::Scenes::WorldRuntime& runtime = tested.world.Runtime();
        runtime.dialogs.current = Client::UI::OptionsDialog;
        runtime.ui.autoAttack = true;
        runtime.chat.noticeSound = false;
        runtime.chat.slidesOn = true;
        runtime.ui.volume = 3;
        runtime.ui.glowDetail = 1;

        ASSERT_TRUE(tested.Draw().HasValue());
        const std::vector<std::string> expected{"opciones",     "ataque  On", "aviso  Off", "volumen",
                                                "carteles  On", "brillo",     "listo"};
        EXPECT_EQ(tested.Texts(), expected);
        // El volumen en x = 0x140 − (42 + 0x36) / 2 y la barra en x + 0x144 + 42.
        const int volumeX = -((7 * LetterWidth + 0x36) / 2);
        EXPECT_EQ(tested.text.texts[3].x, volumeX + 0x140);
        EXPECT_EQ(tested.text.texts[3].y, 0x7d);
        EXPECT_EQ(runtime.ui.volumeBarX, volumeX + 0x144 + 7 * LetterWidth);
        EXPECT_EQ(runtime.ui.volumeBarY, 0x7f);

        const std::vector<DrawnQuad> quads = QuadsOf(tested.sprites);
        // 7 filas, 3 de la barra, 2 del marco del brillo, 5 casillas y las cifras 5, 7, 9, 1 1 y 1 3.
        ASSERT_EQ(quads.size(), 7u + 3u + 2u + 5u + 7u);
        EXPECT_FLOAT_EQ(quads[5].y, 180.0f);
        EXPECT_FLOAT_EQ(quads[6].y, 232.0f);
        const DrawnQuad& fill = quads[9];
        EXPECT_FALSE(fill.slot.has_value());
        EXPECT_FLOAT_EQ(fill.x, static_cast<float>(runtime.ui.volumeBarX + 1));
        // Las casillas hasta la 1 prendidas; las cifras de las apagadas, de color.
        EXPECT_FLOAT_EQ(quads[12].color.red, 0.8f);
        EXPECT_FLOAT_EQ(quads[13].color.red, 0.8f);
        EXPECT_FLOAT_EQ(quads[14].color.red, 0.0f);
        EXPECT_EQ(quads[17].slot, 1);
        EXPECT_FLOAT_EQ(quads[17].color.red, 0.0f);
        EXPECT_FLOAT_EQ(quads[19].color.red, 0.8f);
    }
}

// Un dibujo intermedio de MuRet no escribe la esquina de la barra.
TEST(GameDialogDrawingTest, AnIntermediateDrawDoesNotMoveTheVolumeBar)
{
    DrawingDialogs tested(Devias);
    Client::Scenes::WorldRuntime& runtime = tested.world.Runtime();
    runtime.dialogs.current = Client::UI::OptionsDialog;
    ASSERT_TRUE(tested.Draw(true).HasValue());
    EXPECT_EQ(runtime.ui.volumeBarX, 0);
    EXPECT_EQ(runtime.ui.volumeBarY, 0);
}

// 0x630e14: el marco corto del 0x8a, el botón con su estado y los renglones centrados desde y = ((7 − 2) · 0x12) / 2
// + 0x4c.
TEST(GameDialogDrawingTest, TheMessageBoxDrawsItsFrameButtonAndRows)
{
    for (const std::int32_t map : {Lorencia, Devias})
    {
        DrawingDialogs tested(map);
        Client::Scenes::WorldRuntime& runtime = tested.world.Runtime();
        Client::UI::OpenMessageBox(runtime.dialogBox, runtime.dialogs,
                                   "una caja con un texto bastante largo para dos renglones");
        runtime.dialogBox.buttons[0].state = 2;

        ASSERT_TRUE(tested.Draw().HasValue());
        const std::vector<DrawnQuad> quads = QuadsOf(tested.sprites);
        ASSERT_EQ(quads.size(), 7u);
        EXPECT_EQ(quads[0].slot, 0xfc);
        EXPECT_EQ(quads[1].slot, 0xfb);
        EXPECT_FLOAT_EQ(quads[4].y, 185.0f);
        EXPECT_EQ(quads[5].slot, 0xfc);
        EXPECT_FLOAT_EQ(quads[5].y, 225.0f);
        EXPECT_EQ(quads[6].slot, 0xf2);
        EXPECT_FLOAT_EQ(quads[6].x, static_cast<float>(0x47 + 213));
        EXPECT_FLOAT_EQ(quads[6].y, static_cast<float>(0x8c + 60));

        ASSERT_EQ(tested.text.texts.size(), 2u);
        EXPECT_EQ(tested.text.texts[0].text, "una caja con un texto bastante largo ");
        EXPECT_EQ(tested.text.texts[0].y, (5 * 0x12) / 2 + 0x4c);
        EXPECT_EQ(tested.text.texts[1].y, (5 * 0x12) / 2 + 0x4c + 0x12);
    }
}

// 0x631297: las flechas del 0x8c, gris la del tope; la del mouse con el clic lo gasta (Q-C553), salvo en un dibujo
// intermedio.
TEST(GameDialogDrawingTest, ThePagedArrowsTakeTheClick)
{
    DrawingDialogs tested(Lorencia);
    Client::Scenes::WorldRuntime& runtime = tested.world.Runtime();
    runtime.dialogs.current = Client::UI::PagedDialog;
    runtime.dialogBox.page = 0;
    runtime.dialogBox.lastPage = 2;
    runtime.movement.pendingClick = true;
    tested.mouse = {0x180, 0xd0};

    ASSERT_TRUE(tested.Draw(true).HasValue());
    EXPECT_TRUE(runtime.movement.pendingClick);
    ASSERT_TRUE(tested.Draw().HasValue());
    EXPECT_FALSE(runtime.movement.pendingClick);

    const std::vector<DrawnQuad> quads = QuadsOf(tested.sprites);
    // El marco (6) y las dos flechas, dos veces.
    ASSERT_EQ(quads.size(), 16u);
    EXPECT_EQ(quads[14].slot, 9);
    EXPECT_FLOAT_EQ(quads[14].color.red, 0.2f);
    EXPECT_EQ(quads[15].slot, 9);
    EXPECT_FLOAT_EQ(quads[15].color.red, 0.5f);

    // Sin el clic, la flecha con el mouse en 0,7.
    tested.sprites.Begin(Screen640);
    ASSERT_TRUE(tested.Draw().HasValue());
    EXPECT_FLOAT_EQ(QuadsOf(tested.sprites).back().color.red, 0.7f);
}

// 0x632a2b: las cajas 0x8b, 0x97 y 0x9b sin el objeto del evento no tienen texto; con él, cortan.
TEST(GameDialogDrawingTest, TheEventBoxesCutWithTheEventObject)
{
    DrawingDialogs tested(Devias);
    Client::Scenes::WorldRuntime& runtime = tested.world.Runtime();
    runtime.dialogs.current = 0x97;
    ASSERT_TRUE(tested.Draw().HasValue());
    EXPECT_EQ(QuadsOf(tested.sprites).size(), 9u);
    EXPECT_TRUE(tested.text.texts.empty());

    runtime.eventObject = Client::Scenes::EventMapObject{};
    const auto drawn = tested.Draw();
    ASSERT_FALSE(drawn.HasValue());
    EXPECT_NE(drawn.Error().find("0x632a2b"), std::string::npos) << drawn.Error();
}

// 0x630c51: el OK genérico, con su botón (0xf2 con el mouse) y sus textos de uno o dos renglones.
TEST(GameDialogDrawingTest, TheGenericOkDrawsItsTexts)
{
    for (const std::int32_t map : {Lorencia, Devias})
    {
        DrawingDialogs tested(map);
        Client::Scenes::WorldRuntime& runtime = tested.world.Runtime();
        runtime.dialogs.current = Client::UI::DisconnectDialog;
        tested.mouse = {0x11c, 0x76};

        ASSERT_TRUE(tested.Draw().HasValue());
        const std::vector<DrawnQuad> quads = QuadsOf(tested.sprites);
        ASSERT_EQ(quads.size(), 2u);
        EXPECT_EQ(quads[0].slot, 0xf0);
        EXPECT_EQ(quads[1].slot, 0xf2);
        EXPECT_FLOAT_EQ(quads[1].x, 284.0f);
        ASSERT_EQ(tested.text.texts.size(), 1u);
        EXPECT_EQ(tested.text.texts[0].text, "sin conexion");
        EXPECT_EQ(tested.text.texts[0].y, 0x4b);

        tested.sprites.Begin(Screen640);
        tested.text.texts.clear();
        runtime.dialogs.current = 0x6f;
        tested.mouse = {0x162, 0x76};
        ASSERT_TRUE(tested.Draw().HasValue());
        EXPECT_EQ(QuadsOf(tested.sprites)[1].slot, 0xf1);
        ASSERT_EQ(tested.text.texts.size(), 2u);
        EXPECT_EQ(tested.text.texts[0].y, 0x45);
        EXPECT_EQ(tested.text.texts[1].text, "segundo");
        EXPECT_EQ(tested.text.texts[1].y, 0x51);
    }
}

// 0x632290 y 0x6322c1: el 0x87 y el 0x88 de la caja de la contraseña del almacén (0x5e7bf0) son del OK genérico, con
// el 442 y el 445 en un renglón.
TEST(GameDialogDrawingTest, TheKeypadDialogsAreGenericOks)
{
    for (const std::int32_t map : {Lorencia, Devias, Noria})
    {
        for (const auto& [id, expected] : {std::pair{0x87, "iguales"}, std::pair{0x88, "distinta"}})
        {
            SCOPED_TRACE(id);
            DrawingDialogs tested(map);
            tested.world.Runtime().dialogs.current = id;

            ASSERT_TRUE(tested.Draw().HasValue());
            const std::vector<DrawnQuad> quads = QuadsOf(tested.sprites);
            ASSERT_EQ(quads.size(), 2u);
            EXPECT_EQ(quads[0].slot, 0xf0);
            EXPECT_EQ(quads[1].slot, 0xf1);
            ASSERT_EQ(tested.text.texts.size(), 1u);
            EXPECT_EQ(tested.text.texts[0].text, expected);
            EXPECT_EQ(tested.text.texts[0].y, 0x4b);
        }
    }
}

// Los colores de los textos dibujados, en su orden (los quads de marca de RecordingTextDrawer).
std::vector<Color> TextColorsOf(const SpriteBatch& sprites)
{
    std::vector<Color> colors;
    const auto vertices = sprites.Vertices();
    for (const Client::Render::DrawCommand& command : sprites.Commands())
    {
        if (command.texture != Client::Tests::TextMarkerSlot)
            continue;
        for (std::size_t quad = 0; quad < command.vertexCount / 6; ++quad)
            colors.push_back(vertices[command.firstVertex + quad * 6].color);
    }
    return colors;
}

// 0x632472..0x632636: el 0x8d dibuja su texto desde ((7 − 2 − 1) · 0x12) / 2 + 0x4c = 112 y debajo las respuestas, en
// amarillo y en rojo la del mouse (a no más de 0x6a de x = 0x13f); una respuesta vacía no se dibuja ni baja la y. Al
// final, el blanco (no se ve: nada se dibuja después). El marco lleva 4 filas de 0xfb (las de (3 · 0x12 + 0x31) / 40 =
// 2, con 4 como mínimo).
TEST(GameDialogDrawingTest, TheAnswersDialogDrawsItsTextAndItsAnswers)
{
    constexpr Color White{1.0f, 1.0f, 1.0f, 1.0f};
    constexpr Color Yellow{1.0f, 1.0f, 0.0f, 1.0f};
    constexpr Color Red{1.0f, 0.0f, 0.0f, 1.0f};
    const auto tested = std::make_unique<DrawingDialogs>(Lorencia);
    Client::Scenes::WorldRuntime& runtime = tested->world.Runtime();
    Client::Data::Text::DialogTable table(Client::Data::Text::DialogCount);
    std::ranges::copy(std::string_view("hola viajero"), table[3].text.begin());
    table[3].answerCount = 2;
    std::ranges::copy(std::string_view("seguir"), table[3].answers[0].begin());
    std::ranges::copy(std::string_view("chau"), table[3].answers[1].begin());
    ASSERT_TRUE(
        Client::UI::OpenTheAnswersDialog(runtime.dialogBox, runtime.dialogs, &table, &tested->texts, 3).HasValue());
    tested->mouse = {0x13f - 0x6a, 150};

    ASSERT_TRUE(tested->Draw().HasValue());
    const std::vector<DrawnQuad> quads = QuadsOf(tested->sprites);
    ASSERT_EQ(quads.size(), 6u);
    EXPECT_EQ(quads[4].slot, 0xfb);
    EXPECT_FLOAT_EQ(quads[5].y, 225.0f);
    EXPECT_EQ(tested->Texts(), (std::vector<std::string>{"hola viajero", "1) seguir", "2) chau"}));
    ASSERT_EQ(tested->text.texts.size(), 3u);
    EXPECT_EQ(tested->text.texts[0].y, 112);
    EXPECT_EQ(tested->text.texts[1].y, 130);
    EXPECT_EQ(tested->text.texts[2].y, 148);
    EXPECT_EQ(tested->text.texts[2].x, CenteredX("2) chau"));
    EXPECT_EQ(TextColorsOf(tested->sprites), (std::vector<Color>{White, Yellow, Red}));

    // Afuera de x, ninguna en rojo; la primera vacía no baja la y de la segunda.
    tested->sprites.Begin(Screen640);
    tested->text.texts.clear();
    tested->mouse = {0x13f + 0x6b, 150};
    runtime.dialogBox.answers[0] = '\0';
    ASSERT_TRUE(tested->Draw().HasValue());
    EXPECT_EQ(tested->Texts(), (std::vector<std::string>{"hola viajero", "2) chau"}));
    EXPECT_EQ(tested->text.texts[1].y, 130);
    EXPECT_EQ(TextColorsOf(tested->sprites), (std::vector<Color>{White, Yellow}));
}

// 0x631150..0x631182 y 0x6324a3: con 7 renglones y 3 respuestas, (10 · 0x12 + 0x31) / 40 = 5 filas, y el texto no
// arranca más arriba de 0x46 (sería ((7 − 3 − 7) · 0x12) / 2 + 0x4c = 49).
TEST(GameDialogDrawingTest, TheAnswersDialogGrowsWithItsRows)
{
    const auto tested = std::make_unique<DrawingDialogs>(Lorencia);
    Client::Scenes::WorldRuntime& runtime = tested->world.Runtime();
    runtime.dialogs.current = Client::UI::AnswersDialog;
    runtime.dialogBox.lineCount = 7;
    runtime.dialogBox.answerCount = 3;
    std::ranges::copy(std::string_view("arriba"), runtime.dialogBox.rows.begin());
    ASSERT_TRUE(tested->Draw().HasValue());
    const std::vector<DrawnQuad> quads = QuadsOf(tested->sprites);
    ASSERT_EQ(quads.size(), 7u);
    EXPECT_EQ(quads[5].slot, 0xfb);
    EXPECT_FLOAT_EQ(quads[6].y, 265.0f);
    ASSERT_EQ(tested->Texts(), (std::vector<std::string>{"arriba"}));
    EXPECT_EQ(tested->text.texts[0].y, 0x46);

    // El 0x8a también baja con las respuestas que quedaron (0x632472 es el mismo tramo), sin el mínimo.
    tested->sprites.Begin(Screen640);
    tested->text.texts.clear();
    runtime.dialogs.current = Client::UI::MessageBoxDialog;
    ASSERT_TRUE(tested->Draw().HasValue());
    ASSERT_EQ(tested->Texts(), (std::vector<std::string>{"arriba"}));
    EXPECT_EQ(tested->text.texts[0].y, 49);
}

// El tamaño de un quad del lote (sin los de los textos) en 640 × 480, con el error de dar vuelta la y (480 − y), y lo
// más lejos que llega en la textura.
constexpr float Pixel = 1.0e-3f;
struct QuadExtent
{
    float width = 0.0f;
    float height = 0.0f;
    float du = 0.0f;
    float dv = 0.0f;
};

QuadExtent ExtentOf(const SpriteBatch& sprites, std::size_t index)
{
    const auto vertices = sprites.Vertices();
    std::size_t seen = 0;
    for (const Client::Render::DrawCommand& command : sprites.Commands())
    {
        if (command.texture == Client::Tests::TextMarkerSlot)
            continue;
        const std::size_t quads = command.vertexCount / 6;
        if (index >= seen + quads)
        {
            seen += quads;
            continue;
        }
        const std::size_t first = command.firstVertex + (index - seen) * 6;
        const auto corners = vertices.subspan(first, 6);
        const auto [left, right] = std::ranges::minmax(corners, {}, &Client::Render::SpriteVertex::x);
        const auto [bottom, top] = std::ranges::minmax(corners, {}, &Client::Render::SpriteVertex::y);
        return QuadExtent{.width = right.x - left.x,
                          .height = top.y - bottom.y,
                          .du = std::ranges::max(corners, {}, &Client::Render::SpriteVertex::u).u,
                          .dv = std::ranges::max(corners, {}, &Client::Render::SpriteVertex::v).v};
    }
    return {};
}

// Los renglones de la caja, uno por cada 0x26 bytes de 0x7d36a98, y su cuenta.
void SetRows(Client::UI::DialogBoxState& box, std::initializer_list<std::string_view> rows)
{
    box.rows.fill('\0');
    std::size_t row = 0;
    for (const std::string_view text : rows)
        std::ranges::copy(text, box.rows.begin() + static_cast<std::ptrdiff_t>(row++ * Client::UI::DialogRowBytes));
    box.lineCount = static_cast<std::int32_t>(rows.size());
}

// Un diálogo de sí y no como lo deja 0x633300 y su paso (0x6282dc): el sí con el mouse encima (2) y el no afuera (3).
void OpenYesNo(Client::Scenes::WorldRuntime& runtime, std::int32_t id)
{
    runtime.dialogs.current = id;
    runtime.dialogBox.buttons[0] = {.state = 2, .x = 0x15, .y = 0x5a, .width = 0x46, .height = 0x15};
    runtime.dialogBox.buttons[1] = {.state = 3, .x = 0x78, .y = 0x5a, .width = 0x46, .height = 0x15};
}

// x = centro − (cx / 2) de un texto en 640 × 480.
int CenteredXAt(int center, std::string_view text)
{
    return center - static_cast<int>(text.size()) * LetterWidth / 2;
}

// 0x630cc4..0x630fbe: el marco de los de sí y no, 0xfc en y = 100, 3 filas de 0xfb desde 105 y 0xfc en 225, sin
// mezcla; y los botones, 0xf0 + estado en (x + 213, y + 100) de 70 × 21 (sin el − 1 de las cajas) con las UV (70 /
// 128, 21 / 32). Sin rand().
TEST(GameDialogDrawingTest, TheYesNoDialogsDrawTheirFrameAndButtons)
{
    for (const std::int32_t map : {Lorencia, Devias, Noria})
    {
        for (const std::int32_t id : {0x94, 0x96, 0x98, 0x99, 0xa9})
        {
            SCOPED_TRACE(testing::Message() << "mapa " << map << ", diálogo " << id);
            const auto tested = std::make_unique<DrawingDialogs>(map);
            OpenYesNo(tested->world.Runtime(), id);
            const std::uint32_t before = tested->random.State();

            ASSERT_TRUE(tested->Draw().HasValue());
            EXPECT_EQ(tested->random.State(), before);
            const std::vector<DrawnQuad> quads = QuadsOf(tested->sprites);
            ASSERT_EQ(quads.size(), 7u);
            EXPECT_EQ(quads[0].slot, 0xfc);
            EXPECT_EQ(quads[0].blend, BlendMode::Opaque);
            EXPECT_FLOAT_EQ(quads[0].x, 213.0f);
            EXPECT_FLOAT_EQ(quads[0].y, 100.0f);
            for (std::size_t row = 1; row <= 3; ++row)
            {
                EXPECT_EQ(quads[row].slot, 0xfb);
                EXPECT_FLOAT_EQ(quads[row].y, 105.0f + 40.0f * static_cast<float>(row - 1));
            }
            EXPECT_EQ(quads[4].slot, 0xfc);
            EXPECT_FLOAT_EQ(quads[4].y, 225.0f);
            EXPECT_EQ(quads[5].slot, 0xf2);
            EXPECT_FLOAT_EQ(quads[5].x, static_cast<float>(0x15 + 213));
            EXPECT_FLOAT_EQ(quads[5].y, static_cast<float>(0x5a + 100));
            EXPECT_EQ(quads[6].slot, 0xf3);
            EXPECT_FLOAT_EQ(quads[6].x, static_cast<float>(0x78 + 213));
            const QuadExtent edge = ExtentOf(tested->sprites, 0);
            EXPECT_NEAR(edge.width, 213.0f, Pixel);
            EXPECT_NEAR(edge.height, 5.0f, Pixel);
            EXPECT_FLOAT_EQ(edge.du, 0.83203125f);
            EXPECT_FLOAT_EQ(edge.dv, 0.625f);
            const QuadExtent button = ExtentOf(tested->sprites, 6);
            EXPECT_NEAR(button.width, 70.0f, Pixel);
            EXPECT_NEAR(button.height, 21.0f, Pixel);
            EXPECT_FLOAT_EQ(button.du, 70.0f / 128.0f);
            EXPECT_FLOAT_EQ(button.dv, 21.0f / 32.0f);
            EXPECT_TRUE(tested->text.texts.empty());
        }
    }
}

// 0x630dfd..0x630e09: solo los botones con estado de 1 a 4.
TEST(GameDialogDrawingTest, TheYesNoButtonsGoFromOneToFour)
{
    const auto tested = std::make_unique<DrawingDialogs>(Noria);
    Client::Scenes::WorldRuntime& runtime = tested->world.Runtime();
    OpenYesNo(runtime, 0x98);
    runtime.dialogBox.buttons[0].state = 5;
    runtime.dialogBox.buttons[1].state = 4;
    ASSERT_TRUE(tested->Draw().HasValue());
    std::vector<DrawnQuad> quads = QuadsOf(tested->sprites);
    ASSERT_EQ(quads.size(), 6u);
    EXPECT_EQ(quads[5].slot, 0xf4);

    tested->sprites.Begin(Screen640);
    runtime.dialogBox.buttons[1].state = 0;
    ASSERT_TRUE(tested->Draw().HasValue());
    EXPECT_EQ(QuadsOf(tested->sprites).size(), 5u);
}

// 0x632637..0x632a2a: sin nada en 0x78abb14, los renglones en la fuente gruesa y en (1; 0,7; 0), centrados en x =
// 0x15e desde y = 100 + 0x1e, de a 0x12, y así quedan.
TEST(GameDialogDrawingTest, TheYesNoRowsAreBoldAndOrange)
{
    constexpr Color Orange{1.0f, 0.7f, 0.0f, 1.0f};
    for (const std::int32_t map : {Lorencia, Devias, Noria})
    {
        SCOPED_TRACE(map);
        const auto tested = std::make_unique<DrawingDialogs>(map);
        Client::Scenes::WorldRuntime& runtime = tested->world.Runtime();
        OpenYesNo(runtime, 0x94);
        SetRows(runtime.dialogBox, {"una pregunta", "larga"});

        ASSERT_TRUE(tested->Draw().HasValue());
        ASSERT_EQ(tested->Texts(), (std::vector<std::string>{"una pregunta", "larga"}));
        EXPECT_EQ(tested->text.texts[0].x, CenteredXAt(0x15e, "una pregunta"));
        EXPECT_EQ(tested->text.texts[0].y, 130);
        EXPECT_EQ(tested->text.texts[1].x, CenteredXAt(0x15e, "larga"));
        EXPECT_EQ(tested->text.texts[1].y, 148);
        for (const Client::Tests::RecordedText& recorded : tested->text.texts)
            EXPECT_EQ(recorded.style.font, Client::Render::Text::FontKind::Bold);
        EXPECT_EQ(TextColorsOf(tested->sprites), (std::vector<Color>{Orange, Orange}));
        EXPECT_EQ(runtime.hud.dcFont, Client::Render::Text::FontKind::Bold);
    }
}

// 0x632675..0x632902 y 0x632944..0x6329bf: lo que espera el ítem del cursor (0x78abb14) mueve y pinta los renglones.
TEST(GameDialogDrawingTest, TheCursorChoiceMovesAndPaintsTheYesNoRows)
{
    constexpr Color Orange{1.0f, 0.7f, 0.0f, 1.0f};
    constexpr Color Red{1.0f, 0.0f, 0.0f, 1.0f};
    constexpr Color RedFourth{1.0f, 0.1f, 0.1f, 1.0f};
    constexpr Color Mix{0.9f, 0.9f, 1.0f, 1.0f};
    constexpr Color White{1.0f, 1.0f, 1.0f, 1.0f};
    using Client::Render::Text::FontKind;
    struct Case
    {
        std::int32_t id = 0;
        std::int32_t choice = 0;
        int x = 0;
        int y = 0;
        std::vector<Color> colors;
        std::vector<FontKind> fonts;
    };
    const std::vector<Case> cases{
        // 1: el primero en rojo.
        {0x98,
         1,
         0x15e,
         130,
         {Red, Orange, Orange, Orange},
         {FontKind::Bold, FontKind::Bold, FontKind::Bold, FontKind::Bold}},
        // 3: en 0x140 desde 100 + 0xf, y el cuarto en (1; 0,1; 0,1).
        {0x94,
         3,
         0x140,
         115,
         {Orange, Orange, Orange, RedFourth},
         {FontKind::Bold, FontKind::Bold, FontKind::Bold, FontKind::Bold}},
        // 4: en 0x140 desde 100 + 0x2d.
        {0x99,
         4,
         0x140,
         145,
         {Orange, Orange, Orange, Orange},
         {FontKind::Bold, FontKind::Bold, FontKind::Bold, FontKind::Bold}},
        // 6: el primero en rojo y los demás en la fuente normal y en blanco.
        {0x98,
         6,
         0x15e,
         130,
         {Red, White, White, White},
         {FontKind::Bold, FontKind::Normal, FontKind::Normal, FontKind::Normal}},
        // El 0x96 no se mueve, pero los colores siguen a 0x78abb14: con 2, (0,9; 0,9; 1) sin el título de la mezcla.
        {0x96, 2, 0x15e, 130, {Mix, Mix, Mix, Mix}, {FontKind::Bold, FontKind::Bold, FontKind::Bold, FontKind::Bold}},
        {0x96,
         3,
         0x15e,
         130,
         {Orange, Orange, Orange, RedFourth},
         {FontKind::Bold, FontKind::Bold, FontKind::Bold, FontKind::Bold}},
    };
    for (const std::int32_t map : {Lorencia, Devias, Noria})
    {
        for (const Case& expected : cases)
        {
            SCOPED_TRACE(testing::Message()
                         << "mapa " << map << ", diálogo " << expected.id << ", elección " << expected.choice);
            const auto tested = std::make_unique<DrawingDialogs>(map);
            Client::Scenes::WorldRuntime& runtime = tested->world.Runtime();
            OpenYesNo(runtime, expected.id);
            runtime.npcWindows.cursorChoice = expected.choice;
            SetRows(runtime.dialogBox, {"uno", "dos", "tres", "cuatro"});

            ASSERT_TRUE(tested->Draw().HasValue());
            ASSERT_EQ(tested->text.texts.size(), 4u);
            std::vector<FontKind> fonts;
            for (std::size_t row = 0; row < 4; ++row)
            {
                const Client::Tests::RecordedText& recorded = tested->text.texts[row];
                EXPECT_EQ(recorded.x, CenteredXAt(expected.x, recorded.text));
                EXPECT_EQ(recorded.y, expected.y + 0x12 * static_cast<int>(row));
                fonts.push_back(recorded.style.font);
            }
            EXPECT_EQ(fonts, expected.fonts);
            EXPECT_EQ(TextColorsOf(tested->sprites), expected.colors);
        }
    }
}

// 0x632683..0x6328e3: con 0x78abb14 en 2, el 0x94, el 0x98, el 0x99, el 0xa3 y el 0xa8 llevan antes el título de la
// mezcla, en negrita y en (1, 1, 0), centrado en y = arriba + 0xf + 10; los renglones siguen en 0x140, 0x22 más abajo.
// El texto sale del tipo (0x7d2dc98) por sprintf.
TEST(GameDialogDrawingTest, TheMixTitleGoesBeforeTheRows)
{
    constexpr Color Yellow{1.0f, 1.0f, 0.0f, 1.0f};
    constexpr Color Mix{0.9f, 0.9f, 1.0f, 1.0f};
    struct Case
    {
        std::int32_t kind = 0;
        std::int32_t cape = 0;
        std::string title;
    };
    const std::vector<Case> cases{
        {-3, 0, "caos"},         {4, 0, "cuatro"},        {0xb, 0, "once"},       {0xc, 0, "doce"},
        {0x16, 0, "nivel 12"},   {0x17, 0, "nivel 13"},   {0xf, 0, "quince"},     {0x10, 0, "dieciseis"},
        {0x11, 0, "diecisiete"}, {0x19, 0, "alas uno"},   {0x1a, 0, "alas dos"},  {0x1b, 0, "capa"},
        {0x1c, 3, "capa(lord)"}, {0x1c, 4, "capa(otra)"}, {0x18, 0, "dinorante"}, {5, 0, "doce"},
    };
    for (const std::int32_t map : {Lorencia, Devias, Noria})
    {
        for (const std::int32_t id : {0x94, 0x98, 0x99, 0xa3, 0xa8})
        {
            // En Devias y en Noria, uno de arriba y el de abajo.
            if (map != Lorencia && id != 0x98 && id != 0xa3)
                continue;
            for (const Case& expected : cases)
            {
                SCOPED_TRACE(testing::Message() << "mapa " << map << ", diálogo " << id << ", tipo " << expected.kind);
                const auto tested = std::make_unique<DrawingDialogs>(map);
                tested->texts = Client::Tests::MakeTextTable({{229, "x"},
                                                              {601, "caos"},
                                                              {605, "cuatro"},
                                                              {612, "doce"},
                                                              {614, "once"},
                                                              {618, "doce"},
                                                              {664, "nivel %d"},
                                                              {1172, "dinorante"},
                                                              {1412, "si"},
                                                              {1413, "quince"},
                                                              {1414, "dieciseis"},
                                                              {1415, "diecisiete"},
                                                              {1903, "no"},
                                                              {1921, "alas uno"},
                                                              {1922, "alas dos"},
                                                              {1923, "capa"},
                                                              {1924, "lord"},
                                                              {1925, "otra"}});
                Client::Scenes::WorldRuntime& runtime = tested->world.Runtime();
                OpenYesNo(runtime, id);
                runtime.npcWindows.cursorChoice = 2;
                runtime.npcWindows.mixKind = expected.kind;
                runtime.npcWindows.mixCapeKind = expected.cape;
                SetRows(runtime.dialogBox, {"mezclar", "seguro"});
                const std::uint32_t before = tested->random.State();

                ASSERT_TRUE(tested->Draw().HasValue());
                EXPECT_EQ(tested->random.State(), before);
                const auto& drawn = tested->text.texts;
                ASSERT_GE(drawn.size(), 3u);
                const std::size_t first = drawn.size() - 3;
                const int top = id == 0xa3 ? 0xd2 : 0x64;
                EXPECT_EQ(drawn[first].text, expected.title);
                EXPECT_EQ(drawn[first].x, CenteredX(expected.title));
                EXPECT_EQ(drawn[first].y, top + 0xf + 10);
                EXPECT_EQ(drawn[first].style.font, Client::Render::Text::FontKind::Bold);
                EXPECT_EQ(drawn[first + 1].text, "mezclar");
                EXPECT_EQ(drawn[first + 1].x, CenteredX("mezclar"));
                EXPECT_EQ(drawn[first + 1].y, top + 0xf + 10 + 0x22);
                EXPECT_EQ(drawn[first + 2].y, top + 0xf + 10 + 0x22 + 0x12);
                const std::vector<Color> colors = TextColorsOf(tested->sprites);
                ASSERT_GE(colors.size(), 3u);
                EXPECT_EQ(std::vector<Color>(colors.end() - 3, colors.end()), (std::vector<Color>{Yellow, Mix, Mix}));
            }
        }
    }
}

// 0x6327eb: el 0x1c con otra capa deja el buffer de la pila sin escribir; 0x632855: un título de 100 bytes o más pisa
// la pila. Los dos cortan con el marco ya dibujado.
TEST(GameDialogDrawingTest, TheMixTitleCutsWhatTheOriginalCannotWrite)
{
    for (const auto& [kind, cape, address] : std::to_array<std::tuple<std::int32_t, std::int32_t, std::string>>(
             {{0x1c, 2, "0x6327eb"}, {0x1c, 5, "0x6327eb"}, {6, 0, "0x632855"}}))
    {
        const auto tested = std::make_unique<DrawingDialogs>(Devias);
        tested->texts = Client::Tests::MakeTextTable({{613, std::string(100, 'a')}});
        Client::Scenes::WorldRuntime& runtime = tested->world.Runtime();
        OpenYesNo(runtime, 0x98);
        runtime.npcWindows.cursorChoice = 2;
        runtime.npcWindows.mixKind = kind;
        runtime.npcWindows.mixCapeKind = cape;
        SetRows(runtime.dialogBox, {"mezclar"});
        const auto drawn = tested->Draw();
        ASSERT_FALSE(drawn.HasValue());
        EXPECT_NE(drawn.Error().find(address), std::string::npos) << drawn.Error();
        EXPECT_EQ(QuadsOf(tested->sprites).size(), 7u);
        EXPECT_TRUE(tested->text.texts.empty());
    }
}

// 0x630cc9..0x630cd3 y 0x632658..0x632667: el 0xa3 (la pregunta de la mezcla de joyas) va en y = 210 (0xfc, 3 filas
// desde 215 y 0xfc en 335, los botones en y + 210) y sus renglones en x = 0x145 desde 210 + 0x1e; con 0x78abb14 en 3,
// en 0x140 desde 210 + 0xf.
TEST(GameDialogDrawingTest, TheJewelMixQuestionGoesLower)
{
    for (const std::int32_t map : {Lorencia, Devias, Noria})
    {
        SCOPED_TRACE(map);
        const auto tested = std::make_unique<DrawingDialogs>(map);
        Client::Scenes::WorldRuntime& runtime = tested->world.Runtime();
        OpenYesNo(runtime, 0xa3);
        SetRows(runtime.dialogBox, {"juntar", "diez"});

        ASSERT_TRUE(tested->Draw().HasValue());
        const std::vector<DrawnQuad> quads = QuadsOf(tested->sprites);
        ASSERT_EQ(quads.size(), 7u);
        EXPECT_FLOAT_EQ(quads[0].y, 210.0f);
        EXPECT_FLOAT_EQ(quads[1].y, 215.0f);
        EXPECT_FLOAT_EQ(quads[4].y, 335.0f);
        EXPECT_FLOAT_EQ(quads[5].y, static_cast<float>(0x5a + 210));
        ASSERT_EQ(tested->Texts(), (std::vector<std::string>{"juntar", "diez"}));
        EXPECT_EQ(tested->text.texts[0].x, CenteredXAt(0x145, "juntar"));
        EXPECT_EQ(tested->text.texts[0].y, 240);

        tested->sprites.Begin(Screen640);
        tested->text.texts.clear();
        runtime.npcWindows.cursorChoice = 3;
        ASSERT_TRUE(tested->Draw().HasValue());
        EXPECT_EQ(tested->text.texts[0].x, CenteredX("juntar"));
        EXPECT_EQ(tested->text.texts[0].y, 225);
    }
}

// 0x630d20..0x630deb y 0x63263a..0x632653: el 0x9a lleva 4 filas (0xfc en 265), el sí en (1; 0,3; 0,3) sin el ítem
// (0x78a59cc en 0xff) y el no en blanco, y sus renglones en x = 0x140 desde 130 sin mirar 0x78abb14.
TEST(GameDialogDrawingTest, TheChaosCastleQuestionShowsTheMissingItem)
{
    constexpr Color White{1.0f, 1.0f, 1.0f, 1.0f};
    constexpr Color Reddish{1.0f, 0.3f, 0.3f, 1.0f};
    for (const std::int32_t map : {Lorencia, Devias, Noria})
    {
        for (const std::uint32_t item : {0xffu, 0x28u})
        {
            SCOPED_TRACE(testing::Message() << "mapa " << map << ", ítem " << item);
            const auto tested = std::make_unique<DrawingDialogs>(map);
            Client::Scenes::WorldRuntime& runtime = tested->world.Runtime();
            OpenYesNo(runtime, 0x9a);
            runtime.dialogItem.scalar1Low = item;
            runtime.npcWindows.cursorChoice = 3;
            SetRows(runtime.dialogBox, {"entrar", "al castillo"});

            ASSERT_TRUE(tested->Draw().HasValue());
            const std::vector<DrawnQuad> quads = QuadsOf(tested->sprites);
            ASSERT_EQ(quads.size(), 8u);
            EXPECT_EQ(quads[4].slot, 0xfb);
            EXPECT_FLOAT_EQ(quads[4].y, 225.0f);
            EXPECT_EQ(quads[5].slot, 0xfc);
            EXPECT_FLOAT_EQ(quads[5].y, 265.0f);
            EXPECT_EQ(quads[6].color, item == 0xff ? Reddish : White);
            EXPECT_EQ(quads[7].color, White);
            EXPECT_EQ(tested->text.texts[0].x, CenteredX("entrar"));
            EXPECT_EQ(tested->text.texts[0].y, 130);
        }
    }
}

// 0x630e0f..0x630f53 y 0x630fcb..0x6310d5: los tres botones del 0xa8 en 0x646, con la mezcla normal, con el 1412, el
// 1903 y el 229 en negro y en negrita en x + 0xed − cx / 2 e y + 104; el fondo del texto queda en 0xff y el color
// vuelve.
TEST(GameDialogDrawingTest, TheLuckQuestionDrawsItsThreeButtons)
{
    for (const std::int32_t map : {Lorencia, Devias, Noria})
    {
        SCOPED_TRACE(map);
        const auto tested = std::make_unique<DrawingDialogs>(map);
        tested->texts = Client::Tests::MakeTextTable({{229, "cerrar"}, {1412, "suerte"}, {1903, "sin"}});
        Client::Scenes::WorldRuntime& runtime = tested->world.Runtime();
        runtime.dialogs.current = 0xa8;
        runtime.dialogBox.buttons[0] = {.state = 2, .x = 0x16, .y = 0x5c, .width = 0x31, .height = 0x10};
        runtime.dialogBox.buttons[1] = {.state = 3, .x = 0x52, .y = 0x5c, .width = 0x31, .height = 0x10};
        runtime.dialogBox.thirdButton = {.state = 0, .x = 0x8e, .y = 0x5c, .width = 0x31, .height = 0x10};
        SetRows(runtime.dialogBox, {"el item"});
        const std::uint32_t before = tested->random.State();

        ASSERT_TRUE(tested->Draw().HasValue());
        EXPECT_EQ(tested->random.State(), before);
        const std::vector<DrawnQuad> quads = QuadsOf(tested->sprites);
        ASSERT_EQ(quads.size(), 8u);
        for (const std::size_t at : {5u, 6u, 7u})
        {
            EXPECT_EQ(quads[at].slot, 0x646);
            EXPECT_EQ(quads[at].blend, BlendMode::Alpha);
            EXPECT_FLOAT_EQ(quads[at].y, static_cast<float>(0x5c + 100));
        }
        EXPECT_FLOAT_EQ(quads[5].x, static_cast<float>(0x16 + 213));
        EXPECT_FLOAT_EQ(quads[7].x, static_cast<float>(0x8e + 213));
        const QuadExtent button = ExtentOf(tested->sprites, 7);
        EXPECT_NEAR(button.width, 49.0f, Pixel);
        EXPECT_NEAR(button.height, 16.0f, Pixel);
        EXPECT_FLOAT_EQ(button.du, 0.765625f);
        EXPECT_FLOAT_EQ(button.dv, 1.0f);
        ASSERT_EQ(tested->Texts(), (std::vector<std::string>{"suerte", "sin", "cerrar", "el item"}));
        const auto& drawn = tested->text.texts;
        EXPECT_EQ(drawn[0].x, CenteredXAt(0x16 + 0xed, "suerte"));
        EXPECT_EQ(drawn[0].y, 100 + 0x5c + 4);
        EXPECT_EQ(drawn[2].x, CenteredXAt(0x8e + 0xed, "cerrar"));
        for (std::size_t at = 0; at < 3; ++at)
        {
            EXPECT_EQ(drawn[at].style.font, Client::Render::Text::FontKind::Bold);
            EXPECT_EQ(drawn[at].style.colors.text, 0xff000000u);
            EXPECT_EQ(drawn[at].style.colors.background, 0xffu);
        }
        EXPECT_EQ(drawn[3].style.colors.text, 0xffffffffu);
        EXPECT_EQ(drawn[3].style.colors.background, 0xffu);
        EXPECT_EQ(drawn[3].x, CenteredXAt(0x15e, "el item"));
    }
}

// 0x6304cf y la tabla 0x632bf0: el 0xaa usa el marco de los de sí y no y sus renglones van como los de las cajas
// (0x632472), centrados desde ((7 − 0 − 2) · 0x12) / 2 + 0x4c = 121, y al final el blanco.
TEST(GameDialogDrawingTest, TheNoticeQuestionUsesTheBoxRows)
{
    for (const std::int32_t map : {Lorencia, Devias, Noria})
    {
        SCOPED_TRACE(map);
        const auto tested = std::make_unique<DrawingDialogs>(map);
        Client::Scenes::WorldRuntime& runtime = tested->world.Runtime();
        OpenYesNo(runtime, 0xaa);
        SetRows(runtime.dialogBox, {"una pregunta", "del servidor"});

        ASSERT_TRUE(tested->Draw().HasValue());
        EXPECT_EQ(QuadsOf(tested->sprites).size(), 7u);
        ASSERT_EQ(tested->Texts(), (std::vector<std::string>{"una pregunta", "del servidor"}));
        EXPECT_EQ(tested->text.texts[0].x, CenteredX("una pregunta"));
        EXPECT_EQ(tested->text.texts[0].y, 121);
        EXPECT_EQ(tested->text.texts[1].y, 139);
        EXPECT_EQ(tested->text.texts[0].style.font, Client::Render::Text::FontKind::Normal);
    }
}

// 0x632a45..0x632aeb: los renglones del 0xa9 en amarillo y en la fuente gruesa, centrados en 0x140 desde y = 100 + 0xf
// + 0x1e, sin mirar 0x78abb14; al final, la fuente normal.
TEST(GameDialogDrawingTest, TheBlessRowsAreYellow)
{
    constexpr Color Yellow{1.0f, 1.0f, 0.0f, 1.0f};
    for (const std::int32_t map : {Lorencia, Devias, Noria})
    {
        SCOPED_TRACE(map);
        const auto tested = std::make_unique<DrawingDialogs>(map);
        Client::Scenes::WorldRuntime& runtime = tested->world.Runtime();
        OpenYesNo(runtime, 0xa9);
        runtime.npcWindows.cursorChoice = 6;
        SetRows(runtime.dialogBox, {"bendecir", "este item"});

        ASSERT_TRUE(tested->Draw().HasValue());
        ASSERT_EQ(tested->Texts(), (std::vector<std::string>{"bendecir", "este item"}));
        EXPECT_EQ(tested->text.texts[0].x, CenteredX("bendecir"));
        EXPECT_EQ(tested->text.texts[0].y, 145);
        EXPECT_EQ(tested->text.texts[1].y, 163);
        EXPECT_EQ(tested->text.texts[1].style.font, Client::Render::Text::FontKind::Bold);
        EXPECT_EQ(TextColorsOf(tested->sprites), (std::vector<Color>{Yellow, Yellow}));
        EXPECT_EQ(runtime.hud.dcFont, Client::Render::Text::FontKind::Normal);
    }
}

// Los diálogos que no son de la CL8 cortan con la ola de su paso; los de la mezcla de joyas, el 0x91 y los de sí y no
// del servidor, con la dirección de su tramo.
TEST(GameDialogDrawingTest, TheOtherDialogsAreCut)
{
    DrawingDialogs tested(Lorencia);
    for (const auto& [id, address, wave] : std::to_array<std::tuple<std::int32_t, std::string, std::string>>({
             {0x73, "0x62db60", "CL9.4"},
             {0x15, "0x62db60", "CL9"},
         }))
    {
        tested.world.Runtime().dialogs.current = id;
        const auto drawn = tested.Draw();
        ASSERT_FALSE(drawn.HasValue());
        EXPECT_NE(drawn.Error().find(address), std::string::npos) << drawn.Error();
        EXPECT_NE(drawn.Error().find(wave), std::string::npos) << drawn.Error();
    }
}

// 0x62e65c..0x62e929 y 0x632323, en Lorencia, Devias y Noria: el modo de la caja del caos. El marco (0xfc, 5 filas de
// 0xfb y 0xfc desde y = 120), los dos botones en (245, 190) y (245, 275), en blanco el del mouse y en 0,7 el otro, sus
// títulos en negrita centrados en y + 12, las explicaciones en x = 0xf8 y el título 734 al final, en y = 135 con
// 0xff0080ff.
TEST(GameDialogDrawingTest, TheChaosMixModesDrawTheirButtonsAndNotes)
{
    for (const std::int32_t map : {Lorencia, 2, 3})
    {
        auto tested = std::make_unique<DrawingDialogs>(map);
        tested->texts = Client::Tests::MakeTextTable({{734, "elegir"},
                                                      {735, "normal"},
                                                      {736, "armas"},
                                                      {870, "hacha"},
                                                      {871, "arco"},
                                                      {872, "alas"},
                                                      {873, "caballo"},
                                                      {1923, "mas"}});
        tested->world.Runtime().dialogs.current = 0x8e;
        // En el borde de abajo del segundo: y = 275 + 35.
        tested->mouse = {395, 310};
        ASSERT_TRUE(tested->Draw().HasValue());
        const std::vector<DrawnQuad> quads = QuadsOf(tested->sprites);
        ASSERT_EQ(quads.size(), 9u);
        EXPECT_EQ(quads[0].slot, 0xfc);
        EXPECT_FLOAT_EQ(quads[0].y, 120.0f);
        EXPECT_EQ(quads[1].slot, 0xfb);
        EXPECT_FLOAT_EQ(quads[1].y, 125.0f);
        EXPECT_EQ(quads[6].slot, 0xfc);
        EXPECT_FLOAT_EQ(quads[6].y, 325.0f);
        EXPECT_EQ(quads[7].slot, 0xf0);
        EXPECT_FLOAT_EQ(quads[7].x, 245.0f);
        EXPECT_FLOAT_EQ(quads[7].y, 190.0f);
        EXPECT_FLOAT_EQ(quads[7].color.red, 0.7f);
        EXPECT_FLOAT_EQ(quads[8].y, 275.0f);
        EXPECT_FLOAT_EQ(quads[8].color.red, 1.0f);
        const std::vector<std::string> expected{"normal", "alas", "caballo", "mas", "armas", "hacha", "arco", "elegir"};
        EXPECT_EQ(tested->Texts(), expected);
        const auto& drawn = tested->text.texts;
        EXPECT_EQ(drawn[0].x, CenteredX("normal"));
        EXPECT_EQ(drawn[0].y, 202);
        EXPECT_EQ(drawn[0].style.font, Client::Render::Text::FontKind::Bold);
        EXPECT_EQ(drawn[0].style.colors.text, 0xfffff0c8u);
        EXPECT_EQ(drawn[1].x, 0xf8);
        EXPECT_EQ(drawn[1].y, 150);
        EXPECT_EQ(drawn[1].style.font, Client::Render::Text::FontKind::Normal);
        EXPECT_EQ(drawn[1].style.colors.text, 0xffffffffu);
        EXPECT_EQ(drawn[4].y, 287);
        EXPECT_EQ(drawn[5].y, 240);
        EXPECT_EQ(drawn[6].y, 250);
        EXPECT_EQ(drawn[7].x, CenteredX("elegir"));
        EXPECT_EQ(drawn[7].y, 135);
        EXPECT_EQ(drawn[7].style.colors.text, 0xff0080ffu);
        EXPECT_EQ(tested->world.Runtime().hud.dcFont, Client::Render::Text::FontKind::Bold);
    }
}

// La x de un texto centrado en `center` en 640 × 480.
int CenteredAt(int center, const std::string& text)
{
    return center - static_cast<int>(text.size()) * LetterWidth / 2;
}

// Los textos de la mezcla de joyas, inventados.
Client::Data::Text::TextTable JewelTexts()
{
    return Client::Tests::MakeTextTable({{228, "listo"},
                                         {471, "esperando"},
                                         {858, "fin"},
                                         {1002, "cancelar"},
                                         {1800, "deshacer"},
                                         {1801, "juntar"},
                                         {1802, "nota uno"},
                                         {1803, "nota dos"},
                                         {1804, "elegir joya"},
                                         {1805, "y paquete"},
                                         {1806, "bendicion"},
                                         {1807, "alma"},
                                         {1808, "%d por %d"},
                                         {1816, " salio"},
                                         {1821, "libres %d"},
                                         {1822, "paquetes"}});
}

// 0x62e92e..0x62ed05 y 0x632372, en Lorencia, Devias y Noria: el menú de la mezcla de joyas (0xa1). El marco desde
// y = 120 con 6 filas; antes del primer botón el 1802 y el 1803 en x = 0xf8; los dos botones de 150 × 35 en (245, 190)
// y (245, 255), el del mouse (en el borde) en blanco y el otro en 0,7, con el 1801 y el 1800 en negrita y 0xfffff0c8
// en y + 12; el cancelar en (279, 320) en 0,8, con el 1002 centrado en 0x13f; y el título 1801 al final.
TEST(GameDialogDrawingTest, TheJewelMenuDrawsItsTwoButtonsAndTheCancel)
{
    for (const std::int32_t map : {Lorencia, Devias, Noria})
    {
        SCOPED_TRACE(map);
        auto tested = std::make_unique<DrawingDialogs>(map);
        tested->texts = JewelTexts();
        tested->world.Runtime().dialogs.current = 0xa1;
        tested->mouse = {395, 290};
        ASSERT_TRUE(tested->Draw().HasValue());
        const std::vector<DrawnQuad> quads = QuadsOf(tested->sprites);
        ASSERT_EQ(quads.size(), 11u);
        EXPECT_EQ(quads[0].slot, 0xfc);
        EXPECT_FLOAT_EQ(quads[0].y, 120.0f);
        EXPECT_EQ(quads[6].slot, 0xfb);
        EXPECT_FLOAT_EQ(quads[6].y, 325.0f);
        EXPECT_EQ(quads[7].slot, 0xfc);
        EXPECT_FLOAT_EQ(quads[7].y, 365.0f);
        EXPECT_EQ(quads[8].slot, 0xf0);
        EXPECT_FLOAT_EQ(quads[8].x, 245.0f);
        EXPECT_FLOAT_EQ(quads[8].y, 190.0f);
        EXPECT_FLOAT_EQ(quads[8].color.red, 0.7f);
        EXPECT_FLOAT_EQ(quads[9].y, 255.0f);
        EXPECT_FLOAT_EQ(quads[9].color.red, 1.0f);
        EXPECT_FLOAT_EQ(quads[10].x, 279.0f);
        EXPECT_FLOAT_EQ(quads[10].y, 320.0f);
        EXPECT_FLOAT_EQ(quads[10].color.red, 0.8f);
        const std::vector<std::string> expected{"nota uno", "nota dos", "juntar", "deshacer", "cancelar", "juntar"};
        ASSERT_EQ(tested->Texts(), expected);
        const auto& drawn = tested->text.texts;
        EXPECT_EQ(drawn[0].x, 0xf8);
        EXPECT_EQ(drawn[0].y, 0x96);
        EXPECT_EQ(drawn[0].style.font, Client::Render::Text::FontKind::Normal);
        EXPECT_EQ(drawn[1].y, 0xa0);
        EXPECT_EQ(drawn[2].x, CenteredX("juntar"));
        EXPECT_EQ(drawn[2].y, 202);
        EXPECT_EQ(drawn[2].style.font, Client::Render::Text::FontKind::Bold);
        EXPECT_EQ(drawn[2].style.colors.text, 0xfffff0c8u);
        EXPECT_EQ(drawn[3].y, 267);
        EXPECT_EQ(drawn[4].x, CenteredAt(0x13f, "cancelar"));
        EXPECT_EQ(drawn[4].y, 0x148);
        EXPECT_EQ(drawn[4].style.font, Client::Render::Text::FontKind::Bold);
        EXPECT_EQ(drawn[4].style.colors.text, 0xfffff0c8u);
        EXPECT_EQ(drawn[5].y, 135);
        EXPECT_EQ(drawn[5].style.colors.text, 0xff0080ffu);
        EXPECT_EQ(tested->sprites.Blend(), BlendMode::Alpha);
    }
}

// 0x62ed0a..0x62f36c, en los tres mapas: con la bendición elegida (0x6c62e0 en 0) su botón va en (1; 1; 0,2) y en
// negrita, y el alma con el mouse en blanco, también en negrita y con 0xfffff0c8 porque nada los vuelve (Q-C950); los
// tres paquetes en negrita con "10 · i" y "500.000 · i"; sin joya, en 0,5 y en la normal. El cancelar en (279, 365).
TEST(GameDialogDrawingTest, TheJewelChoiceMarksTheChosenKindAndItsBundles)
{
    for (const std::int32_t map : {Lorencia, Devias, Noria})
    {
        SCOPED_TRACE(map);
        auto tested = std::make_unique<DrawingDialogs>(map);
        tested->texts = JewelTexts();
        tested->world.Runtime().dialogs.current = 0xa2;
        tested->world.Runtime().npcWindows.jewels.kind = 0;
        tested->mouse = {405, 210};
        ASSERT_TRUE(tested->Draw().HasValue());
        std::vector<DrawnQuad> quads = QuadsOf(tested->sprites);
        ASSERT_EQ(quads.size(), 15u);
        EXPECT_FLOAT_EQ(quads[8].y, 405.0f);
        EXPECT_FLOAT_EQ(quads[9].x, 235.0f);
        EXPECT_FLOAT_EQ(quads[9].y, 185.0f);
        EXPECT_FLOAT_EQ(quads[9].color.blue, 0.2f);
        EXPECT_FLOAT_EQ(quads[10].x, 325.0f);
        EXPECT_FLOAT_EQ(quads[10].color.blue, 1.0f);
        EXPECT_FLOAT_EQ(quads[11].x, 235.0f);
        EXPECT_FLOAT_EQ(quads[11].y, 225.0f);
        EXPECT_FLOAT_EQ(quads[11].color.red, 0.7f);
        EXPECT_FLOAT_EQ(quads[13].y, 315.0f);
        EXPECT_FLOAT_EQ(quads[14].x, 279.0f);
        EXPECT_FLOAT_EQ(quads[14].y, 365.0f);
        EXPECT_FLOAT_EQ(quads[14].color.red, 0.7f);
        const std::vector<std::string> expected{"elegir joya",    "y paquete",     "bendicion",
                                                "alma",           "10 por 500000", "20 por 1000000",
                                                "30 por 1500000", "cancelar",      "juntar"};
        ASSERT_EQ(tested->Texts(), expected);
        const auto& drawn = tested->text.texts;
        EXPECT_EQ(drawn[0].x, 0xee);
        EXPECT_EQ(drawn[0].y, 0x9b);
        EXPECT_EQ(drawn[2].x, CenteredAt(275, "bendicion"));
        EXPECT_EQ(drawn[2].y, 193);
        EXPECT_EQ(drawn[2].style.font, Client::Render::Text::FontKind::Bold);
        EXPECT_EQ(drawn[3].x, CenteredAt(365, "alma"));
        EXPECT_EQ(drawn[3].style.font, Client::Render::Text::FontKind::Bold);
        EXPECT_EQ(drawn[3].style.colors.text, 0xfffff0c8u);
        EXPECT_EQ(drawn[4].x, CenteredX("10 por 500000"));
        EXPECT_EQ(drawn[4].y, 237);
        EXPECT_EQ(drawn[4].style.font, Client::Render::Text::FontKind::Bold);
        EXPECT_EQ(drawn[6].y, 327);
        EXPECT_EQ(drawn[7].x, CenteredAt(0x13f, "cancelar"));
        EXPECT_EQ(drawn[7].y, 0x175);

        auto unchosen = std::make_unique<DrawingDialogs>(map);
        unchosen->texts = JewelTexts();
        unchosen->world.Runtime().dialogs.current = 0xa2;
        unchosen->mouse = {300, 240};
        ASSERT_TRUE(unchosen->Draw().HasValue());
        quads = QuadsOf(unchosen->sprites);
        ASSERT_EQ(quads.size(), 15u);
        EXPECT_FLOAT_EQ(quads[9].color.red, 0.5f);
        EXPECT_FLOAT_EQ(quads[10].color.red, 0.5f);
        EXPECT_FLOAT_EQ(quads[11].color.red, 0.5f);
        EXPECT_EQ(unchosen->text.texts[2].style.font, Client::Render::Text::FontKind::Normal);
        EXPECT_EQ(unchosen->text.texts[4].style.font, Client::Render::Text::FontKind::Normal);
        EXPECT_EQ(unchosen->text.texts[4].style.colors.text, 0xffffffffu);
    }
}

// 0x62f371..0x62fae6 y 0x6323c1, en los tres mapas: sin joya, 3 filas, las dos joyas en (235, 160) y (315, 160) con
// su texto en y = 0xa8, el cancelar en (279,5; 200) (en blanco con el mouse en x = 280, sobre el borde de 279,5) y el
// texto en blanco antes del título 1800.
TEST(GameDialogDrawingTest, TheJewelUnmixWithoutAKindDrawsTheCancel)
{
    for (const std::int32_t map : {Lorencia, Devias, Noria})
    {
        SCOPED_TRACE(map);
        for (const auto& [mouseX, cancelColor] : std::to_array<std::pair<int, float>>({{279, 0.7f}, {280, 1.0f}}))
        {
            auto tested = std::make_unique<DrawingDialogs>(map);
            tested->texts = JewelTexts();
            tested->world.Runtime().dialogs.current = 0xa4;
            tested->mouse = {mouseX, 200};
            ASSERT_TRUE(tested->Draw().HasValue());
            const std::vector<DrawnQuad> quads = QuadsOf(tested->sprites);
            ASSERT_EQ(quads.size(), 8u);
            EXPECT_FLOAT_EQ(quads[4].y, 245.0f);
            EXPECT_FLOAT_EQ(quads[5].x, 235.0f);
            EXPECT_FLOAT_EQ(quads[5].y, 160.0f);
            EXPECT_FLOAT_EQ(quads[5].color.red, 0.5f);
            EXPECT_FLOAT_EQ(quads[6].x, 315.0f);
            EXPECT_FLOAT_EQ(quads[7].x, 279.5f);
            EXPECT_FLOAT_EQ(quads[7].y, 200.0f);
            EXPECT_FLOAT_EQ(quads[7].color.red, cancelColor);
            const std::vector<std::string> expected{"bendicion", "alma", "cancelar", "deshacer"};
            ASSERT_EQ(tested->Texts(), expected);
            const auto& drawn = tested->text.texts;
            EXPECT_EQ(drawn[0].x, CenteredAt(275, "bendicion"));
            EXPECT_EQ(drawn[0].y, 0xa8);
            EXPECT_EQ(drawn[1].x, CenteredAt(355, "alma"));
            EXPECT_EQ(drawn[2].x, CenteredAt(0x13f, "cancelar"));
            EXPECT_EQ(drawn[2].y, 0xd0);
            EXPECT_EQ(drawn[2].style.font, Client::Render::Text::FontKind::Bold);
            EXPECT_EQ(drawn[3].y, 135);
        }
    }
}

// 0x62f68f..0x62f739, en los tres mapas: con el alma elegida, 7 filas, el 1821 con las casillas vacías (0x402de0) y
// el 1822 en x = 0xe4 con 0xc8c8c8c8, y la lista de 0x6e5660 corta.
TEST(GameDialogDrawingTest, TheJewelUnmixWithAKindCutsAtTheList)
{
    for (const std::int32_t map : {Lorencia, Devias, Noria})
    {
        SCOPED_TRACE(map);
        auto tested = std::make_unique<DrawingDialogs>(map);
        tested->texts = JewelTexts();
        tested->world.Runtime().dialogs.current = 0xa4;
        tested->world.Runtime().npcWindows.jewels.kind = 1;
        for (const std::size_t cell : {3u, 9u, 63u})
            tested->world.Inventory().cells[cell].id = 0xffff;
        const auto drawn = tested->Draw();
        ASSERT_FALSE(drawn.HasValue());
        EXPECT_NE(drawn.Error().find("0x62f739"), std::string::npos) << drawn.Error();
        EXPECT_NE(drawn.Error().find("P-C338"), std::string::npos) << drawn.Error();
        const std::vector<DrawnQuad> quads = QuadsOf(tested->sprites);
        ASSERT_EQ(quads.size(), 11u);
        EXPECT_FLOAT_EQ(quads[8].y, 405.0f);
        EXPECT_FLOAT_EQ(quads[9].color.red, 0.5f);
        EXPECT_FLOAT_EQ(quads[10].color.blue, 0.2f);
        const std::vector<std::string> expected{"bendicion", "alma", "libres 3", "paquetes"};
        ASSERT_EQ(tested->Texts(), expected);
        EXPECT_EQ(tested->text.texts[2].x, 0xe4);
        EXPECT_EQ(tested->text.texts[2].y, 0xc5);
        EXPECT_EQ(tested->text.texts[2].style.colors.text, 0xc8c8c8c8u);
        EXPECT_EQ(tested->text.texts[2].style.font, Client::Render::Text::FontKind::Bold);
        EXPECT_EQ(tested->text.texts[3].y, 0xd4);
    }
}

// 0x62faeb..0x62fc1a y 0x62fc1f..0x62febe, en los tres mapas: la espera (0xa5) con el 471 centrado en 0x13f, sin
// título; el resultado (0xa6) con "%s%s %s" del 1801 (0x6e5794 en 0) o del 1800, el 1816 y el 858 en negrita, el OK
// de 65 × 25 en (287, 170) y el 228 con 0xfffff0c8. Ninguno tiene título en la cola.
TEST(GameDialogDrawingTest, TheJewelWaitAndResultDrawTheirTexts)
{
    for (const std::int32_t map : {Lorencia, Devias, Noria})
    {
        SCOPED_TRACE(map);
        auto waiting = std::make_unique<DrawingDialogs>(map);
        waiting->texts = JewelTexts();
        waiting->world.Runtime().dialogs.current = 0xa5;
        ASSERT_TRUE(waiting->Draw().HasValue());
        EXPECT_EQ(QuadsOf(waiting->sprites).size(), 4u);
        ASSERT_EQ(waiting->Texts(), std::vector<std::string>{"esperando"});
        EXPECT_EQ(waiting->text.texts[0].x, CenteredAt(0x13f, "esperando"));
        EXPECT_EQ(waiting->text.texts[0].y, 0xa5);
        EXPECT_EQ(waiting->text.texts[0].style.font, Client::Render::Text::FontKind::Normal);

        for (const auto& [mode, title, color] :
             std::to_array<std::tuple<std::int32_t, std::string, float>>({{0, "juntar", 1.0f}, {1, "deshacer", 0.8f}}))
        {
            auto result = std::make_unique<DrawingDialogs>(map);
            result->texts = JewelTexts();
            result->world.Runtime().dialogs.current = 0xa6;
            result->world.Runtime().npcWindows.jewels.mode = mode;
            result->mouse = mode == 0 ? Client::UI::Point{352, 195} : Client::UI::Point{353, 195};
            ASSERT_TRUE(result->Draw().HasValue());
            const std::vector<DrawnQuad> quads = QuadsOf(result->sprites);
            ASSERT_EQ(quads.size(), 5u);
            EXPECT_FLOAT_EQ(quads[4].x, 287.0f);
            EXPECT_FLOAT_EQ(quads[4].y, 170.0f);
            EXPECT_FLOAT_EQ(quads[4].color.red, color);
            const std::string written = title + " salio fin";
            ASSERT_EQ(result->Texts(), (std::vector<std::string>{written, "listo"}));
            EXPECT_EQ(result->text.texts[0].x, CenteredAt(0x13f, written));
            EXPECT_EQ(result->text.texts[0].y, 0x91);
            EXPECT_EQ(result->text.texts[0].style.font, Client::Render::Text::FontKind::Bold);
            EXPECT_EQ(result->text.texts[0].style.colors.text, 0xffffffffu);
            EXPECT_EQ(result->text.texts[1].y, 0xb2);
            EXPECT_EQ(result->text.texts[1].style.colors.text, 0xfffff0c8u);
            EXPECT_EQ(result->world.Runtime().hud.dcFont, Client::Render::Text::FontKind::Normal);
        }
    }
}

// 0x62fd43: lo que no entra en los 256 bytes del 0xa6 pisa la pila y corta.
TEST(GameDialogDrawingTest, TheJewelResultCutsALongText)
{
    auto tested = std::make_unique<DrawingDialogs>(Devias);
    tested->texts =
        Client::Tests::MakeTextTable({{228, "listo"}, {858, "fin"}, {1801, "juntar"}, {1816, std::string(250, 'x')}});
    tested->world.Runtime().dialogs.current = 0xa6;
    const auto drawn = tested->Draw();
    ASSERT_FALSE(drawn.HasValue());
    EXPECT_NE(drawn.Error().find("0x62fd43"), std::string::npos) << drawn.Error();
}

// 0x6304f2..0x6305c9, en los tres mapas: el marco del 0x91 desde y = 100 con 2 filas, y el EDIT de la CL9.4 corta.
TEST(GameDialogDrawingTest, TheTypedTextDrawsItsFrameAndCutsAtTheEdit)
{
    for (const std::int32_t map : {Lorencia, Devias, Noria})
    {
        SCOPED_TRACE(map);
        auto tested = std::make_unique<DrawingDialogs>(map);
        tested->world.Runtime().dialogs.current = 0x91;
        const auto drawn = tested->Draw();
        ASSERT_FALSE(drawn.HasValue());
        EXPECT_NE(drawn.Error().find("0x6305c9"), std::string::npos) << drawn.Error();
        EXPECT_NE(drawn.Error().find("CL9.4"), std::string::npos) << drawn.Error();
        const std::vector<DrawnQuad> quads = QuadsOf(tested->sprites);
        ASSERT_EQ(quads.size(), 4u);
        EXPECT_FLOAT_EQ(quads[0].y, 100.0f);
        EXPECT_FLOAT_EQ(quads[1].y, 105.0f);
        EXPECT_FLOAT_EQ(quads[2].y, 145.0f);
        EXPECT_FLOAT_EQ(quads[3].y, 185.0f);
    }
}
} // namespace
