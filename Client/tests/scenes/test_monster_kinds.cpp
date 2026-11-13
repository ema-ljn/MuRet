#include "Scenes/MonsterKinds.h"

#include "Render/Models/Cloth.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <set>
#include <string>
#include <string_view>
#include <vector>

// Client::Scenes: las tablas de 0x5141e0 y de sus seis creadores de evento, lo que corre en cada mapa, los rand() y
// la cola 0x5140c0 (04, "Los monstruos y los NPC que entran").
namespace
{
using Client::Render::Models::CrtRandom;
using Client::Scenes::MonsterCreationContext;
using Client::Scenes::MonsterCreationOf;
using Client::Scenes::MonsterStep;
using Client::Scenes::MonsterStepKind;
using Client::Scenes::ResolveMonsterSteps;

constexpr std::int32_t Lorencia = 0;
constexpr std::int32_t Devias = 2;
constexpr std::int32_t Tarkan = 8;
constexpr std::int32_t DevilSquare = 9;
constexpr std::int32_t BloodCastle1 = 11;
constexpr std::int32_t BloodCastle7 = 17;
constexpr std::int32_t CastleSiege = 30;
constexpr std::int32_t Crywolf = 34;

std::vector<MonsterStep> Resolve(std::int32_t type, std::int32_t world, CrtRandom& random, std::int32_t x = 0)
{
    auto steps = ResolveMonsterSteps(MonsterCreationOf(type, world), MonsterCreationContext{world, x, false}, random);
    EXPECT_TRUE(steps.HasValue()) << (steps.HasValue() ? "" : steps.Error());
    return steps.HasValue() ? steps.Value() : std::vector<MonsterStep>{};
}

std::vector<MonsterStep> Resolve(std::int32_t type, std::int32_t world)
{
    CrtRandom random(1);
    return Resolve(type, world, random);
}

const MonsterStep* Find(const std::vector<MonsterStep>& steps, MonsterStepKind kind, std::uint16_t offset = 0)
{
    const auto found = std::ranges::find_if(
        steps, [&](const MonsterStep& step)
        { return step.kind == kind && (kind != MonsterStepKind::Write || step.offset == offset); });
    return found == steps.end() ? nullptr : &*found;
}

std::uint32_t Bits(float value)
{
    return std::bit_cast<std::uint32_t>(value);
}

// Cada caso crea una sola vez y carga lo que pide antes de crear; los tipos del switch van de 0 a 301 y los 124
// casos que no son el default aparecen todos.
TEST(MonsterKindsTest, EveryCaseCreatesOnceAfterItsLoads)
{
    std::set<std::uint32_t> cases;
    auto check = [](const Client::Scenes::MonsterKind& kind)
    {
        std::size_t creates = 0;
        for (const MonsterStep& step : kind.steps)
        {
            if (step.kind == MonsterStepKind::Create)
                ++creates;
            if ((step.kind == MonsterStepKind::LoadMonster || step.kind == MonsterStepKind::LoadNpc) && creates != 0)
                ADD_FAILURE() << "el tipo " << kind.type << " carga después de crear";
        }
        EXPECT_EQ(creates, 1u) << "el tipo " << kind.type;
    };
    for (const auto& kind : Client::Scenes::MainMonsterKinds())
    {
        EXPECT_GE(kind.type, 0);
        EXPECT_LE(kind.type, 301);
        check(kind);
        if (kind.address != 0x516e2a)
            cases.insert(kind.address);
    }
    EXPECT_EQ(cases.size(), 124u);
    check(Client::Scenes::DefaultMonsterKind());
    for (const auto& event : Client::Scenes::EventMonsterKinds())
        check(event.kind);
}

// 0x515a1f (Lorencia): Monster10 en el modelo 0x15b, el nombre de 0x6c8aa0 y la escala 0,4. Ningún rand().
TEST(MonsterKindsTest, ASimpleCaseLoadsCreatesNamesAndScales)
{
    CrtRandom random(7);
    const auto steps = Resolve(3, Lorencia, random);
    ASSERT_EQ(steps.size(), 4u);
    EXPECT_EQ(steps[0].kind, MonsterStepKind::LoadMonster);
    EXPECT_EQ(steps[0].value, 9u);
    EXPECT_EQ(steps[1].kind, MonsterStepKind::Create);
    EXPECT_EQ(steps[1].value, 0x15bu);
    EXPECT_EQ(steps[1].address, 0x515a36u);
    EXPECT_EQ(steps[2].kind, MonsterStepKind::Name);
    EXPECT_EQ(steps[2].extra, 0x6c8aa0u);
    EXPECT_EQ(steps[2].text.size(), 4u);
    EXPECT_EQ(steps[3].offset, 0xc);
    EXPECT_EQ(steps[3].value, Bits(0.4f));
    EXPECT_EQ(random.State(), CrtRandom(7).State());
}

// 0x515e74: el 14 es un jugador (0x20a) con el sufijo 209 en +4, +0x38a en 1 y las dos manos.
TEST(MonsterKindsTest, AGuardIsAPlayerModelWithItsSuffixAndHands)
{
    const auto steps = Resolve(14, Devias);
    ASSERT_NE(Find(steps, MonsterStepKind::Create), nullptr);
    EXPECT_EQ(Find(steps, MonsterStepKind::Create)->value, 0x20au);
    EXPECT_EQ(Find(steps, MonsterStepKind::LoadMonster), nullptr);
    ASSERT_NE(Find(steps, MonsterStepKind::Write, 0x4), nullptr);
    EXPECT_EQ(Find(steps, MonsterStepKind::Write, 0x4)->value, 209u);
    EXPECT_EQ(Find(steps, MonsterStepKind::Write, 0x38a)->value, 1u);
    EXPECT_EQ(Find(steps, MonsterStepKind::Write, 0x2b8)->value, 0x218u);
    EXPECT_EQ(Find(steps, MonsterStepKind::Write, 0x2d8)->value, 0xe16u);
}

// 0x516e2a: el default carga Monster01 y crea 0x152; el 0, el 4 y el 8 además escriben su nombre y su arma.
TEST(MonsterKindsTest, TheDefaultCreatesMonster01AndOnlyTheZeroFourAndEightAddMore)
{
    const auto plain = Resolve(399, Lorencia);
    ASSERT_EQ(plain.size(), 2u);
    EXPECT_EQ(plain[0].value, 0u);
    EXPECT_EQ(plain[1].value, 0x152u);
    EXPECT_EQ(MonsterCreationOf(76, Devias).kind.address, 0x516e2au);
    EXPECT_EQ(Resolve(76, Devias).size(), 2u);
    const auto four = Resolve(4, Lorencia);
    EXPECT_EQ(Find(four, MonsterStepKind::Write, 0x2b8)->value, 0x819u);
    EXPECT_EQ(Find(four, MonsterStepKind::Write, 0x1d6)->value, 1u);
    EXPECT_EQ(Find(Resolve(8, Lorencia), MonsterStepKind::Write, 0x78)->value, 1u);
}

// 0x51536b: el 34 viste (0x512930) y solo en Devil Square (el mapa 9) pone la escala en 1 después (0x51542a).
TEST(MonsterKindsTest, TheCursedWizardScalesOnlyInDevilSquare)
{
    const auto devias = Resolve(34, Devias);
    ASSERT_NE(Find(devias, MonsterStepKind::Dress), nullptr);
    EXPECT_EQ(Find(devias, MonsterStepKind::Write, 0xc), nullptr);
    const auto square = Resolve(34, DevilSquare);
    ASSERT_EQ(square.back().kind, MonsterStepKind::Write);
    EXPECT_EQ(square.back().offset, 0xc);
    EXPECT_EQ(square.back().value, Bits(1.0f));
    EXPECT_EQ(square.back().address, 0x51542au);
}

// 0x5146be..0x5146db: +0x2ba en 8 con (mapa − 10) / 3 en 0, que con la división de C cubre del 8 al 12 (Q-C846).
TEST(MonsterKindsTest, TheSpiritKnightWeaponLevelCoversTheMapsEightToTwelve)
{
    for (const std::int32_t world : {Tarkan, DevilSquare, 10, BloodCastle1, 12})
        EXPECT_EQ(Find(Resolve(88, world), MonsterStepKind::Write, 0x2ba)->value, 8u) << world;
    for (const std::int32_t world : {Lorencia, Devias, 7, 13, BloodCastle7})
        EXPECT_EQ(Find(Resolve(88, world), MonsterStepKind::Write, 0x2ba)->value, 0u) << world;
}

// 0x439df0 atiende Kalima (144 a 149 y los suyos) en cualquier mapa, antes del switch.
TEST(MonsterKindsTest, KalimaMonstersComeFromTheirCreatorInEveryMap)
{
    for (const std::int32_t world : {Lorencia, Devias, 24})
    {
        const auto creation = MonsterCreationOf(144, world);
        EXPECT_EQ(creation.creator, 0x439df0u);
        EXPECT_EQ(creation.kind.address, 0x439e1eu);
    }
    const auto steps = Resolve(144, Lorencia);
    EXPECT_EQ(steps[0].value, 0x3fu);
    EXPECT_EQ(steps[1].value, 0x191u);
}

// 0x42d070 solo en el mapa 30: el 104 crea el modelo 0xb; en otro mapa va al default.
TEST(MonsterKindsTest, TheSiegeCreatorOnlyRunsInMapThirty)
{
    EXPECT_EQ(MonsterCreationOf(104, CastleSiege).creator, 0x42d070u);
    const auto siege = Resolve(104, CastleSiege);
    ASSERT_EQ(siege.size(), 3u);
    EXPECT_EQ(siege[0].value, 0xbu);
    EXPECT_EQ(MonsterCreationOf(104, Lorencia).creator, 0x5141e0u);
    EXPECT_EQ(MonsterCreationOf(104, Lorencia).kind.address, 0x516e2au);
}

// 0x42d244: con el asedio prendido (0x7239ec) el 217 sigue otra rama, que corta; apagado, baja 100 la altura.
TEST(MonsterKindsTest, WithTheSiegeOnTheGateCuts)
{
    CrtRandom random(1);
    const auto creation = MonsterCreationOf(217, CastleSiege);
    const auto on = ResolveMonsterSteps(creation, MonsterCreationContext{CastleSiege, 0, true}, random);
    ASSERT_FALSE(on.HasValue());
    EXPECT_NE(on.Error().find("0x42d244"), std::string::npos);
    const auto off = Resolve(217, CastleSiege);
    ASSERT_EQ(off.back().kind, MonsterStepKind::Lower);
    EXPECT_EQ(off.back().value, Bits(100.0f));
}

// 0x4313cc..0x4313ee: en Crywolf el 348 mira a 180 al crearse y, con la X en 62 o en 183, gira +0x24 a 90.
TEST(MonsterKindsTest, TheCrywolfStatueTurnsAtTwoColumns)
{
    for (const std::int32_t x : {62, 183})
    {
        CrtRandom random(1);
        const auto steps = Resolve(348, Crywolf, random, x);
        ASSERT_NE(Find(steps, MonsterStepKind::Write, 0x24), nullptr) << x;
        EXPECT_EQ(Find(steps, MonsterStepKind::Write, 0x24)->value, Bits(90.0f));
        EXPECT_EQ(Find(steps, MonsterStepKind::Create)->extra, Bits(180.0f));
    }
    CrtRandom random(1);
    EXPECT_EQ(Find(Resolve(348, Crywolf, random, 100), MonsterStepKind::Write, 0x24), nullptr);
    EXPECT_EQ(MonsterCreationOf(348, Lorencia).kind.address, 0x516e2au);
}

// 0x51443f: un rand() antes de cargar; con r par, Monster72 (0x47), el modelo 0x199 y el arma 0xa25; con r impar,
// Monster73, 0x19a y 0xc17.
TEST(MonsterKindsTest, TheChaosCastleSoldierDrawsOneRandomBeforeLoading)
{
    std::uint32_t even = 0;
    std::uint32_t odd = 0;
    for (std::uint32_t seed = 1; seed < 8; ++seed)
    {
        CrtRandom random(seed);
        CrtRandom copy(seed);
        const std::int32_t r = copy.Next() % 2;
        const auto steps = Resolve(163, 19, random);
        EXPECT_EQ(random.State(), copy.State());
        ASSERT_EQ(steps[0].kind, MonsterStepKind::Random);
        EXPECT_EQ(steps[1].value, 0x47u + static_cast<std::uint32_t>(r));
        EXPECT_EQ(steps[2].value, 0x199u + static_cast<std::uint32_t>(r));
        EXPECT_EQ(steps.back().value, 0u);
        EXPECT_EQ(steps[steps.size() - 2].value, r == 0 ? 0xa25u : 0xc17u);
        (r == 0 ? even : odd) += 1;
    }
    EXPECT_GT(even, 0u);
    EXPECT_GT(odd, 0u);
}

// 0x514358: el 224 saca su rand() después de la escala y lo suma a 10 en +4.
TEST(MonsterKindsTest, TheGuardsmanDrawsAfterItsScale)
{
    CrtRandom random(3);
    CrtRandom copy(3);
    const auto steps = Resolve(224, Lorencia, random);
    const auto draw =
        std::ranges::find_if(steps, [](const MonsterStep& step) { return step.kind == MonsterStepKind::Random; });
    ASSERT_NE(draw, steps.end());
    EXPECT_EQ((draw - 1)->offset, 0xc);
    EXPECT_EQ((draw + 1)->offset, 0x4);
    EXPECT_EQ((draw + 1)->value, 10u + static_cast<std::uint32_t>(copy.Next() % 2));
}

// 0x514818: el tipo 68 (el caso 0x3a) carga Monster50 (0x612d90(0x31)), que el Data del laboratorio no trae: el
// original se cierra al cargarlo (0x612dd6..0x612de1, 09). La carga corta en la fase 2.
TEST(MonsterKindsTest, TypeSixtyEightAsksForMonster50)
{
    const auto steps = Resolve(68, Tarkan);
    ASSERT_EQ(steps[0].kind, MonsterStepKind::LoadMonster);
    EXPECT_EQ(steps[0].value, 0x31u);
    EXPECT_EQ(steps[0].address, 0x51481au);
    EXPECT_EQ(Find(steps, MonsterStepKind::Create)->value, 0x183u);
}

// 0x5140c0: la cola.
TEST(MonsterKindsTest, TheTailLevelsBloodCastleAndClassifiesTheObject)
{
    using Client::Scenes::MonsterTailOf;
    EXPECT_FALSE(MonsterTailOf(0x54, DevilSquare).clearsLevel);
    EXPECT_TRUE(MonsterTailOf(0x54, 10).clearsLevel);
    EXPECT_EQ(MonsterTailOf(0x54, 10).scaleSteps, 0);
    EXPECT_EQ(MonsterTailOf(0x8f, BloodCastle7).scaleSteps, 2);
    EXPECT_FALSE(MonsterTailOf(0x90, BloodCastle7).clearsLevel);
    EXPECT_FALSE(MonsterTailOf(0x54, 18).clearsLevel);
    EXPECT_EQ(MonsterTailOf(200, Lorencia).objectClass, 2);
    EXPECT_EQ(MonsterTailOf(201, Lorencia).objectClass, 4);
    EXPECT_EQ(MonsterTailOf(259, Lorencia).objectClass, 4);
    EXPECT_EQ(MonsterTailOf(260, Lorencia).objectClass, 2);
    EXPECT_EQ(MonsterTailOf(100, Lorencia).objectClass, 8);
    EXPECT_EQ(MonsterTailOf(110, Lorencia).objectClass, 8);
    EXPECT_EQ(MonsterTailOf(111, Lorencia).objectClass, 2);
    EXPECT_EQ(MonsterTailOf(99, Lorencia).objectClass, 2);
    const float expected = static_cast<float>(2.0 * static_cast<double>(0.05f) + 1.25);
    EXPECT_EQ(Client::Scenes::AddTailScale(1.25f, 2), expected);
}
} // namespace
