#include "Network/Xor32Packet.h"
#include "CastleSiege/SiegeBattleUi.h"
#include "Protocol/Player/OptionData.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Tables/SkillTable.h"
#include "Input/InputState.h"
#include "Input/Key.h"
#include "Network/Packet.h"
#include "Protocol/Outgoing.h"
#include "Models/LogicalAccess.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Kernel/ActorState.h"
#include "Hud/GameHud.h"
#include "Hud/GameUi.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Ui/PanelRegistry.h"
#include "Ui/HudPrimitives.h"
#include "CastleSiege/SiegePanelState.h"

#include "support/Bytes.h"
#include "support/RecordingTextDrawer.h"
#include "support/SameBytes.h"
#include "support/TableFiles.h"
#include "support/UiInput.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
using Client::Core::Expected;
using Client::Crt::CrtRandom;
using Client::Protocol::Outgoing;
using Client::Scenes::GameHudContext;
using Client::Scenes::GameUiContext;
using Client::Tests::Bytes;
using Client::Tests::SameBytes;
namespace Siege = Client::Scenes;

class Requests final : public Client::Scenes::IGameUiRequests
{
public:
    void SendOptionData(const Client::Protocol::Player::OptionBody&) override {}
    void RequestLogout(std::uint8_t) override {}
    void RequestPartyList() override {}
    void SendGuildPacket(Outgoing) override {}
    void SendGamePacket(Outgoing packet) override
    {
        packets.push_back(std::move(packet));
    }
    void SendWindowClose() override {}
    void SendSetQuest(std::uint8_t, std::uint8_t) override {}
    void SendFriendPacket(Outgoing) override {}
    Expected<void, std::string> SaveMacros(std::string_view) override
    {
        return {};
    }
    void Log(std::string_view) override {}
    std::vector<Outgoing> packets{};
};

class SiegeUiTest : public testing::Test
{
protected:
    GameUiContext StepContext(std::int32_t x = 0, std::int32_t y = 0)
    {
        return {.world = *world,
                .random = random,
                .input = input,
                .mouseX = x,
                .mouseY = y,
                .requests = &requests,
                .skills = &skills};
    }
    GameHudContext DrawContext(bool projecting = false)
    {
        sprites.Begin({640, 480});
        auto& runtime = world->Runtime();
        auto& hero = world->Hero();
        return {.world = *world,
                .random = random,
                .ownRecordDepth = runtime.ownRecordDepth,
                .cellXDepth = hero.cellXAccesses,
                .cellYDepth = hero.cellYAccesses,
                .hud = runtime.hud,
                .crywolfSandOverlay = runtime.crywolf.sandOverlay,
                .sound = runtime.lastSoundId,
                .projecting = projecting,
                .sprites = sprites,
                .text = text,
                .skills = &skills,
                .mouse = {-1, -1}};
    }
    void Click()
    {
        world->Runtime().movement.pendingClick = true;
        world->Runtime().movement.leftHeld = true;
    }
    void BattleRole()
    {
        auto& runtime = world->Runtime();
        auto& hero = world->Hero();
        runtime.castleSiege.siegeActive = true;
        hero.byte360 = 6;
        hero.guildStatus = 0x80;
        hero.word1f2 = 0;
        hero.byte371 = 0x46;
        hero.byte373 = 0xff;
    }
    void Cost(std::uint8_t cost)
    {
        std::vector<std::byte> plain(Client::Tables::SkillTableSize);
        plain[0x46 * 60 + 0x33] = static_cast<std::byte>(cost);
        skills = Client::Tables::SkillTable(Client::Tests::EncryptRecords(std::move(plain), 60));
    }
    void ExpectPacket(std::size_t index, const std::vector<std::byte>& bytes, bool xor32 = true)
    {
        ASSERT_LT(index, requests.packets.size());
        auto packet = requests.packets[index].packet;
        if (xor32)
            packet = Client::Network::Xor32Decode(std::move(packet));
        EXPECT_TRUE(SameBytes(packet.Bytes(), bytes));
    }
    std::unique_ptr<Client::Kernel::WorldState> world = std::make_unique<Client::Kernel::WorldState>();
    CrtRandom random{314159};
    Client::Input::InputState input{};
    Requests requests{};
    Client::Tables::SkillTable skills{
        Client::Tests::EncryptRecords(std::vector<std::byte>(Client::Tables::SkillTableSize), 60)};
    Client::Render::SpriteBatch sprites{};
    Client::Tests::RecordingTextDrawer text{sprites};
};

CrtRandom After(CrtRandom random, std::int32_t count)
{
    for (std::int32_t i = 0; i < count; ++i)
        static_cast<void>(random.Next());
    return random;
}

// 0x412140: usa el par global que escribe 0x412120, no el puesto actual del actor.
TEST_F(SiegeUiTest, TheGuildPostSelectsAndPreservesTheSamePanelObject)
{
    auto& runtime = world->Runtime();
    runtime.map = 30;
    runtime.guild.heroGuildIndex = 0;
    constexpr std::array<std::array<std::uint8_t, 2>, 5> posts{{{0xff, 0}, {0, 1}, {0x20, 1}, {0x40, 1}, {0x80, 2}}};
    for (const auto& [post, kind] : posts)
    {
        runtime.guild.heroGuildStatus = post;
        ASSERT_TRUE(Siege::RefreshTheSiegePanel(runtime).HasValue());
        ASSERT_EQ(runtime.castleSiege.siegePanelKind, kind);
        runtime.castleSiege.siegePanel.page = 2;
        runtime.castleSiege.siegePanel.visible = 0;
        runtime.castleSiege.siegePanel.alpha = 0.6f;
        ASSERT_TRUE(Siege::RefreshTheSiegePanel(runtime).HasValue());
        EXPECT_EQ(runtime.castleSiege.siegePanel.page, 2);
        EXPECT_EQ(runtime.castleSiege.siegePanel.visible, 0);
        EXPECT_FLOAT_EQ(runtime.castleSiege.siegePanel.alpha, 0.6f);
    }
    runtime.guild.heroGuildStatus = 0x12;
    ASSERT_TRUE(Siege::RefreshTheSiegePanel(runtime).HasValue());
    EXPECT_EQ(runtime.castleSiege.siegePanelKind, 2);
    runtime.map = 2;
    ASSERT_TRUE(Siege::RefreshTheSiegePanel(runtime).HasValue());
    EXPECT_FALSE(runtime.castleSiege.siegePanelKind.has_value());
}

TEST_F(SiegeUiTest, OnlyTheGuildMasterWithoutUnionOrLeadingItsUnionGetsTheCommanderPanel)
{
    auto& runtime = world->Runtime();
    runtime.map = 30;
    runtime.guild.heroGuildIndex = 0;
    runtime.guild.heroGuildStatus = 0x80;
    auto& guild = runtime.guild.table[0];
    std::ranges::copy(std::string_view("Robles"), guild.guildName.begin());
    std::ranges::copy(std::string_view("Pinos"), guild.unionName.begin());
    ASSERT_TRUE(Siege::RefreshTheSiegePanel(runtime).HasValue());
    EXPECT_EQ(runtime.castleSiege.siegePanelKind, 1);
    guild.unionName = guild.guildName;
    ASSERT_TRUE(Siege::RefreshTheSiegePanel(runtime).HasValue());
    EXPECT_EQ(runtime.castleSiege.siegePanelKind, 2);
    EXPECT_GE(runtime.castleSiege.siegePanel.members.capacity(), 1600u);
}

TEST_F(SiegeUiTest, TheStatusTableAndTheMemberVectorHaveIndependentStorage)
{
    auto& runtime = world->Runtime();
    runtime.castleSiege.siegePanelKind = std::uint8_t{2};
    Siege::AppendSiegePanelMember(runtime, 255, 11, 12);
    Siege::AppendSiegePanelMember(runtime, 0, 13, 14);
    ASSERT_TRUE(Siege::ReceiveSiegePanelStatus(runtime, {2, 0, 255, 7}).HasValue());
    EXPECT_EQ(runtime.castleSiege.siegePanel.membersHidden, 0);
    ASSERT_EQ(runtime.castleSiege.siegePanel.members.size(), 2u);
    EXPECT_EQ(runtime.castleSiege.siegePanel.members[0], (std::array<std::uint8_t, 3>{255, 11, 12}));
    EXPECT_EQ(runtime.castleSiege.siegePanel.status[2], (std::array<std::uint8_t, 5>{2, 0, 255, 7, 100}));
    runtime.castleSiege.siegePanel.status[2][4] = 1;
    ASSERT_TRUE(Siege::ReceiveSiegePanelStatus(runtime, {2, 4, 5, 0}).HasValue());
    EXPECT_EQ(runtime.castleSiege.siegePanel.status[2], (std::array<std::uint8_t, 5>{2, 4, 5, 0, 100}));
    EXPECT_EQ(runtime.castleSiege.siegePanel.status[1], (std::array<std::uint8_t, 5>{}));
    EXPECT_FALSE(Siege::ReceiveSiegePanelStatus(runtime, {7, 1, 2, 3}).HasValue());
    runtime.castleSiege.siegePanelKind = std::uint8_t{0};
    ASSERT_TRUE(Siege::ReceiveSiegePanelStatus(runtime, {255, 1, 2, 3}).HasValue());
    Siege::AppendSiegePanelMember(runtime, 1, 2, 3);
    EXPECT_EQ(runtime.castleSiege.siegePanel.members.size(), 2u);
}

TEST_F(SiegeUiTest, ResettingTheClockPreservesTheRawServerTimeAndTick)
{
    auto& runtime = world->Runtime();
    Siege::SetSiegeClock(runtime, 255, 254, 0xfffffff0u);
    runtime.castleSiege.siegeBattleUi.clock.blinkTime = 750.0f;
    Siege::ResetSiegeClock(runtime);
    const auto& clock = runtime.castleSiege.siegeBattleUi.clock;
    EXPECT_EQ(clock.hours, 255u);
    EXPECT_EQ(clock.minutes, 254u);
    EXPECT_EQ(clock.tick, 0xfffffff0u);
    EXPECT_EQ(clock.blink, 1);
    EXPECT_EQ(clock.x, 243);
    EXPECT_EQ(clock.y, 400);
    EXPECT_FLOAT_EQ(clock.blinkTime, 0.0f);
}

TEST_F(SiegeUiTest, TheWeaponChoiceAndConfirmationProduceTheLiteralB7Then31)
{
    auto& runtime = world->Runtime();
    runtime.panels.delegated.siegeWeapon = 1;
    runtime.castleSiege.siegeBattleUi.weaponKey = 0xabcd;
    Click();
    ASSERT_TRUE(Siege::StepTheSiegeControls(StepContext(528, 140)).HasValue());
    EXPECT_EQ(runtime.castleSiege.siegeBattleUi.weaponChoice, 2);
    EXPECT_FALSE(runtime.movement.pendingClick);
    EXPECT_TRUE(requests.packets.empty());
    Click();
    ASSERT_TRUE(Siege::StepTheSiegeControls(StepContext(620, 378)).HasValue());
    ASSERT_EQ(requests.packets.size(), 2u);
    ExpectPacket(0, Bytes({0xc1, 7, 0xb7, 1, 0xab, 0xcd, 2}));
    ExpectPacket(1, Bytes({0xc1, 3, 0x31}), false);
    EXPECT_EQ(runtime.panels.delegated.siegeWeapon, 0);
    EXPECT_EQ(runtime.castleSiege.siegeBattleUi.weaponChoice, 0);
    // 0x44e444..0x44e6a8: confirmar no consume el clic, a diferencia de escoger y cancelar.
    EXPECT_TRUE(runtime.movement.pendingClick);
}

TEST_F(SiegeUiTest, CancelTheWeaponConsumesTheClickAndSendsOnly31)
{
    auto& runtime = world->Runtime();
    runtime.panels.delegated.siegeWeapon = 2;
    runtime.castleSiege.siegeBattleUi.weaponChoice = 3;
    Click();
    ASSERT_TRUE(Siege::StepTheSiegeControls(StepContext(470, 395)).HasValue());
    ASSERT_EQ(requests.packets.size(), 1u);
    ExpectPacket(0, Bytes({0xc1, 3, 0x31}), false);
    EXPECT_EQ(runtime.panels.delegated.siegeWeapon, 0);
    EXPECT_FALSE(runtime.movement.pendingClick);
}

TEST_F(SiegeUiTest, OpeningTheGateReadsTheActorAndTheWideActionRectangleUsesStrictRightBorder)
{
    auto& runtime = world->Runtime();
    world->Actors()[5].alive = true;
    world->Actors()[5].key = 0x1234;
    world->Actors()[5].flags78 = 0x20000;
    Siege::OpenTheSiegeGate(*world, 0x1234);
    EXPECT_EQ(runtime.castleSiege.siegeBattleUi.gateState, 0x20000u);
    Click();
    ASSERT_TRUE(Siege::StepTheSiegeControls(StepContext(683, 325)).HasValue());
    EXPECT_TRUE(requests.packets.empty());
    ASSERT_TRUE(Siege::StepTheSiegeControls(StepContext(682, 325)).HasValue());
    ASSERT_EQ(requests.packets.size(), 2u);
    ExpectPacket(0, Bytes({0xc1, 7, 0xb2, 1, 0, 0x12, 0x34}));
    ExpectPacket(1, Bytes({0xc1, 3, 0x31}), false);
    EXPECT_FALSE(runtime.movement.pendingClick);
    EXPECT_EQ(runtime.castleSiege.siegeBattleUi.gateKey, 0);
    EXPECT_EQ(runtime.panels.delegated.siegeGate, 0);
}

TEST_F(SiegeUiTest, TheCommanderChoosesAnOrderThenSendsCoordinatesFromTheLastMapCrop)
{
    auto& runtime = world->Runtime();
    runtime.castleSiege.siegePanelKind = std::uint8_t{2};
    auto& panel = runtime.castleSiege.siegePanel;
    panel.page = 2;
    panel.offsetX = 12;
    panel.offsetY = 34;
    Click();
    ASSERT_TRUE(Siege::StepTheBattlePanel(StepContext(492, 308)).HasValue());
    EXPECT_EQ(panel.selected, 1);
    EXPECT_EQ(panel.command, 3);
    Click();
    ASSERT_TRUE(Siege::StepTheBattlePanel(StepContext(512, 308)).HasValue());
    EXPECT_EQ(panel.command, 0);
    Click();
    ASSERT_TRUE(Siege::StepTheBattlePanel(StepContext(550, 350)).HasValue());
    ASSERT_EQ(requests.packets.size(), 1u);
    ExpectPacket(0, Bytes({0xc1, 8, 0xb2, 0x1d, 1, 100, 96, 0}));
    EXPECT_EQ(panel.targetTtl, 100);
    EXPECT_EQ(panel.command, 3);
    EXPECT_EQ(panel.selected, 0);
    EXPECT_FALSE(runtime.movement.pendingClick);
}

TEST_F(SiegeUiTest, ARightClickOnTheCommanderRowCancelsWithoutConsumingTheRightClick)
{
    auto& runtime = world->Runtime();
    runtime.castleSiege.siegePanelKind = std::uint8_t{2};
    runtime.castleSiege.siegePanel.selected = 3;
    runtime.castleSiege.siegePanel.command = 0;
    runtime.ui.rightClick = true;
    ASSERT_TRUE(Siege::StepTheBattlePanel(StepContext(492, 308)).HasValue());
    EXPECT_EQ(runtime.castleSiege.siegePanel.selected, 0);
    EXPECT_EQ(runtime.castleSiege.siegePanel.command, 3);
    EXPECT_TRUE(runtime.ui.rightClick);
    EXPECT_TRUE(requests.packets.empty());
}

TEST_F(SiegeUiTest, WarAloneDoesNotReadTheSkillTableOrChangeTheWheel)
{
    auto& runtime = world->Runtime();
    runtime.castleSiege.siegeActive = true;
    runtime.ui.wheel = 120;
    const auto before = random.State();
    auto step = StepContext();
    step.skills = nullptr;
    ASSERT_TRUE(Siege::StepTheBattleSkill(step).HasValue());
    ASSERT_TRUE(Siege::StepTheBattleSkillChoice(step).HasValue());
    auto draw = DrawContext();
    draw.skills = nullptr;
    ASSERT_TRUE(Siege::DrawTheBattleSkill(draw).HasValue());
    EXPECT_EQ(runtime.ui.wheel, 120);
    EXPECT_EQ(random.State(), before);
    EXPECT_TRUE(sprites.Vertices().empty());
}

TEST_F(SiegeUiTest, ShiftTemporarilyUsesTheSiegeSkillAndInsufficientPointsDoNotRestoreIt)
{
    BattleRole();
    Cost(4);
    auto& hero = world->Hero();
    world->OwnCharacter().SetSkillAt(7, 0x46);
    hero.skillIndex = 3;
    hero.byte372 = 4;
    Client::Tests::PressKey(input, Client::Input::Key::ShiftLeft);
    CrtRandom expected = After(random, 60 + 3848);
    ASSERT_TRUE(Siege::StepTheBattleSkill(StepContext()).HasValue());
    EXPECT_EQ(hero.byte373, 3);
    EXPECT_EQ(hero.skillIndex, 7);
    EXPECT_EQ(random.State(), expected.State());
    Client::Tests::ReleaseKey(input, Client::Input::Key::ShiftLeft);
    hero.byte372 = 3;
    expected = After(random, 60);
    ASSERT_TRUE(Siege::StepTheBattleSkill(StepContext()).HasValue());
    EXPECT_EQ(hero.skillIndex, 7);
    EXPECT_EQ(hero.byte373, 3);
    EXPECT_EQ(random.State(), expected.State());
    hero.byte372 = 4;
    ASSERT_TRUE(Siege::StepTheBattleSkill(StepContext()).HasValue());
    EXPECT_EQ(hero.skillIndex, 3);
    EXPECT_EQ(hero.byte373, 0xff);
}

TEST_F(SiegeUiTest, InitialSelectionRejectsSlotZeroAndTheMasterSearchesEverySkillBeforeTheFirstAllowedOne)
{
    BattleRole();
    auto& hero = world->Hero();
    world->OwnCharacter().SetSkillAt(0, 0x46);
    world->OwnCharacter().SetSkillAt(7, 0x47);
    const CrtRandom expected = After(random, 5 * 3848);
    Siege::InitializeTheBattleSkill(*world, random);
    EXPECT_EQ(hero.byte371, 0x47);
    EXPECT_EQ(random.State(), expected.State());
}

TEST_F(SiegeUiTest, TheSkillDrawConsumesSixtyRandomsAndResetsThePressedArrowOnlyOnTheRealDraw)
{
    BattleRole();
    Cost(10);
    auto& runtime = world->Runtime();
    runtime.castleSiege.siegeBattleUi.skillPressed = 1;
    auto context = DrawContext(true);
    const CrtRandom expected = After(random, 60);
    ASSERT_TRUE(Siege::DrawTheBattleSkill(context).HasValue());
    EXPECT_EQ(random.State(), expected.State());
    EXPECT_EQ(runtime.castleSiege.siegeBattleUi.skillPressed, 1);
    ASSERT_FALSE(sprites.Commands().empty());
    EXPECT_EQ(sprites.Commands()[0].texture->value, 0x65f);
    EXPECT_EQ(sprites.Vertices()[0].color.green, 0.0f);
    auto actual = DrawContext();
    ASSERT_TRUE(Siege::DrawTheBattleSkill(actual).HasValue());
    EXPECT_EQ(runtime.castleSiege.siegeBattleUi.skillPressed, 0);
}

TEST_F(SiegeUiTest, TheClockUsesOneMinuteBorrowAndTheOriginalTickWrap)
{
    auto& runtime = world->Runtime();
    runtime.castleSiege.siegeActive = true;
    runtime.map = 30;
    Siege::ResetSiegeClock(runtime);
    Siege::SetSiegeClock(runtime, 0, 0, 0xfffffff0u);
    auto context = DrawContext();
    context.tickCount = 7200000u - 16u;
    context.worldClock = 1000.0f;
    Siege::DrawTheBattleClock(context);
    EXPECT_EQ(runtime.castleSiege.siegeBattleUi.clock.blink, 1);
    std::size_t minusSigns = 0;
    for (const auto& command : sprites.Commands())
        if (command.texture && command.texture->value == 1)
            for (std::size_t vertex = command.firstVertex; vertex < command.firstVertex + command.vertexCount;
                 vertex += 6)
                if (sprites.Vertices()[vertex].u == -0.1875f)
                    ++minusSigns;
    EXPECT_EQ(minusSigns, 4u); // −1 horas y −60 minutos, dos veces; no normaliza a −2:00.
    context.worldClock = 1000.5f;
    context.projecting = true;
    Siege::DrawTheBattleClock(context);
    EXPECT_EQ(runtime.castleSiege.siegeBattleUi.clock.blink, 1);
    context.projecting = false;
    Siege::DrawTheBattleClock(context);
    EXPECT_EQ(runtime.castleSiege.siegeBattleUi.clock.blink, 0);
    EXPECT_FLOAT_EQ(runtime.castleSiege.siegeBattleUi.clock.blinkTime, 1000.5f);
}

TEST_F(SiegeUiTest, TheMapReadsEachVisibleActorEvenWhenItsDotEndsOutsideTheMap)
{
    auto& runtime = world->Runtime();
    runtime.castleSiege.siegePanelKind = std::uint8_t{0};
    auto& hero = world->Hero();
    hero.cellX = 255;
    hero.cellY = 0;
    auto& actor = world->Actors()[5];
    actor.alive = true;
    actor.byte88 = 1;
    actor.cellX = 0;
    actor.cellY = 255;
    const CrtRandom expected = After(random, 2 + 8 + 8);
    auto draw = DrawContext();
    ASSERT_TRUE(Siege::DrawTheBattlePanel(draw).HasValue());
    EXPECT_EQ(random.State(), expected.State());
    EXPECT_EQ(runtime.castleSiege.siegePanel.offsetX, 191);
    EXPECT_EQ(runtime.castleSiege.siegePanel.offsetY, 128);
    EXPECT_EQ(actor.cellXAccesses, 0);
    EXPECT_EQ(actor.cellYAccesses, 0);
    EXPECT_EQ(sprites.Commands()[0].texture->value, 0x69d);
}

TEST_F(SiegeUiTest, TheCommanderWithoutSideBitsUsesTheMapOffsetAsItsFriendlyMask)
{
    auto& runtime = world->Runtime();
    runtime.castleSiege.siegePanelKind = std::uint8_t{2};
    auto& hero = world->Hero();
    hero.cellY = 0;
    hero.flags78 = 0;
    auto& actor = world->Actors()[5];
    actor.alive = true;
    actor.byte88 = 1;
    actor.flags78 = 128;
    const CrtRandom expected = After(random, 2 + 8);
    auto draw = DrawContext();
    ASSERT_TRUE(Siege::DrawTheBattlePanel(draw).HasValue());
    EXPECT_EQ(random.State(), expected.State());
    EXPECT_EQ(runtime.castleSiege.siegePanel.offsetY, 128);
}

// 0x411c12/0x411ce5/0x411db8: dv=0x3f100000. Los iconos del menú usan sus rectángulos completos.
TEST_F(SiegeUiTest, TheCommandMenuUsesTheNativeAtlasRectangles)
{
    auto& runtime = world->Runtime();
    runtime.castleSiege.siegePanelKind = std::uint8_t{2};
    runtime.castleSiege.siegePanel.selected = 1;
    auto draw = DrawContext();
    ASSERT_TRUE(Siege::DrawTheBattlePanel(draw).HasValue());
    std::array<bool, 3> seen{};
    std::size_t backgrounds = 0;
    for (const auto& command : sprites.Commands())
    {
        if (!command.texture)
            continue;
        const auto slot = command.texture->value;
        for (std::size_t first = command.firstVertex; first < command.firstVertex + command.vertexCount; first += 6)
        {
            const auto& corner = sprites.Vertices()[first + 2];
            if (slot == 0x6a4)
            {
                EXPECT_EQ(std::bit_cast<std::uint32_t>(corner.u), 0x3f700000u);
                EXPECT_EQ(std::bit_cast<std::uint32_t>(corner.v), 0x3f100000u);
                ++backgrounds;
            }
            if (slot < 0x6a0 || slot > 0x6a2)
                continue;
            const std::size_t index = slot - 0x6a0;
            constexpr std::array<std::uint32_t, 3> U{0x3f500000, 0x3f100000, 0x3f300000};
            constexpr std::array<std::uint32_t, 3> V{0x3f500000, 0x3f700000, 0x3f400000};
            EXPECT_EQ(std::bit_cast<std::uint32_t>(corner.u), U[index]);
            EXPECT_EQ(std::bit_cast<std::uint32_t>(corner.v), V[index]);
            seen[index] = true;
        }
    }
    EXPECT_EQ(backgrounds, 3u);
    EXPECT_EQ(seen, (std::array<bool, 3>{true, true, true}));
}

// 0x410434..0x410478 y 0x4119a2..0x4119e6: un status sin caso conserva las tres métricas de la fila previa.
TEST_F(SiegeUiTest, TheStatusMetricsSurviveAnUnrecognizedFollowingStatus)
{
    auto& runtime = world->Runtime();
    auto& hero = world->Hero();
    hero.cellX = 64;
    hero.cellY = 192;
    for (const std::uint8_t kind : {std::uint8_t{1}, std::uint8_t{2}})
    {
        runtime.castleSiege.siegePanelKind = kind;
        runtime.castleSiege.siegePanel.status[0] = {1, 64, 192, 0, 0};
        runtime.castleSiege.siegePanel.status[1] = {2, 65, 191, 4, 0};
        runtime.castleSiege.siegePanel.selected = 1;
        auto draw = DrawContext();
        ASSERT_TRUE(Siege::DrawTheBattlePanel(draw).HasValue());
        bool foundStatus = false;
        bool foundHighlight = false;
        for (const auto& command : sprites.Commands())
        {
            if (!command.texture)
                continue;
            for (std::size_t first = command.firstVertex; first < command.firstVertex + command.vertexCount; first += 6)
            {
                const auto& origin = sprites.Vertices()[first];
                const auto& corner = sprites.Vertices()[first + 2];
                if (command.texture->value == 0x6a4 && corner.x - origin.x == 11.0f)
                {
                    EXPECT_EQ(std::bit_cast<std::uint32_t>(corner.u), 0x3f400000u);
                    EXPECT_EQ(std::bit_cast<std::uint32_t>(corner.v), 0x3f400000u);
                    foundStatus = true;
                }
                if (command.texture->value == 0x6a6)
                {
                    EXPECT_FLOAT_EQ(corner.x - origin.x, 13.0f);
                    EXPECT_FLOAT_EQ(origin.y - corner.y, 13.0f);
                    foundHighlight = true;
                }
            }
        }
        EXPECT_TRUE(foundStatus);
        EXPECT_EQ(foundHighlight, kind == 2);
    }
}

TEST_F(SiegeUiTest, AFirstUnrecognizedStatusDoesNotInventUninitializedStackMetrics)
{
    auto& runtime = world->Runtime();
    runtime.castleSiege.siegePanelKind = std::uint8_t{1};
    auto& hero = world->Hero();
    hero.cellX = 64;
    hero.cellY = 192;
    runtime.castleSiege.siegePanel.status[0] = {1, 64, 192, 4, 2};
    auto draw = DrawContext();
    const auto drawn = Siege::DrawTheBattlePanel(draw);
    ASSERT_FALSE(drawn.HasValue());
    EXPECT_NE(drawn.Error().find("stack"), std::string::npos);
    // El número y el parpadeo preceden al primer uso de las métricas no inicializadas.
    EXPECT_EQ(runtime.castleSiege.siegePanel.status[0][4], 1);
}

// 411c20/411cf3/411dc6: altura18; los tres fondos coinciden con las filas de entrada de410773.
TEST_F(SiegeUiTest, EveryCommanderMenuBackgroundUsesItsNativeEighteenPixelHeight)
{
    auto& runtime = world->Runtime();
    runtime.castleSiege.siegePanelKind = std::uint8_t{2};
    for (const std::uint8_t row : {std::uint8_t{1}, std::uint8_t{6}, std::uint8_t{7}})
    {
        SCOPED_TRACE(row);
        runtime.castleSiege.siegePanel.selected = row;
        const auto expected = After(random, 10);
        auto draw = DrawContext();
        ASSERT_TRUE(Siege::DrawTheBattlePanel(draw).HasValue());
        std::size_t backgrounds = 0;
        for (const auto& command : sprites.Commands())
        {
            if (!command.texture || (command.texture->value != 0x6a4 && command.texture->value != 0x6a5))
                continue;
            for (std::size_t first = command.firstVertex; first < command.firstVertex + command.vertexCount; first += 6)
            {
                const auto& origin = sprites.Vertices()[first];
                const auto& corner = sprites.Vertices()[first + 2];
                EXPECT_EQ(corner.x - origin.x, 30.0f);
                EXPECT_EQ(origin.y - corner.y, 18.0f);
                EXPECT_EQ(corner.u, 0.9375f);
                EXPECT_EQ(corner.v, 0.5625f);
                ++backgrounds;
            }
        }
        EXPECT_EQ(backgrounds, 3u);
        EXPECT_EQ(random.State(), expected.State());
    }
}

// 40f7ff..40f827: unordered pasa la primera prueba, y el C0 de la segunda pone alpha en1.
TEST_F(SiegeUiTest, TheOpacityClickRestoresOneOnTheNativeUnorderedComparison)
{
    auto& runtime = world->Runtime();
    for (const std::uint8_t kind : {std::uint8_t{0}, std::uint8_t{1}, std::uint8_t{2}})
    {
        SCOPED_TRACE(kind);
        runtime.castleSiege.siegePanelKind = kind;
        runtime.castleSiege.siegePanel.alpha = std::numeric_limits<float>::quiet_NaN();
        const auto before = random.State();
        Click();
        ASSERT_TRUE(Siege::StepTheBattlePanel(StepContext(494, 287)).HasValue());
        EXPECT_EQ(runtime.castleSiege.siegePanel.alpha, 1.0f);
        EXPECT_EQ(runtime.castleSiege.siegePanel.page, 1);
        EXPECT_FALSE(runtime.movement.pendingClick);
        EXPECT_FALSE(runtime.movement.leftHeld);
        EXPECT_TRUE(requests.packets.empty());
        EXPECT_EQ(random.State(), before);
    }
}

// 410261..410274: FISTP m64 deNaN deja su dword bajo en0, como OriginalFtolLow (Q-C186).
TEST_F(SiegeUiTest, TheOpacityPercentageUsesTheNativeFtolLowDwordForNan)
{
    auto& runtime = world->Runtime();
    runtime.castleSiege.siegePanelKind = std::uint8_t{0};
    runtime.castleSiege.siegePanel.alpha = std::numeric_limits<float>::quiet_NaN();
    const auto expected = After(random, 10);
    auto draw = DrawContext();
    ASSERT_TRUE(Siege::DrawTheBattlePanel(draw).HasValue());
    std::size_t digitVertices = 0;
    for (const auto& command : sprites.Commands())
        if (command.texture == Client::UI::DigitsSlot)
        {
            digitVertices += command.vertexCount;
            EXPECT_EQ(sprites.Vertices()[command.firstVertex].u, 0.0f);
        }
    EXPECT_EQ(digitVertices, 6u);
    EXPECT_EQ(random.State(), expected.State());
}

TEST_F(SiegeUiTest, AnUnresolvedOpacityFtolKeepsTheBasePrefixAndReportsItsAddress)
{
    auto& runtime = world->Runtime();
    runtime.castleSiege.siegePanelKind = std::uint8_t{0};
    runtime.castleSiege.siegePanel.alpha = std::numeric_limits<float>::infinity();
    const auto expected = After(random, 10);
    auto draw = DrawContext();
    const auto result = Siege::DrawTheBattlePanel(draw);
    ASSERT_FALSE(result.HasValue());
    EXPECT_NE(result.Error().find("0x410274"), std::string::npos) << result.Error();
    bool map = false;
    bool frame = false;
    bool zoom = false;
    for (const auto& command : sprites.Commands())
    {
        if (!command.texture)
            continue;
        map |= command.texture->value == 0x69d;
        frame |= command.texture->value == 0x69f;
        zoom |= command.texture->value == 0x6a7;
        EXPECT_NE(command.texture, Client::UI::DigitsSlot);
    }
    EXPECT_TRUE(map);
    EXPECT_TRUE(frame);
    EXPECT_TRUE(zoom);
    EXPECT_EQ(random.State(), expected.State());
}

// El preview inicializa ancho/alto, pero no el atlas que el loop lee de[ebp-20] (411aa0).
TEST_F(SiegeUiTest, TheCommanderPreviewDoesNotInventTheFirstUnknownStatusAtlas)
{
    auto& runtime = world->Runtime();
    runtime.castleSiege.siegePanelKind = std::uint8_t{2};
    auto& hero = world->Hero();
    hero.cellX = 64;
    hero.cellY = 192;
    auto& panel = runtime.castleSiege.siegePanel;
    panel.selected = 1;
    panel.command = 0;
    panel.status[0] = {1, 64, 192, 4, 2};
    auto draw = DrawContext();
    draw.mouse = {550, 350};
    const auto result = Siege::DrawTheBattlePanel(draw);
    ASSERT_FALSE(result.HasValue());
    EXPECT_NE(result.Error().find("stack"), std::string::npos) << result.Error();
    EXPECT_EQ(panel.status[0][4], 1);
    EXPECT_TRUE(std::ranges::any_of(sprites.Commands(), [](const auto& command)
                                    { return command.texture && command.texture->value == 0x6a0; }));
    EXPECT_FALSE(std::ranges::any_of(sprites.Commands(), [](const auto& command)
                                     { return command.texture && command.texture->value == 0x6a4; }));
}

TEST_F(SiegeUiTest, AStatusOutsideTheCropDoesNotSeedMetricsOrConsumeItsFlashBeforeAnUnknownStatus)
{
    auto& runtime = world->Runtime();
    auto& hero = world->Hero();
    hero.cellX = 64;
    hero.cellY = 192;
    for (const std::uint8_t kind : {std::uint8_t{1}, std::uint8_t{2}})
    {
        SCOPED_TRACE(kind);
        runtime.castleSiege.siegePanelKind = kind;
        runtime.castleSiege.siegePanel.status = {};
        runtime.castleSiege.siegePanel.status[0] = {1, 0, 0, 0, 2};
        runtime.castleSiege.siegePanel.status[1] = {2, 64, 192, 4, 2};
        auto draw = DrawContext();
        const auto result = Siege::DrawTheBattlePanel(draw);
        ASSERT_FALSE(result.HasValue());
        EXPECT_NE(result.Error().find("stack"), std::string::npos) << result.Error();
        EXPECT_EQ(runtime.castleSiege.siegePanel.status[0][4], 2);
        EXPECT_EQ(runtime.castleSiege.siegePanel.status[1][4], 1);
    }
}
} // namespace
