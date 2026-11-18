#include "Scenes/NpcChatter.h"

#include "Core/Expected.h"
#include "Data/Models/PlayerModels.h"
#include "Data/Text/TextTable.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/Skeleton.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Scenes/ActorState.h"
#include "Scenes/CharacterStep.h"
#include "Scenes/ChatBubbles.h"
#include "Scenes/SceneId.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"

#include "support/PlayerActors.h"
#include "support/SceneDataFiles.h"

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

// Lo que dicen los NPC (CL9 hito 2): el giro de 0x4fcd0c, 0x4fc790, el mensaje del nombre (0x4fcd65..0x4fce15) y
// 0x4fc8c0. Los textos son inventados; los índices y las ramas salen del ASM.
namespace
{
using Client::Render::Models::CrtRandom;
using Client::Scenes::ActorState;
using Client::Scenes::CharacterStepContext;
using Client::Scenes::ChatBubble;
using Client::Scenes::WorldState;

constexpr std::size_t NpcSlot = 9;
constexpr std::int32_t Lorencia = 0;
constexpr std::int32_t Devias = 2;
constexpr std::int32_t CastleSiege = 30;
// 0x6c88b0, el nombre del tipo 236.
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

// La primera semilla desde 1 cuyos rand() cumplen `wanted`.
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
                                    .scene = Client::Scenes::SceneId::Game,
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

    // Lo que dice el NPC, el renglón de abajo de su globo; vacío si no tiene.
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
    CrtRandom m_random{1};
    Client::Render::Models::Skeleton m_skeleton = Client::Tests::PlayerActionsSkeleton();
    Client::Data::Text::TextTable m_texts = Client::Tests::MakeTextTable({{1974, "pociones"},
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

// 0x4fcd0c..0x4fcd55: con +0x88 en 4, en Lorencia y Devias, el 0x20a de subtipo 0xd1 a 0xd3 mira a 90 o a 0.
TEST_F(NpcChatterTest, TheArcherTurnsInLorenciaAndDevias)
{
    Npc().angles = {10.0f, 20.0f, 30.0f};
    EXPECT_TRUE(Client::Scenes::TurnTheNpc(Npc(), Lorencia));
    EXPECT_EQ(Npc().angles, glm::vec3(0.0f, 0.0f, 90.0f));
    EXPECT_TRUE(Client::Scenes::TurnTheNpc(Npc(), Devias));
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
        EXPECT_FALSE(Client::Scenes::TurnTheNpc(npc, probe.map));
        EXPECT_EQ(npc.angles, glm::vec3(10.0f, 20.0f, 30.0f));
    }
}

// 0x4fc790: con +0x88 en 4, un rand() par y el tipo, su mensaje con el globo del chat (0x557170 con 0 y −1).
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
        Case{Client::Data::Models::PlayerSkeletonModel, 0x101, "fuerza"},
        Case{Client::Data::Models::PlayerSkeletonModel, 0x100, std::nullopt},
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

        const auto said = Client::Scenes::SayTheNpcLine(Npc(), NpcSlot, Context());

        ASSERT_TRUE(said.HasValue()) << said.Error();
        EXPECT_EQ(Said(), probe.said);
        EXPECT_EQ(m_random.State(), AfterDraws(even, 1).State());
    }
}

// Un rand() impar no dice nada; sin +0x88 en 4 ni lo saca; el 0x1cd calla en el mapa 30.
TEST_F(NpcChatterTest, AnOddRandomOrAnotherKindSaysNothing)
{
    Npc().type = 0x1d3;
    const std::uint32_t odd = SeedWhere([](CrtRandom& random) { return random.Next() % 2 != 0; });
    UseTheSeed(odd);
    ASSERT_TRUE(Client::Scenes::SayTheNpcLine(Npc(), NpcSlot, Context()).HasValue());
    EXPECT_EQ(Said(), std::nullopt);
    EXPECT_EQ(m_random.State(), AfterDraws(odd, 1).State());

    Npc().byte88 = 2;
    UseTheSeed(odd);
    ASSERT_TRUE(Client::Scenes::SayTheNpcLine(Npc(), NpcSlot, Context()).HasValue());
    EXPECT_EQ(m_random.State(), AfterDraws(odd, 0).State());

    Npc().byte88 = 4;
    Npc().type = 0x1cd;
    m_world->Runtime().map = CastleSiege;
    const std::uint32_t even = SeedWhere([](CrtRandom& random) { return random.Next() % 2 == 0; });
    UseTheSeed(even);
    ASSERT_TRUE(Client::Scenes::SayTheNpcLine(Npc(), NpcSlot, Context()).HasValue());
    EXPECT_EQ(Said(), std::nullopt);
    EXPECT_EQ(m_random.State(), AfterDraws(even, 1).State());
}

// Sin Text.bmd o sin el medidor, el que tiene que hablar corta con la dirección de su 0x557170.
TEST_F(NpcChatterTest, WithoutTheTextOrTheMeasureTheSpeakerStops)
{
    Npc().type = 0x1d5;
    const std::uint32_t even = SeedWhere([](CrtRandom& random) { return random.Next() % 2 == 0; });
    m_withTexts = false;
    UseTheSeed(even);
    const auto withoutTexts = Client::Scenes::SayTheNpcLine(Npc(), NpcSlot, Context());
    ASSERT_FALSE(withoutTexts.HasValue());
    EXPECT_NE(withoutTexts.Error().find("0x4fc838"), std::string::npos) << withoutTexts.Error();

    m_withTexts = true;
    m_withMeasure = false;
    UseTheSeed(even);
    const auto withoutMeasure = Client::Scenes::SayTheNpcLine(Npc(), NpcSlot, Context());
    ASSERT_FALSE(withoutMeasure.HasValue());
    EXPECT_NE(withoutMeasure.Error().find("0x4fc838"), std::string::npos) << withoutMeasure.Error();
    EXPECT_EQ(Said(), std::nullopt);
}

// 0x4fcd65..0x4fce15: la primera vez el reloj arranca 5.000 ms atrás, así que el primero habla enseguida, con el 1168
// (0x743a190 en 0). Después, más de 4.000 ms sin signo, y los mensajes se turnan.
TEST_F(NpcChatterTest, TheNamedLineTakesTurnsEveryFourSeconds)
{
    TheMessageName();
    m_now = 1000;
    ASSERT_TRUE(Client::Scenes::SayTheNamedLine(Npc(), NpcSlot, Context()).HasValue());
    EXPECT_EQ(m_clockReads, 2);
    EXPECT_EQ(Said(), "evento dos");
    EXPECT_EQ(m_world->Runtime().namedLineTime, 1000u);
    EXPECT_TRUE(m_world->Runtime().namedLineFirst);

    m_world->Runtime().chat.bubbles = {};
    m_now = 5000;
    ASSERT_TRUE(Client::Scenes::SayTheNamedLine(Npc(), NpcSlot, Context()).HasValue());
    EXPECT_EQ(m_clockReads, 3);
    EXPECT_EQ(Said(), std::nullopt);
    EXPECT_EQ(m_world->Runtime().namedLineTime, 1000u);

    m_now = 5001;
    ASSERT_TRUE(Client::Scenes::SayTheNamedLine(Npc(), NpcSlot, Context()).HasValue());
    EXPECT_EQ(Said(), "evento uno");
    EXPECT_EQ(m_world->Runtime().namedLineTime, 5001u);
    EXPECT_FALSE(m_world->Runtime().namedLineFirst);
}

// 0x4fcda2: el strcmp sigue después de los once bytes iguales y compara +0x1e4 con el 0 de 0x6c88b0. Con algo ahí (lo
// que deja un nombre largo, Q-C847), el nombre no es el del mensaje; un byte de menos tampoco lo es.
TEST_F(NpcChatterTest, TheMessageNameNeedsTheZeroAfterItsElevenBytes)
{
    TheMessageName();
    Npc().nameSpill[0] = std::byte{0};
    EXPECT_TRUE(Client::Scenes::HasTheMessageName(Npc()));
    Npc().nameSpill[0] = std::byte{'s'};
    EXPECT_FALSE(Client::Scenes::HasTheMessageName(Npc()));
    Npc().nameSpill[0] = std::byte{0};
    Npc().byte1e3 = 0;
    EXPECT_FALSE(Client::Scenes::HasTheMessageName(Npc()));

    // Con +0x1e4 en algo, SayTheNamedLine no habla aunque pasen los 4.000 ms.
    TheMessageName();
    Npc().nameSpill[0] = std::byte{'s'};
    m_now = 1000;
    ASSERT_TRUE(Client::Scenes::SayTheNamedLine(Npc(), NpcSlot, Context()).HasValue());
    EXPECT_EQ(Said(), std::nullopt);
}

// El reloj arranca con cualquier personaje, aunque no tenga el nombre; antes de las 5.000 ms del arranque la resta da
// la vuelta sin signo. Sin timeGetTime() no se toca y solo el del nombre corta.
TEST_F(NpcChatterTest, TheClockStartsWithAnyoneAndWithoutItOnlyTheNameStops)
{
    m_now = 300;
    ASSERT_TRUE(Client::Scenes::SayTheNamedLine(Npc(), NpcSlot, Context()).HasValue());
    EXPECT_EQ(m_world->Runtime().namedLineTime, 300u - 5000u);
    EXPECT_EQ(Said(), std::nullopt);
    EXPECT_FALSE(m_world->Runtime().namedLineFirst);

    m_withClock = false;
    m_world->Runtime().namedLineTime.reset();
    ASSERT_TRUE(Client::Scenes::SayTheNamedLine(Npc(), NpcSlot, Context()).HasValue());
    EXPECT_FALSE(m_world->Runtime().namedLineTime.has_value());
    TheMessageName();
    const auto named = Client::Scenes::SayTheNamedLine(Npc(), NpcSlot, Context());
    ASSERT_FALSE(named.HasValue());
    EXPECT_NE(named.Error().find("0x4fcd82"), std::string::npos) << named.Error();
    EXPECT_EQ(Said(), std::nullopt);
}

// 0x4fc8c0 fuera de la acción 1: el rand() % 100 sale igual y vuelve a la 1.
TEST_F(NpcChatterTest, TheTurnedArcherGoesBackToRest)
{
    Npc().animation.action = 0x9d;
    UseTheSeed(7);
    ASSERT_TRUE(Client::Scenes::FinishTheTurnedNpc(Npc(), NpcSlot, Context()).HasValue());
    EXPECT_EQ(Npc().animation.action, 1);
    EXPECT_EQ(Said(), std::nullopt);
    EXPECT_EQ(m_random.State(), AfterDraws(7, 1).State());
}

// 0x4fc8c0 en la acción 1: el tramo del rand() % 100 da la acción y, en Devias, con el segundo rand() par (o múltiplo
// de 3 desde 90), el 904 o el 905. El segundo rand() sale también en Lorencia, que no habla.
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

                ASSERT_TRUE(Client::Scenes::FinishTheTurnedNpc(Npc(), NpcSlot, Context()).HasValue());

                EXPECT_EQ(Npc().animation.action, probe.action);
                EXPECT_EQ(Said(), speaks && map == Devias ? std::optional<std::string>(probe.said) : std::nullopt);
                EXPECT_EQ(m_random.State(), AfterDraws(seed, 2).State());
            }
        }
    }
}
} // namespace
