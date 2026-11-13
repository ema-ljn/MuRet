#include "Scenes/MonsterStepCases.h"

#include "Core/Expected.h"
#include "Data/Models/PlayerModels.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Render/Models/Skeleton.h"
#include "Scenes/ActorState.h"
#include "Scenes/GameCharacters.h"
#include "Scenes/MonsterCreation.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldTexts.h"
#include "World/TerrainWalls.h"
#include "support/SceneDataFiles.h"

#include <gtest/gtest.h>

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Los casos por tipo del paso de los personajes que no son el 0x20a (CL9 hito 2): el nombre del 0xc5 (0x4fd603), la
// caída del 0xf1 (0x4fb910), 0x42a020 y los objetos del mapa 30 de 0x42da90, y 0x42da60 al final del dibujo. Vectores
// literales del ASM c32bbd5f, sin ejecutar el original; los nombres son inventados.
namespace
{
using Client::Scenes::ActorState;
using Client::Scenes::CastleObjectStep;
using Client::Scenes::WorldRuntime;

constexpr std::size_t LevelText = 1176;

ActorState NamedGate(std::string_view name, std::uint16_t level)
{
    ActorState gate;
    gate.type = 0xc5;
    gate.level = level;
    gate.position = {1200.0f, 3400.0f, 150.0f};
    std::vector<std::byte> bytes;
    for (const char letter : name)
        bytes.push_back(static_cast<std::byte>(letter));
    bytes.push_back(std::byte{0});
    EXPECT_TRUE(Client::Scenes::PutActorText(gate, bytes, 0x4fd612).HasValue());
    return gate;
}

std::string QueuedText(const Client::Scenes::WorldText& entry)
{
    const auto end = std::ranges::find(entry.text, '\0');
    return {entry.text.begin(), end};
}

// 0x4fd603..0x4fd698: "%s ." con el nombre, Text[1176] con +0x1d6 sin signo y el strcat de los dos, a la cola de
// 0x4381f0 con la posición del objeto.
TEST(MonsterStepCasesTest, TheKalimaGateQueuesItsNameAndLevel)
{
    const auto texts = Client::Tests::MakeTextTable({{LevelText, "Nivel %d"}});
    const ActorState gate = NamedGate("Portal", 3);
    Client::Scenes::WorldTextQueue queue;

    const auto queued = Client::Scenes::QueueTheGateName(gate, &texts, queue);

    ASSERT_TRUE(queued.HasValue()) << queued.Error();
    ASSERT_EQ(queue.size(), 1u);
    EXPECT_EQ(QueuedText(queue.front()), "Portal .Nivel 3");
    EXPECT_EQ(queue.front().position, gate.position);
}

// El strcat escribe en el buffer de 100 bytes (0xf8 − 0x94): 99 letras y el 0 entran; con una más el original pisaría
// el segundo buffer, y MuRet corta. Sin Text.bmd, también.
TEST(MonsterStepCasesTest, TheGateNameCutsWithoutTextsOrPastItsBuffer)
{
    const auto texts = Client::Tests::MakeTextTable({{LevelText, std::string(90, 'x') + "%d"}});
    Client::Scenes::WorldTextQueue queue;

    ASSERT_TRUE(Client::Scenes::QueueTheGateName(NamedGate("Portal", 3), &texts, queue).HasValue());
    ASSERT_EQ(queue.size(), 1u);
    // 0x438209: 0x4381f0 copia 64 bytes (los 0x10 dwords).
    EXPECT_EQ(QueuedText(queue.front()), "Portal ." + std::string(56, 'x'));

    const auto overflow = Client::Scenes::QueueTheGateName(NamedGate("Portal", 10), &texts, queue);
    ASSERT_FALSE(overflow.HasValue());
    EXPECT_NE(overflow.Error().find("0x4fd67c"), std::string::npos) << overflow.Error();
    EXPECT_NE(overflow.Error().find("101"), std::string::npos) << overflow.Error();

    const auto missing = Client::Scenes::QueueTheGateName(NamedGate("Portal", 3), nullptr, queue);
    ASSERT_FALSE(missing.HasValue());
    EXPECT_NE(missing.Error().find("0x4fd63b"), std::string::npos) << missing.Error();
    EXPECT_EQ(queue.size(), 1u);
    // Un Text.bmd con el 1176 vacío deja el nombre solo.
    const auto empty = Client::Tests::MakeTextTable({{1, "otro"}});
    ASSERT_TRUE(Client::Scenes::QueueTheGateName(NamedGate("Portal", 3), &empty, queue).HasValue());
    ASSERT_EQ(queue.size(), 2u);
    EXPECT_EQ(QueuedText(queue.back()), "Portal .");
}

// 0x4fb910..0x4fb9c7: la z sube +0xe4 y +0xe4 baja 6; con el piso más 30 por encima, la z pasa a él y +0xe4 rebota a
// −0,4f; los ángulos suman +0xcc, +0xd0 y +0xd4, que se achican a 0,8f.
TEST(MonsterStepCasesTest, TheBallFallsBouncesAndSpins)
{
    ActorState ball;
    ball.type = 0xf1;
    ball.position = {500.0f, 700.0f, 260.0f};
    ball.floatE4 = -10.0f;
    ball.angles = {1.0f, 2.0f, 3.0f};
    ball.floatCC = 10.0f;
    ball.floatD0 = -5.0f;
    ball.floatD4 = 2.5f;
    std::vector<glm::vec2> asked;
    const auto ground = [&asked](float x, float y)
    {
        asked.emplace_back(x, y);
        return 200.0;
    };

    // En el aire: 260 − 10 = 250, sobre los 230 del piso.
    Client::Scenes::StepTheFallingBall(ball, ground);
    EXPECT_EQ(ball.position.z, 250.0f);
    EXPECT_EQ(ball.floatE4, -16.0f);
    EXPECT_EQ(ball.angles, glm::vec3(11.0f, -3.0f, 5.5f));
    EXPECT_EQ(ball.floatCC, 8.0f);
    EXPECT_EQ(ball.floatD0, -4.0f);
    EXPECT_EQ(ball.floatD4, 2.0f);
    ASSERT_EQ(asked.size(), 1u);
    EXPECT_EQ(asked[0], glm::vec2(500.0f, 700.0f));

    // 250 − 16 = 234, todavía arriba; después 234 − 22 = 212: el piso (230) la sube y +0xe4 rebota.
    Client::Scenes::StepTheFallingBall(ball, ground);
    EXPECT_EQ(ball.position.z, 234.0f);
    Client::Scenes::StepTheFallingBall(ball, ground);
    EXPECT_EQ(ball.position.z, 230.0f);
    EXPECT_EQ(ball.floatE4, static_cast<float>(-28.0 * static_cast<double>(-0.4f)));

    // Justo en el piso no rebota: el fcom pide el piso estrictamente arriba.
    ActorState resting;
    resting.position = {0.0f, 0.0f, 236.0f};
    resting.floatE4 = -6.0f;
    Client::Scenes::StepTheFallingBall(resting, ground);
    EXPECT_EQ(resting.position.z, 230.0f);
    EXPECT_EQ(resting.floatE4, -12.0f);
}

ActorState Player(glm::vec3 position)
{
    ActorState player;
    player.alive = true;
    player.byte88 = 1;
    player.type = Client::Data::Models::PlayerSkeletonModel;
    player.seen = true;
    player.position = position;
    return player;
}

// 0x42a020: solo los jugadores (+0x88 en 1, el 0x20a) que se ven (+0x16c); dx y dy son el objeto menos el jugador.
TEST(MonsterStepCasesTest, PlayerNearMeasuresByItsMode)
{
    ActorState object;
    object.position = {1000.0f, 1000.0f, 0.0f};
    auto pool = std::make_unique<std::array<ActorState, 401>>();
    const auto near = [&](std::uint8_t mode, float range)
    { return Client::Scenes::PlayerNear(object, *pool, mode, range); };

    // El modo 1: |dx| debajo de 1 y dy en (0, rango).
    (*pool)[5] = Player({1000.5f, 600.0f, 0.0f});
    EXPECT_TRUE(near(1, 500.0f));
    EXPECT_FALSE(near(1, 400.0f));
    (*pool)[5].position.x = 1001.0f;
    EXPECT_FALSE(near(1, 500.0f));
    (*pool)[5].position = {1000.0f, 1000.0f, 0.0f};
    EXPECT_FALSE(near(1, 500.0f));
    (*pool)[5].position = {1000.0f, 1100.0f, 0.0f};
    EXPECT_FALSE(near(1, 500.0f));

    // El modo 0: dx debajo del rango y dy debajo de 100, sin valor absoluto.
    (*pool)[5].position = {5000.0f, 950.0f, 0.0f};
    EXPECT_TRUE(near(0, 10.0f));
    (*pool)[5].position = {5000.0f, 900.0f, 0.0f};
    EXPECT_FALSE(near(0, 10.0f));
    (*pool)[5].position = {900.0f, 950.0f, 0.0f};
    EXPECT_FALSE(near(0, 100.0f));

    // El modo 2: la distancia en el plano; otro modo nunca.
    (*pool)[5].position = {1030.0f, 1040.0f, 0.0f};
    EXPECT_TRUE(near(2, 50.5f));
    EXPECT_FALSE(near(2, 50.0f));
    EXPECT_FALSE(near(3, 1.0e9f));

    // Un NaN pasa las comparaciones de "debajo" (C0) y no la de dy > 0.
    (*pool)[5].position = {std::numeric_limits<float>::quiet_NaN(), 600.0f, 0.0f};
    EXPECT_TRUE(near(1, 500.0f));
    EXPECT_TRUE(near(2, 1.0f));
    (*pool)[5].position = {1000.0f, std::numeric_limits<float>::quiet_NaN(), 0.0f};
    EXPECT_FALSE(near(1, 500.0f));

    // Los que no cuentan: otro +0x88, otro modelo, sin verse o fuera de los 400.
    (*pool)[5] = Player({1000.0f, 600.0f, 0.0f});
    (*pool)[5].byte88 = 2;
    EXPECT_FALSE(near(1, 500.0f));
    (*pool)[5] = Player({1000.0f, 600.0f, 0.0f});
    (*pool)[5].type = 0x153;
    EXPECT_FALSE(near(1, 500.0f));
    (*pool)[5] = Player({1000.0f, 600.0f, 0.0f});
    (*pool)[5].seen = false;
    EXPECT_FALSE(near(1, 500.0f));
    (*pool)[5] = ActorState{};
    (*pool)[400] = Player({1000.0f, 600.0f, 0.0f});
    EXPECT_FALSE(near(1, 500.0f));
    (*pool)[399] = Player({1000.0f, 600.0f, 0.0f});
    EXPECT_TRUE(near(1, 500.0f));
}

class CastleObjectTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_runtime = std::make_unique<WorldRuntime>();
        m_runtime->map = 30;
        m_pool = std::make_unique<std::array<ActorState, 400>>();
        m_skeleton.actions.assign(6,
                                  Client::Render::Models::ActionInfo{.repeats = true, .speed = 0.25f, .keyCount = 10});
    }

    CastleObjectStep Step()
    {
        return {.runtime = *m_runtime,
                .pool = *m_pool,
                .height = [](float x, float y) { return static_cast<double>(x + y) * 0.5; },
                .skeleton = &m_skeleton};
    }

    static ActorState Object(std::uint16_t monster)
    {
        ActorState object;
        object.alive = true;
        object.type = 0x19c;
        object.word376 = monster;
        object.position = {300.0f, 500.0f, 10.0f};
        return object;
    }

    std::unique_ptr<WorldRuntime> m_runtime;
    std::unique_ptr<std::array<ActorState, 400>> m_pool;
    Client::Render::Models::Skeleton m_skeleton;
};

// 0x42da9a: fuera del mapa 30 ninguno hace nada (Lorencia, Devias y Kalima).
TEST_F(CastleObjectTest, OutsideTheCastleMapNothingMoves)
{
    for (const std::int32_t map : {0, 2, 24})
    {
        m_runtime->map = map;
        for (const std::uint16_t monster : std::array<std::uint16_t, 6>{216, 277, 278, 283, 286, 287})
        {
            SCOPED_TRACE(testing::Message() << map << ' ' << monster);
            ActorState object = Object(monster);
            object.byte42d = 0x80;
            object.flags78 = 1;
            const ActorState before = object;
            const auto stepped = Client::Scenes::StepTheCastleObject(object, Step());
            ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
            EXPECT_EQ(object.position, before.position);
            EXPECT_EQ(object.dword100, before.dword100);
            EXPECT_EQ(object.byte42c, before.byte42c);
            EXPECT_EQ(m_runtime->castleGatesClosed, 0);
            EXPECT_EQ(m_runtime->castleMarkX, 0.0f);
            EXPECT_EQ(m_runtime->castleFlags7239e4, 0u);
        }
    }
}

// El 216 (0x42dacb): 200 sobre el terreno, +0x78 a 0x7239e4 y +0x42c en 1 si +0x78 no es 0; sin la altura corta.
TEST_F(CastleObjectTest, TheBannerStandsOnTheGroundAndCopiesItsFlags)
{
    ActorState banner = Object(216);
    banner.flags78 = 0x40;
    ASSERT_TRUE(Client::Scenes::StepTheCastleObject(banner, Step()).HasValue());
    EXPECT_EQ(banner.position.z, 600.0f);
    EXPECT_EQ(m_runtime->castleFlags7239e4, 0x40u);
    EXPECT_EQ(banner.byte42c, 1);

    banner.flags78 = 0;
    ASSERT_TRUE(Client::Scenes::StepTheCastleObject(banner, Step()).HasValue());
    EXPECT_EQ(m_runtime->castleFlags7239e4, 0u);
    EXPECT_EQ(banner.byte42c, 0);

    CastleObjectStep flat = Step();
    flat.height = {};
    const auto cut = Client::Scenes::StepTheCastleObject(banner, flat);
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x42dad6"), std::string::npos) << cut.Error();
}

// El 277 cierra las puertas del frame (0x7239d9), el 278 en el modo 0x80 guarda su x y su y, y el 283 sigue al asedio.
TEST_F(CastleObjectTest, TheGateLockTheMarkAndTheSiegeSwitch)
{
    ActorState lock = Object(277);
    ASSERT_TRUE(Client::Scenes::StepTheCastleObject(lock, Step()).HasValue());
    EXPECT_EQ(m_runtime->castleGatesClosed, 1);

    ActorState mark = Object(278);
    mark.byte42d = 0x40;
    ASSERT_TRUE(Client::Scenes::StepTheCastleObject(mark, Step()).HasValue());
    EXPECT_EQ(m_runtime->castleMarkX, 0.0f);
    mark.byte42d = 0x80;
    ASSERT_TRUE(Client::Scenes::StepTheCastleObject(mark, Step()).HasValue());
    EXPECT_EQ(m_runtime->castleMarkX, 300.0f);
    EXPECT_EQ(m_runtime->castleMarkY, 500.0f);

    ActorState lever = Object(283);
    ASSERT_TRUE(Client::Scenes::StepTheCastleObject(lever, Step()).HasValue());
    EXPECT_EQ(lever.dword58, -2);
    EXPECT_EQ(lever.byte42c, 0);
    m_runtime->siegeActive = true;
    ASSERT_TRUE(Client::Scenes::StepTheCastleObject(lever, Step()).HasValue());
    EXPECT_EQ(lever.dword58, -1);
    EXPECT_EQ(lever.byte42c, 1);
}

// El 286 y el 287: un jugador justo enfrente (modo 1, 500) abre (la acción 4, 0x7239d8 en 2, +0x110 en 1 y +0x100 en
// 300); +0x100 baja 1 en cada paso y, vencido sin nadie, cierra con la acción 0. Con 0x7239d8 en 2 abre sin nadie.
TEST_F(CastleObjectTest, TheGatesOpenForAPlayerInFrontAndCloseWhenTheTimeRunsOut)
{
    for (const std::uint16_t monster : std::array<std::uint16_t, 2>{286, 287})
    {
        SCOPED_TRACE(monster);
        SetUp();
        ActorState gate = Object(monster);
        gate.dword100 = 77;
        ASSERT_TRUE(Client::Scenes::StepTheCastleObject(gate, Step()).HasValue());
        EXPECT_EQ(gate.byte110, 0);
        EXPECT_EQ(gate.dword100, -1);
        EXPECT_EQ(gate.animation.action, 0);

        (*m_pool)[12] = Player({300.0f, 200.0f, 0.0f});
        ASSERT_TRUE(Client::Scenes::StepTheCastleObject(gate, Step()).HasValue());
        EXPECT_EQ(gate.animation.action, 4);
        EXPECT_EQ(m_runtime->castleByte7239d8, 2);
        EXPECT_EQ(gate.byte110, 1);
        EXPECT_EQ(gate.dword100, 299);

        // Con alguien enfrente al vencer, vuelve a 300.
        gate.dword100 = -1;
        ASSERT_TRUE(Client::Scenes::StepTheCastleObject(gate, Step()).HasValue());
        EXPECT_EQ(gate.dword100, 299);
        EXPECT_EQ(gate.animation.action, 4);

        // Sin nadie, cierra.
        (*m_pool)[12] = ActorState{};
        gate.dword100 = -1;
        ASSERT_TRUE(Client::Scenes::StepTheCastleObject(gate, Step()).HasValue());
        EXPECT_EQ(gate.animation.action, 0);
        EXPECT_EQ(gate.byte110, 0);
        EXPECT_EQ(m_runtime->castleByte7239d8, 0);
        EXPECT_EQ(gate.dword100, -2);

        // Otra puerta abierta (0x7239d8 en 2) abre esta también.
        m_runtime->castleByte7239d8 = 2;
        ASSERT_TRUE(Client::Scenes::StepTheCastleObject(gate, Step()).HasValue());
        EXPECT_EQ(gate.animation.action, 4);
        EXPECT_EQ(gate.dword100, 299);
    }
}

// La acción 5 (abriéndose) no cierra; 0x7239d8 en 0 cierra antes de que venza; el dec de +0x100 da la vuelta en el
// mínimo; sin el modelo, el cambio de acción corta.
TEST_F(CastleObjectTest, TheGateEdges)
{
    ActorState gate = Object(286);
    gate.byte110 = 1;
    gate.dword100 = -5;
    gate.animation.action = 5;
    ASSERT_TRUE(Client::Scenes::StepTheCastleObject(gate, Step()).HasValue());
    EXPECT_EQ(gate.byte110, 1);
    EXPECT_EQ(gate.dword100, -6);

    gate.animation.action = 4;
    gate.dword100 = 100;
    m_runtime->castleByte7239d8 = 0;
    ASSERT_TRUE(Client::Scenes::StepTheCastleObject(gate, Step()).HasValue());
    EXPECT_EQ(gate.byte110, 0);
    EXPECT_EQ(gate.animation.action, 0);

    gate.byte110 = 1;
    gate.animation.action = 5;
    gate.dword100 = std::numeric_limits<std::int32_t>::min();
    ASSERT_TRUE(Client::Scenes::StepTheCastleObject(gate, Step()).HasValue());
    EXPECT_EQ(gate.dword100, std::numeric_limits<std::int32_t>::max());

    CastleObjectStep bare = Step();
    bare.skeleton = nullptr;
    ActorState closed = Object(287);
    m_runtime->castleByte7239d8 = 2;
    const auto cut = Client::Scenes::StepTheCastleObject(closed, bare);
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x42db87"), std::string::npos) << cut.Error();
}

// 0x42da60 al final del dibujo: 0x7239dc y 0x7239e0 en 0; sin el 277, las puertas del TerrainWall se abren; con él,
// quedan, y 0x7239d9 vuelve a 0 para el frame siguiente.
TEST(MonsterStepCasesTest, TheEndOfTheFrameOpensTheGatesUnlessTheLockStepped)
{
    const Client::World::GateCell gate = Client::World::GateCells.front();
    const auto cell = Client::Data::Terrain::CellIndex(gate.x, static_cast<std::uint8_t>(gate.y + 1));
    auto walls = std::make_unique<Client::Data::Terrain::TerrainLayer<std::uint16_t>>();
    WorldRuntime runtime;

    walls->fill(4);
    runtime.castleGatesClosed = 1;
    runtime.castleMarkX = 12.0f;
    runtime.castleMarkY = 34.0f;
    Client::Scenes::FinishTheCastleFrame(&runtime, *walls);
    EXPECT_EQ((*walls)[cell], 4);
    EXPECT_EQ(runtime.castleGatesClosed, 0);
    EXPECT_EQ(runtime.castleMarkX, 0.0f);
    EXPECT_EQ(runtime.castleMarkY, 0.0f);

    Client::Scenes::FinishTheCastleFrame(&runtime, *walls);
    EXPECT_EQ((*walls)[cell], 0);

    walls->fill(4);
    Client::Scenes::FinishTheCastleFrame(nullptr, *walls);
    EXPECT_EQ((*walls)[cell], 0);
}
} // namespace
