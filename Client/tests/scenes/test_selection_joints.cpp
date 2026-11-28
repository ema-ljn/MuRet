#include "FrontEnd/SelectionWorld.h"
#include "Terrain/DynamicWorldLight.h"

#include "Core/Expected.h"
#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Models/Cloth.h"
#include "Kernel/ActorState.h"
#include "Joints/FaunaJointCreation.h"
#include "Kernel/DrawTailState.h"
#include "Joints/JointKernels.h"
#include "Kernel/WorldState.h"
#include "Kernel/JointRequest.h"

#include "support/FakeTextureStore.h"
#include "support/RecordingSink.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <memory>

// The selection's joint step (0x634310 → 0x53c670): the same as 0x637123, with the same globals as the game (06,
// "Los joints de las familias").
namespace
{
using Client::Kernel::ActorState;
using Client::Kernel::JointOwnerKind;
using Client::Kernel::JointState;

// 0x614/0xc with a character owner goes on the shared model of the owner's type (0x536b1d, 0x5769ad8): in the
// selection too, without stopping.
TEST(SelectionJointsTest, TheRedBoltFindsTheSharedModelsInTheSelection)
{
    Client::Tests::RecordingSink sink;
    Client::Core::Log log(sink, Client::Core::LogLevel::Debug);
    Client::Tests::FakeTextureStore store;
    Client::FrontEnd::SelectionWorld selection(store, log);
    auto world = std::make_unique<Client::Kernel::WorldState>();
    Client::Terrain::DynamicWorldLight light;
    auto cloth = std::make_unique<Client::Models::ClothWorld>();
    ActorState& owner = world->Actors()[1];
    owner.alive = true;
    owner.type = 0x1a5;
    const glm::vec3 origin{700.25f, 800.5f, 900.75f};
    world->PlaceModel(owner.type, {.scale = 1.5f, .origin = origin});
    glm::vec3 from{10.0f, 40.0f, 30.0f};
    glm::vec3 to{};
    glm::vec3 angles{};
    const Client::Kernel::JointRequest request{.type = 0x614,
                                               .firstPosition = from,
                                               .secondPosition = to,
                                               .angles = angles,
                                               .subtype = 0xc,
                                               .ownerContext = &owner,
                                               .scale = 30.0f,
                                               .key = -1,
                                               .skill = 0,
                                               .skillSerial = 0,
                                               .target = -1};
    auto& joints = world->DrawTail().joints;
    ASSERT_TRUE(Client::Joints::CreateFaunaJoint(joints, request, cloth->random, JointOwnerKind::Actor).HasValue());

    const auto stepped = selection.StepEffects(*world, light.Lighting(), *cloth, 0.0f, {}, 0);

    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    const JointState& joint = joints[0];
    ASSERT_TRUE(joint.alive);
    // A starting y of 40, less than 50 · 1.5: it arrives in the first stretch and stays at the shared model's origin.
    EXPECT_EQ(joint.position, origin);
}

// 0x53c670 in the selection (0x634310) reaches the same cases as the game, but MuRet does not give it the siege and
// the hero's targets (JointKernels::VolleyServices) nor the 0x20b models: the 0x613/0 of width 80 stops where it reads
// them (0x539a50), the hero's 0x61c/0 on its first pass (0x53b569) and the 0x632/0 with an owner on reaching the
// ground (0x53c1c6), with the selection's address behind.
TEST(SelectionJointsTest, TheSelectionCutsWhereTheJointsAskForTheVolleyOrTheShadowModels)
{
    struct Case
    {
        std::int32_t type;
        std::int32_t subtype;
        float width;
        JointOwnerKind kind;
        const char* cut;
    };
    // The 0x613/0 of width 80 drops 0xd0 and reads the siege; the 0x61c/0 (life 20, pass 0; its owner goes by type)
    // compares the owner with the hero; the 0x632/0 falls and touches the ground with its tail lit.
    for (const Case& tested :
         {Case{.type = 0x613, .subtype = 0, .width = 80.0f, .kind = JointOwnerKind::Actor, .cut = "0x539a50"},
          Case{.type = 0x61c, .subtype = 0, .width = 150.0f, .kind = JointOwnerKind::ByType, .cut = "0x53b569"},
          Case{.type = 0x632, .subtype = 0, .width = 120.0f, .kind = JointOwnerKind::Actor, .cut = "0x53c1c6"}})
    {
        SCOPED_TRACE(tested.cut);
        Client::Tests::RecordingSink sink;
        Client::Core::Log log(sink, Client::Core::LogLevel::Debug);
        Client::Tests::FakeTextureStore store;
        Client::FrontEnd::SelectionWorld selection(store, log);
        auto world = std::make_unique<Client::Kernel::WorldState>();
        Client::Terrain::DynamicWorldLight light;
        auto cloth = std::make_unique<Client::Models::ClothWorld>();
        constexpr std::size_t HeroSlot = 3;
        world->Runtime().hero = HeroSlot;
        ActorState& owner = world->Actors()[HeroSlot];
        owner.alive = true;
        glm::vec3 from{1500.0f, 1600.0f, 1220.0f};
        glm::vec3 angles{};
        const Client::Kernel::JointRequest request{.type = tested.type,
                                                   .firstPosition = from,
                                                   .secondPosition = from,
                                                   .angles = angles,
                                                   .subtype = tested.subtype,
                                                   .ownerContext = &owner,
                                                   .scale = tested.width,
                                                   .key = -1,
                                                   .skill = 0,
                                                   .skillSerial = 0,
                                                   .target = -1};
        // The 0x613/0 is born reading the own record (0x532675), as in the game; the other two do not look at it.
        std::uint8_t ownRecordDepth = 0;
        const std::uint8_t ownVolley = 0;
        const Client::Joints::JointKernels::CreationServices services{.ownRecordDepth = &ownRecordDepth,
                                                                      .ownVolley = &ownVolley};
        auto& joints = world->DrawTail().joints;
        const auto created = Client::Joints::CreateFaunaJoint(joints, request, cloth->random, tested.kind, &services);
        ASSERT_TRUE(created.HasValue()) << created.Error();

        const auto stepped =
            selection.StepEffects(*world, light.Lighting(), *cloth, 0.0f, [](float, float) { return 5000.0; }, 0);

        ASSERT_FALSE(stepped.HasValue());
        EXPECT_TRUE(stepped.Error().starts_with(tested.cut)) << stepped.Error();
        EXPECT_TRUE(stepped.Error().ends_with(", en la selección (0x53c670)")) << stepped.Error();
    }
}
} // namespace
