#include "Models/IModelLibrary.h"
#include "Actors/ActionReply.h"

#include "Core/Expected.h"
#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Data/Models/Model.h"
#include "Tables/PlayerModels.h"
#include "Protocol/Movement/Movement.h"
#include "Models/Cloth.h"
#include "Math/OriginalAngles.h"
#include "Models/Skeleton.h"
#include "Models/ModelId.h"
#include "Render/TextureSlot.h"
#include "Actors/ActionSend.h"
#include "Kernel/ActorState.h"
#include "Assets/FaunaModels.h"
#include "Monsters/ModelBody.h"
#include "FrontEnd/SelectionState.h"
#include "Session/WorldEntry.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Actors/Emote.h"
#include "Camera/GameCamera.h"

#include "support/PlayerActors.h"
#include "support/RecordingSink.h"
#include "support/SentActions.h"
#include "Session/ModuleStates.h"
#include "Audio/SoundId.h"

#include <glm/ext/vector_float3.hpp>
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

// Actors::ApplyActionReply: another character's 18 (0x4cb750; 04 § La acción, 18). The keys, classes and positions are
// made up; the expected table comes from the ASM blocks (0x4cb812..0x4cbcdb), not from a run.
namespace
{
using Client::Kernel::ActorState;

constexpr std::uint16_t OtherKey = 0x1234;
constexpr std::size_t OtherSlot = 12;
constexpr std::uint8_t ElfClass = 2;
// A non-player type with ten actions.
constexpr std::uint16_t SmallModel = 0x20;
constexpr std::uint16_t TransformedModel = 0x154;
constexpr std::uint32_t SittingFlag = 0x200000;

class ReplyModels final : public Client::Models::IModelLibrary
{
public:
    ReplyModels()
    {
        small.actions.resize(10);
        transformed.actions.resize(7);
    }
    const Client::Data::Models::Model* Find(Client::Models::ModelId) const override
    {
        return nullptr;
    }
    const Client::Models::Skeleton* SkeletonOf(Client::Models::ModelId model) const override
    {
        if (model.Value() == Client::Tables::PlayerSkeletonModel)
            return &player;
        if (model.Value() == SmallModel)
            return &small;
        return model.Value() == TransformedModel ? &transformed : nullptr;
    }
    std::span<const Client::Render::TextureSlot> MeshTextures(Client::Models::ModelId) const override
    {
        return {};
    }
    Client::Models::Skeleton player = Client::Tests::PlayerActionsSkeleton();
    Client::Models::Skeleton small;
    Client::Models::Skeleton transformed;
};

// 0x4cb871..0x4cbcc8: each action's animation for the other classes and for the elf.
struct TableRow
{
    std::uint8_t action;
    std::uint8_t other;
    std::uint8_t elf;
};

constexpr std::array<TableRow, 28> TableAnimations{{
    {0x80, 0xc5, 0xc7}, {0x81, 0xcb, 0xcc}, {0x82, 0xc9, 0xca}, {0x83, 0x99, 0x9a}, {0x84, 0x9b, 0x9c},
    {0x85, 0x9d, 0x9e}, {0x86, 0xa3, 0xa4}, {0x87, 0xa1, 0xa2}, {0x88, 0xa5, 0xa6}, {0x89, 0xa7, 0xa8},
    {0x8a, 0x9f, 0xa0}, {0x8b, 0xa9, 0xaa}, {0x8c, 0xab, 0xac}, {0x8d, 0xad, 0xae}, {0x8e, 0xaf, 0xb0},
    {0x8f, 0xb1, 0xb2}, {0x90, 0xb3, 0xb4}, {0x91, 0xb5, 0xb6}, {0x92, 0xb7, 0xb7}, {0x93, 0xb8, 0xb8},
    {0x94, 0xc0, 0xc0}, {0x95, 0xb9, 0xb9}, {0x96, 0xba, 0xba}, {0x97, 0xbb, 0xbb}, {0x98, 0xbc, 0xbc},
    {0x99, 0xbd, 0xbd}, {0x9a, 0xbe, 0xbe}, {0x9b, 0xbf, 0xbf},
}};

// The animation 0x4cb750 requests from 0x4ea030 for `action`.
std::uint8_t ExpectedAnimation(std::uint8_t action, std::uint8_t characterClass)
{
    if (action == 0x12)
        return 0x98;
    if (action == 0x7e)
        return 5;
    const auto found = std::ranges::find(TableAnimations, action, &TableRow::action);
    if (found == TableAnimations.end())
        return action;
    return (characterClass & 7) == ElfClass ? found->elf : found->other;
}

class ActionReplyTest : public ::testing::Test
{
protected:
    ActionReplyTest()
    {
        m_world->Runtime().map = 0;
        Other() = Client::Tests::UnarmedPlayer();
        Other().alive = true;
        Other().key = OtherKey;
        Other().cellX = 11;
        Other().cellY = 12;
        Other().targetX = 13;
        Other().targetY = 14;
        Other().position = {1.0f, 2.0f, 3.0f};
        Other().movement = 1;
    }

    ActorState& Other()
    {
        return m_world->Actors()[OtherSlot];
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

    Client::Core::Expected<void, std::string> Apply(std::uint8_t direction, std::uint8_t action,
                                                    std::uint16_t key = OtherKey)
    {
        Client::Actors::ActionSender send{};
        if (m_withSender)
            send = Client::Tests::RecordingActions(m_sent);
        return Client::Actors::ApplyActionReply(
            Client::Protocol::Movement::Action{.key = key, .direction = direction, .action = action}, Resources(),
            send);
    }

    std::unique_ptr<Client::Kernel::WorldState> m_world = std::make_unique<Client::Kernel::WorldState>();
    Client::Session::ModuleStates m_states;
    Client::FrontEnd::SelectionState m_selection{*m_world};
    Client::Camera::GameCamera m_camera;
    Client::Camera::GameCameraInput m_input;
    Client::Models::ClothWorld m_cloth;
    ReplyModels m_models;
    Client::Tests::RecordingSink m_sink;
    Client::Core::Log m_log{m_sink, Client::Core::LogLevel::Debug};
    bool m_withSender = true;
    std::vector<Client::Tests::SentAction> m_sent;
};

// 0x4cb7fd..0x4cbcdb: every action that goes to neither 0x4f2250 nor 0x4f4a10, with every class: its block's animation
// (those the model lacks, 205 or more, are not set), sound 0x6f only with 0x12 and no rand().
TEST_F(ActionReplyTest, EveryActionWithEveryClass)
{
    const auto seed = m_cloth.random.State();
    for (const std::uint8_t characterClass :
         {std::uint8_t{0}, std::uint8_t{1}, ElfClass, std::uint8_t{3}, std::uint8_t{0x12}, std::uint8_t{0x21}})
    {
        for (int value = 0; value <= 0xff; ++value)
        {
            const auto action = static_cast<std::uint8_t>(value);
            if (action >= 0x78 && action <= 0x7b)
                continue;
            SCOPED_TRACE(testing::Message() << "clase " << int{characterClass} << ", acción " << value);
            Other().characterClass = characterClass;
            Other().animation.action = 0;
            Other().movement = 1;
            m_world->Runtime().lastSoundId.reset();
            ASSERT_TRUE(Apply(2, action).HasValue());
            const std::uint8_t animation = ExpectedAnimation(action, characterClass);
            EXPECT_EQ(Other().animation.action, animation < Client::Tests::PlayerActionCount ? animation : 0);
            EXPECT_EQ(Other().movement, 0);
            EXPECT_EQ(Other().angles.z, 45.0f);
            EXPECT_EQ(m_world->Runtime().lastSoundId,
                      action == 0x12 ? std::optional{Client::Audio::SoundId{0x6f}} : std::nullopt);
        }
    }
    EXPECT_EQ(m_cloth.random.State(), seed);
    EXPECT_TRUE(m_sent.empty());
}

// What others see of an emote (the action of its C1 18) is the animation its author set (0x576a60 with the elf's +1
// except from 0xb7 to 0xc0), in the 22 cases of 0x576eb0.
TEST_F(ActionReplyTest, TheEmoteLooksTheSameFromOutside)
{
    const std::vector<Client::Actors::EmoteCase> cases = {
        {0x99, 0x83}, {0x9b, 0x84}, {0x9d, 0x85}, {0xa3, 0x86}, {0xa1, 0x87}, {0xa5, 0x88}, {0xa9, 0x8b}, {0xa7, 0x89},
        {0xab, 0x8c}, {0xaf, 0x8e}, {0x9f, 0x8a}, {0xad, 0x8d}, {0xb1, 0x8f}, {0xb3, 0x90}, {0xb5, 0x91}, {0xb7, 0x92},
        {0xb8, 0x93}, {0xc0, 0x94}, {0xbc, 0x98}, {0xbd, 0x99}, {0xbf, 0x9b}, {0xbe, 0x9a},
    };
    for (const std::uint8_t characterClass : {std::uint8_t{0}, ElfClass})
    {
        for (const auto& [animation, action] : cases)
        {
            SCOPED_TRACE(testing::Message() << "clase " << int{characterClass} << ", acción " << int{action});
            Other().characterClass = characterClass;
            ASSERT_TRUE(Apply(1, action).HasValue());
            const bool shared = animation >= 0xb7 && animation <= 0xc0;
            const auto played =
                static_cast<std::uint8_t>(characterClass == ElfClass && !shared ? animation + 1 : animation);
            EXPECT_EQ(Other().animation.action, played);
        }
    }
}

// 0x4cb794..0x4cb7fa: the angle is (direction − 1) · 45 unwrapped, and X and Y go to the centre of the target cell
// (+0x39a and +0x39b), not the logical one, for any action (Q-C779). Z and the cells are untouched.
TEST_F(ActionReplyTest, TheAngleComesFromTheDirectionAndThePositionSnapsToTheTarget)
{
    struct Turn
    {
        std::uint8_t direction;
        float angle;
    };
    const std::vector<Turn> angles = {{0, -45.0f}, {1, 0.0f}, {3, 90.0f}, {8, 315.0f}, {9, 360.0f}, {0xff, 11430.0f}};
    for (const auto& [direction, angle] : angles)
    {
        SCOPED_TRACE(int{direction});
        ASSERT_TRUE(Apply(direction, 0x7e).HasValue());
        EXPECT_EQ(Other().angles.z, angle);
    }
    EXPECT_EQ(Other().position, glm::vec3(1350.0f, 1450.0f, 3.0f));
    Other().targetX = 0;
    Other().targetY = 0xff;
    ASSERT_TRUE(Apply(1, 0x83).HasValue());
    EXPECT_EQ(Other().position, glm::vec3(50.0f, 25550.0f, 3.0f));
    EXPECT_EQ(Other().cellX, 11);
    EXPECT_EQ(Other().cellY, 12);
    EXPECT_EQ(Other().targetX, 0);
    EXPECT_EQ(Other().targetY, 0xff);
}

// 0x4cb781: with the dark horse in +0x318 it does nothing; the other pets do not stop it.
TEST_F(ActionReplyTest, TheDarkHorseIsLeftAlone)
{
    Other().pet.model = 0x1c16;
    Other().animation.action = 1;
    ASSERT_TRUE(Apply(3, 0x83).HasValue());
    EXPECT_EQ(Other().animation.action, 1);
    EXPECT_EQ(Other().angles.z, 0.0f);
    EXPECT_EQ(Other().position, glm::vec3(1.0f, 2.0f, 3.0f));
    EXPECT_EQ(Other().movement, 1);

    for (const std::uint16_t pet : {std::uint16_t{0x1c14}, std::uint16_t{0x1c15}, std::uint16_t{0x1c37}})
    {
        Other().pet.model = pet;
        Other().animation.action = 1;
        ASSERT_TRUE(Apply(3, 0x83).HasValue());
        EXPECT_EQ(Other().animation.action, 0x99);
    }
}

// 0x5110d0 (MOVSX): with no live key it returns slot 400 and the 18 keeps writing there (Q-C778).
TEST_F(ActionReplyTest, AMissingKeyUpdatesTheSpareActorAtSlot400)
{
    ActorState& spare = m_world->Actors()[400];
    spare = Client::Tests::UnarmedPlayer();
    spare.alive = false;
    spare.key = 0x4321;
    spare.targetX = 0;
    spare.targetY = 0xff;
    spare.cellX = 11;
    spare.cellY = 12;
    spare.position = {1.0f, 2.0f, 3.0f};
    spare.angles = {4.0f, 5.0f, 6.0f};
    spare.movement = 1;

    ASSERT_TRUE(Apply(0xff, 0x83, 0x4321).HasValue());

    EXPECT_FALSE(spare.alive);
    EXPECT_EQ(spare.key, 0x4321);
    EXPECT_EQ(spare.movement, 0);
    EXPECT_EQ(spare.angles, glm::vec3(4.0f, 5.0f, 11430.0f));
    EXPECT_EQ(spare.position, glm::vec3(50.0f, 25550.0f, 3.0f));
    EXPECT_EQ(spare.animation.action, 0x99);
    EXPECT_EQ(spare.cellX, 11);
    EXPECT_EQ(spare.cellY, 12);
    EXPECT_EQ(spare.targetX, 0);
    EXPECT_EQ(spare.targetY, 0xff);

    EXPECT_EQ(Other().animation.action, 0);
    EXPECT_EQ(Other().movement, 1);
    EXPECT_EQ(Other().position, glm::vec3(1.0f, 2.0f, 3.0f));
}

// 0x5110d0 ignores unsigned keys from 0x8000 because of the MOVSX, and does not find a key held only by a dead actor.
TEST_F(ActionReplyTest, DeadAndHighBitKeysAlsoSelectTheSpareActor)
{
    ActorState& spare = m_world->Actors()[400];
    spare = Client::Tests::UnarmedPlayer();
    spare.alive = false;
    spare.targetX = 13;
    spare.targetY = 14;
    spare.position = {1.0f, 2.0f, 3.0f};

    Other().alive = false;
    ASSERT_TRUE(Apply(3, 0x83).HasValue());
    EXPECT_EQ(spare.animation.action, 0x99);
    EXPECT_EQ(spare.movement, 0);
    EXPECT_EQ(spare.position, glm::vec3(1350.0f, 1450.0f, 3.0f));
    EXPECT_EQ(Other().animation.action, 0);

    spare = Client::Tests::UnarmedPlayer();
    spare.alive = false;
    spare.targetX = 13;
    spare.targetY = 14;
    spare.position = {1.0f, 2.0f, 3.0f};
    Other().alive = true;
    Other().key = 0x8001;
    Other().position = {7.0f, 8.0f, 9.0f};
    ASSERT_TRUE(Apply(0xff, 0x83, 0x8001).HasValue());
    EXPECT_EQ(spare.animation.action, 0x99);
    EXPECT_EQ(spare.angles.z, 11430.0f);
    EXPECT_EQ(Other().animation.action, 0);
    EXPECT_EQ(Other().position, glm::vec3(7.0f, 8.0f, 9.0f));
}

// 0x5110d0
TEST_F(ActionReplyTest, TheFirstAliveActorWithADuplicateKeyWins)
{
    Other().key = OtherKey;
    ActorState& first = m_world->Actors()[5];
    first = Client::Tests::UnarmedPlayer();
    first.alive = true;
    first.key = OtherKey;
    ASSERT_TRUE(Apply(3, 0x83).HasValue());
    EXPECT_EQ(first.animation.action, 0x99);
    EXPECT_EQ(Other().animation.action, 0);
}

// 0x4cb820..0x4cb852: 0x78 and 0x79 strike with 0x4f4a10 (no weapons, the 0x22, no sound: the only rand() is the close
// of 0x4f3f80's own record) and then set +0x381 to 1, the frame to 0 (even if 0x4ea030 left it elsewhere by staying in
// the same action), the +0x3a4 target to the low WORD of [0x57678b0] and [0x5767964] to the index.
TEST_F(ActionReplyTest, TheStrikeAttacksAndRemembersTheAttacker)
{
    constexpr std::uint32_t OwnRecordDraws = 0xf08;
    m_world->Runtime().heroSlot = 0x10007;
    for (const std::int32_t map : {0, 2})
    {
        for (const std::uint8_t action : {std::uint8_t{0x78}, std::uint8_t{0x79}})
        {
            SCOPED_TRACE(testing::Message() << "mapa " << map << ", acción " << int{action});
            m_world->Runtime().map = map;
            m_world->Runtime().lastAttacker = 0;
            m_world->Runtime().lastSoundId.reset();
            Other().movement = 1;
            Other().byte381 = 0;
            Other().attackCount = 0;
            Other().animation.action = 0x22;
            Other().animation.frame = 1.5f;
            auto expected = m_cloth.random;
            expected.Discard(OwnRecordDraws);
            const auto applied = Apply(3, action);
            ASSERT_TRUE(applied.HasValue()) << applied.Error();
            EXPECT_EQ(Other().angles.z, 90.0f);
            EXPECT_EQ(Other().movement, 0);
            EXPECT_EQ(Other().position, glm::vec3(1350.0f, 1450.0f, 3.0f));
            EXPECT_EQ(Other().animation.action, 0x22);
            EXPECT_EQ(Other().animation.frame, 0.0f);
            EXPECT_EQ(Other().byte381, 1);
            EXPECT_EQ(Other().attackCount, 1);
            EXPECT_EQ(Other().attackTarget, 7);
            EXPECT_EQ(m_world->Runtime().lastAttacker, static_cast<std::int32_t>(OtherSlot));
            EXPECT_FALSE(m_world->Runtime().lastSoundId.has_value());
            EXPECT_EQ(m_cloth.random.State(), expected.State());
        }
    }
    EXPECT_TRUE(m_sent.empty());
}

// 0x4cb820 writes the hero's +0x3a4 WORD; 0x4ea190 must read that same target when following with the head.
TEST_F(ActionReplyTest, TheModelHeadTracksTheHeroSlotWrittenByAnAttackReply)
{
    constexpr std::size_t StaleTargetSlot = 6;
    constexpr std::size_t HeroTargetSlot = 7;
    m_world->Runtime().heroSlot = static_cast<std::uint16_t>(HeroTargetSlot);
    Other().type = SmallModel;
    Other().attackTarget = static_cast<std::uint16_t>(StaleTargetSlot);
    ASSERT_TRUE(Apply(3, 0x78).HasValue());
    ASSERT_EQ(Other().attackTarget, static_cast<std::uint16_t>(HeroTargetSlot));

    ActorState& heroTarget = m_world->Actors()[HeroTargetSlot];
    heroTarget.position = Other().position + glm::vec3{0.0f, 100.0f, 150.0f};
    m_world->Actors()[StaleTargetSlot].position = Other().position + glm::vec3{0.0f, -100.0f, 200.0f};
    Other().angles.z = static_cast<float>(Client::Math::CreateAngle(Other().position.x, Other().position.y,
                                                                    heroTarget.position.x, heroTarget.position.y));
    Other().animation.action = 2;

    const auto before = m_cloth.random.State();
    ASSERT_TRUE(Client::Monsters::SetTheModelHeadTarget(Other(), m_world->Actors(), m_cloth.random).HasValue());
    EXPECT_EQ(Other().headTurn.target.x, 0.0f);
    EXPECT_EQ(Other().headTurn.target.y, static_cast<float>((static_cast<double>(heroTarget.position.z) -
                                                             (static_cast<double>(Other().position.z) + 50.0)) *
                                                            static_cast<double>(0.2f)));
    EXPECT_EQ(m_cloth.random.State(), before);
}

// A cut in 0x4f4a10 (a player with a melee weapon in the right hand and no Item.bmd to read its mark) leaves the angle,
// the Movement and the position, and writes neither +0x381 nor [0x5767964].
TEST_F(ActionReplyTest, AStrikeThatCutsLeavesTheAttackerAlone)
{
    Other().hands[0].model = 0x212;
    const auto applied = Apply(3, 0x78);
    ASSERT_FALSE(applied.HasValue());
    EXPECT_NE(applied.Error().find("0x4f4a10: falta Item.bmd"), std::string::npos) << applied.Error();
    EXPECT_EQ(Other().movement, 0);
    EXPECT_EQ(Other().byte381, 0);
    EXPECT_EQ(m_world->Runtime().lastAttacker, 0);
}

// 0x4cb781 is the only exit before the common stores, also when the lookup fell on 400.
TEST_F(ActionReplyTest, DarkHorseInTheSpareSlotStopsTheReplyBeforeAnyStores)
{
    ActorState& spare = m_world->Actors()[400];
    spare = Client::Tests::UnarmedPlayer();
    spare.alive = false;
    spare.targetX = 13;
    spare.targetY = 14;
    spare.position = {1.0f, 2.0f, 3.0f};
    spare.angles = {4.0f, 5.0f, 6.0f};
    spare.movement = 7;
    spare.animation.action = 0x20;
    spare.animation.frame = 9.5f;
    spare.pet.model = 0x1c16;
    m_world->Runtime().lastSoundId = Client::Audio::SoundId{0x44};
    const auto random = m_cloth.random.State();

    const auto applied = Apply(0xff, 0x78, 0x4321);

    ASSERT_TRUE(applied.HasValue());
    EXPECT_FALSE(spare.alive);
    EXPECT_EQ(spare.position, glm::vec3(1.0f, 2.0f, 3.0f));
    EXPECT_EQ(spare.angles, glm::vec3(4.0f, 5.0f, 6.0f));
    EXPECT_EQ(spare.movement, 7);
    EXPECT_EQ(spare.animation.action, 0x20);
    EXPECT_EQ(spare.animation.frame, 9.5f);
    EXPECT_EQ(m_world->Runtime().lastSoundId, Client::Audio::SoundId{0x44});
    EXPECT_EQ(m_cloth.random.State(), random);
}

// 0x4cb820: the ghost record also completes the attack and its suffix.
TEST_F(ActionReplyTest, AStrikeOnTheSpareSlotCompletesTheAttackAndItsSuffix)
{
    ActorState& spare = m_world->Actors()[400];
    spare = Client::Tests::UnarmedPlayer();
    spare.alive = false;
    spare.targetX = 13;
    spare.targetY = 14;
    spare.position = {1.0f, 2.0f, 3.0f};
    spare.movement = 7;
    spare.byte381 = 1;
    spare.animation.action = 0x20;
    spare.animation.frame = 9.5f;
    spare.attackTarget = 0x1234;
    m_world->Runtime().lastAttacker = 77;

    for (const std::uint8_t action : {std::uint8_t{0x78}, std::uint8_t{0x79}})
    {
        SCOPED_TRACE(int{action});
        spare.movement = 7;
        const auto applied = Apply(3, action, 0x4321);

        ASSERT_TRUE(applied.HasValue()) << applied.Error();
        EXPECT_EQ(spare.movement, 0);
        EXPECT_EQ(spare.angles.z, 90.0f);
        EXPECT_EQ(spare.position, glm::vec3(1350.0f, 1450.0f, 3.0f));
        EXPECT_EQ(spare.byte381, 1);
        EXPECT_EQ(spare.animation.action, 0x22);
        EXPECT_EQ(spare.animation.frame, 0.0f);
        EXPECT_EQ(spare.attackTarget, static_cast<std::uint16_t>(m_world->Runtime().heroSlot));
        EXPECT_EQ(m_world->Runtime().lastAttacker, 400);
    }
}

// 0x4cb812: 0x7a goes to 0x4f2250 even for the failed lookup, which returns slot 400.
TEST_F(ActionReplyTest, AStopActionAppliesToTheSpareActorAtSlot400)
{
    ActorState& spare = m_world->Actors()[400];
    spare = Client::Tests::UnarmedPlayer();
    spare.alive = false;
    spare.targetX = 13;
    spare.targetY = 14;
    spare.position = {1.0f, 2.0f, 3.0f};
    spare.movement = 7;
    spare.animation.action = 0x20;

    const auto random = m_cloth.random;
    const auto applied = Apply(3, 0x7a, 0x4321);

    ASSERT_TRUE(applied.HasValue()) << applied.Error();
    EXPECT_FALSE(spare.alive);
    EXPECT_EQ(spare.movement, 0);
    EXPECT_EQ(spare.angles.z, 90.0f);
    EXPECT_EQ(spare.position, glm::vec3(1350.0f, 1450.0f, 3.0f));
    EXPECT_EQ(spare.animation.action, 1);
    auto expectedRandom = random;
    static_cast<void>(expectedRandom.Next());
    EXPECT_EQ(m_cloth.random.State(), expectedRandom.State());
    EXPECT_EQ(Other().animation.action, 0);
}

// 0x4cb853..0x4cb864 requests the sound even if Player.bmd lacks animation 0x98.
TEST_F(ActionReplyTest, ShieldSetsItsSoundWhenTheModelLacksItsAnimation)
{
    m_models.player.actions.resize(0x98);
    Other().animation.action = 3;
    Other().animation.frame = 7.5f;
    m_world->Runtime().lastSoundId = Client::Audio::SoundId{0x44};

    ASSERT_TRUE(Apply(3, 0x12).HasValue());

    EXPECT_EQ(Other().animation.action, 3);
    EXPECT_EQ(Other().animation.frame, 7.5f);
    EXPECT_EQ(m_world->Runtime().lastSoundId, Client::Audio::SoundId{0x6f});
}

// 0x4ea030 does not reset the frame if the reply asks for the current action again, but the previous stores still run.
TEST_F(ActionReplyTest, RepeatingTheCurrentAnimationStillUpdatesTheReplyPosition)
{
    Other().animation.action = 5;
    Other().animation.frame = 3.5f;

    ASSERT_TRUE(Apply(0xff, 0x7e).HasValue());

    EXPECT_EQ(Other().movement, 0);
    EXPECT_EQ(Other().angles.z, 11430.0f);
    EXPECT_EQ(Other().position, glm::vec3(1350.0f, 1450.0f, 3.0f));
    EXPECT_EQ(Other().animation.action, 5);
    EXPECT_EQ(Other().animation.frame, 3.5f);
}

// 0x4cb812: 0x7a and 0x7b go to 0x4f2250, with its suffix (one rand()); without weapons, 1 and 2 with the elf.
TEST_F(ActionReplyTest, TheStopActionsGoThroughSetActorStop)
{
    for (const std::int32_t map : {0, 2})
    {
        m_world->Runtime().map = map;
        for (const std::uint8_t action : {std::uint8_t{0x7a}, std::uint8_t{0x7b}})
        {
            for (const std::uint8_t characterClass : {std::uint8_t{0}, ElfClass})
            {
                SCOPED_TRACE(testing::Message()
                             << "mapa " << map << ", clase " << int{characterClass} << ", acción " << int{action});
                const auto before = m_cloth.random;
                Other().characterClass = characterClass;
                Other().animation.action = 0x20;
                ASSERT_TRUE(Apply(3, action).HasValue());
                EXPECT_EQ(Other().animation.action, characterClass == ElfClass ? 2 : 1);
                auto expected = before;
                static_cast<void>(expected.Next());
                EXPECT_EQ(m_cloth.random.State(), expected.State());
            }
        }
    }
    EXPECT_TRUE(m_sent.empty());
}

TEST_F(ActionReplyTest, AStopReplyUsesTheTransformedActorsSevenActionModel)
{
    Other().type = TransformedModel;
    Other().animation.action = 2;

    const auto stopped = Apply(3, 0x7a);

    ASSERT_TRUE(stopped.HasValue()) << stopped.Error();
    EXPECT_EQ(Other().animation.action, 0);
}

// Sitting (+0x7c with 0x200000), 0x4f2250 sets 0xca and sends the C1 18 0x82 with the angle the 18 just set.
TEST_F(ActionReplyTest, ASittingActorSendsTheSitAction)
{
    Other().flags7c = SittingFlag;
    for (const std::int32_t map : {0, 2})
    {
        m_world->Runtime().map = map;
        m_sent.clear();
        Other().animation.action = 1;
        ASSERT_TRUE(Apply(3, 0x7a).HasValue());
        EXPECT_EQ(Other().animation.action, 0xca);
        EXPECT_EQ(m_sent, (std::vector<Client::Tests::SentAction>{{3, 0x82}}));
    }

    m_withSender = false;
    Other().animation.action = 1;
    const auto unsent = Apply(3, 0x7a);
    ASSERT_FALSE(unsent.HasValue());
    EXPECT_NE(unsent.Error().find("0x4f2416"), std::string::npos);
}

// 0x4ea030 checks the actions of the character's model: those it lacks are not set. A missing model is the original's
// zeroed record (+0x26, no actions): it sets nothing and does not close the session.
TEST_F(ActionReplyTest, TheAnimationUsesTheModelOfTheActor)
{
    Other().type = SmallModel;
    ASSERT_TRUE(Apply(3, 0x7e).HasValue());
    EXPECT_EQ(Other().animation.action, 5);
    ASSERT_TRUE(Apply(3, 0x83).HasValue());
    EXPECT_EQ(Other().animation.action, 5);

    Other().type = 0x99;
    const auto applied = Apply(3, 0x7e);
    ASSERT_TRUE(applied.HasValue()) << applied.Error();
    EXPECT_EQ(Other().animation.action, 5);
    EXPECT_EQ(m_cloth.random.State(), 1u);
}

TEST_F(ActionReplyTest, AMonsterTakesTheActionsOfItsLoadedModel)
{
    // Action 11, which the map library's model (ten actions) lacks.
    ReplyModels fauna;
    fauna.small.actions.resize(12);
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        m_world->Runtime().map = map;
        m_states.assets.fauna.Bind(&fauna);
        Other().type = SmallModel;
        Other().animation = {};
        const auto applied = Apply(3, 11);
        m_states.assets.fauna.Bind(nullptr);
        ASSERT_TRUE(applied.HasValue()) << applied.Error();
        EXPECT_EQ(Other().animation.action, 11);
    }
}
} // namespace
