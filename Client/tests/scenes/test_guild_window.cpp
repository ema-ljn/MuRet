#include "Network/Xor32Packet.h"
#include "Guild/GuildWindow.h"

#include "Core/Expected.h"
#include "Core/PixelSize.h"
#include "Crt/CrtRandom.h"
#include "Tables/TextTable.h"
#include "Protocol/Outgoing.h"
#include "Render/SpriteBatch.h"
#include "Models/LogicalAccess.h"
#include "Render/SpriteVertex.h"
#include "Render/TextureSlot.h"
#include "Render/Text/FontSet.h"
#include "Kernel/MouseState.h"
#include "Kernel/ActorState.h"
#include "Ui/GameHudContext.h"
#include "Guild/GuildDrawing.h"
#include "Guild/GuildMarks.h"
#include "Guild/GuildPanels.h"
#include "Guild/GuildState.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Chat/ChatLog.h"
#include "Ui/Dialogs.h"
#include "Ui/MessageArea.h"
#include "Ui/OldControls.h"
#include "Ui/OldWindowBase.h"
#include "Ui/PanelDialog.h"
#include "Ui/PanelRegistry.h"
#include "Audio/SoundId.h"

#include "support/FakeTextureStore.h"
#include "support/RecordingTextDrawer.h"
#include "support/SceneDataFiles.h"
#include "Chat/ChatRuntime.h"
#include "Ui/GameUiState.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <initializer_list>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// The guild window (CL9 milestone 8, phases 4 and 5): 0x64f3e0 with its step (0x45aa80(0) and 0x651840) and its draw
// (0x651c20): the frame, the title, the guild with the score or the texts without a guild, the close button, the tabs
// and the union request (E9); and the three tabs with their steps (0x64fc00, 0x650350 and 0x650c10), their draws
// (0x64fdf0, 0x6507d0 and 0x651400) and their lists. In Lorencia (0) and Devias (2). The guilds, the names and the
// texts are invented.
namespace
{
using Client::Crt::CrtRandom;
using Client::Guild::GuildMarkTextures;
using Client::Guild::GuildState;
using Client::Kernel::WorldState;
using Client::Render::SpriteBatch;
using Client::Render::SpriteVertex;
using Client::Ui::GameHudContext;

constexpr std::uint32_t Seed = 23;
constexpr Client::Core::PixelSize Screen640{640, 480};
constexpr int NoTexture = -1;
constexpr int Marker = -2;
constexpr float Left = 450.0f;
constexpr std::int32_t WindowX = 0x1c2;
constexpr std::uint16_t InTheGuild = 0x3;
constexpr std::uint8_t MemberStatus = 0x00;
constexpr std::uint8_t MasterStatus = 0x80;
constexpr std::uint8_t BattleMasterStatus = 0x20;
constexpr std::uint32_t Now = 5000;
constexpr std::uint8_t PageUp = 0x21;
constexpr std::uint8_t PageDown = 0x22;
constexpr std::uint32_t ChosenText = 0xff000000u;
constexpr std::uint32_t RowText = 0xffc8dce6u;

std::vector<std::byte> BytesOf(std::initializer_list<int> values)
{
    std::vector<std::byte> bytes;
    for (const int value : values)
        bytes.push_back(static_cast<std::byte>(value));
    return bytes;
}

std::vector<std::byte> PlainOf(const Client::Protocol::Outgoing& sent)
{
    const auto plain = Client::Network::Xor32Decode(sent.packet);
    return {plain.Bytes().begin(), plain.Bytes().end()};
}

struct DrawnQuad
{
    int slot = NoTexture;
    SpriteVertex first{};
    SpriteVertex third{};
};

// The batch's quads, without the text markers.
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
        if (slot == Marker)
            continue;
        for (std::size_t quad = 0; quad < command.vertexCount / 6; ++quad)
        {
            const std::size_t first = command.firstVertex + quad * 6;
            quads.push_back(DrawnQuad{.slot = slot, .first = vertices[first], .third = vertices[first + 2]});
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

// How many quads of the batch go with `slot` at (x, y), w × h.
std::size_t CountQuadsAt(const std::vector<DrawnQuad>& quads, int slot, float x, float y, float width, float height)
{
    constexpr float Tolerance = 1e-3f;
    return static_cast<std::size_t>(std::count_if(quads.begin(), quads.end(),
                                                  [&](const DrawnQuad& quad)
                                                  {
                                                      return quad.slot == slot &&
                                                             std::abs(quad.first.x - x) < Tolerance &&
                                                             std::abs(quad.first.y - (480.0f - y)) < Tolerance &&
                                                             std::abs(quad.third.x - (x + width)) < Tolerance &&
                                                             std::abs(quad.third.y - (480.0f - y - height)) < Tolerance;
                                                  }));
}

// The position of the first text `text` from `from`, or the end.
std::size_t FindText(const std::vector<Client::Tests::RecordedText>& texts, std::string_view text, std::size_t from = 0)
{
    for (std::size_t at = from; at < texts.size(); ++at)
    {
        if (texts[at].text == text)
            return at;
    }
    return texts.size();
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
    for (int draws = 0; draws <= 10000; ++draws)
    {
        if (reference.State() == random.State())
            return draws;
        static_cast<void>(reference.Next());
    }
    return -1;
}

Client::Tables::TextTable WindowTexts(const std::map<std::size_t, std::string>& replaced = {})
{
    std::map<std::size_t, std::string> texts{{180, "Gremio"},
                                             {228, "Aceptar"},
                                             {229, "Cancelar"},
                                             {182, "Gremios"},
                                             {185, "Sin gremio"},
                                             {186, "busca uno"},
                                             {187, "o arma otro"},
                                             {188, "Disolver"},
                                             {189, "Dejar"},
                                             {220, "Salir"},
                                             {946, "Aviso"},
                                             {1022, "Server"},
                                             {1256, "Puntos: %d"},
                                             {1270, "No deja el gremio"},
                                             {1271, "No deja la alianza"},
                                             {1300, "Jefe"},
                                             {1301, "Ayudante"},
                                             {1302, "Guerrero"},
                                             {1307, "Cargo"},
                                             {1308, "Quitar"},
                                             {1309, "Echar"},
                                             {1310, "Socios: %d"},
                                             {1311, "Hacerlo ayudante"},
                                             {1312, "Hacerlo guerrero"},
                                             {1314, "%s sera %s"},
                                             {1315, "Elegi el cargo"},
                                             {1321, "Aviso de"},
                                             {1323, "El aviso"},
                                             {1324, "Romperla"},
                                             {1325, "Dejarla"},
                                             {1330, "Miembros"},
                                             {1332, "Clan"},
                                             {1352, "Alianza"},
                                             {1361, "Nadie"},
                                             {1362, "Socios: %d/%d"},
                                             {1363, "Disolver 1"},
                                             {1364, "Disolver 2"},
                                             {1365, "Disolver 3"},
                                             {1366, "Disolver 4"},
                                             {1367, "Miembro: %s"},
                                             {1368, "pierde el cargo"},
                                             {1369, "se va del gremio"},
                                             {1387, "Dejaste la alianza"},
                                             {1389, "Nombre"},
                                             {1422, "Echarlo"},
                                             {1423, "Gremio: %s"}};
    for (std::size_t line = 0; line < 11; ++line)
        texts.emplace(1257 + line, std::format("Sin alianza {}", line));
    for (const auto& [index, text] : replaced)
        texts[index] = text;
    auto table = Client::Tables::LoadTextTable(Client::Tests::TextTableBytes(texts));
    EXPECT_TRUE(table.HasValue());
    return std::move(table).Value();
}

class GuildWindowTest : public ::testing::TestWithParam<std::int32_t>
{
protected:
    GuildWindowTest() : m_text(m_sprites)
    {
        m_sprites.Begin(Screen640);
        Runtime().map = GetParam();
        Hero().guildStatus = 0xff;
        Hero().word1f2 = 0xffff;
        Runtime().panels.delegated.guild = 1;
    }

    Client::Kernel::WorldRuntime& Runtime()
    {
        return m_world->Runtime();
    }

    Client::Kernel::ActorState& Hero()
    {
        return m_world->Hero();
    }

    GuildState& Guild()
    {
        return Runtime().guild;
    }

    // The hero in guild 3 ("Talas", 17 points), with or without a union.
    void JoinTheGuild(std::string_view alliance)
    {
        Hero().guildStatus = MemberStatus;
        Hero().word1f2 = InTheGuild;
        Fill(Guild().table[InTheGuild].guildName, "Talas");
        Fill(Guild().table[InTheGuild].unionName, alliance);
        Guild().score = 17;
    }

    Client::Guild::GuildSender Sender()
    {
        return [this](Client::Protocol::Outgoing packet) { m_sent.push_back(std::move(packet)); };
    }

    Client::Core::Expected<void, std::string> Step()
    {
        return Client::Guild::StepTheGuildWindow(
            Client::Guild::GuildWindowStepContext{.world = *m_world,
                                                  .mouseX = m_mouseX,
                                                  .mouseY = m_mouseY,
                                                  .screen = Screen640,
                                                  .send = Sender(),
                                                  .keyEdge =
                                                      [this](std::uint8_t key)
                                                  {
                                                      const auto found = std::find(m_edges.begin(), m_edges.end(), key);
                                                      if (found == m_edges.end())
                                                          return false;
                                                      m_edges.erase(found);
                                                      return true;
                                                  },
                                                  .keyDown = [](std::uint8_t) { return false; },
                                                  .now = Now,
                                                  .measure = &m_text,
                                                  .texts = &m_texts});
    }

    // A member of the 52: in 0x787a5e8's table (in the packet's order) and in the list (0x6517a0).
    void AddMember(std::string_view name, std::uint8_t status, std::uint8_t server = 0)
    {
        Client::Guild::GuildMemberSlot& slot = Guild().members[static_cast<std::size_t>(Guild().memberCount)];
        Fill(slot.name, name);
        slot.status = status;
        slot.server = server;
        ++Guild().memberCount;
        Client::Guild::AddTheGuildMember(*m_world, slot);
    }

    // A guild of the E9's union (0x6517e0), with a single-color mark.
    void AddUnion(std::string_view name, std::uint8_t number, std::uint8_t color)
    {
        std::array<std::uint8_t, Client::Guild::GuildMarkNibbles> mark{};
        mark.fill(color);
        Client::Guild::AddTheUnion(Guild().window, mark, name, number);
    }

    // The notice of the 0D type 2 (0x651680).
    void AddNotice(std::string_view text)
    {
        ASSERT_TRUE(
            Client::Guild::AddTheGuildNotice(Guild().window, m_text, Runtime().hud.dcFont, Screen640, text).HasValue());
    }

    std::string MemberAt(std::size_t at)
    {
        return std::string(Client::Guild::GuildTextOf(Guild().window.members.at(at).slot.name));
    }

    // 0x45b200: the edge over the button and, the next frame, the release over it.
    Client::Core::Expected<void, std::string> Click(std::size_t button)
    {
        return ClickOn(Guild().window.memberButtons.at(button));
    }

    // The same on +0x42c, +0x64c or +0x6d4.
    Client::Core::Expected<void, std::string> ClickTheTabButton(std::size_t button)
    {
        return ClickOn(Guild().window.tabButtons.at(button));
    }

    Client::Core::Expected<void, std::string> ClickOn(const Client::Ui::OldButton& target)
    {
        m_mouseX = target.base.x + target.base.width / 2;
        m_mouseY = target.base.y + target.base.height / 2;
        Client::Kernel::MouseState& mouse = Runtime().mouse;
        mouse.pendingClick = true;
        mouse.leftHeld = true;
        if (auto pressed = Step(); !pressed.HasValue())
            return pressed;
        mouse.pendingClick = false;
        mouse.leftHeld = false;
        mouse.releasedClick = true;
        auto released = Step();
        mouse.releasedClick = false;
        return released;
    }

    Client::Core::Expected<void, std::string> Draw()
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
                               .texts = &m_texts,
                               .mouse = {m_mouseX, m_mouseY},
                               .guildMarks = &m_marks};
        return Client::Guild::DrawTheGuildWindow(context);
    }

    // 0x6605b0 through the registry (LendThePanelDialogStepTo), with LendTheGuildTo's answer.
    Client::Core::Expected<void, std::string> StepTheDialog()
    {
        Client::Ui::PanelContext panels{.random = m_random, .dialogs = Runtime().dialogs};
        Client::Guild::LendTheGuildTo(panels, *m_world, Sender());
        Client::Guild::LendThePanelDialogStepTo(panels, *m_world, Sender(), m_mouseX, m_mouseY, Now);
        return panels.panelDialogStep();
    }

    // A click at (x, y) of the dialog's step: the edge and the release in two frames.
    Client::Core::Expected<void, std::string> ClickTheDialogAt(std::int32_t x, std::int32_t y)
    {
        m_mouseX = x;
        m_mouseY = y;
        Client::Kernel::MouseState& mouse = Runtime().mouse;
        mouse.pendingClick = true;
        mouse.leftHeld = true;
        if (auto pressed = StepTheDialog(); !pressed.HasValue())
            return pressed;
        mouse.pendingClick = false;
        mouse.leftHeld = false;
        mouse.releasedClick = true;
        auto released = StepTheDialog();
        mouse.releasedClick = false;
        return released;
    }

    Client::Core::Expected<void, std::string> ClickThePositionsButton(std::size_t button)
    {
        const Client::Ui::OldButton& target = Guild().positions.buttons.at(button);
        return ClickTheDialogAt(target.base.x + target.base.width / 2, target.base.y + target.base.height / 2);
    }

    // 0x660830 (the last of 0x6579e0).
    Client::Core::Expected<void, std::string> DrawTheDialog()
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
                               .texts = &m_texts,
                               .mouse = {m_mouseX, m_mouseY},
                               .guildMarks = &m_marks};
        return Client::Guild::DrawThePanelDialog(context, runtime.panels.delegated.panelDialog);
    }

    // The master chooses `name` (with its position) and presses the position button.
    void OpenThePositionsFor(std::string_view name, std::uint8_t status)
    {
        JoinTheGuild("");
        Hero().guildStatus = MasterStatus;
        AddMember(name, status);
        const auto clicked = Click(Client::Guild::GuildAssignButton);
        ASSERT_TRUE(clicked.HasValue()) << clicked.Error();
    }

    // The mouse over the step's tab `tab` (0x34 × 0x10 at x + 0xf, 0x43 and 0x77, y + 0x4b).
    void PointAtTheTab(std::int32_t tab)
    {
        constexpr std::array<std::int32_t, 3> Lefts{0xf, 0x43, 0x77};
        m_mouseX = WindowX + Lefts[static_cast<std::size_t>(tab)] + 1;
        m_mouseY = 0x4b + 1;
    }

    std::unique_ptr<WorldState> m_world = std::make_unique<WorldState>();
    CrtRandom m_random{Seed};
    Client::Tables::TextTable m_texts = WindowTexts();
    std::optional<Client::Audio::SoundId> m_sound{};
    SpriteBatch m_sprites;
    Client::Tests::RecordingTextDrawer m_text;
    Client::Tests::FakeTextureStore m_store;
    GuildMarkTextures m_marks{m_store};
    std::vector<Client::Protocol::Outgoing> m_sent;
    std::vector<std::uint8_t> m_edges;
    std::int32_t m_mouseX = 0;
    std::int32_t m_mouseY = 300;
};

// 0x651c20 with the hero without a guild (+0x368 at 0xff): the background, the title in bold and texts 185 to 187 in
// the normal font, every 0xf, and the close button; neither the score nor the tabs. The step does not reach the tabs
// either. No rand().
TEST_P(GuildWindowTest, WithoutAGuildItShowsTheThreeTextsAndNoTabs)
{
    ASSERT_TRUE(Step().HasValue());
    ASSERT_TRUE(Draw().HasValue());
    const std::vector<DrawnQuad> quads = QuadsOf(m_sprites);
    ASSERT_EQ(quads.size(), 3U);
    ExpectQuadAt(quads[0], 0x104, Left, 0.0f, 190.0f, 256.0f);
    ExpectQuadAt(quads[1], 0x105, Left, 256.0f, 190.0f, 177.0f);
    ExpectQuadAt(quads[2], 0x118, Left + 25.0f, 395.0f, 24.0f, 24.0f);
    ASSERT_EQ(m_text.texts.size(), 4U);
    EXPECT_EQ(m_text.texts[0].text, "Gremio");
    EXPECT_EQ(m_text.texts[0].x, WindowX + 0x23);
    EXPECT_EQ(m_text.texts[0].y, 0xc);
    EXPECT_EQ(m_text.texts[0].style.colors.text, 0xffdcdcdcu);
    EXPECT_EQ(m_text.texts[0].style.colors.background, 0U);
    EXPECT_EQ(m_text.texts[0].style.font, Client::Render::Text::FontKind::Bold);
    const std::array<std::string_view, 3> lines{"Sin gremio", "busca uno", "o arma otro"};
    for (std::size_t line = 0; line < lines.size(); ++line)
    {
        SCOPED_TRACE(line);
        const auto& text = m_text.texts[line + 1];
        EXPECT_EQ(text.text, lines[line]);
        EXPECT_EQ(text.x, WindowX + 0x14);
        EXPECT_EQ(text.y, 0x32 + 0xf * static_cast<int>(line));
        EXPECT_EQ(text.style.colors.text, 0xffe6e6e6u);
        EXPECT_EQ(text.style.font, Client::Render::Text::FontKind::Normal);
    }
    EXPECT_TRUE(m_sent.empty());
    EXPECT_EQ(Draws(m_random), 0);
}

// With a guild: "%s ( Score:%d )" in 0xff64ffc8 under the title, the lines under tabs 0 and 2 (without a texture), the
// three tabs (1, chosen since 0x64f3e0, white and 2 taller and higher; the others at 0.6) with their texts at
// y + 0x4b + 4 (the chosen one, one higher). The members one (0x6507d0), without members and without being master,
// draws nothing.
TEST_P(GuildWindowTest, WithAGuildItDrawsTheScoreAndTheTabs)
{
    JoinTheGuild("");
    const auto drawn = Draw();
    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    const std::vector<DrawnQuad> quads = QuadsOf(m_sprites);
    ASSERT_EQ(quads.size(), 8U);
    ExpectQuadAt(quads[3], NoTexture, Left + 0xf, 0x5b, 54.0f, 1.0f);
    ExpectQuadAt(quads[4], NoTexture, Left + 0xf + 0x6c, 0x5b, 54.0f, 1.0f);
    EXPECT_FLOAT_EQ(quads[3].first.color.red, 146.0f / 255.0f);
    ExpectQuadAt(quads[5], 0x646, Left + 0xf, 0x4b, 52.0f, 16.0f);
    ExpectQuadAt(quads[6], 0x646, Left + 0xf + 0x36, 0x4b - 2, 52.0f, 18.0f);
    ExpectQuadAt(quads[7], 0x646, Left + 0xf + 0x6c, 0x4b, 52.0f, 16.0f);
    EXPECT_FLOAT_EQ(quads[5].first.color.red, 0.6f);
    EXPECT_FLOAT_EQ(quads[6].first.color.red, 1.0f);
    ASSERT_EQ(m_text.texts.size(), 5U);
    EXPECT_EQ(m_text.texts[1].text, "Talas ( Score:17 )");
    EXPECT_EQ(m_text.texts[1].x, WindowX + 0x23);
    EXPECT_EQ(m_text.texts[1].y, 0x35);
    EXPECT_EQ(m_text.texts[1].style.colors.text, 0xff64ffc8u);
    EXPECT_EQ(m_text.texts[2].text, "Aviso");
    EXPECT_EQ(m_text.texts[2].y, 0x4b + 4);
    EXPECT_EQ(m_text.texts[3].text, "Miembros");
    EXPECT_EQ(m_text.texts[3].y, 0x4b + 3);
    EXPECT_EQ(m_text.texts[4].text, "Alianza");
    EXPECT_EQ(m_text.texts[4].style.font, Client::Render::Text::FontKind::Bold);
    EXPECT_EQ(Draws(m_random), 0);
}

// With +0x368 set and +0x1f2 at −1, the name comes from byte 9 of member 74 (Q-C1281); outside the table, it stops.
TEST_P(GuildWindowTest, TheHerosGuildNameReadsBeforeTheTable)
{
    JoinTheGuild("");
    Hero().word1f2 = 0xffff;
    Guild().members[74].name[9] = 'x';
    static_cast<void>(Draw());
    ASSERT_GE(m_text.texts.size(), 2U);
    EXPECT_EQ(m_text.texts[1].text, "x ( Score:17 )");
    Hero().word1f2 = 2000;
    const auto cut = Draw();
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x651da0"), std::string::npos) << cut.Error();
}

// 0x651895..0x6518e3: the click on the close button consumes it, leaves 0x6c8dcc at 6 and closes (0x6522f0: tab 1
// and sound 0x1c, after the click's 0x19).
TEST_P(GuildWindowTest, TheCloseButtonClosesTheWindow)
{
    Guild().window.tab = 2;
    m_mouseX = WindowX + 0x19 + 3;
    m_mouseY = 0x18b + 3;
    Runtime().mouse.pendingClick = true;
    ASSERT_TRUE(Step().HasValue());
    EXPECT_FALSE(Runtime().mouse.pendingClick);
    EXPECT_EQ(Runtime().mouse.updateTimeMax, 6);
    EXPECT_EQ(Runtime().panels.delegated.guild, 0);
    EXPECT_EQ(Guild().window.tab, 1);
    EXPECT_EQ(Runtime().lastSoundId, Client::Audio::SoundId{0x1c});
    EXPECT_EQ(Runtime().ui.mouseOnUi, 1);
}

// 0x651907..0x651bb5: with the left button down, the tab under the mouse. The union one asks for the list (C1 03 E9 in
// the clear) only the first time and only with a union; then, the tab's step (0x650c10 and 0x64fc00, which without
// being master and without the button do nothing but the list).
TEST_P(GuildWindowTest, TheTabsSwitchAndTheUnionTabAsksForTheListOnce)
{
    JoinTheGuild("Ceibo");
    Runtime().mouse.leftHeld = true;
    PointAtTheTab(2);
    auto stepped = Step();
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(Guild().window.tab, 2);
    EXPECT_EQ(Guild().window.unionRequested, 1);
    ASSERT_EQ(m_sent.size(), 1U);
    const auto bytes = m_sent[0].packet.Bytes();
    ASSERT_EQ(bytes.size(), 3U);
    EXPECT_EQ(bytes[0], std::byte{0xc1});
    EXPECT_EQ(bytes[1], std::byte{0x03});
    EXPECT_EQ(bytes[2], std::byte{0xe9});

    PointAtTheTab(0);
    stepped = Step();
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(Guild().window.tab, 0);

    PointAtTheTab(2);
    static_cast<void>(Step());
    EXPECT_EQ(Guild().window.tab, 2);
    EXPECT_EQ(m_sent.size(), 1U);

    Runtime().mouse.leftHeld = false;
    PointAtTheTab(1);
    static_cast<void>(Step());
    EXPECT_EQ(Guild().window.tab, 2);
    EXPECT_EQ(Draws(m_random), 0);
}

// Without a union, the tab changes and no E9 goes out.
TEST_P(GuildWindowTest, WithoutAnAllianceTheUnionTabSendsNothing)
{
    JoinTheGuild("");
    Runtime().mouse.leftHeld = true;
    PointAtTheTab(2);
    static_cast<void>(Step());
    EXPECT_EQ(Guild().window.tab, 2);
    EXPECT_EQ(Guild().window.unionRequested, 0);
    EXPECT_TRUE(m_sent.empty());
}

// 0x6517a0 → 0x46c930: each member enters at the front; with the list empty, +0x130 checks whether it is the hero. At
// the end, the last of the deque (the first to enter) chosen and the scroll to the end. 0x6517d0 empties it, chooses 0
// and scrolls to 0.
TEST_P(GuildWindowTest, TheMemberListPutsEachNewMemberFirst)
{
    Hero().name = {std::byte{'S'}, std::byte{'a'}, std::byte{'u'}, std::byte{'c'}, std::byte{'e'}};
    AddMember("Sauce", BattleMasterStatus);
    EXPECT_EQ(Guild().window.heroFirst, 1);
    AddMember("Ombu", MemberStatus);
    AddMember("Tala", MemberStatus);
    ASSERT_EQ(Guild().window.members.size(), 3U);
    EXPECT_EQ(MemberAt(0), "Tala");
    EXPECT_EQ(MemberAt(1), "Ombu");
    EXPECT_EQ(MemberAt(2), "Sauce");
    EXPECT_EQ(Guild().window.memberList.selection, 3);
    EXPECT_EQ(Guild().window.memberList.scroll, 0);
    EXPECT_EQ(Guild().window.heroFirst, 1);

    Client::Guild::ClearTheGuildMembers(Guild().window);
    EXPECT_TRUE(Guild().window.members.empty());
    EXPECT_EQ(Guild().window.memberList.selection, 0);
    AddMember("Ombu", MemberStatus);
    EXPECT_EQ(Guild().window.heroFirst, 0);
    EXPECT_EQ(Draws(m_random), 0);
}

// 0x650350 with 20 members (18 rows): they entered with the scroll at the end (2). PgDn scrolls 18 toward the start
// and PgUp goes back, clamped to [0, 2]. Each edge, once.
TEST_P(GuildWindowTest, PageUpAndPageDownScrollTheMemberList)
{
    JoinTheGuild("");
    for (int member = 0; member < 20; ++member)
        AddMember(std::format("Socio{}", member), MemberStatus);
    EXPECT_EQ(Guild().window.memberList.scroll, 2);
    EXPECT_EQ(Guild().window.memberList.selection, 20);
    m_edges = {PageDown};
    ASSERT_TRUE(Step().HasValue());
    EXPECT_EQ(Guild().window.memberList.scroll, 0);
    ASSERT_TRUE(Step().HasValue());
    EXPECT_EQ(Guild().window.memberList.scroll, 0);
    m_edges = {PageUp};
    ASSERT_TRUE(Step().HasValue());
    EXPECT_EQ(Guild().window.memberList.scroll, 2);
    EXPECT_EQ(Runtime().ui.capture, 0);
    EXPECT_EQ(Draws(m_random), 0);
}

// 0x46d3a0: the click on a row (0xd high from its y − 3) chooses it (scroll + row + 1), flips +0 of its element and
// spends the click. Row 0 is the bottom one: with three, at y (3 − 0) · 13 − 0xeb − 10 + 0x159 = 139.
TEST_P(GuildWindowTest, ClickingAMemberRowChoosesAndTogglesIt)
{
    JoinTheGuild("");
    AddMember("Sauce", BattleMasterStatus);
    AddMember("Ombu", MemberStatus);
    AddMember("Tala", MemberStatus);
    m_mouseX = 0x1d2 + 4;
    m_mouseY = 139 - 3 + 2;
    Runtime().mouse.pendingClick = true;
    ASSERT_TRUE(Step().HasValue());
    EXPECT_FALSE(Runtime().mouse.pendingClick);
    EXPECT_EQ(Guild().window.memberList.selection, 1);
    EXPECT_EQ(Guild().window.members[0].toggled, 1);
    EXPECT_EQ(Guild().window.members[1].toggled, 0);

    // Row 1, 13 higher.
    m_mouseY -= 13;
    Runtime().mouse.pendingClick = true;
    ASSERT_TRUE(Step().HasValue());
    EXPECT_EQ(Guild().window.memberList.selection, 2);
    EXPECT_EQ(Guild().window.members[1].toggled, 1);
    EXPECT_EQ(Draws(m_random), 0);
}

// 0x6504dc..0x650652 and 0x64def0: the master removes the chosen one's position (0x20): the dialog with Confirm and
// Cancel and the name in 0x7d535a8; Confirm sends C1 0F E1 03 00 and the name with the XOR32, and C1 03 52 in the
// clear.
TEST_P(GuildWindowTest, TheMasterDismissesTheChosenMember)
{
    JoinTheGuild("");
    Hero().guildStatus = MasterStatus;
    AddMember("Sauce", BattleMasterStatus);
    AddMember("Ombu", MemberStatus);
    ASSERT_EQ(Guild().window.memberList.selection, 2);
    const auto clicked = Click(Client::Guild::GuildDismissButton);
    ASSERT_TRUE(clicked.HasValue()) << clicked.Error();
    const Client::Ui::PanelDialogState& dialog = Runtime().panels.delegated.panelDialog;
    EXPECT_EQ(dialog.owner, Client::Ui::PanelDialogOwner::GuildDismiss);
    EXPECT_EQ(dialog.flags, Client::Ui::PanelDialogConfirm);
    ASSERT_EQ(dialog.lineCount, 2);
    EXPECT_EQ(dialog.lines[0], "Miembro: Sauce");
    EXPECT_EQ(dialog.lines[1], "pierde el cargo");
    EXPECT_EQ(Guild().window.dialog, Now);
    EXPECT_EQ(Client::Guild::GuildTextOf(Guild().chosenName), "Sauce");
    EXPECT_EQ(Runtime().lastSoundId, Client::Audio::SoundId{0x19});
    EXPECT_TRUE(m_sent.empty());

    auto answered =
        Client::Guild::AnswerThePanelDialog(*m_world, Sender(), dialog.owner, Client::Ui::PanelAnswerConfirm);
    ASSERT_TRUE(answered.HasValue());
    EXPECT_TRUE(answered.Value());
    ASSERT_EQ(m_sent.size(), 2U);
    EXPECT_EQ(PlainOf(m_sent[0]), BytesOf({0xc1, 0x0f, 0xe1, 0x03, 0x00, 'S', 'a', 'u', 'c', 'e', 0, 0, 0, 0, 0}));
    const auto list = m_sent[1].packet.Bytes();
    EXPECT_EQ(std::vector<std::byte>(list.begin(), list.end()), BytesOf({0xc1, 0x03, 0x52}));

    // Cancelar no manda nada.
    answered = Client::Guild::AnswerThePanelDialog(*m_world, Sender(), Client::Ui::PanelDialogOwner::GuildDismiss,
                                                   Client::Ui::PanelAnswerCancel);
    ASSERT_TRUE(answered.HasValue());
    EXPECT_EQ(m_sent.size(), 2U);
    EXPECT_EQ(Draws(m_random), 0);
}

// 0x650658..0x6507b5 and 0x64e550: kicking the chosen one looks up its slot in 0x787a5e8 (0x64faa0) and opens the
// dialog; Confirm opens the game's dialog 0x7d with that slot in 0x6d5e2c. A 0x00 cannot have its position removed.
TEST_P(GuildWindowTest, TheMasterKicksTheChosenMember)
{
    JoinTheGuild("");
    Hero().guildStatus = MasterStatus;
    AddMember("Sauce", BattleMasterStatus);
    AddMember("Ombu", MemberStatus);
    Client::Ui::SelectInTheList(Guild().window.memberList, Guild().window.members.size(), 1);
    ASSERT_TRUE(Click(Client::Guild::GuildDismissButton).HasValue());
    EXPECT_EQ(Runtime().panels.delegated.panelDialog.opened, 0U);

    const auto clicked = Click(Client::Guild::GuildKickButton);
    ASSERT_TRUE(clicked.HasValue()) << clicked.Error();
    const Client::Ui::PanelDialogState& dialog = Runtime().panels.delegated.panelDialog;
    EXPECT_EQ(dialog.owner, Client::Ui::PanelDialogOwner::GuildKick);
    EXPECT_EQ(dialog.lines[0], "Miembro: Ombu");
    EXPECT_EQ(dialog.lines[1], "se va del gremio");
    EXPECT_EQ(Guild().kickIndex, 1);
    EXPECT_EQ(Guild().kickMember, -1);

    ASSERT_TRUE(Client::Guild::AnswerThePanelDialog(*m_world, Sender(), dialog.owner, Client::Ui::PanelAnswerConfirm)
                    .HasValue());
    EXPECT_EQ(Runtime().dialogs.current, 0x7d);
    EXPECT_EQ(Guild().kickMember, 1);
    EXPECT_TRUE(m_sent.empty());
    EXPECT_EQ(Draws(m_random), 0);
}

// 0x6503cf..0x6504d4: the position button on the chosen one stores its rank and its name and opens the empty panels'
// dialog (0xd5 × 0x2b, without buttons or owner) with the positions window's hooks (0x6602b0), with what it gives in
// +0x428. Without being master, the buttons do nothing. The one opened after comes back without hooks (0x660090).
TEST_P(GuildWindowTest, TheAssignButtonOpensThePositionsWindow)
{
    JoinTheGuild("");
    AddMember("Sauce", BattleMasterStatus);
    ASSERT_TRUE(Click(Client::Guild::GuildAssignButton).HasValue());
    ASSERT_TRUE(Click(Client::Guild::GuildKickButton).HasValue());
    Client::Ui::PanelDialogState& dialog = Runtime().panels.delegated.panelDialog;
    EXPECT_EQ(dialog.opened, 0U);

    Hero().guildStatus = MasterStatus;
    const auto clicked = Click(Client::Guild::GuildAssignButton);
    ASSERT_TRUE(clicked.HasValue()) << clicked.Error();
    EXPECT_EQ(Guild().chosenStatus, BattleMasterStatus);
    EXPECT_EQ(Client::Guild::GuildTextOf(Guild().chosenName), "Sauce");
    EXPECT_EQ(dialog.opened, Now);
    EXPECT_EQ(Guild().window.dialog, Now);
    EXPECT_EQ(dialog.owner, Client::Ui::PanelDialogOwner::None);
    EXPECT_EQ(dialog.hooks, Client::Ui::PanelDialogHooks::GuildPositions);
    EXPECT_EQ(dialog.flags, 0U);
    EXPECT_EQ(dialog.lineCount, 0);
    EXPECT_EQ(dialog.width, 0xd5);
    EXPECT_EQ(dialog.height, 0x2b);
    EXPECT_TRUE(m_sent.empty());

    // Closing does not clear the hooks; opening another one does.
    ASSERT_TRUE(Client::Ui::CloseThePanelDialog(dialog).HasValue());
    EXPECT_EQ(dialog.hooks, Client::Ui::PanelDialogHooks::GuildPositions);
    ASSERT_TRUE(Click(Client::Guild::GuildDismissButton).HasValue());
    EXPECT_EQ(dialog.owner, Client::Ui::PanelDialogOwner::GuildDismiss);
    EXPECT_EQ(dialog.hooks, Client::Ui::PanelDialogHooks::None);
    EXPECT_EQ(Draws(m_random), 0);
}

// 0x6600f9, the busy dialog's notice (0x6d69a8 in EUC-KR, main.exe's bytes).
std::string BusyNotice()
{
    constexpr std::array<std::uint8_t, 26> Binary{0xc0, 0xcc, 0xb9, 0xcc, 0x20, 0xc6, 0xcb, 0xbe, 0xf7,
                                                  0xc3, 0xa2, 0xc0, 0xcc, 0x20, 0xb6, 0xb0, 0x20, 0xc0,
                                                  0xd6, 0xbd, 0xc0, 0xb4, 0xcf, 0xb4, 0xd9, 0x2e};
    std::string text;
    for (const std::uint8_t byte : Binary)
        text.push_back(static_cast<char>(byte));
    return text;
}

// 0x657930 gives the dialog's step and then the window's: with the remove-position one open, the click on kick
// reaches 0x6507ad, and 0x6600d0 (0x6600f7..0x66011d) gives the notice of 0x6d69a8 in the chat (0x555190 with type 1),
// leaves the open one as it was and returns 0, which goes to +0x428. Without stopping and without rand().
TEST_P(GuildWindowTest, ASecondMasterButtonWithTheDialogOpenGetsTheBusyNotice)
{
    JoinTheGuild("");
    Hero().guildStatus = MasterStatus;
    AddMember("Sauce", BattleMasterStatus);
    ASSERT_TRUE(Click(Client::Guild::GuildDismissButton).HasValue());
    const Client::Ui::PanelDialogState& dialog = Runtime().panels.delegated.panelDialog;
    ASSERT_EQ(dialog.owner, Client::Ui::PanelDialogOwner::GuildDismiss);
    ASSERT_EQ(Guild().window.dialog, Now);
    ASSERT_TRUE(Runtime().chat.log.entries.empty());
    Guild().kickIndex = 7;

    // 0x6507ad: 0x64faa0's slot goes to 0x7d536c8 before 0x6600d0.
    const auto clicked = Click(Client::Guild::GuildKickButton);
    ASSERT_TRUE(clicked.HasValue()) << clicked.Error();
    ASSERT_EQ(Runtime().chat.log.entries.size(), 1U);
    EXPECT_EQ(Runtime().chat.log.entries.front().text, BusyNotice());
    EXPECT_EQ(Runtime().chat.log.entries.front().type, Client::Ui::SystemMessage);
    EXPECT_EQ(Guild().window.dialog, 0U);
    EXPECT_EQ(Guild().kickIndex, 0);
    EXPECT_EQ(dialog.opened, Now);
    EXPECT_EQ(dialog.owner, Client::Ui::PanelDialogOwner::GuildDismiss);
    EXPECT_EQ(dialog.lines[1], "pierde el cargo");
    EXPECT_EQ(dialog.hooks, Client::Ui::PanelDialogHooks::None);
    EXPECT_TRUE(m_sent.empty());
    EXPECT_EQ(Draws(m_random), 0);
}

// 0x6504b6..0x6504d4 with another one open: 0x6600d0 gives the notice and 0 (to +0x428), and 0x6602b0 still sets the
// positions window's hooks on the open one. Its step and draw become the positions' (Accept sends the E1 and the 52)
// and its closing answers whoever opened it (0x64def0 with Cancel, 0x10: nothing).
TEST_P(GuildWindowTest, TheAssignButtonHooksThePositionsOntoTheOpenDialog)
{
    JoinTheGuild("");
    Hero().guildStatus = MasterStatus;
    AddMember("Sauce", BattleMasterStatus);
    ASSERT_TRUE(Click(Client::Guild::GuildDismissButton).HasValue());
    Client::Ui::PanelDialogState& dialog = Runtime().panels.delegated.panelDialog;
    ASSERT_EQ(dialog.owner, Client::Ui::PanelDialogOwner::GuildDismiss);

    const auto clicked = Click(Client::Guild::GuildAssignButton);
    ASSERT_TRUE(clicked.HasValue()) << clicked.Error();
    ASSERT_EQ(Runtime().chat.log.entries.size(), 1U);
    EXPECT_EQ(Runtime().chat.log.entries.front().text, BusyNotice());
    EXPECT_EQ(Guild().window.dialog, 0U);
    EXPECT_EQ(Guild().chosenStatus, BattleMasterStatus);
    EXPECT_EQ(dialog.opened, Now);
    EXPECT_EQ(dialog.owner, Client::Ui::PanelDialogOwner::GuildDismiss);
    EXPECT_EQ(dialog.flags, Client::Ui::PanelDialogConfirm);
    EXPECT_EQ(dialog.hooks, Client::Ui::PanelDialogHooks::GuildPositions);

    ASSERT_TRUE(DrawTheDialog().HasValue());
    Guild().positions.choice = Client::Guild::AssistantChoice;
    const auto accepted = ClickThePositionsButton(Client::Guild::GuildPositionsOkButton);
    ASSERT_TRUE(accepted.HasValue()) << accepted.Error();
    ASSERT_EQ(m_sent.size(), 2U);
    EXPECT_EQ(PlainOf(m_sent[0]), BytesOf({0xc1, 0x0f, 0xe1, 0x02, 0x40, 'S', 'a', 'u', 'c', 'e', 0, 0, 0, 0, 0}));
    const auto list = m_sent[1].packet.Bytes();
    EXPECT_EQ(std::vector<std::byte>(list.begin(), list.end()), BytesOf({0xc1, 0x03, 0x52}));
    EXPECT_EQ(dialog.opened, 0U);
    EXPECT_EQ(dialog.owner, Client::Ui::PanelDialogOwner::None);
    EXPECT_EQ(Draws(m_random), 0);
}

// 0x64f1c0: the 0x96 box at (0xd6, 0x37), which leaves the corner for the step; the two 170 × 20 boxes at
// (x + 0x14, y + 0x13) and (x + 0x14, y + 0x27), the chosen position's filled (0x621aa0); the texts centered in 170
// from x + 0x14, with the font and colors that were set; and the two buttons at y + 0x75. No rand().
TEST_P(GuildWindowTest, ThePositionsWindowDrawsItsBoxesAndTexts)
{
    OpenThePositionsFor("Sauce", MemberStatus);
    Runtime().hud.textColors = Client::Render::Text::TextColors{.text = RowText, .background = 0};
    const auto drawn = DrawTheDialog();
    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    EXPECT_EQ(Guild().positions.x, 0xd6);
    EXPECT_EQ(Guild().positions.y, 0x37);

    const std::vector<DrawnQuad> quads = QuadsOf(m_sprites);
    EXPECT_EQ(CountQuadsAt(quads, 0xfc, 214.0f, 55.0f, 213.0f, 5.0f), 1U);
    EXPECT_EQ(CountQuadsAt(quads, 0xfb, 214.0f, 60.0f, 213.0f, 40.0f), 1U);
    EXPECT_EQ(CountQuadsAt(quads, 0xfb, 214.0f, 180.0f, 213.0f, 20.0f), 1U);
    EXPECT_EQ(CountQuadsAt(quads, 0xfc, 214.0f, 200.0f, 213.0f, 5.0f), 1U);
    // The assistant's (0x40, the one 0x64f3e0 leaves), filled; the warrior's, not.
    EXPECT_EQ(CountQuadsAt(quads, NoTexture, 234.0f, 74.0f, 170.0f, 20.0f), 1U);
    EXPECT_EQ(CountQuadsAt(quads, 0x117, 234.0f, 74.0f, 170.0f, 2.0f), 1U);
    EXPECT_EQ(CountQuadsAt(quads, 0x104, 234.0f, 74.0f, 2.0f, 20.0f), 1U);
    EXPECT_EQ(CountQuadsAt(quads, NoTexture, 234.0f, 94.0f, 170.0f, 20.0f), 0U);
    EXPECT_EQ(CountQuadsAt(quads, 0x117, 234.0f, 94.0f, 170.0f, 2.0f), 1U);

    struct Line
    {
        std::string_view text;
        int y;
    };
    const std::array<Line, 4> lines{Line{"Hacerlo ayudante", 55 + 0x18}, Line{"Hacerlo guerrero", 55 + 0x2e},
                                    Line{"Sauce sera Ayudante", 55 + 0x4b}, Line{"Elegi el cargo", 55 + 0x5d}};
    std::size_t at = 0;
    for (const Line& line : lines)
    {
        SCOPED_TRACE(line.text);
        at = FindText(m_text.texts, line.text, at);
        ASSERT_LT(at, m_text.texts.size());
        const auto& text = m_text.texts[at];
        const int width = Client::Tests::RecordingTextDrawer::LetterWidth * static_cast<int>(line.text.size());
        EXPECT_EQ(text.x, 234);
        EXPECT_EQ(text.y, line.y);
        EXPECT_TRUE(text.windowed);
        EXPECT_EQ(text.firstColumn, -((170 - width) / 2));
        EXPECT_EQ(text.style.colors.text, RowText);
    }
    EXPECT_LT(FindText(m_text.texts, "Aceptar", at), m_text.texts.size());
    EXPECT_LT(FindText(m_text.texts, "Cancelar", at), m_text.texts.size());
    EXPECT_EQ(Guild().positions.buttons[Client::Guild::GuildPositionsOkButton].base.x, 214 + 0x37);
    EXPECT_EQ(Guild().positions.buttons[Client::Guild::GuildPositionsCancelButton].base.x, 214 + 0x6e);
    EXPECT_EQ(Guild().positions.buttons[Client::Guild::GuildPositionsOkButton].base.y, 55 + 0x75);

    // With the warrior chosen, the filled one is the bottom one and the text says 1302.
    Guild().positions.choice = Client::Guild::BattleMasterChoice;
    m_text.texts.clear();
    m_sprites.Begin(Screen640);
    ASSERT_TRUE(DrawTheDialog().HasValue());
    const std::vector<DrawnQuad> again = QuadsOf(m_sprites);
    EXPECT_EQ(CountQuadsAt(again, NoTexture, 234.0f, 74.0f, 170.0f, 20.0f), 0U);
    EXPECT_EQ(CountQuadsAt(again, NoTexture, 234.0f, 94.0f, 170.0f, 20.0f), 1U);
    EXPECT_LT(FindText(m_text.texts, "Sauce sera Guerrero"), m_text.texts.size());
    EXPECT_EQ(Draws(m_random), 0);
}

// 0x64e600: with the click's edge, the boxes choose the position (0x40 or 0x20) from the last draw's corner; Accept
// sends C1 0F E1 01 (the chosen one without a position), the position and the 10 bytes of 0x7d535a8, and C1 03 52,
// and closes the dialog. No rand().
TEST_P(GuildWindowTest, ThePositionsWindowAppointsAMember)
{
    OpenThePositionsFor("Sauce", MemberStatus);
    // Before the first draw the corner is 0x64f3e0's, (0, 0).
    ASSERT_TRUE(ClickTheDialogAt(0x14 + 2, 0x29 + 2).HasValue());
    EXPECT_EQ(Guild().positions.choice, Client::Guild::BattleMasterChoice);
    ASSERT_TRUE(DrawTheDialog().HasValue());
    Runtime().ui.mouseOnUi = 0;
    ASSERT_TRUE(ClickTheDialogAt(234 + 2, 74 + 2).HasValue());
    EXPECT_EQ(Guild().positions.choice, Client::Guild::AssistantChoice);
    EXPECT_EQ(Runtime().ui.mouseOnUi, 1);
    ASSERT_TRUE(ClickTheDialogAt(234 + 2, 55 + 0x29 + 2).HasValue());
    EXPECT_EQ(Guild().positions.choice, Client::Guild::BattleMasterChoice);
    EXPECT_TRUE(m_sent.empty());
    ASSERT_NE(Runtime().panels.delegated.panelDialog.opened, 0U);

    const auto clicked = ClickThePositionsButton(Client::Guild::GuildPositionsOkButton);
    ASSERT_TRUE(clicked.HasValue()) << clicked.Error();
    ASSERT_EQ(m_sent.size(), 2U);
    EXPECT_EQ(PlainOf(m_sent[0]), BytesOf({0xc1, 0x0f, 0xe1, 0x01, 0x20, 'S', 'a', 'u', 'c', 'e', 0, 0, 0, 0, 0}));
    const auto list = m_sent[1].packet.Bytes();
    EXPECT_EQ(std::vector<std::byte>(list.begin(), list.end()), BytesOf({0xc1, 0x03, 0x52}));
    EXPECT_EQ(Runtime().panels.delegated.panelDialog.opened, 0U);
    EXPECT_EQ(Runtime().lastSoundId, Client::Audio::SoundId{0x19});
    EXPECT_EQ(Draws(m_random), 0);
}

// 0x64eb53: with the chosen one holding a position (0x7d535b4 other than 0), the E1 goes with 2; the 10 bytes are
// those of 0x7d535a8 after the strcpy (what follows the 0 stays from the previous name, Q-C1292). Cancel (0x7d53640)
// closes without sending.
TEST_P(GuildWindowTest, ThePositionsWindowChangesAPositionOrCancels)
{
    Fill(Guild().chosenName, "Algarrobo");
    OpenThePositionsFor("Tala", BattleMasterStatus);
    ASSERT_TRUE(DrawTheDialog().HasValue());
    Guild().positions.choice = Client::Guild::AssistantChoice;
    ASSERT_TRUE(ClickThePositionsButton(Client::Guild::GuildPositionsOkButton).HasValue());
    ASSERT_EQ(m_sent.size(), 2U);
    EXPECT_EQ(PlainOf(m_sent[0]),
              BytesOf({0xc1, 0x0f, 0xe1, 0x02, 0x40, 'T', 'a', 'l', 'a', 0, 'r', 'o', 'b', 'o', 0}));

    m_sent.clear();
    ASSERT_TRUE(Click(Client::Guild::GuildAssignButton).HasValue());
    ASSERT_NE(Runtime().panels.delegated.panelDialog.opened, 0U);
    const auto cancelled = ClickThePositionsButton(Client::Guild::GuildPositionsCancelButton);
    ASSERT_TRUE(cancelled.HasValue()) << cancelled.Error();
    EXPECT_EQ(Runtime().panels.delegated.panelDialog.opened, 0U);
    EXPECT_TRUE(m_sent.empty());
    EXPECT_EQ(Draws(m_random), 0);
}

// 0x64fdf0 without being master: the 53 frame with the 48 mark of the hero's guild (0x5f16a0(3, 1)), the 140 × 62
// frame with the guild, the score, the members (1310) and who wrote the notice in boxes of 140 from x + 0x1e, every
// 0xd from y + 0xb2; the notice title at y + 0xfa in one of 136; the notice list, 0x88 × 0x50, hanging from
// y + 0x154, its row (the chosen one, in black) and the 140 × 95 frame; the +0x42c button with 189. No rand().
TEST_P(GuildWindowTest, TheNoticeTabDrawsTheGuildAndItsNotice)
{
    JoinTheGuild("");
    Guild().window.tab = 0;
    Fill(Guild().window.name88, "Ceibo");
    AddMember("Sauce", MemberStatus);
    AddMember("Ombu", MemberStatus);
    AddNotice("Hola socios");
    const auto drawn = Draw();
    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();

    const std::vector<DrawnQuad> quads = QuadsOf(m_sprites);
    EXPECT_EQ(CountQuadsAt(quads, 0x117, Left + 0x45, 0x68, 53.0f, 2.0f), 1U);
    EXPECT_EQ(CountQuadsAt(quads, GuildMarkTextures::SlotOf(InTheGuild, true).value, Left + 0x48, 0x6b, 48.0f, 48.0f),
              1U);
    EXPECT_EQ(CountQuadsAt(quads, 0x117, Left + 0x19, 0xaa, 140.0f, 2.0f), 1U);
    EXPECT_EQ(CountQuadsAt(quads, 0x104, Left + 0x19, 0xaa, 2.0f, 62.0f), 1U);
    EXPECT_EQ(CountQuadsAt(quads, 0x117, Left + 0x19, 0xf6, 140.0f, 2.0f), 1U);
    EXPECT_EQ(CountQuadsAt(quads, 0x104, Left + 0x19, 0xf6, 2.0f, 95.0f), 1U);

    const Client::Ui::OldList& list = Guild().window.noticeList;
    EXPECT_EQ(list.base.x, WindowX + 0x1c);
    EXPECT_EQ(list.base.y, 0x104 + 0x50);
    EXPECT_EQ(list.base.width, 0x88);
    EXPECT_EQ(list.base.height, 0x50);

    ASSERT_EQ(m_text.texts.size(), 12U);
    const std::array<std::string_view, 4> lines{"Clan :", "Puntos: 17", "Socios: 2", "Aviso de : Ceibo"};
    for (std::size_t line = 0; line < lines.size(); ++line)
    {
        SCOPED_TRACE(line);
        const auto& text = m_text.texts[5 + line];
        EXPECT_EQ(text.text, lines[line]);
        EXPECT_EQ(text.x, WindowX + 0x1e);
        EXPECT_EQ(text.y, 0xb2 + 0xd * static_cast<int>(line));
        EXPECT_TRUE(text.windowed);
        EXPECT_EQ(text.firstColumn, 0);
        EXPECT_EQ(text.columns, 140);
    }
    EXPECT_EQ(m_text.texts[9].text, "El aviso");
    EXPECT_EQ(m_text.texts[9].y, 0xfa);
    EXPECT_EQ(m_text.texts[9].columns, 136);
    EXPECT_EQ(m_text.texts[10].text, "Hola socios");
    EXPECT_EQ(m_text.texts[10].x, WindowX + 0x1c + 4);
    EXPECT_EQ(m_text.texts[10].y, Client::Ui::ListRowY(list, 1, 0));
    EXPECT_EQ(m_text.texts[10].style.colors.text, ChosenText);
    EXPECT_EQ(m_text.texts[11].text, "Dejar");
    EXPECT_EQ(Draws(m_random), 0);
}

// 0x6500e8: the master opens its record (0x57630c8) for the member limit and when closing it draws its rand(): level
// / 10, or with class 4 (+0xb & 7) leadership / 10 plus level / 10, up to 0x50. Without whoever wrote the notice,
// 1361; the button, with 188.
TEST_P(GuildWindowTest, TheMastersNoticeTabShowsTheMemberCapAndClosesTheRecord)
{
    JoinTheGuild("");
    Hero().guildStatus = MasterStatus;
    Guild().window.tab = 0;
    AddMember("Sauce", MasterStatus);
    AddMember("Ombu", MemberStatus);
    Client::Kernel::OwnCharacterRecord& record = m_world->OwnCharacter();
    record.characterClass = 1;
    record.level = 123;
    record.leadership = 700;
    ASSERT_TRUE(Draw().HasValue());
    ASSERT_EQ(m_text.texts.size(), 11U);
    EXPECT_EQ(m_text.texts[7].text, "Socios: 2/12");
    EXPECT_EQ(m_text.texts[8].text, "Aviso de : Nadie");
    EXPECT_EQ(m_text.texts.back().text, "Disolver");
    EXPECT_EQ(Draws(m_random), Client::Models::OwnRecordCloseRandoms);

    m_text.texts.clear();
    record.characterClass = 0x14;
    record.level = 400;
    ASSERT_TRUE(Draw().HasValue());
    ASSERT_GE(m_text.texts.size(), 8U);
    EXPECT_EQ(m_text.texts[7].text, "Socios: 2/80");
    EXPECT_EQ(Draws(m_random), 2 * Client::Models::OwnRecordCloseRandoms);
}

// 0x6507d0 with the master: the member list (0x47a810 with 0x46cb20 and 0x46d050): headers 1389, 1307 and 1022 at
// y − height − 0xc; each row with its name at x + 8, the rank in its color (0x20, 0xc8 and 0x64 with alpha 0x7f) and
// its text centered in 0x46 from x + 8 + 0x2d, and the server + 1 in 0xff00c4ff; the chosen one in black. Then, the
// three buttons.
TEST_P(GuildWindowTest, TheMembersTabDrawsTheRowsAndTheMasterButtons)
{
    JoinTheGuild("");
    Hero().guildStatus = MasterStatus;
    AddMember("Sauce", BattleMasterStatus, 0);
    AddMember("Ombu", MemberStatus, 0xff);
    const auto drawn = Draw();
    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    const Client::Ui::OldList& list = Guild().window.memberList;
    const auto& texts = m_text.texts;
    ASSERT_GE(texts.size(), 8U);
    const std::array<std::string_view, 3> headers{"Nombre", "Cargo", "Server"};
    const std::array<int, 3> lefts{0xe, 0x41, 0x6a};
    for (std::size_t header = 0; header < headers.size(); ++header)
    {
        SCOPED_TRACE(header);
        EXPECT_EQ(texts[5 + header].text, headers[header]);
        EXPECT_EQ(texts[5 + header].x, 0x1d2 + lefts[header]);
        EXPECT_EQ(texts[5 + header].y, 0x159 - 0xeb - 0xc);
    }

    const std::size_t sauce = FindText(texts, "Sauce");
    ASSERT_LT(sauce + 2, texts.size());
    const int sauceY = Client::Ui::ListRowY(list, 2, 1);
    EXPECT_EQ(texts[sauce].x, 0x1d2 + 8);
    EXPECT_EQ(texts[sauce].y, sauceY);
    EXPECT_EQ(texts[sauce].style.colors.text, ChosenText);
    EXPECT_EQ(texts[sauce + 1].text, "Guerrero");
    EXPECT_EQ(texts[sauce + 1].x, 0x1d2 + 8 + 0x2d);
    EXPECT_TRUE(texts[sauce + 1].windowed);
    EXPECT_EQ(texts[sauce + 1].firstColumn, -11);
    EXPECT_EQ(texts[sauce + 1].columns, 70);
    EXPECT_EQ(texts[sauce + 2].text, "1");
    EXPECT_EQ(texts[sauce + 2].x, 0x1d2 + 0x9d - 0x1e);
    EXPECT_EQ(texts[sauce + 2].style.colors.text, 0xff00c4ffu);

    const std::size_t ombu = FindText(texts, "Ombu");
    ASSERT_LT(ombu + 1, texts.size());
    EXPECT_EQ(texts[ombu].y, Client::Ui::ListRowY(list, 2, 0));
    EXPECT_EQ(texts[ombu].style.colors.text, RowText);
    EXPECT_NE(texts[ombu + 1].y, texts[ombu].y);

    ASSERT_GE(texts.size(), 3U);
    EXPECT_EQ(texts[texts.size() - 3].text, "Cargo");
    EXPECT_EQ(texts[texts.size() - 2].text, "Quitar");
    EXPECT_EQ(texts[texts.size() - 1].text, "Echar");

    const std::vector<DrawnQuad> quads = QuadsOf(m_sprites);
    const auto band = std::find_if(quads.begin(), quads.end(),
                                   [](const DrawnQuad& quad)
                                   {
                                       return quad.slot == NoTexture &&
                                              std::abs(quad.first.color.green - 200.0f / 255.0f) < 1e-4f &&
                                              std::abs(quad.first.color.blue - 100.0f / 255.0f) < 1e-4f;
                                   });
    ASSERT_NE(band, quads.end());
    EXPECT_NEAR(band->first.x, 466.0f, 1e-3f);
    EXPECT_NEAR(band->first.y, 480.0f - static_cast<float>(sauceY - 3), 1e-3f);
    EXPECT_NEAR(band->first.color.alpha, 127.0f / 255.0f, 1e-4f);
    EXPECT_EQ(Draws(m_random), 0);
}

// 0x651400 without a union (+4 of the hero's entry empty): texts 1257 to 1267 at x + 0x19 from y + 0x6a; from the
// sixth, in white on 0xff0000cc, which stays.
TEST_P(GuildWindowTest, WithoutAnAllianceTheUnionTabExplainsIt)
{
    JoinTheGuild("");
    Guild().window.tab = 2;
    ASSERT_TRUE(Draw().HasValue());
    ASSERT_EQ(m_text.texts.size(), 16U);
    const std::array<int, 11> tops{0x6a, 0x79, 0x88, 0x97, 0xa6, 0xbf, 0xce, 0xdd, 0xf1, 0x100, 0x10f};
    for (std::size_t line = 0; line < tops.size(); ++line)
    {
        SCOPED_TRACE(line);
        const auto& text = m_text.texts[5 + line];
        EXPECT_EQ(text.text, std::format("Sin alianza {}", line));
        EXPECT_EQ(text.x, WindowX + 0x19);
        EXPECT_EQ(text.y, tops[line]);
        if (line >= 5)
        {
            EXPECT_EQ(text.style.colors.text, 0xffffffffu);
            EXPECT_EQ(text.style.colors.background, 0xff0000ccu);
        }
    }
    EXPECT_NE(m_text.texts[9].style.colors.background, 0xff0000ccu);
    EXPECT_EQ(Runtime().hud.textColors.background, 0xff0000ccu);
    EXPECT_EQ(Draws(m_random), 0);
}

// 0x651400 with a union: list +0x2f8, 0xa0 × 0x50, at x + 0xf, hanging from y + 0x6e plus the notice list's height,
// which is 0x45a210's 100 until tab 0's first draw (Q-C1293); each row (0x46dfa0) with the 8 × 8 mark through entry
// 1999 (which then goes back to the hero's guild's, Q-C1294), the name at x + 0x10 and the number right of x + 0x8e.
// The union master sees +0x6d4 (1422) and +0x64c with 1324; the other master, +0x64c with 1325 and +0x6d4 off.
TEST_P(GuildWindowTest, TheUnionTabListsTheGuildsAndTheMasterButtons)
{
    JoinTheGuild("Talas");
    Hero().guildStatus = MasterStatus;
    Guild().table[InTheGuild].mark.fill(5);
    Guild().window.tab = 2;
    AddUnion("Talas", 3, 1);
    AddUnion("Ombues", 12, 2);
    auto drawn = Draw();
    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    const Client::Ui::OldList& list = Guild().window.unionList;
    EXPECT_EQ(list.base.x, WindowX + 0xf);
    EXPECT_EQ(list.base.y, 0x6e + 100);
    EXPECT_EQ(list.base.width, 0xa0);
    EXPECT_EQ(list.base.height, 0x50);

    const auto& texts = m_text.texts;
    ASSERT_GE(texts.size(), 7U);
    EXPECT_EQ(texts[5].text, "Gremios");
    EXPECT_EQ(texts[5].x, WindowX + 0xf + 0xf);
    EXPECT_EQ(texts[5].y, 0x6e + 100 - 0x50 - 0xc);
    EXPECT_EQ(texts[6].text, "Miembros");
    EXPECT_EQ(texts[6].x, WindowX + 0xf + 0x71);

    const std::size_t talas = FindText(texts, "Talas");
    ASSERT_LT(talas + 1, texts.size());
    const int talasY = Client::Ui::ListRowY(list, 2, 1);
    EXPECT_EQ(texts[talas].x, WindowX + 0xf + 4 + 0xc);
    EXPECT_EQ(texts[talas].y, talasY);
    EXPECT_EQ(texts[talas].style.colors.text, ChosenText);
    EXPECT_EQ(texts[talas + 1].text, "3");
    EXPECT_EQ(texts[talas + 1].x, WindowX + 0xf + 4 + 0x8a - 6);
    const std::size_t ombues = FindText(texts, "Ombues");
    ASSERT_LT(ombues + 1, texts.size());
    EXPECT_EQ(texts[ombues].style.colors.text, RowText);
    EXPECT_EQ(texts[ombues + 1].text, "12");
    EXPECT_EQ(texts[ombues + 1].x, WindowX + 0xf + 4 + 0x8a - 12);

    // P-C289: each row with its slot and its mark (Ombues, gray 2, in 0; Talas, black 1, in 1); nothing in the one of
    // (1999, 1).
    const std::vector<DrawnQuad> quads = QuadsOf(m_sprites);
    const int firstRow = Client::Render::FirstGuildUnionMarkTextureSlot.value;
    EXPECT_EQ(CountQuadsAt(quads, firstRow + 1, static_cast<float>(WindowX + 0xf + 4), static_cast<float>(talasY), 8.0f,
                           8.0f),
              1U);
    EXPECT_EQ(CountQuadsAt(quads, firstRow, static_cast<float>(WindowX + 0xf + 4),
                           static_cast<float>(Client::Ui::ListRowY(list, 2, 0)), 8.0f, 8.0f),
              1U);
    EXPECT_FALSE(m_store.Find(GuildMarkTextures::SlotOf(Client::Guild::CreatingGuildIndex, true)).has_value());
    const auto ombuesMark = m_store.Find(Client::Render::TextureSlot{static_cast<std::uint16_t>(firstRow)});
    const auto talasMark = m_store.Find(Client::Render::TextureSlot{static_cast<std::uint16_t>(firstRow + 1)});
    ASSERT_TRUE(ombuesMark.has_value());
    ASSERT_TRUE(talasMark.has_value());
    EXPECT_EQ(ombuesMark->texture.pixels[0], 0x80);
    EXPECT_EQ(talasMark->texture.pixels[0], 0);
    EXPECT_EQ(talasMark->texture.pixels[3], 0xff);
    EXPECT_EQ(Guild().table[static_cast<std::size_t>(Client::Guild::CreatingGuildIndex)].mark,
              Guild().table[InTheGuild].mark);

    const Client::Guild::GuildWindowState& window = Guild().window;
    EXPECT_EQ(texts[texts.size() - 2].text, "Echarlo");
    EXPECT_EQ(texts[texts.size() - 1].text, "Romperla");
    EXPECT_EQ(window.tabButtons[Client::Guild::GuildUnionKickButton].base.state, Client::Ui::ShownWindow);

    EXPECT_EQ(Draws(m_random), 0);

    // After tab 0 (which, with the master, opens and closes the own record), the list hangs from height 0x50.
    Guild().window.tab = 0;
    ASSERT_TRUE(Draw().HasValue());
    Guild().window.tab = 2;
    ASSERT_TRUE(Draw().HasValue());
    EXPECT_EQ(list.base.y, 0x6e + 0x50);

    // With another union: +0x64c with 1325 and +0x6d4 off, not drawn.
    Fill(Guild().table[InTheGuild].unionName, "Ceibo");
    m_text.texts.clear();
    ASSERT_TRUE(Draw().HasValue());
    ASSERT_FALSE(m_text.texts.empty());
    EXPECT_EQ(m_text.texts.back().text, "Dejarla");
    EXPECT_EQ(FindText(m_text.texts, "Echarlo"), m_text.texts.size());
    EXPECT_EQ(window.tabButtons[Client::Guild::GuildUnionKickButton].base.state, Client::Ui::DisabledWindow);
    EXPECT_EQ(Draws(m_random), Client::Models::OwnRecordCloseRandoms);
}

// 0x64fc00: the master of the guild that is the union's (+0xd equal to +4) gets notice 1270 with OK and no function.
TEST_P(GuildWindowTest, TheAllianceMasterCannotLeaveTheGuild)
{
    JoinTheGuild("Talas");
    Hero().guildStatus = MasterStatus;
    Guild().window.tab = 0;
    const auto clicked = ClickTheTabButton(Client::Guild::GuildLeaveButton);
    ASSERT_TRUE(clicked.HasValue()) << clicked.Error();
    const Client::Ui::PanelDialogState& dialog = Runtime().panels.delegated.panelDialog;
    EXPECT_EQ(dialog.owner, Client::Ui::PanelDialogOwner::None);
    EXPECT_EQ(dialog.flags, Client::Ui::PanelDialogOk);
    ASSERT_EQ(dialog.lineCount, 1);
    EXPECT_EQ(dialog.lines[0], "No deja el gremio");
    EXPECT_EQ(Guild().window.dialog, Now);
    EXPECT_EQ(Runtime().dialogs.current, 0);
    EXPECT_TRUE(m_sent.empty());
}

// 0x64fce7 and 0x64de90: the other master opens the disband dialog (1363 to 1366, Confirm); Confirm opens the game's
// dialog 0x7d with slot 0 in 0x6d5e2c, the chat field closed and a single field for Tab. Cancel, nothing.
TEST_P(GuildWindowTest, TheMasterDisbandsTheGuildThroughTheNumberDialog)
{
    JoinTheGuild("");
    Hero().guildStatus = MasterStatus;
    Guild().window.tab = 0;
    ASSERT_TRUE(ClickTheTabButton(Client::Guild::GuildLeaveButton).HasValue());
    const Client::Ui::PanelDialogState& dialog = Runtime().panels.delegated.panelDialog;
    EXPECT_EQ(dialog.owner, Client::Ui::PanelDialogOwner::GuildDisband);
    EXPECT_EQ(dialog.flags, Client::Ui::PanelDialogConfirm);
    ASSERT_EQ(dialog.lineCount, 4);
    for (std::size_t line = 0; line < 4; ++line)
        EXPECT_EQ(dialog.lines[line], std::format("Disolver {}", line + 1));

    auto answered =
        Client::Guild::AnswerThePanelDialog(*m_world, Sender(), dialog.owner, Client::Ui::PanelAnswerCancel);
    ASSERT_TRUE(answered.HasValue());
    EXPECT_EQ(Runtime().dialogs.current, 0);

    Runtime().ui.chatOpen = true;
    answered = Client::Guild::AnswerThePanelDialog(*m_world, Sender(), dialog.owner, Client::Ui::PanelAnswerConfirm);
    ASSERT_TRUE(answered.HasValue());
    EXPECT_TRUE(answered.Value());
    EXPECT_EQ(Runtime().dialogs.current, 0x7d);
    EXPECT_EQ(Guild().kickMember, 0);
    EXPECT_FALSE(Runtime().ui.chatOpen);
    EXPECT_EQ(Runtime().chat.input.fieldCount, 1);
    EXPECT_TRUE(m_sent.empty());
    EXPECT_EQ(Draws(m_random), 0);
}

// 0x64fce7..0x64fd8f: the disband rows are contiguous 0x32-byte slots. 1364 in Spanish (54 bytes) runs into the one
// below and 1365's strcpy overwrites its tail: row 2 is its first 50 bytes and 1365 (Q-C1366). A text past the fourth
// slot stops.
TEST_P(GuildWindowTest, ALongDisbandLineRunsIntoTheNextOne)
{
    const std::string spanish = "Todos los elementos del Baul del gremio desaparecieron";
    ASSERT_EQ(spanish.size(), 54u);
    m_texts = WindowTexts({{1364, spanish}, {1365, "Y el Ranking del gremio tambien desaparecio"}});
    JoinTheGuild("");
    Hero().guildStatus = MasterStatus;
    Guild().window.tab = 0;
    ASSERT_TRUE(ClickTheTabButton(Client::Guild::GuildLeaveButton).HasValue());
    const Client::Ui::PanelDialogState& dialog = Runtime().panels.delegated.panelDialog;
    ASSERT_EQ(dialog.lineCount, 4);
    EXPECT_EQ(dialog.lines[0], "Disolver 1");
    EXPECT_EQ(dialog.lines[1], spanish.substr(0, 0x32) + "Y el Ranking del gremio tambien desaparecio");
    EXPECT_EQ(dialog.lines[2], "Y el Ranking del gremio tambien desaparecio");
    EXPECT_EQ(dialog.lines[3], "Disolver 4");

    Runtime().panels.delegated.panelDialog = {};
    Guild().window.dialog = 0;
    m_texts = WindowTexts({{1366, std::string(0x32, 'x')}});
    const auto cut = ClickTheTabButton(Client::Guild::GuildLeaveButton);
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x64fd8f"), std::string::npos);
}

// 0x64fd9c: one who is not master leaves: its slot in 0x787a5e8 (0x64faa0 with its name) in 0x6d5e2c and dialog 0x7d.
TEST_P(GuildWindowTest, AMemberLeavesTheGuildThroughTheNumberDialog)
{
    JoinTheGuild("");
    Hero().name = {std::byte{'O'}, std::byte{'m'}, std::byte{'b'}, std::byte{'u'}};
    Guild().window.tab = 0;
    AddMember("Sauce", MasterStatus);
    AddMember("Ombu", MemberStatus);
    ASSERT_TRUE(ClickTheTabButton(Client::Guild::GuildLeaveButton).HasValue());
    EXPECT_EQ(Guild().kickMember, 1);
    EXPECT_EQ(Runtime().dialogs.current, 0x7d);
    EXPECT_EQ(Runtime().chat.input.fieldCount, 1);
    EXPECT_EQ(Runtime().panels.delegated.panelDialog.opened, 0U);
    EXPECT_TRUE(m_sent.empty());
}

// 0x650d22: the union master does not leave it (1271, OK).
TEST_P(GuildWindowTest, TheAllianceMasterCannotLeaveTheAlliance)
{
    JoinTheGuild("Talas");
    Hero().guildStatus = MasterStatus;
    Guild().window.tab = 2;
    ASSERT_TRUE(ClickTheTabButton(Client::Guild::GuildUnionLeaveButton).HasValue());
    const Client::Ui::PanelDialogState& dialog = Runtime().panels.delegated.panelDialog;
    EXPECT_EQ(dialog.flags, Client::Ui::PanelDialogOk);
    ASSERT_EQ(dialog.lineCount, 1);
    EXPECT_EQ(dialog.lines[0], "No deja la alianza");
    EXPECT_TRUE(m_sent.empty());
}

// 0x650d35..0x651290: the other master sends C1 07 E5 01 02 with its key (+0x1f5 and +0x1f4) with the XOR32 and gives
// notice 1387 in the chat (0x555190 with type 1).
TEST_P(GuildWindowTest, AMasterLeavesTheAlliance)
{
    JoinTheGuild("Ceibo");
    Hero().guildStatus = MasterStatus;
    Hero().key = 0x1234;
    Guild().window.tab = 2;
    const auto clicked = ClickTheTabButton(Client::Guild::GuildUnionLeaveButton);
    ASSERT_TRUE(clicked.HasValue()) << clicked.Error();
    ASSERT_EQ(m_sent.size(), 1U);
    EXPECT_EQ(PlainOf(m_sent[0]), BytesOf({0xc1, 0x07, 0xe5, 0x01, 0x02, 0x12, 0x34}));
    ASSERT_FALSE(Runtime().chat.log.entries.empty());
    EXPECT_EQ(Runtime().chat.log.entries.back().text, "Dejaste la alianza");
    EXPECT_EQ(Runtime().chat.log.entries.back().type, Client::Ui::SystemMessage);
    EXPECT_EQ(Runtime().panels.delegated.panelDialog.opened, 0U);
    EXPECT_EQ(Draws(m_random), 0);
}

// 0x6512a2..0x6513e0 and 0x650830: +0x6d4 on another guild of the union copies its name to 0x7d535a8 (strcpy: what is
// past the 0 stays from the previous name, Q-C1292) and opens the dialog of 1423 and 1369; Confirm sends C1 0C EB 01
// and the 8 bytes of 0x7d535a8 with the XOR32. On the own guild, nothing.
TEST_P(GuildWindowTest, TheAllianceMasterKicksAGuildOutOfIt)
{
    JoinTheGuild("Talas");
    Hero().guildStatus = MasterStatus;
    Guild().window.tab = 2;
    Fill(Guild().chosenName, "Sauceda1234");
    AddUnion("Talas", 3, 1);
    AddUnion("Ombues", 12, 2);
    Client::Ui::SelectInTheList(Guild().window.unionList, Guild().window.unions.size(), 1);
    const auto clicked = ClickTheTabButton(Client::Guild::GuildUnionKickButton);
    ASSERT_TRUE(clicked.HasValue()) << clicked.Error();
    const Client::Ui::PanelDialogState& dialog = Runtime().panels.delegated.panelDialog;
    EXPECT_EQ(dialog.owner, Client::Ui::PanelDialogOwner::UnionKick);
    EXPECT_EQ(dialog.flags, Client::Ui::PanelDialogConfirm);
    ASSERT_EQ(dialog.lineCount, 2);
    EXPECT_EQ(dialog.lines[0], "Gremio: Ombues");
    EXPECT_EQ(dialog.lines[1], "se va del gremio");
    const std::array<char, 12> chosen{'O', 'm', 'b', 'u', 'e', 's', '\0', '1', '2', '3', '4', '\0'};
    EXPECT_EQ(Guild().chosenName, chosen);
    EXPECT_TRUE(m_sent.empty());

    auto answered =
        Client::Guild::AnswerThePanelDialog(*m_world, Sender(), dialog.owner, Client::Ui::PanelAnswerCancel);
    ASSERT_TRUE(answered.HasValue());
    EXPECT_TRUE(m_sent.empty());
    answered = Client::Guild::AnswerThePanelDialog(*m_world, Sender(), dialog.owner, Client::Ui::PanelAnswerConfirm);
    ASSERT_TRUE(answered.HasValue());
    EXPECT_TRUE(answered.Value());
    ASSERT_EQ(m_sent.size(), 1U);
    EXPECT_EQ(PlainOf(m_sent[0]), BytesOf({0xc1, 0x0c, 0xeb, 0x01, 'O', 'm', 'b', 'u', 'e', 's', 0, '1'}));

    Runtime().panels.delegated.panelDialog = Client::Ui::PanelDialogState{};
    Client::Ui::SelectInTheList(Guild().window.unionList, Guild().window.unions.size(), 2);
    ASSERT_TRUE(ClickTheTabButton(Client::Guild::GuildUnionKickButton).HasValue());
    EXPECT_EQ(Runtime().panels.delegated.panelDialog.opened, 0U);
    EXPECT_EQ(Draws(m_random), 0);
}

// 0x650c5a and 0x650c7b: without being master, or without a union, the tab's buttons do nothing.
TEST_P(GuildWindowTest, OnlyAMasterWithAnAllianceHasTheUnionButtons)
{
    JoinTheGuild("Talas");
    Guild().window.tab = 2;
    AddUnion("Ombues", 12, 2);
    ASSERT_TRUE(ClickTheTabButton(Client::Guild::GuildUnionLeaveButton).HasValue());
    ASSERT_TRUE(ClickTheTabButton(Client::Guild::GuildUnionKickButton).HasValue());
    Hero().guildStatus = MasterStatus;
    Fill(Guild().table[InTheGuild].unionName, "");
    ASSERT_TRUE(ClickTheTabButton(Client::Guild::GuildUnionLeaveButton).HasValue());
    ASSERT_TRUE(ClickTheTabButton(Client::Guild::GuildUnionKickButton).HasValue());
    EXPECT_EQ(Runtime().panels.delegated.panelDialog.opened, 0U);
    EXPECT_TRUE(m_sent.empty());
}

// 0x651680: the notice cut into rows of 0x6e (0x459c70), each at the front (0x46bca0), which scrolls it to the end
// (the count minus the rows), and at the end scrolled as much the other way (+0x34 with the count minus the rows):
// scroll 0, with the last one in on the bottom row. 0x651790 empties it.
TEST_P(GuildWindowTest, TheNoticeLinesGoFirstAndTheLastOnesStayInView)
{
    for (int notice = 1; notice <= 8; ++notice)
        AddNotice(std::format("Aviso {}", notice));
    const Client::Guild::GuildWindowState& window = Guild().window;
    ASSERT_EQ(window.notice.size(), 8U);
    EXPECT_EQ(window.notice[0].text, "Aviso 8");
    EXPECT_EQ(window.notice[7].text, "Aviso 1");
    EXPECT_EQ(window.noticeList.selection, 8);
    EXPECT_EQ(window.noticeList.scroll, 0);

    const std::string_view text = "uno dos tres cuatro cinco seis siete";
    const auto wrapped = Client::Chat::WrapByWidth(text, m_text, Runtime().hud.dcFont, Screen640, 0x6e, 5, 0);
    ASSERT_TRUE(wrapped.HasValue());
    std::vector<std::string> lines;
    for (const std::string& line : wrapped.Value().lines)
    {
        if (!line.empty())
            lines.push_back(line);
    }
    ASSERT_GE(lines.size(), 2U);
    AddNotice(text);
    ASSERT_EQ(window.notice.size(), 8U + lines.size());
    for (std::size_t line = 0; line < lines.size(); ++line)
        EXPECT_EQ(window.notice[line].text, lines[lines.size() - 1 - line]);

    Client::Guild::ClearTheGuildNotice(Guild().window);
    EXPECT_TRUE(window.notice.empty());
    EXPECT_EQ(window.noticeList.selection, 0);
    EXPECT_EQ(window.noticeList.scroll, 0);
}

// 0x6517e0 → 0x46d900: without a name, nothing; the name with strncpy of 8 and the number, at the front. 0x651830
// empties it.
TEST_P(GuildWindowTest, TheUnionListPutsEachGuildFirst)
{
    AddUnion("", 1, 1);
    EXPECT_TRUE(Guild().window.unions.empty());
    AddUnion("Talas", 3, 1);
    AddUnion("Ombues", 12, 2);
    const Client::Guild::GuildWindowState& window = Guild().window;
    ASSERT_EQ(window.unions.size(), 2U);
    EXPECT_EQ(Client::Guild::GuildTextOf(window.unions[0].name), "Ombues");
    EXPECT_EQ(window.unions[0].byte0, 12);
    EXPECT_EQ(window.unions[0].mark[0], 2);
    EXPECT_EQ(Client::Guild::GuildTextOf(window.unions[1].name), "Talas");
    EXPECT_EQ(window.unionList.selection, 2);
    Client::Guild::ClearTheUnions(Guild().window);
    EXPECT_TRUE(window.unions.empty());
    EXPECT_EQ(window.unionList.selection, 0);
}

INSTANTIATE_TEST_SUITE_P(Maps, GuildWindowTest, ::testing::Values(0, 2));
} // namespace
