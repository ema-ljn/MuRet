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
#include <cstdint>
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
    EXPECT_EQ(ModelTrailCutOf(Monster(0x1a7, 0))->address, 0x441260u);
    EXPECT_TRUE(ModelTrailCutOf(Monster(0x1b3, 0))->beforeTheRandom);
    EXPECT_EQ(ModelTrailCutOf(Monster(0x1ab, 0))->address, 0x432bd0u);
    EXPECT_FALSE(ModelTrailCutOf(Monster(0x1ab, 0))->beforeTheRandom);
    EXPECT_FALSE(ModelTrailCutOf(Monster(0x1ac, 0)).has_value());
    EXPECT_EQ(ModelTrailCutOf(Monster(0x1b9, 0))->address, 0x426d90u);
}

// 0x506ccc..0x50862f: el caso del modelo, los que van directo a 0x50862f y los seis helpers de 0x5085db.
TEST(ModelBodyTest, TheEffectsOfTheOtherModelsCutByTypeAndMap)
{
    using Client::Scenes::ModelEffectsCutOf;
    EXPECT_EQ(ModelEffectsCutOf(0x152, 0), std::optional<std::uint32_t>{0x5084b3});
    EXPECT_EQ(ModelEffectsCutOf(0x1aa, 0), std::optional<std::uint32_t>{0x507493});
    EXPECT_EQ(ModelEffectsCutOf(0x1f5, 2), std::optional<std::uint32_t>{0x508093});
    EXPECT_EQ(ModelEffectsCutOf(0x153, 0), std::nullopt);
    EXPECT_EQ(ModelEffectsCutOf(0x183, 30), std::nullopt);
    EXPECT_EQ(ModelEffectsCutOf(0x194, 0), std::optional<std::uint32_t>{0x43bdd0});
    EXPECT_EQ(ModelEffectsCutOf(0x19c, 30), std::optional<std::uint32_t>{0x42de90});
    EXPECT_EQ(ModelEffectsCutOf(0x19c, 0), std::nullopt);
    EXPECT_EQ(ModelEffectsCutOf(0x1a4, 2), std::optional<std::uint32_t>{0x441210});
    EXPECT_EQ(ModelEffectsCutOf(0x1b2, 35), std::optional<std::uint32_t>{0x42f550});
    EXPECT_EQ(ModelEffectsCutOf(0x1b2, 0), std::optional<std::uint32_t>{0x431470});
    EXPECT_EQ(ModelEffectsCutOf(0x1ac, 0), std::nullopt);
    EXPECT_EQ(ModelEffectsCutOf(0x1bb, 0), std::optional<std::uint32_t>{0x426ce0});
    EXPECT_EQ(ModelEffectsCutOf(0xa8, 30), std::nullopt);
    EXPECT_EQ(ModelEffectsCutOf(0x1cb, 0), std::nullopt);
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
        model.actions.assign(8, model.actions.front());
        for (auto& bone : model.bones)
            if (!bone.dummy)
                bone.actions.assign(8, bone.actions.front());
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
        return Client::Scenes::StepCharacter(
            m_actor, {.world = *m_world, .random = random, .scene = Client::Scenes::SceneId::Game});
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

// Monster01 (0x152) tiene caso en 0x505de0 (0x5084b3): corta después del & 7. Un modelo sin cargar (el 0x154) corta
// en 0x4f9580, después de 0x508ba0.
TEST_F(MonsterStep, TheModelsWithTheirOwnEffectsCutAfterTheirRandom)
{
    m_actor.animation.action = 1;
    m_actor.type = 0x152;
    CrtRandom random(Seed);
    const auto stepped = Step(random);
    ASSERT_FALSE(stepped.HasValue());
    EXPECT_NE(stepped.Error().find("0x5084b3"), std::string::npos) << stepped.Error();
    CrtRandom expected = Client::Tests::AfterDraws(Seed, 8 + 1 + 8);
    DrawTheHeadAndTheEffects(expected);
    EXPECT_EQ(random.State(), expected.State());

    m_actor.type = 0x154;
    CrtRandom other(Seed);
    const auto missing = Step(other);
    ASSERT_FALSE(missing.HasValue());
    EXPECT_NE(missing.Error().find("0x4f9580"), std::string::npos) << missing.Error();
    EXPECT_EQ(other.State(), Client::Tests::AfterDraws(Seed, 8).State());
}

// La biblioteca del mapa detrás de FaunaResources con el 0x1f5 (un NPC) en el esqueleto de Monster02.
class TraderLibrary final : public Client::Scenes::IModelLibrary
{
public:
    explicit TraderLibrary(const Skeleton& skeleton) : m_skeleton(skeleton) {}

    [[nodiscard]] const Client::Data::Models::Model* Find(std::uint16_t) const override
    {
        return nullptr;
    }
    [[nodiscard]] const Skeleton* SkeletonOf(std::uint16_t model) const override
    {
        return model == 0x1f5 ? &m_skeleton : nullptr;
    }
    [[nodiscard]] std::span<const Client::Render::TextureSlot> MeshTextures(std::uint16_t) const override
    {
        return {};
    }

private:
    const Skeleton& m_skeleton;
};

// 0x4fce7a..0x4fced1: el 0x1f5 mira la key que dejó la última pose en su modelo (+0xa8, FaunaModels::PoseKey): con la
// última de su acción, rand() % 32 en 0 le pone la acción 1 y si no la 0 (0x4ea030); con otra, nada. Después sigue
// hasta el caso de su modelo en 0x505de0 (0x508093), que corta.
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
    ASSERT_FALSE(other.HasValue());
    EXPECT_NE(other.Error().find("0x508093"), std::string::npos) << other.Error();
    EXPECT_EQ(m_actor.animation.action, 2);

    int rests = 0;
    int others = 0;
    for (std::uint32_t seed = 1; seed < 300; ++seed)
    {
        SCOPED_TRACE(seed);
        const auto stepped = stepFrom(last, seed);
        ASSERT_FALSE(stepped.HasValue());
        EXPECT_NE(stepped.Error().find("0x508093"), std::string::npos) << stepped.Error();
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
} // namespace
