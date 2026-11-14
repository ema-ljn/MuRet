#include "Scenes/ActorShock.h"

#include "Core/Expected.h"
#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Data/Models/PlayerModels.h"
#include "Render/Models/Cloth.h"
#include "Scenes/ActorState.h"
#include "Scenes/FaunaModels.h"
#include "Scenes/LoginModels.h"
#include "Scenes/SelectionState.h"
#include "Scenes/WorldEntry.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "World/GameCamera.h"

#include "support/FakeTextureStore.h"
#include "support/PlayerActors.h"
#include "support/RecordingSink.h"
#include "support/SceneDataFiles.h"
#include "support/SyntheticModels.h"
#include "support/TemporaryFolder.h"
#include "support/TextureFiles.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

// Scenes::SetActorShock (0x4f53a0) con un monstruo: la acción 5 con las acciones del modelo que cargó su alta y el
// sonido del golpe de su tabla (0x4f553d..0x4f55a7). Monster01 (0x152) y Monster02 (0x153) son sintéticos, con las
// tablas de 0x612d90: los sonidos {0, 1, 2, 3, 4} y {5, 6, 7, 8, 9}. Los mapas: Lorencia y Devias.
namespace
{
using Client::Render::Models::CrtRandom;
using Client::Scenes::ActorState;

constexpr std::uint32_t Seed = 0x2468;

class ActorShockTest : public ::testing::Test
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
        m_world->FaunaResources().TextureTable().Bind(m_store);
        auto context = m_world->FaunaResources().LoadContextOf(m_folder.Path(), m_log);
        ASSERT_TRUE(context.has_value());
        for (const std::int32_t number : {0, 1})
        {
            const auto loaded = m_world->FaunaResources().LoadMonster(number, 0, *context);
            ASSERT_TRUE(loaded.HasValue()) << loaded.Error();
        }
        m_models.Set(Client::Data::Models::PlayerSkeletonModel, {}, Client::Tests::PlayerActionsSkeleton(), {});
        Monster().alive = true;
        Monster().type = 0x152;
        Monster().pet.model = Client::Scenes::NoItemModel;
    }

    ActorState& Monster()
    {
        return m_world->Actors()[9];
    }

    Client::Core::Expected<void, std::string> Shock(CrtRandom& random)
    {
        m_cloth.random = random;
        const auto shocked = Client::Scenes::SetActorShock(Monster(), 30, Resources());
        random = m_cloth.random;
        return shocked;
    }

    Client::Scenes::WorldEntryResources Resources()
    {
        return {.world = *m_world,
                .selection = m_selection,
                .dataRoot = {},
                .cloth = m_cloth,
                .camera = m_camera,
                .cameraInput = m_input,
                .models = m_models,
                .log = m_log};
    }

    Client::Tests::TemporaryFolder m_folder{"actor-shock"};
    Client::Tests::FakeTextureStore m_store;
    Client::Tests::RecordingSink m_sink;
    Client::Core::Log m_log{m_sink, Client::Core::LogLevel::Debug};
    std::unique_ptr<Client::Scenes::WorldState> m_world = std::make_unique<Client::Scenes::WorldState>();
    Client::Scenes::SelectionState m_selection{*m_world};
    Client::World::GameCamera m_camera;
    Client::World::GameCameraInput m_input;
    Client::Render::Models::ClothWorld m_cloth;
    Client::Scenes::SingleModelLibrary m_models;
};

// 0x4f5475..0x4f5480 y 0x4f553d..0x4f55a7: el monstruo quieto pasa a la acción 5 de su modelo (no la de Player.bmd) y
// pide el WORD +0xae o +0xb0 de su tabla, según rand() & 1, más 0xd2. Un rand().
TEST_F(ActorShockTest, AMonsterTakesTheActionAndTheSoundOfItsLoadedModel)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        m_world->Runtime().map = map;
        for (const std::uint16_t type : {std::uint16_t{0x152}, std::uint16_t{0x153}})
        {
            SCOPED_TRACE(type);
            Monster().type = type;
            Monster().animation = {};
            m_world->Runtime().lastSoundId.reset();
            CrtRandom random(Seed);
            const auto shocked = Shock(random);
            ASSERT_TRUE(shocked.HasValue()) << shocked.Error();
            EXPECT_EQ(Monster().animation.action, 5);
            const std::int32_t first = type == 0x152 ? 2 : 7;
            const std::int32_t drawn = CrtRandom(Seed).Next() & 1;
            EXPECT_EQ(m_world->Runtime().lastSoundId, std::optional<std::int32_t>{first + drawn + 0xd2});
            EXPECT_EQ(random.State(), Client::Tests::AfterDraws(Seed, 1).State());
        }
    }
}

// 0x4f555d..0x4f5564: con el subtipo 9, el sonido va 5 más arriba. En las acciones 3 y 4 la acción no cambia.
TEST_F(ActorShockTest, TheNinthSubtypeShiftsTheSoundAndTheAttackKeepsItsAction)
{
    Monster().subtype = 9;
    Monster().animation.action = 3;
    CrtRandom random(Seed);
    ASSERT_TRUE(Shock(random).HasValue());
    EXPECT_EQ(Monster().animation.action, 3);
    const std::int32_t drawn = CrtRandom(Seed).Next() & 1;
    EXPECT_EQ(m_world->Runtime().lastSoundId, std::optional<std::int32_t>{2 + drawn + 5 + 0xd2});
}

// Un modelo sin cargar es el registro en 0 del original: 0x4ea030 no pone nada (0x4ea056) y su tabla, en 0 (0x60dbe0),
// pide el sonido 0xd2 con su rand(). No cierra la sesión.
TEST_F(ActorShockTest, AMonsterWithoutItsModelKeepsItsActionAndSoundsFromTheZeroRecord)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        m_world->Runtime().map = map;
        Monster().type = 0x154;
        Monster().animation = {};
        m_world->Runtime().lastSoundId.reset();
        CrtRandom random(Seed);
        const auto shocked = Shock(random);
        ASSERT_TRUE(shocked.HasValue()) << shocked.Error();
        EXPECT_EQ(Monster().animation.action, 0);
        EXPECT_EQ(m_world->Runtime().lastSoundId, std::optional<std::int32_t>{0xd2});
        EXPECT_EQ(random.State(), Client::Tests::AfterDraws(Seed, 1).State());
    }
}

// El 0x160 sale antes de su tabla (0x4f5536): sin sonido ni rand().
TEST_F(ActorShockTest, TheSilentMonsterDrawsNothing)
{
    Monster().type = 0x160;
    CrtRandom random(Seed);
    ASSERT_TRUE(Shock(random).HasValue());
    EXPECT_FALSE(m_world->Runtime().lastSoundId.has_value());
    EXPECT_EQ(random.State(), CrtRandom(Seed).State());
}
} // namespace
