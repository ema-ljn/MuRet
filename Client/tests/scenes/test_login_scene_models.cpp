#include "Scenes/LoginScene.h"

#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Data/Models/Model.h"
#include "Data/Terrain/TerrainHeights.h"
#include "Data/Textures/TextureSet.h"
#include "Input/InputState.h"
#include "Render/ModelBatch.h"
#include "Render/ModelVertex.h"
#include "Render/Models/HalfLifeMath.h"
#include "Render/Models/ItemGlow.h"
#include "Render/Models/Skeleton.h"
#include "Render/TextureSlot.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/GameData.h"
#include "Scenes/LoginModels.h"
#include "Scenes/SceneCanvas.h"
#include "Scenes/SceneContext.h"
#include "Scenes/TextureSets.h"
#include "World/TerrainLighting.h"
#include "support/FakeClock.h"
#include "support/FakeTextureStore.h"
#include "support/FixedRandom.h"
#include "support/JpegVectors.h"
#include "support/RecordingFrameTarget.h"
#include "support/RecordingSink.h"
#include "support/SceneDataFiles.h"
#include "support/SyntheticModels.h"
#include "support/TemporaryFolder.h"

#include <gtest/gtest.h>

#include <glm/ext/vector_float3.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Client::Scenes::LoginScene con su logo y su personaje (CL6.4): Logo03.bmd y la luz del mundo 56
// sintéticos, y Player.bmd y las partes de una biblioteca a mano. La cuenta del logo, la luz del
// terreno, la acción del personaje y el orden de 0x6350b0 (06 § El logo en el binario).
namespace
{
using Client::Core::Log;
using Client::Core::LogLevel;
using Client::Input::InputState;
using Client::Render::ModelBlend;
using Client::Render::ModelVertex;
using Client::Render::TextureSlot;
using Client::Render::Models::ActionInfo;
using Client::Render::Models::BoneKey;
using Client::Render::Models::Skeleton;
using Client::Render::Models::SkeletonBone;
using Client::Scenes::GameData;
using Client::Scenes::IModelLibrary;
using Client::Scenes::LoginCharacterLight;
using Client::Scenes::LoginCharacterTransform;
using Client::Scenes::LoginLogo;
using Client::Scenes::LoginScene;
using Client::Scenes::SceneContext;
using Client::Tests::FakeClock;
using Client::Tests::FakeTextureStore;
using Client::Tests::FixedSeed;
using Client::Tests::FrameEvent;
using Client::Tests::RecordingFrameTarget;
using Client::Tests::RecordingSink;
using Client::Tests::TemporaryFolder;

using Kind = FrameEvent::Kind;

constexpr float Rounding = 1e-4f;
// Hasta las dos acciones de 0x634c60 (LoginModels.h).
constexpr std::size_t PlayerActions = 0x56;
constexpr float RaisedHeight = 100.0f;

// Un triángulo del hueso 0 con la textura `texture` (su índice) y `name`.
Client::Data::Models::Mesh TriangleMesh(std::int16_t texture, std::string name)
{
    Client::Data::Models::Mesh mesh;
    mesh.texture = texture;
    mesh.vertices = {{0, {0, 0, 0}}, {0, {1, 0, 0}}, {0, {0, 1, 0}}};
    mesh.normals = {{0, {0, 0, 1}, 0}};
    mesh.texCoords = {{0, 0}};
    mesh.triangles = {{3, {0, 1, 2, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}}};
    mesh.textureName = std::move(name);
    return mesh;
}

// Un Logo03.bmd de dos mallas, logo.jpg y logo2.jpg, con un hueso y una acción de una key, como el del
// laboratorio; y sus dos texturas, salvo `withoutSecondTexture`.
void WriteLogo(const std::filesystem::path& root, bool withoutSecondTexture = false)
{
    Client::Data::Models::Model logo;
    logo.version = 0x0a;
    logo.name = "logo";
    logo.meshes = {TriangleMesh(0, "logo.jpg"), TriangleMesh(1, "logo2.jpg")};
    Client::Data::Models::Action action;
    action.keyCount = 1;
    logo.actions = {action};
    Client::Data::Models::Bone bone;
    bone.name = "root";
    bone.actions = {Client::Data::Models::BoneKeys{{{0.0f, 0.0f, 0.0f}}, {{0.0f, 0.0f, 0.0f}}}};
    logo.bones = {bone};
    Client::Tests::WriteSceneFile(root, "Logo/Logo03.bmd",
                                  Client::Tests::PlainBmd(Client::Tests::BmdContent(logo), logo.version));
    const std::array<Client::Data::Textures::TextureSetEntry, 2> textures{{
        {"Logo\\logo.jpg", 0x455, Client::Scenes::LoginLogoSampling},
        {"Logo\\logo2.jpg", 0x456, Client::Scenes::LoginLogoSampling},
    }};
    Client::Tests::WriteTextureSet(root, std::span(textures).first(withoutSecondTexture ? 1 : 2));
}

// World56 con las alturas en 0 (plano: la sombra es 1) y la luz de light-256.jpg.
void WriteWorld56(const std::filesystem::path& root)
{
    Client::Tests::WriteSceneFile(root, "World56/TerrainHeight.OZB",
                                  std::vector<std::byte>(Client::Data::Terrain::TerrainHeightsSize));
    Client::Tests::WriteSceneFile(root, "World56/TerrainLight.OZJ",
                                  Client::Tests::OzjOf(Client::Tests::ReadJpegVector("light-256.jpg")));
}

// Player.bmd con 48 huesos y 0x56 acciones de dos keys que avanzan una por paso: en las 0x54 y 0x55,
// el hueso 0 a 100 de altura; en las demás, en el origen. Y una parte de un triángulo para cualquier
// otro modelo, sin la sombra.
class LoginPlayer final : public IModelLibrary
{
public:
    LoginPlayer() : m_part(Client::Data::Models::Model{})
    {
        SkeletonBone bone;
        bone.parent = -1;
        for (std::size_t action = 0; action < PlayerActions; ++action)
        {
            BoneKey key;
            key.rotation = Client::Render::Models::AngleQuaternion({0.0f, 0.0f, 0.0f});
            key.position = {0.0f, 0.0f, action == 0x54 || action == 0x55 ? RaisedHeight : 0.0f};
            bone.actions.push_back({key, key});
        }
        m_body.bones.assign(48, bone);
        m_body.actions.assign(PlayerActions,
                              ActionInfo{.repeats = true, .speed = 1.0f, .keyCount = 2, .lockPositions = false});
        m_part.meshes = {TriangleMesh(0, "part.jpg")};
    }

    [[nodiscard]] const Client::Data::Models::Model* Find(std::uint16_t model) const override
    {
        return model == 0x20b ? nullptr : &m_part;
    }
    [[nodiscard]] const Skeleton* SkeletonOf(std::uint16_t model) const override
    {
        return model == 0x20a ? &m_body : nullptr;
    }
    [[nodiscard]] std::span<const TextureSlot> MeshTextures(std::uint16_t /*model*/) const override
    {
        return m_slots;
    }

private:
    Skeleton m_body;
    Client::Data::Models::Model m_part;
    std::array<TextureSlot, 1> m_slots{TextureSlot{0x300}};
};

struct Harness
{
    explicit Harness(bool withLogo = true, bool withWorld = true) : log(sink, LogLevel::Debug)
    {
        Client::Tests::WriteTextureSet(folder.Path(), Client::Scenes::LoginTextureSet);
        Client::Tests::WriteTextureSet(folder.Path(), Client::Scenes::WindowTextureSet);
        if (withLogo)
        {
            WriteLogo(folder.Path());
        }
        if (withWorld)
        {
            WriteWorld56(folder.Path());
        }
    }

    LoginScene& Make(std::mt19937::result_type seed = FixedSeed, FakeClock* sceneClock = nullptr)
    {
        return scene.emplace(SceneContext{folder.Path(), store, log, sceneClock}, data, models, nullptr, "0.0.0", seed);
    }

    // El evento de los modelos del frame, después de un frame.
    FrameEvent ModelsOfAFrame()
    {
        Client::Scenes::SceneCanvas& canvas = target.BeginFrame(std::chrono::milliseconds{40});
        scene->Render(canvas, 0.0f);
        target.Present();
        const std::vector<FrameEvent>& events = target.frames.back().events;
        const auto drawn = std::ranges::find(events, Kind::Models, &FrameEvent::kind);
        EXPECT_NE(drawn, events.end());
        return drawn != events.end() ? *drawn : FrameEvent{};
    }

    void Steps(int count)
    {
        for (int step = 0; step < count; ++step)
        {
            scene->Step(input);
        }
    }

    TemporaryFolder folder{"login-scene-models"};
    FakeTextureStore store;
    RecordingSink sink;
    Log log;
    GameData data;
    LoginPlayer models;
    RecordingFrameTarget target;
    std::optional<LoginScene> scene;
    InputState input;
};

// Si alguna línea del log tiene `text`.
bool Logged(const RecordingSink& sink, std::string_view text)
{
    return std::ranges::any_of(sink.lines,
                               [text](const std::string& line) { return line.find(text) != std::string::npos; });
}

glm::vec3 ColorOf(const ModelVertex& vertex)
{
    return {vertex.color.red, vertex.color.green, vertex.color.blue};
}

void ExpectNear(const glm::vec3& actual, const glm::vec3& expected)
{
    EXPECT_NEAR(actual.x, expected.x, Rounding);
    EXPECT_NEAR(actual.y, expected.y, Rounding);
    EXPECT_NEAR(actual.z, expected.z, Rounding);
}

// La luz de light-256.jpg en (x, y), sin sombra: el terreno es plano.
glm::vec3 WorldLightAt(const std::filesystem::path& root, const glm::vec3& position)
{
    const auto lighting = Client::World::LoadTerrainLighting(root, 55);
    EXPECT_TRUE(lighting.HasValue()) << lighting.Error();
    return lighting.HasValue() ? Client::World::LightAt(*lighting.Value(), position.x, position.y) : glm::vec3(0.0f);
}

// 0x6350b0: en el pase 3D, el personaje (0x510d00) y después el logo (0x6046a0), que es el último:
// un tramo opaco con logo.jpg y el aditivo de su blendMesh con logo2.jpg.
TEST(LoginSceneModelsTest, TheCharacterGoesBeforeTheLogo)
{
    Harness tested;
    ASSERT_TRUE(tested.Make().Enter().HasValue());
    tested.Steps(1);

    const FrameEvent models = tested.ModelsOfAFrame();

    ASSERT_GE(models.modelCommands.size(), 3u);
    const std::size_t count = models.modelCommands.size();
    EXPECT_EQ(models.modelCommands[0].state.texture, TextureSlot{0x300});
    EXPECT_EQ(models.modelCommands[count - 2].state.texture, TextureSlot{0x455});
    EXPECT_EQ(models.modelCommands[count - 2].state.blend, ModelBlend::Opaque);
    EXPECT_EQ(models.modelCommands[count - 1].state.texture, TextureSlot{0x456});
    EXPECT_EQ(models.modelCommands[count - 1].state.blend, ModelBlend::Additive);
}

// 0x50ba30 fuera de la escena 4: la luz del terreno en el lugar del personaje más su +0xf4 (0,3), por
// el factor del nivel de la parte (las botas, +7, la primera).
TEST(LoginSceneModelsTest, TheCharacterTakesTheLightOfTheTerrain)
{
    Harness tested;
    ASSERT_TRUE(tested.Make().Enter().HasValue());
    tested.Steps(1);

    const FrameEvent models = tested.ModelsOfAFrame();

    ASSERT_FALSE(models.modelVertices.empty());
    const glm::vec3 terrain = WorldLightAt(tested.folder.Path(), LoginCharacterTransform().position);
    EXPECT_GT(terrain.x + terrain.y + terrain.z, 0.0f);
    const float factor = Client::Render::Models::ItemGlowOf(7, false, false).baseFactor;
    ExpectNear(ColorOf(models.modelVertices[0]), (terrain + glm::vec3(LoginCharacterLight)) * factor);
}

// Sin la luz del mundo, el login entra igual, con un aviso en el log, y el personaje va con la suya.
TEST(LoginSceneModelsTest, WithoutTheWorldLightTheCharacterGoesWithItsOwn)
{
    Harness tested(true, false);
    ASSERT_TRUE(tested.Make().Enter().HasValue());
    tested.Steps(1);

    const FrameEvent models = tested.ModelsOfAFrame();

    ASSERT_FALSE(models.modelVertices.empty());
    const float factor = Client::Render::Models::ItemGlowOf(7, false, false).baseFactor;
    ExpectNear(ColorOf(models.modelVertices[0]), glm::vec3(LoginCharacterLight * factor));
    EXPECT_TRUE(Logged(tested.sink, "sin la luz del terreno"));
}

// Q-C105: el color del logo es 0,1 de la luz del terreno más la suya, cuenta · 0,02, de −0,02 en el
// primer paso a 0,78 en el 41, y 1,5 desde el 42; su blendMesh va además por esa luz.
TEST(LoginSceneModelsTest, TheLogoLightRisesAndThenJumps)
{
    Harness tested;
    ASSERT_TRUE(tested.Make().Enter().HasValue());
    const glm::vec3 terrain = WorldLightAt(tested.folder.Path(), LoginLogo().transform.position) * 0.1f;
    int steps = 0;
    for (const auto& [step, light] : {std::pair{1, -0.02f}, std::pair{2, 0.0f}, std::pair{41, 39.0f * 0.02f},
                                      std::pair{42, 1.5f}, std::pair{200, 1.5f}})
    {
        SCOPED_TRACE(step);
        tested.Steps(step - steps);
        steps = step;

        const FrameEvent models = tested.ModelsOfAFrame();

        const std::size_t count = models.modelVertices.size();
        ASSERT_GE(count, 6u);
        const glm::vec3 color = terrain + glm::vec3(light);
        ExpectNear(ColorOf(models.modelVertices[count - 6]), color);
        ExpectNear(ColorOf(models.modelVertices[count - 3]), color * light);
    }
}

// Al salir se sueltan las dos texturas del logo, como las demás de la escena.
TEST(LoginSceneModelsTest, LeaveReleasesTheLogo)
{
    Harness tested;
    LoginScene& scene = tested.Make();
    ASSERT_TRUE(scene.Enter().HasValue());
    ASSERT_TRUE(tested.store.Find(TextureSlot{0x455}).has_value());
    ASSERT_TRUE(tested.store.Find(TextureSlot{0x456}).has_value());

    scene.Leave();

    EXPECT_FALSE(tested.store.Find(TextureSlot{0x455}).has_value());
    EXPECT_FALSE(tested.store.Find(TextureSlot{0x456}).has_value());
    EXPECT_EQ(tested.store.Size(), 0u);
}

// 0x60da30 saltea un modelo que no está y no se llama Monster, Player ni PlayerTest: sin Logo03.bmd,
// el login entra, sin el logo.
TEST(LoginSceneModelsTest, WithoutLogo03TheLoginEntersWithoutIt)
{
    Harness tested(false);
    ASSERT_TRUE(tested.Make().Enter().HasValue());
    tested.Steps(1);

    const FrameEvent models = tested.ModelsOfAFrame();

    for (const auto& command : models.modelCommands)
    {
        EXPECT_NE(command.state.texture, TextureSlot{0x455});
    }
    EXPECT_TRUE(Logged(tested.sink, "el logo no se puede leer"));
    EXPECT_FALSE(tested.store.Find(TextureSlot{0x455}).has_value());
}

// 0x60d5f0 con su quinto argumento en 1: una textura del logo que falta corta la entrada.
TEST(LoginSceneModelsTest, AMissingLogoTextureStopsTheLogin)
{
    Harness tested(false);
    WriteLogo(tested.folder.Path(), true);

    const auto entered = tested.Make().Enter();

    ASSERT_FALSE(entered.HasValue());
    EXPECT_NE(entered.Error().find("logo2"), std::string::npos) << entered.Error();
}

// La semilla con la que el primer rand() % 6 da `remainder`.
std::mt19937::result_type SeedWhoseFirstRollIs(int remainder)
{
    for (std::mt19937::result_type seed = 0;; ++seed)
    {
        std::mt19937 random(seed);
        if (static_cast<int>(random() % 32768) % 6 == remainder)
        {
            return seed;
        }
    }
}

// La altura del primer vértice del personaje (el de las botas, del hueso 0): 580 en la acción de
// parado, y 580 + 100 · 1,6 en la 0x54 o la 0x55.
float CharacterHeight(Harness& tested)
{
    const FrameEvent models = tested.ModelsOfAFrame();
    EXPECT_FALSE(models.modelVertices.empty());
    return models.modelVertices.empty() ? 0.0f : models.modelVertices[0].position.z;
}

// 0x634c60 con el reloj del bucle: en la ventana de cada 2.300 ms, el primer rand() % 6 en 1 pone la
// 0x54; fuera de la ventana, o con otro resto, queda la de parado (0x4f2250 con el caballo).
TEST(LoginSceneModelsTest, TheCharacterChangesItsActionInItsWindow)
{
    const float standing = LoginCharacterTransform().position.z;
    const float raised = standing + (RaisedHeight * LoginCharacterTransform().scale);
    {
        Harness tested;
        FakeClock clock;
        clock.Advance(std::chrono::milliseconds{(3 * 2300) + 50});
        ASSERT_TRUE(tested.Make(SeedWhoseFirstRollIs(1), &clock).Enter().HasValue());
        tested.Steps(1);
        EXPECT_NEAR(CharacterHeight(tested), raised, Rounding);
    }
    {
        Harness tested;
        FakeClock clock;
        clock.Advance(std::chrono::milliseconds{(3 * 2300) + 500});
        ASSERT_TRUE(tested.Make(SeedWhoseFirstRollIs(1), &clock).Enter().HasValue());
        tested.Steps(1);
        EXPECT_NEAR(CharacterHeight(tested), standing, Rounding);
    }
    {
        Harness tested;
        FakeClock clock;
        clock.Advance(std::chrono::milliseconds{(3 * 2300) + 50});
        ASSERT_TRUE(tested.Make(SeedWhoseFirstRollIs(0), &clock).Enter().HasValue());
        tested.Steps(1);
        EXPECT_NEAR(CharacterHeight(tested), standing, Rounding);
    }
}
} // namespace
