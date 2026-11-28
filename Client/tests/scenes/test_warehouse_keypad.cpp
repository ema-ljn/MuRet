#include "Network/Xor32Packet.h"
#include "Kernel/MouseState.h"
#include "Warehouse/WarehouseKeypad.h"

#include "Core/Expected.h"
#include "Core/PixelSize.h"
#include "Crt/CrtRandom.h"
#include "Tables/TextTable.h"
#include "Input/InputState.h"
#include "Network/Delivery.h"
#include "Network/Packet.h"
#include "Protocol/Outgoing.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/TextStyle.h"
#include "Kernel/ActorState.h"
#include "Hud/GameHud.h"
#include "Hud/GamePanels.h"
#include "Hud/GameUi.h"
#include "Items/InventoryMoves.h"
#include "Npc/NpcWindowState.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Ui/GameUiState.h"
#include "Ui/PanelRegistry.h"
#include "Audio/SoundId.h"
#include "support/Bytes.h"
#include "support/RecordingTextDrawer.h"
#include "support/SameBytes.h"
#include "support/SceneDataFiles.h"
#include "Chat/ChatRuntime.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

// The warehouse password box (CL9 milestone 6, phase 3; 06 § Los dibujos de las ventanas de los NPC and 04 § Las
// ventanas de los NPC): what is under the mouse (0x5e7b50), the step (0x5e7bf0, with the 83 and the 99 byte by byte
// and their rand()) and the drawing (0x5e9b10), in 640 × 480. The texts, the numbers and the chat are made up and the
// texts measure 6 columns per letter. Each test runs in Lorencia, Devias and Noria.
namespace
{
using Client::Crt::CrtRandom;
using Client::Hud::GameHudContext;
using Client::Kernel::WorldState;
using Client::Network::Delivery;
using Client::Network::Xor32Decode;
using Client::Render::Color;
using Client::Render::SpriteBatch;
using Client::Render::SpriteVertex;
using Client::Render::Text::FontKind;
using Client::Tests::Bytes;
using Client::Tests::SameBytes;
using Client::Warehouse::KeypadKeyAt;

constexpr Client::Core::PixelSize Screen640{640, 480};
constexpr std::uint32_t Seed = 0x2b7e1516u;
constexpr std::int32_t Lorencia = 0;
constexpr std::int32_t Devias = 2;
constexpr std::int32_t Noria = 3;
constexpr Color White{1.0f, 1.0f, 1.0f, 1.0f};
constexpr Color HoveredKey{1.0f, 1.0f, 0.8f, 1.0f};
constexpr Color Key{0.85f, 0.85f, 0.65f, 1.0f};
constexpr int NoTexture = -1;
// The frame (6), the bar (1), two per key (22) and the OK and cancel (2).
constexpr std::size_t KeypadQuads = 6 + 1 + 22 + 2;
// The padding's rand() and the serial's of a C1 in C3; 0x657a90's trade (0x20) and shop (0x10000) queries.
constexpr std::uint32_t C3 = 2;
constexpr std::uint32_t CloseAllRandoms = 2;
constexpr int EraseKey = 10;
constexpr int OkKey = 11;
constexpr int CancelKey = 12;

CrtRandom Advanced(CrtRandom random, std::uint32_t count)
{
    random.Discard(count);
    return random;
}

std::map<std::size_t, std::string> KeypadTexts()
{
    return {{690, "Clave"},   {691, "Cambiar"}, {692, "Quitar"},   {693, "Nueva"},
            {694, "Repetir"}, {695, "Cuatro"},  {696, "Otra vez"}, {697, "Siete"}};
}

struct DrawnQuad
{
    int slot = NoTexture;
    Color color{};
    SpriteVertex first{};
    SpriteVertex third{};
};

// The textured quads that are not text marks, in their order.
std::vector<DrawnQuad> TexturedQuadsOf(const SpriteBatch& sprites)
{
    std::vector<DrawnQuad> quads;
    const auto vertices = sprites.Vertices();
    for (const Client::Render::DrawCommand& command : sprites.Commands())
    {
        if (!command.texture || command.texture == Client::Tests::TextMarkerSlot)
            continue;
        for (std::size_t quad = 0; quad < command.vertexCount / 6; ++quad)
        {
            const std::size_t first = command.firstVertex + quad * 6;
            quads.push_back(DrawnQuad{.slot = command.texture->value,
                                      .color = vertices[first].color,
                                      .first = vertices[first],
                                      .third = vertices[first + 2]});
        }
    }
    return quads;
}

void ExpectQuadAt(const DrawnQuad& quad, int slot, float x, float y, float width, float height)
{
    constexpr float Tolerance = 1e-3f;
    EXPECT_EQ(quad.slot, slot);
    EXPECT_NEAR(quad.first.x, x, Tolerance);
    EXPECT_NEAR(quad.first.y, 480.0f - y, Tolerance);
    EXPECT_NEAR(quad.third.x, x + width, Tolerance);
    EXPECT_NEAR(quad.third.y, 480.0f - y - height, Tolerance);
}

// Key `key`'s corner in the drawing: every 40 in x from 0xdf and every 38 in y from 0x9a.
std::tuple<float, float> KeyCorner(int key)
{
    return {static_cast<float>((key % 5) * 40 + 0xdf), static_cast<float>((key / 5) * 38 + 0x9a)};
}

class WarehouseKeypadTest : public ::testing::TestWithParam<std::int32_t>
{
protected:
    WarehouseKeypadTest() : m_world(std::make_unique<WorldState>()), m_text(m_sprites)
    {
        m_sprites.Begin(Screen640);
        m_world->Runtime().map = GetParam();
        // A made-up shuffled keypad.
        Runtime().npcWindows.keypad = {7, 3, 9, 0, 5, 1, 8, 2, 6, 4};
    }

    Client::Kernel::WorldRuntime& Runtime()
    {
        return m_world->Runtime();
    }

    Client::Core::Expected<void, std::string> Draw(std::int32_t x, std::int32_t y)
    {
        m_text.texts.clear();
        m_sprites.Begin(Screen640);
        Client::Kernel::WorldRuntime& runtime = Runtime();
        Client::Kernel::ActorState& hero = m_world->Hero();
        GameHudContext context{.world = *m_world,
                               .random = m_random,
                               .ownRecordDepth = runtime.ownRecordDepth,
                               .cellXDepth = hero.cellXAccesses,
                               .cellYDepth = hero.cellYAccesses,
                               .hud = runtime.hud,
                               .crywolfSandOverlay = runtime.crywolf.sandOverlay,
                               .sound = m_sound,
                               .sprites = m_sprites,
                               .text = m_text,
                               .texts = m_drawnTexts,
                               .mouse = {.x = x, .y = y}};
        return Client::Warehouse::DrawTheKeypad(context);
    }

    void Type(std::string_view digits)
    {
        Runtime().npcWindows.typedNumber.fill(0);
        for (std::size_t i = 0; i < digits.size(); ++i)
            Runtime().npcWindows.typedNumber[i] = static_cast<std::uint8_t>(digits[i]);
    }

    // 0x5e7bf0 with the mouse at (x, y), with the NPC windows' wiring as the UI step lends it.
    Client::Core::Expected<bool, std::string> Step(std::int32_t x, std::int32_t y)
    {
        Client::Hud::GamePanels panels(*m_world, m_random);
        const auto send = [this](Client::Protocol::Outgoing packet)
        {
            m_sent.push_back(std::move(packet));
            return Client::Core::Expected<void, std::string>{};
        };
        panels.LendTheNpcWindows(*m_world, m_random, Client::Items::InventoryServices{}, send);
        const Client::Hud::GameUiContext context{
            .world = *m_world, .random = m_random, .input = m_input, .mouseX = x, .mouseY = y, .sendNpcWindow = send};
        return Client::Warehouse::StepTheKeypad(context, panels.Registry());
    }

    // The click on key `key` (0 to 12), inside what 0x5e7b50 hits.
    Client::Core::Expected<bool, std::string> Press(int key)
    {
        Client::Kernel::LatchTheLeftButton(Runtime().mouse, true, true);
        if (key == OkKey || key == CancelKey)
            return Step(0x109 + 0x4e * (key - OkKey) + 5, 0xeb + 5);
        return Step((key % 5) * 40 + 0xdf + 5, (key / 5) * 40 + 0x96 + 5);
    }

    // The keys with the digits of `digits`, per the shuffled keypad.
    void PressDigits(std::string_view digits)
    {
        for (const char digit : digits)
        {
            const auto& keys = Runtime().npcWindows.keypad;
            const auto found = std::ranges::find(keys, static_cast<std::uint16_t>(digit - '0'));
            ASSERT_NE(found, keys.end()) << digit;
            const auto pressed = Press(static_cast<int>(std::distance(keys.begin(), found)));
            ASSERT_TRUE(pressed.HasValue()) << pressed.Error();
            ASSERT_TRUE(pressed.Value());
        }
    }

    [[nodiscard]] std::string Typed()
    {
        const auto& typed = Runtime().npcWindows.typedNumber;
        return {typed.begin(), std::ranges::find(typed, std::uint8_t{0})};
    }

    // Packet `index`'s C1 without the XOR32.
    [[nodiscard]] Client::Network::Packet Plain(std::size_t index) const
    {
        return Xor32Decode(m_sent.at(index).packet);
    }

    std::unique_ptr<WorldState> m_world;
    CrtRandom m_random{Seed};
    std::optional<Client::Audio::SoundId> m_sound{};
    SpriteBatch m_sprites;
    Client::Tests::RecordingTextDrawer m_text;
    Client::Tables::TextTable m_texts = Client::Tests::MakeTextTable(KeypadTexts());
    const Client::Tables::TextTable* m_drawnTexts = &m_texts;
    Client::Input::InputState m_input;
    std::vector<Client::Protocol::Outgoing> m_sent;
};

// 0x5e7b50: the eleven keys of 32 from (0xdf, 0x96) every 40, and the OK and cancel of 0x46 × 0x15 from (0x109, 0xeb)
// every 0x4e, with the top and left edges inside.
TEST_P(WarehouseKeypadTest, TheKeyUnderTheMouse)
{
    for (const auto& [x, y, key] :
         {std::tuple{0xdf, 0x96, 0}, std::tuple{0xdf + 31, 0x96 + 31, 0}, std::tuple{0xdf + 32, 0x96, -1},
          std::tuple{0xdf + 40, 0x96, 1}, std::tuple{0xdf + 160, 0x96, 4}, std::tuple{0xdf, 0x96 + 40, 5},
          std::tuple{0xdf, 0x96 + 39, -1}, std::tuple{0xdf + 160, 0xbe, 9}, std::tuple{0xdf, 0xe6, 10},
          std::tuple{0xdf + 40, 0xe6, -1}, std::tuple{0x109, 0xeb, 11}, std::tuple{0x109 + 0x45, 0xff, 11},
          std::tuple{0x109 + 0x46, 0xff, -1}, std::tuple{0x157, 0xeb, 12}, std::tuple{0x157, 0x100, -1},
          std::tuple{0x157, 0xea, -1}})
    {
        SCOPED_TRACE(testing::Message() << x << ", " << y);
        EXPECT_EQ(KeypadKeyAt(x, y), key);
    }
}

// 0x5e9b16..0x5e9b1d
TEST_P(WarehouseKeypadTest, ClosedItDrawsNothing)
{
    ASSERT_TRUE(Draw(0, 0).HasValue());

    EXPECT_TRUE(TexturedQuadsOf(m_sprites).empty());
    EXPECT_TRUE(m_text.texts.empty());
}

// 0x5e9b23..0x5ea042 with mode 1 (the four-digit password): the frame, the two lines (690 and 695), the 52 bar
// centered on 320, the keys with their number and the OK and cancel. No rand().
TEST_P(WarehouseKeypadTest, ThePasswordModeDrawsTheFrameTheBarAndTheKeys)
{
    Runtime().ui.field78abb20 = 1;

    ASSERT_TRUE(Draw(0, 0).HasValue());

    EXPECT_EQ(m_random.State(), Seed);
    const std::vector<DrawnQuad> quads = TexturedQuadsOf(m_sprites);
    ASSERT_EQ(quads.size(), KeypadQuads);
    ExpectQuadAt(quads[0], 0xfc, 213.0f, 100.0f, 213.0f, 5.0f);
    for (std::size_t row = 0; row < 4; ++row)
        ExpectQuadAt(quads[1 + row], 0xfb, 213.0f, 105.0f + 40.0f * static_cast<float>(row), 213.0f, 40.0f);
    ExpectQuadAt(quads[5], 0xfc, 213.0f, 265.0f, 213.0f, 5.0f);
    EXPECT_EQ(quads[5].color, White);
    ExpectQuadAt(quads[6], 0xfd, 294.0f, 134.0f, 52.0f, 16.0f);
    EXPECT_EQ(quads[6].color, (Color{0.3f, 0.3f, 0.3f, 1.0f}));
    for (int key = 0; key < 10; ++key)
    {
        SCOPED_TRACE(key);
        const auto [x, y] = KeyCorner(key);
        ExpectQuadAt(quads[7 + 2 * static_cast<std::size_t>(key)], 0x115, x, y, 32.0f, 32.0f);
        ExpectQuadAt(quads[8 + 2 * static_cast<std::size_t>(key)], 0x115, x, y, 32.0f, 32.0f);
        EXPECT_EQ(quads[7 + 2 * static_cast<std::size_t>(key)].color, Key);
    }
    ExpectQuadAt(quads[27], 0x115, 223.0f, 230.0f, 32.0f, 32.0f);
    ExpectQuadAt(quads[28], 0xfe, 223.0f, 230.0f, 32.0f, 32.0f);
    ExpectQuadAt(quads[29], 0xf1, 265.0f, 239.0f, 70.0f, 21.0f);
    ExpectQuadAt(quads[30], 0xf3, 343.0f, 239.0f, 70.0f, 21.0f);
    EXPECT_EQ(quads[29].color, White);

    const auto& texts = m_text.texts;
    ASSERT_EQ(texts.size(), 2U + 10U);
    EXPECT_EQ(texts[0].text, "Clave");
    EXPECT_EQ(std::tuple(texts[0].x, texts[0].y), std::tuple(320 - 15, 0x6e));
    EXPECT_EQ(texts[0].style.font, FontKind::Bold);
    EXPECT_EQ(texts[0].style.colors.text, 0xffffc4c4u);
    EXPECT_EQ(texts[0].style.colors.background, 0u);
    EXPECT_EQ(texts[1].text, "Cuatro");
    EXPECT_EQ(std::tuple(texts[1].x, texts[1].y), std::tuple(320 - 18, 0x7a));
    // The "*" of the empty typed digits is not drawn; the numbers, in the big bold font.
    EXPECT_EQ(texts[2].text, "7");
    EXPECT_EQ(std::tuple(texts[2].x, texts[2].y), std::tuple(0xdf + 0xc, 0x9a + 6));
    EXPECT_EQ(texts[2].style.font, FontKind::BigBold);
    EXPECT_EQ(texts[2].style.colors.text, 0xffc48080u);
    EXPECT_EQ(texts[11].text, "4");
    EXPECT_EQ(std::tuple(texts[11].x, texts[11].y), std::tuple(0xdf + 160 + 0xc, 0x9a + 38 + 6));
}

// 0x5e9d7b..0x5e9e12: the typed digits, a "*" per byte, at (digits − _ftol(half − 320), 0x84): with four digits,
// 4 + 294; with seven (modes 2, 3 and 6), the 91 bar and 7 − _ftol(−274.5) = 281.
TEST_P(WarehouseKeypadTest, TheTypedDigitsAreStars)
{
    Runtime().ui.field78abb20 = 1;
    Type("1234");
    ASSERT_TRUE(Draw(0, 0).HasValue());
    ASSERT_GE(m_text.texts.size(), 3U);
    EXPECT_EQ(m_text.texts[2].text, "****");
    EXPECT_EQ(std::tuple(m_text.texts[2].x, m_text.texts[2].y), std::tuple(298, 0x84));
    EXPECT_EQ(m_text.texts[2].style.font, FontKind::BigBold);
    EXPECT_EQ(m_text.texts[2].style.colors.text, 0xffc48080u);

    Runtime().ui.field78abb20 = 2;
    Type("98765");
    ASSERT_TRUE(Draw(0, 0).HasValue());
    const std::vector<DrawnQuad> quads = TexturedQuadsOf(m_sprites);
    ASSERT_EQ(quads.size(), KeypadQuads);
    ExpectQuadAt(quads[6], 0xfd, 274.5f, 134.0f, 91.0f, 16.0f);
    ASSERT_GE(m_text.texts.size(), 3U);
    EXPECT_EQ(m_text.texts[0].text, "Cambiar");
    EXPECT_EQ(m_text.texts[1].text, "Siete");
    EXPECT_EQ(m_text.texts[2].text, "*****");
    EXPECT_EQ(m_text.texts[2].x, 281);
}

// 0x5e9c15..0x5e9cb3: title 690 + (mode − 1) between 0 and 4, 6 with 691; prompt 697 with 2, 3 and 6, 696 with 5 and
// 695 with the others.
TEST_P(WarehouseKeypadTest, TheModeChoosesTheTitleAndThePrompt)
{
    for (const auto& [mode, title, prompt] :
         {std::tuple{1, "Clave", "Cuatro"}, std::tuple{2, "Cambiar", "Siete"}, std::tuple{3, "Quitar", "Siete"},
          std::tuple{4, "Nueva", "Cuatro"}, std::tuple{5, "Repetir", "Otra vez"}, std::tuple{6, "Cambiar", "Siete"}})
    {
        SCOPED_TRACE(mode);
        Runtime().ui.field78abb20 = mode;
        ASSERT_TRUE(Draw(0, 0).HasValue());
        ASSERT_GE(m_text.texts.size(), 2U);
        EXPECT_EQ(m_text.texts[0].text, title);
        EXPECT_EQ(m_text.texts[1].text, prompt);
    }
}

// 0x5e9e1c..0x5e9fb4: the hovered key (by 0x5e7b50, every 40 from 0x96) goes in (1, 1, 0.8) with the text in
// 0xff00c4c4; pressed (0x78abb3a), InventoryBox2 on top and the number one point higher. Key 8 is drawn at
// y = 0x9a + 38 = 192 and is hit from y = 190.
TEST_P(WarehouseKeypadTest, TheKeyUnderTheMouseLightsUpAndPresses)
{
    Runtime().ui.field78abb20 = 1;
    const int key = 8;
    const int mouseX = 0xdf + 3 * 40;
    const int mouseY = 0x96 + 40;
    ASSERT_TRUE(Draw(mouseX, mouseY).HasValue());
    std::vector<DrawnQuad> quads = TexturedQuadsOf(m_sprites);
    ASSERT_EQ(quads.size(), KeypadQuads);
    const auto [x, y] = KeyCorner(key);
    ExpectQuadAt(quads[7 + 2 * key], 0x115, x, y, 32.0f, 32.0f);
    EXPECT_EQ(quads[7 + 2 * key].color, HoveredKey);
    ExpectQuadAt(quads[8 + 2 * key], 0x115, x, y, 32.0f, 32.0f);
    EXPECT_EQ(quads[9 + 2 * key].color, Key);
    ASSERT_EQ(m_text.texts.size(), 12U);
    EXPECT_EQ(m_text.texts[2 + key].text, "6");
    EXPECT_EQ(m_text.texts[2 + key].y, 192 + 6);
    EXPECT_EQ(m_text.texts[2 + key].style.colors.text, 0xff00c4c4u);

    Runtime().npcWindows.keypadPressed = 1;
    ASSERT_TRUE(Draw(mouseX, mouseY).HasValue());
    quads = TexturedQuadsOf(m_sprites);
    ExpectQuadAt(quads[8 + 2 * key], 0x116, x, y, 32.0f, 32.0f);
    EXPECT_EQ(m_text.texts[2 + key].y, 192 + 5);
    EXPECT_EQ(m_text.texts[2 + key + 1].y, 192 + 6);
}

// 0x5e9ee3..0x5e9f27 and 0x5e9fee..0x5ea02a: erase is back1 (0xfe), back2 hovered and back3 pressed; the OK and cancel
// go up one when hovered.
TEST_P(WarehouseKeypadTest, TheEraseKeyAndTheButtonsLightUp)
{
    Runtime().ui.field78abb20 = 5;
    ASSERT_TRUE(Draw(0xdf, 0x96 + 80).HasValue());
    std::vector<DrawnQuad> quads = TexturedQuadsOf(m_sprites);
    ASSERT_EQ(quads.size(), KeypadQuads);
    ExpectQuadAt(quads[28], 0xff, 223.0f, 230.0f, 32.0f, 32.0f);
    EXPECT_EQ(quads[27].color, HoveredKey);

    Runtime().npcWindows.keypadPressed = 1;
    ASSERT_TRUE(Draw(0xdf, 0x96 + 80).HasValue());
    quads = TexturedQuadsOf(m_sprites);
    ExpectQuadAt(quads[28], 0x100, 223.0f, 230.0f, 32.0f, 32.0f);

    ASSERT_TRUE(Draw(0x109, 0xeb).HasValue());
    quads = TexturedQuadsOf(m_sprites);
    EXPECT_EQ(quads[28].slot, 0xfe);
    EXPECT_EQ(quads[29].slot, 0xf2);
    EXPECT_EQ(quads[30].slot, 0xf3);
    ASSERT_TRUE(Draw(0x157, 0xff).HasValue());
    quads = TexturedQuadsOf(m_sprites);
    EXPECT_EQ(quads[29].slot, 0xf1);
    EXPECT_EQ(quads[30].slot, 0xf4);
}

TEST_P(WarehouseKeypadTest, AMissingTextStopsWithItsAddress)
{
    Runtime().ui.field78abb20 = 3;
    m_drawnTexts = nullptr;

    const auto drawn = Draw(0, 0);

    ASSERT_FALSE(drawn.HasValue());
    EXPECT_NE(drawn.Error().find("0x5e9c63"), std::string::npos) << drawn.Error();
}

// 0x5e7c12..0x5e7c19: with the mode at 0 it returns 0 without touching the click, and 0x5ee140 goes on.
TEST_P(WarehouseKeypadTest, ClosedTheStepLetsTheClickThrough)
{
    Client::Kernel::LatchTheLeftButton(Runtime().mouse, true, true);

    const auto stepped = Step(0xdf + 5, 0x96 + 5);

    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_FALSE(stepped.Value());
    EXPECT_TRUE(Runtime().mouse.pendingClick);
    EXPECT_TRUE(Runtime().mouse.leftHeld);
    EXPECT_EQ(Runtime().npcWindows.keypadPressed, 0);
    EXPECT_FALSE(Runtime().lastSoundId.has_value());
}

// 0x5e7c2b..0x5e7ce5 and 0x5e9aad..0x5e9ae1: the key's digit is appended with sound 0x19 up to mode 1's four; the key
// stays pressed (0x78abb3a) and the click and the button down at 0. Erase removes the last; without a click the step
// also returns 1, and the released button releases the key. No rand().
TEST_P(WarehouseKeypadTest, TheKeysTypeEraseAndPress)
{
    Runtime().ui.field78abb20 = 1;

    PressDigits("90");
    EXPECT_EQ(Typed(), "90");
    EXPECT_EQ(Runtime().lastSoundId, Client::Audio::SoundId{0x19});
    EXPECT_EQ(Runtime().npcWindows.keypadPressed, 1);
    EXPECT_FALSE(Runtime().mouse.pendingClick);
    EXPECT_FALSE(Runtime().mouse.leftHeld);

    PressDigits("123");
    EXPECT_EQ(Typed(), "9012");
    ASSERT_TRUE(Press(EraseKey).HasValue());
    EXPECT_EQ(Typed(), "901");
    ASSERT_TRUE(Press(EraseKey).HasValue());
    ASSERT_TRUE(Press(EraseKey).HasValue());
    ASSERT_TRUE(Press(EraseKey).HasValue());
    ASSERT_TRUE(Press(EraseKey).HasValue());
    EXPECT_EQ(Typed(), "");

    Runtime().mouse.releasedClick = true;
    const auto released = Step(0, 0);
    ASSERT_TRUE(released.HasValue()) << released.Error();
    EXPECT_TRUE(released.Value());
    EXPECT_EQ(Runtime().npcWindows.keypadPressed, 0);
    EXPECT_EQ(Runtime().ui.field78abb20, 1);

    // The click outside the keys also presses it and is spent.
    Runtime().mouse.releasedClick = false;
    Client::Kernel::LatchTheLeftButton(Runtime().mouse, true, true);
    ASSERT_TRUE(Step(0, 0).HasValue());
    EXPECT_EQ(Runtime().npcWindows.keypadPressed, 1);
    EXPECT_FALSE(Runtime().mouse.pendingClick);
    EXPECT_EQ(m_random.State(), Seed);
    EXPECT_TRUE(m_sent.empty());
}

// 0x5e7cf9..0x5e7d0a and 0x5e7cea: the OK with fewer digits does nothing; cancel sets the mode to 0 and keeps the
// typed digits.
TEST_P(WarehouseKeypadTest, OkWaitsForEveryDigitAndCancelCloses)
{
    Runtime().ui.field78abb20 = 2;
    PressDigits("123456");
    ASSERT_TRUE(Press(OkKey).HasValue());
    EXPECT_EQ(Runtime().ui.field78abb20, 2);
    EXPECT_TRUE(m_sent.empty());

    ASSERT_TRUE(Press(CancelKey).HasValue());
    EXPECT_EQ(Runtime().ui.field78abb20, 0);
    EXPECT_EQ(Typed(), "123456");
    EXPECT_TRUE(m_sent.empty());
    EXPECT_EQ(m_random.State(), Seed);
}

// 0x5e931c..0x5e9a88 (Q-C924): mode 1's OK sends C1 10 83 00, the atoi of the typed digits and the first ten bytes of
// the chat message buffer (0x7798bf8), with the XOR32 and in C3 (its 2 rand()), and leaves the mode at 0.
TEST_P(WarehouseKeypadTest, UnlockingSends83WithTheChatBuffer)
{
    Runtime().ui.field78abb20 = 1;
    constexpr std::string_view Chat = "hola mundo, chau";
    std::ranges::copy(Chat, Runtime().chat.input.messageBuffer.begin());
    PressDigits("4821");
    const CrtRandom expected = Advanced(m_random, C3);

    ASSERT_TRUE(Press(OkKey).HasValue());

    EXPECT_EQ(m_random.State(), expected.State());
    EXPECT_EQ(Runtime().ui.field78abb20, 0);
    ASSERT_EQ(m_sent.size(), 1U);
    EXPECT_EQ(m_sent[0].delivery, Delivery::Encrypted);
    EXPECT_TRUE(SameBytes(Plain(0).Bytes(), Bytes({0xc1, 0x10, 0x83, 0x00, 0xd5, 0x12, 'h', 'o', 'l', 'a', ' ', 'm',
                                                   'u', 'n', 'd', 'o'})));
    EXPECT_EQ(Typed(), "4821");
}

// 0x5e7d10..0x5e7d5b: with four digits, four equal ones empty the typed digits and open the 0x87 without closing the
// keypad; with the personal number's seven, they are not checked.
TEST_P(WarehouseKeypadTest, FourEqualDigitsOpenTheDialog)
{
    Runtime().ui.field78abb20 = 4;
    PressDigits("5555");
    ASSERT_TRUE(Press(OkKey).HasValue());
    EXPECT_EQ(Runtime().dialogs.current, 0x87);
    EXPECT_EQ(Runtime().ui.field78abb20, 4);
    EXPECT_EQ(Typed(), "");
    EXPECT_EQ(m_random.State(), Seed);

    Runtime().dialogs.current = 0;
    Runtime().ui.field78abb20 = 3;
    PressDigits("5555555");
    ASSERT_TRUE(Press(OkKey).HasValue());
    EXPECT_EQ(Runtime().dialogs.current, 0);
    ASSERT_EQ(m_sent.size(), 1U);
}

// 0x5e7d80..0x5e7e8b and 0x5e7e90..0x5e854d: mode 4 stores the password (0x78aba07) and shuffles again (40 rand()) in
// 5; the same one moves its atoi to 0x78abb24 and goes on in 2 without shuffling; the personal number sends
// C1 10 83 01 with the password and the ten bytes of the typed digits, with the XOR32 and in C3.
TEST_P(WarehouseKeypadTest, SettingAPasswordSends83WithOne)
{
    Runtime().ui.field78abb20 = 4;
    PressDigits("2580");
    CrtRandom keys = m_random;
    std::array<std::uint16_t, Client::Npc::WarehouseKeyCount> keypad{};
    Client::Items::ShuffleTheKeypad(keys, keypad);

    ASSERT_TRUE(Press(OkKey).HasValue());
    EXPECT_EQ(m_random.State(), keys.State());
    EXPECT_EQ(Runtime().npcWindows.keypad, keypad);
    EXPECT_EQ(Runtime().ui.field78abb20, 5);
    EXPECT_EQ(Typed(), "");
    EXPECT_EQ(Runtime().npcWindows.newPassword, (std::array<std::uint8_t, 4>{'2', '5', '8', '0'}));

    PressDigits("2580");
    ASSERT_TRUE(Press(OkKey).HasValue());
    EXPECT_EQ(m_random.State(), keys.State());
    EXPECT_EQ(Runtime().npcWindows.keypad, keypad);
    EXPECT_EQ(Runtime().ui.field78abb20, 2);
    EXPECT_EQ(Runtime().npcWindows.warehousePassword, 2580);
    EXPECT_EQ(Typed(), "");

    PressDigits("1234567");
    const CrtRandom expected = Advanced(m_random, C3);
    ASSERT_TRUE(Press(OkKey).HasValue());
    EXPECT_EQ(m_random.State(), expected.State());
    EXPECT_EQ(Runtime().ui.field78abb20, 0);
    ASSERT_EQ(m_sent.size(), 1U);
    EXPECT_EQ(m_sent[0].delivery, Delivery::Encrypted);
    EXPECT_TRUE(SameBytes(Plain(0).Bytes(), Bytes({0xc1, 0x10, 0x83, 0x01, 0x14, 0x0a, '1', '2', '3', '4', '5', '6',
                                                   '7', 0x00, 0x00, 0x00})));
}

// 0x5e7e7e..0x5e7e8b: a different repeated password opens the 0x88 with the keypad already closed.
TEST_P(WarehouseKeypadTest, ADifferentRepeatOpensTheDialog)
{
    Runtime().ui.field78abb20 = 5;
    Runtime().npcWindows.newPassword = {'2', '5', '8', '0'};
    PressDigits("2581");

    ASSERT_TRUE(Press(OkKey).HasValue());

    EXPECT_EQ(Runtime().dialogs.current, 0x88);
    EXPECT_EQ(Runtime().ui.field78abb20, 0);
    EXPECT_EQ(Runtime().npcWindows.warehousePassword, 0);
    EXPECT_EQ(Typed(), "2581");
    EXPECT_TRUE(m_sent.empty());
    EXPECT_EQ(m_random.State(), Seed);
}

// 0x5e8c02..0x5e931a: mode 3 (the lock) sends C1 10 83 02 with the password at 0 and the typed digits.
TEST_P(WarehouseKeypadTest, RemovingThePasswordSends83WithTwo)
{
    Runtime().ui.field78abb20 = 3;
    Runtime().npcWindows.warehousePassword = 2580;
    PressDigits("7654321");
    const CrtRandom expected = Advanced(m_random, C3);

    ASSERT_TRUE(Press(OkKey).HasValue());

    EXPECT_EQ(m_random.State(), expected.State());
    ASSERT_EQ(m_sent.size(), 1U);
    EXPECT_EQ(m_sent[0].delivery, Delivery::Encrypted);
    EXPECT_TRUE(SameBytes(Plain(0).Bytes(), Bytes({0xc1, 0x10, 0x83, 0x02, 0x00, 0x00, '7', '6', '5', '4', '3', '2',
                                                   '1', 0x00, 0x00, 0x00})));
}

// 0x5e8552..0x5e8bfd (Q-C925): mode 6 sends the C1 03 31 in the clear, closes everything (0x657a90: the warehouse
// sends its C1 03 82) and sends C1 0D 99 with the typed digits, with the XOR32 and in C3.
TEST_P(WarehouseKeypadTest, ThePersonalNumberClosesEverythingAndSends99)
{
    Runtime().ui.field78abb20 = 6;
    Runtime().panels.warehouse = 1;
    Runtime().panels.inventory = 1;
    PressDigits("1112223");
    const CrtRandom expected = Advanced(m_random, CloseAllRandoms + C3);

    const auto pressed = Press(OkKey);

    ASSERT_TRUE(pressed.HasValue()) << pressed.Error();
    EXPECT_EQ(m_random.State(), expected.State());
    EXPECT_EQ(Runtime().ui.field78abb20, 0);
    EXPECT_EQ(Runtime().panels.warehouse, 0);
    EXPECT_EQ(Runtime().panels.inventory, 0);
    ASSERT_EQ(m_sent.size(), 3U);
    EXPECT_EQ(m_sent[0].delivery, Delivery::Clear);
    EXPECT_TRUE(SameBytes(m_sent[0].packet.Bytes(), Bytes({0xc1, 0x03, 0x31})));
    EXPECT_TRUE(SameBytes(m_sent[1].packet.Bytes(), Bytes({0xc1, 0x03, 0x82})));
    EXPECT_EQ(m_sent[2].delivery, Delivery::Encrypted);
    EXPECT_TRUE(
        SameBytes(Plain(2).Bytes(), Bytes({0xc1, 0x0d, 0x99, '1', '1', '1', '2', '2', '2', '3', 0x00, 0x00, 0x00})));
}

INSTANTIATE_TEST_SUITE_P(Maps, WarehouseKeypadTest, ::testing::Values(Lorencia, Devias, Noria));
} // namespace
