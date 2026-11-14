#include "Scenes/CastleBattleReceive.h"

#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Protocol/CastleBattle.h"
#include "Render/Models/Cloth.h"
#include "Scenes/ActorState.h"
#include "Scenes/CastleSiegeReceive.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/CastleBattleState.h"
#include "UI/CastleSiegeState.h"

#include "support/RecordingSink.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

// Los paquetes de la batalla del castillo sobre el mundo (CL9 hito 13; 04 § Castle Siege, la batalla). Ningún caso
// mira el mapa: cada test corre en Lorencia (0), Devias (2) y el castillo (30). Los valores son inventados; el estado
// del rand() se compara para ver que ninguno saca.
namespace
{
namespace Battle = Client::Protocol::CastleBattle;
using Client::Render::Models::CrtRandom;
using Client::Scenes::CastleReceiveContext;
using Client::Scenes::WorldState;
using Client::UI::SwitchHolderHalf;
using Client::UI::SwitchHoldersSize;

constexpr std::uint32_t Seed = 4242;
constexpr std::size_t PhantomRecord = 400;

class CastleBattleReceiveTest : public ::testing::TestWithParam<std::int32_t>
{
protected:
    CastleBattleReceiveTest() : m_world(std::make_unique<WorldState>()), m_random(Seed)
    {
        m_world->Runtime().map = GetParam();
    }

    [[nodiscard]] CastleReceiveContext Context()
    {
        return CastleReceiveContext{.world = *m_world, .random = m_random, .log = m_log};
    }

    void Apply(const Battle::CastleBattlePacket& packet)
    {
        const auto applied = Client::Scenes::ApplyCastleBattlePacket(packet, Context());
        ASSERT_TRUE(applied.HasValue()) << applied.Error();
    }

    [[nodiscard]] std::string CutOf(const Battle::CastleBattlePacket& packet)
    {
        const auto applied = Client::Scenes::ApplyCastleBattlePacket(packet, Context());
        EXPECT_FALSE(applied.HasValue());
        return applied.HasValue() ? std::string{} : applied.Error();
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

    std::unique_ptr<WorldState> m_world;
    CrtRandom m_random;
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
    ExpectNoRandom();
}

// B9 03 (0x4e07f0) distinto de 0: los campos de [0x7d53700] y después el 0x200000, que abre la ventana del hito 12.
TEST_P(CastleBattleReceiveTest, HuntZoneInfoWritesTheWindowBeforeOpeningIt)
{
    const std::string cut = CutOf(Battle::HuntZoneInfo{.mode = 2, .allowed = 9, .fee = 7000, .at0c = 11, .at10 = 22});
    EXPECT_NE(cut.find("0x6599a0(0x200000)"), std::string::npos) << cut;
    const auto& window = Castle().huntZone;
    EXPECT_EQ(window.mode, 2);
    EXPECT_EQ(window.allowed, 1);
    EXPECT_EQ(window.fee, 7000u);
    EXPECT_EQ(window.field1c, 7000u);
    EXPECT_EQ(window.field24, 11u);
    EXPECT_EQ(window.field20, 22u);
}

// Con 0, el diálogo del texto 860 (hito 8), sin tocar la ventana.
TEST_P(CastleBattleReceiveTest, HuntZoneInfoAndEntryWithZeroCutAtTheDialog)
{
    Castle().huntZone.fee = 5;
    const std::string info = CutOf(Battle::HuntZoneInfo{.mode = 0});
    EXPECT_NE(info.find("0x4e07fd"), std::string::npos) << info;
    EXPECT_NE(info.find("P-C90"), std::string::npos) << info;
    EXPECT_EQ(Castle().huntZone.fee, 5u);
    EXPECT_EQ(Castle().huntZone.mode, 0);

    const std::string entry = CutOf(Battle::HuntZoneEntryResult{.result = 0});
    EXPECT_NE(entry.find("0x4e086d"), std::string::npos) << entry;
    Apply(Battle::HuntZoneEntryResult{.result = 1});
    Apply(Battle::HuntZoneEntryResult{.result = 0xff});
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
TEST_P(CastleBattleReceiveTest, MinimapMembersOnlyCutWithTheClassTwoPanel)
{
    const Battle::MinimapMembers some{.members = {{.index = 0, .x = 10, .y = 20}}};
    Apply(some);
    m_world->Runtime().siegePanelKind = 1;
    Apply(some);
    m_world->Runtime().siegePanelKind = 2;
    Apply(Battle::MinimapMembers{});
    const std::string cut = CutOf(some);
    EXPECT_NE(cut.find("0x411ed0"), std::string::npos) << cut;
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
    ASSERT_TRUE(Castle().battle.switchHolders.has_value());
    const auto& first = *Castle().battle.switchHolders;
    for (std::size_t i = 0; i < SwitchHolderHalf; ++i)
        EXPECT_EQ(first.at(i), 0x40 + i) << i;
    for (std::size_t i = SwitchHolderHalf; i < SwitchHoldersSize; ++i)
        EXPECT_EQ(first.at(i), 0) << i;

    Castle().battle.switchHolders.reset();
    Apply(Holder(0x013e, 0x80));
    const auto& second = *Castle().battle.switchHolders;
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
    const auto& holders = *Castle().battle.switchHolders;
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
    auto& battle = Castle().battle;
    Client::Scenes::ForgetTheSwitchHolder(battle, 0x013d);
    EXPECT_FALSE(battle.switchHolders.has_value());

    Apply(Holder(0x013d, 0x10));
    Apply(Holder(0x0002, 0x60));
    Client::Scenes::ForgetTheSwitchHolder(battle, 0x0777);
    const auto& holders = *battle.switchHolders;
    EXPECT_EQ(holders.at(3), 0x13);
    for (const std::size_t at : {std::size_t{4}, std::size_t{5}, std::size_t{6}})
    {
        EXPECT_EQ(holders.at(at), 0x10 + at) << at;
        EXPECT_EQ(holders.at(SwitchHolderHalf + at), 0) << at;
    }
    EXPECT_EQ(holders.at(SwitchHolderHalf + 7), 0x67);
    // Con el bit de arriba, la clave entera no es 0x13d.
    Client::Scenes::ForgetTheSwitchHolder(battle, 0x813d);
    EXPECT_EQ(holders.at(6), 0x16);
    Client::Scenes::ForgetTheSwitchHolder(battle, 0x013d);
    for (const std::size_t at : {std::size_t{4}, std::size_t{5}, std::size_t{6}})
        EXPECT_EQ(holders.at(at), 0) << at;
    EXPECT_EQ(holders.at(7), 0x17);
}
} // namespace
