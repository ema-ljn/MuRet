#include "Models/IModelLibrary.h"
#include "Scenes/LoginScene.h"
#include "Kernel/WorldState.h"

#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Crt/CrtRandom.h"
#include "Data/Models/Model.h"
#include "Tables/PlayerModels.h"
#include "Data/Terrain/TerrainHeights.h"
#include "Input/InputState.h"
#include "Render/ModelBatch.h"
#include "Render/ModelVertex.h"
#include "Models/Cloth.h"
#include "Math/HalfLifeMath.h"
#include "Models/ItemGlow.h"
#include "Models/Skeleton.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Render/TextureSlot.h"
#include "Kernel/ActorState.h"
#include "Actors/Drawing/CharacterDrawing.h"
#include "Pets/DarkHorse.h"
#include "Joints/FaunaJointCreation.h"
#include "Tables/GameData.h"
#include "Kernel/DrawTailState.h"
#include "FrontEnd/LoginActor.h"
#include "FrontEnd/LoginModels.h"
#include "Render/Frame/SceneCanvas.h"
#include "Scenes/SceneContext.h"
#include "Assets/TextureSets.h"
#include "Kernel/WorldRuntime.h"
#include "Data/Terrain/TerrainObjects.h"
#include "Models/Animation.h"
#include "Models/ModelId.h"
#include "Kernel/JointRequest.h"
#include "Effects/ModelEffects.h"
#include "Particles/Particles.h"
#include "Effects/WizardModelEffects.h"
#include "Ui/MessageArea.h"
#include "Terrain/TerrainLighting.h"
#include "support/FakeClock.h"
#include "support/FakeTextureStore.h"
#include "support/FixedRandom.h"
#include "support/JpegVectors.h"
#include "support/LoginLogoFiles.h"
#include "support/RecordingFrameTarget.h"
#include "support/RecordingSink.h"
#include "support/SceneDataFiles.h"
#include "support/TemporaryFolder.h"
#include "support/TerrainFiles.h"
#include "Chat/ChatRuntime.h"

#include <gtest/gtest.h>

#include <glm/ext/vector_float3.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

// Client::Scenes::LoginScene with its logo and its character (CL6.4): synthetic Logo03.bmd and world 56 light, and
// Player.bmd and the parts from a hand-made library. The logo's count, the terrain light, the character's action and
// the order of 0x6350b0 (06 § El logo en el binario).
namespace
{
using Client::Core::Log;
using Client::Core::LogLevel;
using Client::FrontEnd::LoginCharacterLight;
using Client::FrontEnd::LoginCharacterTransform;
using Client::FrontEnd::LoginLogo;
using Client::Input::InputState;
using Client::Models::ActionInfo;
using Client::Models::BoneKey;
using Client::Models::IModelLibrary;
using Client::Models::Skeleton;
using Client::Models::SkeletonBone;
using Client::Render::ModelBlend;
using Client::Render::ModelVertex;
using Client::Render::TextureSlot;
using Client::Scenes::LoginScene;
using Client::Scenes::SceneContext;
using Client::Tables::GameData;
using Client::Tests::FakeClock;
using Client::Tests::FakeTextureStore;
using Client::Tests::FixedSeed;
using Client::Tests::FrameEvent;
using Client::Tests::RecordingFrameTarget;
using Client::Tests::RecordingSink;
using Client::Tests::TemporaryFolder;

using Kind = FrameEvent::Kind;

constexpr float Rounding = 1e-4f;
// Up to 0x634c60's two actions (LoginModels.h).
constexpr std::size_t PlayerActions = 0x56;
constexpr float RaisedHeight = 100.0f;

// World56 with heights at 0 (flat: the shadow is 1) and the light of light-256.jpg.
void WriteWorld56(const std::filesystem::path& root)
{
    Client::Tests::WriteSceneFile(root, "World56/TerrainHeight.OZB",
                                  std::vector<std::byte>(Client::Data::Terrain::TerrainHeightsSize));
    Client::Tests::WriteSceneFile(root, "World56/TerrainLight.OZJ",
                                  Client::Tests::OzjOf(Client::Tests::ReadJpegVector("light-256.jpg")));
}

// A skeleton of `bones` parentless bones with `actions` two-key actions that advance one per step: in the two of
// `raised`, each bone at height 100; in the others, at the origin.
Skeleton RaisingSkeleton(std::size_t actions, std::size_t bones, std::array<std::size_t, 2> raised)
{
    SkeletonBone bone;
    bone.parent = -1;
    for (std::size_t action = 0; action < actions; ++action)
    {
        BoneKey key;
        key.rotation = Client::Math::AngleQuaternion({0.0f, 0.0f, 0.0f});
        key.position = {0.0f, 0.0f, action == raised[0] || action == raised[1] ? RaisedHeight : 0.0f};
        bone.actions.push_back({key, key});
    }
    Skeleton skeleton;
    skeleton.bones.assign(bones, bone);
    skeleton.actions.assign(actions, ActionInfo{.repeats = true, .speed = 1.0f, .keyCount = 2, .lockPositions = false});
    return skeleton;
}

// Player.bmd with 48 bones and 0x56 actions (0x54 and 0x55 raise bone 0), a one-triangle part for any other model,
// without the shadow, and the horse (0xbf) with its seven actions (5 and 6 raise it) and its texture at 0x3bc.
class LoginPlayer final : public IModelLibrary
{
public:
    LoginPlayer()
        : m_body(RaisingSkeleton(PlayerActions, 48, {0x54, 0x55})), m_horse(RaisingSkeleton(7, 1, {5, 6})),
          m_part(Client::Data::Models::Model{})
    {
        m_part.meshes = {Client::Tests::TriangleMesh(0, "part.jpg")};
    }

    [[nodiscard]] const Client::Data::Models::Model* Find(Client::Models::ModelId model) const override
    {
        return model.Value() == 0x20b ? nullptr : &m_part;
    }
    [[nodiscard]] const Skeleton* SkeletonOf(Client::Models::ModelId model) const override
    {
        if (model.Value() == Client::Pets::DarkHorseModel)
        {
            return &m_horse;
        }
        return model.Value() == 0x20a ? &m_body : nullptr;
    }
    [[nodiscard]] std::span<const TextureSlot> MeshTextures(Client::Models::ModelId model) const override
    {
        return model.Value() == Client::Pets::DarkHorseModel ? m_horseSlots : m_slots;
    }

    // The body's bone 0, a dummy: 0x4eb973 does not write it and what the buffer it is posed in had stays.
    void RetainTheDummyRoot()
    {
        m_body.bones[0].dummy = true;
    }

private:
    Skeleton m_body;
    Skeleton m_horse;
    Client::Data::Models::Model m_part;
    std::array<TextureSlot, 1> m_slots{TextureSlot{0x300}};
    std::array<TextureSlot, 1> m_horseSlots{TextureSlot{0x3bc}};
};

// Six pixels per byte: the tests' texts do not reach the 256 where 0x555190 cuts.
class LetterMeasure final : public Client::Render::Text::ITextMeasure
{
public:
    [[nodiscard]] Client::Render::Text::TextExtent Measure(Client::Render::Text::FontKind /*font*/,
                                                           std::string_view text) const override
    {
        return {static_cast<int>(text.size()) * 6, 13};
    }
};

struct Harness
{
    explicit Harness(bool withLogo = true, bool withWorld = true) : log(sink, LogLevel::Debug)
    {
        Client::Tests::WriteTextureSet(folder.Path(), Client::Assets::LoginTextureSet);
        Client::Tests::WriteTextureSet(folder.Path(), Client::Assets::WindowTextureSet);
        if (withLogo)
        {
            Client::Tests::WriteLoginLogo(folder.Path());
        }
        if (withWorld)
        {
            WriteWorld56(folder.Path());
        }
    }

    // With `cloth`, the loop's cloth globals: the CRT rand().
    LoginScene& Make(std::mt19937::result_type seed = FixedSeed, FakeClock* sceneClock = nullptr,
                     Client::Models::ClothWorld* cloth = nullptr, Client::Kernel::WorldState* world = nullptr)
    {
        SceneContext context{folder.Path(), store, log, sceneClock, cloth};
        context.world = world;
        context.textMeasure = &letters;
        return scene.emplace(context, data, models, nullptr, "0.0.0", seed);
    }

    FrameEvent ModelsOfAFrame()
    {
        Client::Render::Frame::SceneCanvas& canvas = target.BeginFrame(std::chrono::milliseconds{40});
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
    LetterMeasure letters;
    FakeTextureStore store;
    RecordingSink sink;
    Log log;
    GameData data;
    LoginPlayer models;
    RecordingFrameTarget target;
    std::optional<LoginScene> scene;
    InputState input;
};

// The logo's two slots with a world without models: those that 0x60d530(0x41c) of 0x616ec0 leaves.
const TextureSlot LogoSlot{0x41c};
const TextureSlot LogoSlot2{0x41d};

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

// The light of light-256.jpg at (x, y), without shadow: the terrain is flat.
glm::vec3 WorldLightAt(const std::filesystem::path& root, const glm::vec3& position)
{
    const auto lighting = Client::Terrain::LoadTerrainLighting(root, 55);
    EXPECT_TRUE(lighting.HasValue()) << lighting.Error();
    return lighting.HasValue() ? Client::Terrain::LightAt(*lighting.Value(), position.x, position.y) : glm::vec3(0.0f);
}

// 0x6350b0: in the 3D pass, the character (0x510d00) and then the logo (0x6046a0), which is the last: an opaque
// stretch with logo.jpg and the additive one of its blendMesh with logo2.jpg. The test's Data has no Object56: the
// world loads no model and leaves the next slot at 0x41c, where the logo goes (Q-C92).
TEST(LoginSceneModelsTest, TheCharacterGoesBeforeTheLogo)
{
    Harness tested;
    ASSERT_TRUE(tested.Make().Enter().HasValue());
    tested.Steps(1);

    const FrameEvent models = tested.ModelsOfAFrame();

    ASSERT_GE(models.modelCommands.size(), 3u);
    const std::size_t count = models.modelCommands.size();
    EXPECT_EQ(models.modelCommands[0].state.texture, TextureSlot{0x300});
    EXPECT_EQ(models.modelCommands[count - 2].state.texture, LogoSlot);
    EXPECT_EQ(models.modelCommands[count - 2].state.blend, ModelBlend::Opaque);
    EXPECT_EQ(models.modelCommands[count - 1].state.texture, LogoSlot2);
    EXPECT_EQ(models.modelCommands[count - 1].state.blend, ModelBlend::Additive);
}

// 0x50ba30 outside scene 4: the terrain light at the character's place plus its +0xf4 (0.3), times the factor of the
// part's level (the boots, +7, the first).
TEST(LoginSceneModelsTest, TheCharacterTakesTheLightOfTheTerrain)
{
    Harness tested;
    ASSERT_TRUE(tested.Make().Enter().HasValue());
    tested.Steps(1);

    const FrameEvent models = tested.ModelsOfAFrame();

    ASSERT_FALSE(models.modelVertices.empty());
    const glm::vec3 terrain = WorldLightAt(tested.folder.Path(), LoginCharacterTransform().position);
    EXPECT_GT(terrain.x + terrain.y + terrain.z, 0.0f);
    const float factor = Client::Models::ItemGlowOf(7, false, false).baseFactor;
    ExpectNear(ColorOf(models.modelVertices[0]), (terrain + glm::vec3(LoginCharacterLight)) * factor);
}

// Without the world light, the login enters anyway, with a log warning, and the character goes with its own.
TEST(LoginSceneModelsTest, WithoutTheWorldLightTheCharacterGoesWithItsOwn)
{
    Harness tested(true, false);
    ASSERT_TRUE(tested.Make().Enter().HasValue());
    tested.Steps(1);

    const FrameEvent models = tested.ModelsOfAFrame();

    ASSERT_FALSE(models.modelVertices.empty());
    const float factor = Client::Models::ItemGlowOf(7, false, false).baseFactor;
    ExpectNear(ColorOf(models.modelVertices[0]), glm::vec3(LoginCharacterLight * factor));
    EXPECT_TRUE(Logged(tested.sink, "sin la luz del terreno"));
}

// Q-C105: the logo's color is 0.1 of the terrain light plus its own, and its blendMesh also goes by that light.
// 0x606560 only runs the objects that were drawn (+0x16c): the first step does not write the light, which stays as
// 0x606660 left it (+0xf4 at 0 and +0x68 at 1); from the second, count · 0.02, from 0 at the 2nd to 0.78 at the
// 41st, and 1.5 from the 42nd.
TEST(LoginSceneModelsTest, TheLogoLightRisesAndThenJumps)
{
    Harness tested;
    ASSERT_TRUE(tested.Make().Enter().HasValue());
    const glm::vec3 terrain = WorldLightAt(tested.folder.Path(), LoginLogo().transform.position) * 0.1f;
    int steps = 0;
    for (const auto& [step, light, blendLight] :
         {std::tuple{1, 0.0f, 1.0f}, std::tuple{2, 0.0f, 0.0f}, std::tuple{41, 39.0f * 0.02f, 39.0f * 0.02f},
          std::tuple{42, 1.5f, 1.5f}, std::tuple{200, 1.5f, 1.5f}})
    {
        SCOPED_TRACE(step);
        tested.Steps(step - steps);
        steps = step;

        const FrameEvent models = tested.ModelsOfAFrame();

        const std::size_t count = models.modelVertices.size();
        ASSERT_GE(count, 6u);
        const glm::vec3 color = terrain + glm::vec3(light);
        ExpectNear(ColorOf(models.modelVertices[count - 6]), color);
        ExpectNear(ColorOf(models.modelVertices[count - 3]), color * blendLight);
    }
}

// While it has not been drawn, the logo does not change (0x606560 with +0x16c at 0), and the routes go on: after ten
// steps without a draw, the light is 0x606660's; once drawn, the next step gives it the count, 9 · 0.02.
TEST(LoginSceneModelsTest, TheLogoDoesNotChangeUntilItIsDrawn)
{
    Harness tested;
    ASSERT_TRUE(tested.Make().Enter().HasValue());
    const glm::vec3 terrain = WorldLightAt(tested.folder.Path(), LoginLogo().transform.position) * 0.1f;
    tested.Steps(10);

    const FrameEvent before = tested.ModelsOfAFrame();
    tested.Steps(1);
    const FrameEvent after = tested.ModelsOfAFrame();

    ASSERT_GE(before.modelVertices.size(), 6u);
    ASSERT_GE(after.modelVertices.size(), 6u);
    ExpectNear(ColorOf(before.modelVertices[before.modelVertices.size() - 3]), terrain);
    ExpectNear(ColorOf(after.modelVertices[after.modelVertices.size() - 6]), terrain + glm::vec3(9.0f * 0.02f));
}

TEST(LoginSceneModelsTest, LeaveReleasesTheLogo)
{
    Harness tested;
    LoginScene& scene = tested.Make();
    ASSERT_TRUE(scene.Enter().HasValue());
    ASSERT_TRUE(tested.store.Find(LogoSlot).has_value());
    ASSERT_TRUE(tested.store.Find(LogoSlot2).has_value());

    scene.Leave();

    EXPECT_FALSE(tested.store.Find(LogoSlot).has_value());
    EXPECT_FALSE(tested.store.Find(LogoSlot2).has_value());
    EXPECT_EQ(tested.store.Size(), 0u);
}

// 0x60da30 skips a model that is missing and is not named Monster, Player or PlayerTest: without Logo03.bmd, the
// login enters, without the logo.
TEST(LoginSceneModelsTest, WithoutLogo03TheLoginEntersWithoutIt)
{
    Harness tested(false);
    ASSERT_TRUE(tested.Make().Enter().HasValue());
    tested.Steps(1);

    const FrameEvent models = tested.ModelsOfAFrame();

    for (const auto& command : models.modelCommands)
    {
        EXPECT_NE(command.state.texture, LogoSlot);
    }
    EXPECT_TRUE(Logged(tested.sink, "el logo no se puede leer"));
    EXPECT_FALSE(tested.store.Find(LogoSlot).has_value());
}

// 0x60d5f0 with its fifth argument at 1: a missing logo texture stops the entry.
TEST(LoginSceneModelsTest, AMissingLogoTextureStopsTheLogin)
{
    Harness tested(false);
    Client::Tests::WriteLoginLogo(tested.folder.Path(), true);

    const auto entered = tested.Make().Enter();

    ASSERT_FALSE(entered.HasValue());
    EXPECT_NE(entered.Error().find("logo2"), std::string::npos) << entered.Error();
}

std::mt19937::result_type SeedWhoseFirstRollIs(int remainder)
{
    for (std::mt19937::result_type seed = 0;; ++seed)
    {
        Client::Crt::CrtRandom random(static_cast<std::uint32_t>(seed));
        if (random.Next() % 6 == remainder)
        {
            return seed;
        }
    }
}

// The height of the character's first vertex (the boots', bone 0's): 580 in the standing action, and 580 + 100 · 1.6
// in 0x54 or 0x55.
float CharacterHeight(Harness& tested)
{
    const FrameEvent models = tested.ModelsOfAFrame();
    EXPECT_FALSE(models.modelVertices.empty());
    return models.modelVertices.empty() ? 0.0f : models.modelVertices[0].position.z;
}

// 0x634c60 with the loop clock: in the window of every 2300 ms, the first rand() % 6 at 1 sets 0x54; outside the
// window, or with another remainder, the standing one stays (0x4f2250 with the horse).
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

// With the loop (the cloth's globals in the context), the gesture comes from the CRT rand(), the original's
// (0x634cca), and not from the scene's seed: with the same CRT, two seeds whose first rand() % 6 gives 1 and 0 do the
// same, and depending on the CRT's state the gesture comes out or not. Outside the window, never. Where that rand()
// falls among the step's others is not fixed here: it is P-C27 d.
TEST(LoginSceneModelsTest, WithTheLoopTheGestureTakesTheRandOfTheCrt)
{
    const float standing = LoginCharacterTransform().position.z;
    const float raised = standing + (RaisedHeight * LoginCharacterTransform().scale);
    const std::chrono::milliseconds inside{(3 * 2300) + 50};
    const std::chrono::milliseconds outside{(3 * 2300) + 500};
    const auto heightAfterAStep = [](std::chrono::milliseconds clock, std::mt19937::result_type seed, std::uint32_t crt)
    {
        Harness tested;
        FakeClock sceneClock;
        sceneClock.Advance(clock);
        Client::Models::ClothWorld cloth{.random = Client::Crt::CrtRandom(crt)};
        EXPECT_TRUE(tested.Make(seed, &sceneClock, &cloth).Enter().HasValue());
        tested.Steps(1);
        return CharacterHeight(tested);
    };
    constexpr std::uint32_t CrtStates = 12;
    std::uint32_t gestures = 0;
    for (std::uint32_t crt = 0; crt < CrtStates; ++crt)
    {
        const float height = heightAfterAStep(inside, SeedWhoseFirstRollIs(1), crt);
        EXPECT_NEAR(heightAfterAStep(inside, SeedWhoseFirstRollIs(0), crt), height, Rounding)
            << "la CRT en " << crt << ": la semilla no cuenta";
        EXPECT_NEAR(heightAfterAStep(outside, SeedWhoseFirstRollIs(1), crt), standing, Rounding) << crt;
        if (height > standing + Rounding)
        {
            EXPECT_NEAR(height, raised, Rounding) << crt;
            ++gestures;
        }
        else
        {
            EXPECT_NEAR(height, standing, Rounding) << crt;
        }
    }
    EXPECT_GT(gestures, 0u) << "con algún estado de la CRT, el gesto";
    EXPECT_LT(gestures, CrtStates) << "y con otro, no";
}

// 0x4fcd00: when the action ends (0x4ec200 wraps around), 0x4fcb10 sets the standing one again (0x4f2250, 0x4a with
// the horse). The library's 0x54 has two keys at one per step: it ends on the second, and on the third the character
// is already standing.
TEST(LoginSceneModelsTest, TheCharacterStandsAgainWhenItsGestureEnds)
{
    const float standing = LoginCharacterTransform().position.z;
    const float raised = standing + (RaisedHeight * LoginCharacterTransform().scale);
    Harness tested;
    FakeClock clock;
    clock.Advance(std::chrono::milliseconds{(3 * 2300) + 50});
    ASSERT_TRUE(tested.Make(SeedWhoseFirstRollIs(1), &clock).Enter().HasValue());
    tested.Steps(1);
    ASSERT_NEAR(CharacterHeight(tested), raised, Rounding);
    clock.Advance(std::chrono::milliseconds{500});

    tested.Steps(2);

    EXPECT_NEAR(CharacterHeight(tested), standing, Rounding);
}

// The horse's stretch (its texture, 0x3bc) among the frame's, or the end if it is not there.
std::size_t HorseCommand(const FrameEvent& models)
{
    const auto horse =
        std::ranges::find(models.modelCommands, std::optional{TextureSlot{0x3bc}},
                          [](const Client::Render::ModelDrawCommand& command) { return command.state.texture; });
    return static_cast<std::size_t>(horse - models.modelCommands.begin());
}

// 0x6350b0: the characters (0x510d00), the helpers (0x4453b0, the horse) and the objects (0x6046a0, the logo). After
// the first step, the horse is already there, with normal blending: its alpha is 0.1.
TEST(LoginSceneModelsTest, TheHorseGoesBetweenTheCharacterAndTheLogo)
{
    Harness tested;
    ASSERT_TRUE(tested.Make().Enter().HasValue());
    tested.Steps(1);

    const FrameEvent models = tested.ModelsOfAFrame();

    const std::size_t count = models.modelCommands.size();
    ASSERT_GE(count, 4u);
    EXPECT_EQ(HorseCommand(models), count - 3);
    EXPECT_EQ(models.modelCommands[count - 3].state.blend, ModelBlend::Alpha);
    EXPECT_EQ(models.modelCommands[count - 2].state.texture, LogoSlot);
    EXPECT_EQ(models.modelCommands[0].state.texture, TextureSlot{0x300});
}

// 0x443d70 creates it with alpha at 0, and 0x5ffdf0 draws nothing below 0.01: before the first step it is not there.
// Then it rises a tenth of what it lacks on each one (0x4e9980), and from the 44th it passes 0.99 and goes opaque.
TEST(LoginSceneModelsTest, TheHorseFadesInStepByStep)
{
    Harness tested;
    ASSERT_TRUE(tested.Make().Enter().HasValue());
    {
        const FrameEvent models = tested.ModelsOfAFrame();
        EXPECT_EQ(HorseCommand(models), models.modelCommands.size());
    }
    tested.Steps(2);
    {
        const FrameEvent models = tested.ModelsOfAFrame();
        ASSERT_LT(HorseCommand(models), models.modelCommands.size());
        const Client::Render::ModelDrawCommand& horse = models.modelCommands[HorseCommand(models)];
        EXPECT_FLOAT_EQ(models.modelVertices[horse.firstVertex].color.alpha, 0.19f);
    }
    tested.Steps(42);
    {
        const FrameEvent models = tested.ModelsOfAFrame();
        ASSERT_LT(HorseCommand(models), models.modelCommands.size());
        const Client::Render::ModelDrawCommand& horse = models.modelCommands[HorseCommand(models)];
        EXPECT_EQ(horse.state.blend, ModelBlend::Opaque);
        EXPECT_FLOAT_EQ(models.modelVertices[horse.firstVertex].color.alpha, 1.0f);
    }
}

// 0x444010 in the same step, after 0x634c60 gave the rider 0x54: the horse takes its 5, which raises it, at the
// rider's place and with 0x445260's 1.6 scale.
TEST(LoginSceneModelsTest, TheHorseRearsWithItsRider)
{
    const float standing = LoginCharacterTransform().position.z;
    const float raised = standing + (RaisedHeight * Client::Pets::DarkHorseLoginScale);
    for (const int roll : {1, 0})
    {
        Harness tested;
        FakeClock clock;
        clock.Advance(std::chrono::milliseconds{(3 * 2300) + 50});
        ASSERT_TRUE(tested.Make(SeedWhoseFirstRollIs(roll), &clock).Enter().HasValue());
        tested.Steps(1);

        const FrameEvent models = tested.ModelsOfAFrame();

        ASSERT_LT(HorseCommand(models), models.modelCommands.size());
        const ModelVertex& first = models.modelVertices[models.modelCommands[HorseCommand(models)].firstVertex];
        EXPECT_NEAR(first.position.z, roll == 1 ? raised : standing, Rounding) << roll;
        EXPECT_NEAR(first.position.x, LoginCharacterTransform().position.x, 2.0f);
    }
}

// The horse sees the rider's action before the rider advances (0x445230 before 0x508f00): on the step the gesture
// ends (the second, outside the window), the rider goes back to 0x4a afterwards, and the horse stays raised. On the
// next it takes its 0 and advances 0.3: the pose goes 30 % from the gesture's key toward the standing one (0x4eb860
// with the previous action).
TEST(LoginSceneModelsTest, TheHorseStandsAStepAfterItsRider)
{
    const float standing = LoginCharacterTransform().position.z;
    const float raised = standing + (RaisedHeight * Client::Pets::DarkHorseLoginScale);
    Harness tested;
    FakeClock clock;
    clock.Advance(std::chrono::milliseconds{(3 * 2300) + 50});
    ASSERT_TRUE(tested.Make(SeedWhoseFirstRollIs(1), &clock).Enter().HasValue());
    tested.Steps(1);
    clock.Advance(std::chrono::milliseconds{500});
    const auto horseHeight = [&tested]
    {
        const FrameEvent models = tested.ModelsOfAFrame();
        EXPECT_LT(HorseCommand(models), models.modelCommands.size());
        return HorseCommand(models) < models.modelCommands.size()
                   ? models.modelVertices[models.modelCommands[HorseCommand(models)].firstVertex].position.z
                   : 0.0f;
    };

    tested.Steps(1);
    EXPECT_NEAR(horseHeight(), raised, Rounding);
    tested.Steps(1);
    EXPECT_NEAR(horseHeight(), raised - (0.3f * RaisedHeight * Client::Pets::DarkHorseLoginScale), Rounding);
    EXPECT_GT(horseHeight(), standing);
}

// Each login entry creates the horse again (0x6349a0): invisible until the first step.
TEST(LoginSceneModelsTest, TheHorseIsCreatedAgainOnEachEntry)
{
    Harness tested;
    LoginScene& scene = tested.Make();
    ASSERT_TRUE(scene.Enter().HasValue());
    tested.Steps(5);
    scene.Leave();

    ASSERT_TRUE(scene.Enter().HasValue());

    const FrameEvent models = tested.ModelsOfAFrame();
    EXPECT_EQ(HorseCommand(models), models.modelCommands.size());
}
} // namespace

namespace
{
TEST(LoginSceneModelsTest, NativeLoginPosesUpdateTheDurableCarryAndIntermediateDrawsKeepItsHistory)
{
    Client::Kernel::WorldState world;
    Harness tested;
    auto attributes = Client::Tests::PlainTerrainAttributes(56, Client::Tests::AttributeCells::Bytes);
    std::fill(attributes.begin() + 4, attributes.end(), std::byte{0});
    Client::Tests::WriteSceneFile(tested.folder.Path(), "World56/EncTerrain56.att",
                                  Client::Tests::EncryptTerrainAttributes(std::move(attributes)));
    Client::Tests::WriteSceneFile(tested.folder.Path(), "World56/EncTerrain56.map",
                                  Client::Tests::EncryptK16(Client::Tests::PlainTerrainMapping(56)));
    Client::Tests::WriteSceneFile(tested.folder.Path(), "Interface/accept_box01.OZJ",
                                  Client::Tests::OzjOf(Client::Tests::ReadJpegVector("baseline-420.jpg")));
    Client::Tests::WriteSceneFile(tested.folder.Path(), "World56/TileGrass01.OZJ",
                                  Client::Tests::OzjOf(Client::Tests::ReadJpegVector("baseline-420.jpg")));
    Client::Tests::WriteSceneFile(tested.folder.Path(), "Object8/drop01.OZJ",
                                  Client::Tests::OzjOf(Client::Tests::ReadJpegVector("baseline-420.jpg")));
    const auto entered = tested.Make(FixedSeed, nullptr, nullptr, &world).Enter();
    ASSERT_TRUE(entered.HasValue()) << entered.Error();
    // This fixture's synthetic library only lends body/horse; the raven is tested separately.
    for (auto& actor : world.Actors())
        actor.darkSpirit.reset();
    tested.Steps(1);
    static_cast<void>(tested.ModelsOfAFrame());
    ASSERT_FALSE(world.PoseCarry().bones.empty());
    ASSERT_TRUE(world.PoseCarry().models.contains(Client::FrontEnd::LoginLogoModel));
    world.PoseCarry().bones[0][3] = {7, 8, 9};
    static_cast<void>(tested.ModelsOfAFrame());
    EXPECT_EQ(world.PoseCarry().bones[0][3], glm::vec3(7, 8, 9));
    tested.scene->Leave();
    EXPECT_EQ(world.PoseCarry().bones[0][3], glm::vec3(7, 8, 9));
    EXPECT_EQ(tested.scene->Failure(), std::nullopt);
}

// The login of the pose tests: world 56 with its files, without the raven, and one step. With `cloth`, the cloth's
// global stream: the 0x50ba30 branch with the cloth and Player.bmd.
void EnterTheLoginWithItsWorld(Harness& tested, Client::Kernel::WorldState& world,
                               Client::Models::ClothWorld* cloth = nullptr)
{
    auto attributes = Client::Tests::PlainTerrainAttributes(56, Client::Tests::AttributeCells::Bytes);
    std::fill(attributes.begin() + 4, attributes.end(), std::byte{0});
    Client::Tests::WriteSceneFile(tested.folder.Path(), "World56/EncTerrain56.att",
                                  Client::Tests::EncryptTerrainAttributes(std::move(attributes)));
    Client::Tests::WriteSceneFile(tested.folder.Path(), "World56/EncTerrain56.map",
                                  Client::Tests::EncryptK16(Client::Tests::PlainTerrainMapping(56)));
    Client::Tests::WriteSceneFile(tested.folder.Path(), "Interface/accept_box01.OZJ",
                                  Client::Tests::OzjOf(Client::Tests::ReadJpegVector("baseline-420.jpg")));
    Client::Tests::WriteSceneFile(tested.folder.Path(), "World56/TileGrass01.OZJ",
                                  Client::Tests::OzjOf(Client::Tests::ReadJpegVector("baseline-420.jpg")));
    Client::Tests::WriteSceneFile(tested.folder.Path(), "Object8/drop01.OZJ",
                                  Client::Tests::OzjOf(Client::Tests::ReadJpegVector("baseline-420.jpg")));
    const auto entered = tested.Make(FixedSeed, nullptr, cloth, &world).Enter();
    ASSERT_TRUE(entered.HasValue()) << entered.Error();
    for (auto& actor : world.Actors())
        actor.darkSpirit.reset();
    tested.Steps(1);
}

// 0x635206 → 0x510d00 → 0x50ba30 with the login's: the branch with the cloth and Player.bmd looks at the shadow guards
// (0x50bbff..0x50bc71, CastsShadow: alpha from 0.5 and without +0x3e4; world 56 is map 55, with shadow) and, in a
// frame of the original, 0x60cf70 with the 0x20b (0x50bcec) copies the character's scale and position to it
// (WorldState::PlaceShadowModel), which 0x632/0 and /1 read on reaching the ground. With alpha at 0.4 or with +0x3e4,
// no; an intermediate draw neither.
TEST(LoginSceneModelsTest, TheRealBranchPlacesTheShadowModelBehindItsGuards)
{
    constexpr std::uint16_t Shadow = Client::Tables::PlayerShadowModel;
    auto world = std::make_unique<Client::Kernel::WorldState>();
    Harness tested;
    Client::Models::ClothWorld cloth;
    ASSERT_NO_FATAL_FAILURE(EnterTheLoginWithItsWorld(tested, *world, &cloth));
    ASSERT_EQ(world->Runtime().map, 55);
    auto& actor = world->Actors()[Client::FrontEnd::LoginActorSlot];
    ASSERT_TRUE(actor.alive && actor.seen);
    // Without the scepter: its model is not in the synthetic library and what hangs would stop at 0x50a169.
    for (auto& hand : actor.hands)
        hand.model = Client::Kernel::NoItemModel;
    const auto drawAt = [&tested](Client::Core::Duration clock)
    {
        tested.scene->Render(tested.target.BeginFrame(clock), 0.0f);
        tested.target.Present();
        EXPECT_EQ(tested.scene->Failure(), std::nullopt);
    };
    using Client::Models::OriginalFrameTime;

    world->PlaceModel(Shadow, {});
    drawAt(OriginalFrameTime);
    EXPECT_EQ(world->ModelPlacement(Shadow).origin, actor.position);
    EXPECT_EQ(world->ModelPlacement(Shadow).scale, actor.scale);

    world->PlaceModel(Shadow, {});
    drawAt(OriginalFrameTime);
    EXPECT_EQ(world->ModelPlacement(Shadow).scale, 0.0f);

    actor.alpha = 0.4f;
    drawAt(OriginalFrameTime * 2);
    EXPECT_EQ(world->ModelPlacement(Shadow).scale, 0.0f);

    actor.alpha = 1.0f;
    actor.byte3e4 = 1;
    drawAt(OriginalFrameTime * 3);
    EXPECT_EQ(world->ModelPlacement(Shadow).scale, 0.0f);

    actor.byte3e4 = 0;
    drawAt(OriginalFrameTime * 4);
    EXPECT_EQ(world->ModelPlacement(Shadow).origin, actor.position);
    tested.scene->Leave();
}

// 0x634b45..0x634b5e: on entering, 120 empty messages leave the old area empty.
TEST(LoginSceneModelsTest, EnteringEmptiesTheOldMessageArea)
{
    // Before the scene, which reads it on leaving.
    Client::Kernel::WorldState world;
    Harness tested;
    world.Runtime().chat.area.rows[0].text = "viejo";
    world.Runtime().chat.area.count = 1;

    ASSERT_NO_FATAL_FAILURE(EnterTheLoginWithItsWorld(tested, world));

    EXPECT_EQ(world.Runtime().chat.area.rows[0].text, "");
    EXPECT_EQ(world.Runtime().chat.area.count, static_cast<std::int32_t>(Client::Ui::MessageAreaRows));
}

// Q-C410: 0x6349a0 builds the login's one with 0x517270 → 0x511290, which sets +0x11c to 1 (0x511bf7) and gives it
// its bone buffer (+0x120); 0x6001b0 poses the body there (0x600259). The body does not reach the global buffer
// (0x68b181c) in either a frame of the original or an intermediate one, and only the frame of the original stores it
// in its own.
TEST(LoginSceneModelsTest, TheBodyGoesToItsOwnBonesAndNeverToTheGlobalPose)
{
    Client::Kernel::WorldState world;
    Harness tested;
    ASSERT_NO_FATAL_FAILURE(EnterTheLoginWithItsWorld(tested, world));
    auto& actor = world.Actors()[Client::FrontEnd::LoginActorSlot];
    actor.bodyPose.clear();

    static_cast<void>(tested.ModelsOfAFrame());

    EXPECT_FALSE(world.PoseCarry().models.contains(Client::Tables::PlayerSkeletonModel));
    ASSERT_EQ(actor.bodyPose.size(), 48u);
    actor.bodyPose[0][3] = {7, 8, 9};
    // Q-C412: 0x6001b0 leaves its scale and position in Player.bmd's shared model.
    EXPECT_EQ(world.PlayerModel().scale, actor.scale);
    EXPECT_EQ(world.PlayerModel().origin, actor.position);

    static_cast<void>(tested.ModelsOfAFrame());

    EXPECT_FALSE(world.PoseCarry().models.contains(Client::Tables::PlayerSkeletonModel));
    EXPECT_EQ(actor.bodyPose[0][3], glm::vec3(7, 8, 9));
    EXPECT_EQ(tested.scene->Failure(), std::nullopt);
}

// The body starts from its own bones: dummy bone 0 keeps what the character's previous draw left, not whatever is in
// the global. The boot is the first part: its vertex (0, 0, 0) of bone 0 comes out of (5, 10, 20) times the 1.6
// scale plus (10603, 14112, 580), to float at each step (0x4ebc80); with the global, (70, 110, 130), it would give
// (10715, 14288, 788).
TEST(LoginSceneModelsTest, TheBodyKeepsTheDummyMatricesOfItsOwnBones)
{
    Client::Kernel::WorldState world;
    Harness tested;
    tested.models.RetainTheDummyRoot();
    ASSERT_NO_FATAL_FAILURE(EnterTheLoginWithItsWorld(tested, world));
    Client::Math::BoneTransform own(1.0f);
    own[3] = {5.0f, 10.0f, 20.0f};
    auto& actor = world.Actors()[Client::FrontEnd::LoginActorSlot];
    actor.bodyPose.assign(1, own);
    world.PoseCarry().bones.assign(1, Client::Math::BoneTransform(1.0f));
    world.PoseCarry().bones[0][3] = {70.0f, 110.0f, 130.0f};

    const FrameEvent models = tested.ModelsOfAFrame();

    // With the world loaded, the pass does not start with the character: the boots are looked up by their texture.
    const auto boots =
        std::ranges::find(models.modelCommands, std::optional{TextureSlot{0x300}},
                          [](const Client::Render::ModelDrawCommand& command) { return command.state.texture; });
    ASSERT_NE(boots, models.modelCommands.end());
    ASSERT_LT(boots->firstVertex, models.modelVertices.size());
    EXPECT_EQ(models.modelVertices[boots->firstVertex].position, (glm::vec3{10611.0f, 14128.0f, 612.0f}));
    ASSERT_EQ(actor.bodyPose.size(), 48u);
    EXPECT_EQ(actor.bodyPose[0], own);
    EXPECT_EQ(tested.scene->Failure(), std::nullopt);
}

// The meteor in the login scene: 0x6046a0 drops it into the world table with the draw clock in the window
// (ftol(clock) % 8000 from 7950), and the step (0x634d18 → 0x52d220) advances it with the wizards' one, after the
// dragon, without stopping: the 0x5f2/0x24 trail follows the meteor (its owner, slot 1) in the particles' step
// (0x634d22 → 0x5446e0), and the 0x5f2/0x25 and the 0x5d9/5 fire come out with it. The login used to stop at
// 0x604427.
TEST(LoginSceneModelsTest, TheLoginDropsTheMeteorOfTheObject89AndKeepsGoing)
{
    constexpr std::int16_t MeteorMarker = 0x59;
    auto world = std::make_unique<Client::Kernel::WorldState>();
    Harness tested;
    auto attributes = Client::Tests::PlainTerrainAttributes(56, Client::Tests::AttributeCells::Bytes);
    std::fill(attributes.begin() + 4, attributes.end(), std::byte{0});
    Client::Tests::WriteSceneFile(tested.folder.Path(), "World56/EncTerrain56.att",
                                  Client::Tests::EncryptTerrainAttributes(std::move(attributes)));
    Client::Tests::WriteSceneFile(tested.folder.Path(), "World56/EncTerrain56.map",
                                  Client::Tests::EncryptK16(Client::Tests::PlainTerrainMapping(56)));
    const std::vector<std::byte> ozj = Client::Tests::OzjOf(Client::Tests::ReadJpegVector("baseline-420.jpg"));
    Client::Tests::WriteSceneFile(tested.folder.Path(), "Interface/accept_box01.OZJ", ozj);
    Client::Tests::WriteSceneFile(tested.folder.Path(), "World56/TileGrass01.OZJ", ozj);
    Client::Tests::WriteSceneFile(tested.folder.Path(), "Object8/drop01.OZJ", ozj);
    Client::Data::Terrain::TerrainObject marker;
    marker.type = MeteorMarker;
    marker.position = {11679.0f, 14191.0f, 333.0f};
    marker.scale = 1.0f;
    Client::Tests::WriteSceneFile(
        tested.folder.Path(), "World56/EncTerrain56.obj",
        Client::Tests::EncryptK16(Client::Tests::PlainTerrainObjects(56, 1, std::span(&marker, 1))));
    Client::Models::ClothWorld cloth;
    const auto entered = tested.Make(FixedSeed, nullptr, &cloth, world.get()).Enter();
    ASSERT_TRUE(entered.HasValue()) << entered.Error();
    for (auto& actor : world->Actors())
        actor.darkSpirit.reset();
    const auto effects = world->Map().effects.Effects();
    ASSERT_TRUE(effects[0].alive) << "el dragón del mapa 55";
    ASSERT_EQ(effects[0].type, Client::Effects::DragonEffect);
    for (int frame = 0; frame < 4 && !effects[1].alive; ++frame)
    {
        tested.scene->Render(
            tested.target.BeginFrame(std::chrono::milliseconds{7950} + frame * Client::Models::OriginalFrameTime),
            0.0f);
        tested.target.Present();
    }
    ASSERT_EQ(tested.scene->Failure(), std::nullopt);
    const Client::Effects::ModelEffect& meteor = effects[1];
    ASSERT_TRUE(meteor.alive);
    EXPECT_EQ(meteor.type, Client::Effects::FireEffect);
    EXPECT_EQ(meteor.subtype, 9);
    const std::int32_t dragonLife = effects[0].life;
    const glm::vec3 born = meteor.transform.position;
    tested.Steps(1);
    ASSERT_EQ(tested.scene->Failure(), std::nullopt);
    EXPECT_EQ(effects[0].life, dragonLife - 1);
    EXPECT_EQ(meteor.life, 599);
    EXPECT_EQ(meteor.previousPosition, born);
    EXPECT_EQ(meteor.transform.position.y, born.y + meteor.velocity.y);
    EXPECT_EQ(meteor.light, glm::vec3(1.0f, std::bit_cast<float>(0x3e4ccccdu), std::bit_cast<float>(0x3e4ccccdu)));
    const auto alive = [&](std::int32_t type, std::int32_t subtype)
    {
        return std::ranges::count_if(world->Particles().All(),
                                     [&](const Client::Particles::Particle& particle) {
                                         return particle.alive && particle.type == type && particle.subtype == subtype;
                                     });
    };
    EXPECT_EQ(alive(0x5f2, 0x24), 1);
    EXPECT_EQ(alive(0x5f2, 0x25), 1);
    EXPECT_EQ(alive(0x5d9, 5), 1);
    tested.Steps(30);
    ASSERT_EQ(tested.scene->Failure(), std::nullopt);
    EXPECT_TRUE(meteor.alive);
    EXPECT_EQ(meteor.life, 569);
    tested.scene->Leave();
}

// The login draws the world's joints (0x635217 → 0x53c6a0(0), after the objects and before the effects) and advances
// them (0x634d1d → 0x53c670, between the effects and the particles: the common tail takes 1 from the life,
// 0x53c3ba). A hand-built stretch of the 0x60f comes out in the models' pass, with its texture, and a Kundun ray
// (0x613/3, ownerless, width 50) loses one life in a step.
TEST(LoginSceneModelsTest, TheLoginStepsAndDrawsTheJointsOfTheWorld)
{
    auto world = std::make_unique<Client::Kernel::WorldState>();
    Harness tested;
    auto attributes = Client::Tests::PlainTerrainAttributes(56, Client::Tests::AttributeCells::Bytes);
    std::fill(attributes.begin() + 4, attributes.end(), std::byte{0});
    Client::Tests::WriteSceneFile(tested.folder.Path(), "World56/EncTerrain56.att",
                                  Client::Tests::EncryptTerrainAttributes(std::move(attributes)));
    Client::Tests::WriteSceneFile(tested.folder.Path(), "World56/EncTerrain56.map",
                                  Client::Tests::EncryptK16(Client::Tests::PlainTerrainMapping(56)));
    const std::vector<std::byte> ozj = Client::Tests::OzjOf(Client::Tests::ReadJpegVector("baseline-420.jpg"));
    Client::Tests::WriteSceneFile(tested.folder.Path(), "Interface/accept_box01.OZJ", ozj);
    Client::Tests::WriteSceneFile(tested.folder.Path(), "World56/TileGrass01.OZJ", ozj);
    Client::Tests::WriteSceneFile(tested.folder.Path(), "Object8/drop01.OZJ", ozj);
    Client::Tests::WriteSceneFile(tested.folder.Path(), "World56/EncTerrain56.obj",
                                  Client::Tests::EncryptK16(Client::Tests::PlainTerrainObjects(
                                      56, 0, std::span<const Client::Data::Terrain::TerrainObject>{})));
    Client::Models::ClothWorld cloth;
    const auto entered = tested.Make(FixedSeed, nullptr, &cloth, world.get()).Enter();
    ASSERT_TRUE(entered.HasValue()) << entered.Error();
    for (auto& actor : world->Actors())
        actor.darkSpirit.reset();
    auto& joints = world->DrawTail().joints;
    Client::Kernel::JointState& drawn = joints[0];
    drawn.alive = true;
    drawn.type = 0x60f;
    drawn.texture = 0x60f;
    drawn.count = 1;
    drawn.maximumCount = 3;
    drawn.drawEnabled = 1;
    drawn.blend = 1;
    drawn.light = glm::vec3(1.0f);
    drawn.points[0][2] = {11600.0f, 14100.0f, 300.0f};
    drawn.points[0][3] = {11700.0f, 14100.0f, 300.0f};
    drawn.points[1][2] = {11600.0f, 14200.0f, 300.0f};
    drawn.points[1][3] = {11700.0f, 14200.0f, 300.0f};

    tested.scene->Render(tested.target.BeginFrame(Client::Models::OriginalFrameTime), 0.0f);
    tested.target.Present();

    ASSERT_EQ(tested.scene->Failure(), std::nullopt);
    const std::vector<FrameEvent>& events = tested.target.frames.back().events;
    const auto models = std::ranges::find(events, Kind::Models, &FrameEvent::kind);
    ASSERT_NE(models, events.end());
    const auto faces =
        std::ranges::count_if(models->modelCommands,
                              [&](const auto& command)
                              {
                                  return command.vertexCount > 0 && command.state.texture &&
                                         command.state.texture->value == 0x60f &&
                                         models->modelVertices.at(command.firstVertex).position == drawn.points[0][2];
                              });
    EXPECT_EQ(faces, 1);

    drawn.alive = false;
    glm::vec3 point{11679.0f, 14191.0f, 533.0f};
    glm::vec3 angles{-10.0f, 0.0f, 40.0f};
    const Client::Kernel::JointRequest request{.type = 0x613,
                                               .firstPosition = point,
                                               .secondPosition = point,
                                               .angles = angles,
                                               .subtype = 3,
                                               .ownerContext = nullptr,
                                               .scale = 50.0f,
                                               .key = 0,
                                               .skill = 0,
                                               .skillSerial = 0,
                                               .target = -1};
    ASSERT_TRUE(Client::Joints::CreateFaunaJoint(joints, request, cloth.random).HasValue());
    const Client::Kernel::JointState& ray = joints[0];
    ASSERT_TRUE(ray.alive);
    ASSERT_EQ(ray.type, 0x613);
    const std::int32_t life = ray.life;

    tested.Steps(1);

    ASSERT_EQ(tested.scene->Failure(), std::nullopt);
    EXPECT_EQ(ray.life, life - 1);
    tested.scene->Leave();
}
} // namespace
