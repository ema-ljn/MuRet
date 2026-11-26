#include "Scenes/CastleBattleReceive.h"

#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Crt/CrtRandom.h"
#include "Tables/TextTable.h"
#include "Data/Textures/Texture.h"
#include "Data/Textures/TextureSet.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Scenes/ModelTextureTable.h"
#include "Scenes/ViewportPlayers.h"
#include "Protocol/Actors/Viewport.h"
#include "UI/PanelDialog.h"
#include "Protocol/CastleSiege/CastleBattle.h"
#include "Protocol/Outgoing.h"
#include "Scenes/ActorState.h"
#include "Scenes/CastleSiegeReceive.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/CastleBattleState.h"
#include "UI/CastleSiegeState.h"

#include "support/RecordingSink.h"
#include "support/FakeTextureStore.h"
#include "support/SceneDataFiles.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <map>
#include <string_view>
#include <utility>
#include <vector>
#include <string>

// Los paquetes de la batalla del castillo sobre el mundo (CL9 hito 13; 04 § Castle Siege, la batalla). Ningún caso
// mira el mapa: cada test corre en Lorencia (0), Devias (2) y el castillo (30). Los valores son inventados; el estado
// del rand() se compara para ver que ninguno saca.
namespace
{
namespace Battle = Client::Protocol::CastleSiege;
using Client::Crt::CrtRandom;
using Client::Scenes::CastleReceiveContext;
using Client::Scenes::WorldState;
using Client::UI::SwitchHolderHalf;
using Client::UI::SwitchHoldersSize;

constexpr std::uint32_t Seed = 4242;
constexpr std::size_t PhantomRecord = 400;

class BattleMeasure final : public Client::Render::Text::ITextMeasure
{
public:
    Client::Render::Text::TextExtent Measure(Client::Render::Text::FontKind, std::string_view text) const override
    {
        return {static_cast<int>(text.size()) * 6, 13};
    }
};
std::map<std::size_t, std::string> BattleTexts()
{
    std::map<std::size_t, std::string> texts{{860, "denegado"}, {1980, "%s %u"}, {1982, "mensaje 1982"}};
    for (std::size_t id = 1484; id <= 1497; ++id)
        texts[id] = "mensaje " + std::to_string(id);
    texts[1486] = "ocupado por %s";
    texts[1488] = "intenta %s";
    return texts;
}

class CastleBattleReceiveTest : public ::testing::TestWithParam<std::int32_t>
{
protected:
    CastleBattleReceiveTest() : m_world(std::make_unique<WorldState>()), m_random(Seed)
    {
        m_world->Runtime().map = GetParam();
    }

    [[nodiscard]] CastleReceiveContext Context()
    {
        return CastleReceiveContext{.world = *m_world,
                                    .random = m_random,
                                    .log = m_log,
                                    .measure = &m_measure,
                                    .texts = &m_texts,
                                    .send = [this](Client::Protocol::Outgoing packet)
                                    { m_sent.push_back(std::move(packet)); },
                                    .tick = 77};
    }

    void Apply(const Battle::CastleBattlePacket& packet)
    {
        const auto applied = Client::Scenes::ApplyCastleBattlePacket(packet, Context());
        ASSERT_TRUE(applied.HasValue()) << applied.Error();
    }

    [[nodiscard]] Client::UI::CastleSiegeState& Castle()
    {
        return m_world->Runtime().castleSiege;
    }

    [[nodiscard]] Client::Scenes::ActorState& Actor(std::size_t index)
    {
        return m_world->Actors()[index];
    }

    // Un personaje vivo con esta clave en el lugar `index`.
    void Place(std::size_t index, std::uint16_t key)
    {
        Actor(index).alive = true;
        Actor(index).key = key;
    }

    void ExpectNoRandom()
    {
        EXPECT_EQ(m_random.State(), CrtRandom(Seed).State());
    }

    Client::Tests::FakeTextureStore m_store;
    std::unique_ptr<WorldState> m_world;
    CrtRandom m_random;
    BattleMeasure m_measure;
    Client::Tables::TextTable m_texts = Client::Tests::MakeTextTable(BattleTexts());
    std::vector<Client::Protocol::Outgoing> m_sent;
    Client::Tests::RecordingSink m_sink;
    Client::Core::Log m_log{m_sink, Client::Core::LogLevel::Debug};
};

INSTANTIATE_TEST_SUITE_P(Maps, CastleBattleReceiveTest, ::testing::Values(0, 2, 30));

// B9 01 (0x4e0750): +0x93 del que tiene la clave; sin nadie con ella, el registro 400.
TEST_P(CastleBattleReceiveTest, NpcLevelWritesByte93OfTheKeyOrOfRecord400)
{
    Place(7, 0x0123);
    Apply(Battle::NpcLevel{.key = 0x0123, .level = 3});
    EXPECT_EQ(Actor(7).byte93, 3);

    Apply(Battle::NpcLevel{.key = 0x0456, .level = 5});
    EXPECT_EQ(Actor(PhantomRecord).byte93, 5);
    EXPECT_EQ(Actor(7).byte93, 3);
    ExpectNoRandom();
}

// B9 02 (0x4e0790): el par de cada byte arriba y el impar abajo, 64 colores, y una vuelta más de la marca.
TEST_P(CastleBattleReceiveTest, OwnerMarkUnpacksTheNibblesHighFirst)
{
    auto& table = m_world->FaunaResources().TextureTable();
    table.Bind(m_store);
    ASSERT_TRUE(table
                    .Upload(Client::Data::Textures::LoadedTexture{
                        .slot = 0x69c,
                        .texture = {.width = 64,
                                    .height = 64,
                                    .textureWidth = 64,
                                    .textureHeight = 64,
                                    .format = Client::Data::Textures::PixelFormat::Rgba,
                                    .pixels = std::vector<std::uint8_t>(64 * 64 * 4)}})
                    .HasValue());
    Battle::OwnerMark mark;
    for (std::size_t i = 0; i < mark.packed.size(); ++i)
        mark.packed.at(i) = static_cast<std::uint8_t>((i % 16) * 0x10 + (15 - (i % 16)));
    Apply(mark);
    Apply(mark);
    const auto& battle = Castle().battle;
    for (std::size_t i = 0; i < mark.packed.size(); ++i)
    {
        EXPECT_EQ(battle.ownerMark.at(2 * i), i % 16) << i;
        EXPECT_EQ(battle.ownerMark.at((2 * i) + 1), 15 - (i % 16)) << i;
    }
    EXPECT_EQ(battle.ownerMarkRevision, 2u);
    EXPECT_EQ(m_store.loadCalls, 3);
    ExpectNoRandom();
}

// B9 03 (0x4e07f0) distinto de 0: los campos de [0x7d53700] y después el 0x200000, que abre la ventana del hito 12.
TEST_P(CastleBattleReceiveTest, HuntZoneInfoWritesTheWindowBeforeOpeningIt)
{
    Apply(Battle::HuntZoneInfo{.mode = 2, .allowed = 9, .fee = 7000, .at0c = 11, .at10 = 22});
    EXPECT_EQ(m_world->Runtime().panels.delegated.secondCastleSiege, 1);
    const auto& window = Castle().huntZone;
    EXPECT_EQ(window.mode, 2);
    EXPECT_EQ(window.allowed, 1);
    EXPECT_EQ(window.fee, 7000u);
    EXPECT_EQ(window.editedFee, 7000u);
    EXPECT_EQ(window.max, 11u);
    EXPECT_EQ(window.step, 22u);
}

// Con 0 cancela el diálogo previo y abre el texto 860, conservando los campos de la zona.
TEST_P(CastleBattleReceiveTest, HuntZoneInfoAndEntryWithZeroOpenTheFailureDialog)
{
    Castle().huntZone.fee = 5;
    Apply(Battle::HuntZoneInfo{.mode = 0});
    auto& dialog = m_world->Runtime().panels.delegated.panelDialog;
    EXPECT_EQ(dialog.lines[0], "denegado");
    EXPECT_EQ(dialog.flags, Client::UI::PanelDialogOk);
    EXPECT_EQ(Castle().huntZone.fee, 5u);
    EXPECT_EQ(Castle().huntZone.mode, 0);
    Apply(Battle::HuntZoneEntryResult{.result = 0});
    EXPECT_EQ(dialog.lines[0], "denegado");
    Apply(Battle::HuntZoneEntryResult{.result = 1});
    Apply(Battle::HuntZoneEntryResult{.result = 0xff});
    EXPECT_EQ(dialog.lines[0], "denegado");
    ExpectNoRandom();
}

// BA (0x4cf1b0): +0x1bc solo con cuál en 0x28, también en el registro 400.
TEST_P(CastleBattleReceiveTest, ActorFieldWritesByte1bcOnlyWithKind28)
{
    Place(3, 0x0042);
    Apply(Battle::ActorField{.key = 0x0042, .kind = 0x27, .value = 9});
    EXPECT_EQ(Actor(3).byte1bc, 0);
    Apply(Battle::ActorField{.key = 0x0042, .kind = 0x28, .value = 9});
    EXPECT_EQ(Actor(3).byte1bc, 9);
    Apply(Battle::ActorField{.key = 0x8042, .kind = 0x28, .value = 4});
    EXPECT_EQ(Actor(PhantomRecord).byte1bc, 4);
    ExpectNoRandom();
}

// BB (0x4e0f30): solo con el panel del asedio de la clase 2 y alguna terna.
TEST_P(CastleBattleReceiveTest, MinimapMembersAppendOnlyToTheClassTwoPanelAndWrapTheirIndex)
{
    const Battle::MinimapMembers some{.members = {{.index = 0, .x = 10, .y = 20}}};
    Apply(some);
    auto& runtime = m_world->Runtime();
    runtime.siegePanelKind = std::uint8_t{1};
    Apply(some);
    EXPECT_TRUE(runtime.siegePanel.members.empty());
    runtime.siegePanelKind = std::uint8_t{2};
    Apply(some);
    Apply(Battle::MinimapMembers{.members = {{.index = 255, .x = 30, .y = 40}}});
    Apply(Battle::MinimapMembers{});
    ASSERT_EQ(runtime.siegePanel.members.size(), 2u);
    EXPECT_EQ(runtime.siegePanel.members[0], (std::array<std::uint8_t, 3>{1, 10, 20}));
    EXPECT_EQ(runtime.siegePanel.members[1], (std::array<std::uint8_t, 3>{0, 30, 40}));
    EXPECT_EQ(runtime.siegePanel.membersHidden, 0);
    ExpectNoRandom();
}

Battle::SwitchHolder Holder(std::uint16_t key, std::uint8_t fill)
{
    Battle::SwitchHolder holder{.key = key};
    for (std::size_t i = 0; i < holder.bytes.size(); ++i)
        holder.bytes.at(i) = static_cast<std::uint8_t>(fill + i);
    return holder;
}

// B2 20 (0x4e04e0) sin objeto: lo arma con la mitad que elige la clave tal cual, también el último byte; la otra
// queda en 0 en MuRet (Q-C1400).
TEST_P(CastleBattleReceiveTest, SwitchHolderCreatesTheObjectWithItsHalf)
{
    Apply(Holder(0x013d, 0x40));
    ASSERT_TRUE(m_world->Runtime().castleSiegeInfo.has_value());
    const auto& first = *m_world->Runtime().castleSiegeInfo;
    for (std::size_t i = 0; i < SwitchHolderHalf; ++i)
        EXPECT_EQ(first.at(i), 0x40 + i) << i;
    for (std::size_t i = SwitchHolderHalf; i < SwitchHoldersSize; ++i)
        EXPECT_EQ(first.at(i), 0) << i;

    m_world->Runtime().castleSiegeInfo.reset();
    Apply(Holder(0x013e, 0x80));
    const auto& second = *m_world->Runtime().castleSiegeInfo;
    for (std::size_t i = 0; i < SwitchHolderHalf; ++i)
    {
        EXPECT_EQ(second.at(i), 0) << i;
        EXPECT_EQ(second.at(SwitchHolderHalf + i), 0x80 + i) << i;
    }
    ExpectNoRandom();
}

// Con objeto: la mitad tal cual y el último byte en 0 (0x4e05e4, Q-C1401).
TEST_P(CastleBattleReceiveTest, SwitchHolderUpdatesItsHalfWithTheLastByteInZero)
{
    Apply(Holder(0x013d, 0x10));
    Apply(Holder(0x0001, 0x60));
    Apply(Holder(0x013d, 0x20));
    const auto& holders = *m_world->Runtime().castleSiegeInfo;
    for (std::size_t i = 0; i + 1 < SwitchHolderHalf; ++i)
    {
        EXPECT_EQ(holders.at(i), 0x20 + i) << i;
        EXPECT_EQ(holders.at(SwitchHolderHalf + i), 0x60 + i) << i;
    }
    EXPECT_EQ(holders.at(SwitchHolderHalf - 1), 0);
    EXPECT_EQ(holders.at(SwitchHoldersSize - 1), 0);
}

// 0x4ca799: el 0x14 apaga +4..+6 con la clave 0x13d y +0x1f..+0x21 con cualquier otra; sin objeto, nada.
TEST_P(CastleBattleReceiveTest, ForgetTheSwitchHolderClearsTheKeyAndTheShownByte)
{
    auto& runtime = m_world->Runtime();
    Client::Scenes::ForgetTheSwitchHolder(runtime, 0x013d);
    EXPECT_FALSE(runtime.castleSiegeInfo.has_value());

    Apply(Holder(0x013d, 0x10));
    Apply(Holder(0x0002, 0x60));
    Client::Scenes::ForgetTheSwitchHolder(runtime, 0x0777);
    const auto& holders = *runtime.castleSiegeInfo;
    EXPECT_EQ(holders.at(3), 0x13);
    for (const std::size_t at : {std::size_t{4}, std::size_t{5}, std::size_t{6}})
    {
        EXPECT_EQ(holders.at(at), 0x10 + at) << at;
        EXPECT_EQ(holders.at(SwitchHolderHalf + at), 0) << at;
    }
    EXPECT_EQ(holders.at(SwitchHolderHalf + 7), 0x67);
    // Con el bit de arriba, la clave entera no es 0x13d.
    Client::Scenes::ForgetTheSwitchHolder(runtime, 0x813d);
    EXPECT_EQ(holders.at(6), 0x16);
    Client::Scenes::ForgetTheSwitchHolder(runtime, 0x013d);
    for (const std::size_t at : {std::size_t{4}, std::size_t{5}, std::size_t{6}})
        EXPECT_EQ(holders.at(at), 0) << at;
    EXPECT_EQ(holders.at(7), 0x17);
}
// El B2 20 y el viewport 14 deben operar sobre el mismo objeto que lee el HUD.
TEST_P(CastleBattleReceiveTest, AViewportLeaveClearsTheReceivedHolderUsingTheFullWireKey)
{
    Apply(Holder(0x013d, 0x10));
    Apply(Holder(0x0002, 0x60));
    Client::Scenes::ApplyViewportLeave(Client::Protocol::Actors::Leave{.keys = {0x813d}}, *m_world);
    auto& holders = *m_world->Runtime().castleSiegeInfo;
    EXPECT_EQ(holders[6], 0x16);
    EXPECT_EQ(holders[0x1b + 6], 0);
    Client::Scenes::ApplyViewportLeave(Client::Protocol::Actors::Leave{.keys = {0x013d}}, *m_world);
    EXPECT_EQ(holders[6], 0);
    ExpectNoRandom();
}

TEST_P(CastleBattleReceiveTest, CrownRegistrationKeepsUnsignedDurationWrapAndTheTimedCallback)
{
    const std::array<std::pair<std::uint32_t, std::uint32_t>, 4> cases{
        {{29999, 1}, {30000, 0}, {30001, 0xffffffffu}, {0xffffffffu, 30001}}};
    for (const auto& [elapsed, duration] : cases)
    {
        Apply(Battle::CrownRegistration{.result = 0, .elapsed = elapsed});
        const auto& dialog = m_world->Runtime().panels.delegated.panelDialog;
        EXPECT_EQ(dialog.timerMs, duration);
        EXPECT_EQ(dialog.owner, Client::UI::PanelDialogOwner::CastleCrownRegistration);
        EXPECT_EQ(dialog.flags, Client::UI::PanelDialogTimed);
        EXPECT_EQ(dialog.placed, 0);
    }
    Apply(Battle::CrownRegistration{.result = 255});
    EXPECT_EQ(m_world->Runtime().panels.delegated.panelDialog.opened, 0u);
    ExpectNoRandom();
}

TEST_P(CastleBattleReceiveTest, SiegeSideClearsOnlyTheFirstOpposingBitAndZeroPreservesFlags)
{
    auto& runtime = m_world->Runtime();
    auto& hero = Actor(runtime.hero);
    hero.flags78 = 0x80000u | 0x400000u | 0x800000u | 0x10u;
    Apply(Battle::SiegeSide{.side = 1});
    EXPECT_EQ(hero.flags78, 0xc40010u);
    Apply(Battle::SiegeSide{.side = 0});
    EXPECT_EQ(hero.flags78, 0xc40010u);
    Apply(Battle::SiegeSide{.side = 2});
    EXPECT_EQ(hero.flags78, 0xc80010u);
    ExpectNoRandom();
}

TEST_P(CastleBattleReceiveTest, TheBattleClockStoresRawHoursAndMinutesAndTheReceiveTick)
{
    auto& clock = m_world->Runtime().siegeBattleUi.clock;
    clock.blink = 1;
    clock.blinkTime = 123.0f;
    Apply(Battle::SiegeTime{.hours = 255, .minutes = 255});
    EXPECT_EQ(clock.hours, 255u);
    EXPECT_EQ(clock.minutes, 255u);
    EXPECT_EQ(clock.tick, 77u);
    EXPECT_EQ(clock.blink, 1);
    EXPECT_FLOAT_EQ(clock.blinkTime, 123.0f);
    ExpectNoRandom();
}

} // namespace
