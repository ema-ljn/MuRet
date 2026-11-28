#include "Scenes/SelectionWorld.h"
#include "Terrain/DynamicWorldLight.h"

#include "Core/Expected.h"
#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Models/Cloth.h"
#include "Kernel/ActorState.h"
#include "Scenes/FaunaJointCreation.h"
#include "Scenes/GameDrawTail.h"
#include "Scenes/JointKernels.h"
#include "Kernel/WorldState.h"
#include "Kernel/JointRequest.h"

#include "support/FakeTextureStore.h"
#include "support/RecordingSink.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <memory>

// El paso de los joints de la selección (0x634310 → 0x53c670): el mismo de 0x637123, con los mismos globales que el
// juego (06, "Los joints de las familias").
namespace
{
using Client::Kernel::ActorState;
using Client::Kernel::JointOwnerKind;
using Client::Scenes::JointState;

// El 0x614/0xc con un personaje de dueño se pone en el modelo compartido del tipo del dueño (0x536b1d, 0x5769ad8): en
// la selección también, sin cortar.
TEST(SelectionJointsTest, TheRedBoltFindsTheSharedModelsInTheSelection)
{
    Client::Tests::RecordingSink sink;
    Client::Core::Log log(sink, Client::Core::LogLevel::Debug);
    Client::Tests::FakeTextureStore store;
    Client::Scenes::SelectionWorld selection(store, log);
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
    ASSERT_TRUE(Client::Scenes::CreateFaunaJoint(joints, request, cloth->random, JointOwnerKind::Actor).HasValue());

    const auto stepped = selection.StepEffects(*world, light.Lighting(), *cloth, 0.0f, {}, 0);

    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    const JointState& joint = joints[0];
    ASSERT_TRUE(joint.alive);
    // 40 de y de partida, menos que 50 · 1,5: llega en el primer tramo y queda en el origen del modelo compartido.
    EXPECT_EQ(joint.position, origin);
}

// 0x53c670 en la selección (0x634310) llega a los mismos casos que el juego, pero MuRet no le da el asedio y los
// blancos del héroe (JointKernels::VolleyServices) ni los modelos del 0x20b: el 0x613/0 de ancho 80 corta donde los
// lee (0x539a50), el 0x61c/0 del héroe en su primera vuelta (0x53b569) y la 0x632/0 con dueño al llegar al suelo
// (0x53c1c6), con la dirección de la selección detrás.
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
    // El 0x613/0 de ancho 80 suelta el 0xd0 y lee el asedio; el 0x61c/0 (vida 20, vuelta 0; su dueño va por el tipo)
    // compara al dueño con el héroe; la 0x632/0 cae y toca el suelo con la cola prendida.
    for (const Case& tested :
         {Case{.type = 0x613, .subtype = 0, .width = 80.0f, .kind = JointOwnerKind::Actor, .cut = "0x539a50"},
          Case{.type = 0x61c, .subtype = 0, .width = 150.0f, .kind = JointOwnerKind::ByType, .cut = "0x53b569"},
          Case{.type = 0x632, .subtype = 0, .width = 120.0f, .kind = JointOwnerKind::Actor, .cut = "0x53c1c6"}})
    {
        SCOPED_TRACE(tested.cut);
        Client::Tests::RecordingSink sink;
        Client::Core::Log log(sink, Client::Core::LogLevel::Debug);
        Client::Tests::FakeTextureStore store;
        Client::Scenes::SelectionWorld selection(store, log);
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
        // El 0x613/0 nace leyendo el registro propio (0x532675), como en el juego; los otros dos no lo miran.
        std::uint8_t ownRecordDepth = 0;
        const std::uint8_t ownVolley = 0;
        const Client::Scenes::JointKernels::CreationServices services{.ownRecordDepth = &ownRecordDepth,
                                                                      .ownVolley = &ownVolley};
        auto& joints = world->DrawTail().joints;
        const auto created = Client::Scenes::CreateFaunaJoint(joints, request, cloth->random, tested.kind, &services);
        ASSERT_TRUE(created.HasValue()) << created.Error();

        const auto stepped =
            selection.StepEffects(*world, light.Lighting(), *cloth, 0.0f, [](float, float) { return 5000.0; }, 0);

        ASSERT_FALSE(stepped.HasValue());
        EXPECT_TRUE(stepped.Error().starts_with(tested.cut)) << stepped.Error();
        EXPECT_TRUE(stepped.Error().ends_with(", en la selección (0x53c670)")) << stepped.Error();
    }
}
} // namespace
