#include "Actors/ActorShock.h"

#include "Core/Expected.h"
#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Crt/CrtRandom.h"
#include "Tables/PlayerModels.h"
#include "Models/Cloth.h"
#include "Kernel/ActorState.h"
#include "Assets/FaunaModels.h"
#include "FrontEnd/LoginModels.h"
#include "FrontEnd/SelectionState.h"
#include "Session/WorldEntry.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Camera/GameCamera.h"

#include "support/FakeTextureStore.h"
#include "support/PlayerActors.h"
#include "support/RecordingSink.h"
#include "support/SceneDataFiles.h"
#include "support/SyntheticModels.h"
#include "support/TemporaryFolder.h"
#include "support/TextureFiles.h"
#include "Session/ModuleStates.h"
#include "Audio/SoundId.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

// Actors::SetActorShock (0x4f53a0) with a monster: action 5 with the actions of the model its creation loaded and the
// hit sound from its table (0x4f553d..0x4f55a7). Monster01 (0x152) and Monster02 (0x153) are synthetic, with the tables
// of 0x612d90: sounds {0, 1, 2, 3, 4} and {5, 6, 7, 8, 9}. The maps: Lorencia and Devias.
namespace
{
using Client::Crt::CrtRandom;
using Client::Kernel::ActorState;

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
        m_states.assets.fauna.TextureTable().Bind(m_store);
        auto context = m_states.assets.fauna.LoadContextOf(m_folder.Path(), m_log);
        ASSERT_TRUE(context.has_value());
        context->sounds = &m_world->Runtime().modelSounds;
        for (const std::int32_t number : {0, 1})
        {
            const auto loaded = m_states.assets.fauna.LoadMonster(number, 0, *context);
            ASSERT_TRUE(loaded.HasValue()) << loaded.Error();
        }
        m_models.Set(Client::Tables::PlayerSkeletonModel, {}, Client::Tests::PlayerActionsSkeleton(), {});
        Monster().alive = true;
        Monster().type = 0x152;
        Monster().pet.model = Client::Kernel::NoItemModel;
    }

    ActorState& Monster()
    {
        return m_world->Actors()[9];
    }

    Client::Core::Expected<void, std::string> Shock(CrtRandom& random)
    {
        m_cloth.random = random;
        const auto shocked = Client::Actors::SetActorShock(Monster(), 30, Resources());
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
                .log = m_log,
                .states = &m_states};
    }

    Client::Tests::TemporaryFolder m_folder{"actor-shock"};
    Client::Tests::FakeTextureStore m_store;
    Client::Tests::RecordingSink m_sink;
    Client::Core::Log m_log{m_sink, Client::Core::LogLevel::Debug};
    Client::Session::ModuleStates m_states;
    std::unique_ptr<Client::Kernel::WorldState> m_world = std::make_unique<Client::Kernel::WorldState>();
    Client::Scenes::SelectionState m_selection{*m_world};
    Client::Camera::GameCamera m_camera;
    Client::Camera::GameCameraInput m_input;
    Client::Models::ClothWorld m_cloth;
    Client::Scenes::SingleModelLibrary m_models;
};

// 0x4f5475..0x4f5480 and 0x4f553d..0x4f55a7: the still monster goes to action 5 of its model (not Player.bmd's) and
// asks for WORD +0xae or +0xb0 of its table, by rand() & 1, plus 0xd2. One rand().
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
            EXPECT_EQ(m_world->Runtime().lastSoundId, Client::Audio::SoundId{first + drawn + 0xd2});
            EXPECT_EQ(random.State(), Client::Tests::AfterDraws(Seed, 1).State());
        }
    }
}

// 0x4f555d..0x4f5564: with subtype 9, the sound goes 5 higher. In actions 3 and 4 the action does not change.
TEST_F(ActorShockTest, TheNinthSubtypeShiftsTheSoundAndTheAttackKeepsItsAction)
{
    Monster().subtype = 9;
    Monster().animation.action = 3;
    CrtRandom random(Seed);
    ASSERT_TRUE(Shock(random).HasValue());
    EXPECT_EQ(Monster().animation.action, 3);
    const std::int32_t drawn = CrtRandom(Seed).Next() & 1;
    EXPECT_EQ(m_world->Runtime().lastSoundId, Client::Audio::SoundId{2 + drawn + 5 + 0xd2});
}

// A model not loaded is the original's zeroed record: 0x4ea030 sets nothing (0x4ea056) and its table, at 0 (0x60dbe0),
// asks for sound 0xd2 with its rand(). It does not close the session.
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
        EXPECT_EQ(m_world->Runtime().lastSoundId, Client::Audio::SoundId{0xd2});
        EXPECT_EQ(random.State(), Client::Tests::AfterDraws(Seed, 1).State());
    }
}

// 0x160 returns before its table (0x4f5536): no sound and no rand().
TEST_F(ActorShockTest, TheSilentMonsterDrawsNothing)
{
    Monster().type = 0x160;
    CrtRandom random(Seed);
    ASSERT_TRUE(Shock(random).HasValue());
    EXPECT_FALSE(m_world->Runtime().lastSoundId.has_value());
    EXPECT_EQ(random.State(), CrtRandom(Seed).State());
}
} // namespace
