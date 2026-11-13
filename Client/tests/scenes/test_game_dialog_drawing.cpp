#include "Scenes/GameDialogDrawing.h"

#include "App/WindowState.h"
#include "Core/Expected.h"
#include "Data/Text/TextTable.h"
#include "Render/Models/Cloth.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/TextStyle.h"
#include "Scenes/ActorState.h"
#include "Scenes/EventMapObject.h"
#include "Scenes/GameHud.h"
#include "Scenes/GameMovementState.h"
#include "Scenes/SocialState.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/Dialogs.h"
#include "UI/GameDialogs.h"
#include "UI/GameUiState.h"
#include "UI/Widget.h"

#include "support/RecordingTextDrawer.h"
#include "support/SceneDataFiles.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
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
        {418, "de %s"},
        {419, "comerciar?"},
        {425, "entrar a la party?"},
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
        EXPECT_EQ(tested->random.State(), Client::Render::Models::CrtRandom{}.State());
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

// Los diálogos que no son de la CL8 cortan con la ola de su paso.
TEST(GameDialogDrawingTest, TheOtherDialogsAreCut)
{
    DrawingDialogs tested(Lorencia);
    for (const auto& [id, wave] : std::to_array<std::pair<std::int32_t, std::string>>(
             {{0x8d, "CL9.1"}, {0x73, "CL9 hito 7"}, {0x76, "CL9.4"}, {0x8e, "CL9.3"}, {0x15, "CL9"}}))
    {
        tested.world.Runtime().dialogs.current = id;
        const auto drawn = tested.Draw();
        ASSERT_FALSE(drawn.HasValue());
        EXPECT_NE(drawn.Error().find("0x62db60"), std::string::npos) << drawn.Error();
        EXPECT_NE(drawn.Error().find(wave), std::string::npos) << drawn.Error();
    }
}
} // namespace
