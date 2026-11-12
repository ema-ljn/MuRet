#include "Scenes/ChatBubbleDraw.h"

#include "App/WindowState.h"
#include "Core/Expected.h"
#include "Render/SpriteBatch.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/TextBox.h"
#include "Render/Text/TextStyle.h"
#include "Scenes/ActorState.h"
#include "Scenes/ChatBubbles.h"
#include "Scenes/GameProjection.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/ChatInput.h"
#include "UI/Widget.h"
#include "support/RecordingTextDrawer.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// El dibujo de los globos (CL8 hito 3; 06 § El chat, "Los globos"): 0x5a9050, 0x555840 y 0x555b00 con TextOut distinto
// de 1. La proyección de los tests deja la x de la pantalla en el centro menos la x del dueño y la y en el centro más
// la altura del globo (el techo más la z más 60) menos 300, con el ojo siempre a 1. El texto mide como el rasterizador
// falso: 6 columnas por letra. Los nombres y los textos son inventados.
namespace
{
using Client::App::PixelSize;
using Client::Render::SpriteBatch;
using Client::Render::Text::FontKind;
using Client::Render::Text::TextColors;
using Client::Scenes::ActorState;
using Client::Scenes::ChatBubble;
using Client::Scenes::ChatBubbleDrawContext;
using Client::Scenes::GameProjection;
using Client::Scenes::WorldState;
using Client::Tests::RecordedText;
using Client::Tests::RecordingTextDrawer;

constexpr PixelSize Screen640{640, 480};
constexpr PixelSize Screen1024{1024, 768};
// FontStepFor: 12 en 640 y 14 en 1024.
constexpr int Step640 = 12;
constexpr int Step1024 = 14;
constexpr float PlayerTop = 120.0f;

GameProjection FlatProjection(PixelSize screen)
{
    GameProjection projection;
    projection.view = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, -300.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    projection.horizontalScale = 1.0f;
    projection.verticalScale = 1.0f;
    projection.centerX = screen.width / 2;
    projection.centerY = screen.height / 2;
    projection.screenWidth = screen.width;
    projection.screenHeight = screen.height;
    return projection;
}

class ChatBubbleDrawTest : public ::testing::Test
{
protected:
    ChatBubbleDrawTest() : m_world(std::make_unique<WorldState>()), m_text(m_sprites)
    {
        UseTheScreen(Screen640);
        Actor(0, "Heroe", glm::vec3(0.0f, 0.0f, 0.0f));
    }

    void UseTheScreen(PixelSize screen)
    {
        m_sprites.Begin(screen);
        m_projection = FlatProjection(screen);
    }

    ActorState& Actor(std::size_t slot, std::string_view name, glm::vec3 position)
    {
        ActorState& actor = m_world->Actors()[slot];
        actor.alive = true;
        actor.seen = true;
        actor.position = position;
        actor.boundsTop = PlayerTop;
        actor.name.fill(std::byte{0});
        for (std::size_t i = 0; i < name.size(); ++i)
            actor.name[i] = static_cast<std::byte>(name[i]);
        return actor;
    }

    // El globo `index` de `owner`, con su nombre y un renglón vivo.
    ChatBubble& Bubble(std::size_t index, std::size_t owner, std::string name, std::string line)
    {
        ChatBubble& bubble = m_world->Runtime().chat.bubbles[index];
        bubble.name = std::move(name);
        bubble.line1 = std::move(line);
        bubble.time1 = 50;
        bubble.owner = owner;
        return bubble;
    }

    Client::Core::Expected<void, std::string> Draw(bool projecting = false, Client::UI::Point mouse = {}, int blink = 0,
                                                   bool withProjection = true)
    {
        ChatBubbleDrawContext context{.world = *m_world,
                                      .projection = withProjection ? &m_projection : nullptr,
                                      .sprites = m_sprites,
                                      .text = m_text,
                                      .dcFont = m_dcFont,
                                      .textColors = m_textColors,
                                      .clampBoxes = m_clampBoxes,
                                      .boxWidth = m_boxWidth,
                                      .boxHeight = m_boxHeight,
                                      .mouse = mouse,
                                      .blink = blink,
                                      .projecting = projecting};
        return Client::Scenes::DrawTheChatBubbles(context);
    }

    [[nodiscard]] ChatBubble& Bubbles(std::size_t index)
    {
        return m_world->Runtime().chat.bubbles[index];
    }

    [[nodiscard]] const std::vector<RecordedText>& Texts() const
    {
        return m_text.texts;
    }

    std::unique_ptr<WorldState> m_world;
    SpriteBatch m_sprites;
    RecordingTextDrawer m_text;
    GameProjection m_projection{};
    FontKind m_dcFont = FontKind::Bold;
    TextColors m_textColors{};
    std::uint8_t m_clampBoxes = 0;
    std::int32_t m_boxWidth = 0;
    std::int32_t m_boxHeight = 0;
};

// 0x555840: el nombre con 3 más, el gremio y el puesto, y los renglones que tienen tiempo; el alto, cinco pasos.
TEST_F(ChatBubbleDrawTest, TheMeasureTakesTheWidestShownText)
{
    ChatBubble bubble;
    bubble.name = "Tero";
    bubble.line1 = "hola che";
    bubble.line2 = "una linea larga";
    const auto measure = [&](int step)
    {
        const auto measured = Client::Scenes::MeasureTheChatBubble(bubble, m_text, step);
        ASSERT_TRUE(measured.HasValue()) << measured.Error();
    };

    measure(Step640);
    EXPECT_EQ(bubble.width, 4 * 6 + 3);
    EXPECT_EQ(bubble.height, 5 * Step640);
    bubble.time1 = 1;
    measure(Step640);
    EXPECT_EQ(bubble.width, 8 * 6);
    bubble.time2 = 1;
    measure(Step640);
    EXPECT_EQ(bubble.width, 15 * 6);
    bubble.guild = "Gremio de pruebas";
    measure(Step1024);
    EXPECT_EQ(bubble.width, 17 * 6);
    EXPECT_EQ(bubble.height, 5 * Step1024);
}

// 0x5559dd: el título de una tienda personal corta, después de escribir el tamaño.
TEST_F(ChatBubbleDrawTest, AShopTitleIsCutAfterTheMeasure)
{
    ChatBubble bubble;
    bubble.name = "Tero";
    bubble.shopTitle = "Vendo cosas";

    const auto measured = Client::Scenes::MeasureTheChatBubble(bubble, m_text, Step640);

    ASSERT_FALSE(measured.HasValue());
    EXPECT_NE(measured.Error().find("0x5559dd"), std::string::npos);
    EXPECT_EQ(bubble.width, 27);
    EXPECT_EQ(bubble.height, 60);
}

// 0x5a9050: centrado sobre el techo del dueño más 60, 0x24 más arriba, y dibujado en píxeles: el nombre y el renglón,
// un paso más abajo, con la fuente normal en el DC y 0x77f3b66 prendido.
TEST_F(ChatBubbleDrawTest, TheBubbleGoesCenteredAboveItsOwner)
{
    Actor(4, "Tero", glm::vec3(100.0f, 7.0f, 10.0f));
    Bubble(0, 4, "Tero", "hola");

    const auto drawn = Draw();

    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    // x: 320 − 100 − 27 / 2; y: 240 + (120 + 10 + 60 − 300) − 36.
    EXPECT_EQ(Bubbles(0).x, 207);
    EXPECT_EQ(Bubbles(0).y, 94);
    EXPECT_EQ(Bubbles(0).width, 27);
    EXPECT_EQ(Bubbles(0).height, 60);
    ASSERT_EQ(Texts().size(), 2u);
    EXPECT_EQ(Texts()[0].text, "Tero");
    EXPECT_EQ(Texts()[0].x, 207);
    EXPECT_EQ(Texts()[0].y, 94);
    EXPECT_EQ(Texts()[0].style.font, FontKind::Normal);
    EXPECT_EQ(Texts()[0].box.value().width, 27);
    EXPECT_EQ(Texts()[0].box.value().rows, Step640);
    EXPECT_EQ(Texts()[0].box.value().prefixColumns, 0);
    EXPECT_TRUE(Texts()[0].clampY);
    EXPECT_EQ(Texts()[1].text, "hola");
    EXPECT_EQ(Texts()[1].y, 94 + Step640);
    EXPECT_EQ(m_dcFont, FontKind::Normal);
    EXPECT_EQ(m_clampBoxes, 1);
    EXPECT_EQ(m_boxWidth, 27);
    EXPECT_EQ(m_boxHeight, 60);
    ASSERT_FALSE(m_sprites.Commands().empty());
    EXPECT_EQ(m_sprites.Commands().back().blend, Client::Render::BlendMode::Alpha);
}

// En 1024 × 768 el globo se ubica en 640 × 480 (la mitad de 27 · 640 / 1024 es 8) y se dibuja en píxeles, con el paso
// de 14.
TEST_F(ChatBubbleDrawTest, OnAWiderScreenItIsDrawnInPixels)
{
    UseTheScreen(Screen1024);
    Actor(4, "Tero", glm::vec3(100.0f, 7.0f, 10.0f));
    Bubble(0, 4, "Tero", "hola");

    const auto drawn = Draw();

    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    // (512 − 100) · 640 / 1024 = 257 y (384 − 110) · 480 / 768 = 171.
    EXPECT_EQ(Bubbles(0).x, 249);
    EXPECT_EQ(Bubbles(0).y, 135);
    EXPECT_EQ(Bubbles(0).height, 5 * Step1024);
    ASSERT_EQ(Texts().size(), 2u);
    EXPECT_EQ(Texts()[0].x, 398);
    EXPECT_EQ(Texts()[0].y, 216);
    EXPECT_EQ(Texts()[0].box.value().rows, Step1024);
    EXPECT_EQ(Texts()[1].y, 216 + Step1024);
}

// 0x5a910d: el primero de un par que se pisa sube arriba del otro si su y está antes de la mitad del otro, y si no,
// baja debajo. El otro, ya sin pisarse, queda donde estaba.
TEST_F(ChatBubbleDrawTest, AnOverlappingBubbleMovesAboveOrBelowTheOther)
{
    Actor(4, "Tero", glm::vec3(100.0f, 0.0f, 10.0f));
    Actor(5, "Benteveo", glm::vec3(100.0f, 0.0f, 10.0f));
    Bubble(0, 4, "Tero", "hola");
    Bubble(1, 5, "Benteveo", "hola");

    ASSERT_TRUE(Draw().HasValue());
    EXPECT_EQ(Bubbles(0).y, 94 - 60);
    EXPECT_EQ(Bubbles(1).y, 94);

    // El segundo 40 más arriba: el primero (94) queda en su mitad de abajo y baja a 54 + 60.
    m_world->Actors()[5].position.z = -30.0f;
    ASSERT_TRUE(Draw().HasValue());
    EXPECT_EQ(Bubbles(1).y, 54);
    EXPECT_EQ(Bubbles(0).y, 114);
}

// 0x5a9216: una x o una y negativa va a 0 y una que pasa de W o H menos el tamaño va contra el borde.
TEST_F(ChatBubbleDrawTest, TheEdgesKeepTheBubbleOnTheScreen)
{
    Actor(4, "Tero", glm::vec3(-400.0f, 0.0f, 10.0f));
    Actor(5, "Benteveo", glm::vec3(400.0f, 0.0f, -300.0f));
    Bubble(0, 4, "Tero", "hola");
    Bubble(1, 5, "Benteveo", "hola");

    ASSERT_TRUE(Draw().HasValue());
    EXPECT_EQ(Bubbles(0).x, 640 - 27);
    EXPECT_EQ(Bubbles(1).x, 0);
    EXPECT_EQ(Bubbles(1).y, 0);
}

// Q-C522: el borde compara la x de 640 × 480 con W menos el ancho en píxeles. En 1024 × 768 una x de 690 no se corre,
// y 0x554000 pone la caja contra el borde de la pantalla.
TEST_F(ChatBubbleDrawTest, TheEdgeComparesDesignXWithPixels)
{
    UseTheScreen(Screen1024);
    Actor(4, "Tero", glm::vec3(-605.0f, 0.0f, 10.0f));
    Bubble(0, 4, "Tero", "hola");

    ASSERT_TRUE(Draw().HasValue());

    EXPECT_EQ(Bubbles(0).x, 690);
    ASSERT_EQ(Texts().size(), 2u);
    EXPECT_EQ(Texts()[0].x, 1104);
    const auto& quad = m_sprites.Vertices();
    ASSERT_FALSE(quad.empty());
    EXPECT_EQ(quad[0].x, 1024.0f - 27.0f);
}

// 0x5565ef..0x5566d2: el nombre con el color de +0x64, y el de adelante y el fondo del globo propio, del mismo
// gremio (+0x1f2, también sin gremio los dos) o por +0x370 del dueño (0x513270 y 0x5132a0).
TEST_F(ChatBubbleDrawTest, TheColorsComeFromTheOwner)
{
    Actor(4, "Tero", glm::vec3(100.0f, 0.0f, 10.0f));
    ChatBubble& bubble = Bubble(0, 4, "Tero", "");
    bubble.nameTime = 100;
    bubble.time1 = 0;
    struct Case
    {
        std::size_t owner = 0;
        std::uint8_t color = 0;
        std::uint16_t guild = 0;
        std::uint8_t relation = 0;
        std::uint32_t name = 0;
        std::uint32_t prefix = 0;
        std::uint32_t background = 0;
    };
    const std::vector<Case> cases{
        {.owner = 4,
         .color = 2,
         .guild = 0,
         .relation = 0,
         .name = 0xffffb48cu,
         .prefix = 0xff00ffc8u,
         .background = 0x96503214u},
        {.owner = 4,
         .color = 9,
         .guild = 3,
         .relation = 0,
         .name = 0xff0000ffu,
         .prefix = 0xffffe6e6u,
         .background = 0x96321e0au},
        {.owner = 4,
         .color = 0,
         .guild = 3,
         .relation = 8,
         .name = 0xfff0ff96u,
         .prefix = 0xff001effu,
         .background = 0x96000000u},
        {.owner = 4,
         .color = 5,
         .guild = 3,
         .relation = 4,
         .name = 0xff1e50ffu,
         .prefix = 0xff00ffc8u,
         .background = 0x96503214u},
        {.owner = 0,
         .color = 1,
         .guild = 3,
         .relation = 0,
         .name = 0xffff7864u,
         .prefix = 0xff00ffc8u,
         .background = 0x9600643cu},
    };
    for (const Case& c : cases)
    {
        SCOPED_TRACE(c.name);
        bubble.owner = c.owner;
        bubble.color = c.color;
        m_world->Actors()[4].word1f2 = c.guild;
        m_world->Actors()[4].byte370 = c.relation;
        m_text.texts.clear();
        ASSERT_TRUE(Draw().HasValue());
        ASSERT_EQ(Texts().size(), 1u);
        EXPECT_EQ(Texts()[0].style.colors.text, c.name);
        EXPECT_EQ(Texts()[0].style.colors.background, c.background);
        EXPECT_EQ(Texts()[0].box.value().prefixColor, c.prefix);
    }
}

// 0x5567ff..0x556cd2: el gremio y el puesto con su cx de adelante, el nombre, y los dos renglones con el fondo de +0x65
// y la mitad del alfa con menos de 10 pasos; los colores del último quedan en 0x6c8e5c y 0x6c8e64.
TEST_F(ChatBubbleDrawTest, TheLinesComeInTheirOrderAndColors)
{
    Actor(4, "Tero", glm::vec3(100.0f, 0.0f, 10.0f));
    ChatBubble& bubble = Bubble(0, 4, "Tero", "abajo");
    bubble.guild = "Gremio";
    bubble.guildPost = "Jefe";
    bubble.line2 = "arriba";
    bubble.time2 = 5;
    bubble.guildByte = 1;

    ASSERT_TRUE(Draw().HasValue());

    ASSERT_EQ(Texts().size(), 5u);
    const std::vector<std::string> order{"Gremio", "Jefe", "Tero", "arriba", "abajo"};
    for (std::size_t i = 0; i < order.size(); ++i)
    {
        EXPECT_EQ(Texts()[i].text, order[i]);
        EXPECT_EQ(Texts()[i].y, Texts()[0].y + static_cast<int>(i) * Step640);
    }
    EXPECT_EQ(Texts()[0].box.value().prefixColumns, 36);
    EXPECT_EQ(Texts()[1].box.value().prefixColumns, 24);
    EXPECT_EQ(Texts()[2].box.value().prefixColumns, 0);
    EXPECT_EQ(Texts()[3].style.colors, (TextColors{.text = 0x80c8dce6u, .background = 0x9600321eu}));
    EXPECT_EQ(Texts()[4].style.colors, (TextColors{.text = 0xffc8dce6u, .background = 0x9600321eu}));
    EXPECT_EQ(m_textColors, (TextColors{.text = 0xffc8dce6u, .background = 0x9600321eu}));

    // Sin el de arriba, solo el de abajo; con +0x65 en otro valor, el tercer fondo.
    m_text.texts.clear();
    bubble.time2 = 0;
    bubble.time1 = 9;
    bubble.guildByte = 7;
    ASSERT_TRUE(Draw().HasValue());
    ASSERT_EQ(Texts().size(), 4u);
    EXPECT_EQ(Texts()[3].text, "abajo");
    EXPECT_EQ(Texts()[3].style.colors, (TextColors{.text = 0x80c8dce6u, .background = 0x96000032u}));
}

// 0x5566d7..0x5567f4: con el mouse encima, el campo del chat abierto, el héroe en la zona segura y otro nombre, el
// texto y el fondo se cambian en 3 de cada 6 dibujos del área; los renglones no.
TEST_F(ChatBubbleDrawTest, TheMouseSwapsTheColorsOfAnotherName)
{
    Actor(4, "Tero", glm::vec3(100.0f, 0.0f, 10.0f));
    Bubble(0, 4, "Tero", "hola");
    m_world->Runtime().ui.chatOpen = true;
    m_world->Actors()[0].safeZone = 1;
    const Client::UI::Point inside{210, 100};

    ASSERT_TRUE(Draw(false, inside, 2).HasValue());
    ASSERT_EQ(Texts().size(), 2u);
    EXPECT_EQ(Texts()[0].style.colors, (TextColors{.text = 0x96503214u, .background = 0xfff0ff96u}));
    EXPECT_EQ(Texts()[1].style.colors.text, 0xffc8dce6u);

    const auto nameColors = [&](Client::UI::Point mouse, int blink)
    {
        m_text.texts.clear();
        const auto drawn = Draw(false, mouse, blink);
        EXPECT_TRUE(drawn.HasValue());
        return Texts().empty() ? TextColors{} : Texts()[0].style.colors;
    };
    const TextColors plain{.text = 0xfff0ff96u, .background = 0x96503214u};
    EXPECT_EQ(nameColors(inside, 3), plain);
    EXPECT_EQ(nameColors(Client::UI::Point{207 + 27, 100}, 0), plain);
    m_world->Runtime().ui.chatOpen = false;
    EXPECT_EQ(nameColors(inside, 0), plain);
    m_world->Runtime().ui.chatOpen = true;
    Bubbles(0).name = "Heroe";
    EXPECT_EQ(nameColors(inside, 0), plain);
}

// 0x555b18 y 0x555b2a..0x555bda: el dueño con el bit 0x10000000 de +0x7c, o de otro bando en el asedio, no tiene
// globo, pero el lugar se calcula igual.
TEST_F(ChatBubbleDrawTest, SomeOwnersHaveNoBubble)
{
    ActorState& owner = Actor(4, "Tero", glm::vec3(100.0f, 0.0f, 10.0f));
    Bubble(0, 4, "Tero", "hola");
    owner.flags7c = 0x10000000u;
    ASSERT_TRUE(Draw().HasValue());
    EXPECT_TRUE(Texts().empty());
    EXPECT_EQ(Bubbles(0).x, 207);

    owner.flags7c = 0;
    owner.flags78 = 0x4000u;
    owner.byte360 = 10;
    m_world->Actors()[0].byte360 = 9;
    m_world->Runtime().siegeActive = true;
    ASSERT_TRUE(Draw().HasValue());
    EXPECT_TRUE(Texts().empty());

    owner.byte360 = 6;
    ASSERT_TRUE(Draw().HasValue());
    EXPECT_EQ(Texts().size(), 2u);
}

// Un dibujo intermedio de MuRet dibuja igual, pero no escribe la tabla: el clic del paso lee la del último frame.
TEST_F(ChatBubbleDrawTest, AnIntermediateDrawLeavesTheTableAlone)
{
    Actor(4, "Tero", glm::vec3(100.0f, 0.0f, 10.0f));
    Bubble(0, 4, "Tero", "hola");

    ASSERT_TRUE(Draw(true).HasValue());

    EXPECT_EQ(Texts().size(), 2u);
    EXPECT_EQ(Texts()[0].x, 207);
    EXPECT_EQ(Bubbles(0).x, 0);
    EXPECT_EQ(Bubbles(0).width, 0);
}

// Sin la proyección del pase del mundo, un globo vivo corta; sin globos vivos no hace falta.
TEST_F(ChatBubbleDrawTest, WithoutTheProjectionALiveBubbleIsCut)
{
    ASSERT_TRUE(Draw(false, {}, 0, false).HasValue());
    EXPECT_EQ(m_dcFont, FontKind::Normal);

    Actor(4, "Tero", glm::vec3(100.0f, 0.0f, 10.0f));
    Bubble(0, 4, "Tero", "hola");
    const auto drawn = Draw(false, {}, 0, false);
    ASSERT_FALSE(drawn.HasValue());
    EXPECT_NE(drawn.Error().find("0x5a90be"), std::string::npos);
}

// 0x5a90b2: un globo vivo sin dueño es de una tienda personal (CL9.4).
TEST_F(ChatBubbleDrawTest, ABubbleWithoutOwnerIsCut)
{
    Bubbles(3).nameTime = 5;

    const auto drawn = Draw();

    ASSERT_FALSE(drawn.HasValue());
    EXPECT_NE(drawn.Error().find("0x5a90b2"), std::string::npos);
}

// 0x557650 lee dónde quedó el globo: antes del dibujo el derecho no lo toca; después, el derecho sobre uno ajeno
// llega al susurro (0x5577a2).
TEST_F(ChatBubbleDrawTest, TheRightClickOnADrawnBubbleReachesTheWhisper)
{
    ActorState& owner = Actor(4, "Tero", glm::vec3(100.0f, 0.0f, 10.0f));
    owner.word376 = 0xff;
    Bubble(0, 4, "Tero", "hola");
    Client::Scenes::WorldRuntime& runtime = m_world->Runtime();
    runtime.ui.chatOpen = true;
    runtime.ui.rightClick = true;
    m_world->Actors()[0].safeZone = 1;
    const Client::Scenes::ChatBubbleStepInput input{.mouseX = 210, .mouseY = 100, .screen = Screen640};
    ASSERT_TRUE(Client::Scenes::StepTheChatBubbles(*m_world, input).HasValue());
    EXPECT_TRUE(runtime.ui.rightClick);
    EXPECT_EQ(Client::UI::CStringOf(runtime.chat.input.whisperBuffer), "");

    ASSERT_TRUE(Draw().HasValue());
    ASSERT_TRUE(Client::Scenes::StepTheChatBubbles(*m_world, input).HasValue());

    EXPECT_FALSE(runtime.ui.rightClick);
    EXPECT_EQ(Client::UI::CStringOf(runtime.chat.input.whisperBuffer), "Tero");
    EXPECT_EQ(runtime.lastSoundId, 0x19);
}
} // namespace
