#include "Monsters/NpcChatter.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Tables/PlayerModels.h"
#include "Tables/TextTable.h"
#include "Models/Skeleton.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Kernel/ActorState.h"
#include "Actors/CharacterStep.h"
#include "Chat/ChatBubbles.h"
#include "Kernel/SceneId.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Monsters/MonstersState.h"

#include "support/PlayerActors.h"
#include "support/SceneDataFiles.h"
#include "Chat/ChatRuntime.h"

#include <glm/ext/vector_float3.hpp>
#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

// What the NPCs say: the 0x4fcd0c turn, 0x4fc790, the name message (0x4fcd65..0x4fce15) and 0x4fc8c0. The texts are
// made up; the indices and branches come from the ASM.
namespace
{
using Client::Actors::CharacterStepContext;
using Client::Chat::ChatBubble;
using Client::Crt::CrtRandom;
using Client::Kernel::ActorState;
using Client::Kernel::WorldState;

constexpr std::size_t NpcSlot = 9;
constexpr std::int32_t Lorencia = 0;
constexpr std::int32_t Devias = 2;
constexpr std::int32_t CastleSiege = 30;
// 0x6c88b0, the name of type 236.
constexpr std::array<std::uint8_t, 11> MessageName{0xc8, 0xb2, 0xb1, 0xdd, 0xc0, 0xc7, 0x20, 0xb1, 0xc3, 0xbc, 0xf6};

class LetterMeasure final : public Client::Render::Text::ITextMeasure
{
public:
    [[nodiscard]] Client::Render::Text::TextExtent Measure(Client::Render::Text::FontKind /*font*/,
                                                           std::string_view text) const override
    {
        return {static_cast<int>(text.size()) * 6, 13};
    }
};

std::uint32_t SeedWhere(const std::function<bool(CrtRandom&)>& wanted)
{
    for (std::uint32_t seed = 1;; ++seed)
    {
        CrtRandom random(seed);
        if (wanted(random))
            return seed;
    }
}

CrtRandom AfterDraws(std::uint32_t seed, int draws)
{
    CrtRandom random(seed);
    for (int i = 0; i < draws; ++i)
        static_cast<void>(random.Next());
    return random;
}

class NpcChatterTest : public ::testing::Test
{
protected:
    NpcChatterTest()
    {
        m_world->Runtime().map = Devias;
        ActorState& npc = Npc();
        npc = Client::Tests::UnarmedPlayer();
        npc.alive = true;
        npc.seen = true;
        npc.byte88 = 4;
        npc.subtype = 0xd2;
        npc.animation.action = 1;
        Name("Arquera");
    }

    ActorState& Npc()
    {
        return m_world->Actors()[NpcSlot];
    }

    void Name(std::string_view name)
    {
        Npc().name.fill(std::byte{0});
        for (std::size_t i = 0; i < name.size(); ++i)
            Npc().name[i] = static_cast<std::byte>(name[i]);
        Npc().byte1e3 = 0;
    }

    void TheMessageName()
    {
        for (std::size_t i = 0; i < Npc().name.size(); ++i)
            Npc().name[i] = std::byte{MessageName[i]};
        Npc().byte1e3 = MessageName[10];
    }

    CharacterStepContext Context()
    {
        return CharacterStepContext{.world = *m_world,
                                    .random = m_random,
                                    .scene = Client::Kernel::SceneId::Game,
                                    .playerSkeleton = &m_skeleton,
                                    .texts = m_withTexts ? &m_texts : nullptr,
                                    .textMeasure = m_withMeasure ? &m_letters : nullptr,
                                    .systemTime = m_withClock ? std::function<std::uint32_t()>(
                                                                    [this]()
                                                                    {
                                                                        ++m_clockReads;
                                                                        return m_now;
                                                                    })
                                                              : std::function<std::uint32_t()>()};
    }

    // The bottom line of the NPC's bubble; empty if it has none.
    [[nodiscard]] std::optional<std::string> Said() const
    {
        for (const ChatBubble& bubble : m_world->Runtime().chat.bubbles)
        {
            if (bubble.owner == NpcSlot && bubble.time1 > 0)
                return bubble.line1;
        }
        return std::nullopt;
    }

    void UseTheSeed(std::uint32_t seed)
    {
        m_random = CrtRandom(seed);
    }

    std::unique_ptr<WorldState> m_world = std::make_unique<WorldState>();
    Client::Monsters::MonstersState m_monsters;
    CrtRandom m_random{1};
    Client::Models::Skeleton m_skeleton = Client::Tests::PlayerActionsSkeleton();
    Client::Tables::TextTable m_texts = Client::Tests::MakeTextTable({{1974, "pociones"},
                                                                      {1975, "alas"},
                                                                      {1976, "gremio"},
                                                                      {1827, "fuerza"},
                                                                      {904, "flechas"},
                                                                      {905, "arco"},
                                                                      {1167, "evento uno"},
                                                                      {1168, "evento dos"}});
    LetterMeasure m_letters;
    bool m_withTexts = true;
    bool m_withMeasure = true;
    bool m_withClock = true;
    std::uint32_t m_now = 0;
    int m_clockReads = 0;
};

// 0x4fcd0c..0x4fcd55: with +0x88 at 4, in Lorencia and Devias, the 0x20a of subtype 0xd1 to 0xd3 faces 90 or 0.
TEST_F(NpcChatterTest, TheArcherTurnsInLorenciaAndDevias)
{
    Npc().angles = {10.0f, 20.0f, 30.0f};
    EXPECT_TRUE(Client::Monsters::TurnTheNpc(Npc(), Lorencia));
    EXPECT_EQ(Npc().angles, glm::vec3(0.0f, 0.0f, 90.0f));
    EXPECT_TRUE(Client::Monsters::TurnTheNpc(Npc(), Devias));
    EXPECT_EQ(Npc().angles, glm::vec3(0.0f));

    struct Case
    {
        std::string_view name;
        void (*prepare)(ActorState&);
        std::int32_t map;
    };
    const std::array cases{
        Case{"Noria", [](ActorState&) {}, 3},
        Case{"un jugador", [](ActorState& npc) { npc.byte88 = 1; }, Lorencia},
        Case{"el subtipo 0xd0", [](ActorState& npc) { npc.subtype = 0xd0; }, Lorencia},
        Case{"el subtipo 0xd4", [](ActorState& npc) { npc.subtype = 0xd4; }, Devias},
        Case{"otro modelo", [](ActorState& npc) { npc.type = 0x1d3; }, Devias},
    };
    for (const Case& probe : cases)
    {
        SCOPED_TRACE(probe.name);
        ActorState npc = Npc();
        probe.prepare(npc);
        npc.angles = {10.0f, 20.0f, 30.0f};
        EXPECT_FALSE(Client::Monsters::TurnTheNpc(npc, probe.map));
        EXPECT_EQ(npc.angles, glm::vec3(10.0f, 20.0f, 30.0f));
    }
}

// 0x4fc790: with +0x88 at 4, an even rand() and the type, its message in the chat bubble (0x557170 with 0 and −1).
TEST_F(NpcChatterTest, AnEvenRandomSaysTheLineOfTheType)
{
    struct Case
    {
        std::uint16_t type;
        std::uint16_t npcClass;
        std::optional<std::string> said;
    };
    const std::array cases{
        Case{0x1cd, 0, "pociones"},
        Case{0x1d3, 0, "alas"},
        Case{0x1d5, 0, "gremio"},
        Case{Client::Tables::PlayerSkeletonModel, 0x101, "fuerza"},
        Case{Client::Tables::PlayerSkeletonModel, 0x100, std::nullopt},
        Case{0x1d4, 0, std::nullopt},
        Case{0x1cc, 0, std::nullopt},
    };
    const std::uint32_t even = SeedWhere([](CrtRandom& random) { return random.Next() % 2 == 0; });
    for (const Case& probe : cases)
    {
        SCOPED_TRACE(probe.type);
        m_world->Runtime().chat.bubbles = {};
        UseTheSeed(even);
        Npc().type = probe.type;
        Npc().word376 = probe.npcClass;

        const auto said = Client::Monsters::SayTheNpcLine(Npc(), NpcSlot, Context());

        ASSERT_TRUE(said.HasValue()) << said.Error();
        EXPECT_EQ(Said(), probe.said);
        EXPECT_EQ(m_random.State(), AfterDraws(even, 1).State());
    }
}

// An odd rand() says nothing; without +0x88 at 4 it is not even drawn; 0x1cd is silent on map 30.
TEST_F(NpcChatterTest, AnOddRandomOrAnotherKindSaysNothing)
{
    Npc().type = 0x1d3;
    const std::uint32_t odd = SeedWhere([](CrtRandom& random) { return random.Next() % 2 != 0; });
    UseTheSeed(odd);
    ASSERT_TRUE(Client::Monsters::SayTheNpcLine(Npc(), NpcSlot, Context()).HasValue());
    EXPECT_EQ(Said(), std::nullopt);
    EXPECT_EQ(m_random.State(), AfterDraws(odd, 1).State());

    Npc().byte88 = 2;
    UseTheSeed(odd);
    ASSERT_TRUE(Client::Monsters::SayTheNpcLine(Npc(), NpcSlot, Context()).HasValue());
    EXPECT_EQ(m_random.State(), AfterDraws(odd, 0).State());

    Npc().byte88 = 4;
    Npc().type = 0x1cd;
    m_world->Runtime().map = CastleSiege;
    const std::uint32_t even = SeedWhere([](CrtRandom& random) { return random.Next() % 2 == 0; });
    UseTheSeed(even);
    ASSERT_TRUE(Client::Monsters::SayTheNpcLine(Npc(), NpcSlot, Context()).HasValue());
    EXPECT_EQ(Said(), std::nullopt);
    EXPECT_EQ(m_random.State(), AfterDraws(even, 1).State());
}

// Without Text.bmd or the measure, the speaker cuts with the address of its 0x557170.
TEST_F(NpcChatterTest, WithoutTheTextOrTheMeasureTheSpeakerStops)
{
    Npc().type = 0x1d5;
    const std::uint32_t even = SeedWhere([](CrtRandom& random) { return random.Next() % 2 == 0; });
    m_withTexts = false;
    UseTheSeed(even);
    const auto withoutTexts = Client::Monsters::SayTheNpcLine(Npc(), NpcSlot, Context());
    ASSERT_FALSE(withoutTexts.HasValue());
    EXPECT_NE(withoutTexts.Error().find("0x4fc838"), std::string::npos) << withoutTexts.Error();

    m_withTexts = true;
    m_withMeasure = false;
    UseTheSeed(even);
    const auto withoutMeasure = Client::Monsters::SayTheNpcLine(Npc(), NpcSlot, Context());
    ASSERT_FALSE(withoutMeasure.HasValue());
    EXPECT_NE(withoutMeasure.Error().find("0x4fc838"), std::string::npos) << withoutMeasure.Error();
    EXPECT_EQ(Said(), std::nullopt);
}

// 0x4fcd65..0x4fce15: the first time the clock starts 5000 ms back, so the first one speaks at once, with 1168
// (0x743a190 at 0). Then, more than 4000 ms unsigned, and the messages take turns.
TEST_F(NpcChatterTest, TheNamedLineTakesTurnsEveryFourSeconds)
{
    TheMessageName();
    m_now = 1000;
    ASSERT_TRUE(Client::Monsters::SayTheNamedLine(Npc(), NpcSlot, Context(), &m_monsters).HasValue());
    EXPECT_EQ(m_clockReads, 2);
    EXPECT_EQ(Said(), "evento dos");
    EXPECT_EQ(m_monsters.namedLineTime, 1000u);
    EXPECT_TRUE(m_monsters.namedLineFirst);

    m_world->Runtime().chat.bubbles = {};
    m_now = 5000;
    ASSERT_TRUE(Client::Monsters::SayTheNamedLine(Npc(), NpcSlot, Context(), &m_monsters).HasValue());
    EXPECT_EQ(m_clockReads, 3);
    EXPECT_EQ(Said(), std::nullopt);
    EXPECT_EQ(m_monsters.namedLineTime, 1000u);

    m_now = 5001;
    ASSERT_TRUE(Client::Monsters::SayTheNamedLine(Npc(), NpcSlot, Context(), &m_monsters).HasValue());
    EXPECT_EQ(Said(), "evento uno");
    EXPECT_EQ(m_monsters.namedLineTime, 5001u);
    EXPECT_FALSE(m_monsters.namedLineFirst);
}

// 0x4fcda2: the strcmp goes on after the eleven equal bytes and compares +0x1e4 with the 0 of 0x6c88b0. With something
// there (what a long name leaves, Q-C847), the name is not the message's; one byte short is not either.
TEST_F(NpcChatterTest, TheMessageNameNeedsTheZeroAfterItsElevenBytes)
{
    TheMessageName();
    Npc().nameSpill[0] = std::byte{0};
    EXPECT_TRUE(Client::Monsters::HasTheMessageName(Npc()));
    Npc().nameSpill[0] = std::byte{'s'};
    EXPECT_FALSE(Client::Monsters::HasTheMessageName(Npc()));
    Npc().nameSpill[0] = std::byte{0};
    Npc().byte1e3 = 0;
    EXPECT_FALSE(Client::Monsters::HasTheMessageName(Npc()));

    TheMessageName();
    Npc().nameSpill[0] = std::byte{'s'};
    m_now = 1000;
    ASSERT_TRUE(Client::Monsters::SayTheNamedLine(Npc(), NpcSlot, Context(), &m_monsters).HasValue());
    EXPECT_EQ(Said(), std::nullopt);
}

// The clock starts with any character, even without the name; before the start's 5000 ms the unsigned subtraction
// wraps. Without timeGetTime() it is not touched and only the named one cuts.
TEST_F(NpcChatterTest, TheClockStartsWithAnyoneAndWithoutItOnlyTheNameStops)
{
    m_now = 300;
    ASSERT_TRUE(Client::Monsters::SayTheNamedLine(Npc(), NpcSlot, Context(), &m_monsters).HasValue());
    EXPECT_EQ(m_monsters.namedLineTime, 300u - 5000u);
    EXPECT_EQ(Said(), std::nullopt);
    EXPECT_FALSE(m_monsters.namedLineFirst);

    m_withClock = false;
    m_monsters.namedLineTime.reset();
    ASSERT_TRUE(Client::Monsters::SayTheNamedLine(Npc(), NpcSlot, Context(), &m_monsters).HasValue());
    EXPECT_FALSE(m_monsters.namedLineTime.has_value());
    TheMessageName();
    const auto named = Client::Monsters::SayTheNamedLine(Npc(), NpcSlot, Context(), &m_monsters);
    ASSERT_FALSE(named.HasValue());
    EXPECT_NE(named.Error().find("0x4fcd82"), std::string::npos) << named.Error();
    EXPECT_EQ(Said(), std::nullopt);
}

// Without the monsters' state the clock of 0x7437488 has no home: the step stops and nothing is said.
TEST_F(NpcChatterTest, TheNamedLineStopsWithoutTheMonstersState)
{
    TheMessageName();
    m_now = 1000;
    const auto named = Client::Monsters::SayTheNamedLine(Npc(), NpcSlot, Context(), nullptr);
    ASSERT_FALSE(named.HasValue());
    EXPECT_NE(named.Error().find("0x4fcd65"), std::string::npos) << named.Error();
    EXPECT_EQ(Said(), std::nullopt);
}

// 0x4fc8c0 outside action 1: the rand() % 100 is drawn anyway and it goes back to action 1.
TEST_F(NpcChatterTest, TheTurnedArcherGoesBackToRest)
{
    Npc().animation.action = 0x9d;
    UseTheSeed(7);
    ASSERT_TRUE(Client::Monsters::FinishTheTurnedNpc(Npc(), NpcSlot, Context()).HasValue());
    EXPECT_EQ(Npc().animation.action, 1);
    EXPECT_EQ(Said(), std::nullopt);
    EXPECT_EQ(m_random.State(), AfterDraws(7, 1).State());
}

// 0x4fc8c0 in action 1: the rand() % 100 range gives the action and, in Devias, with the second rand() even (or a
// multiple of 3 from 90), 904 or 905. The second rand() is drawn in Lorencia too, which does not speak.
TEST_F(NpcChatterTest, TheRestingArcherDrawsItsNextAction)
{
    struct Case
    {
        int low;
        int high;
        int oneIn;
        std::uint8_t action;
        std::string said;
    };
    const std::array cases{
        Case{0, 80, 2, 1, "flechas"},  Case{80, 85, 2, 0x9d, "flechas"}, Case{85, 90, 2, 0x9f, "flechas"},
        Case{90, 95, 3, 0xab, "arco"}, Case{95, 100, 3, 0xa5, "arco"},
    };
    for (const Case& probe : cases)
    {
        SCOPED_TRACE(probe.action);
        for (const bool speaks : {true, false})
        {
            for (const std::int32_t map : {Devias, Lorencia})
            {
                SCOPED_TRACE(::testing::Message() << "habla " << speaks << " en el mapa " << map);
                const std::uint32_t seed = SeedWhere(
                    [&](CrtRandom& random)
                    {
                        const int choice = random.Next() % 100;
                        return choice >= probe.low && choice < probe.high &&
                               (random.Next() % probe.oneIn == 0) == speaks;
                    });
                m_world->Runtime().chat.bubbles = {};
                m_world->Runtime().map = map;
                Npc().animation.action = 1;
                UseTheSeed(seed);

                ASSERT_TRUE(Client::Monsters::FinishTheTurnedNpc(Npc(), NpcSlot, Context()).HasValue());

                EXPECT_EQ(Npc().animation.action, probe.action);
                EXPECT_EQ(Said(), speaks && map == Devias ? std::optional<std::string>(probe.said) : std::nullopt);
                EXPECT_EQ(m_random.State(), AfterDraws(seed, 2).State());
            }
        }
    }
}
} // namespace
