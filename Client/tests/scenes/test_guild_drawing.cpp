#include "Guild/GuildDrawing.h"

#include "Core/Expected.h"
#include "Core/PixelSize.h"
#include "Crt/CrtRandom.h"
#include "Tables/TextTable.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/FontSet.h"
#include "Render/TextureSlot.h"
#include "Kernel/ActorState.h"
#include "Events/EventGlobals.h"
#include "Ui/GameHudContext.h"
#include "Hud/GamePanels.h"
#include "Guild/GuildMarks.h"
#include "Guild/GuildState.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Ui/OldWindowDraw.h"
#include "Ui/PanelDialog.h"
#include "Ui/PanelRegistry.h"
#include "Audio/SoundId.h"

#include "support/FakeTextureStore.h"
#include "support/RecordingTextDrawer.h"
#include "support/SceneDataFiles.h"

#include <gtest/gtest.h>

#include <array>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Guild::DrawTheWarScore and DrawThePanelDialog (CL9 milestone 8, phase 2): the guild war score (0x5a7640) and the
// panels' dialog (0x660830), against their ASM, in Lorencia (0), Devias (2) and Arena (6, the war's soccer one). The
// names and the texts are invented.
namespace
{
using Client::Crt::CrtRandom;
using Client::Guild::GuildMarkTextures;
using Client::Ui::GameHudContext;
using Client::Kernel::WorldState;
using Client::Render::BlendMode;
using Client::Render::SpriteBatch;
using Client::Render::SpriteVertex;

constexpr std::uint32_t Seed = 7;
constexpr Client::Core::PixelSize Screen640{640, 480};
constexpr int NoTexture = -1;
constexpr int Marker = -2;
constexpr std::int32_t HeroGuild = 3;
constexpr std::int32_t RivalGuild = 5;

struct DrawnQuad
{
    int slot = NoTexture;
    BlendMode blend = BlendMode::Alpha;
    bool alphaTest = true;
    SpriteVertex first{};
    SpriteVertex third{};
};

std::vector<DrawnQuad> QuadsOf(const SpriteBatch& sprites)
{
    std::vector<DrawnQuad> quads;
    const auto vertices = sprites.Vertices();
    for (const Client::Render::DrawCommand& command : sprites.Commands())
    {
        int slot = NoTexture;
        if (command.texture == Client::Tests::TextMarkerSlot)
            slot = Marker;
        else if (command.texture)
            slot = command.texture->value;
        for (std::size_t quad = 0; quad < command.vertexCount / 6; ++quad)
        {
            const std::size_t first = command.firstVertex + quad * 6;
            quads.push_back(DrawnQuad{.slot = slot,
                                      .blend = command.blend,
                                      .alphaTest = command.alphaTest,
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

template <std::size_t Size> void Fill(std::array<char, Size>& target, std::string_view text)
{
    target = {};
    for (std::size_t i = 0; i < text.size() && i + 1 < Size; ++i)
        target[i] = text[i];
}

int Draws(const CrtRandom& random)
{
    CrtRandom reference(Seed);
    for (int draws = 0; draws <= 1000; ++draws)
    {
        if (reference.State() == random.State())
            return draws;
        static_cast<void>(reference.Next());
    }
    return -1;
}

int MarkSlot(std::int32_t index)
{
    return GuildMarkTextures::SlotOf(index, true).value;
}

// The dialog buttons' texts (0x65feb0), invented.
Client::Tables::TextTable ButtonTexts()
{
    const std::map<std::size_t, std::string> texts{
        {228, "Dale"},     {229, "Nop"},     {1037, "Va"}, {1038, "Ni ahi"}, {1390, "%d:0%d"}, {1391, "%d:%d"},
        {1392, "Wait %d"}, {1393, "Result"}, {1394, "VS"}, {1395, "Tie"},    {1396, "Won"},    {1397, "Lost"}};
    auto table = Client::Tables::LoadTextTable(Client::Tests::TextTableBytes(texts));
    EXPECT_TRUE(table.HasValue());
    return std::move(table).Value();
}

class GuildDrawingTest : public ::testing::TestWithParam<std::int32_t>
{
protected:
    GuildDrawingTest() : m_text(m_sprites)
    {
        m_sprites.Begin(Screen640);
        Runtime().map = GetParam();
        Client::Guild::GuildState& guild = Runtime().guild;
        guild.tableCount = 6;
        guild.table[HeroGuild].key = 30;
        Fill(guild.table[HeroGuild].guildName, "Horneros");
        guild.table[HeroGuild].mark.fill(4);
        guild.table[RivalGuild].key = 50;
        Fill(guild.table[RivalGuild].guildName, "Teros");
        guild.table[RivalGuild].mark.fill(12);
        Fill(guild.rival, "Teros");
        guild.ourScore = 3;
        guild.theirScore = 12;
        Hero().word1f2 = 0xffff;
    }

    Client::Kernel::WorldRuntime& Runtime()
    {
        return m_world->Runtime();
    }

    Client::Kernel::ActorState& Hero()
    {
        return m_world->Hero();
    }

    Client::Core::Expected<void, std::string> Score(bool withMarks = true)
    {
        Client::Kernel::WorldRuntime& runtime = Runtime();
        Client::Kernel::ActorState& hero = Hero();
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
                               .guildMarks = withMarks ? &m_marks : nullptr};
        Client::Hud::GamePanels panels(*m_world, m_random);
        return Client::Guild::DrawTheWarScore(context, panels.Registry());
    }

    Client::Core::Expected<void, std::string> EventSummary()
    {
        auto& runtime = Runtime();
        auto& hero = Hero();
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
                               .texts = &m_texts};
        return Client::Guild::DrawTheEventTimeAndOutcome(context);
    }

    Client::Ui::PanelDialogState& PanelDialog()
    {
        return Runtime().panels.delegated.panelDialog;
    }

    void Open(std::vector<std::string> lines, std::uint32_t flags, std::int32_t placed)
    {
        m_lines = std::move(lines);
        const auto opened = Client::Ui::OpenThePanelDialog(
            PanelDialog(),
            Client::Ui::PanelDialogRequest{.lines = m_lines,
                                           .flags = flags,
                                           .owner = Client::Ui::PanelDialogOwner::Relationship,
                                           .placed = placed},
            m_text, Client::Render::Text::FontKind::Normal, 1000);
        ASSERT_EQ(opened, Client::Ui::PanelDialogOpening::Opened);
    }

    Client::Core::Expected<void, std::string> Dialog(bool withTexts = true, std::uint32_t tick = 0,
                                                     bool projecting = false)
    {
        Client::Kernel::WorldRuntime& runtime = Runtime();
        Client::Kernel::ActorState& hero = Hero();
        GameHudContext context{.world = *m_world,
                               .random = m_random,
                               .ownRecordDepth = runtime.ownRecordDepth,
                               .cellXDepth = hero.cellXAccesses,
                               .cellYDepth = hero.cellYAccesses,
                               .hud = runtime.hud,
                               .crywolfSandOverlay = runtime.crywolf.sandOverlay,
                               .sound = m_sound,
                               .projecting = projecting,
                               .sprites = m_sprites,
                               .text = m_text,
                               .texts = withTexts ? &m_texts : nullptr,
                               .tickCount = tick};
        return Client::Guild::DrawThePanelDialog(context, PanelDialog());
    }

    std::unique_ptr<WorldState> m_world = std::make_unique<WorldState>();
    CrtRandom m_random{Seed};
    Client::Tables::TextTable m_texts = ButtonTexts();
    std::vector<std::string> m_lines;
    std::optional<Client::Audio::SoundId> m_sound{};
    SpriteBatch m_sprites;
    Client::Tests::RecordingTextDrawer m_text;
    Client::Tests::FakeTextureStore m_store;
    GuildMarkTextures m_marks{m_store};
};

TEST_P(GuildDrawingTest, TheEventClockKeepsItsNativeWindowAndShadow)
{
    Runtime().events.timeValue = 61;
    Runtime().events.timeKind = 1;
    Runtime().panels.inventory = 1;
    ASSERT_TRUE(EventSummary().HasValue());
    ASSERT_EQ(m_text.texts.size(), 2u);
    EXPECT_EQ(m_text.texts[0].text, "1:01");
    EXPECT_EQ(m_text.texts[0].x, 213);
    EXPECT_EQ(m_text.texts[0].y, 350);
    EXPECT_EQ(m_text.texts[0].style.font, Client::Render::Text::FontKind::BigBold);
    EXPECT_EQ(m_text.texts[0].style.colors.text, 0xff0a0affu);
    EXPECT_EQ(m_text.texts[1].x, 214);
    EXPECT_EQ(m_text.texts[1].y, 351);
    EXPECT_EQ(m_text.texts[1].style.colors.text, 0xffffffffu);
    EXPECT_EQ(Runtime().hud.dcFont, Client::Render::Text::FontKind::Normal);
    EXPECT_EQ(Draws(m_random), 2);
    m_text.texts.clear();
    Runtime().events.timeValue = 660;
    ASSERT_TRUE(EventSummary().HasValue());
    EXPECT_TRUE(m_text.texts.empty());
    EXPECT_EQ(Draws(m_random), 2);
}

TEST_P(GuildDrawingTest, TheOutcomeReadsTenBytesAndTheAdjacentSecondScore)
{
    auto& outcome = Runtime().events.guildWarOutcome;
    outcome.present = 0;
    outcome.firstName = {'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J'};
    outcome.secondName = {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9'};
    outcome.firstScore = 0x1234;
    outcome.secondScore = 65;
    Runtime().hud.textColors.background = 0x80000000u;
    ASSERT_TRUE(EventSummary().HasValue());
    ASSERT_EQ(m_text.texts.size(), 8u);
    EXPECT_EQ(m_text.texts[0].text, "Result");
    EXPECT_EQ(m_text.texts[2].text, "ABCDEFGHIJ");
    EXPECT_EQ(m_text.texts[2].x, 180);
    EXPECT_EQ(m_text.texts[3].text, "0123456789A");
    EXPECT_EQ(m_text.texts[3].x, 345);
    EXPECT_EQ(m_text.texts[4].text, "(4660)");
    EXPECT_EQ(m_text.texts[5].text, "(65)");
    EXPECT_EQ(m_text.texts[6].text, "Won");
    EXPECT_EQ(m_text.texts[6].style.colors.text, 0x0a0affffu);
    EXPECT_EQ(m_text.texts[7].text, "Lost");
    EXPECT_EQ(m_text.texts[7].style.colors.text, 0xff0a0affu);
    EXPECT_EQ(Runtime().hud.textColors.background, 0x80000000u);
    EXPECT_EQ(Runtime().hud.dcFont, Client::Render::Text::FontKind::Normal);
    EXPECT_EQ(Draws(m_random), 0);
    const auto quads = QuadsOf(m_sprites);
    ASSERT_EQ(quads.size(), 16u);
    ExpectQuadAt(quads[0], 0xfc, 170, 120, 300, 5);
    ExpectQuadAt(quads[6], 0xfc, 170, 325, 300, 5);
    ExpectQuadAt(quads.back(), 0xf1, 285, 280, 70, 20);
}

// 0x5a7649..0x5a7672: without the war, or with the hero without a guild, only blending off and the text background.
TEST_P(GuildDrawingTest, WithoutAWarOnlyTheStateChanges)
{
    m_sprites.SetBlend(BlendMode::Additive);
    Hero().word1f2 = HeroGuild;
    ASSERT_TRUE(Score().HasValue());
    Runtime().guild.warOn = 1;
    Hero().word1f2 = 0xffff;
    ASSERT_TRUE(Score().HasValue());
    EXPECT_TRUE(QuadsOf(m_sprites).empty());
    EXPECT_TRUE(m_text.texts.empty());
    EXPECT_EQ(m_sprites.Blend(), BlendMode::Opaque);
    EXPECT_EQ(Runtime().hud.textColors.background, 0x80000000u);
    EXPECT_EQ(m_store.loadCalls, 0);
    EXPECT_EQ(Draws(m_random), 0);
}

// With the war: x = 640 · 0.5 − 20 = 300; the hero's mark at (300, 4) and the rival's at (300, 19), 8 × 8 and blended;
// the scores at 313 and the names at 330, in bold, the hero's red and the rival's orange without a team. Without the
// inventory, 0x5bb8d0 queries nothing.
TEST_P(GuildDrawingTest, TheScoreDrawsBothMarksScoresAndNames)
{
    Runtime().guild.warOn = 1;
    Hero().word1f2 = HeroGuild;
    ASSERT_TRUE(Score().HasValue());
    const std::vector<DrawnQuad> quads = QuadsOf(m_sprites);
    ASSERT_EQ(quads.size(), 6u);
    ExpectQuadAt(quads[0], MarkSlot(HeroGuild), 300.0f, 4.0f, 8.0f, 8.0f);
    EXPECT_EQ(quads[0].blend, BlendMode::Alpha);
    EXPECT_FLOAT_EQ(quads[0].first.u, 0.0f);
    EXPECT_FLOAT_EQ(quads[0].third.u, 1.0f);
    ExpectQuadAt(quads[3], MarkSlot(RivalGuild), 300.0f, 19.0f, 8.0f, 8.0f);
    const auto& texts = m_text.texts;
    ASSERT_EQ(texts.size(), 4u);
    const std::array<std::string_view, 4> expected{"3", "Horneros", "12", "Teros"};
    const std::array<int, 4> xs{313, 330, 313, 330};
    const std::array<int, 4> ys{4, 4, 19, 19};
    const std::array<std::uint32_t, 4> colors{0xff003cffu, 0xff003cffu, 0xffff9600u, 0xffff9600u};
    for (std::size_t at = 0; at < texts.size(); ++at)
    {
        SCOPED_TRACE(at);
        EXPECT_EQ(texts[at].text, expected[at]);
        EXPECT_EQ(texts[at].x, xs[at]);
        EXPECT_EQ(texts[at].y, ys[at]);
        EXPECT_EQ(texts[at].style.colors.text, colors[at]);
        EXPECT_EQ(texts[at].style.colors.background, 0x80000000u);
        EXPECT_EQ(texts[at].style.font, Client::Render::Text::FontKind::Bold);
    }
    // Both marks, uploaded with the clear background: the hero's red (4) and the rival's blue (12).
    const auto ours = m_store.Find(Client::Render::TextureSlot{static_cast<std::uint16_t>(MarkSlot(HeroGuild))});
    const auto theirs = m_store.Find(Client::Render::TextureSlot{static_cast<std::uint16_t>(MarkSlot(RivalGuild))});
    ASSERT_TRUE(ours.has_value());
    ASSERT_TRUE(theirs.has_value());
    EXPECT_EQ(ours->texture.pixels[0], 0xff);
    EXPECT_EQ(theirs->texture.pixels[2], 0xff);
    EXPECT_EQ(Runtime().hud.dcFont, Client::Render::Text::FontKind::Bold);
    EXPECT_EQ(Runtime().hud.textColors.text, 0xffff9600u);
    EXPECT_EQ(m_sprites.Blend(), BlendMode::Alpha);
    EXPECT_EQ(Draws(m_random), 0);
}

// With the team (0x576796e) the colors swap; a rival not in the table draws entry 0's mark (0x5a78ba); an empty name
// is not drawn. With the inventory open, the 450 world leaves x at 205 and the only call to 0x5bb8d0 queries the shop
// and the trade: 2 rand().
TEST_P(GuildDrawingTest, TheTeamSwapsTheColorsAndAnUnknownRivalShowsTheFirstMark)
{
    Client::Guild::GuildState& guild = Runtime().guild;
    guild.warOn = 1;
    guild.warTeam = 1;
    Fill(guild.rival, "Benteveo");
    Hero().word1f2 = HeroGuild;
    Runtime().panels.inventory = 1;
    ASSERT_TRUE(Score().HasValue());
    const std::vector<DrawnQuad> quads = QuadsOf(m_sprites);
    ASSERT_EQ(quads.size(), 6u);
    ExpectQuadAt(quads[0], MarkSlot(HeroGuild), 205.0f, 4.0f, 8.0f, 8.0f);
    ExpectQuadAt(quads[3], MarkSlot(0), 205.0f, 19.0f, 8.0f, 8.0f);
    ASSERT_EQ(m_text.texts.size(), 4u);
    EXPECT_EQ(m_text.texts[0].x, 218);
    EXPECT_EQ(m_text.texts[0].style.colors.text, 0xffff9600u);
    EXPECT_EQ(m_text.texts[3].text, "Benteveo");
    EXPECT_EQ(m_text.texts[3].style.colors.text, 0xff003cffu);
    EXPECT_EQ(Draws(m_random), 2);

    m_sprites.Begin(Screen640);
    m_text.texts.clear();
    guild.table[HeroGuild].guildName = {};
    ASSERT_TRUE(Score().HasValue());
    ASSERT_EQ(m_text.texts.size(), 3u);
    EXPECT_EQ(m_text.texts[1].text, "12");
}

// Without the marks table, the war stops; without the war, it is not needed.
TEST_P(GuildDrawingTest, WithoutTheMarksTheWarIsCut)
{
    Hero().word1f2 = HeroGuild;
    ASSERT_TRUE(Score(false).HasValue());
    Runtime().guild.warOn = 1;
    const auto cut = Score(false);
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x5a76d8"), std::string::npos);
}

// 0x5a7a01..0x5a7cbd: the F3 23 needs neither the own guild nor the war. The names look up the first 1999 entries,
// even if tableCount is lower; the second name that does not exist uses mark 0.
TEST_P(GuildDrawingTest, SoccerDrawsTheReceivedTeamsWithoutTheHeroGuild)
{
    auto& soccer = Runtime().events.soccer;
    soccer.active = true;
    Fill(soccer.firstName, "Teros");
    Fill(soccer.secondName, "Ausente");
    Runtime().guild.tableCount = 0;
    Runtime().guild.ourScore = 27;
    Runtime().guild.theirScore = 4;
    Runtime().panels.inventory = 1;
    ASSERT_TRUE(Score().HasValue());
    const auto quads = QuadsOf(m_sprites);
    ASSERT_EQ(quads.size(), 6u);
    ExpectQuadAt(quads[0], MarkSlot(RivalGuild), 205, 4, 8, 8);
    ExpectQuadAt(quads[3], MarkSlot(0), 205, 19, 8, 8);
    ASSERT_EQ(m_text.texts.size(), 4u);
    const std::array<std::string_view, 4> expected{"27", "Teros", "4", "Ausente"};
    for (std::size_t index = 0; index < expected.size(); ++index)
    {
        EXPECT_EQ(m_text.texts[index].text, expected[index]);
        EXPECT_EQ(m_text.texts[index].x, index % 2 == 0 ? 218 : 235);
        EXPECT_EQ(m_text.texts[index].y, index < 2 ? 4 : 19);
        EXPECT_EQ(m_text.texts[index].style.colors.text, index < 2 ? 0xff003cffu : 0xffff9600u);
        EXPECT_EQ(m_text.texts[index].style.font, Client::Render::Text::FontKind::Bold);
    }
    EXPECT_EQ(Draws(m_random), 2);
}

TEST_P(GuildDrawingTest, SoccerFollowsTheWarAndDoesNotSearchTheLastGuild)
{
    Runtime().guild.warOn = 1;
    Hero().word1f2 = HeroGuild;
    auto& soccer = Runtime().events.soccer;
    soccer.active = true;
    Fill(soccer.firstName, "Ultimo");
    Fill(soccer.secondName, "Horneros");
    Fill(Runtime().guild.table.back().guildName, "Ultimo");
    Runtime().panels.inventory = 1;
    ASSERT_TRUE(Score().HasValue());
    const auto quads = QuadsOf(m_sprites);
    ASSERT_EQ(quads.size(), 12u);
    ExpectQuadAt(quads[6], MarkSlot(0), 205, 4, 8, 8);
    ExpectQuadAt(quads[9], MarkSlot(HeroGuild), 205, 19, 8, 8);
    EXPECT_EQ(Draws(m_random), 4);
}

// 0x660830 with Confirm and Cancel and two lines: height 2 · (13 + 2) + 0x3f = 93 and y = 0x82 − 46 = 84. The box
// (0x6576a0) without alpha test and with alpha 0.8: the edge, two rows of 40, the rest of 83 − 80 = 3 (dv 3 · 0.025 ·
// 0.625) and the other edge. The lines centered on 0x140 from 104, 15 apart, in the normal font and in white; the
// buttons at y = 104 + 30 + 9 = 143, at 0x104 and 0x14a, with their texts. No rand().
TEST_P(GuildDrawingTest, ThePanelDialogDrawsItsBoxLinesAndButtons)
{
    Open({"Una alianza", "con Teros"}, Client::Ui::PanelDialogConfirm, 0);
    ASSERT_EQ(PanelDialog().height, 93);
    Runtime().hud.dcFont = Client::Render::Text::FontKind::Bold;
    ASSERT_TRUE(Dialog().HasValue());
    const std::vector<DrawnQuad> quads = QuadsOf(m_sprites);
    ASSERT_EQ(quads.size(), 11u);
    ExpectQuadAt(quads[0], 0xfc, 214.0f, 84.0f, 213.0f, 5.0f);
    ExpectQuadAt(quads[1], 0xfb, 214.0f, 89.0f, 213.0f, 40.0f);
    ExpectQuadAt(quads[2], 0xfb, 214.0f, 129.0f, 213.0f, 40.0f);
    ExpectQuadAt(quads[3], 0xfb, 214.0f, 169.0f, 213.0f, 3.0f);
    ExpectQuadAt(quads[4], 0xfc, 214.0f, 172.0f, 213.0f, 5.0f);
    for (std::size_t at = 0; at < 5; ++at)
    {
        SCOPED_TRACE(at);
        EXPECT_EQ(quads[at].blend, BlendMode::Alpha);
        EXPECT_FALSE(quads[at].alphaTest);
        EXPECT_FLOAT_EQ(quads[at].first.color.alpha, 0.8f);
        EXPECT_FLOAT_EQ(quads[at].third.u, 0.83203125f);
    }
    EXPECT_FLOAT_EQ(quads[3].first.v, 0.0f);
    EXPECT_NEAR(quads[3].third.v, 0.046875f, 1e-7f);
    EXPECT_NEAR(quads[2].third.v, 0.625f, 1e-7f);
    EXPECT_EQ(quads[5].slot, Marker);
    EXPECT_EQ(quads[6].slot, Marker);
    ExpectQuadAt(quads[7], Client::Ui::WindowButtonSlot, 260.0f, 143.0f, 50.0f, 18.0f);
    ExpectQuadAt(quads[9], Client::Ui::WindowButtonSlot, 330.0f, 143.0f, 50.0f, 18.0f);
    const auto& texts = m_text.texts;
    ASSERT_EQ(texts.size(), 4u);
    EXPECT_EQ(texts[0].text, "Una alianza");
    EXPECT_EQ(texts[0].x, 0x140 - 33);
    EXPECT_EQ(texts[0].y, 104);
    EXPECT_EQ(texts[1].text, "con Teros");
    EXPECT_EQ(texts[1].x, 0x140 - 27);
    EXPECT_EQ(texts[1].y, 119);
    for (std::size_t at = 0; at < 2; ++at)
    {
        EXPECT_EQ(texts[at].style.font, Client::Render::Text::FontKind::Normal);
        EXPECT_EQ(texts[at].style.colors.text, 0xffffffffu);
        EXPECT_EQ(texts[at].style.colors.background, 0u);
    }
    EXPECT_EQ(texts[2].text, "Va");
    EXPECT_EQ(texts[3].text, "Ni ahi");
    EXPECT_EQ(PanelDialog().buttons[Client::Ui::PanelOkButton].label, "Dale");
    EXPECT_EQ(PanelDialog().buttons[Client::Ui::PanelConfirmButton].base.y, 143);
    EXPECT_EQ(m_sprites.Blend(), BlendMode::Opaque);
    EXPECT_EQ(Draws(m_random), 0);
}

// With +0xa24 the box goes at y = 10; one line: height 15 + 0x3f = 78, one row and the rest of 28 (dv 0.4375). Accept
// alone, at 0x127 and y = 30 + 15 + 9 = 54.
TEST_P(GuildDrawingTest, AtTheTopWithOneButton)
{
    Open({"Listo"}, Client::Ui::PanelDialogOk, 1);
    ASSERT_TRUE(Dialog().HasValue());
    const std::vector<DrawnQuad> quads = QuadsOf(m_sprites);
    ASSERT_EQ(quads.size(), 7u);
    ExpectQuadAt(quads[0], 0xfc, 214.0f, 10.0f, 213.0f, 5.0f);
    ExpectQuadAt(quads[1], 0xfb, 214.0f, 15.0f, 213.0f, 40.0f);
    ExpectQuadAt(quads[2], 0xfb, 214.0f, 55.0f, 213.0f, 28.0f);
    EXPECT_NEAR(quads[2].third.v, 0.4375f, 1e-7f);
    ExpectQuadAt(quads[3], 0xfc, 214.0f, 83.0f, 213.0f, 5.0f);
    ExpectQuadAt(quads[5], Client::Ui::WindowButtonSlot, 295.0f, 54.0f, 50.0f, 18.0f);
    ASSERT_EQ(m_text.texts.size(), 2u);
    EXPECT_EQ(m_text.texts[0].y, 30);
    EXPECT_EQ(m_text.texts[1].text, "Dale");
}

// Closed, it does not draw; the buttons still need their texts when they are copied for the first time.
TEST_P(GuildDrawingTest, ThePanelDialogNeedsItsButtonTexts)
{
    ASSERT_TRUE(Dialog().HasValue());
    EXPECT_TRUE(QuadsOf(m_sprites).empty());
    Open({"Che"}, Client::Ui::PanelDialogConfirm, 0);
    auto cut = Dialog(false);
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x65ff12"), std::string::npos);
}

TEST_P(GuildDrawingTest, ThePanelTimerDrawsItsNativeProgressAndUpdatesElapsedSecondsWithoutClamping)
{
    Open({"Uno", "Dos"}, Client::Ui::PanelDialogConfirm | Client::Ui::PanelDialogTimed, 0);
    ASSERT_TRUE(Dialog(true, 3500).HasValue());
    const auto quads = QuadsOf(m_sprites);
    const auto back = std::find_if(quads.begin(), quads.end(), [](const DrawnQuad& q) { return q.slot == 0x667; });
    const auto progress = std::find_if(quads.begin(), quads.end(), [](const DrawnQuad& q) { return q.slot == 0x668; });
    ASSERT_NE(back, quads.end());
    ASSERT_NE(progress, quads.end());
    ExpectQuadAt(*back, 0x667, 245, 132, 150, 12);
    ExpectQuadAt(*progress, 0x668, 241, 132, 75, 12);
    EXPECT_EQ(progress->blend, BlendMode::Additive);
    EXPECT_FLOAT_EQ(progress->third.u, 75.0f / 256.0f);
    EXPECT_EQ(PanelDialog().elapsedSeconds, 2u);
    m_sprites.Begin(Screen640);
    ASSERT_TRUE(Dialog(true, 8500).HasValue());
    const auto over = QuadsOf(m_sprites);
    const auto full = std::find_if(over.begin(), over.end(), [](const DrawnQuad& q) { return q.slot == 0x668; });
    ASSERT_NE(full, over.end());
    ExpectQuadAt(*full, 0x668, 241, 132, 225, 12);
    EXPECT_EQ(PanelDialog().elapsedSeconds, 7u);
    EXPECT_EQ(m_random.State(), CrtRandom(Seed).State());
}

TEST_P(GuildDrawingTest, ProjectingThePanelTimerPreservesItsPublishedElapsedSeconds)
{
    Open({}, Client::Ui::PanelDialogTimed, 0);
    PanelDialog().elapsedSeconds = 99;
    ASSERT_TRUE(Dialog(true, 3500, true).HasValue());
    EXPECT_EQ(PanelDialog().elapsedSeconds, 99u);
    EXPECT_EQ(m_random.State(), CrtRandom(Seed).State());
}

INSTANTIATE_TEST_SUITE_P(Maps, GuildDrawingTest, ::testing::Values(0, 2, 6));
} // namespace
