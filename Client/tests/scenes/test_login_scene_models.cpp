#include "Scenes/LoginScene.h"

#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Data/Models/Model.h"
#include "Data/Terrain/TerrainHeights.h"
#include "Input/InputState.h"
#include "Render/ModelBatch.h"
#include "Render/ModelVertex.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/HalfLifeMath.h"
#include "Render/Models/ItemGlow.h"
#include "Render/Models/Skeleton.h"
#include "Render/TextureSlot.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/DarkHorse.h"
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
#include "support/LoginLogoFiles.h"
#include "support/RecordingFrameTarget.h"
#include "support/RecordingSink.h"
#include "support/SceneDataFiles.h"
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
#include <tuple>
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

// World56 con las alturas en 0 (plano: la sombra es 1) y la luz de light-256.jpg.
void WriteWorld56(const std::filesystem::path& root)
{
    Client::Tests::WriteSceneFile(root, "World56/TerrainHeight.OZB",
                                  std::vector<std::byte>(Client::Data::Terrain::TerrainHeightsSize));
    Client::Tests::WriteSceneFile(root, "World56/TerrainLight.OZJ",
                                  Client::Tests::OzjOf(Client::Tests::ReadJpegVector("light-256.jpg")));
}

// Un esqueleto de `bones` huesos sin padre con `actions` acciones de dos keys que avanzan una por paso:
// en las dos de `raised`, cada hueso a 100 de altura; en las demás, en el origen.
Skeleton RaisingSkeleton(std::size_t actions, std::size_t bones, std::array<std::size_t, 2> raised)
{
    SkeletonBone bone;
    bone.parent = -1;
    for (std::size_t action = 0; action < actions; ++action)
    {
        BoneKey key;
        key.rotation = Client::Render::Models::AngleQuaternion({0.0f, 0.0f, 0.0f});
        key.position = {0.0f, 0.0f, action == raised[0] || action == raised[1] ? RaisedHeight : 0.0f};
        bone.actions.push_back({key, key});
    }
    Skeleton skeleton;
    skeleton.bones.assign(bones, bone);
    skeleton.actions.assign(actions, ActionInfo{.repeats = true, .speed = 1.0f, .keyCount = 2, .lockPositions = false});
    return skeleton;
}

// Player.bmd con 48 huesos y 0x56 acciones (las 0x54 y 0x55 alzan el hueso 0), una parte de un
// triángulo para cualquier otro modelo, sin la sombra, y el caballo (0xbf) con sus siete acciones (la
// 5 y la 6 lo alzan) y su textura en 0x3bc.
class LoginPlayer final : public IModelLibrary
{
public:
    LoginPlayer()
        : m_body(RaisingSkeleton(PlayerActions, 48, {0x54, 0x55})), m_horse(RaisingSkeleton(7, 1, {5, 6})),
          m_part(Client::Data::Models::Model{})
    {
        m_part.meshes = {Client::Tests::TriangleMesh(0, "part.jpg")};
    }

    [[nodiscard]] const Client::Data::Models::Model* Find(std::uint16_t model) const override
    {
        return model == 0x20b ? nullptr : &m_part;
    }
    [[nodiscard]] const Skeleton* SkeletonOf(std::uint16_t model) const override
    {
        if (model == Client::Scenes::DarkHorseModel)
        {
            return &m_horse;
        }
        return model == 0x20a ? &m_body : nullptr;
    }
    [[nodiscard]] std::span<const TextureSlot> MeshTextures(std::uint16_t model) const override
    {
        return model == Client::Scenes::DarkHorseModel ? m_horseSlots : m_slots;
    }

private:
    Skeleton m_body;
    Skeleton m_horse;
    Client::Data::Models::Model m_part;
    std::array<TextureSlot, 1> m_slots{TextureSlot{0x300}};
    std::array<TextureSlot, 1> m_horseSlots{TextureSlot{0x3bc}};
};

struct Harness
{
    explicit Harness(bool withLogo = true, bool withWorld = true) : log(sink, LogLevel::Debug)
    {
        Client::Tests::WriteTextureSet(folder.Path(), Client::Scenes::LoginTextureSet);
        Client::Tests::WriteTextureSet(folder.Path(), Client::Scenes::WindowTextureSet);
        if (withLogo)
        {
            Client::Tests::WriteLoginLogo(folder.Path());
        }
        if (withWorld)
        {
            WriteWorld56(folder.Path());
        }
    }

    // Con `cloth`, lo global de la tela del bucle: el rand() de la CRT.
    LoginScene& Make(std::mt19937::result_type seed = FixedSeed, FakeClock* sceneClock = nullptr,
                     Client::Render::Models::ClothWorld* cloth = nullptr)
    {
        return scene.emplace(SceneContext{folder.Path(), store, log, sceneClock, cloth}, data, models, nullptr, "0.0.0",
                             seed);
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

// Los dos slots del logo con un mundo sin modelos: los que deja 0x60d530(0x41c) de 0x616ec0.
const TextureSlot LogoSlot{0x41c};
const TextureSlot LogoSlot2{0x41d};

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
// un tramo opaco con logo.jpg y el aditivo de su blendMesh con logo2.jpg. El Data del test no tiene
// Object56: el mundo no carga ningún modelo y deja el próximo slot en 0x41c, donde sigue el logo
// (Q-C92).
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

// Q-C105: el color del logo es 0,1 de la luz del terreno más la suya, y su blendMesh va además por
// esa luz. 0x606560 solo corre los objetos que se dibujaron (+0x16c): el primer paso no escribe la
// luz, que queda la de 0x606660 (+0xf4 en 0 y +0x68 en 1); desde el segundo, cuenta · 0,02, de 0 en
// el 2 a 0,78 en el 41, y 1,5 desde el 42.
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

// Mientras no se dibujó, el logo no cambia (0x606560 con +0x16c en 0), y las rutas siguen: a los diez
// pasos sin dibujo, la luz es la de 0x606660; dibujado, el paso siguiente le da la cuenta, 9 · 0,02.
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

// Al salir se sueltan las dos texturas del logo, como las demás de la escena.
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
        EXPECT_NE(command.state.texture, LogoSlot);
    }
    EXPECT_TRUE(Logged(tested.sink, "el logo no se puede leer"));
    EXPECT_FALSE(tested.store.Find(LogoSlot).has_value());
}

// 0x60d5f0 con su quinto argumento en 1: una textura del logo que falta corta la entrada.
TEST(LoginSceneModelsTest, AMissingLogoTextureStopsTheLogin)
{
    Harness tested(false);
    Client::Tests::WriteLoginLogo(tested.folder.Path(), true);

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

// Con el bucle (lo global de la tela en el contexto), el gesto sale del rand() de la CRT, el del original
// (0x634cca), y no de la semilla de la escena: con la misma CRT, dos semillas cuyo primer rand() % 6 da 1 y 0
// hacen lo mismo, y según el estado de la CRT el gesto sale o no. Fuera de la ventana, nunca. Dónde cae ese
// rand() entre los demás del paso no se fija acá: es P-C27 d.
TEST(LoginSceneModelsTest, WithTheLoopTheGestureTakesTheRandOfTheCrt)
{
    const float standing = LoginCharacterTransform().position.z;
    const float raised = standing + (RaisedHeight * LoginCharacterTransform().scale);
    const std::chrono::milliseconds inside{(3 * 2300) + 50};
    const std::chrono::milliseconds outside{(3 * 2300) + 500};
    // La altura del personaje después de un paso con el reloj en `clock`, la semilla `seed` y la CRT en `crt`.
    const auto heightAfterAStep = [](std::chrono::milliseconds clock, std::mt19937::result_type seed, std::uint32_t crt)
    {
        Harness tested;
        FakeClock sceneClock;
        sceneClock.Advance(clock);
        Client::Render::Models::ClothWorld cloth{.random = Client::Render::Models::CrtRandom(crt)};
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

// 0x4fcd00: cuando la acción termina (0x4ec200 da la vuelta), 0x4fcb10 le pone la de parado otra vez
// (0x4f2250, la 0x4a con el caballo). La 0x54 de la biblioteca tiene dos keys a una por paso: termina
// en el segundo, y en el tercero el personaje ya está parado.
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

// El tramo del caballo (su textura, 0x3bc) entre los del frame, o el final si no está.
std::size_t HorseCommand(const FrameEvent& models)
{
    const auto horse =
        std::ranges::find(models.modelCommands, std::optional{TextureSlot{0x3bc}},
                          [](const Client::Render::ModelDrawCommand& command) { return command.state.texture; });
    return static_cast<std::size_t>(horse - models.modelCommands.begin());
}

// 0x6350b0: los personajes (0x510d00), los ayudantes (0x4453b0, el caballo) y los objetos (0x6046a0,
// el logo). Después del primer paso, el caballo ya está, con la mezcla normal: su alpha es 0,1.
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

// 0x443d70 lo crea con el alpha en 0, y 0x5ffdf0 no dibuja nada por debajo de 0,01: antes del primer
// paso no está. Después sube un décimo de lo que le falta en cada uno (0x4e9980), y desde el 44 pasa
// 0,99 y va opaco.
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

// 0x444010 en el mismo paso, después de que 0x634c60 le dio al jinete la 0x54: el caballo toma su 5,
// que lo alza, en el lugar del jinete y con la escala 1,6 de 0x445260.
TEST(LoginSceneModelsTest, TheHorseRearsWithItsRider)
{
    const float standing = LoginCharacterTransform().position.z;
    const float raised = standing + (RaisedHeight * Client::Scenes::DarkHorseLoginScale);
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

// El caballo ve la acción del jinete antes de que el jinete avance (0x445230 antes de 0x508f00): en el
// paso en que termina el gesto (el segundo, fuera de la ventana), el jinete vuelve a la 0x4a después,
// y el caballo sigue alzado. En el que sigue toma su 0 y avanza 0,3: la pose va un 30 % desde la key
// del gesto hacia la de parado (0x4eb860 con la acción anterior).
TEST(LoginSceneModelsTest, TheHorseStandsAStepAfterItsRider)
{
    const float standing = LoginCharacterTransform().position.z;
    const float raised = standing + (RaisedHeight * Client::Scenes::DarkHorseLoginScale);
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
    EXPECT_NEAR(horseHeight(), raised - (0.3f * RaisedHeight * Client::Scenes::DarkHorseLoginScale), Rounding);
    EXPECT_GT(horseHeight(), standing);
}

// Cada entrada al login crea el caballo otra vez (0x6349a0): invisible hasta el primer paso.
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
