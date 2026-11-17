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
#include <string_view>
#include <memory>
#include <optional>
#include <string>
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
        {381, "salir"},          {382, "otro servidor"},
        {383, "otro personaje"}, {384, "cerrar"},
        {385, "opciones"},       {386, "ataque"},
        {387, "aviso"},          {388, "listo"},
        {389, "volumen"},        {401, "uno"},
        {402, "sin conexion"},   {405, "primero"},
        {406, "segundo"},        {409, "fin"},
        {857, "ganaste"},        {858, "mision cumplida"},
        {859, "perdiste"},       {860, "mision sin cumplir"},
        {861, "exp %d"},         {863, "puntos %d"},
        {919, "carteles"},       {1840, "brillo"},
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

// 0x632a2b: las cajas 0x8b, 0x97 y 0x9b sin el objeto del evento no tienen texto; con él, el de su método +0x10 por
// el tipo del objeto y no por la caja (test_event_drawing mira cada uno), en Lorencia y en Devias.
TEST(GameDialogDrawingTest, TheEventBoxesDrawTheTextOfTheEventObject)
{
    for (const std::int32_t map : {Lorencia, Devias})
    {
        SCOPED_TRACE(map);
        DrawingDialogs tested(map);
        Client::Scenes::WorldRuntime& runtime = tested.world.Runtime();
        runtime.dialogs.current = 0x97;
        ASSERT_TRUE(tested.Draw().HasValue());
        EXPECT_EQ(QuadsOf(tested.sprites).size(), 9u);
        EXPECT_TRUE(tested.text.texts.empty());

        runtime.eventObject = Client::Scenes::EventMapObject{.kind = Client::Scenes::EventMapKind::BloodCastle};
        tested.sprites.Begin(Screen640);
        const auto drawn = tested.Draw();
        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
        EXPECT_EQ(QuadsOf(tested.sprites).size(), 9u);
        EXPECT_EQ(tested.Texts(), (std::vector<std::string>{"perdiste", "mision sin cumplir", "exp 0", "puntos 0"}));
    }
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

// Los diálogos que no son de la CL8 cortan con la ola de su paso.
TEST(GameDialogDrawingTest, TheOtherDialogsAreCut)
{
    DrawingDialogs tested(Lorencia);
    for (const auto& [id, wave] :
         std::to_array<std::pair<std::int32_t, std::string>>({{0x73, "CL9.4"}, {0x8e, "CL9.3"}, {0x15, "CL9"}}))
    {
        tested.world.Runtime().dialogs.current = id;
        const auto drawn = tested.Draw();
        ASSERT_FALSE(drawn.HasValue());
        EXPECT_NE(drawn.Error().find("0x62db60"), std::string::npos) << drawn.Error();
        EXPECT_NE(drawn.Error().find(wave), std::string::npos) << drawn.Error();
    }
}
} // namespace
