#include "Scenes/CastleWindows.h"

#include "App/WindowState.h"
#include "Core/Expected.h"
#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Core/StderrSink.h"
#include "Data/Text/TextTable.h"
#include "Input/InputState.h"
#include "Network/Crypto/Xor32.h"
#include "Protocol/OptionData.h"
#include "Protocol/Outgoing.h"
#include "Protocol/CastleSiege.h"
#include "Render/Models/Cloth.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Scenes/GameUi.h"
#include "Scenes/GameHud.h"
#include "Scenes/GameChat.h"
#include "Scenes/GuildDrawing.h"
#include "Scenes/CastleSiegeReceive.h"
#include "Scenes/GuildState.h"
#include "UI/CastleSiegeState.h"
#include "UI/PanelDialog.h"
#include "UI/ChatInput.h"
#include "Render/SpriteBatch.h"
#include "Render/ScreenScale.h"
#include "Render/TextureSlot.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/OldWindowBase.h"
#include "UI/PanelRegistry.h"

#include "support/Bytes.h"
#include "support/SameBytes.h"
#include "support/SceneDataFiles.h"
#include "support/RecordingTextDrawer.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
using Client::Tests::Bytes;
using Client::Tests::SameBytes;
class Requests final : public Client::Scenes::IGameUiRequests
{
public:
    void SendOptionData(const Client::Protocol::OptionData::OptionBody&) override {}
    void RequestLogout(std::uint8_t) override {}
    void RequestPartyList() override {}
    void SendGuildPacket(Client::Protocol::Outgoing packet) override
    {
        sent.push_back(std::move(packet));
    }
    void SendGamePacket(Client::Protocol::Outgoing packet) override
    {
        sent.push_back(std::move(packet));
    }
    void SendWindowClose() override {}
    void SendSetQuest(std::uint8_t, std::uint8_t) override {}
    void SendFriendPacket(Client::Protocol::Outgoing packet) override
    {
        sent.push_back(std::move(packet));
    }
    Client::Core::Expected<void, std::string> SaveMacros(std::string_view) override
    {
        return {};
    }
    void Log(std::string_view) override {}
    std::vector<Client::Protocol::Outgoing> sent;
};
class LetterMeasure final : public Client::Render::Text::ITextMeasure
{
public:
    Client::Render::Text::TextExtent Measure(Client::Render::Text::FontKind, std::string_view text) const override
    {
        return {static_cast<int>(text.size()) * 6, 13};
    }
};
Client::Data::Text::TextTable Texts()
{
    std::map<std::size_t, std::string> texts;
    for (std::size_t id = 0; id < Client::Data::Text::TextCount; ++id)
        texts[id] = "texto " + std::to_string(id);
    texts[1565] = "recuperación %d"; // 0x772fbd8, argumento de 0x661050.
    return Client::Tests::MakeTextTable(texts);
}

class CastleWindowsTest : public ::testing::Test
{
protected:
    Client::Scenes::GameUiContext Context(std::int32_t x = 0, std::int32_t y = 0)
    {
        return {.world = *m_world,
                .random = m_random,
                .input = m_input,
                .mouseX = x,
                .mouseY = y,
                .measure = &m_measure,
                .texts = &m_texts,
                .requests = &m_requests,
                .systemTime = 1234};
    }
    void Release(Client::UI::OldButton& button)
    {
        button.base.x = 10;
        button.base.y = 10;
        m_world->Runtime().ui.pressedButton = button.base.id;
        m_world->Runtime().movement.releasedClick = true;
    }
    void ExpectRandoms(int count)
    {
        Client::Render::Models::CrtRandom expected(4242);
        for (int i = 0; i < count; ++i)
            static_cast<void>(expected.Next());
        EXPECT_EQ(m_random.State(), expected.State());
    }
    Client::Core::Expected<void, std::string> Draw(Client::App::PixelSize screen = {640, 480})
    {
        auto& runtime = m_world->Runtime();
        auto& hero = m_world->Actors()[runtime.hero];
        m_sprites.Begin(screen);
        Client::Scenes::GameHudContext context{.world = *m_world,
                                               .random = m_random,
                                               .ownRecordDepth = runtime.ownRecordDepth,
                                               .cellXDepth = hero.cellXAccesses,
                                               .cellYDepth = hero.cellYAccesses,
                                               .hud = runtime.hud,
                                               .crywolfSandOverlay = runtime.crywolf.sandOverlay,
                                               .sound = runtime.lastSoundId,
                                               .sprites = m_sprites,
                                               .text = m_text,
                                               .texts = &m_texts};
        return Client::Scenes::DrawTheCastleWindows(context);
    }
    const Client::Render::DrawCommand* QuadAt(float x, float y) const
    {
        const auto screen = m_sprites.Screen();
        const float pixelX = Client::Render::ScaleX(x, screen);
        // FanCorners conserva las inversas float nativas incluso en 640 × 480: no es simplemente H − y.
        const float pixelTop = static_cast<float>(static_cast<double>(screen.height) -
                                                  static_cast<double>(Client::Render::ScaleY(y, screen)));
        const auto vertices = m_sprites.Vertices();
        for (const auto& command : m_sprites.Commands())
            for (std::size_t i = command.firstVertex; i < command.firstVertex + command.vertexCount; i += 6)
                if (vertices[i].x == pixelX && vertices[i].y == pixelTop)
                    return &command;
        return nullptr;
    }
    void ExpectTextRow(std::int32_t y, bool alpha)
    {
        std::size_t count = 0;
        for (const auto& text : m_text.texts)
            if (text.y == y)
            {
                const auto* command = QuadAt(static_cast<float>(text.x), static_cast<float>(text.y));
                ASSERT_NE(command, nullptr) << text.text;
                EXPECT_EQ(command->texture, Client::Tests::TextMarkerSlot) << text.text;
                if (alpha)
                {
                    EXPECT_EQ(command->blend, Client::Render::BlendMode::Alpha) << text.text;
                    EXPECT_TRUE(command->alphaTest) << text.text;
                }
                ++count;
            }
        EXPECT_GT(count, 0u) << y;
    }
    void ExpectTexturedAlphaQuad(float x, float y, std::uint16_t slot)
    {
        const auto* command = QuadAt(x, y);
        ASSERT_NE(command, nullptr);
        EXPECT_EQ(command->texture, Client::Render::TextureSlot{slot});
        EXPECT_EQ(command->blend, Client::Render::BlendMode::Alpha);
        EXPECT_TRUE(command->alphaTest);
    }
    std::unique_ptr<Client::Scenes::WorldState> m_world = std::make_unique<Client::Scenes::WorldState>();
    Client::Render::Models::CrtRandom m_random{4242};
    Client::Input::InputState m_input;
    Requests m_requests;
    LetterMeasure m_measure;
    Client::Data::Text::TextTable m_texts = Texts();
    Client::Render::SpriteBatch m_sprites;
    Client::Tests::RecordingTextDrawer m_text{m_sprites};
};

TEST_F(CastleWindowsTest, RegistrationTabTwoRequestsOnlyTheNativeStates)
{
    auto& window = m_world->Runtime().castleSiege.registration;
    window.x = 450;
    window.button.base.state = 6;
    for (const std::int32_t state : {1, 2, 3, 5, 6, 9, -1})
    {
        window.tab = 0;
        window.state = state;
        m_requests.sent.clear();
        m_world->Runtime().movement.leftHeld = true;
        const auto stepped = Client::Scenes::StepTheCastleWindow(Context(580, 95), Client::UI::CastleSiegePanel);
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(window.tab, 2);
        EXPECT_TRUE(m_world->Runtime().movement.leftHeld);
        const bool request = state == 1 || state == 3 || state == 5 || state == 6;
        ASSERT_EQ(m_requests.sent.size(), request ? 1u : 0u);
        if (request)
        {
            EXPECT_TRUE(SameBytes(m_requests.sent[0].packet.Bytes(), Bytes({0xc1, 3, state < 4 ? 0xb4 : 0xb5})));
        }
        const auto repeated = Client::Scenes::StepTheCastleWindow(Context(580, 95), Client::UI::CastleSiegePanel);
        ASSERT_TRUE(repeated.HasValue()) << repeated.Error();
        EXPECT_EQ(m_requests.sent.size(), request ? 1u : 0u);
    }
    ExpectRandoms(0);
}

TEST_F(CastleWindowsTest, HuntEntryWithExactlyTheFeeIsDeniedButOneMoreIsAccepted)
{
    auto& window = m_world->Runtime().castleSiege.huntZone;
    window.mode = 1;
    window.allowed = 1;
    window.fee = 100;
    m_world->Runtime().panels.delegated.secondCastleSiege = 1;
    m_world->OwnCharacter().zen = 100;
    Release(window.enter);
    auto stepped = Client::Scenes::StepTheCastleWindow(Context(11, 11), Client::UI::SecondCastleSiegePanel);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_TRUE(m_requests.sent.empty());
    EXPECT_EQ(m_world->Runtime().panels.delegated.panelDialog.lineCount, 2);
    EXPECT_EQ(m_world->Runtime().panels.delegated.secondCastleSiege, 1);
    m_world->OwnCharacter().zen = 101;
    Release(window.enter);
    stepped = Client::Scenes::StepTheCastleWindow(Context(11, 11), Client::UI::SecondCastleSiegePanel);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    ASSERT_EQ(m_requests.sent.size(), 1u);
    EXPECT_TRUE(SameBytes(Client::Network::Crypto::Xor32Decode(m_requests.sent[0].packet).Bytes(),
                          Bytes({0xc1, 8, 0xb9, 5, 100, 0, 0, 0})));
    EXPECT_EQ(m_world->Runtime().panels.delegated.secondCastleSiege, 1);
    ExpectRandoms(3848 * 2);
}

TEST_F(CastleWindowsTest, TheOtherHuntModeEntersBeforeClosingWithoutReadingMoney)
{
    auto& window = m_world->Runtime().castleSiege.huntZone;
    window.mode = 2;
    window.fee = 0x01020304;
    m_world->Runtime().panels.delegated.secondCastleSiege = 1;
    Release(window.enter);
    const auto stepped = Client::Scenes::StepTheCastleWindow(Context(11, 11), Client::UI::SecondCastleSiegePanel);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    ASSERT_EQ(m_requests.sent.size(), 2u);
    EXPECT_TRUE(SameBytes(Client::Network::Crypto::Xor32Decode(m_requests.sent[0].packet).Bytes(),
                          Bytes({0xc1, 8, 0xb9, 5, 4, 3, 2, 1})));
    EXPECT_TRUE(SameBytes(m_requests.sent[1].packet.Bytes(), Bytes({0xc1, 3, 0x31})));
    EXPECT_EQ(m_world->Runtime().panels.delegated.secondCastleSiege, 0);
    ExpectRandoms(0);
}

TEST_F(CastleWindowsTest, RegistrationChecksTheUnsignedLevelInsideItsLogicalAccess)
{
    auto& window = m_world->Runtime().castleSiege.registration;
    window.tab = 1;
    window.state = 1;
    m_world->Actors()[0].guildStatus = 0x80;
    m_world->OwnCharacter().level = 199;
    Release(window.button);
    auto stepped = Client::Scenes::StepTheCastleWindow(Context(11, 11), Client::UI::CastleSiegePanel);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_TRUE(m_requests.sent.empty());
    EXPECT_EQ(m_world->Runtime().panels.delegated.panelDialog.lineCount, 2);
    m_world->OwnCharacter().level = 200;
    Release(window.button);
    stepped = Client::Scenes::StepTheCastleWindow(Context(11, 11), Client::UI::CastleSiegePanel);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    ASSERT_EQ(m_requests.sent.size(), 1u);
    EXPECT_TRUE(
        SameBytes(Client::Network::Crypto::Xor32Decode(m_requests.sent[0].packet).Bytes(), Bytes({0xc1, 4, 0xb2, 1})));
    ExpectRandoms(3848 * 2);
}

TEST_F(CastleWindowsTest, SeniorModalBlocksTabsAndCloseAfterMarkingHover)
{
    auto& runtime = m_world->Runtime();
    runtime.panels.delegated.castleSiegeList = 1;
    runtime.panels.delegated.panelDialog.opened = 5;
    runtime.castleSiege.senior.x = 450;
    runtime.movement.leftHeld = true;
    const auto stepped = Client::Scenes::StepTheCastleWindow(Context(590, 55), Client::UI::CastleSiegeListPanel);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(runtime.panels.delegated.castleSiegeListMode, 0);
    EXPECT_EQ(runtime.ui.mouseOnUi, 1);
    EXPECT_TRUE(m_requests.sent.empty());
    ExpectRandoms(0);
}

TEST_F(CastleWindowsTest, RegistrationDrawCountsMarksAndUpdatesTheButtonDisabledState)
{
    auto& runtime = m_world->Runtime();
    runtime.panels.delegated.castleSiege = 1;
    auto& window = runtime.castleSiege.registration;
    window.tab = 1;
    window.state = 3;
    window.registered = 1;
    auto& mark = m_world->Inventory().cells[7];
    mark.id = 0x1c15;
    mark.scalar1Low = 0x18;
    mark.occupied = 1;
    mark.durability = 2;
    const auto drawn = Draw();
    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    EXPECT_EQ(window.x, 450);
    EXPECT_EQ(window.right, 770);
    EXPECT_EQ(window.button.base.state, 0);
    EXPECT_EQ(window.button.base.x, 520);
    EXPECT_EQ(window.button.base.y, 199);
    ExpectRandoms(3848);
}

TEST_F(CastleWindowsTest, SeniorTaxArrowsModifyOnlyOnTheReleasedSignalDuringDraw)
{
    auto& runtime = m_world->Runtime();
    runtime.panels.delegated.castleSiegeList = 1;
    runtime.panels.delegated.castleSiegeListMode = 2;
    runtime.movement.pendingClick = true;
    runtime.castleSiege.data.SetU32(Client::UI::ChaosTaxAt, 2);
    // Draw usa su propio mouse, igual al de GameScene. Esta prueba pasa la posición en el contexto de abajo.
    m_sprites.Begin({640, 480});
    auto& hero = m_world->Actors()[0];
    Client::Scenes::GameHudContext context{.world = *m_world,
                                           .random = m_random,
                                           .ownRecordDepth = runtime.ownRecordDepth,
                                           .cellXDepth = hero.cellXAccesses,
                                           .cellYDepth = hero.cellYAccesses,
                                           .hud = runtime.hud,
                                           .crywolfSandOverlay = runtime.crywolf.sandOverlay,
                                           .sound = runtime.lastSoundId,
                                           .sprites = m_sprites,
                                           .text = m_text,
                                           .texts = &m_texts,
                                           .mouse = {608, 88}};
    auto drawn = Client::Scenes::DrawTheCastleWindows(context);
    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    EXPECT_EQ(runtime.castleSiege.data.U32(Client::UI::ChaosTaxAt), 2u);
    runtime.movement.releasedClick = true;
    drawn = Client::Scenes::DrawTheCastleWindows(context);
    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    EXPECT_EQ(runtime.castleSiege.data.U32(Client::UI::ChaosTaxAt), 3u);
    EXPECT_FALSE(runtime.movement.releasedClick);
    ExpectRandoms(0);
}

TEST_F(CastleWindowsTest, SeniorMoneyRunsFromItsButtonThroughTheSharedEditAndEnterBeforeCleanup)
{
    auto& runtime = m_world->Runtime();
    runtime.panels.delegated.castleSiegeList = 1;
    runtime.panels.delegated.castleSiegeListMode = 2;
    Release(runtime.castleSiege.senior.buttons[5]);
    const auto stepped = Client::Scenes::StepTheCastleWindow(Context(11, 11), Client::UI::CastleSiegeListPanel);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    auto& dialog = runtime.panels.delegated.panelDialog;
    ASSERT_EQ(dialog.owner, Client::UI::PanelDialogOwner::CastleMoney);
    ASSERT_EQ(dialog.flags, 0x12u);
    ASSERT_EQ(dialog.opened, 1234u);
    m_sprites.Begin({640, 480});
    auto& hero = m_world->Actors()[0];
    Client::Scenes::GameHudContext hud{.world = *m_world,
                                       .random = m_random,
                                       .ownRecordDepth = runtime.ownRecordDepth,
                                       .cellXDepth = hero.cellXAccesses,
                                       .cellYDepth = hero.cellYAccesses,
                                       .hud = runtime.hud,
                                       .crywolfSandOverlay = runtime.crywolf.sandOverlay,
                                       .sound = runtime.lastSoundId,
                                       .sprites = m_sprites,
                                       .text = m_text,
                                       .texts = &m_texts};
    const auto drawn = Client::Scenes::DrawThePanelDialog(hud, dialog);
    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    EXPECT_EQ(runtime.chat.input.focus, Client::UI::ChatFocus::Dialog);
    EXPECT_EQ(runtime.chat.input.dialogEdit.base.x, 290);
    EXPECT_EQ(runtime.chat.input.dialogEdit.base.width, 70);
    EXPECT_EQ(runtime.chat.input.dialogEdit.filter, 1);
    runtime.chat.input.dialogEdit.text.SetText("16909060");
    const Client::Scenes::GameChatContext chat{
        .world = *m_world, .random = m_random, .send = [this](Client::Protocol::Outgoing packet) {
            m_requests.sent.push_back(std::move(packet));
        }};
    const auto entered = Client::Scenes::PressEnterInTheGame(chat);
    ASSERT_TRUE(entered.HasValue()) << entered.Error();
    ASSERT_EQ(m_requests.sent.size(), 1u);
    EXPECT_TRUE(SameBytes(Client::Network::Crypto::Xor32Decode(m_requests.sent[0].packet).Bytes(),
                          Bytes({0xc1, 8, 0xb2, 0x10, 4, 3, 2, 1})));
    EXPECT_EQ(dialog.opened, 0u);
    EXPECT_TRUE(runtime.chat.input.dialogEdit.text.Value().empty());
    EXPECT_EQ(runtime.chat.input.dialogEdit.base.state, Client::UI::HiddenWindow);
    EXPECT_EQ(runtime.chat.input.focus, Client::UI::ChatFocus::Game);
    ExpectRandoms(0);
}
TEST_F(CastleWindowsTest, RegisteringAMarkUsesTheFirstMatchingSlotEvenWithZeroDurabilityAndWithoutMasterStatus)
{
    auto& runtime = m_world->Runtime();
    auto& window = runtime.castleSiege.registration;
    window.tab = 1;
    window.state = 3;
    auto& mark = m_world->Inventory().cells[3];
    mark.occupied = 1;
    mark.id = 0x1c15;
    mark.scalar1Low = 0x18;
    mark.durability = 0;
    m_world->Actors()[0].guildStatus = 0;
    Release(window.button);
    const auto stepped = Client::Scenes::StepTheCastleWindow(Context(11, 11), Client::UI::CastleSiegePanel);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    ASSERT_EQ(m_requests.sent.size(), 1u);
    EXPECT_TRUE(SameBytes(Client::Network::Crypto::Xor32Decode(m_requests.sent[0].packet).Bytes(),
                          Bytes({0xc1, 5, 0xb2, 4, 3})));
    ExpectRandoms(3848);
}

TEST_F(CastleWindowsTest, SeniorCraftFindsTheLastFourByFiveRectangleUsingTypeInsteadOfOccupied)
{
    auto& runtime = m_world->Runtime();
    runtime.panels.delegated.castleSiegeListMode = 3;
    m_world->Actors()[0].word1f2 = 0;
    m_world->Actors()[0].guildStatus = 0x80;
    for (auto& cell : m_world->Inventory().cells)
    {
        cell.id = 0;
        cell.occupied = 0;
    }
    for (std::size_t y = 3; y < 8; ++y)
        for (std::size_t x = 4; x < 8; ++x)
        {
            auto& cell = m_world->Inventory().cells[y * 8 + x];
            cell.id = 0xffff;
            cell.occupied = 1;
        }
    Release(runtime.castleSiege.senior.buttons[6]);
    auto stepped = Client::Scenes::StepTheCastleWindow(Context(11, 11), Client::UI::CastleSiegeListPanel);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    ASSERT_EQ(m_requests.sent.size(), 1u);
    EXPECT_TRUE(SameBytes(Client::Network::Crypto::Xor32Decode(m_requests.sent[0].packet).Bytes(),
                          Bytes({0xc1, 4, 0x86, 0x12})));
    m_world->Inventory().cells[63].id = 0;
    Release(runtime.castleSiege.senior.buttons[6]);
    stepped = Client::Scenes::StepTheCastleWindow(Context(11, 11), Client::UI::CastleSiegeListPanel);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(m_requests.sent.size(), 1u);
    EXPECT_FALSE(runtime.chat.log.entries.empty());
    ExpectRandoms(0);
}

TEST_F(CastleWindowsTest, SiegeListDrawFiltersGuildsButTheMouseTogglesTheUnfilteredNativeRow)
{
    auto& runtime = m_world->Runtime();
    runtime.panels.delegated.castleSiege = 1;
    m_world->Actors()[0].word1f2 = 0xffff;
    // Q-C1281: la alianza de la entrada −1 coincide con el nombre del miembro74, no se rechaza el índice.
    runtime.guild.members[Client::Scenes::MemberBeforeTheTable].name = {'H', 'E', 'R', 'O'};
    auto& window = runtime.castleSiege.registration;
    window.tab = 2;
    window.state = 3;
    window.siegeGuilds.push_back({.name = {'O', 'T', 'H', 'E', 'R'}});
    window.siegeGuilds.push_back({.name = {'H', 'E', 'R', 'O'}});
    auto drawn = Draw();
    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    const auto other = std::ranges::find(m_text.texts, "OTHER", &Client::Tests::RecordedText::text);
    const auto hero = std::ranges::find(m_text.texts, "HERO", &Client::Tests::RecordedText::text);
    EXPECT_EQ(other, m_text.texts.end());
    ASSERT_NE(hero, m_text.texts.end());
    EXPECT_EQ(hero->y, 141);
    runtime.movement.pendingClick = true;
    const auto stepped = Client::Scenes::StepTheCastleWindow(Context(480, 139), Client::UI::CastleSiegePanel);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(window.siegeGuilds[0].toggled, 1);
    EXPECT_EQ(window.siegeGuilds[1].toggled, 0);
    EXPECT_EQ(window.siegeGuildList.selection, 1);
    EXPECT_FALSE(runtime.movement.pendingClick);
    EXPECT_TRUE(m_requests.sent.empty());
    ExpectRandoms(0);
}

// 470540..470627: nombre en x+6 con modo1; marcas/estado/rango terminan en x+89/+124/+144 con modo4.
// A 800px, 559−12/1,25 se trunca a549: no es 559−trunc(12/1,25).
TEST_F(CastleWindowsTest, SiegeGuildColumnsEndAtTheNativeAnchorsAtBothScreenWidths)
{
    auto& runtime = m_world->Runtime();
    runtime.panels.delegated.castleSiege = 1;
    m_world->Actors()[0].word1f2 = 0xffff;
    runtime.guild.members[Client::Scenes::MemberBeforeTheTable].name = {'G', 'U', 'I', 'L', 'D'};
    auto& window = runtime.castleSiege.registration;
    window.tab = 2;
    window.state = 3;
    window.siegeGuilds.push_back({.name = {'G', 'U', 'I', 'L', 'D'}, .marks = 0xffffffffu, .first = 1, .second = 255});
    for (const int width : {640, 800})
    {
        SCOPED_TRACE(width);
        m_text.texts.clear();
        const auto drawn = Draw({width, 480});
        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
        std::vector<Client::Tests::RecordedText> row;
        for (const auto& text : m_text.texts)
            if (text.y == 128)
                row.push_back(text);
        ASSERT_EQ(row.size(), 4u);
        EXPECT_EQ(row[0].text, "GUILD");
        EXPECT_EQ(row[0].x, 476);
        EXPECT_EQ(row[1].text, "-1");
        EXPECT_EQ(row[1].x, width == 640 ? 547 : 549);
        EXPECT_EQ(row[2].text, "texto 1531");
        EXPECT_EQ(row[2].x, width == 640 ? 534 : 546);
        EXPECT_EQ(row[3].text, "255");
        EXPECT_EQ(row[3].x, width == 640 ? 596 : 599);
    }
    ExpectRandoms(0);
}

// 47139b..471465: nombre izquierdo; los dos estados usan modo4 con anclas x+104 y x+141.
TEST_F(CastleWindowsTest, RegisteredGuildColumnsEndAtTheirOwnNativeAnchors)
{
    auto& runtime = m_world->Runtime();
    runtime.panels.delegated.castleSiege = 1;
    auto& window = runtime.castleSiege.registration;
    window.tab = 2;
    window.state = 5;
    window.registeredGuilds.push_back({.name = {'W', 'I', 'N', 'N', 'E', 'R'}, .first = 1, .second = 0});
    for (const int width : {640, 800})
    {
        SCOPED_TRACE(width);
        m_text.texts.clear();
        const auto drawn = Draw({width, 480});
        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
        std::vector<Client::Tests::RecordedText> row;
        for (const auto& text : m_text.texts)
            if (text.y == 128)
                row.push_back(text);
        ASSERT_EQ(row.size(), 3u);
        EXPECT_EQ(row[0].text, "WINNER");
        EXPECT_EQ(row[0].x, 476);
        EXPECT_EQ(row[1].text, "texto 1606");
        EXPECT_EQ(row[1].x, width == 640 ? 514 : 526);
        EXPECT_EQ(row[2].text, "texto 1608");
        EXPECT_EQ(row[2].x, width == 640 ? 551 : 563);
    }
    ExpectRandoms(0);
}

TEST_F(CastleWindowsTest, RegistrationRestoresTextureAfterItsTabUnderlinesBeforeDatesAndClose)
{
    auto& runtime = m_world->Runtime();
    runtime.panels.delegated.castleSiege = 1;
    runtime.castleSiege.registration.state = 1;
    runtime.castleSiege.registration.tab = 0;
    const auto drawn = Draw();
    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    ExpectTextRow(125, false);
    ExpectTextRow(139, false);
    const auto* close = QuadAt(475, 395);
    ASSERT_NE(close, nullptr);
    EXPECT_EQ(close->texture, Client::Render::TextureSlot{0x118});
    ExpectRandoms(0);
}

TEST_F(CastleWindowsTest, HuntAdminRestoresAlphaAfterTheAmountBackgroundAndBothButtonGroups)
{
    auto& runtime = m_world->Runtime();
    runtime.panels.delegated.secondCastleSiege = 1;
    runtime.castleSiege.huntZone.mode = 3;
    const auto drawn = Draw();
    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    ExpectTextRow(163, true);
    ExpectTextRow(195, true);
    ExpectTexturedAlphaQuad(570, 185, 9);
    ExpectTexturedAlphaQuad(570, 206, 9);
    ExpectTextRow(253, true);
    ExpectTextRow(266, true);
    ExpectTextRow(279, true);
    ExpectRandoms(0);
}

TEST_F(CastleWindowsTest, SeniorTaxesRestoreAlphaForBothInstructionsAndTheBalanceAfterTheirButtons)
{
    auto& runtime = m_world->Runtime();
    runtime.panels.delegated.castleSiegeList = 1;
    runtime.panels.delegated.castleSiegeListMode = 2;
    const auto drawn = Draw();
    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    ExpectTextRow(201, true);
    ExpectTexturedAlphaQuad(450, 281, 0x117);
    ExpectTextRow(301, true);
    ExpectTextRow(347, true);
    ExpectTexturedAlphaQuad(475, 395, 0x118);
    ExpectRandoms(0);
}

TEST_F(CastleWindowsTest, TheStatueRecoveryGainKeepsTheTypeOfTheGateRecordReceivedThroughItsValidAlias)
{
    namespace Castle = Client::Protocol::CastleSiege;
    Castle::NpcRecord record{};
    const auto put = [&](std::size_t at, std::uint32_t value)
    {
        for (std::size_t i = 0; i < 4; ++i)
            record[at + i] = static_cast<std::byte>(value >> (i * 8));
    };
    put(Client::UI::RecordType, Client::UI::GateType);
    put(Client::UI::RecordIndex, 0xfffffffdu);
    put(Client::UI::RecordMaxLife, 500000);
    put(Client::UI::RecordLife, 9);
    record[Client::UI::RecordAlive] = std::byte{1};
    Client::Core::StderrSink sink;
    Client::Core::Log log(sink, Client::Core::LogLevel::Warning);
    const Client::Scenes::CastleReceiveContext receive{.world = *m_world, .random = m_random, .log = log};
    const auto applied =
        Client::Scenes::ApplyCastleSiegePacket(Castle::NpcList{.result = 1, .records = {record}}, receive);
    ASSERT_TRUE(applied.HasValue()) << applied.Error();
    auto& runtime = m_world->Runtime();
    ASSERT_EQ(runtime.castleSiege.data.U32(Client::UI::FirstStatueAt), Client::UI::GateType);
    runtime.panels.delegated.castleSiegeList = 1;
    runtime.panels.delegated.castleSiegeListMode = 1;
    const auto drawn = Draw();
    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    EXPECT_TRUE(std::ranges::any_of(m_text.texts, [](const auto& text) { return text.text == "recuperación 0"; }));
    ExpectRandoms(0);
}

} // namespace
