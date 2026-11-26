#include "Scenes/GameDialogDrawing.h"

#include "Core/Expected.h"
#include "Core/PixelSize.h"
#include "Crt/CrtRandom.h"
#include "Tables/DialogTable.h"
#include "Tables/TextTable.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/TextStyle.h"
#include "Scenes/ActorState.h"
#include "Scenes/EventMapObject.h"
#include "Scenes/GameHud.h"
#include "Scenes/GameMovementState.h"
#include "Scenes/GuildState.h"
#include "Scenes/JewelBundleList.h"
#include "Scenes/NpcWindowState.h"
#include "Scenes/SocialState.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/ChatInput.h"
#include "UI/Dialogs.h"
#include "UI/GameDialogs.h"
#include "UI/GameUiState.h"
#include "UI/OldControls.h"
#include "UI/OldWindowBase.h"
#include "UI/Widget.h"

#include "support/RecordingTextDrawer.h"
#include "support/SceneDataFiles.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
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

constexpr Client::Core::PixelSize Screen640{640, 480};
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

template <std::size_t Size> void Name(std::array<std::byte, Size>& name, std::string_view text)
{
    name = {};
    for (std::size_t at = 0; at < text.size() && at < Size; ++at)
        name[at] = static_cast<std::byte>(text[at]);
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
                               .mouse = mouse,
                               .arrowHeld = arrowHeld};
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
    Client::Crt::CrtRandom random;
    std::uint8_t cellDepth = 0;
    std::optional<std::int32_t> sound;
    SpriteBatch sprites;
    Client::Tests::RecordingTextDrawer text{sprites};
    Client::UI::Point mouse{};
    bool arrowHeld = false;
    Client::Tables::TextTable texts = Client::Tests::MakeTextTable({
        {381, "salir"},
        {382, "otro servidor"},
        {383, "otro personaje"},
        {384, "cerrar"},
        {385, "opciones"},
        {386, "ataque"},
        {387, "aviso"},
        {388, "listo"},
        {389, "volumen"},
        {390, "nombre corto"},
        {391, "simbolo invalido"},
        {392, "nombre"},
        {393, "no permitido"},
        {401, "uno"},
        {402, "sin conexion"},
        {405, "primero"},
        {406, "segundo"},
        {409, "fin"},
        {418, "de %s"},
        {419, "comerciar?"},
        {420, "zen al almacen"},
        {421, "zen del almacen"},
        {422, "zen al comercio"},
        {425, "entrar a la party?"},
        {426, "marca vacia"},
        {427, "nro"},
        {428, "seguro?"},
        {430, "guerra de %s"},
        {431, "aceptas?"},
        {432, "aceptas la batalla?"},
        {423, "sin zen"},
        {424, "tope del comercio"},
        {440, "contrasena incorrecta"},
        {441, "ya estaba trancado"},
        {442, "iguales"},
        {445, "distinta"},
        {857, "ganaste"},
        {858, "mision cumplida"},
        {859, "perdiste"},
        {860, "mision sin cumplir"},
        {861, "exp %d"},
        {863, "puntos %d"},
        {919, "carteles"},
        {1128, "titulo de la tienda"},
        {1129, "precio del item"},
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

// 0x630818 y 0x63093e con la cola de 0x63140b: el 0x74 (sin zen, 0x631dc5) y el 0x75 (el tope del comercio,
// 0x631df7), el 0x85 y el 0x86 (la contraseña del almacén, 0x63222e y 0x63225f) y el 0x89 (0x6322f2) son del OK
// genérico, con el 423, el 424, el 440, el 441 y el 401 en un renglón, centrado en y = [EBP − 0x20] + 0xf (0x4b).
TEST(GameDialogDrawingTest, TheMoneyAndWarehouseNoticesAreGenericOks)
{
    for (const std::int32_t map : {Lorencia, Devias, Noria})
    {
        for (const auto& [id, expected] :
             std::to_array<std::pair<std::int32_t, std::string_view>>({{0x74, "sin zen"},
                                                                       {0x75, "tope del comercio"},
                                                                       {0x85, "contrasena incorrecta"},
                                                                       {0x86, "ya estaba trancado"},
                                                                       {0x89, "uno"}}))
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
            EXPECT_EQ(tested.text.texts[0].x, CenteredX(std::string(expected)));
            EXPECT_EQ(tested.text.texts[0].y, 0x4b);
        }
    }
}

// 0x632441: el 0x90 (el 99 con 1, 0x4e4a40) es del OK genérico como el 0x8f, pero su renglón no sale de Text.bmd: es la
// cadena de la .data 0x6d6320, en coreano (EUC-KR), "서버 이전시 오류 발생 : 문의 바람", con sus bytes tal cual.
TEST(GameDialogDrawingTest, TheServerMoveErrorDrawsTheStringOfTheData)
{
    DrawingDialogs tested(Lorencia);
    tested.world.Runtime().dialogs.current = 0x90;

    ASSERT_TRUE(tested.Draw().HasValue());
    const std::vector<DrawnQuad> quads = QuadsOf(tested.sprites);
    ASSERT_EQ(quads.size(), 2u);
    EXPECT_EQ(quads[0].slot, 0xf0);
    ASSERT_EQ(tested.text.texts.size(), 1u);
    EXPECT_EQ(tested.text.texts[0].text,
              "\xbc\xad\xb9\xf6 \xc0\xcc\xc0\xfc\xbd\xc3 \xbf\xc0\xb7\xf9 \xb9\xdf\xbb\xfd : "
              "\xb9\xae\xc0\xc7 \xb9\xd9\xb6\xf7");
    EXPECT_EQ(tested.text.texts[0].text.size(), 33u);
    EXPECT_EQ(tested.text.texts[0].y, 0x4b);
}

// Los avisos del alta del gremio (CL9 hito 8): el marco y el OK del genérico (0x630818 y 0x63093e) y sus textos, el
// 0x72 en dos renglones (392 y 393 en 75 − 6 y 75 + 6) y el 0x79, el 0x7a y el 0x7e en uno (390, 391 y 426 en 75).
TEST(GameDialogDrawingTest, TheGuildCreationNoticesDrawLikeTheGenericOk)
{
    for (const std::int32_t map : {Lorencia, Devias})
    {
        SCOPED_TRACE(map);
        DrawingDialogs tested(map);
        Client::Scenes::WorldRuntime& runtime = tested.world.Runtime();
        for (const auto& [id, rows] :
             std::to_array<std::pair<std::int32_t, std::vector<std::string>>>({{0x72, {"nombre", "no permitido"}},
                                                                               {0x79, {"nombre corto"}},
                                                                               {0x7a, {"simbolo invalido"}},
                                                                               {0x7e, {"marca vacia"}}}))
        {
            SCOPED_TRACE(id);
            tested.sprites.Begin(Screen640);
            tested.text.texts.clear();
            runtime.dialogs.current = id;
            tested.mouse = {0x11c, 0x76};
            const auto drawn = tested.Draw();
            ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
            const std::vector<DrawnQuad> quads = QuadsOf(tested.sprites);
            ASSERT_EQ(quads.size(), 2u);
            EXPECT_EQ(quads[0].slot, 0xf0);
            EXPECT_EQ(quads[1].slot, 0xf2);
            EXPECT_FLOAT_EQ(quads[1].x, 284.0f);
            EXPECT_EQ(tested.Texts(), rows);
            if (rows.size() == 1)
                EXPECT_EQ(tested.text.texts[0].y, 75);
            else
            {
                EXPECT_EQ(tested.text.texts[0].y, 75 - 6);
                EXPECT_EQ(tested.text.texts[1].y, 75 + 6);
            }
        }
    }
}

// 0x77 (CL9 hito 7, fase 2): el marco del OK genérico, el sí y el no (0x6309d4), y en la fuente del DC "de %s" con el
// nombre del primero vivo de los 400 con la clave de 0x78aba4c (0x5110d0, con MOVSX) en 75 − 6 y la pregunta en 75 + 6.
// Sin nadie con la clave, el 400 (sin nombre).
TEST(GameDialogDrawingTest, ThePartyInvitationNamesWhoInvites)
{
    for (const std::int32_t map : {Lorencia, Devias})
    {
        SCOPED_TRACE(map);
        auto tested = std::make_unique<DrawingDialogs>(map);
        Client::Scenes::WorldRuntime& runtime = tested->world.Runtime();
        runtime.dialogs.current = 0x77;
        Client::Scenes::ActorState& dead = tested->world.Actors()[3];
        dead.key = 0x8456;
        Name(dead.name, "Zorzal");
        Client::Scenes::ActorState& inviter = tested->world.Actors()[7];
        inviter.alive = true;
        inviter.key = 0x8456;
        Name(inviter.name, "Brezal");
        runtime.social.party.requestKey = static_cast<std::int16_t>(0x8456);
        tested->mouse = {0x12f, 0x76};

        ASSERT_TRUE(tested->Draw().HasValue());
        const std::vector<DrawnQuad> quads = QuadsOf(tested->sprites);
        ASSERT_EQ(quads.size(), 3u);
        EXPECT_EQ(quads[0].slot, 0xf0);
        EXPECT_EQ(quads[0].x, 213.0f);
        EXPECT_EQ(quads[0].y, 60.0f);
        EXPECT_EQ(quads[1].slot, 0xf2);
        EXPECT_EQ(quads[1].x, 234.0f);
        EXPECT_EQ(quads[1].y, 98.0f);
        EXPECT_EQ(quads[1].blend, BlendMode::Opaque);
        EXPECT_EQ(quads[2].slot, 0xf3);
        EXPECT_EQ(quads[2].x, 334.0f);
        EXPECT_EQ(tested->Texts(), (std::vector<std::string>{"de Brezal", "entrar a la party?"}));
        EXPECT_EQ(tested->text.texts[0].x, CenteredX("de Brezal"));
        EXPECT_EQ(tested->text.texts[0].y, 75 - 6);
        EXPECT_EQ(tested->text.texts[0].style.font, Client::Render::Text::FontKind::Normal);
        EXPECT_EQ(tested->text.texts[1].y, 75 + 6);
        EXPECT_EQ(tested->text.texts[1].style.colors.text, 0xffffffffu);
        EXPECT_EQ(tested->sprites.Blend(), BlendMode::Alpha);

        // La clave del 40 sin signo (0x8456) no es la de nadie: el 400, sin nombre.
        runtime.social.party.requestKey = 0x8456;
        tested->sprites.Begin(Screen640);
        tested->text.texts.clear();
        tested->mouse = {0x130, 0x76};
        ASSERT_TRUE(tested->Draw().HasValue());
        EXPECT_EQ(QuadsOf(tested->sprites)[1].slot, 0xf1);
        EXPECT_EQ(tested->Texts(), (std::vector<std::string>{"de ", "entrar a la party?"}));
        EXPECT_EQ(tested->random.State(), Client::Crt::CrtRandom{}.State());
    }
}

// 0x78: "de %s" con el otro del comercio (0x78aba1c) en la negrita grande en 75 − 11 y la pregunta en la normal en
// 75 + 11; el no con el mouse en [0x14e, 0x194) × [0x62, 0x77).
TEST(GameDialogDrawingTest, TheTradeRequestNamesTheOther)
{
    for (const std::int32_t map : {Lorencia, Devias})
    {
        SCOPED_TRACE(map);
        auto tested = std::make_unique<DrawingDialogs>(map);
        Client::Scenes::WorldRuntime& runtime = tested->world.Runtime();
        runtime.dialogs.current = 0x78;
        Name(runtime.social.trade.otherName, "Brezal");
        tested->mouse = {0x193, 0x62};

        ASSERT_TRUE(tested->Draw().HasValue());
        const std::vector<DrawnQuad> quads = QuadsOf(tested->sprites);
        ASSERT_EQ(quads.size(), 3u);
        EXPECT_EQ(quads[1].slot, 0xf1);
        EXPECT_EQ(quads[2].slot, 0xf4);
        ASSERT_EQ(tested->text.texts.size(), 2u);
        EXPECT_EQ(tested->text.texts[0].text, "de Brezal");
        EXPECT_EQ(tested->text.texts[0].y, 75 - 11);
        EXPECT_EQ(tested->text.texts[0].style.font, Client::Render::Text::FontKind::BigBold);
        EXPECT_EQ(tested->text.texts[1].text, "comerciar?");
        EXPECT_EQ(tested->text.texts[1].x, CenteredX("comerciar?"));
        EXPECT_EQ(tested->text.texts[1].y, 75 + 11);
        EXPECT_EQ(tested->text.texts[1].style.font, Client::Render::Text::FontKind::Normal);
        EXPECT_EQ(runtime.hud.dcFont, Client::Render::Text::FontKind::Normal);
    }
}

// 0x7f (CL9 hito 8): el marco del OK genérico, el sí y el no (0x6309d4) como el 0x77, y en la fuente del DC (0x63212a)
// "guerra de %s" con el rival de 0x5767804 (hasta su cero) en 75 − 6 y el 431 en 75 + 6; con el tipo de guerra
// (0x576796d), el 432. Sin rand().
TEST(GameDialogDrawingTest, TheWarRequestNamesTheRivalAndAsksByWarType)
{
    for (const std::int32_t map : {Lorencia, Devias})
    {
        SCOPED_TRACE(map);
        auto tested = std::make_unique<DrawingDialogs>(map);
        Client::Scenes::WorldRuntime& runtime = tested->world.Runtime();
        runtime.dialogs.current = 0x7f;
        runtime.guild.rival = {'T', 'e', 'r', 'o', 's', '\0', 'x', 'x', '\0'};
        tested->mouse = {0xea, 0x62};

        ASSERT_TRUE(tested->Draw().HasValue());
        const std::vector<DrawnQuad> quads = QuadsOf(tested->sprites);
        ASSERT_EQ(quads.size(), 3u);
        EXPECT_EQ(quads[0].slot, 0xf0);
        EXPECT_EQ(quads[0].x, 213.0f);
        EXPECT_EQ(quads[0].y, 60.0f);
        EXPECT_EQ(quads[1].slot, 0xf2);
        EXPECT_EQ(quads[1].x, 234.0f);
        EXPECT_EQ(quads[1].y, 98.0f);
        EXPECT_EQ(quads[2].slot, 0xf3);
        EXPECT_EQ(quads[2].x, 334.0f);
        EXPECT_EQ(tested->Texts(), (std::vector<std::string>{"guerra de Teros", "aceptas?"}));
        EXPECT_EQ(tested->text.texts[0].x, CenteredX("guerra de Teros"));
        EXPECT_EQ(tested->text.texts[0].y, 75 - 6);
        EXPECT_EQ(tested->text.texts[0].style.font, Client::Render::Text::FontKind::Normal);
        EXPECT_EQ(tested->text.texts[1].x, CenteredX("aceptas?"));
        EXPECT_EQ(tested->text.texts[1].y, 75 + 6);
        EXPECT_EQ(tested->sprites.Blend(), BlendMode::Alpha);

        runtime.guild.warType = 1;
        tested->sprites.Begin(Screen640);
        tested->text.texts.clear();
        tested->mouse = {0x14e, 0x62};
        ASSERT_TRUE(tested->Draw().HasValue());
        EXPECT_EQ(QuadsOf(tested->sprites)[1].slot, 0xf1);
        EXPECT_EQ(QuadsOf(tested->sprites)[2].slot, 0xf4);
        EXPECT_EQ(tested->Texts(), (std::vector<std::string>{"guerra de Teros", "aceptas la batalla?"}));
        EXPECT_EQ(tested->random.State(), Client::Crt::CrtRandom{}.State());
    }
}

// 0x630818 y 0x630af0..0x630cb8 (el 0x7d, CL9 hito 8): el marco del OK genérico; el EDIT del número ([0x5763508]) a la
// vista con +0x4c en 9, el fondo en 0, el tope 7, de 0x46 × 0xe en (0xf3, 0x62) y con el foco (0x4682a0: 0x5762ec0 con
// su id); en la mezcla normal, el cuadro negro de +0x4c, el bitmap y los '*' (el cursor, al final, recortado); en la
// opaca, el sí (0xf1, o 0xf2 con el mouse en [0x143, 0x16b) × [0x62, 0x77)) en 323 y el no (0xf3, o 0xf4 en [0x175,
// 0x19d)) en 373, de 40 × 21. En la cola (0x631f36), el 427 y el 428 centrados en 75 − 6 y 75 + 6, en rojo, que queda.
// Sin rand().
TEST(GameDialogDrawingTest, TheLeaveNumberDrawsItsEditAndTheNarrowButtons)
{
    for (const std::int32_t map : {Lorencia, Devias})
    {
        SCOPED_TRACE(map);
        auto tested = std::make_unique<DrawingDialogs>(map);
        Client::Scenes::WorldRuntime& runtime = tested->world.Runtime();
        runtime.dialogs.current = 0x7d;
        runtime.ui.focusedList = -3;
        Client::UI::ChatInputState& input = runtime.chat.input;
        input.numberField.text.Type("12");
        tested->mouse = {0x16a, 0x76};

        ASSERT_TRUE(tested->Draw().HasValue());
        const Client::UI::OldEdit& field = input.numberField;
        EXPECT_EQ(field.base.state, 0);
        EXPECT_EQ(field.filter, 9);
        EXPECT_EQ(field.backgroundColor, 0u);
        EXPECT_EQ(field.base.x, 0xf3);
        EXPECT_EQ(field.base.y, 0x62);
        EXPECT_EQ(field.base.width, 0x46);
        EXPECT_EQ(field.base.height, 0xe);
        EXPECT_EQ(input.focus, Client::UI::ChatFocus::NumberField);
        EXPECT_EQ(runtime.ui.focusedList, Client::UI::NumberFieldId);
        EXPECT_EQ(field.draws, 1);

        const std::vector<DrawnQuad> quads = QuadsOf(tested->sprites);
        ASSERT_EQ(quads.size(), 5u);
        EXPECT_EQ(quads[0].slot, 0xf0);
        EXPECT_EQ(quads[0].x, 213.0f);
        EXPECT_EQ(quads[0].y, 60.0f);
        EXPECT_FALSE(quads[1].slot.has_value());
        EXPECT_EQ(quads[1].x, 243.0f);
        EXPECT_EQ(quads[1].y, 98.0f);
        EXPECT_EQ(quads[1].blend, BlendMode::Alpha);
        EXPECT_EQ(quads[1].color, (Color{.red = 0.0f, .green = 0.0f, .blue = 0.0f, .alpha = 1.0f}));
        EXPECT_FALSE(quads[2].slot.has_value());
        EXPECT_EQ(quads[2].color.alpha, 0.0f);
        // El cursor va en la columna 3 + 12 + 1 de un bitmap de ftol(3 + 12) = 15: al final del texto queda recortado
        // (el editor XP de UI::OldDraw::DrawTheOldEdit, con el margen de 3 antes del texto).
        EXPECT_EQ(quads[3].slot, 0xf2);
        EXPECT_EQ(quads[3].x, 323.0f);
        EXPECT_EQ(quads[3].y, 98.0f);
        EXPECT_EQ(quads[3].blend, BlendMode::Opaque);
        EXPECT_EQ(quads[4].slot, 0xf3);
        EXPECT_EQ(quads[4].x, 373.0f);

        EXPECT_EQ(tested->Texts(), (std::vector<std::string>{"**", "nro", "seguro?"}));
        EXPECT_EQ(tested->text.texts[0].x, 0xf3 + 3);
        EXPECT_EQ(tested->text.texts[1].x, CenteredX("nro"));
        EXPECT_EQ(tested->text.texts[1].y, 75 - 6);
        EXPECT_EQ(tested->text.texts[1].style.colors.text, 0xff0000ffu);
        EXPECT_EQ(tested->text.texts[2].x, CenteredX("seguro?"));
        EXPECT_EQ(tested->text.texts[2].y, 75 + 6);
        EXPECT_EQ(runtime.hud.textColors.text, 0xff0000ffu);
        EXPECT_EQ(tested->sprites.Blend(), BlendMode::Alpha);

        // El tope 7 (EM_LIMITTEXT) frena lo que se teclea desde ahora. Cada WM_CHAR pasa por la subclase (0x467b10),
        // que pone +0x80 en 0 (0x467b9d) antes de dárselo al EDIT.
        Client::UI::TypeInTheChat(input, "345678");
        EXPECT_EQ(input.numberField.text.Value(), "1234567");
        EXPECT_EQ(input.numberField.draws, 0);

        // Un dibujo intermedio no cuenta el parpadeo; fuera del sí (x 0x16b) y en el no, 0xf1 y 0xf4.
        tested->sprites.Begin(Screen640);
        tested->text.texts.clear();
        tested->mouse = {0x16b, 0x62};
        ASSERT_TRUE(tested->Draw(true).HasValue());
        EXPECT_EQ(input.numberField.draws, 0);
        EXPECT_EQ(QuadsOf(tested->sprites)[3].slot, 0xf1);
        tested->sprites.Begin(Screen640);
        tested->mouse = {0x175, 0x62};
        ASSERT_TRUE(tested->Draw().HasValue());
        EXPECT_EQ(input.numberField.draws, 1);
        EXPECT_EQ(QuadsOf(tested->sprites)[4].slot, 0xf4);
        EXPECT_EQ(tested->random.State(), Client::Crt::CrtRandom{}.State());
    }
}

// 0x467f30 (EM_LIMITTEXT por SendMessageA) en cada frame del 0x7d y del 0x73 pasa por la subclase 0x467b10: con una
// flecha abajo, +0x80 en 0 antes de que 0x468320 mire el cursor, que se ve desde el primer frame (con 13, apagado).
TEST(GameDialogDrawingTest, AHeldArrowLightsTheDialogCaretAtOnce)
{
    for (const std::int32_t dialog : {0x7d, 0x73})
    {
        SCOPED_TRACE(dialog);
        auto tested = std::make_unique<DrawingDialogs>(Lorencia);
        Client::Scenes::WorldRuntime& runtime = tested->world.Runtime();
        runtime.dialogs.current = dialog;
        Client::UI::ChatInputState& input = runtime.chat.input;
        Client::UI::OldEdit& edit = dialog == 0x7d ? input.numberField : input.dialogEdit;
        // El cursor al principio: al final del texto quedaría fuera del bitmap.
        edit.text.Type("12");
        edit.text.SetCursor(0);
        edit.draws = 13;
        tested->arrowHeld = true;

        ASSERT_TRUE(tested->Draw().HasValue());
        EXPECT_EQ(edit.draws, 1);
        EXPECT_TRUE(std::ranges::any_of(tested->text.texts, [](const Client::Tests::RecordedText& text)
                                        { return text.columns == 1 && text.style.colors.background == 0xffc8c8c8u; }));

        tested->sprites.Begin(Screen640);
        tested->text.texts.clear();
        edit.draws = 13;
        tested->arrowHeld = false;
        ASSERT_TRUE(tested->Draw().HasValue());
        EXPECT_EQ(edit.draws, 14);
        EXPECT_FALSE(std::ranges::any_of(tested->text.texts, [](const Client::Tests::RecordedText& text)
                                         { return text.columns == 1 && text.style.colors.background == 0xffc8c8c8u; }));
    }
}
// 0x6304e1 → 0x471ae0 (CL9 hito 7): la caja de confirmación 0x9c con dos renglones, de (12 + 2) de alto en 640: el
// marco (0xfc en y 120, 0xfb desde 125 con h + 40 de alto y 0xfc en h + 165), el sí en (255, h + 140) con el mouse en
// el borde de abajo y la x del borde derecho (0xf2), el no sin él (0xf3), y los renglones centrados desde 0x87 de 14 en
// 14. Todo sin mezcla, y la normal al final.
TEST(GameDialogDrawingTest, TheConfirmBoxDrawsItsTwoButtonsAndItsRows)
{
    for (const std::int32_t map : {Lorencia, Devias})
    {
        SCOPED_TRACE(map);
        auto tested = std::make_unique<DrawingDialogs>(map);
        Client::Scenes::WorldRuntime& runtime = tested->world.Runtime();
        runtime.dialogs.current = 0x9c;
        runtime.social.confirmBox = {.id = 1000, .type = 2, .lines = {"abrir la tienda con", "estos precios"}};
        constexpr int Height = 2 * (12 + 2);
        tested->mouse = {0x133, Height + 156};

        ASSERT_TRUE(tested->Draw().HasValue());
        const std::vector<DrawnQuad> quads = QuadsOf(tested->sprites);
        ASSERT_EQ(quads.size(), 5u);
        EXPECT_EQ(quads[0].slot, 0xfc);
        EXPECT_FLOAT_EQ(quads[0].x, 213.0f);
        EXPECT_FLOAT_EQ(quads[0].y, 120.0f);
        EXPECT_EQ(quads[1].slot, 0xfb);
        EXPECT_FLOAT_EQ(quads[1].y, 125.0f);
        EXPECT_EQ(quads[2].slot, 0xfc);
        EXPECT_FLOAT_EQ(quads[2].y, static_cast<float>(Height + 40 + 125));
        EXPECT_EQ(quads[3].slot, 0xf2);
        EXPECT_FLOAT_EQ(quads[3].x, 255.0f);
        EXPECT_FLOAT_EQ(quads[3].y, static_cast<float>(Height + 140));
        EXPECT_EQ(quads[4].slot, 0xf3);
        EXPECT_FLOAT_EQ(quads[4].x, 330.0f);
        EXPECT_FLOAT_EQ(quads[4].y, static_cast<float>(Height + 140));
        for (const DrawnQuad& quad : quads)
            EXPECT_EQ(quad.blend, BlendMode::Opaque);

        ASSERT_EQ(tested->Texts(), (std::vector<std::string>{"abrir la tienda con", "estos precios"}));
        EXPECT_EQ(tested->text.texts[0].x, CenteredX("abrir la tienda con"));
        EXPECT_EQ(tested->text.texts[0].y, 0x87);
        EXPECT_EQ(tested->text.texts[1].x, CenteredX("estos precios"));
        EXPECT_EQ(tested->text.texts[1].y, 0x87 + 14);
        EXPECT_EQ(tested->text.texts[1].style.font, Client::Render::Text::FontKind::Normal);
        EXPECT_EQ(tested->text.texts[1].style.colors.text, 0xffffffffu);
        EXPECT_EQ(tested->sprites.Blend(), BlendMode::Alpha);
        EXPECT_EQ(runtime.dialogs.current, 0x9c);
    }
}

// Los bordes del mouse: el no con la x de 0x17e y la y de h + 140; un píxel más abajo, ninguno.
TEST(GameDialogDrawingTest, TheConfirmBoxLightsTheButtonUnderTheMouse)
{
    for (const std::int32_t map : {Lorencia, Devias})
    {
        SCOPED_TRACE(map);
        auto tested = std::make_unique<DrawingDialogs>(map);
        Client::Scenes::WorldRuntime& runtime = tested->world.Runtime();
        runtime.dialogs.current = 0x9c;
        runtime.social.confirmBox = {.id = 1000, .type = 2, .lines = {"una"}};
        constexpr int Height = 12 + 2;
        tested->mouse = {0x17e, Height + 140};
        ASSERT_TRUE(tested->Draw().HasValue());
        std::vector<DrawnQuad> quads = QuadsOf(tested->sprites);
        ASSERT_EQ(quads.size(), 5u);
        EXPECT_EQ(quads[3].slot, 0xf1);
        EXPECT_EQ(quads[4].slot, 0xf4);

        tested->sprites.Begin(Screen640);
        tested->mouse = {0x17e, Height + 157};
        ASSERT_TRUE(tested->Draw().HasValue());
        quads = QuadsOf(tested->sprites);
        ASSERT_EQ(quads.size(), 5u);
        EXPECT_EQ(quads[3].slot, 0xf1);
        EXPECT_EQ(quads[4].slot, 0xf3);
    }
}

// Con un botón, el sí en (295, h + 140), con el mouse de 0x127 a 0x15b; con otro tipo, ninguno.
TEST(GameDialogDrawingTest, TheConfirmBoxWithOneButtonOrNone)
{
    for (const std::int32_t map : {Lorencia, Devias})
    {
        SCOPED_TRACE(map);
        auto tested = std::make_unique<DrawingDialogs>(map);
        Client::Scenes::WorldRuntime& runtime = tested->world.Runtime();
        runtime.dialogs.current = 0x9c;
        runtime.social.confirmBox = {.id = 7, .type = 1, .lines = {"una"}};
        constexpr int Height = 12 + 2;
        tested->mouse = {0x127, Height + 150};
        ASSERT_TRUE(tested->Draw().HasValue());
        std::vector<DrawnQuad> quads = QuadsOf(tested->sprites);
        ASSERT_EQ(quads.size(), 4u);
        EXPECT_EQ(quads[3].slot, 0xf2);
        EXPECT_FLOAT_EQ(quads[3].x, 295.0f);
        EXPECT_FLOAT_EQ(quads[3].y, static_cast<float>(Height + 140));

        tested->sprites.Begin(Screen640);
        tested->mouse = {0x126, Height + 150};
        ASSERT_TRUE(tested->Draw().HasValue());
        quads = QuadsOf(tested->sprites);
        ASSERT_EQ(quads.size(), 4u);
        EXPECT_EQ(quads[3].slot, 0xf1);

        tested->sprites.Begin(Screen640);
        tested->text.texts.clear();
        runtime.social.confirmBox = {.id = 7, .type = 0, .lines = {}};
        ASSERT_TRUE(tested->Draw().HasValue());
        quads = QuadsOf(tested->sprites);
        ASSERT_EQ(quads.size(), 3u);
        EXPECT_FLOAT_EQ(quads[2].y, 165.0f);
        EXPECT_TRUE(tested->text.texts.empty());
    }
}

// Los quads sin textura de un lote: los rectángulos de color (el fondo del EDIT, ColorQuad).
std::vector<DrawnQuad> ColorQuadsOf(const std::vector<DrawnQuad>& quads)
{
    std::vector<DrawnQuad> colored;
    std::ranges::copy_if(quads, std::back_inserter(colored), [](const DrawnQuad& quad) { return !quad.slot; });
    return colored;
}

// 0x73 (0x630818..0x6308c6): el marco del OK genérico, sin el botón, y el EDIT de [0x5763504] rearmado en el frame
// (a la vista, +0x4c en 9, el tope de 8, 0x46 × 0xe en (0x11b, 0x5f)) con el foco y su dibujo: el negro del filtro 8
// y el fondo. Después, en la cola (0x631c91), el texto por 0x78abacc: 420 con 0, 421 con 1 y 422 con otro, en y =
// 0x4b.
TEST(GameDialogDrawingTest, TheMoneyDialogDrawsItsFrameItsEditAndItsText)
{
    for (const std::int32_t map : {Lorencia, Devias})
    {
        SCOPED_TRACE(map);
        for (const auto& [mark, expected] : {std::pair<std::int32_t, std::string>{0, "zen al almacen"},
                                             std::pair<std::int32_t, std::string>{1, "zen del almacen"},
                                             std::pair<std::int32_t, std::string>{-1, "zen al comercio"}})
        {
            SCOPED_TRACE(mark);
            auto tested = std::make_unique<DrawingDialogs>(map);
            Client::Scenes::WorldRuntime& runtime = tested->world.Runtime();
            runtime.dialogs.current = 0x73;
            runtime.social.trade.field78abacc = mark;
            Client::UI::OldEdit& edit = runtime.chat.input.dialogEdit;
            edit.text.SetText("150");
            edit.base.state = Client::UI::HiddenWindow;
            runtime.chat.input.focus = Client::UI::ChatFocus::Game;
            tested->mouse = {0x11c, 0x76};

            ASSERT_TRUE(tested->Draw().HasValue());
            EXPECT_EQ(edit.base.state, Client::UI::ShownWindow);
            EXPECT_EQ(edit.filter, 9);
            EXPECT_EQ(edit.base.x, 0x11b);
            EXPECT_EQ(edit.base.y, 0x5f);
            EXPECT_EQ(edit.base.width, 0x46);
            EXPECT_EQ(edit.base.height, 0xe);
            EXPECT_EQ(edit.draws, 1);
            EXPECT_EQ(runtime.chat.input.focus, Client::UI::ChatFocus::Dialog);
            EXPECT_EQ(runtime.ui.focusedList, Client::UI::DialogFieldId);

            const std::vector<DrawnQuad> quads = QuadsOf(tested->sprites);
            ASSERT_EQ(quads.size(), 3u);
            EXPECT_EQ(quads[0].slot, 0xf0);
            EXPECT_EQ(quads[0].blend, BlendMode::Opaque);
            EXPECT_FLOAT_EQ(quads[0].x, 213.0f);
            EXPECT_FLOAT_EQ(quads[0].y, 60.0f);
            const std::vector<DrawnQuad> colored = ColorQuadsOf(quads);
            ASSERT_EQ(colored.size(), 2u);
            EXPECT_FLOAT_EQ(colored[0].x, static_cast<float>(0x11b));
            EXPECT_FLOAT_EQ(colored[0].y, static_cast<float>(0x5f));

            ASSERT_FALSE(tested->text.texts.empty());
            EXPECT_EQ(tested->text.texts.front().text, "150");
            const Client::Tests::RecordedText& last = tested->text.texts.back();
            EXPECT_EQ(last.text, expected);
            EXPECT_EQ(last.x, CenteredX(expected));
            EXPECT_EQ(last.y, 0x4b);
            EXPECT_EQ(tested->sprites.Blend(), BlendMode::Alpha);

            // El tope de lo tecleado es 8 (la vtable + 0x3c).
            edit.text.SetText({});
            Client::UI::TypeInTheEdit(edit, "1234567890");
            EXPECT_EQ(edit.text.Value(), "12345678");
        }
    }
}

// En un dibujo intermedio de MuRet, el EDIT se arma y se dibuja sobre una copia: ni su estado ni el foco cambian.
TEST(GameDialogDrawingTest, TheProjectingPassLeavesTheDialogEdit)
{
    auto tested = std::make_unique<DrawingDialogs>(Lorencia);
    Client::Scenes::WorldRuntime& runtime = tested->world.Runtime();
    runtime.dialogs.current = 0x73;
    Client::UI::OldEdit& edit = runtime.chat.input.dialogEdit;
    edit.base.state = Client::UI::HiddenWindow;
    runtime.chat.input.focus = Client::UI::ChatFocus::Game;
    ASSERT_TRUE(tested->Draw(true).HasValue());
    EXPECT_EQ(edit.base.state, Client::UI::HiddenWindow);
    EXPECT_EQ(edit.draws, 0);
    EXPECT_EQ(runtime.chat.input.focus, Client::UI::ChatFocus::Game);
}

// 0x91 (0x6304f2..0x630813): el marco (0xfc en (213, 100), 0xfb en 105 y 145, 0xfc en 185), el EDIT que armó
// 0x5eba70 con el foco y su dibujo, y sin mezcla el OK (0xf2 con el mouse en el borde de abajo a la derecha,
// (0x133, 0xb0)) en (255, 160) y el cancelar (0xf3) en (330, 160). El texto (0x631d2f): 1128 con el título y 1129 con
// el precio, en y = 0x73; con otro modo, ninguno.
TEST(GameDialogDrawingTest, TheShopDialogDrawsItsBoxItsEditAndItsButtons)
{
    for (const std::int32_t map : {Lorencia, Devias})
    {
        SCOPED_TRACE(map);
        for (const auto& [mode, expected] :
             {std::pair<std::int32_t, std::string>{1, "titulo de la tienda"},
              std::pair<std::int32_t, std::string>{2, "precio del item"}, std::pair<std::int32_t, std::string>{0, ""}})
        {
            SCOPED_TRACE(mode);
            auto tested = std::make_unique<DrawingDialogs>(map);
            Client::Scenes::WorldRuntime& runtime = tested->world.Runtime();
            runtime.dialogs.current = 0x91;
            runtime.social.personalShop.dialogMode = mode;
            Client::UI::OldEdit& edit = runtime.chat.input.dialogEdit;
            edit.base.state = Client::UI::ShownWindow;
            edit.filter = 0xc;
            edit.backgroundColor = 0;
            edit.base.x = 0xfd;
            edit.base.y = 0x8a;
            edit.base.width = 0x85;
            edit.base.height = 0xa;
            edit.text.SetText("Puesto");
            runtime.chat.input.focus = Client::UI::ChatFocus::Game;
            tested->mouse = {0x133, 0xb0};

            ASSERT_TRUE(tested->Draw().HasValue());
            EXPECT_EQ(runtime.chat.input.focus, Client::UI::ChatFocus::Dialog);
            EXPECT_EQ(runtime.ui.focusedList, Client::UI::DialogFieldId);
            EXPECT_EQ(edit.base.x, 0xfd);
            EXPECT_EQ(edit.draws, 1);
            const std::vector<DrawnQuad> quads = QuadsOf(tested->sprites);
            ASSERT_EQ(quads.size(), 8u);
            EXPECT_EQ(quads[0].slot, 0xfc);
            EXPECT_FLOAT_EQ(quads[0].x, 213.0f);
            EXPECT_FLOAT_EQ(quads[0].y, 100.0f);
            EXPECT_EQ(quads[1].slot, 0xfb);
            EXPECT_FLOAT_EQ(quads[1].y, 105.0f);
            EXPECT_EQ(quads[2].slot, 0xfb);
            EXPECT_FLOAT_EQ(quads[2].y, 145.0f);
            EXPECT_EQ(quads[3].slot, 0xfc);
            EXPECT_FLOAT_EQ(quads[3].y, 185.0f);
            EXPECT_FALSE(quads[4].slot);
            EXPECT_FLOAT_EQ(quads[4].x, static_cast<float>(0xfd));
            EXPECT_FLOAT_EQ(quads[4].y, static_cast<float>(0x8a));
            EXPECT_FALSE(quads[5].slot);
            EXPECT_EQ(quads[6].slot, 0xf2);
            EXPECT_FLOAT_EQ(quads[6].x, 255.0f);
            EXPECT_FLOAT_EQ(quads[6].y, 160.0f);
            EXPECT_EQ(quads[6].blend, BlendMode::Opaque);
            EXPECT_EQ(quads[7].slot, 0xf3);
            EXPECT_FLOAT_EQ(quads[7].x, 330.0f);
            EXPECT_FLOAT_EQ(quads[7].y, 160.0f);
            EXPECT_EQ(quads[7].blend, BlendMode::Opaque);

            ASSERT_FALSE(tested->text.texts.empty());
            EXPECT_EQ(tested->text.texts.front().text, "Puesto");
            const Client::Tests::RecordedText& last = tested->text.texts.back();
            if (expected.empty())
                EXPECT_EQ(last.text, "Puesto");
            else
            {
                EXPECT_EQ(last.text, expected);
                EXPECT_EQ(last.x, CenteredX(expected));
                EXPECT_EQ(last.y, 0x73);
            }
            EXPECT_EQ(tested->sprites.Blend(), BlendMode::Alpha);
        }
    }
}

// Los bordes del mouse del 0x91: el cancelar con (0x14a, 0xa0) es 0xf4 y el OK, 0xf1; un píxel a la izquierda del
// cancelar o arriba, ninguno.
TEST(GameDialogDrawingTest, TheShopDialogLightsTheButtonUnderTheMouse)
{
    for (const std::int32_t map : {Lorencia, Devias})
    {
        SCOPED_TRACE(map);
        for (const auto& [mouse, ok, cancel] :
             {std::tuple<Client::UI::Point, std::uint16_t, std::uint16_t>{{0x14a, 0xa0}, 0xf1, 0xf4},
              std::tuple<Client::UI::Point, std::uint16_t, std::uint16_t>{{0x149, 0xa0}, 0xf1, 0xf3},
              std::tuple<Client::UI::Point, std::uint16_t, std::uint16_t>{{0xff, 0x9f}, 0xf1, 0xf3},
              std::tuple<Client::UI::Point, std::uint16_t, std::uint16_t>{{0xff, 0xa0}, 0xf2, 0xf3}})
        {
            auto tested = std::make_unique<DrawingDialogs>(map);
            Client::Scenes::WorldRuntime& runtime = tested->world.Runtime();
            runtime.dialogs.current = 0x91;
            runtime.chat.input.dialogEdit.base.state = Client::UI::ShownWindow;
            tested->mouse = mouse;
            ASSERT_TRUE(tested->Draw().HasValue());
            const std::vector<DrawnQuad> quads = QuadsOf(tested->sprites);
            ASSERT_GE(quads.size(), 2u);
            EXPECT_EQ(quads[quads.size() - 2].slot, ok) << mouse.x << "," << mouse.y;
            EXPECT_EQ(quads.back().slot, cancel) << mouse.x << "," << mouse.y;
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
    Client::Tables::DialogTable table(Client::Tables::DialogCount);
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
            runtime.social.trade.warningKind = expected.choice;
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
                runtime.social.trade.warningKind = 2;
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
        runtime.social.trade.warningKind = 2;
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
        runtime.social.trade.warningKind = 3;
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
            runtime.social.trade.warningKind = 3;
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
        runtime.social.trade.warningKind = 6;
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

// Los diálogos sin dibujo todavía cortan con la ola de su paso.
TEST(GameDialogDrawingTest, TheOtherDialogsAreCut)
{
    DrawingDialogs tested(Lorencia);
    for (const auto& [id, address, wave] : std::to_array<std::tuple<std::int32_t, std::string, std::string>>({
             {0x76, "0x62db60", "CL9.4"},
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
Client::Tables::TextTable JewelTexts()
{
    return Client::Tests::MakeTextTable({{188, "ok"},
                                         {228, "listo"},
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

// 0x62f68f..0x62f9c1, en los tres mapas: con el alma elegida, 7 filas, el 1821 con las casillas vacías (0x402de0) y
// el 1822 en x = 0xe4 con 0xc8c8c8c8; la lista de 0x6e5660 en (0xe4, 0x6d + 0xf0) con su marco, su barra, el velo del
// elegido y sus filas; el OK (0xf0 de 80 × 25 en (234,5; 359), con la mezcla apagada que dejó la lista) con el 188 en
// (1; 1; 0,2) y 0xfffff0c8, y el cancelar en blanco con el mouse encima, los dos en negrita en y = 367. Después la
// normal, el texto en blanco y el blanco, con el que sale el título 1800 de la cola.
TEST(GameDialogDrawingTest, TheJewelUnmixWithAKindDrawsTheListAndItsButtons)
{
    constexpr Color White{1.0f, 1.0f, 1.0f, 1.0f};
    for (const std::int32_t map : {Lorencia, Devias, Noria})
    {
        SCOPED_TRACE(map);
        auto tested = std::make_unique<DrawingDialogs>(map);
        tested->texts = JewelTexts();
        auto& runtime = tested->world.Runtime();
        runtime.dialogs.current = 0xa4;
        runtime.npcWindows.jewels.kind = 1;
        for (const std::size_t cell : {3u, 9u, 63u})
            tested->world.Inventory().cells[cell].id = 0xffff;
        Client::Scenes::JewelBundleList& list = runtime.npcWindows.jewels.bundles;
        Client::Scenes::AddTheBundle(list, 8, 0);
        Client::Scenes::AddTheBundle(list, 5, 2);
        list.selection = 1;
        tested->mouse = {330, 380};
        ASSERT_TRUE(tested->Draw().HasValue());
        EXPECT_EQ(list.x, 0xe4);
        EXPECT_EQ(list.y, 0x15d);
        const std::vector<DrawnQuad> quads = QuadsOf(tested->sprites);
        ASSERT_EQ(quads.size(), 22u);
        EXPECT_FLOAT_EQ(quads[8].y, 405.0f);
        EXPECT_FALSE(quads[11].slot.has_value());
        EXPECT_FLOAT_EQ(quads[11].x, 227.0f);
        EXPECT_EQ(quads[12].slot, 0x649);
        EXPECT_FALSE(quads[19].slot.has_value());
        EXPECT_FLOAT_EQ(quads[19].y, 256.0f - 3.0f);
        EXPECT_EQ(quads[20].slot, 0xf0);
        EXPECT_FLOAT_EQ(quads[20].x, 234.5f);
        EXPECT_FLOAT_EQ(quads[20].y, 359.0f);
        EXPECT_EQ(quads[20].blend, BlendMode::Opaque);
        EXPECT_FLOAT_EQ(quads[20].color.blue, 1.0f);
        EXPECT_EQ(quads[21].slot, 0xf0);
        EXPECT_FLOAT_EQ(quads[21].x, 324.5f);
        EXPECT_FLOAT_EQ(quads[21].color.red, 1.0f);
        const std::vector<std::string> expected{"bendicion", "alma", "libres 3", "paquetes", "alma,  30",
                                                "alma,  10", "ok",   "cancelar", "deshacer"};
        ASSERT_EQ(tested->Texts(), expected);
        const auto& drawn = tested->text.texts;
        EXPECT_EQ(drawn[4].x, 0xe4 + 6);
        EXPECT_EQ(drawn[4].y, 256);
        EXPECT_EQ(drawn[4].style.colors.text, 0xff000000u);
        EXPECT_EQ(drawn[4].style.font, Client::Render::Text::FontKind::Normal);
        EXPECT_EQ(drawn[5].style.colors.text, 0xffc8dce6u);
        EXPECT_EQ(drawn[6].x, CenteredAt(0x112, "ok"));
        EXPECT_EQ(drawn[6].y, 367);
        EXPECT_EQ(drawn[6].style.colors.text, 0xfffff0c8u);
        EXPECT_EQ(drawn[6].style.font, Client::Render::Text::FontKind::Bold);
        EXPECT_FLOAT_EQ(TextColorsOf(tested->sprites)[6].blue, 0.2f);
        EXPECT_EQ(drawn[7].x, CenteredAt(0x16c, "cancelar"));
        EXPECT_EQ(drawn[7].y, 367);
        EXPECT_EQ(drawn[7].style.font, Client::Render::Text::FontKind::Bold);
        EXPECT_EQ(drawn[8].y, 135);
        EXPECT_EQ(TextColorsOf(tested->sprites)[8], White);
    }
}

// 0x62f78a..0x62f807 y 0x62f8ec, en los tres mapas: sin elegido, el 188 en 0,5 con el color de texto que había (el
// 0xc8c8c8c8 de las notas), y el cancelar sin el mouse en 0,7. Sin filas, la lista solo dibuja su marco y la mezcla
// queda prendida para el OK.
TEST(GameDialogDrawingTest, TheJewelUnmixOkIsGreyWithoutAChoice)
{
    constexpr Color White{1.0f, 1.0f, 1.0f, 1.0f};
    for (const std::int32_t map : {Lorencia, Devias, Noria})
    {
        SCOPED_TRACE(map);
        auto tested = std::make_unique<DrawingDialogs>(map);
        tested->texts = JewelTexts();
        auto& runtime = tested->world.Runtime();
        runtime.dialogs.current = 0xa4;
        runtime.npcWindows.jewels.kind = 0;
        ASSERT_TRUE(tested->Draw().HasValue());
        const std::vector<DrawnQuad> quads = QuadsOf(tested->sprites);
        ASSERT_EQ(quads.size(), 21u);
        EXPECT_EQ(quads[19].blend, BlendMode::Alpha);
        EXPECT_FLOAT_EQ(quads[20].color.red, 0.7f);
        const auto& drawn = tested->text.texts;
        ASSERT_EQ(drawn.size(), 7u);
        EXPECT_EQ(drawn[4].text, "ok");
        EXPECT_EQ(drawn[4].style.colors.text, 0xc8c8c8c8u);
        EXPECT_FLOAT_EQ(TextColorsOf(tested->sprites)[4].red, 0.5f);
        EXPECT_EQ(drawn[6].text, "deshacer");
        EXPECT_EQ(TextColorsOf(tested->sprites)[6], White);
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

// Un renglón de 0x7d36a98, como lo copia 0x633300 (strcpy).
void PutRow(Client::UI::DialogBoxState& box, std::size_t row, std::string_view text)
{
    std::ranges::copy(text, box.rows.begin() + static_cast<std::ptrdiff_t>(row * Client::UI::DialogRowBytes));
    box.rows[row * Client::UI::DialogRowBytes + text.size()] = '\0';
}

// 0x94 con el aviso 3 (0x630cbd y 0x632637): el marco desde y = 100 (0xfc, tres 0xfb y 0xfc en 225), los botones de
// 0x7d368b8 en (x + 213, y + 100) con su estado (el primero con el mouse, 0xf2), sin mezcla; los cuatro renglones en la
// negrita, centrados en 0x140 desde y = 0x73 de 0x12 en 0x12, en (1; 0,7; 0) y el cuarto en (1; 0,1; 0,1).
TEST(GameDialogDrawingTest, TheTradeWarningDrawsItsBoxAndItsRows)
{
    for (const std::int32_t map : {Lorencia, Devias})
    {
        SCOPED_TRACE(map);
        auto tested = std::make_unique<DrawingDialogs>(map);
        Client::Scenes::WorldRuntime& runtime = tested->world.Runtime();
        runtime.dialogs.current = 0x94;
        runtime.social.trade.warningKind = 3;
        Client::UI::DialogBoxState& box = runtime.dialogBox;
        box.buttons = {Client::UI::DialogButton{2, 0x15, 0x5a, 0x46, 0x15},
                       Client::UI::DialogButton{3, 0x78, 0x5a, 0x46, 0x15}};
        const std::vector<std::string> rows{"el otro", "cambio", "sus items", "cuidado"};
        for (std::size_t row = 0; row < rows.size(); ++row)
            PutRow(box, row, rows[row]);
        box.lineCount = 4;

        ASSERT_TRUE(tested->Draw().HasValue());
        const std::vector<DrawnQuad> quads = QuadsOf(tested->sprites);
        ASSERT_EQ(quads.size(), 7u);
        EXPECT_EQ(quads[0].slot, 0xfc);
        EXPECT_FLOAT_EQ(quads[0].x, 213.0f);
        EXPECT_FLOAT_EQ(quads[0].y, 100.0f);
        for (std::size_t row = 0; row < 3; ++row)
        {
            EXPECT_EQ(quads[1 + row].slot, 0xfb);
            EXPECT_FLOAT_EQ(quads[1 + row].y, 105.0f + 40.0f * static_cast<float>(row));
        }
        EXPECT_EQ(quads[4].slot, 0xfc);
        EXPECT_FLOAT_EQ(quads[4].y, 225.0f);
        EXPECT_EQ(quads[5].slot, 0xf2);
        EXPECT_FLOAT_EQ(quads[5].x, 213.0f + 0x15);
        EXPECT_FLOAT_EQ(quads[5].y, 100.0f + 0x5a);
        EXPECT_EQ(quads[6].slot, 0xf3);
        EXPECT_FLOAT_EQ(quads[6].x, 213.0f + 0x78);
        for (const DrawnQuad& quad : quads)
            EXPECT_EQ(quad.blend, BlendMode::Opaque);

        ASSERT_EQ(tested->Texts(), rows);
        for (std::size_t row = 0; row < rows.size(); ++row)
        {
            EXPECT_EQ(tested->text.texts[row].x, CenteredX(rows[row]));
            EXPECT_EQ(tested->text.texts[row].y, 0x73 + 0x12 * static_cast<int>(row));
            EXPECT_EQ(tested->text.texts[row].style.font, Client::Render::Text::FontKind::Bold);
        }
        const std::vector<Color> colors = TextColorsOf(tested->sprites);
        ASSERT_EQ(colors.size(), 4u);
        EXPECT_FLOAT_EQ(colors[0].green, 0.7f);
        EXPECT_FLOAT_EQ(colors[0].blue, 0.0f);
        EXPECT_FLOAT_EQ(colors[2].green, 0.7f);
        EXPECT_FLOAT_EQ(colors[3].red, 1.0f);
        EXPECT_FLOAT_EQ(colors[3].green, 0.1f);
        EXPECT_FLOAT_EQ(colors[3].blue, 0.1f);
        EXPECT_EQ(tested->sprites.Blend(), BlendMode::Alpha);
    }
}

// Con el aviso 5 (la compra, 0x633300(1, 1100, 0x94)), el renglón va centrado en 0x15e desde y = 100 + 0x1e; un
// botón sin estado (0) o con más de 4 no se dibuja.
TEST(GameDialogDrawingTest, TheBuyWarningCentersItsRowFurtherRight)
{
    auto tested = std::make_unique<DrawingDialogs>(Lorencia);
    Client::Scenes::WorldRuntime& runtime = tested->world.Runtime();
    runtime.dialogs.current = 0x94;
    runtime.social.trade.warningKind = 5;
    Client::UI::DialogBoxState& box = runtime.dialogBox;
    box.buttons = {Client::UI::DialogButton{0, 0x15, 0x5a, 0x46, 0x15},
                   Client::UI::DialogButton{5, 0x78, 0x5a, 0x46, 0x15}};
    PutRow(box, 0, "comprar?");
    box.lineCount = 1;
    ASSERT_TRUE(tested->Draw().HasValue());
    EXPECT_EQ(QuadsOf(tested->sprites).size(), 5u);
    ASSERT_EQ(tested->Texts(), (std::vector<std::string>{"comprar?"}));
    EXPECT_EQ(tested->text.texts[0].x, 0x15e - static_cast<int>(std::string_view("comprar?").size()) * LetterWidth / 2);
    EXPECT_EQ(tested->text.texts[0].y, 100 + 0x1e);
}

// El 0x99 del precio bajo (0x630cc4, el mismo marco desde y = 100) con el aviso 6 (0x5dc7e9): sus renglones centrados
// en 0x15e desde y = 100 + 0x1e, el primero en (1, 0, 0) con la negrita y el otro en blanco con la normal.
TEST(GameDialogDrawingTest, TheLowPriceWarningDrawsLikeTheWarning)
{
    auto tested = std::make_unique<DrawingDialogs>(Devias);
    Client::Scenes::WorldRuntime& runtime = tested->world.Runtime();
    runtime.dialogs.current = Client::UI::MoneyTextDialog;
    runtime.social.trade.warningKind = 6;
    Client::UI::DialogBoxState& box = runtime.dialogBox;
    box.buttons = {Client::UI::DialogButton{1, 0x15, 0x5a, 0x46, 0x15},
                   Client::UI::DialogButton{3, 0x78, 0x5a, 0x46, 0x15}};
    const std::vector<std::string> rows{"precio 1,000 zen", "vender?"};
    for (std::size_t row = 0; row < rows.size(); ++row)
        PutRow(box, row, rows[row]);
    box.lineCount = 2;
    ASSERT_TRUE(tested->Draw().HasValue());
    const std::vector<DrawnQuad> quads = QuadsOf(tested->sprites);
    ASSERT_EQ(quads.size(), 7u);
    EXPECT_FLOAT_EQ(quads[0].y, 100.0f);
    EXPECT_EQ(quads[5].slot, 0xf1);
    EXPECT_FLOAT_EQ(quads[5].y, 100.0f + 0x5a);
    EXPECT_EQ(quads[6].slot, 0xf3);
    ASSERT_EQ(tested->Texts(), rows);
    for (std::size_t row = 0; row < rows.size(); ++row)
    {
        EXPECT_EQ(tested->text.texts[row].x, 0x15e - static_cast<int>(rows[row].size()) * LetterWidth / 2);
        EXPECT_EQ(tested->text.texts[row].y, 100 + 0x1e + 0x12 * static_cast<int>(row));
    }
    EXPECT_EQ(tested->text.texts[0].style.font, Client::Render::Text::FontKind::Bold);
    EXPECT_EQ(tested->text.texts[1].style.font, Client::Render::Text::FontKind::Normal);
    const std::vector<Color> colors = TextColorsOf(tested->sprites);
    ASSERT_EQ(colors.size(), 2u);
    EXPECT_FLOAT_EQ(colors[0].red, 1.0f);
    EXPECT_FLOAT_EQ(colors[0].green, 0.0f);
    EXPECT_FLOAT_EQ(colors[1].green, 1.0f);
    EXPECT_FLOAT_EQ(colors[1].blue, 1.0f);
}
} // namespace

TEST(GameDialogDrawingTest, TheWarehouseConfirmationDrawsItsNumberEditAndBothInstructions)
{
    for (const std::int32_t map : {Lorencia, Devias, 3})
    {
        auto tested = std::make_unique<DrawingDialogs>(map);
        auto& runtime = tested->world.Runtime();
        runtime.dialogs.current = 0x95;
        runtime.chat.input.numberField.text.Type("12");
        tested->texts = Client::Tests::MakeTextTable({{443, "clave"}, {444, "confirmo"}});
        const auto before = tested->random;
        const auto drawn = tested->Draw();
        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
        EXPECT_EQ(tested->Texts(), (std::vector<std::string>{"**", "clave", "confirmo"}));
        EXPECT_EQ(tested->text.texts[1].y, 69);
        EXPECT_EQ(tested->text.texts[2].y, 81);
        EXPECT_EQ(tested->text.texts[1].style.colors.text, 0xff0000ffu);
        EXPECT_EQ(runtime.chat.input.numberField.base.x, 243);
        EXPECT_EQ(runtime.chat.input.numberField.base.y, 98);
        EXPECT_EQ(runtime.chat.input.numberField.draws, 1);
        EXPECT_EQ(runtime.ui.focusedList, Client::UI::NumberFieldId);
        const auto quads = QuadsOf(tested->sprites);
        ASSERT_EQ(quads.size(), 5u);
        EXPECT_EQ(quads[3].x, 323);
        EXPECT_EQ(quads[4].x, 373);
        EXPECT_EQ(tested->random.State(), before.State());
    }
}
