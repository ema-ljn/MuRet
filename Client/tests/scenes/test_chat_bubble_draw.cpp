#include "Chat/ChatBubbleDraw.h"

#include "Core/Expected.h"
#include "Core/PixelSize.h"
#include "Tables/TextTable.h"
#include "Render/SpriteBatch.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/TextBox.h"
#include "Render/Text/TextStyle.h"
#include "Kernel/ActorState.h"
#include "Chat/ChatBubbles.h"
#include "Scenes/GameProjection.h"
#include "Social/SocialState.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Chat/ChatInput.h"
#include "Ui/Widget.h"
#include "Audio/SoundId.h"
#include "support/RecordingTextDrawer.h"
#include "support/SceneDataFiles.h"
#include "Chat/ChatRuntime.h"
#include "Ui/GameUiState.h"

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

// The bubbles' drawing (CL8 milestone 3; 06 § El chat, "Los globos"): 0x5a9050, 0x555840 and 0x555b00 with TextOut
// other than 1. The tests' projection leaves the screen x at the center minus the owner's x and the y at the center
// plus the bubble's height (the top plus the z plus 60) minus 300, with the eye always at 1. The text measures like
// the fake rasterizer: 6 columns per letter. The names, the titles and the texts (the 1104 too) are made up.
namespace
{
using Client::Chat::ChatBubble;
using Client::Chat::ChatBubbleDrawContext;
using Client::Core::PixelSize;
using Client::Kernel::ActorState;
using Client::Kernel::WorldState;
using Client::Render::SpriteBatch;
using Client::Render::Text::FontKind;
using Client::Render::Text::TextColors;
using Client::Scenes::GameProjection;
using Client::Tests::RecordedText;
using Client::Tests::RecordingTextDrawer;

constexpr PixelSize Screen640{640, 480};
constexpr PixelSize Screen1024{1024, 768};
// FontStepFor: 12 at 640 and 14 at 1024.
constexpr int Step640 = 12;
constexpr int Step1024 = 14;
constexpr float PlayerTop = 120.0f;
// The text 1104, the head of a shop's title: 8 letters, 48 columns.
constexpr std::string_view ShopHeader = "[Puesto]";
constexpr int ShopHeaderWidth = 48;

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

    ChatBubble& Bubble(std::size_t index, std::size_t owner, std::string name, std::string line)
    {
        ChatBubble& bubble = m_world->Runtime().chat.bubbles[index];
        bubble.name = std::move(name);
        bubble.line1 = std::move(line);
        bubble.time1 = 50;
        bubble.owner = owner;
        return bubble;
    }

    Client::Core::Expected<void, std::string> Draw(bool projecting = false, Client::Ui::Point mouse = {}, int blink = 0,
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
                                      .projecting = projecting,
                                      .texts = m_withTexts ? &m_texts : nullptr};
        return Client::Chat::DrawTheChatBubbles(context);
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
    Client::Tables::TextTable m_texts = Client::Tests::MakeTextTable({{1104, std::string(ShopHeader)}});
    bool m_withTexts = true;
};

// 0x555840: the name with 3 more, the guild and the post, and the lines that have time; the height, five steps.
TEST_F(ChatBubbleDrawTest, TheMeasureTakesTheWidestShownText)
{
    ChatBubble bubble;
    bubble.name = "Tero";
    bubble.line1 = "hola che";
    bubble.line2 = "una linea larga";
    const auto measure = [&](int step)
    {
        const auto measured = Client::Chat::MeasureTheChatBubble(bubble, m_text, step, nullptr);
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

// 0x5559c2..0x555a62: with a shop's short title (+0x54), the width is at least the title's plus the text 1104's, in
// bold, and the height adds a line. Without Text.bmd it stops, after writing the usual size.
TEST_F(ChatBubbleDrawTest, AShopTitleWidensTheMeasureByOneRow)
{
    ChatBubble bubble;
    bubble.name = "Tero";
    bubble.shopTitle = "Vendo cosas";

    ASSERT_TRUE(Client::Chat::MeasureTheChatBubble(bubble, m_text, Step640, &m_texts).HasValue());
    EXPECT_EQ(bubble.width, 11 * 6 + ShopHeaderWidth);
    EXPECT_EQ(bubble.height, 6 * Step640);

    // A name wider than both stays.
    bubble.name = "Un nombre bastante largo";
    ASSERT_TRUE(Client::Chat::MeasureTheChatBubble(bubble, m_text, Step1024, &m_texts).HasValue());
    EXPECT_EQ(bubble.width, 24 * 6 + 3);
    EXPECT_EQ(bubble.height, 6 * Step1024);

    bubble.name = "Tero";
    const auto measured = Client::Chat::MeasureTheChatBubble(bubble, m_text, Step640, nullptr);
    ASSERT_FALSE(measured.HasValue());
    EXPECT_NE(measured.Error().find("0x555a0f"), std::string::npos);
    EXPECT_EQ(bubble.width, 27);
    EXPECT_EQ(bubble.height, 60);
}

// 0x5a9050: centered above the owner's top plus 60, 0x24 higher, and drawn in pixels: the name and the line, one step
// lower, with the normal font in the DC and 0x77f3b66 set.
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

// At 1024 × 768 the bubble is placed in 640 × 480 (half of 27 · 640 / 1024 is 8) and drawn in pixels, step 14.
TEST_F(ChatBubbleDrawTest, OnAWiderScreenItIsDrawnInPixels)
{
    UseTheScreen(Screen1024);
    Actor(4, "Tero", glm::vec3(100.0f, 7.0f, 10.0f));
    Bubble(0, 4, "Tero", "hola");

    const auto drawn = Draw();

    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    // (512 − 100) · 640 / 1024 = 257 and (384 − 110) · 480 / 768 = 171.
    EXPECT_EQ(Bubbles(0).x, 249);
    EXPECT_EQ(Bubbles(0).y, 135);
    EXPECT_EQ(Bubbles(0).height, 5 * Step1024);
    ASSERT_EQ(Texts().size(), 2u);
    EXPECT_EQ(Texts()[0].x, 398);
    EXPECT_EQ(Texts()[0].y, 216);
    EXPECT_EQ(Texts()[0].box.value().rows, Step1024);
    EXPECT_EQ(Texts()[1].y, 216 + Step1024);
}

// 0x5a910d: the first of an overlapping pair goes up above the other if its y is before the other's middle, and if
// not, goes down below it. The other, no longer overlapped, stays where it was.
TEST_F(ChatBubbleDrawTest, AnOverlappingBubbleMovesAboveOrBelowTheOther)
{
    Actor(4, "Tero", glm::vec3(100.0f, 0.0f, 10.0f));
    Actor(5, "Benteveo", glm::vec3(100.0f, 0.0f, 10.0f));
    Bubble(0, 4, "Tero", "hola");
    Bubble(1, 5, "Benteveo", "hola");

    ASSERT_TRUE(Draw().HasValue());
    EXPECT_EQ(Bubbles(0).y, 94 - 60);
    EXPECT_EQ(Bubbles(1).y, 94);

    // The second one 40 higher: the first (94) is in its lower half and goes down to 54 + 60.
    m_world->Actors()[5].position.z = -30.0f;
    ASSERT_TRUE(Draw().HasValue());
    EXPECT_EQ(Bubbles(1).y, 54);
    EXPECT_EQ(Bubbles(0).y, 114);
}

// 0x5a9216: a negative x or y goes to 0 and one past W or H minus the size goes against the edge.
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

// Q-C522: the edge compares the 640 × 480 x with W minus the width in pixels. At 1024 × 768 an x of 690 does not
// move, and 0x554000 puts the box against the screen's edge.
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

// 0x5565ef..0x5566d2: the name with the color of +0x64, and the prefix and the background of the own bubble, of the
// same guild (+0x1f2, also both without a guild) or by the owner's +0x370 (0x513270 and 0x5132a0).
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

// 0x5567ff..0x556cd2: the guild and the post with their prefix cx, the name, and the two lines with the background of
// +0x65 and half the alpha with fewer than 10 steps; the last one's colors stay in 0x6c8e5c and 0x6c8e64.
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

    // Without the upper one, only the lower one; with +0x65 at another value, the third background.
    m_text.texts.clear();
    bubble.time2 = 0;
    bubble.time1 = 9;
    bubble.guildByte = 7;
    ASSERT_TRUE(Draw().HasValue());
    ASSERT_EQ(Texts().size(), 4u);
    EXPECT_EQ(Texts()[3].text, "abajo");
    EXPECT_EQ(Texts()[3].style.colors, (TextColors{.text = 0x80c8dce6u, .background = 0x96000032u}));
}

// 0x5566d7..0x5567f4: with the mouse over it, the chat field open, the hero in the safe zone and another name, the
// text and the background swap in 3 of every 6 draws of the area; the lines do not.
TEST_F(ChatBubbleDrawTest, TheMouseSwapsTheColorsOfAnotherName)
{
    Actor(4, "Tero", glm::vec3(100.0f, 0.0f, 10.0f));
    Bubble(0, 4, "Tero", "hola");
    m_world->Runtime().ui.chatOpen = true;
    m_world->Actors()[0].safeZone = 1;
    const Client::Ui::Point inside{210, 100};

    ASSERT_TRUE(Draw(false, inside, 2).HasValue());
    ASSERT_EQ(Texts().size(), 2u);
    EXPECT_EQ(Texts()[0].style.colors, (TextColors{.text = 0x96503214u, .background = 0xfff0ff96u}));
    EXPECT_EQ(Texts()[1].style.colors.text, 0xffc8dce6u);

    const auto nameColors = [&](Client::Ui::Point mouse, int blink)
    {
        m_text.texts.clear();
        const auto drawn = Draw(false, mouse, blink);
        EXPECT_TRUE(drawn.HasValue());
        return Texts().empty() ? TextColors{} : Texts()[0].style.colors;
    };
    const TextColors plain{.text = 0xfff0ff96u, .background = 0x96503214u};
    EXPECT_EQ(nameColors(inside, 3), plain);
    EXPECT_EQ(nameColors(Client::Ui::Point{207 + 27, 100}, 0), plain);
    m_world->Runtime().ui.chatOpen = false;
    EXPECT_EQ(nameColors(inside, 0), plain);
    m_world->Runtime().ui.chatOpen = true;
    Bubbles(0).name = "Heroe";
    EXPECT_EQ(nameColors(inside, 0), plain);
}

// 0x555b18 and 0x555b2a..0x555bda: the owner with the bit 0x10000000 of +0x7c, or of another side in the siege, has
// no bubble, but the place is computed anyway.
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
    m_world->Runtime().castleFlags.siegeActive = true;
    ASSERT_TRUE(Draw().HasValue());
    EXPECT_TRUE(Texts().empty());

    owner.byte360 = 6;
    ASSERT_TRUE(Draw().HasValue());
    EXPECT_EQ(Texts().size(), 2u);
}

// An intermediate MuRet draw draws the same, but does not write the table: the step's click reads the last frame's.
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

// Without the world pass's projection, a live bubble stops; without live bubbles it is not needed.
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

// 0x5a90b2: a live bubble without an owner (+0x288, which only 0x557430 writes, with no caller) stops.
TEST_F(ChatBubbleDrawTest, ABubbleWithoutOwnerIsCut)
{
    Bubbles(3).nameTime = 5;

    const auto drawn = Draw();

    ASSERT_FALSE(drawn.HasValue());
    EXPECT_NE(drawn.Error().find("0x5a90b2"), std::string::npos);
}

// 0x557650 reads where the bubble ended up: before the draw the right click does not touch it; after it, the right
// click on someone else's reaches the whisper (0x5577a2).
TEST_F(ChatBubbleDrawTest, TheRightClickOnADrawnBubbleReachesTheWhisper)
{
    ActorState& owner = Actor(4, "Tero", glm::vec3(100.0f, 0.0f, 10.0f));
    owner.word376 = 0xff;
    Bubble(0, 4, "Tero", "hola");
    Client::Kernel::WorldRuntime& runtime = m_world->Runtime();
    runtime.ui.chatOpen = true;
    runtime.ui.rightClick = true;
    m_world->Actors()[0].safeZone = 1;
    const Client::Chat::ChatBubbleStepInput input{.mouseX = 210, .mouseY = 100, .screen = Screen640};
    ASSERT_TRUE(Client::Chat::StepTheChatBubbles(*m_world, input).HasValue());
    EXPECT_TRUE(runtime.ui.rightClick);
    EXPECT_EQ(Client::Ui::CStringOf(runtime.chat.input.whisperBuffer), "");

    ASSERT_TRUE(Draw().HasValue());
    ASSERT_TRUE(Client::Chat::StepTheChatBubbles(*m_world, input).HasValue());

    EXPECT_FALSE(runtime.ui.rightClick);
    EXPECT_EQ(Client::Ui::CStringOf(runtime.chat.input.whisperBuffer), "Tero");
    EXPECT_EQ(runtime.lastSoundId, Client::Audio::SoundId{0x19});
}

// 0x5563a4..0x5565ef: with the owner's sign, the text 1104 (as the prefix, of its width) and the short title to its
// right, in bold and two lines higher, with the colors of 0x451af0, 0x451b50 and 0x451bb0; the other lines, one
// higher (Q-C978), and the DC in the normal font.
TEST_F(ChatBubbleDrawTest, ABubbleOfAShopOwnerHasItsTitleAbove)
{
    Actor(4, "Tero", glm::vec3(100.0f, 7.0f, 10.0f));
    Bubble(0, 4, "Tero", "hola").shopTitle = "Vendo cosas";
    Client::Social::PersonalShopState& shops = m_world->Runtime().social.personalShop;
    shops.titles[4] = Client::Social::ShopTitle{.key = 0x0444, .name = "Tero", .title = "Vendo cosas y mas cosas"};

    ASSERT_TRUE(Draw().HasValue());

    // x: 320 − 100 − 114 / 2; y: 94, as without a shop; 72 high.
    EXPECT_EQ(Bubbles(0).x, 163);
    EXPECT_EQ(Bubbles(0).y, 94);
    EXPECT_EQ(Bubbles(0).width, 114);
    EXPECT_EQ(Bubbles(0).height, 72);
    ASSERT_EQ(Texts().size(), 4u);
    const TextColors plain{.text = 0xff00e6e6u, .background = 0x8029396cu};
    EXPECT_EQ(Texts()[0].text, ShopHeader);
    EXPECT_EQ(Texts()[0].x, 163);
    EXPECT_EQ(Texts()[0].y, 94 - 2 * Step640);
    EXPECT_EQ(Texts()[0].style.font, FontKind::Bold);
    EXPECT_EQ(Texts()[0].style.colors, plain);
    EXPECT_EQ(
        Texts()[0].box.value(),
        (Client::Render::Text::BoxShape{
            .width = ShopHeaderWidth, .rows = Step640, .prefixColumns = ShopHeaderWidth, .prefixColor = 0xff0096fau}));
    EXPECT_TRUE(Texts()[0].clampY);
    EXPECT_EQ(Texts()[1].text, "Vendo cosas");
    EXPECT_EQ(Texts()[1].x, 163 + ShopHeaderWidth);
    EXPECT_EQ(Texts()[1].y, 94 - 2 * Step640);
    EXPECT_EQ(Texts()[1].style.font, FontKind::Bold);
    EXPECT_EQ(Texts()[1].style.colors, plain);
    EXPECT_EQ(Texts()[1].box.value().width, 114 - ShopHeaderWidth);
    EXPECT_EQ(Texts()[1].box.value().prefixColumns, 0);
    EXPECT_EQ(Texts()[2].text, "Tero");
    EXPECT_EQ(Texts()[2].y, 94 - Step640);
    EXPECT_EQ(Texts()[2].style.font, FontKind::Normal);
    EXPECT_EQ(Texts()[3].text, "hola");
    EXPECT_EQ(Texts()[3].y, 94);
    EXPECT_EQ(m_dcFont, FontKind::Normal);
}

// 0x451af0, 0x451b50 and 0x451bb0: the sign's +0x60 (0x4517c0) changes the colors; +0x370 at 2 or 8 goes like PK 6,
// and PK 4 and 5 have their own.
TEST_F(ChatBubbleDrawTest, TheShopTitleColorsComeFromTheSignAndTheOwner)
{
    ActorState& owner = Actor(4, "Tero", glm::vec3(100.0f, 7.0f, 10.0f));
    Bubble(0, 4, "Tero", "hola").shopTitle = "Vendo";
    Client::Social::PersonalShopState& shops = m_world->Runtime().social.personalShop;
    shops.titles[4] = Client::Social::ShopTitle{.key = 0x0444, .name = "Tero", .title = "Vendo"};
    struct Case
    {
        bool hovered = false;
        std::uint8_t pk = 0;
        std::uint8_t relation = 0;
        std::uint32_t text = 0;
        std::uint32_t header = 0;
        std::uint32_t background = 0;
    };
    const std::vector<Case> cases{
        {.hovered = true,
         .pk = 0,
         .relation = 0,
         .text = 0xffffe6e6u,
         .header = 0xff29396cu,
         .background = 0x800096fau},
        {.hovered = false,
         .pk = 3,
         .relation = 1,
         .text = 0xff00e6e6u,
         .header = 0xff0096fau,
         .background = 0x8029396cu},
        {.hovered = false,
         .pk = 4,
         .relation = 0,
         .text = 0xff00b4e6u,
         .header = 0xff00b4e6u,
         .background = 0x8029396cu},
        {.hovered = true,
         .pk = 5,
         .relation = 0,
         .text = 0xff006ee6u,
         .header = 0xff006ee6u,
         .background = 0x80527ab6u},
        {.hovered = true,
         .pk = 6,
         .relation = 0,
         .text = 0xff0014f0u,
         .header = 0xff0014f0u,
         .background = 0x80527ab6u},
        {.hovered = false,
         .pk = 0,
         .relation = 2,
         .text = 0xff0014f0u,
         .header = 0xff0014f0u,
         .background = 0x8029396cu},
        {.hovered = true,
         .pk = 4,
         .relation = 8,
         .text = 0xff0014f0u,
         .header = 0xff0014f0u,
         .background = 0x80527ab6u},
    };
    for (const Case& c : cases)
    {
        SCOPED_TRACE(c.text);
        shops.titles[4].hovered = c.hovered ? 1 : 0;
        owner.pk = c.pk;
        owner.byte370 = c.relation;
        m_text.texts.clear();
        ASSERT_TRUE(Draw().HasValue());
        ASSERT_EQ(Texts().size(), 4u);
        for (std::size_t row = 0; row < 2; ++row)
        {
            EXPECT_EQ(Texts()[row].style.colors, (TextColors{.text = c.text, .background = c.background}));
            EXPECT_EQ(Texts()[row].box.value().prefixColor, c.header);
        }
    }
}

// The draw looks at the sign (0x451810) and not at +0x54: a bubble from before the sign has no title but draws the
// text 1104 and an empty title with the bubble's width minus the text's, negative in a narrow one (Q-C978); a title
// without a sign widens the measurement and is not drawn. Without Text.bmd, the draw with a sign stops.
TEST_F(ChatBubbleDrawTest, TheTitleRowsFollowTheSignAndNotTheBubble)
{
    Actor(4, "Tero", glm::vec3(100.0f, 7.0f, 10.0f));
    Bubble(0, 4, "Tero", "hola");
    Client::Social::PersonalShopState& shops = m_world->Runtime().social.personalShop;
    shops.titles[4] = Client::Social::ShopTitle{.key = 0x0444, .name = "Tero", .title = "Vendo"};

    ASSERT_TRUE(Draw().HasValue());
    EXPECT_EQ(Bubbles(0).width, 27);
    EXPECT_EQ(Bubbles(0).height, 60);
    ASSERT_EQ(Texts().size(), 4u);
    EXPECT_EQ(Texts()[0].text, ShopHeader);
    EXPECT_EQ(Texts()[1].text, "");
    EXPECT_EQ(Texts()[1].box.value().width, 27 - ShopHeaderWidth);
    EXPECT_EQ(Texts()[2].y, 94 - Step640);

    m_withTexts = false;
    const auto cut = Draw();
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x556421"), std::string::npos);

    m_withTexts = true;
    shops.titles.clear();
    Bubbles(0).shopTitle = "Vendo";
    m_text.texts.clear();
    ASSERT_TRUE(Draw().HasValue());
    EXPECT_EQ(Bubbles(0).width, 30 + ShopHeaderWidth);
    EXPECT_EQ(Bubbles(0).height, 72);
    ASSERT_EQ(Texts().size(), 2u);
    EXPECT_EQ(Texts()[0].text, "Tero");
    EXPECT_EQ(Texts()[0].y, Bubbles(0).y);
}
} // namespace
