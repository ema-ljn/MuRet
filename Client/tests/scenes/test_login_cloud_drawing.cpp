#include "support/CheckedWorldObjectProjection.h"

#include "Scenes/WorldObjectsDrawing.h"

#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Scenes/LoginScene.h"
#include "Scenes/SceneContext.h"
#include "Scenes/GameData.h"
#include "Scenes/TextureSets.h"
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
#include "Render/Models/Animation.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/Skeleton.h"
#include "Render/Pass3D.h"
#include "Render/TextureSlot.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/TextureMetadata.h"
#include "Data/Textures/Texture.h"
#include "World/Particles.h"
#include "World/TerrainCulling.h"
#include "World/WorldObjects.h"
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

// El emisor de 0x602ed0 y el pase de ojo de 0x548550(0), sin GL.
namespace
{
constexpr std::uint8_t LoginMap = 55;
constexpr std::int16_t CloudMarker = 0x54;
constexpr std::int32_t CloudType = 0x622;
constexpr std::int32_t CloudSubtype = 10;
class CloudTextures final : public Client::Scenes::IModelLibrary
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
    // clouds.jpg: 64 × 32 y 3 componentes, ligada en el slot de su selector, como en GameModels.
    std::optional<Client::Scenes::TextureMetadata> TextureMetadataOf(std::uint32_t selector) const override
    {
        if (selector != static_cast<std::uint32_t>(CloudType))
        {
            return std::nullopt;
        }
        Client::Scenes::TextureMetadata metadata(Client::Data::Textures::Texture{});
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
Client::World::WorldObject Marker(std::int16_t type = CloudMarker)
{
    Client::Data::Terrain::TerrainObject record;
    record.type = type;
    record.position = {1000.0f, 2000.0f, 300.0f};
    record.angle = {10.0f, 20.0f, 30.0f};
    record.scale = 2.5f;
    return Client::Tests::CheckedWorldObjectProjection(std::span(&record, 1), LoginMap,
                                                       Client::World::ObjectScene::Login)
        .front();
}
void Emit(Client::World::WorldObject& object, Client::World::Particles& particles,
          Client::Render::Models::CrtRandom& random, std::uint8_t map = LoginMap,
          const Client::World::TerrainPolygon& polygon = {.everything = true})
{
    const CloudTextures models;
    Client::Render::ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});
    Client::Scenes::DrawWorldObjects(batch, models, std::span(&object, 1), polygon, nullptr, map,
                                     std::chrono::milliseconds{1234}, 0.0f,
                                     Client::Scenes::ObjectFrameEffects{.random = &random, .particles = &particles});
}
// Scene 2 solo controla el farol. Seed 4: rand común 51, guard 17945 y cinco rand de creación.
TEST(LoginCloudDrawingTest, ASeenHiddenMarkerEmitsOutsideSceneTwoAfterTheCommonRand)
{
    auto object = Marker();
    Client::World::Particles particles;
    Client::Render::Models::CrtRandom random(4);
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
    Client::World::Particles particles;
    Client::Render::Models::CrtRandom random(1);
    Emit(object, particles, random);
    EXPECT_FALSE(particles.Any());
    EXPECT_EQ(random.State(), 0xc823f683u) << "41 común y 18467 % 5 distinto de cero";
}
TEST(LoginCloudDrawingTest, AMissingPoolStillConsumesTheCloudGuard)
{
    const CloudTextures models;
    auto object = Marker();
    Client::Render::Models::CrtRandom random(4);
    Client::Render::ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});
    Client::Scenes::DrawWorldObjects(batch, models, std::span(&object, 1), {.everything = true}, nullptr, LoginMap, {},
                                     0.0f, Client::Scenes::ObjectFrameEffects{.random = &random});
    EXPECT_TRUE(object.seen);
    EXPECT_EQ(random.State(), 0xc6192e9eu) << "común51 y guard17945, sin los cinco rand de creación";
}

TEST(LoginCloudDrawingTest, OtherMapsAndTypesConsumeOnlyTheCommonRand)
{
    for (const bool otherMap : std::array{false, true})
    {
        SCOPED_TRACE(otherMap);
        auto object = Marker(otherMap ? CloudMarker : 0x55);
        Client::World::Particles particles;
        Client::Render::Models::CrtRandom random(4);
        Emit(object, particles, random, otherMap ? 54 : LoginMap);
        EXPECT_FALSE(particles.Any());
        EXPECT_EQ(random.State(), 0x0033aeb7u);
    }
}
TEST(LoginCloudDrawingTest, AnUnseenMarkerDoesNotReachTheEmitter)
{
    auto object = Marker();
    Client::World::Particles particles;
    Client::Render::Models::CrtRandom random(4);
    const auto polygon =
        Client::World::TerrainPolygonOf(Client::World::CullingScene::Game, 0, {14350.0f, 12050.0f, 165.0f}, -45.0f);
    Emit(object, particles, random, LoginMap, polygon);
    EXPECT_FALSE(object.seen);
    EXPECT_FALSE(particles.Any());
    EXPECT_EQ(random.State(), 4u);
}
TEST(LoginCloudDrawingTest, AFullPoolConsumesTheGuardWithoutCreationRandomness)
{
    auto object = Marker();
    Client::World::Particles particles;
    for (auto& particle : particles.All())
    {
        particle.alive = true;
        particle.type = -1;
    }
    Client::Render::Models::CrtRandom random(4);
    Emit(object, particles, random);
    EXPECT_EQ(random.State(), 0xc6192e9eu);
    for (const auto& particle : particles.All())
    {
        EXPECT_EQ(particle.type, -1);
    }
}
void SetCloud(Client::World::Particle& particle, float baseAngle)
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
// Al compactar los vivos, el slot 1 se trataría como par. 0x620b10 reemplaza la mezcla del JPEG (0x620a90):
// cada nube publica las dos antes de su quad, así que los tramos que dibujan van de a uno.
TEST(LoginCloudDrawingTest, DrawingUsesSubtractiveCloudsAndRealPoolIndices)
{
    const CloudTextures textures;
    Client::World::Particles particles;
    SetCloud(particles.All()[1], 10.0f);
    SetCloud(particles.All()[3], 30.0f);
    SetCloud(particles.All()[4], 40.0f);
    Client::Render::ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});
    ASSERT_TRUE(
        Client::Scenes::DrawParticles(batch, particles, textures, Client::Render::ViewRows(glm::mat4(1.0f)), 100.0f)
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
        Client::Scenes::DrawParticles(batch, particles, textures, Client::Render::ViewRows(glm::mat4(1.0f)), 200.0f)
            .HasValue());
    EXPECT_FLOAT_EQ(particles.All()[1].rotation, 2.0f) << "no acumula el giro anterior";
    EXPECT_FLOAT_EQ(particles.All()[4].rotation, 48.0f);
}

// Dos objetos y dos frames: marcador/roca y después marcador/roca; la segunda nube
// usa 5261/28277/24162. Agrupar los frames de cada objeto usaría otros desplazamientos.
TEST(LoginCloudDrawingTest, CatchUpVisitsEveryObjectBeforeStartingTheNextOriginalFrame)
{
    const CloudTextures models;
    std::array objects{Marker(), Marker(1)};
    std::array<std::optional<int>, 2> lastRandoms;
    Client::World::Particles particles;
    Client::Render::Models::CrtRandom random(4);
    Client::Render::ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});
    Client::Scenes::DrawWorldObjects(
        batch, models, objects, {.everything = true}, nullptr, LoginMap, {}, 0.0f,
        Client::Scenes::ObjectFrameEffects{
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
            // Solo los tramos que dibujan: los efectos sin geometría de 0x620a90 van con la mezcla aditiva.
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

// El marcador no necesita BMD: está oculto, pero su dibujo pide nubes al pool de LoginScene.
// Un Enter nuevo borra las partículas previas aunque su textura siga en la biblioteca del juego.
TEST(LoginCloudDrawingTest, TheSceneEmitsInTheEyePassAndClearsThePoolOnEnteringAgain)
{
    Client::Tests::TemporaryFolder folder("login-cloud");
    Client::Tests::WriteTextureSet(folder.Path(), Client::Scenes::LoginTextureSet);
    Client::Tests::WriteTextureSet(folder.Path(), Client::Scenes::WindowTextureSet);
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
    Client::Render::Models::ClothWorld cloth;
    const Client::Scenes::GameData data;
    const CloudTextures textures;
    Client::Scenes::LoginScene scene(
        Client::Scenes::SceneContext{.dataRoot = folder.Path(), .textures = store, .log = log, .cloth = &cloth}, data,
        textures, nullptr, "0.0.0", 1);
    ASSERT_TRUE(scene.Enter().HasValue());
    Client::Tests::RecordingFrameTarget target;
    std::size_t clouds = 0;
    for (int frame = 0; frame < 16 && clouds == 0; ++frame)
    {
        scene.Render(
            target.BeginFrame(std::chrono::milliseconds{1234} + frame * Client::Render::Models::OriginalFrameTime),
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
