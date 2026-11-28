#include "Hud/Dialogs/GameDialogDrawing.h"

#include "Core/Expected.h"
#include "Core/PixelSize.h"
#include "Crt/CrtRandom.h"
#include "Tables/DialogTable.h"
#include "Tables/TextTable.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/TextStyle.h"
#include "Kernel/ActorState.h"
#include "Events/EventMapObject.h"
#include "Hud/GameHud.h"
#include "Guild/GuildState.h"
#include "ChaosMix/JewelBundleList.h"
#include "Npc/NpcWindowState.h"
#include "Social/SocialState.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Chat/ChatInput.h"
#include "Ui/Dialogs.h"
#include "Ui/GameDialogs.h"
#include "Ui/GameUiState.h"
#include "Ui/OldControls.h"
#include "Ui/OldWindowBase.h"
#include "Ui/Widget.h"
#include "Audio/SoundId.h"

#include "support/RecordingTextDrawer.h"
#include "support/SceneDataFiles.h"
#include "Chat/ChatRuntime.h"

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

// The game's dialogs in the drawing (0x62db60, CL8 milestone 6): the Esc menu, the options with the volume bar and the
// glow, the boxes with their buttons, their lines and the arrows of 0x8c, the generic OK and the stops. In 640 × 480,
// with made-up texts and the world in Lorencia (0) and in Devias (2).
namespace
{
using Client::Hud::GameHudContext;
using Client::Kernel::WorldState;
using Client::Render::BlendMode;
using Client::Render::Color;
using Client::Render::SpriteBatch;

constexpr Client::Core::PixelSize Screen640{640, 480};
constexpr std::int32_t Lorencia = 0;
constexpr std::int32_t Devias = 2;
constexpr std::int32_t Noria = 3;
// RecordingTextDrawer: 6 columns per letter.
constexpr int LetterWidth = Client::Tests::RecordingTextDrawer::LetterWidth;

// A batch quad: the texture, the blending, the color and the top left corner in 640 × 480.
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

// x = 0x140 − (cx / 2) of a text centered in 640 × 480.
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
        Client::Kernel::WorldRuntime& runtime = world.Runtime();
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
        return Client::Hud::Dialogs::DrawTheGameDialogs(context);
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
    std::optional<Client::Audio::SoundId> sound;
    SpriteBatch sprites;
    Client::Tests::RecordingTextDrawer text{sprites};
    Client::Ui::Point mouse{};
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

// 0x62db60 with no dialog: no blending and white, the text colors and the font, and normal blending at the end.
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

// 0x62dbcf: the 5 buttons, the mouse's one again in additive with (0.8, 0.6, 0.4), and the centered texts.
TEST(GameDialogDrawingTest, TheEscapeMenuDrawsItsButtonsAndItsTexts)
{
    for (const std::int32_t map : {Lorencia, Devias})
    {
        DrawingDialogs tested(map);
        tested.world.Runtime().dialogs.current = Client::Ui::EscapeMenuDialog;
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
        // 0x554180 leaves the size of the last text.
        EXPECT_EQ(tested.world.Runtime().hud.arrowsTextWidth, 6 * LetterWidth);
        EXPECT_EQ(tested.sprites.Blend(), BlendMode::Alpha);
    }
}

// 0x62df3f: the rows, the keys with two spaces, the volume bar with its corner for the step, and the glow.
TEST(GameDialogDrawingTest, TheOptionsDrawTheVolumeBarAndTheGlow)
{
    for (const std::int32_t map : {Lorencia, Devias})
    {
        DrawingDialogs tested(map);
        Client::Kernel::WorldRuntime& runtime = tested.world.Runtime();
        runtime.dialogs.current = Client::Ui::OptionsDialog;
        runtime.ui.autoAttack = true;
        runtime.chat.noticeSound = false;
        runtime.chat.slidesOn = true;
        runtime.ui.volume = 3;
        runtime.ui.glowDetail = 1;

        ASSERT_TRUE(tested.Draw().HasValue());
        const std::vector<std::string> expected{"opciones",     "ataque  On", "aviso  Off", "volumen",
                                                "carteles  On", "brillo",     "listo"};
        EXPECT_EQ(tested.Texts(), expected);
        // The volume at x = 0x140 − (42 + 0x36) / 2 and the bar at x + 0x144 + 42.
        const int volumeX = -((7 * LetterWidth + 0x36) / 2);
        EXPECT_EQ(tested.text.texts[3].x, volumeX + 0x140);
        EXPECT_EQ(tested.text.texts[3].y, 0x7d);
        EXPECT_EQ(runtime.ui.volumeBarX, volumeX + 0x144 + 7 * LetterWidth);
        EXPECT_EQ(runtime.ui.volumeBarY, 0x7f);

        const std::vector<DrawnQuad> quads = QuadsOf(tested.sprites);
        // 7 rows, 3 of the bar, 2 of the glow's frame, 5 boxes and the digits 5, 7, 9, 1 1 and 1 3.
        ASSERT_EQ(quads.size(), 7u + 3u + 2u + 5u + 7u);
        EXPECT_FLOAT_EQ(quads[5].y, 180.0f);
        EXPECT_FLOAT_EQ(quads[6].y, 232.0f);
        const DrawnQuad& fill = quads[9];
        EXPECT_FALSE(fill.slot.has_value());
        EXPECT_FLOAT_EQ(fill.x, static_cast<float>(runtime.ui.volumeBarX + 1));
        // The boxes up to 1 lit; the digits of the unlit ones, in color.
        EXPECT_FLOAT_EQ(quads[12].color.red, 0.8f);
        EXPECT_FLOAT_EQ(quads[13].color.red, 0.8f);
        EXPECT_FLOAT_EQ(quads[14].color.red, 0.0f);
        EXPECT_EQ(quads[17].slot, 1);
        EXPECT_FLOAT_EQ(quads[17].color.red, 0.0f);
        EXPECT_FLOAT_EQ(quads[19].color.red, 0.8f);
    }
}

TEST(GameDialogDrawingTest, AnIntermediateDrawDoesNotMoveTheVolumeBar)
{
    DrawingDialogs tested(Devias);
    Client::Kernel::WorldRuntime& runtime = tested.world.Runtime();
    runtime.dialogs.current = Client::Ui::OptionsDialog;
    ASSERT_TRUE(tested.Draw(true).HasValue());
    EXPECT_EQ(runtime.ui.volumeBarX, 0);
    EXPECT_EQ(runtime.ui.volumeBarY, 0);
}

// 0x630e14: the short frame of 0x8a, the button with its state and the lines centered from y = ((7 − 2) · 0x12) / 2
// + 0x4c.
TEST(GameDialogDrawingTest, TheMessageBoxDrawsItsFrameButtonAndRows)
{
    for (const std::int32_t map : {Lorencia, Devias})
    {
        DrawingDialogs tested(map);
        Client::Kernel::WorldRuntime& runtime = tested.world.Runtime();
        Client::Ui::OpenMessageBox(runtime.dialogBox, runtime.dialogs,
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

// 0x631297: the arrows of 0x8c, the one at the limit gray; the mouse's one spends the click (Q-C553), except in an
// intermediate drawing.
TEST(GameDialogDrawingTest, ThePagedArrowsTakeTheClick)
{
    DrawingDialogs tested(Lorencia);
    Client::Kernel::WorldRuntime& runtime = tested.world.Runtime();
    runtime.dialogs.current = Client::Ui::PagedDialog;
    runtime.dialogBox.page = 0;
    runtime.dialogBox.lastPage = 2;
    runtime.mouse.pendingClick = true;
    tested.mouse = {0x180, 0xd0};

    ASSERT_TRUE(tested.Draw(true).HasValue());
    EXPECT_TRUE(runtime.mouse.pendingClick);
    ASSERT_TRUE(tested.Draw().HasValue());
    EXPECT_FALSE(runtime.mouse.pendingClick);

    const std::vector<DrawnQuad> quads = QuadsOf(tested.sprites);
    // The frame (6) and the two arrows, twice.
    ASSERT_EQ(quads.size(), 16u);
    EXPECT_EQ(quads[14].slot, 9);
    EXPECT_FLOAT_EQ(quads[14].color.red, 0.2f);
    EXPECT_EQ(quads[15].slot, 9);
    EXPECT_FLOAT_EQ(quads[15].color.red, 0.5f);

    // Without the click, the arrow with the mouse at 0.7.
    tested.sprites.Begin(Screen640);
    ASSERT_TRUE(tested.Draw().HasValue());
    EXPECT_FLOAT_EQ(QuadsOf(tested.sprites).back().color.red, 0.7f);
}

// 0x632a2b: boxes 0x8b, 0x97 and 0x9b without the event object have no text; with it, the one of its method +0x10 by
// the object's type and not by the box (test_event_drawing checks each one), in Lorencia and in Devias.
TEST(GameDialogDrawingTest, TheEventBoxesDrawTheTextOfTheEventObject)
{
    for (const std::int32_t map : {Lorencia, Devias})
    {
        SCOPED_TRACE(map);
        DrawingDialogs tested(map);
        Client::Kernel::WorldRuntime& runtime = tested.world.Runtime();
        runtime.dialogs.current = 0x97;
        ASSERT_TRUE(tested.Draw().HasValue());
        EXPECT_EQ(QuadsOf(tested.sprites).size(), 9u);
        EXPECT_TRUE(tested.text.texts.empty());

        runtime.eventObject = Client::Events::EventMapObject{.kind = Client::Events::EventMapKind::BloodCastle};
        tested.sprites.Begin(Screen640);
        const auto drawn = tested.Draw();
        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
        EXPECT_EQ(QuadsOf(tested.sprites).size(), 9u);
        EXPECT_EQ(tested.Texts(), (std::vector<std::string>{"perdiste", "mision sin cumplir", "exp 0", "puntos 0"}));
    }
}

// 0x630c51: the generic OK, with its button (0xf2 with the mouse) and its texts of one or two lines.
TEST(GameDialogDrawingTest, TheGenericOkDrawsItsTexts)
{
    for (const std::int32_t map : {Lorencia, Devias})
    {
        DrawingDialogs tested(map);
        Client::Kernel::WorldRuntime& runtime = tested.world.Runtime();
        runtime.dialogs.current = Client::Ui::DisconnectDialog;
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

// 0x632290 and 0x6322c1: the 0x87 and the 0x88 of the warehouse password box (0x5e7bf0) are generic OKs, with the 442
// and the 445 on one line.
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

// 0x630818 and 0x63093e with the tail of 0x63140b: the 0x74 (no zen, 0x631dc5) and the 0x75 (the trade limit,
// 0x631df7), the 0x85 and the 0x86 (the warehouse password, 0x63222e and 0x63225f) and the 0x89 (0x6322f2) are generic
// OKs, with the 423, 424, 440, 441 and 401 on one line, centered at y = [EBP − 0x20] + 0xf (0x4b).
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

// 0x632441: the 0x90 (the 99 with 1, 0x4e4a40) is a generic OK like the 0x8f, but its line does not come from
// Text.bmd: it is the .data string 0x6d6320, in Korean (EUC-KR), "서버 이전시 오류 발생 : 문의 바람", with its bytes as
// is.
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

// The guild creation notices (CL9 milestone 8): the generic one's frame and OK (0x630818 and 0x63093e) and their
// texts, the 0x72 on two lines (392 and 393 at 75 − 6 and 75 + 6) and the 0x79, 0x7a and 0x7e on one (390, 391 and
// 426 at 75).
TEST(GameDialogDrawingTest, TheGuildCreationNoticesDrawLikeTheGenericOk)
{
    for (const std::int32_t map : {Lorencia, Devias})
    {
        SCOPED_TRACE(map);
        DrawingDialogs tested(map);
        Client::Kernel::WorldRuntime& runtime = tested.world.Runtime();
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

// 0x77 (CL9 milestone 7, phase 2): the generic OK's frame, the yes and the no (0x6309d4), and in the DC's font "de %s"
// with the name of the first one alive of the 400 with the key of 0x78aba4c (0x5110d0, with MOVSX) at 75 − 6 and the
// question at 75 + 6. With nobody holding the key, the 400 (no name).
TEST(GameDialogDrawingTest, ThePartyInvitationNamesWhoInvites)
{
    for (const std::int32_t map : {Lorencia, Devias})
    {
        SCOPED_TRACE(map);
        auto tested = std::make_unique<DrawingDialogs>(map);
        Client::Kernel::WorldRuntime& runtime = tested->world.Runtime();
        runtime.dialogs.current = 0x77;
        Client::Kernel::ActorState& dead = tested->world.Actors()[3];
        dead.key = 0x8456;
        Name(dead.name, "Zorzal");
        Client::Kernel::ActorState& inviter = tested->world.Actors()[7];
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

        // The unsigned key of the 40 (0x8456) is nobody's: the 400, no name.
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

// 0x78: "de %s" with the other one of the trade (0x78aba1c) in the big bold at 75 − 11 and the question in the normal
// at 75 + 11; the no with the mouse in [0x14e, 0x194) × [0x62, 0x77).
TEST(GameDialogDrawingTest, TheTradeRequestNamesTheOther)
{
    for (const std::int32_t map : {Lorencia, Devias})
    {
        SCOPED_TRACE(map);
        auto tested = std::make_unique<DrawingDialogs>(map);
        Client::Kernel::WorldRuntime& runtime = tested->world.Runtime();
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

// 0x7f (CL9 milestone 8): the generic OK's frame, the yes and the no (0x6309d4) like the 0x77, and in the DC's font
// (0x63212a) "guerra de %s" with the rival of 0x5767804 (up to its zero) at 75 − 6 and the 431 at 75 + 6; with the war
// type (0x576796d), the 432. No rand().
TEST(GameDialogDrawingTest, TheWarRequestNamesTheRivalAndAsksByWarType)
{
    for (const std::int32_t map : {Lorencia, Devias})
    {
        SCOPED_TRACE(map);
        auto tested = std::make_unique<DrawingDialogs>(map);
        Client::Kernel::WorldRuntime& runtime = tested->world.Runtime();
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

// 0x630818 and 0x630af0..0x630cb8 (the 0x7d, CL9 milestone 8): the generic OK's frame; the number EDIT ([0x5763508])
// visible with +0x4c at 9, the background at 0, limit 7, 0x46 × 0xe at (0xf3, 0x62) and with the focus (0x4682a0:
// 0x5762ec0 with its id); in normal blending, the black box of +0x4c, the bitmap and the '*' (the caret, at the end,
// clipped); in opaque, the yes (0xf1, or 0xf2 with the mouse in [0x143, 0x16b) × [0x62, 0x77)) at 323 and the no
// (0xf3, or 0xf4 in [0x175, 0x19d)) at 373, of 40 × 21. In the tail (0x631f36), the 427 and the 428 centered at 75 − 6
// and 75 + 6, in red, which stays. No rand().
TEST(GameDialogDrawingTest, TheLeaveNumberDrawsItsEditAndTheNarrowButtons)
{
    for (const std::int32_t map : {Lorencia, Devias})
    {
        SCOPED_TRACE(map);
        auto tested = std::make_unique<DrawingDialogs>(map);
        Client::Kernel::WorldRuntime& runtime = tested->world.Runtime();
        runtime.dialogs.current = 0x7d;
        runtime.ui.focusedList = -3;
        Client::Chat::ChatInputState& input = runtime.chat.input;
        input.numberField.text.Type("12");
        tested->mouse = {0x16a, 0x76};

        ASSERT_TRUE(tested->Draw().HasValue());
        const Client::Ui::OldEdit& field = input.numberField;
        EXPECT_EQ(field.base.state, 0);
        EXPECT_EQ(field.filter, 9);
        EXPECT_EQ(field.backgroundColor, 0u);
        EXPECT_EQ(field.base.x, 0xf3);
        EXPECT_EQ(field.base.y, 0x62);
        EXPECT_EQ(field.base.width, 0x46);
        EXPECT_EQ(field.base.height, 0xe);
        EXPECT_EQ(input.focus, Client::Chat::ChatFocus::NumberField);
        EXPECT_EQ(runtime.ui.focusedList, Client::Chat::NumberFieldId);
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
        // The caret goes in column 3 + 12 + 1 of a bitmap of ftol(3 + 12) = 15: at the end of the text it is clipped
        // (the XP editor of Ui::OldDraw::DrawTheOldEdit, with the margin of 3 before the text).
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

        // Limit 7 (EM_LIMITTEXT) stops what is typed from now on. Each WM_CHAR goes through the subclass (0x467b10),
        // which sets +0x80 to 0 (0x467b9d) before giving it to the EDIT.
        Client::Chat::TypeInTheChat(input, "345678");
        EXPECT_EQ(input.numberField.text.Value(), "1234567");
        EXPECT_EQ(input.numberField.draws, 0);

        // An intermediate drawing does not count the blink; outside the yes (x 0x16b) and on the no, 0xf1 and 0xf4.
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

// 0x467f30 (EM_LIMITTEXT through SendMessageA) on each frame of the 0x7d and the 0x73 goes through the subclass
// 0x467b10: with an arrow held down, +0x80 at 0 before 0x468320 looks at the caret, which shows from the first frame
// (with 13, off).
TEST(GameDialogDrawingTest, AHeldArrowLightsTheDialogCaretAtOnce)
{
    for (const std::int32_t dialog : {0x7d, 0x73})
    {
        SCOPED_TRACE(dialog);
        auto tested = std::make_unique<DrawingDialogs>(Lorencia);
        Client::Kernel::WorldRuntime& runtime = tested->world.Runtime();
        runtime.dialogs.current = dialog;
        Client::Chat::ChatInputState& input = runtime.chat.input;
        Client::Ui::OldEdit& edit = dialog == 0x7d ? input.numberField : input.dialogEdit;
        // The caret at the start: at the end of the text it would be outside the bitmap.
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
// 0x6304e1 → 0x471ae0 (CL9 milestone 7): the confirm box 0x9c with two lines, (12 + 2) high in 640: the frame (0xfc at
// y 120, 0xfb from 125 with h + 40 of height and 0xfc at h + 165), the yes at (255, h + 140) with the mouse on the
// bottom edge and the x of the right edge (0xf2), the no without it (0xf3), and the lines centered from 0x87 every 14.
// All without blending, and normal blending at the end.
TEST(GameDialogDrawingTest, TheConfirmBoxDrawsItsTwoButtonsAndItsRows)
{
    for (const std::int32_t map : {Lorencia, Devias})
    {
        SCOPED_TRACE(map);
        auto tested = std::make_unique<DrawingDialogs>(map);
        Client::Kernel::WorldRuntime& runtime = tested->world.Runtime();
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

// The mouse edges: the no with the x of 0x17e and the y of h + 140; one pixel lower, none.
TEST(GameDialogDrawingTest, TheConfirmBoxLightsTheButtonUnderTheMouse)
{
    for (const std::int32_t map : {Lorencia, Devias})
    {
        SCOPED_TRACE(map);
        auto tested = std::make_unique<DrawingDialogs>(map);
        Client::Kernel::WorldRuntime& runtime = tested->world.Runtime();
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

// With one button, the yes at (295, h + 140), with the mouse from 0x127 to 0x15b; with another type, none.
TEST(GameDialogDrawingTest, TheConfirmBoxWithOneButtonOrNone)
{
    for (const std::int32_t map : {Lorencia, Devias})
    {
        SCOPED_TRACE(map);
        auto tested = std::make_unique<DrawingDialogs>(map);
        Client::Kernel::WorldRuntime& runtime = tested->world.Runtime();
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

// The untextured quads of a batch: the color rectangles (the EDIT's background, ColorQuad).
std::vector<DrawnQuad> ColorQuadsOf(const std::vector<DrawnQuad>& quads)
{
    std::vector<DrawnQuad> colored;
    std::ranges::copy_if(quads, std::back_inserter(colored), [](const DrawnQuad& quad) { return !quad.slot; });
    return colored;
}

// 0x73 (0x630818..0x6308c6): the generic OK's frame, without the button, and the EDIT of [0x5763504] rebuilt in the
// frame (visible, +0x4c at 9, limit 8, 0x46 × 0xe at (0x11b, 0x5f)) with the focus and its drawing: the black of filter
// 8 and the background. Then, in the tail (0x631c91), the text by 0x78abacc: 420 with 0, 421 with 1 and 422 with
// another, at y = 0x4b.
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
            Client::Kernel::WorldRuntime& runtime = tested->world.Runtime();
            runtime.dialogs.current = 0x73;
            runtime.social.trade.field78abacc = mark;
            Client::Ui::OldEdit& edit = runtime.chat.input.dialogEdit;
            edit.text.SetText("150");
            edit.base.state = Client::Ui::HiddenWindow;
            runtime.chat.input.focus = Client::Chat::ChatFocus::Game;
            tested->mouse = {0x11c, 0x76};

            ASSERT_TRUE(tested->Draw().HasValue());
            EXPECT_EQ(edit.base.state, Client::Ui::ShownWindow);
            EXPECT_EQ(edit.filter, 9);
            EXPECT_EQ(edit.base.x, 0x11b);
            EXPECT_EQ(edit.base.y, 0x5f);
            EXPECT_EQ(edit.base.width, 0x46);
            EXPECT_EQ(edit.base.height, 0xe);
            EXPECT_EQ(edit.draws, 1);
            EXPECT_EQ(runtime.chat.input.focus, Client::Chat::ChatFocus::Dialog);
            EXPECT_EQ(runtime.ui.focusedList, Client::Chat::DialogFieldId);

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

            // The typing limit is 8 (the vtable + 0x3c).
            edit.text.SetText({});
            Client::Ui::TypeInTheEdit(edit, "1234567890");
            EXPECT_EQ(edit.text.Value(), "12345678");
        }
    }
}

// In an intermediate MuRet drawing, the EDIT is built and drawn on a copy: neither its state nor the focus change.
TEST(GameDialogDrawingTest, TheProjectingPassLeavesTheDialogEdit)
{
    auto tested = std::make_unique<DrawingDialogs>(Lorencia);
    Client::Kernel::WorldRuntime& runtime = tested->world.Runtime();
    runtime.dialogs.current = 0x73;
    Client::Ui::OldEdit& edit = runtime.chat.input.dialogEdit;
    edit.base.state = Client::Ui::HiddenWindow;
    runtime.chat.input.focus = Client::Chat::ChatFocus::Game;
    ASSERT_TRUE(tested->Draw(true).HasValue());
    EXPECT_EQ(edit.base.state, Client::Ui::HiddenWindow);
    EXPECT_EQ(edit.draws, 0);
    EXPECT_EQ(runtime.chat.input.focus, Client::Chat::ChatFocus::Game);
}

// 0x91 (0x6304f2..0x630813): the frame (0xfc at (213, 100), 0xfb at 105 and 145, 0xfc at 185), the EDIT that 0x5eba70
// built with the focus and its drawing, and without blending the OK (0xf2 with the mouse on the bottom right edge,
// (0x133, 0xb0)) at (255, 160) and the cancel (0xf3) at (330, 160). The text (0x631d2f): 1128 with the title and 1129
// with the price, at y = 0x73; with another mode, none.
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
            Client::Kernel::WorldRuntime& runtime = tested->world.Runtime();
            runtime.dialogs.current = 0x91;
            runtime.social.personalShop.dialogMode = mode;
            Client::Ui::OldEdit& edit = runtime.chat.input.dialogEdit;
            edit.base.state = Client::Ui::ShownWindow;
            edit.filter = 0xc;
            edit.backgroundColor = 0;
            edit.base.x = 0xfd;
            edit.base.y = 0x8a;
            edit.base.width = 0x85;
            edit.base.height = 0xa;
            edit.text.SetText("Puesto");
            runtime.chat.input.focus = Client::Chat::ChatFocus::Game;
            tested->mouse = {0x133, 0xb0};

            ASSERT_TRUE(tested->Draw().HasValue());
            EXPECT_EQ(runtime.chat.input.focus, Client::Chat::ChatFocus::Dialog);
            EXPECT_EQ(runtime.ui.focusedList, Client::Chat::DialogFieldId);
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

// The mouse edges of the 0x91: the cancel with (0x14a, 0xa0) is 0xf4 and the OK, 0xf1; one pixel left of the cancel or
// above, none.
TEST(GameDialogDrawingTest, TheShopDialogLightsTheButtonUnderTheMouse)
{
    for (const std::int32_t map : {Lorencia, Devias})
    {
        SCOPED_TRACE(map);
        for (const auto& [mouse, ok, cancel] :
             {std::tuple<Client::Ui::Point, std::uint16_t, std::uint16_t>{{0x14a, 0xa0}, 0xf1, 0xf4},
              std::tuple<Client::Ui::Point, std::uint16_t, std::uint16_t>{{0x149, 0xa0}, 0xf1, 0xf3},
              std::tuple<Client::Ui::Point, std::uint16_t, std::uint16_t>{{0xff, 0x9f}, 0xf1, 0xf3},
              std::tuple<Client::Ui::Point, std::uint16_t, std::uint16_t>{{0xff, 0xa0}, 0xf2, 0xf3}})
        {
            auto tested = std::make_unique<DrawingDialogs>(map);
            Client::Kernel::WorldRuntime& runtime = tested->world.Runtime();
            runtime.dialogs.current = 0x91;
            runtime.chat.input.dialogEdit.base.state = Client::Ui::ShownWindow;
            tested->mouse = mouse;
            ASSERT_TRUE(tested->Draw().HasValue());
            const std::vector<DrawnQuad> quads = QuadsOf(tested->sprites);
            ASSERT_GE(quads.size(), 2u);
            EXPECT_EQ(quads[quads.size() - 2].slot, ok) << mouse.x << "," << mouse.y;
            EXPECT_EQ(quads.back().slot, cancel) << mouse.x << "," << mouse.y;
        }
    }
}

// The colors of the drawn texts, in their order (RecordingTextDrawer's marker quads).
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

// 0x632472..0x632636: the 0x8d draws its text from ((7 − 2 − 1) · 0x12) / 2 + 0x4c = 112 and the answers below, in
// yellow and the mouse's in red (no more than 0x6a from x = 0x13f); an empty answer is not drawn and does not lower the
// y. At the end, white (unseen: nothing is drawn after). The frame has 4 rows of 0xfb (those of (3 · 0x12 + 0x31) / 40
// = 2, with 4 as the minimum).
TEST(GameDialogDrawingTest, TheAnswersDialogDrawsItsTextAndItsAnswers)
{
    constexpr Color White{1.0f, 1.0f, 1.0f, 1.0f};
    constexpr Color Yellow{1.0f, 1.0f, 0.0f, 1.0f};
    constexpr Color Red{1.0f, 0.0f, 0.0f, 1.0f};
    const auto tested = std::make_unique<DrawingDialogs>(Lorencia);
    Client::Kernel::WorldRuntime& runtime = tested->world.Runtime();
    Client::Tables::DialogTable table(Client::Tables::DialogCount);
    std::ranges::copy(std::string_view("hola viajero"), table[3].text.begin());
    table[3].answerCount = 2;
    std::ranges::copy(std::string_view("seguir"), table[3].answers[0].begin());
    std::ranges::copy(std::string_view("chau"), table[3].answers[1].begin());
    ASSERT_TRUE(
        Client::Ui::OpenTheAnswersDialog(runtime.dialogBox, runtime.dialogs, &table, &tested->texts, 3).HasValue());
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

    // Outside x, none in red; the empty first one does not lower the y of the second.
    tested->sprites.Begin(Screen640);
    tested->text.texts.clear();
    tested->mouse = {0x13f + 0x6b, 150};
    runtime.dialogBox.answers[0] = '\0';
    ASSERT_TRUE(tested->Draw().HasValue());
    EXPECT_EQ(tested->Texts(), (std::vector<std::string>{"hola viajero", "2) chau"}));
    EXPECT_EQ(tested->text.texts[1].y, 130);
    EXPECT_EQ(TextColorsOf(tested->sprites), (std::vector<Color>{White, Yellow}));
}

// 0x631150..0x631182 and 0x6324a3: with 7 lines and 3 answers, (10 · 0x12 + 0x31) / 40 = 5 rows, and the text does not
// start higher than 0x46 (it would be ((7 − 3 − 7) · 0x12) / 2 + 0x4c = 49).
TEST(GameDialogDrawingTest, TheAnswersDialogGrowsWithItsRows)
{
    const auto tested = std::make_unique<DrawingDialogs>(Lorencia);
    Client::Kernel::WorldRuntime& runtime = tested->world.Runtime();
    runtime.dialogs.current = Client::Ui::AnswersDialog;
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

    // The 0x8a also goes down with the answers left (0x632472 is the same stretch), without the minimum.
    tested->sprites.Begin(Screen640);
    tested->text.texts.clear();
    runtime.dialogs.current = Client::Ui::MessageBoxDialog;
    ASSERT_TRUE(tested->Draw().HasValue());
    ASSERT_EQ(tested->Texts(), (std::vector<std::string>{"arriba"}));
    EXPECT_EQ(tested->text.texts[0].y, 49);
}

// The size of a batch quad (without the text ones) in 640 × 480, with the error of flipping the y (480 − y), and the
// farthest it reaches in the texture.
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

// The box's lines, one per 0x26 bytes of 0x7d36a98, and their count.
void SetRows(Client::Ui::DialogBoxState& box, std::initializer_list<std::string_view> rows)
{
    box.rows.fill('\0');
    std::size_t row = 0;
    for (const std::string_view text : rows)
        std::ranges::copy(text, box.rows.begin() + static_cast<std::ptrdiff_t>(row++ * Client::Ui::DialogRowBytes));
    box.lineCount = static_cast<std::int32_t>(rows.size());
}

// A yes/no dialog as 0x633300 and its step (0x6282dc) leave it: the yes with the mouse over it (2) and the no outside
// (3).
void OpenYesNo(Client::Kernel::WorldRuntime& runtime, std::int32_t id)
{
    runtime.dialogs.current = id;
    runtime.dialogBox.buttons[0] = {.state = 2, .x = 0x15, .y = 0x5a, .width = 0x46, .height = 0x15};
    runtime.dialogBox.buttons[1] = {.state = 3, .x = 0x78, .y = 0x5a, .width = 0x46, .height = 0x15};
}

// x = center − (cx / 2) of a text in 640 × 480.
int CenteredXAt(int center, std::string_view text)
{
    return center - static_cast<int>(text.size()) * LetterWidth / 2;
}

// 0x630cc4..0x630fbe: the yes/no dialogs' frame, 0xfc at y = 100, 3 rows of 0xfb from 105 and 0xfc at 225, without
// blending; and the buttons, 0xf0 + state at (x + 213, y + 100) of 70 × 21 (without the boxes' − 1) with the UVs (70 /
// 128, 21 / 32). No rand().
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

// 0x630dfd..0x630e09: only the buttons with a state from 1 to 4.
TEST(GameDialogDrawingTest, TheYesNoButtonsGoFromOneToFour)
{
    const auto tested = std::make_unique<DrawingDialogs>(Noria);
    Client::Kernel::WorldRuntime& runtime = tested->world.Runtime();
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

// 0x632637..0x632a2a: with nothing at 0x78abb14, the lines in the bold font and in (1, 0.7, 0), centered at x = 0x15e
// from y = 100 + 0x1e, every 0x12, and so they stay.
TEST(GameDialogDrawingTest, TheYesNoRowsAreBoldAndOrange)
{
    constexpr Color Orange{1.0f, 0.7f, 0.0f, 1.0f};
    for (const std::int32_t map : {Lorencia, Devias, Noria})
    {
        SCOPED_TRACE(map);
        const auto tested = std::make_unique<DrawingDialogs>(map);
        Client::Kernel::WorldRuntime& runtime = tested->world.Runtime();
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

// 0x632675..0x632902 and 0x632944..0x6329bf: what the cursor item waits for (0x78abb14) moves and paints the lines.
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
        // 1: the first in red.
        {0x98,
         1,
         0x15e,
         130,
         {Red, Orange, Orange, Orange},
         {FontKind::Bold, FontKind::Bold, FontKind::Bold, FontKind::Bold}},
        // 3: at 0x140 from 100 + 0xf, and the fourth in (1, 0.1, 0.1).
        {0x94,
         3,
         0x140,
         115,
         {Orange, Orange, Orange, RedFourth},
         {FontKind::Bold, FontKind::Bold, FontKind::Bold, FontKind::Bold}},
        // 4: at 0x140 from 100 + 0x2d.
        {0x99,
         4,
         0x140,
         145,
         {Orange, Orange, Orange, Orange},
         {FontKind::Bold, FontKind::Bold, FontKind::Bold, FontKind::Bold}},
        // 6: the first in red and the others in the normal font and in white.
        {0x98,
         6,
         0x15e,
         130,
         {Red, White, White, White},
         {FontKind::Bold, FontKind::Normal, FontKind::Normal, FontKind::Normal}},
        // The 0x96 does not move, but the colors follow 0x78abb14: with 2, (0.9, 0.9, 1) without the mix title.
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
            Client::Kernel::WorldRuntime& runtime = tested->world.Runtime();
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

// 0x632683..0x6328e3: with 0x78abb14 at 2, the 0x94, 0x98, 0x99, 0xa3 and 0xa8 carry the mix title first, in bold and
// in (1, 1, 0), centered at y = top + 0xf + 10; the lines go on at 0x140, 0x22 lower. The text comes from the type
// (0x7d2dc98) through sprintf.
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
            // In Devias and in Noria, one of the top ones and the bottom one.
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
                Client::Kernel::WorldRuntime& runtime = tested->world.Runtime();
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

// 0x6327eb: the 0x1c with another layer leaves the stack buffer unwritten; 0x632855: a title of 100 bytes or more
// overwrites the stack. Both stop with the frame already drawn.
TEST(GameDialogDrawingTest, TheMixTitleCutsWhatTheOriginalCannotWrite)
{
    for (const auto& [kind, cape, address] : std::to_array<std::tuple<std::int32_t, std::int32_t, std::string>>(
             {{0x1c, 2, "0x6327eb"}, {0x1c, 5, "0x6327eb"}, {6, 0, "0x632855"}}))
    {
        const auto tested = std::make_unique<DrawingDialogs>(Devias);
        tested->texts = Client::Tests::MakeTextTable({{613, std::string(100, 'a')}});
        Client::Kernel::WorldRuntime& runtime = tested->world.Runtime();
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

// 0x630cc9..0x630cd3 and 0x632658..0x632667: the 0xa3 (the jewel mix question) goes at y = 210 (0xfc, 3 rows from 215
// and 0xfc at 335, the buttons at y + 210) and its lines at x = 0x145 from 210 + 0x1e; with 0x78abb14 at 3, at 0x140
// from 210 + 0xf.
TEST(GameDialogDrawingTest, TheJewelMixQuestionGoesLower)
{
    for (const std::int32_t map : {Lorencia, Devias, Noria})
    {
        SCOPED_TRACE(map);
        const auto tested = std::make_unique<DrawingDialogs>(map);
        Client::Kernel::WorldRuntime& runtime = tested->world.Runtime();
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

// 0x630d20..0x630deb and 0x63263a..0x632653: the 0x9a has 4 rows (0xfc at 265), the yes in (1, 0.3, 0.3) without the
// item (0x78a59cc at 0xff) and the no in white, and its lines at x = 0x140 from 130 without looking at 0x78abb14.
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
            Client::Kernel::WorldRuntime& runtime = tested->world.Runtime();
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

// 0x630e0f..0x630f53 and 0x630fcb..0x6310d5: the three buttons of the 0xa8 in 0x646, with normal blending, with the
// 1412, the 1903 and the 229 in black and bold at x + 0xed − cx / 2 and y + 104; the text background stays at 0xff and
// the color comes back.
TEST(GameDialogDrawingTest, TheLuckQuestionDrawsItsThreeButtons)
{
    for (const std::int32_t map : {Lorencia, Devias, Noria})
    {
        SCOPED_TRACE(map);
        const auto tested = std::make_unique<DrawingDialogs>(map);
        tested->texts = Client::Tests::MakeTextTable({{229, "cerrar"}, {1412, "suerte"}, {1903, "sin"}});
        Client::Kernel::WorldRuntime& runtime = tested->world.Runtime();
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

// 0x6304cf and the table 0x632bf0: the 0xaa uses the yes/no dialogs' frame and its lines go like the boxes' (0x632472),
// centered from ((7 − 0 − 2) · 0x12) / 2 + 0x4c = 121, and white at the end.
TEST(GameDialogDrawingTest, TheNoticeQuestionUsesTheBoxRows)
{
    for (const std::int32_t map : {Lorencia, Devias, Noria})
    {
        SCOPED_TRACE(map);
        const auto tested = std::make_unique<DrawingDialogs>(map);
        Client::Kernel::WorldRuntime& runtime = tested->world.Runtime();
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

// 0x632a45..0x632aeb: the lines of the 0xa9 in yellow and in the bold font, centered at 0x140 from y = 100 + 0xf +
// 0x1e, without looking at 0x78abb14; at the end, the normal font.
TEST(GameDialogDrawingTest, TheBlessRowsAreYellow)
{
    constexpr Color Yellow{1.0f, 1.0f, 0.0f, 1.0f};
    for (const std::int32_t map : {Lorencia, Devias, Noria})
    {
        SCOPED_TRACE(map);
        const auto tested = std::make_unique<DrawingDialogs>(map);
        Client::Kernel::WorldRuntime& runtime = tested->world.Runtime();
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

// The dialogs with no drawing yet stop with their step's wave.
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

// 0x62e65c..0x62e929 and 0x632323, in Lorencia, Devias and Noria: the chaos box's mode. The frame (0xfc, 5 rows of 0xfb
// and 0xfc from y = 120), the two buttons at (245, 190) and (245, 275), the mouse's in white and the other at 0.7,
// their titles in bold centered at y + 12, the explanations at x = 0xf8 and the title 734 at the end, at y = 135 with
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
        // On the bottom edge of the second: y = 275 + 35.
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

// The x of a text centered at `center` in 640 × 480.
int CenteredAt(int center, const std::string& text)
{
    return center - static_cast<int>(text.size()) * LetterWidth / 2;
}

// Made-up jewel mix texts.
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

// 0x62e92e..0x62ed05 and 0x632372, in Lorencia, Devias and Noria: the jewel mix menu (0xa1). The frame from y = 120
// with 6 rows; before the first button the 1802 and the 1803 at x = 0xf8; the two buttons of 150 × 35 at (245, 190)
// and (245, 255), the mouse's (on the edge) in white and the other at 0.7, with the 1801 and the 1800 in bold and
// 0xfffff0c8 at y + 12; the cancel at (279, 320) at 0.8, with the 1002 centered at 0x13f; and the title 1801 at the
// end.
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

// 0x62ed0a..0x62f36c, in the three maps: with the bless chosen (0x6c62e0 at 0) its button goes in (1, 1, 0.2) and in
// bold, and the soul with the mouse in white, also bold and with 0xfffff0c8 because nothing resets them (Q-C950); the
// three bundles in bold with "10 · i" and "500000 · i"; without a jewel, at 0.5 and in the normal one. The cancel at
// (279, 365).
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

// 0x62f371..0x62fae6 and 0x6323c1, in the three maps: without a jewel, 3 rows, the two jewels at (235, 160) and (315,
// 160) with their text at y = 0xa8, the cancel at (279.5, 200) (in white with the mouse at x = 280, over the edge of
// 279.5) and the text in white before the title 1800.
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

// 0x62f68f..0x62f9c1, in the three maps: with the soul chosen, 7 rows, the 1821 with the empty cells (0x402de0) and the
// 1822 at x = 0xe4 with 0xc8c8c8c8; the list of 0x6e5660 at (0xe4, 0x6d + 0xf0) with its frame, its bar, the chosen
// one's veil and its rows; the OK (0xf0 of 80 × 25 at (234.5, 359), with the blending off that the list left) with the
// 188 in (1, 1, 0.2) and 0xfffff0c8, and the cancel in white with the mouse over it, both in bold at y = 367. Then the
// normal font, the text in white and the white, with which the tail's title 1800 comes out.
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
        Client::Npc::JewelBundleList& list = runtime.npcWindows.jewels.bundles;
        Client::ChaosMix::AddTheBundle(list, 8, 0);
        Client::ChaosMix::AddTheBundle(list, 5, 2);
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

// 0x62f78a..0x62f807 and 0x62f8ec, in the three maps: with no choice, the 188 at 0.5 with the text color that was there
// (the notes' 0xc8c8c8c8), and the cancel without the mouse at 0.7. With no rows, the list only draws its frame and
// blending stays on for the OK.
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

// 0x62faeb..0x62fc1a and 0x62fc1f..0x62febe, in the three maps: the wait (0xa5) with the 471 centered at 0x13f, without
// title; the result (0xa6) with "%s%s %s" of the 1801 (0x6e5794 at 0) or of the 1800, the 1816 and the 858 in bold,
// the OK of 65 × 25 at (287, 170) and the 228 with 0xfffff0c8. Neither has a title in the tail.
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
            result->mouse = mode == 0 ? Client::Ui::Point{352, 195} : Client::Ui::Point{353, 195};
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

// 0x62fd43: what does not fit in the 256 bytes of the 0xa6 overwrites the stack and stops.
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

// A line of 0x7d36a98, as 0x633300 copies it (strcpy).
void PutRow(Client::Ui::DialogBoxState& box, std::size_t row, std::string_view text)
{
    std::ranges::copy(text, box.rows.begin() + static_cast<std::ptrdiff_t>(row * Client::Ui::DialogRowBytes));
    box.rows[row * Client::Ui::DialogRowBytes + text.size()] = '\0';
}

// 0x94 with notice 3 (0x630cbd and 0x632637): the frame from y = 100 (0xfc, three 0xfb and 0xfc at 225), the buttons
// of 0x7d368b8 at (x + 213, y + 100) with their state (the first with the mouse, 0xf2), without blending; the four
// lines in bold, centered at 0x140 from y = 0x73 every 0x12, in (1, 0.7, 0) and the fourth in (1, 0.1, 0.1).
TEST(GameDialogDrawingTest, TheTradeWarningDrawsItsBoxAndItsRows)
{
    for (const std::int32_t map : {Lorencia, Devias})
    {
        SCOPED_TRACE(map);
        auto tested = std::make_unique<DrawingDialogs>(map);
        Client::Kernel::WorldRuntime& runtime = tested->world.Runtime();
        runtime.dialogs.current = 0x94;
        runtime.social.trade.warningKind = 3;
        Client::Ui::DialogBoxState& box = runtime.dialogBox;
        box.buttons = {Client::Ui::DialogButton{2, 0x15, 0x5a, 0x46, 0x15},
                       Client::Ui::DialogButton{3, 0x78, 0x5a, 0x46, 0x15}};
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

// With notice 5 (the purchase, 0x633300(1, 1100, 0x94)), the line goes centered at 0x15e from y = 100 + 0x1e; a button
// with no state (0) or with more than 4 is not drawn.
TEST(GameDialogDrawingTest, TheBuyWarningCentersItsRowFurtherRight)
{
    auto tested = std::make_unique<DrawingDialogs>(Lorencia);
    Client::Kernel::WorldRuntime& runtime = tested->world.Runtime();
    runtime.dialogs.current = 0x94;
    runtime.social.trade.warningKind = 5;
    Client::Ui::DialogBoxState& box = runtime.dialogBox;
    box.buttons = {Client::Ui::DialogButton{0, 0x15, 0x5a, 0x46, 0x15},
                   Client::Ui::DialogButton{5, 0x78, 0x5a, 0x46, 0x15}};
    PutRow(box, 0, "comprar?");
    box.lineCount = 1;
    ASSERT_TRUE(tested->Draw().HasValue());
    EXPECT_EQ(QuadsOf(tested->sprites).size(), 5u);
    ASSERT_EQ(tested->Texts(), (std::vector<std::string>{"comprar?"}));
    EXPECT_EQ(tested->text.texts[0].x, 0x15e - static_cast<int>(std::string_view("comprar?").size()) * LetterWidth / 2);
    EXPECT_EQ(tested->text.texts[0].y, 100 + 0x1e);
}

// The 0x99 of the low price (0x630cc4, the same frame from y = 100) with notice 6 (0x5dc7e9): its lines centered at
// 0x15e from y = 100 + 0x1e, the first in (1, 0, 0) in bold and the other in white in the normal one.
TEST(GameDialogDrawingTest, TheLowPriceWarningDrawsLikeTheWarning)
{
    auto tested = std::make_unique<DrawingDialogs>(Devias);
    Client::Kernel::WorldRuntime& runtime = tested->world.Runtime();
    runtime.dialogs.current = Client::Ui::MoneyTextDialog;
    runtime.social.trade.warningKind = 6;
    Client::Ui::DialogBoxState& box = runtime.dialogBox;
    box.buttons = {Client::Ui::DialogButton{1, 0x15, 0x5a, 0x46, 0x15},
                   Client::Ui::DialogButton{3, 0x78, 0x5a, 0x46, 0x15}};
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
        EXPECT_EQ(runtime.ui.focusedList, Client::Chat::NumberFieldId);
        const auto quads = QuadsOf(tested->sprites);
        ASSERT_EQ(quads.size(), 5u);
        EXPECT_EQ(quads[3].x, 323);
        EXPECT_EQ(quads[4].x, 373);
        EXPECT_EQ(tested->random.State(), before.State());
    }
}
