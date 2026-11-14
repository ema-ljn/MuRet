#include "Scenes/ModelBody.h"

#include "Core/Expected.h"
#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Data/Models/Model.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/OriginalAngles.h"
#include "Render/Models/Skeleton.h"
#include "Scenes/ActorState.h"
#include "Render/TextureSlot.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/CharacterHead.h"
#include "Scenes/CharacterStep.h"
#include "Scenes/FaunaModels.h"
#include "Scenes/SceneId.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "support/FakeTextureStore.h"
#include "support/PlayerActors.h"
#include "support/RecordingSink.h"
#include "support/SceneDataFiles.h"
#include "support/SyntheticModels.h"
#include "support/TemporaryFolder.h"
#include "support/TextureFiles.h"

#include <gtest/gtest.h>

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <ios>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

// El cuerpo de los monstruos y los NPC (CL9 hito 2, P-C52 d): 0x4f9580, 0x4fcbb0, 0x42d890, 0x4ea190, 0x4ea0e0 y los
// cortes de 0x4fcee0 y 0x505de0 por tipo.
namespace
{
using Client::Render::Models::ActionInfo;
using Client::Render::Models::CrtRandom;
using Client::Render::Models::Skeleton;
using Client::Scenes::ActorState;
using Client::Scenes::ModelActionEnd;

constexpr std::uint32_t Seed = 0x2468ace1u;

void Skip(CrtRandom& random, int draws)
{
    for (int draw = 0; draw < draws; ++draw)
        static_cast<void>(random.Next());
}

Skeleton EightActions()
{
    Skeleton skeleton;
    skeleton.actions.assign(8, ActionInfo{.repeats = true, .speed = 0.25f, .keyCount = 10});
    return skeleton;
}

ActorState Monster(std::uint16_t type, std::uint8_t action)
{
    ActorState actor;
    actor.alive = true;
    actor.type = type;
    actor.animation.action = action;
    actor.animation.frame = 4.0f;
    return actor;
}

TEST(ModelBodyTest, TheSpeedOfTheOtherModelsByType)
{
    using Client::Scenes::ModelSpeedOf;
    EXPECT_EQ(ModelSpeedOf(0xa8, 0, 0.0f, 0.3f), 0.6f);
    EXPECT_EQ(ModelSpeedOf(0xac, 6, 7.0f, 0.3f), 0.6f);
    // 0x4f96f7..0x4f9712: el 0x192 en la 6 solo pasado el frame 6.
    EXPECT_EQ(ModelSpeedOf(0x192, 6, 6.5f, 0.3f), 0.3f * 4.0f);
    EXPECT_EQ(ModelSpeedOf(0x192, 6, 6.0f, 0.3f), 0.3f);
    EXPECT_EQ(ModelSpeedOf(0x192, 5, 7.0f, 0.3f), 0.3f);
    EXPECT_EQ(ModelSpeedOf(0x1a9, 6, 0.0f, 0.3f), 0.3f * 0.5f);
    EXPECT_EQ(ModelSpeedOf(0x1a9, 1, 0.0f, 0.3f), 0.3f);
    EXPECT_EQ(ModelSpeedOf(0x153, 6, 9.0f, 0.3f), 0.3f);
}

// 0x4fcbb0: los monstruos vuelven a la 0 desde las 1, 3, 4, 5, 8, 9 y 0xb sin rand(); la 2 y la 7 siguen; la 6 corta
// con +0x38a en 0 (0x52e810) y no hace nada con él en 1.
TEST(ModelBodyTest, TheMonstersGoBackToRestWithoutRandom)
{
    const Skeleton skeleton = EightActions();
    Client::Scenes::WorldRuntime runtime;
    for (const std::uint8_t action : std::array<std::uint8_t, 4>{1, 3, 4, 5})
    {
        SCOPED_TRACE(static_cast<int>(action));
        ActorState actor = Monster(0x153, action);
        CrtRandom random(Seed);
        const auto end = Client::Scenes::FinishTheModelAction(actor, 3, skeleton, runtime, random);
        ASSERT_TRUE(end.HasValue()) << end.Error();
        EXPECT_EQ(end.Value(), ModelActionEnd::Done);
        EXPECT_EQ(actor.animation.action, 0);
        EXPECT_EQ(actor.animation.frame, 0.0f);
        EXPECT_EQ(random.State(), Seed);
    }
    for (const std::uint8_t action : std::array<std::uint8_t, 2>{2, 7})
    {
        ActorState actor = Monster(0x1c9, action);
        CrtRandom random(Seed);
        ASSERT_TRUE(Client::Scenes::FinishTheModelAction(actor, 3, skeleton, runtime, random).HasValue());
        EXPECT_EQ(actor.animation.action, action);
        EXPECT_EQ(random.State(), Seed);
    }
    ActorState dying = Monster(0x160, 6);
    CrtRandom random(Seed);
    const auto cut = Client::Scenes::FinishTheModelAction(dying, 3, skeleton, runtime, random);
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x4fcc3c"), std::string::npos) << cut.Error();
    dying.byte38a = 1;
    ASSERT_TRUE(Client::Scenes::FinishTheModelAction(dying, 3, skeleton, runtime, random).HasValue());
    EXPECT_EQ(dying.animation.action, 6);
    EXPECT_EQ(random.State(), Seed);
}

// 0x4fcc77..0x4fccf7: el 0x1ce y el 0x1cf sortean con rand() % 16 (debajo de 12 la 0, si no 1 + rand() % 2); los demás
// fuera de los monstruos, con rand() % 2; el NewFace no sortea; el 0xc5 pone el byte de la acción en 1 sin 0x4ea030; el
// 0x28 en el mapa 1 va a la 0.
TEST(ModelBodyTest, TheNpcsDrawTheirNextAction)
{
    const Skeleton skeleton = EightActions();
    Client::Scenes::WorldRuntime runtime;
    for (std::uint32_t seed = 1; seed < 40; ++seed)
    {
        SCOPED_TRACE(seed);
        CrtRandom expected(seed);
        const int first = expected.Next() % 16;
        const int action = first < 12 ? 0 : 1 + expected.Next() % 2;
        ActorState smith = Monster(0x1ce, 3);
        CrtRandom random(seed);
        ASSERT_TRUE(Client::Scenes::FinishTheModelAction(smith, 3, skeleton, runtime, random).HasValue());
        EXPECT_EQ(smith.animation.action, action);
        EXPECT_EQ(random.State(), expected.State());

        CrtRandom other(seed);
        const int otherAction = CrtRandom(seed).Next() % 2;
        ActorState girl = Monster(0x1cd, 3);
        ASSERT_TRUE(Client::Scenes::FinishTheModelAction(girl, 3, skeleton, runtime, other).HasValue());
        EXPECT_EQ(girl.animation.action, otherAction);
        EXPECT_EQ(other.State(), Client::Tests::AfterDraws(seed, 1).State());
    }
    ActorState face = Monster(0xa9, 3);
    CrtRandom random(Seed);
    ASSERT_TRUE(Client::Scenes::FinishTheModelAction(face, 3, skeleton, runtime, random).HasValue());
    EXPECT_EQ(face.animation.action, 3);
    ActorState named = Monster(0xc5, 3);
    ASSERT_TRUE(Client::Scenes::FinishTheModelAction(named, 3, skeleton, runtime, random).HasValue());
    EXPECT_EQ(named.animation.action, 1);
    EXPECT_EQ(named.animation.frame, 4.0f);
    EXPECT_EQ(random.State(), Seed);
    runtime.map = 1;
    ActorState statue = Monster(0x28, 3);
    ASSERT_TRUE(Client::Scenes::FinishTheModelAction(statue, 3, skeleton, runtime, random).HasValue());
    EXPECT_EQ(statue.animation.action, 0);
    EXPECT_EQ(random.State(), Seed);
}

// 0x42d890 en el mapa 30, por +0x376: 221 y 222 a la 0; 277 con +0x78 en 0x20000 a la 2 y +0x42c en 0, con +0x78 en 0
// a la 0 y +0x42c en 1; 286 y 287 de la 5 a la 4 con 0x7239d8 en 2, +0x110 en 1 y +0x100 en 0x12c. Devuelven 1 y
// 0x4fcbb0 vuelve a avanzar; fuera del 30, nada.
TEST(ModelBodyTest, TheCastleObjectsFinishByTheirKind)
{
    const Skeleton skeleton = EightActions();
    Client::Scenes::WorldRuntime runtime;
    runtime.map = 30;
    CrtRandom random(Seed);
    ActorState guard = Monster(0x1c0, 3);
    guard.word376 = 221;
    auto end = Client::Scenes::FinishTheModelAction(guard, 3, skeleton, runtime, random);
    ASSERT_TRUE(end.HasValue()) << end.Error();
    EXPECT_EQ(end.Value(), ModelActionEnd::AdvanceAgain);
    EXPECT_EQ(guard.animation.action, 0);

    ActorState carried = Monster(0x1c0, 3);
    carried.word376 = 277;
    carried.flags78 = 0x20000;
    ASSERT_EQ(Client::Scenes::FinishTheModelAction(carried, 3, skeleton, runtime, random).Value(),
              ModelActionEnd::AdvanceAgain);
    EXPECT_EQ(carried.animation.action, 2);
    EXPECT_EQ(carried.byte42c, 0);
    carried.flags78 = 0;
    ASSERT_EQ(Client::Scenes::FinishTheModelAction(carried, 3, skeleton, runtime, random).Value(),
              ModelActionEnd::AdvanceAgain);
    EXPECT_EQ(carried.animation.action, 0);
    EXPECT_EQ(carried.byte42c, 1);

    ActorState gate = Monster(0x1c0, 5);
    gate.word376 = 286;
    gate.animation.previousAction = 1;
    gate.animation.previousKey = 7;
    ASSERT_EQ(Client::Scenes::FinishTheModelAction(gate, 3, skeleton, runtime, random).Value(),
              ModelActionEnd::AdvanceAgain);
    EXPECT_EQ(gate.animation.action, 4);
    EXPECT_EQ(gate.animation.previousAction, 4);
    EXPECT_EQ(gate.animation.previousKey, 0);
    EXPECT_EQ(gate.animation.frame, 0.0f);
    EXPECT_EQ(runtime.castleByte7239d8, 2);
    EXPECT_EQ(gate.byte110, 1);
    EXPECT_EQ(gate.dword100, 0x12c);
    // La 3 no es de 0x42d890: sigue a los monstruos, que la vuelven a la 0.
    gate.word376 = 287;
    gate.animation.action = 3;
    ASSERT_EQ(Client::Scenes::FinishTheModelAction(gate, 3, skeleton, runtime, random).Value(), ModelActionEnd::Done);
    EXPECT_EQ(gate.animation.action, 0);
    EXPECT_EQ(random.State(), Seed);

    runtime.map = 0;
    ActorState outside = Monster(0x1c0, 2);
    outside.word376 = 221;
    ASSERT_EQ(Client::Scenes::FinishTheModelAction(outside, 3, skeleton, runtime, random).Value(),
              ModelActionEnd::Done);
    EXPECT_EQ(outside.animation.action, 2);
}

// 0x4ea190 y 0x4ea0e0: la 2 mira al personaje de +0x3a4 si está a menos de 90 grados; sin blanco (−1) o con otra acción
// el blanco queda en 0; la 6 no lo toca; la 0 sortea.
TEST(ModelBodyTest, TheHeadOfAnotherModelLooksAtItsTarget)
{
    auto world = std::make_unique<Client::Scenes::WorldState>();
    auto pool = world->Actors();
    pool[5].position = {300.0f, 900.0f, 180.0f};
    ActorState actor = Monster(0x160, 2);
    actor.position = {100.0f, 200.0f, 30.0f};
    actor.word3a4 = 5;
    const auto heading = static_cast<float>(Client::Render::Models::CreateAngle(100.0f, 200.0f, 300.0f, 900.0f));
    actor.angles.z = heading;
    CrtRandom random(Seed);
    ASSERT_TRUE(Client::Scenes::SetTheModelHeadTarget(actor, pool, random).HasValue());
    EXPECT_EQ(actor.headTurn.target.x, 0.0f);
    EXPECT_EQ(actor.headTurn.target.y, static_cast<float>((180.0 - (30.0 + 50.0)) * static_cast<double>(0.2f)));
    // De espaldas: a 180 grados, en 0.
    actor.angles.z = heading >= 180.0f ? heading - 180.0f : heading + 180.0f;
    ASSERT_TRUE(Client::Scenes::SetTheModelHeadTarget(actor, pool, random).HasValue());
    EXPECT_EQ(actor.headTurn.target, (glm::vec2{0.0f, 0.0f}));
    // Debajo: el ángulo negativo suma 360.
    actor.angles.z = heading;
    pool[5].position.z = 0.0f;
    ASSERT_TRUE(Client::Scenes::SetTheModelHeadTarget(actor, pool, random).HasValue());
    EXPECT_EQ(actor.headTurn.target.y, static_cast<float>((0.0 - 80.0) * static_cast<double>(0.2f)) + 360.0f);
    actor.word3a4 = 0xffff;
    ASSERT_TRUE(Client::Scenes::SetTheModelHeadTarget(actor, pool, random).HasValue());
    EXPECT_EQ(actor.headTurn.target, (glm::vec2{0.0f, 0.0f}));
    actor.word3a4 = 500;
    const auto outside = Client::Scenes::SetTheModelHeadTarget(actor, pool, random);
    ASSERT_FALSE(outside.HasValue());
    EXPECT_NE(outside.Error().find("0x4ea24c"), std::string::npos);
    actor.animation.action = 6;
    actor.headTurn.target = {7.0f, 8.0f};
    ASSERT_TRUE(Client::Scenes::SetTheModelHeadTarget(actor, pool, random).HasValue());
    EXPECT_EQ(actor.headTurn.target, (glm::vec2{7.0f, 8.0f}));
    actor.animation.action = 3;
    ASSERT_TRUE(Client::Scenes::SetTheModelHeadTarget(actor, pool, random).HasValue());
    EXPECT_EQ(actor.headTurn.target, (glm::vec2{0.0f, 0.0f}));
    EXPECT_EQ(random.State(), Seed);
    actor.animation.action = 0;
    ASSERT_TRUE(Client::Scenes::SetTheModelHeadTarget(actor, pool, random).HasValue());
    CrtRandom expected(Seed);
    EXPECT_EQ(random.State(), Client::Tests::AfterDraws(Seed, expected.Next() % 32 == 0 ? 3 : 1).State());
}

// Desde el frame 3, 0x4fcee0 con otro modelo: la estela del 0x194 en la 3, del 0x195 y el 0x197 en la 3 y la 4, de
// +0x376 en 0x47, 0x4a o 0x12d y de una espada en la derecha; y los helpers por tipo.
TEST(ModelBodyTest, TheTrailOfTheOtherModelsCutsByType)
{
    using Client::Scenes::ModelTrailCutOf;
    ActorState sword = Monster(0x160, 3);
    sword.hands[0].model = 0x212;
    ASSERT_TRUE(ModelTrailCutOf(sword).has_value());
    EXPECT_EQ(ModelTrailCutOf(sword)->address, 0x4fd233u);
    sword.animation.action = 2;
    EXPECT_FALSE(ModelTrailCutOf(sword).has_value());
    sword.hands[0].model = 0x412;
    sword.animation.action = 4;
    EXPECT_FALSE(ModelTrailCutOf(sword).has_value());
    EXPECT_EQ(ModelTrailCutOf(Monster(0x194, 3))->address, 0x4fd233u);
    EXPECT_FALSE(ModelTrailCutOf(Monster(0x194, 4)).has_value());
    EXPECT_EQ(ModelTrailCutOf(Monster(0x197, 4))->address, 0x4fd233u);
    ActorState knight = Monster(0x160, 4);
    knight.word376 = 0x12d;
    EXPECT_EQ(ModelTrailCutOf(knight)->address, 0x4fd233u);
    // 0x432bd0 está portado: el 0x1ab no corta acá (StepTheSwingsOf1abTo1b4).
    EXPECT_FALSE(ModelTrailCutOf(Monster(0x1ab, 3)).has_value());
    EXPECT_FALSE(ModelTrailCutOf(Monster(0x1ac, 0)).has_value());
}

// 0x4fd5c1..0x4fd5df: 0x441260, 0x42fa40 y 0x426d90 solo dejan su estela (0x52e470, CL11) en sus acciones; quietos (la
// 0) o en otra, no hacen nada.
TEST(ModelBodyTest, TheTrailHelpersCutOnlyInTheirActions)
{
    using Client::Scenes::ModelTrailCutOf;
    struct Case
    {
        std::uint16_t type;
        std::uint8_t action;
        std::optional<std::uint32_t> address;
    };
    constexpr std::optional<std::uint32_t> None;
    // 0x44127c: el 0x1a7 en la 3 o la 4. 0x42fa64 y 0x42fb61: el 0x1b3 en la 3 y el 0x1b1 en la 4. 0x426db9 y
    // 0x426ebc: el 0x1ba en la 3 y el 0x1b9 en la 3 o la 4.
    const Case cases[]{
        {0x1a7, 0, None},     {0x1a7, 2, None},     {0x1a7, 3, 0x441260}, {0x1a7, 4, 0x441260}, {0x1b3, 0, None},
        {0x1b3, 3, 0x42fa40}, {0x1b3, 4, None},     {0x1b1, 0, None},     {0x1b1, 3, None},     {0x1b1, 4, 0x42fa40},
        {0x1b2, 3, None},     {0x1ba, 0, None},     {0x1ba, 3, 0x426d90}, {0x1ba, 4, None},     {0x1b9, 0, None},
        {0x1b9, 3, 0x426d90}, {0x1b9, 4, 0x426d90}, {0x1b9, 6, None},     {0x1b8, 0, None},     {0x1b8, 3, 0x426d90},
    };
    for (const Case& entry : cases)
    {
        SCOPED_TRACE(testing::Message() << std::hex << entry.type << " " << static_cast<int>(entry.action));
        const auto cut = ModelTrailCutOf(Monster(entry.type, entry.action));
        ASSERT_EQ(cut.has_value(), entry.address.has_value());
        if (!cut.has_value())
            continue;
        EXPECT_EQ(cut->address, *entry.address);
        // 0x441260 y 0x42fa40 van antes del rand() de 0x432bd0; 0x426d90, después.
        EXPECT_EQ(cut->beforeTheRandom, *entry.address != 0x426d90u);
    }
}

// 0x427057..0x427068: el 0x1b8 en la 3 o la 4 deja estela con el frame (+0x114) en 5,06f (0x6b8a30) o menos, o NaN.
TEST(ModelBodyTest, TheTrailOfThe1b8EndsAfterItsFifthFrame)
{
    using Client::Scenes::ModelTrailCutOf;
    const float last = std::bit_cast<float>(0x40a1eb85u);
    for (const std::uint8_t action : {std::uint8_t{3}, std::uint8_t{4}})
    {
        SCOPED_TRACE(static_cast<int>(action));
        ActorState actor = Monster(0x1b8, action);
        actor.animation.frame = last;
        ASSERT_TRUE(ModelTrailCutOf(actor).has_value());
        EXPECT_EQ(ModelTrailCutOf(actor)->address, 0x426d90u);
        actor.animation.frame = std::nextafter(last, 10.0f);
        EXPECT_FALSE(ModelTrailCutOf(actor).has_value());
        actor.animation.frame = std::numeric_limits<float>::quiet_NaN();
        EXPECT_TRUE(ModelTrailCutOf(actor).has_value());
    }
}

// Un Monster02 (el modelo 0x153) cargado por 0x612d90 en WorldState::FaunaResources da su paso fuera del pool: avanza
// con la velocidad de su acción (0,2 la 1, 0x612e5a..0x612e9f) y, al terminar la 3, vuelve a la 0 (0x4fcbb0), que
// después sortea la cabeza con 0x4ea190. Los rand(): los dos accesos de 0x508ba0, el % 6 del cuerpo, la celda de
// 0x505de0, 0x4ea190 (solo con la 0), la cabeza común y el & 7.
class MonsterStep : public ::testing::Test
{
protected:
    void SetUp() override
    {
        auto model = Client::Tests::SampleModel();
        {
            const auto first = model.actions.front();
            model.actions.assign(8, first);
        }
        for (auto& bone : model.bones)
            if (!bone.dummy)
            {
                const auto first = bone.actions.front();
                bone.actions.assign(8, first);
            }
        model.meshes.front().textureName = "body.tga";
        for (const char* name : {"Monster/Monster01.bmd", "Monster/Monster02.bmd"})
            Client::Tests::WriteSceneFile(m_folder.Path(), name,
                                          Client::Tests::PlainBmd(Client::Tests::BmdContent(model), 0x0a));
        Client::Tests::WriteSceneFile(m_folder.Path(), "Monster/body.OZT", Client::Tests::PlainOzt(2, 2));
        m_world->Map().terrain.attributes = std::make_unique<Client::Data::Terrain::TerrainAttributes>();
        m_world->FaunaResources().TextureTable().Bind(m_store);
        auto context = m_world->FaunaResources().LoadContextOf(m_folder.Path(), m_log);
        ASSERT_TRUE(context.has_value());
        for (const std::int32_t number : {0, 1})
        {
            const auto loaded = m_world->FaunaResources().LoadMonster(number, 0, *context);
            ASSERT_TRUE(loaded.HasValue()) << loaded.Error();
        }
        ASSERT_NE(m_world->FaunaResources().SkeletonOf(0x153), nullptr);
        m_actor.alive = true;
        m_actor.type = 0x153;
        m_actor.byte88 = 2;
        m_actor.hands[0].model = 0xffff;
        m_actor.hands[1].model = 0xffff;
    }

    Client::Core::Expected<void, std::string> Step(CrtRandom& random)
    {
        return Client::Scenes::StepCharacter(m_actor, {.world = *m_world,
                                                       .random = random,
                                                       .scene = Client::Scenes::SceneId::Game,
                                                       .particles = &m_world->Particles()});
    }

    const Skeleton& Model() const
    {
        return *m_world->FaunaResources().SkeletonOf(0x153);
    }

    Client::Tests::TemporaryFolder m_folder{"model-body"};
    Client::Tests::FakeTextureStore m_store;
    Client::Tests::RecordingSink m_sink;
    Client::Core::Log m_log{m_sink, Client::Core::LogLevel::Debug};
    std::unique_ptr<Client::Scenes::WorldState> m_world = std::make_unique<Client::Scenes::WorldState>();
    ActorState m_actor;
};

// Lo que saca después de la celda de 0x505de0: la cabeza común y el & 7.
void DrawTheHeadAndTheEffects(CrtRandom& random)
{
    Client::Scenes::CharacterHead head;
    Client::Scenes::StepCharacterHead(head, true, random);
    static_cast<void>(random.Next());
}

TEST_F(MonsterStep, AMonsterAdvancesWithTheSpeedOfItsModel)
{
    m_actor.animation.action = 1;
    m_actor.animation.frame = 0.0f;
    CrtRandom random(Seed);
    const auto stepped = Step(random);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(m_actor.animation.frame, Model().actions[1].speed);
    EXPECT_EQ(Model().actions[1].speed, 0.2f);
    EXPECT_EQ(m_actor.headTurn.target, (glm::vec2{0.0f, 0.0f}));
    CrtRandom expected = Client::Tests::AfterDraws(Seed, 8 + 1 + 8);
    DrawTheHeadAndTheEffects(expected);
    EXPECT_EQ(random.State(), expected.State());
}

TEST_F(MonsterStep, AMonsterThatEndsItsAttackRests)
{
    const auto& action = Model().actions[3];
    m_actor.animation.action = 3;
    m_actor.animation.frame = static_cast<float>(action.keyCount) - 0.1f;
    CrtRandom random(Seed);
    const auto stepped = Step(random);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(m_actor.animation.action, 0);
    EXPECT_EQ(m_actor.animation.frame, 0.0f);
    CrtRandom expected = Client::Tests::AfterDraws(Seed, 8 + 1 + 8);
    // 0x4ea190 con la 0: rand() % 32 y, si da 0, dos más.
    if (CrtRandom(expected).Next() % 32 == 0)
        Skip(expected, 3);
    else
        Skip(expected, 1);
    DrawTheHeadAndTheEffects(expected);
    EXPECT_EQ(random.State(), expected.State());
}

// Un modelo sin cargar (el 0x154) es el registro en 0 de 0x5769ad8 (+0x26 sin acciones): 0x4f9580 no avanza, el
// rand() % 6 de 0x4fd7e4 sale igual y 0x505de0 va a 0x505e15, que arma la caja de +0x13c..+0x168 con la de +0x124 y
// la posición y sale, sin la celda ni el & 7 (B3). En Lorencia, en Devias y en Atlans.
TEST_F(MonsterStep, AModelThatIsNotLoadedOnlyBoxesItself)
{
    for (const std::int32_t map : {0, 2, 7})
    {
        SCOPED_TRACE(map);
        m_world->Runtime().map = map;
        m_actor.type = 0x154;
        m_actor.animation = {};
        m_actor.animation.action = 1;
        m_actor.animation.frame = 0.5f;
        m_actor.position = {1000.0f, 2000.0f, 30.0f};
        m_actor.boundsMin = {-60.0f, -60.0f, 0.0f};
        m_actor.boundsMaxX = 50.0f;
        m_actor.boundsMaxY = 70.0f;
        m_actor.boundsTop = 150.0f;
        m_actor.field14c = 9.0f;
        m_actor.field164 = 9.0f;
        CrtRandom random(Seed);
        const auto stepped = Step(random);
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(m_actor.animation.action, 1);
        EXPECT_EQ(m_actor.animation.frame, 0.5f);
        EXPECT_EQ(m_actor.field13c, 940.0f);
        EXPECT_EQ(m_actor.field140, 1940.0f);
        EXPECT_EQ(m_actor.field144, 30.0f);
        EXPECT_EQ(m_actor.field148, 110.0f);
        EXPECT_EQ(m_actor.field158, 130.0f);
        EXPECT_EQ(m_actor.field168, 150.0f);
        for (const float zero : {m_actor.field14c, m_actor.field150, m_actor.field154, m_actor.field15c,
                                 m_actor.field160, m_actor.field164})
            EXPECT_EQ(zero, 0.0f);
        EXPECT_EQ(random.State(), Client::Tests::AfterDraws(Seed, 8 + 1).State());
    }
}

// Monster01 (0x152) tiene caso en 0x505de0 (0x5084b3): fuera de las ventanas de frame de su aliento no saca más que el
// & 7 (Scenes::StepTheModelEffects).
TEST_F(MonsterStep, AModelWithItsOwnCaseOutsideItsWindowOnlyDrawsTheBrightness)
{
    m_actor.animation.action = 1;
    m_actor.type = 0x152;
    CrtRandom random(Seed);
    const auto stepped = Step(random);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    CrtRandom expected = Client::Tests::AfterDraws(Seed, 8 + 1 + 8);
    DrawTheHeadAndTheEffects(expected);
    EXPECT_EQ(random.State(), expected.State());
}

// La biblioteca del mapa detrás de FaunaResources con el 0x1f5 (un NPC) en el esqueleto de Monster02.
class TraderLibrary final : public Client::Scenes::IModelLibrary
{
public:
    explicit TraderLibrary(const Skeleton& skeleton, std::uint16_t model = 0x1f5) : m_skeleton(skeleton), m_model(model)
    {
    }

    [[nodiscard]] const Client::Data::Models::Model* Find(std::uint16_t) const override
    {
        return nullptr;
    }
    [[nodiscard]] const Skeleton* SkeletonOf(std::uint16_t model) const override
    {
        return model == m_model ? &m_skeleton : nullptr;
    }
    [[nodiscard]] std::span<const Client::Render::TextureSlot> MeshTextures(std::uint16_t) const override
    {
        return {};
    }

private:
    const Skeleton& m_skeleton;
    std::uint16_t m_model;
};

// 0x4fce7a..0x4fced1: el 0x1f5 mira la key que dejó la última pose en su modelo (+0xa8, FaunaModels::PoseKey): con la
// última de su acción, rand() % 32 en 0 le pone la acción 1 y si no la 0 (0x4ea030); con otra, nada. Después sigue
// hasta el caso de su modelo en 0x505de0 (0x508093): en la 0, su luz y sus joints; en las otras, nada.
TEST_F(MonsterStep, TheTraderPicksItsNextActionWhenItsModelEndedTheLastPose)
{
    TraderLibrary trader(Model());
    m_world->FaunaResources().Bind(&trader);
    m_actor.type = 0x1f5;
    const std::int16_t last = static_cast<std::int16_t>(Model().actions[2].keyCount - 1);
    const auto stepFrom = [this](std::int16_t key, std::uint32_t seed)
    {
        m_actor.animation.action = 2;
        m_actor.animation.frame = 0.0f;
        m_world->FaunaResources().RecordPoseKey(0x1f5, key);
        CrtRandom random(seed);
        return Step(random);
    };

    const auto other = stepFrom(static_cast<std::int16_t>(last + 1), Seed);
    ASSERT_TRUE(other.HasValue()) << other.Error();
    EXPECT_EQ(m_actor.animation.action, 2);

    int rests = 0;
    int others = 0;
    for (std::uint32_t seed = 1; seed < 300; ++seed)
    {
        SCOPED_TRACE(seed);
        const auto stepped = stepFrom(last, seed);
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        if (m_actor.animation.action == 0)
            ++rests;
        else if (m_actor.animation.action == 1)
            ++others;
        else
            ADD_FAILURE() << static_cast<int>(m_actor.animation.action);
        EXPECT_EQ(m_actor.animation.frame, 0.0f);
    }
    EXPECT_GT(rests, others);
    EXPECT_GT(others, 0);
    m_world->FaunaResources().Bind(nullptr);
}

// 0x4fd5d7: desde el frame 3, 0x432bd0 con el 0x1b0 en la 4 fuera de (5, 7) saca su rand() de 0x432bf1 y los dos de
// cada una de sus diez poses (Scenes::StepTheSwingsOf1abTo1b4), entre el % 6 del cuerpo y la celda de 0x505de0;
// después, el helper 0x431470 no hace nada con el 0x1b0 en la 4. El esqueleto de Monster02 con diez keys por acción.
TEST_F(MonsterStep, TheSwingOfThe1b0DrawsItsRandomsBeforeItsEffects)
{
    Skeleton swinging = Model();
    for (auto& action : swinging.actions)
        action.keyCount = 10;
    for (auto& bone : swinging.bones)
        for (auto& keys : bone.actions)
            if (!keys.empty())
                keys.resize(10, keys.front());
    TraderLibrary library(swinging, 0x1b0);
    m_world->FaunaResources().Bind(&library);
    m_actor.type = 0x1b0;
    m_actor.animation.action = 4;
    m_actor.animation.frame = 3.0f;
    CrtRandom random(Seed);
    const auto stepped = Step(random);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    ASSERT_EQ(m_actor.animation.action, 4);
    ASSERT_GE(m_actor.animation.frame, 3.0f);
    ASSERT_LE(m_actor.animation.frame, 5.0f);
    CrtRandom expected = Client::Tests::AfterDraws(Seed, 8 + 1 + 1 + 20 + 8);
    DrawTheHeadAndTheEffects(expected);
    EXPECT_EQ(random.State(), expected.State());
    EXPECT_EQ(m_world->PoseCarry().models.count(0x1b0), 1u);
    m_world->FaunaResources().Bind(nullptr);
}

// 0x4fd5c1: quieto desde el frame 3, el 0x1a7 (Monster85, de los tipos 294 y 302: la 31) no deja estela en 0x441260
// (solo en la 3 y la 4); sigue con el rand() de 0x432bf1 y llega a 0x505de0, donde ningún helper actúa con él.
TEST_F(MonsterStep, AStanding1a7GoesPastItsTrailHelper)
{
    Skeleton standing = Model();
    for (auto& action : standing.actions)
        action.keyCount = 10;
    for (auto& bone : standing.bones)
        for (auto& keys : bone.actions)
            if (!keys.empty())
                keys.resize(10, keys.front());
    TraderLibrary library(standing, 0x1a7);
    m_world->FaunaResources().Bind(&library);
    for (const std::int32_t map : {0, 2, 31})
    {
        SCOPED_TRACE(map);
        m_world->Runtime().map = map;
        m_actor.type = 0x1a7;
        m_actor.animation.action = 0;
        m_actor.animation.frame = 3.0f;
        CrtRandom random(Seed);
        const auto stepped = Step(random);
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        ASSERT_EQ(m_actor.animation.action, 0);
        CrtRandom expected = Client::Tests::AfterDraws(Seed, 8 + 1 + 1 + 8);
        // 0x4ea190 con la 0: rand() % 32 y, si da 0, dos más.
        if (CrtRandom(expected).Next() % 32 == 0)
            Skip(expected, 3);
        else
            Skip(expected, 1);
        DrawTheHeadAndTheEffects(expected);
        EXPECT_EQ(random.State(), expected.State());
    }
    m_world->FaunaResources().Bind(nullptr);
}
} // namespace
