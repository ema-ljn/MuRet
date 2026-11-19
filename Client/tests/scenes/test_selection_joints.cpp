#include "Scenes/SelectionWorld.h"

#include "Core/Expected.h"
#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Render/Models/Cloth.h"
#include "Scenes/ActorState.h"
#include "Scenes/FaunaJointCreation.h"
#include "Scenes/GameDrawTail.h"
#include "Scenes/JointKernels.h"
#include "Scenes/WorldState.h"
#include "World/IFishStepServices.h"

#include "support/FakeTextureStore.h"
#include "support/RecordingSink.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <memory>

// El paso de los joints de la selección (0x634310 → 0x53c670): el mismo de 0x637123, con los mismos globales que el
// juego (06, "Los joints de las familias").
namespace
{
using Client::Scenes::ActorState;
using Client::Scenes::JointOwnerKind;
using Client::Scenes::JointState;

// El 0x614/0xc con un personaje de dueño se pone en el modelo compartido del tipo del dueño (0x536b1d, 0x5769ad8): en
// la selección también, sin cortar.
TEST(SelectionJointsTest, TheRedBoltFindsTheSharedModelsInTheSelection)
{
    Client::Tests::RecordingSink sink;
    Client::Core::Log log(sink, Client::Core::LogLevel::Debug);
    Client::Tests::FakeTextureStore store;
    Client::Scenes::SelectionWorld selection(store, log);
    auto world = std::make_unique<Client::Scenes::WorldState>();
    auto cloth = std::make_unique<Client::Render::Models::ClothWorld>();
    ActorState& owner = world->Actors()[1];
    owner.alive = true;
    owner.type = 0x1a5;
    const glm::vec3 origin{700.25f, 800.5f, 900.75f};
    world->PlaceModel(owner.type, {.scale = 1.5f, .origin = origin});
    glm::vec3 from{10.0f, 40.0f, 30.0f};
    glm::vec3 to{};
    glm::vec3 angles{};
    const Client::World::FishEffectRequest request{.type = 0x614,
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
    ASSERT_TRUE(Client::Scenes::CreateFaunaJoint(joints, request, cloth->random, JointOwnerKind::Actor).HasValue());

    const auto stepped = selection.StepEffects(*world, *cloth, 0.0f, {}, 0);

    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    const JointState& joint = joints[0];
    ASSERT_TRUE(joint.alive);
    // 40 de y de partida, menos que 50 · 1,5: llega en el primer tramo y queda en el origen del modelo compartido.
    EXPECT_EQ(joint.position, origin);
}
} // namespace
