#include "Core/Expected.h"
#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Data/Models/Model.h"
#include "Render/ModelBatch.h"
#include "Scenes/FaunaDrawing.h"
#include "Scenes/FaunaJointCreation.h"
#include "Scenes/GameDrawTail.h"
#include "Scenes/GameParticles.h"
#include "Scenes/NoriaObjectServices.h"
#include "Scenes/WorldModels.h"
#include "World/ObjectModelStepState.h"
#include "World/Sprites.h"
#include "World/TerrainHeight.h"
#include "support/FakeTextureStore.h"
#include "support/RecordingSink.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/Skeleton.h"
#include "Render/TextureSlot.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/WorldObjectsDrawing.h"
#include "World/Particles.h"
#include "World/TerrainLighting.h"
#include "World/WorldObjects.h"
#include "World/WorldTerrain.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <set>
#include <string>
#include <span>
#include <vector>

namespace
{
// El batch usa el mismo walker de producción; la falta de esqueleto permite aislar los servicios.
class ObjectModels final : public Client::Scenes::IModelLibrary
{
public:
    const Client::Data::Models::Model* Find(std::uint16_t) const override
    {
        return nullptr;
    }
    const Client::Render::Models::Skeleton* SkeletonOf(std::uint16_t) const override
    {
        return nullptr;
    }
    std::span<const Client::Render::TextureSlot> MeshTextures(std::uint16_t) const override
    {
        return {};
    }
};

TEST(ObjectStepIntegrationTest, EveryRealLorenciaAndDeviasObjectCanStepWithDurableServices)
{
    const ObjectModels models;
    for (const std::uint8_t map : {std::uint8_t{0}, std::uint8_t{2}})
    {
        SCOPED_TRACE(map);
        const auto loaded = Client::World::LoadWorldObjects(std::filesystem::path{MURET_CLIENT_REPO_DATA_DIR}, map);
        ASSERT_TRUE(loaded.HasValue()) << loaded.Error();
        auto created = Client::World::CreateWorldObjects(loaded.Value(), map, Client::World::ObjectScene::Other);
        ASSERT_FALSE(created.failure.has_value());
        ASSERT_GT(created.objects.size(), 100u);
        for (auto& object : created.objects)
        {
            object.seen = true;
        }
        auto lighting = std::make_unique<Client::World::TerrainLighting>();
        auto particles = std::make_unique<Client::World::Particles>();
        const glm::vec3 hero{13500.0f, 12500.0f, 0.0f};
        std::vector<std::int32_t> sounds;
        Client::World::ObjectStepGlobals globals;
        globals.dynamicLighting = lighting.get();
        globals.particles = particles.get();
        globals.heroPosition = &hero;
        globals.sound = [&](std::int32_t id) { sounds.push_back(id); };
        Client::Render::Models::CrtRandom random(7);
        for (int step = 0; step < 4; ++step)
        {
            globals.worldClock = static_cast<float>(step * 40);
            const auto stepped = Client::Scenes::StepGameWorldObjects(created.objects, map, models, random, globals);
            ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        }
        EXPECT_NE(random.State(), 7u);
        EXPECT_TRUE(std::ranges::any_of(particles->All(), [](const auto& particle) { return particle.alive; }));
        EXPECT_TRUE(std::ranges::any_of(lighting->cells, [](const auto& color) { return color[0] > 0.0f; }));
    }
}
TEST(ObjectStepIntegrationTest, RealNoriaObjectsDrawAndStepWithTheFairyJointsAndTerrainParticles)
{
    const std::filesystem::path root{MURET_CLIENT_REPO_DATA_DIR};
    Client::Tests::FakeTextureStore textures;
    Client::Tests::RecordingSink sink;
    Client::Core::Log log(sink, Client::Core::LogLevel::Debug);
    Client::Scenes::WorldModels models;
    const auto modelLoad = models.Load(root, 3, textures, log, nullptr);
    ASSERT_TRUE(modelLoad.HasValue()) << modelLoad.Error();
    const auto records = Client::World::LoadWorldObjects(root, 3);
    ASSERT_TRUE(records.HasValue()) << records.Error();
    ASSERT_EQ(records.Value().size(), 9399u);
    auto created = Client::World::CreateWorldObjects(records.Value(), 3, Client::World::ObjectScene::Other);
    ASSERT_FALSE(created.failure.has_value());
    Client::World::ObjectModelStepState state;
    auto particles = std::make_unique<Client::World::Particles>();
    Client::World::ObjectStepGlobals globals;
    globals.modelState = &state;
    globals.particles = particles.get();
    for (auto& object : created.objects)
        object.seen = true;
    Client::Render::Models::CrtRandom random(7);
    const auto stepped = Client::Scenes::StepGameWorldObjects(created.objects, 3, models, random, globals);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(state.unlitMeshes.at(0x2a), 0);
    EXPECT_EQ(state.unlitMeshes.at(0x2b), 0);
    std::set<std::int16_t> types;
    std::vector<Client::World::WorldObject> representatives;
    for (const auto& object : created.objects)
        if (types.insert(object.type).second)
            representatives.push_back(object);
    ASSERT_EQ(representatives.size(), 44u);
    Client::Scenes::FaunaDrawCarry pose;
    Client::World::FrameSprites sprites;
    std::vector<Client::Scenes::JointState> joints(100);
    Client::Scenes::GameObjectPass pass;
    pass.map = 3;
    pass.random = &random;
    pass.particles = particles.get();
    pass.sprites = &sprites;
    pass.retainedPose = &pose;
    pass.modelState = &state;
    pass.onPose = [&](const Client::Scenes::DrawnObject& drawn)
    { return Client::Scenes::PublishFaunaPose(pose, models, drawn, true); };
    int jointCalls = 0;
    pass.createJoint = [&](const Client::World::FishEffectRequest& request) -> Client::Core::Expected<void, std::string>
    {
        const auto made = Client::Scenes::CreateFaunaJoint(joints, request, random);
        if (!made.HasValue())
            return Client::Core::Unexpected(made.Error());
        ++jointCalls;
        return {};
    };
    Client::Render::ModelBatch batch;
    const auto drawn = Client::Scenes::DrawGameWorldObjects(
        batch, models, representatives, Client::World::TerrainPolygon{.everything = true}, nullptr, {}, pass);
    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    EXPECT_FALSE(batch.Vertices().empty());
    EXPECT_TRUE(sprites.Any());
    const auto fairy = std::ranges::find_if(representatives, [](const auto& object) { return object.type == 0x27; });
    ASSERT_NE(fairy, representatives.end());
    Client::Scenes::DrawnObject fairyPose;
    fairyPose.model = 0x27;
    fairyPose.transform = fairy->transform;
    fairyPose.animation = fairy->animation;
    ASSERT_TRUE(Client::Scenes::PublishFaunaPose(pose, models, fairyPose, true).HasValue());
    random = Client::Render::Models::CrtRandom(4);
    const auto emitted = Client::Scenes::StepNoriaObjectFrame(*fairy, 61, pass);
    ASSERT_TRUE(emitted.HasValue()) << emitted.Error();
    EXPECT_GE(jointCalls, 8);
    EXPECT_TRUE(std::ranges::any_of(joints, [](const auto& joint) { return joint.alive && joint.type == 0x617; }));
    EXPECT_TRUE(std::ranges::any_of(particles->All(),
                                    [](const auto& particle) { return particle.alive && particle.type == 0x5c5; }));
    const auto terrain = Client::World::LoadWorldTerrain(root, 3);
    ASSERT_TRUE(terrain.HasValue()) << terrain.Error();
    const std::int32_t map = 3;
    const auto particleStep = Client::Scenes::StepGameParticles(
        *particles, random, map, [&](float x, float y)
        { return Client::World::TerrainHeightAt(*terrain.Value().heights, *terrain.Value().attributes, x, y); });
    ASSERT_TRUE(particleStep.HasValue()) << particleStep.Error();
    ASSERT_TRUE(Client::Scenes::StepJoints(joints, random).HasValue());
}
} // namespace
