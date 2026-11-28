#include "ChaosMix/ChaosBoxDrawing.h"

#include "Core/Expected.h"
#include "Core/PixelSize.h"
#include "Crt/CrtRandom.h"
#include "Tables/TextTable.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/TextStyle.h"
#include "Kernel/ActorState.h"
#include "Hud/GameHud.h"
#include "Hud/GamePanels.h"
#include "Npc/NpcWindowState.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Kernel/ItemState.h"
#include "Audio/SoundId.h"
#include "support/RecordingTextDrawer.h"
#include "support/SceneDataFiles.h"
#include "Events/EventGlobals.h"
#include "Ui/GameUiState.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// The chaos box drawing (0x5febf0, CL9 milestone 6, phase 3; 06 § La caja del caos), at (0x104, 0) in 640 × 480: the
// window, the grid, the mode's title with the tax, the mix's name, rate and price, the warnings and the mix button.
// The texts are made up and measure 6 columns per letter. It takes no rand().
namespace
{
using Client::Crt::CrtRandom;
using Client::Hud::GameHudContext;
using Client::Kernel::ItemState;
using Client::Kernel::WorldState;
using Client::Npc::NpcWindowState;
using Client::Render::SpriteBatch;
using Client::Render::Text::FontKind;
using Client::Tests::RecordedText;

constexpr Client::Core::PixelSize Screen640{640, 480};
constexpr std::uint32_t Seed = 0x0c4a05u;
constexpr std::int32_t Left = 0x104;
constexpr int NoTexture = -1;
constexpr int Marker = -2;
// 0x5ff6f0: the mix button, 44 × 32 at (Left + 75, 300).
constexpr std::int32_t ButtonX = Left + 75;
constexpr std::int32_t ButtonY = 300;

std::map<std::size_t, std::string> ChaosTexts()
{
    return {{583, "Caos"},       {584, "%s: %d%%"},     {586, "joya para %s"}, {587, "y nada mas"},
            {588, "ojo con %s"}, {589, "puede fallar"}, {590, "o romperse"},   {591, "mezcla"},
            {601, "nada"},       {602, "arma"},         {603, "boleto"},       {604, "mas diez"},
            {606, "aviso uno"},  {607, "aviso dos"},    {608, "aviso tres"},   {610, "falla uno"},
            {611, "falla dos"},  {612, "unicornio"},    {614, "alas"},         {664, "+% d creativo"},
            {735, "Normal"},     {736, "Arma"},         {1172, "capa"},        {1412, "crear"},
            {1413, "bendicion"}, {1622, "zen %s (%s)"}, {1623, "tasa %d%%"},   {1636, "pocion %s (%s)"},
            {1863, "lobo"},      {1921, "cuerno"},      {1923, "fenrir"}};
}

// The slots of the batch's quads, in order (NoTexture without a texture, Marker for a text's mark).
std::vector<int> SlotsOf(const SpriteBatch& sprites)
{
    std::vector<int> slots;
    for (const Client::Render::DrawCommand& command : sprites.Commands())
    {
        int slot = NoTexture;
        if (command.texture == Client::Tests::TextMarkerSlot)
            slot = Marker;
        else if (command.texture)
            slot = command.texture->value;
        for (std::size_t quad = 0; quad < command.vertexCount / 6; ++quad)
            slots.push_back(slot);
    }
    return slots;
}

struct Corner
{
    float x = 0.0f;
    float y = 0.0f;
};

// The top-left corner of quad `index` with `slot`, in 640 × 480.
std::optional<Corner> CornerOf(const SpriteBatch& sprites, int slot, std::size_t index = 0)
{
    const auto vertices = sprites.Vertices();
    std::size_t seen = 0;
    for (const Client::Render::DrawCommand& command : sprites.Commands())
    {
        if (!command.texture || command.texture->value != slot)
            continue;
        for (std::size_t quad = 0; quad < command.vertexCount / 6; ++quad)
        {
            if (seen++ != index)
                continue;
            const Client::Render::SpriteVertex& first = vertices[command.firstVertex + quad * 6];
            return Corner{first.x, 480.0f - first.y};
        }
    }
    return std::nullopt;
}

class ChaosBoxDrawingTest : public ::testing::Test
{
protected:
    ChaosBoxDrawingTest() : m_world(std::make_unique<WorldState>()), m_text(m_sprites)
    {
        m_sprites.Begin(Screen640);
        Runtime().panels.chaosBox = 1;
        for (ItemState& cell : Npc().chaosBox)
            cell = ItemState{.id = 0xffff};
    }

    Client::Kernel::WorldRuntime& Runtime()
    {
        return m_world->Runtime();
    }

    NpcWindowState& Npc()
    {
        return Runtime().npcWindows;
    }

    Client::Core::Expected<void, std::string> Draw(std::int32_t x = 0, std::int32_t y = 0, bool leftDown = false)
    {
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
                               .mouse = {.x = x, .y = y},
                               .leftDown = leftDown};
        Client::Hud::GamePanels panels(*m_world, m_random);
        return Client::ChaosMix::DrawTheChaosBox(context, panels.Registry());
    }

    // What was drawn from the name's row on, without the title.
    [[nodiscard]] std::vector<RecordedText> Lines() const
    {
        std::vector<RecordedText> lines;
        for (const RecordedText& text : m_text.texts)
        {
            if (!text.windowed)
                lines.push_back(text);
        }
        return lines;
    }

    void Restart()
    {
        m_text.texts.clear();
        m_sprites.Begin(Screen640);
    }

    std::unique_ptr<WorldState> m_world;
    CrtRandom m_random{Seed};
    std::optional<Client::Audio::SoundId> m_sound{};
    SpriteBatch m_sprites;
    Client::Tests::RecordingTextDrawer m_text;
    Client::Tables::TextTable m_texts = Client::Tests::MakeTextTable(ChaosTexts());
    const Client::Tables::TextTable* m_drawnTexts = &m_texts;
};

void ExpectLine(const RecordedText& line, std::int32_t y, const std::string& text, std::uint32_t color)
{
    EXPECT_EQ(line.x, Left + 0x14) << text;
    EXPECT_EQ(line.y, y) << text;
    EXPECT_EQ(line.text, text);
    EXPECT_EQ(line.style.colors.text, color) << text;
    EXPECT_EQ(line.style.colors.background, 0x80282828u) << text;
    EXPECT_EQ(line.style.font, FontKind::Bold) << text;
}

// 0x5febf0 with the 3 and luck, mode 0 and tax at 3, in Lorencia, Devias and Noria (the chaos goblin's): the window
// (0x104 and 0x105), the 32 cells (0x115 empty, 0x116 with the sword), title 735 and the rate in the 120-column box,
// name 604 in light blue, rate 50 + 25, price 2000000 with the tax (2060000), the chaos jewel, the 3's three warnings
// and button 0x124. Leaves the price in 0x7d2dca0 and the corner at (0x104, 0).
TEST_F(ChaosBoxDrawingTest, TheUpgradeShowsItsNameRateAndTaxedPrice)
{
    for (const std::int32_t map : {0, 2, 3})
    {
        SCOPED_TRACE(map);
        Restart();
        Runtime().map = map;
        Runtime().ui.npcWindowX = 0;
        Npc().chaosBox[0] = ItemState{.id = 0x0001, .occupied = 1};
        Npc().chaosBox[8] = ItemState{.id = 0x0001};
        Npc().mixKind = 3;
        Npc().mixLuck = 1;
        Npc().chaosMixMode = 0;
        Npc().chaosTaxRate = 3;

        const auto drawn = Draw();
        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();

        EXPECT_EQ(m_random.State(), Seed);
        EXPECT_EQ(Runtime().ui.npcWindowX, Left);
        EXPECT_EQ(Runtime().ui.npcWindowY, 0);
        EXPECT_EQ(Npc().mixPrice, 2'000'000u);
        const std::vector<int> slots = SlotsOf(m_sprites);
        ASSERT_GE(slots.size(), 34u);
        EXPECT_EQ(slots[0], 0x104);
        EXPECT_EQ(slots[1], 0x105);
        EXPECT_EQ(slots[2], 0x116);
        EXPECT_EQ(slots[3], 0x115);
        EXPECT_EQ(slots[10], 0x116);
        EXPECT_EQ(slots[33], 0x115);
        EXPECT_EQ(slots.back(), 0x124);
        const auto button = CornerOf(m_sprites, 0x124);
        ASSERT_TRUE(button.has_value());
        EXPECT_FLOAT_EQ(button->x, static_cast<float>(ButtonX));
        EXPECT_FLOAT_EQ(button->y, static_cast<float>(ButtonY));
        const auto box = CornerOf(m_sprites, 0x116, 1);
        ASSERT_TRUE(box.has_value());
        EXPECT_FLOAT_EQ(box->x, Left + 15.0f);
        EXPECT_FLOAT_EQ(box->y, 130.0f);

        ASSERT_GE(m_text.texts.size(), 2u);
        const RecordedText& title = m_text.texts[0];
        EXPECT_TRUE(title.windowed);
        EXPECT_EQ(title.text, "Normal");
        EXPECT_EQ(title.x, Left + 0x23);
        EXPECT_EQ(title.y, 8);
        EXPECT_EQ(title.columns, 120);
        EXPECT_EQ(title.style.colors.text, 0xffdcdcdcu);
        EXPECT_EQ(title.style.colors.background, 0xff141414u);
        EXPECT_EQ(m_text.texts[1].text, "tasa 3%");
        EXPECT_EQ(m_text.texts[1].y, 0x14);

        const std::vector<RecordedText> lines = Lines();
        ASSERT_EQ(lines.size(), 8u);
        ExpectLine(lines[0], 0x32, "mas diez", 0xff30ffffu);
        ExpectLine(lines[1], 0x46, "mezcla: 75%", 0xffffe6d2u);
        ExpectLine(lines[2], 0x5a, "zen 2,060,000 (2,000,000)", 0xffffe6d2u);
        ExpectLine(lines[3], 0xd2, "joya para mezcla", 0xff1432ffu);
        ExpectLine(lines[4], 0xe1, "y nada mas", 0xff1432ffu);
        ExpectLine(lines[5], 0xf0, "aviso uno", 0xff1432ffu);
        ExpectLine(lines[6], 0xff, "aviso dos", 0xff1432ffu);
        ExpectLine(lines[7], 0x10e, "aviso tres", 0xff1432ffu);
    }
}

// 0x5ff6f0..0x5ff816: with the mouse over the button (x87: [x, x + 44) × [y, y + 32)), the pressed button (0x125) if
// the left is down, and help 591 in the normal font at (x, y − 13). Outside, nothing more.
TEST_F(ChaosBoxDrawingTest, TheMixButtonPressesAndShowsItsHelp)
{
    Npc().mixKind = 0;
    ASSERT_TRUE(Draw(ButtonX + 43, ButtonY + 31, true).HasValue());
    EXPECT_EQ(SlotsOf(m_sprites).back(), Marker);
    EXPECT_TRUE(CornerOf(m_sprites, 0x125).has_value());
    const RecordedText& help = m_text.texts.back();
    EXPECT_EQ(help.text, "mezcla");
    EXPECT_EQ(help.x, ButtonX);
    EXPECT_EQ(help.y, ButtonY - 13);
    EXPECT_EQ(help.style.font, FontKind::Normal);
    EXPECT_EQ(Runtime().hud.dcFont, FontKind::Normal);
    EXPECT_EQ(Runtime().hud.textColors.text, 0xffffffffu);
    EXPECT_EQ(Runtime().hud.textColors.background, 0u);

    Restart();
    ASSERT_TRUE(Draw(ButtonX + 10, ButtonY + 10, false).HasValue());
    EXPECT_FALSE(CornerOf(m_sprites, 0x125).has_value());
    EXPECT_EQ(m_text.texts.back().text, "mezcla");

    for (const auto& [x, y] : {std::pair{ButtonX - 1, ButtonY}, std::pair{ButtonX + 44, ButtonY},
                               std::pair{ButtonX, ButtonY - 1}, std::pair{ButtonX, ButtonY + 32}})
    {
        SCOPED_TRACE(testing::Message() << x << ", " << y);
        Restart();
        ASSERT_TRUE(Draw(x, y, true).HasValue());
        EXPECT_EQ(SlotsOf(m_sprites).back(), 0x124);
        EXPECT_EQ(Runtime().hud.dcFont, FontKind::Bold);
    }
}

// 0x5ff0c5..0x5ff2cd and 0x5ff2f2..0x5ff3e4: without a mix (EDI 0) name 601 in red, rate 0 and price "0"; in mode 1,
// warnings 588 (with 591), 589 and 590 and title 736. The 0xb shows as the wings (614, light blue). The 2 with
// 0x7d2dc9d appends "+ n%" in light blue to the same buffer.
TEST_F(ChaosBoxDrawingTest, TheNameAndRateFollowTheKind)
{
    Npc().mixKind = 0;
    Npc().chaosMixMode = 1;
    ASSERT_TRUE(Draw().HasValue());
    EXPECT_EQ(m_text.texts[0].text, "Arma");
    std::vector<RecordedText> lines = Lines();
    ASSERT_EQ(lines.size(), 8u);
    ExpectLine(lines[0], 0x32, "nada", 0xff3030ffu);
    ExpectLine(lines[1], 0x46, "mezcla: 0%", 0xffffe6d2u);
    ExpectLine(lines[2], 0x5a, "zen 0 (0)", 0xffffe6d2u);
    ExpectLine(lines[5], 0xf0, "ojo con mezcla", 0xff1432ffu);
    ExpectLine(lines[6], 0xff, "puede fallar", 0xff1432ffu);
    ExpectLine(lines[7], 0x10e, "o romperse", 0xff1432ffu);
    EXPECT_EQ(Npc().mixPrice, 0u);

    Restart();
    Npc().mixKind = 0xb;
    Npc().chaosMixMode = 0;
    ASSERT_TRUE(Draw().HasValue());
    lines = Lines();
    ASSERT_EQ(lines.size(), 5u);
    ExpectLine(lines[0], 0x32, "alas", 0xff30ffffu);

    Restart();
    Npc().mixKind = 2;
    Npc().mixLevel = 2;
    Npc().field7d2dc9d = 5;
    ASSERT_TRUE(Draw().HasValue());
    lines = Lines();
    ExpectLine(lines[0], 0x32, "boleto", 0xff30ffffu);
    ExpectLine(lines[1], 0x46, "mezcla: 75% + 5%", 0xff30ffffu);
    ExpectLine(lines[2], 0x5a, "zen 100,000 (100,000)", 0xffffe6d2u);
}

// 0x5ff109..0x5ff280: the built names (0x18 with 1172, 0x16 with 664 and the type − 10, 0xf with "%s %s" and 1412,
// 0x1c with "%s %s %s" and the cape) and the potions' price (1636, with 590 below drawn first); the 5 with warnings 610
// and 611.
TEST_F(ChaosBoxDrawingTest, TheBuiltNamesAndThePotionPrice)
{
    Npc().mixKind = 0x18;
    Npc().mixWingRate = 40;
    ASSERT_TRUE(Draw().HasValue());
    EXPECT_EQ(Lines()[0].text, "capa");
    EXPECT_EQ(Lines()[1].text, "mezcla: 40%");

    Restart();
    Npc().mixKind = 0x16;
    ASSERT_TRUE(Draw().HasValue());
    EXPECT_EQ(Lines()[0].text, "+ 12 creativo");

    Restart();
    Npc().mixKind = 0xf;
    ASSERT_TRUE(Draw().HasValue());
    std::vector<RecordedText> lines = Lines();
    ASSERT_EQ(lines.size(), 6u);
    EXPECT_EQ(lines[0].text, "bendicion crear");
    ExpectLine(lines[2], 0x10e, "o romperse", 0xffffe6d2u);
    ExpectLine(lines[3], 0x5a, "pocion 100,000 (100,000)", 0xffffe6d2u);

    Restart();
    Npc().mixKind = 0x1c;
    Npc().mixCapeKind = 3;
    ASSERT_TRUE(Draw().HasValue());
    EXPECT_EQ(Lines()[0].text, "fenrir lobo crear");

    Restart();
    Npc().mixKind = 0x19;
    ASSERT_TRUE(Draw().HasValue());
    EXPECT_EQ(Lines()[0].text, "cuerno crear");

    Restart();
    Npc().mixKind = 5;
    ASSERT_TRUE(Draw().HasValue());
    lines = Lines();
    ASSERT_EQ(lines.size(), 7u);
    EXPECT_EQ(lines[5].text, "falla uno");
    EXPECT_EQ(lines[6].text, "falla dos");
}

// 0x5fecaa..0x5fece7 and 0x5feda3: mode 2 onward sets title 583 at 0xc without the rate; with the mix requested
// (0x7d2dc80 at 2), the price at 0 and nothing but the window, the grid and the title. Closed, nothing.
TEST_F(ChaosBoxDrawingTest, TheOtherModesAndTheRequestedMix)
{
    Npc().chaosMixMode = 2;
    Npc().chaosMixState = 2;
    Npc().mixPrice = 77;
    ASSERT_TRUE(Draw().HasValue());
    ASSERT_EQ(m_text.texts.size(), 1u);
    EXPECT_EQ(m_text.texts[0].text, "Caos");
    EXPECT_EQ(m_text.texts[0].y, 0xc);
    EXPECT_EQ(Npc().mixPrice, 0u);
    EXPECT_EQ(SlotsOf(m_sprites).size(), 2u + 32u + 1u);

    Restart();
    Runtime().panels.chaosBox = 0;
    Runtime().ui.npcWindowX = 7;
    ASSERT_TRUE(Draw().HasValue());
    EXPECT_TRUE(SlotsOf(m_sprites).empty());
    EXPECT_EQ(Runtime().ui.npcWindowX, 7);
}

// 0x5fee3a: the level-8 2 reads the first dword of 0x6c9dc0's format and keeps drawing; without Text.bmd, the error
// keeps the title's address.
TEST_F(ChaosBoxDrawingTest, TheNativeDataPastTheRatesStillDrawsItsQuote)
{
    Npc().mixKind = 2;
    Npc().mixLevel = 8;
    const auto past = Draw();
    ASSERT_TRUE(past.HasValue()) << past.Error();
    EXPECT_EQ(Npc().mixPrice, 1'600'000u);
    bool rateDrawn = false;
    for (const auto& text : m_text.texts)
        rateDrawn = rateDrawn || text.text == "mezcla: 723546917%";
    EXPECT_TRUE(rateDrawn);

    Restart();
    m_drawnTexts = nullptr;
    const auto missing = Draw();
    ASSERT_FALSE(missing.HasValue());
    EXPECT_NE(missing.Error().find("0x5fed39"), std::string::npos) << missing.Error();
}
} // namespace

TEST_F(ChaosBoxDrawingTest, HighInvitationsReadTheLiveGlowMapAndCollapseGlobals)
{
    Runtime().ui.glowDetail = 2;
    Runtime().map = 3;
    Runtime().events.objectCollapse = {.map = 18, .type = 77, .counter = 99, .delay = 1.25f};
    Npc().mixKind = 2;
    const std::array<std::string_view, 6> expected{"mezcla: 2%",  "mezcla: 3%",  "mezcla: 77%",
                                                   "mezcla: 18%", "mezcla: 99%", "mezcla: 1067450368%"};
    for (std::size_t index = 0; index < expected.size(); ++index)
    {
        Restart();
        Npc().mixLevel = 11 + static_cast<std::int32_t>(index);
        const auto drawn = Draw();
        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
        ASSERT_GE(Lines().size(), 2u);
        EXPECT_EQ(Lines()[1].text, expected[index]);
    }
}
