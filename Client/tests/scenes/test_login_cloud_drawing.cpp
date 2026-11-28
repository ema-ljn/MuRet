#include "Models/IModelLibrary.h"
#include "support/CheckedWorldObjectProjection.h"
#include "support/PlayerActors.h"

#include "Crt/CrtRandom.h"
#include "WorldObjects/WorldObjectsDrawing.h"
#include "Effects/ParticleDrawServices.h"

#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Scenes/LoginScene.h"
#include "Scenes/SceneContext.h"
#include "Tables/GameData.h"
#include "Assets/TextureSets.h"
#include "support/FakeTextureStore.h"
#include "support/RecordingFrameTarget.h"
#include "support/RecordingSink.h"
#include "support/SceneDataFiles.h"
#include "support/TemporaryFolder.h"
#include "support/TerrainFiles.h"
#include "Data/Models/Model.h"
#include "Data/Terrain/TerrainObjects.h"
#include "Render/ModelBatch.h"
#include "Render/Billboard.h"
#include "Render/ModelVertex.h"
#include "Models/Animation.h"
#include "Models/Cloth.h"
#include "Models/Skeleton.h"
#include "Render/Pass3D.h"
#include "Render/TextureSlot.h"
#include "Models/TextureMetadata.h"
#include "Models/ModelId.h"
#include "Data/Textures/Texture.h"
#include "Effects/ModelEffects.h"
#include "Particles/Particles.h"
#include "Terrain/TerrainCulling.h"
#include "Effects/WizardModelEffects.h"
#include "WorldObjects/WorldObjects.h"
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>
#include <gtest/gtest.h>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

// The emitter of 0x602ed0 and the eye pass of 0x548550(0), without GL.
namespace
{
constexpr std::uint8_t LoginMap = 55;
constexpr std::int16_t CloudMarker = 0x54;
constexpr std::int16_t MeteorMarker = 0x59;
constexpr std::int32_t CloudType = 0x622;
constexpr std::int32_t CloudSubtype = 10;
class CloudTextures final : public Client::Models::IModelLibrary
{
public:
    const Client::Data::Models::Model* Find(Client::Models::ModelId) const override
    {
        return nullptr;
    }
    const Client::Models::Skeleton* SkeletonOf(Client::Models::ModelId) const override
    {
        return nullptr;
    }
    std::span<const Client::Render::TextureSlot> MeshTextures(Client::Models::ModelId) const override
    {
        return {};
    }
    // clouds.jpg: 64 × 32 and 3 components, bound in its selector's slot, as in GameModels.
    std::optional<Client::Models::TextureMetadata> TextureMetadataOf(std::uint32_t selector) const override
    {
        if (selector != static_cast<std::uint32_t>(CloudType))
        {
            return std::nullopt;
        }
        Client::Models::TextureMetadata metadata(Client::Data::Textures::Texture{});
        metadata.size = glm::vec2(64.0f, 32.0f);
        metadata.components = 3;
        return metadata;
    }
    std::optional<Client::Render::TextureSlot> TextureSlotOf(std::uint32_t selector) const override
    {
        return TextureMetadataOf(selector) ? std::optional(Client::Render::TextureSlot{CloudType}) : std::nullopt;
    }
    std::optional<glm::vec2> TextureSize(Client::Render::TextureSlot slot) const override
    {
        const auto metadata = TextureMetadataOf(slot.value);
        return metadata ? std::optional(metadata->size) : std::nullopt;
    }
};
Client::Kernel::WorldObject Marker(std::int16_t type = CloudMarker)
{
    Client::Data::Terrain::TerrainObject record;
    record.type = type;
    record.position = {1000.0f, 2000.0f, 300.0f};
    record.angle = {10.0f, 20.0f, 30.0f};
    record.scale = 2.5f;
    return Client::Tests::CheckedWorldObjectProjection(std::span(&record, 1), LoginMap,
                                                       Client::WorldObjects::ObjectScene::Login)
        .front();
}
void Emit(Client::Kernel::WorldObject& object, Client::Particles::Particles& particles, Client::Crt::CrtRandom& random,
          std::uint8_t map = LoginMap, const Client::Terrain::TerrainPolygon& polygon = {.everything = true})
{
    const CloudTextures models;
    Client::Render::ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});
    Client::WorldObjects::DrawWorldObjects(
        batch, models, std::span(&object, 1), polygon, nullptr, map, std::chrono::milliseconds{1234}, 0.0f,
        Client::WorldObjects::ObjectFrameEffects{.random = &random, .particles = &particles});
}
// Scene 2 only controls the lantern. Seed 4: common rand 51, guard 17945 and five creation rands.
TEST(LoginCloudDrawingTest, ASeenHiddenMarkerEmitsOutsideSceneTwoAfterTheCommonRand)
{
    auto object = Marker();
    Client::Particles::Particles particles;
    Client::Crt::CrtRandom random(4);
    Emit(object, particles, random);
    EXPECT_TRUE(object.seen);
    ASSERT_TRUE(particles.Any());
    const auto& particle = particles.All()[0];
    EXPECT_EQ(particle.type, CloudType);
    EXPECT_EQ(particle.subtype, CloudSubtype);
    EXPECT_EQ(particle.position, glm::vec3(1159.0f, 2186.0f, 245.0f));
    EXPECT_EQ(particle.angles, object.transform.angles);
    EXPECT_FLOAT_EQ(particle.scale, 3.7666667f);
    EXPECT_EQ(particle.light, glm::vec3(0.0f));
    EXPECT_EQ(random.State(), 0x514f647du);
}
TEST(LoginCloudDrawingTest, ARejectedRollStillConsumesTheCloudGuard)
{
    auto object = Marker();
    Client::Particles::Particles particles;
    Client::Crt::CrtRandom random(1);
    Emit(object, particles, random);
    EXPECT_FALSE(particles.Any());
    EXPECT_EQ(random.State(), 0xc823f683u) << "41 común y 18467 % 5 distinto de cero";
}
TEST(LoginCloudDrawingTest, AMissingPoolStillConsumesTheCloudGuard)
{
    const CloudTextures models;
    auto object = Marker();
    Client::Crt::CrtRandom random(4);
    Client::Render::ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});
    Client::WorldObjects::DrawWorldObjects(batch, models, std::span(&object, 1), {.everything = true}, nullptr,
                                           LoginMap, {}, 0.0f,
                                           Client::WorldObjects::ObjectFrameEffects{.random = &random});
    EXPECT_TRUE(object.seen);
    EXPECT_EQ(random.State(), 0xc6192e9eu) << "común51 y guard17945, sin los cinco rand de creación";
}

// 0x6043ff..0x604427: map 55's 0x59 with ftol(clock) % 8000 from 7950 drops a 0xc2/9 meteor; without the model effects
// table (ObjectFrameEffects::modelEffects) it has nowhere to put it: it cuts before its three rand(), after the common
// one, through ObjectFrameEffects::onFailure. Outside that window (7949) or on another map, only the common rand.
TEST(LoginCloudDrawingTest, TheMeteorMarkerCutsInsideItsWindow)
{
    struct Case
    {
        std::uint8_t map;
        std::int64_t clock;
        bool cut;
    };
    for (const Case& tested : {Case{LoginMap, 7950, true}, Case{LoginMap, 15999, true}, Case{LoginMap, 7949, false},
                               Case{LoginMap, 8000, false}, Case{54, 7950, false}})
    {
        SCOPED_TRACE(tested.clock);
        SCOPED_TRACE(static_cast<int>(tested.map));
        auto object = Marker(MeteorMarker);
        const CloudTextures models;
        Client::Particles::Particles particles;
        Client::Crt::CrtRandom random(4);
        Client::Crt::CrtRandom common(4);
        static_cast<void>(common.Next());
        std::vector<std::string> failures;
        Client::Render::ModelBatch batch;
        batch.Begin(Client::Render::Pass3D{});
        Client::WorldObjects::DrawWorldObjects(
            batch, models, std::span(&object, 1), {.everything = true}, nullptr, tested.map,
            std::chrono::milliseconds{tested.clock}, 0.0f,
            Client::WorldObjects::ObjectFrameEffects{
                .random = &random, .particles = &particles, .onFailure = [&](const std::string& error) {
                    failures.push_back(error);
                }});
        EXPECT_EQ(random.State(), common.State());
        EXPECT_FALSE(particles.Any());
        ASSERT_EQ(failures.size(), tested.cut ? 1U : 0U);
        if (tested.cut)
        {
            EXPECT_NE(failures[0].find("0x604427"), std::string::npos) << failures[0];
            EXPECT_NE(failures[0].find("CL11"), std::string::npos) << failures[0];
        }
    }
}

// Map 55's 0x59 drawn once with the clock `clock`, with the model effects table.
std::vector<std::string> DropTheMeteor(Client::Kernel::WorldObject& object, Client::Effects::ModelEffects& effects,
                                       Client::Crt::CrtRandom& random, std::int64_t clock)
{
    const CloudTextures models;
    Client::Particles::Particles particles;
    std::vector<std::string> failures;
    Client::Render::ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});
    Client::WorldObjects::DrawWorldObjects(
        batch, models, std::span(&object, 1), {.everything = true}, nullptr, LoginMap, std::chrono::milliseconds{clock},
        0.0f,
        Client::WorldObjects::ObjectFrameEffects{.random = &random,
                                                 .particles = &particles,
                                                 .modelEffects = &effects,
                                                 .onFailure = [&](const std::string& error)
                                                 { failures.push_back(error); }});
    EXPECT_FALSE(particles.Any());
    return failures;
}

// 0x604427..0x6044b2: in the window, after the common rand, x and y with (rand() & 0x7ff) − 1024 and z with
// rand() % 600 + 3000 over the object, and 0x518cf0(0xc2, point, angles, light, 9, 0, −1): case 9 (0x51b280) goes on
// with its five rand(). With seed 4: common 51; 17945, 27159 and 386 give (1537; 1511; 3686); the case, 17345 (scale
// 25 · 0.13f), 27504 (x + 16 + 130), 20815 (z + 15 + 400), 20576 (vy −1 − 8) and 10960 (vz −10 − 0). The replica
// passes the same point through the wizards' creation in a twin table with the same CRT.
TEST(LoginCloudDrawingTest, TheMeteorMarkerDropsAMeteorInsideItsWindow)
{
    auto object = Marker(MeteorMarker);
    object.light = {0.25f, 0.5f, 0.75f};
    Client::Effects::ModelEffects effects;
    Client::Crt::CrtRandom random(4);
    EXPECT_TRUE(DropTheMeteor(object, effects, random, 15999).empty());
    EXPECT_EQ(random.State(), Client::Tests::AfterDraws(4, 9).State());
    Client::Effects::ModelEffects twin;
    Client::Crt::CrtRandom twinRandom = Client::Tests::AfterDraws(4, 4);
    ASSERT_TRUE(Client::Effects::CreateWizardModelEffect(twin,
                                                         {.type = Client::Effects::FireEffect,
                                                          .position = {1537.0f, 1511.0f, 3686.0f},
                                                          .angles = object.transform.angles,
                                                          .light = object.light,
                                                          .subtype = 9},
                                                         twinRandom, {})
                    .HasValue());
    EXPECT_EQ(twinRandom.State(), random.State());
    const Client::Effects::ModelEffect& meteor = effects.Effects()[0];
    const Client::Effects::ModelEffect& expected = twin.Effects()[0];
    ASSERT_TRUE(meteor.alive);
    EXPECT_FALSE(effects.Effects()[1].alive);
    EXPECT_EQ(meteor.type, Client::Effects::FireEffect);
    EXPECT_EQ(meteor.subtype, 9);
    EXPECT_EQ(meteor.life, 600);
    EXPECT_EQ(meteor.transform.position, glm::vec3(1683.0f, 1511.0f, 4101.0f));
    EXPECT_EQ(meteor.transform.scale, 3.25f);
    EXPECT_EQ(meteor.velocity, glm::vec3(0.0f, -9.0f, -10.0f));
    EXPECT_EQ(meteor.transform.position, expected.transform.position);
    EXPECT_EQ(meteor.transform.angles, expected.transform.angles);
    EXPECT_EQ(meteor.light, expected.light);
    EXPECT_EQ(meteor.velocity, expected.velocity);
    EXPECT_EQ(meteor.transform.scale, expected.transform.scale);
    EXPECT_EQ(meteor.ownerContext, nullptr);
    EXPECT_EQ(meteor.disturbanceState, expected.disturbanceState);
    EXPECT_EQ(meteor.objectState.blendMesh, expected.objectState.blendMesh);
}

// 0x518cf0 with the table full does nothing and takes no rand(): the point's three already came out
// (0x604427..0x604471).
TEST(LoginCloudDrawingTest, AFullTableTakesOnlyTheThreeRandsOfThePoint)
{
    auto object = Marker(MeteorMarker);
    Client::Effects::ModelEffects effects;
    for (Client::Effects::ModelEffect& effect : effects.Effects())
    {
        effect.alive = true;
        effect.type = -1;
    }
    Client::Crt::CrtRandom random(4);
    EXPECT_TRUE(DropTheMeteor(object, effects, random, 7950).empty());
    EXPECT_EQ(random.State(), Client::Tests::AfterDraws(4, 4).State());
    for (const Client::Effects::ModelEffect& effect : effects.Effects())
        EXPECT_EQ(effect.type, -1);
}

TEST(LoginCloudDrawingTest, OtherMapsAndTypesConsumeOnlyTheCommonRand)
{
    for (const bool otherMap : std::array{false, true})
    {
        SCOPED_TRACE(otherMap);
        auto object = Marker(otherMap ? CloudMarker : 0x55);
        Client::Particles::Particles particles;
        Client::Crt::CrtRandom random(4);
        Emit(object, particles, random, otherMap ? 54 : LoginMap);
        EXPECT_FALSE(particles.Any());
        EXPECT_EQ(random.State(), 0x0033aeb7u);
    }
}
TEST(LoginCloudDrawingTest, AnUnseenMarkerDoesNotReachTheEmitter)
{
    auto object = Marker();
    Client::Particles::Particles particles;
    Client::Crt::CrtRandom random(4);
    const auto polygon =
        Client::Terrain::TerrainPolygonOf(Client::Terrain::CullingScene::Game, 0, {14350.0f, 12050.0f, 165.0f}, -45.0f);
    Emit(object, particles, random, LoginMap, polygon);
    EXPECT_FALSE(object.seen);
    EXPECT_FALSE(particles.Any());
    EXPECT_EQ(random.State(), 4u);
}
TEST(LoginCloudDrawingTest, AFullPoolConsumesTheGuardWithoutCreationRandomness)
{
    auto object = Marker();
    Client::Particles::Particles particles;
    for (auto& particle : particles.All())
    {
        particle.alive = true;
        particle.type = -1;
    }
    Client::Crt::CrtRandom random(4);
    Emit(object, particles, random);
    EXPECT_EQ(random.State(), 0xc6192e9eu);
    for (const auto& particle : particles.All())
    {
        EXPECT_EQ(particle.type, -1);
    }
}
void SetCloud(Client::Particles::Particle& particle, float baseAngle)
{
    particle.alive = true;
    particle.type = CloudType;
    particle.texture = CloudType;
    particle.subtype = CloudSubtype;
    particle.position = {0.0f, 0.0f, -300.0f};
    particle.scale = 2.0f;
    particle.light = {0.4f, 0.4f, 0.4f};
    particle.rotationFactor = 2.0f;
    particle.start.y = baseAngle;
}
// Compacting the live ones would treat slot 1 as even. 0x620b10 replaces the JPEG's blend (0x620a90): each cloud
// publishes both before its quad, so the runs that draw go one by one.
TEST(LoginCloudDrawingTest, DrawingUsesSubtractiveCloudsAndRealPoolIndices)
{
    const CloudTextures textures;
    Client::Particles::Particles particles;
    SetCloud(particles.All()[1], 10.0f);
    SetCloud(particles.All()[3], 30.0f);
    SetCloud(particles.All()[4], 40.0f);
    Client::Render::ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});
    ASSERT_TRUE(
        Client::Effects::DrawParticles(batch, particles, textures, Client::Render::ViewRows(glm::mat4(1.0f)), 100.0f)
            .HasValue());
    std::size_t draws = 0;
    for (const auto& command : batch.Commands())
    {
        if (command.operation != Client::Render::ModelStateOperation::Draw)
        {
            EXPECT_EQ(command.vertexCount, 0u);
            continue;
        }
        ++draws;
        EXPECT_EQ(command.state.blend, Client::Render::ModelBlend::Subtractive);
        EXPECT_EQ(command.state.texture, Client::Render::TextureSlot{CloudType});
        EXPECT_EQ(command.vertexCount, 6u);
    }
    EXPECT_EQ(draws, 3u);
    EXPECT_FLOAT_EQ(particles.All()[1].rotation, 6.0f);
    EXPECT_FLOAT_EQ(particles.All()[3].rotation, 26.0f);
    EXPECT_FLOAT_EQ(particles.All()[4].rotation, 44.0f);
    ASSERT_EQ(batch.Vertices().size(), 18u);
    for (const auto& vertex : batch.Vertices())
    {
        EXPECT_FLOAT_EQ(vertex.color.red, 0.4f);
        EXPECT_FLOAT_EQ(vertex.position.z, -300.0f);
    }
    batch.Begin(Client::Render::Pass3D{});
    ASSERT_TRUE(
        Client::Effects::DrawParticles(batch, particles, textures, Client::Render::ViewRows(glm::mat4(1.0f)), 200.0f)
            .HasValue());
    EXPECT_FLOAT_EQ(particles.All()[1].rotation, 2.0f) << "no acumula el giro anterior";
    EXPECT_FLOAT_EQ(particles.All()[4].rotation, 48.0f);
}

// Two objects and two frames: marker/rock and then marker/rock; the second cloud uses 5261/28277/24162. Grouping each
// object's frames would use other offsets.
TEST(LoginCloudDrawingTest, CatchUpVisitsEveryObjectBeforeStartingTheNextOriginalFrame)
{
    const CloudTextures models;
    std::array objects{Marker(), Marker(1)};
    std::array<std::optional<int>, 2> lastRandoms;
    Client::Particles::Particles particles;
    Client::Crt::CrtRandom random(4);
    Client::Render::ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});
    Client::WorldObjects::DrawWorldObjects(
        batch, models, objects, {.everything = true}, nullptr, LoginMap, {}, 0.0f,
        Client::WorldObjects::ObjectFrameEffects{
            .random = &random, .particles = &particles, .frames = 2, .lastRandoms = lastRandoms});
    EXPECT_EQ(random.State(), 0x4d199f94u);
    ASSERT_TRUE(particles.All()[1].alive);
    EXPECT_EQ(particles.All()[1].position, glm::vec3(861.0f, 2077.0f, 262.0f));
    EXPECT_EQ(lastRandoms[0], 10960);
    EXPECT_EQ(lastRandoms[1], 19737);
}

std::size_t CloudsIn(const Client::Tests::RecordedFrame& frame)
{
    std::size_t count = 0;
    for (const auto& event : frame.events)
    {
        for (const auto& command : event.modelCommands)
        {
            // Only the runs that draw: 0x620a90's effects without geometry go with the additive blend.
            if (command.state.texture == Client::Render::TextureSlot{CloudType} &&
                command.operation == Client::Render::ModelStateOperation::Draw)
            {
                EXPECT_EQ(command.state.blend, Client::Render::ModelBlend::Subtractive);
                EXPECT_TRUE(event.pass.has_value());
                if (event.pass)
                {
                    EXPECT_EQ(event.pass->view, glm::mat4(1.0f));
                }
                count += command.vertexCount / 6;
            }
        }
    }
    return count;
}

// The marker needs no BMD: it is hidden, but its draw asks LoginScene's pool for clouds. A new Enter clears the
// earlier particles even though their texture stays in the game library.
TEST(LoginCloudDrawingTest, TheSceneEmitsInTheEyePassAndClearsThePoolOnEnteringAgain)
{
    Client::Tests::TemporaryFolder folder("login-cloud");
    Client::Tests::WriteTextureSet(folder.Path(), Client::Assets::LoginTextureSet);
    Client::Tests::WriteTextureSet(folder.Path(), Client::Assets::WindowTextureSet);
    const auto writeObjects = [&](bool withMarker)
    {
        Client::Data::Terrain::TerrainObject marker;
        marker.type = CloudMarker;
        marker.position = {11679.0f, 14191.0f, 333.0f};
        marker.scale = 1.0f;
        const std::span<const Client::Data::Terrain::TerrainObject> records(&marker, withMarker ? 1u : 0u);
        Client::Tests::WriteSceneFile(
            folder.Path(), "World56/EncTerrain56.obj",
            Client::Tests::EncryptK16(Client::Tests::PlainTerrainObjects(56, withMarker ? 1 : 0, records)));
    };
    writeObjects(true);
    Client::Tests::FakeTextureStore store;
    Client::Tests::RecordingSink sink;
    Client::Core::Log log(sink, Client::Core::LogLevel::Debug);
    Client::Models::ClothWorld cloth;
    const Client::Tables::GameData data;
    const CloudTextures textures;
    Client::Scenes::LoginScene scene(
        Client::Scenes::SceneContext{.dataRoot = folder.Path(), .textures = store, .log = log, .cloth = &cloth}, data,
        textures, nullptr, "0.0.0", 1);
    ASSERT_TRUE(scene.Enter().HasValue());
    Client::Tests::RecordingFrameTarget target;
    std::size_t clouds = 0;
    for (int frame = 0; frame < 16 && clouds == 0; ++frame)
    {
        scene.Render(target.BeginFrame(std::chrono::milliseconds{1234} + frame * Client::Models::OriginalFrameTime),
                     0.0f);
        target.Present();
        clouds = CloudsIn(target.frames.back());
    }
    ASSERT_GT(clouds, 0u);
    const auto stateAfterEmission = cloth.random.State();
    const auto sameClock = target.frames.back().clock;
    scene.Render(target.BeginFrame(sameClock), 0.0f);
    target.Present();
    EXPECT_EQ(cloth.random.State(), stateAfterEmission) << "sin otro frame original, solo se dibuja";
    EXPECT_EQ(CloudsIn(target.frames.back()), clouds);
    scene.Leave();
    writeObjects(false);
    ASSERT_TRUE(scene.Enter().HasValue());
    scene.Render(target.BeginFrame(std::chrono::milliseconds{2000}), 0.0f);
    target.Present();
    EXPECT_EQ(CloudsIn(target.frames.back()), 0u);
    scene.Leave();
}

} // namespace
