#include "Models/IModelLibrary.h"
#include "Scenes/LoginScene.h"
#include "Scenes/WorldState.h"

#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Crt/CrtRandom.h"
#include "Data/Models/Model.h"
#include "Data/Models/PlayerModels.h"
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
#include "Scenes/ActorState.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/DarkHorse.h"
#include "Scenes/FaunaJointCreation.h"
#include "Tables/GameData.h"
#include "Scenes/GameDrawTail.h"
#include "Scenes/LoginActor.h"
#include "Scenes/LoginModels.h"
#include "Render/Frame/SceneCanvas.h"
#include "Scenes/SceneContext.h"
#include "Scenes/TextureSets.h"
#include "Scenes/WorldRuntime.h"
#include "Data/Terrain/TerrainObjects.h"
#include "Models/Animation.h"
#include "World/IFishStepServices.h"
#include "World/ModelEffects.h"
#include "World/Particles.h"
#include "World/WizardModelEffects.h"
#include "UI/MessageArea.h"
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
#include "support/TerrainFiles.h"

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
using Client::Render::Models::IModelLibrary;
using Client::Render::Models::Skeleton;
using Client::Render::Models::SkeletonBone;
using Client::Scenes::GameData;
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
        key.rotation = Client::Math::AngleQuaternion({0.0f, 0.0f, 0.0f});
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

    // El hueso 0 del cuerpo, dummy: 0x4eb973 no lo escribe y queda el del buffer donde se posa.
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

// Seis píxeles por byte: los textos de los tests no llegan a los 256 del corte de 0x555190.
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
                     Client::Render::Models::ClothWorld* cloth = nullptr, Client::Scenes::WorldState* world = nullptr)
    {
        SceneContext context{folder.Path(), store, log, sceneClock, cloth};
        context.world = world;
        context.textMeasure = &letters;
        return scene.emplace(context, data, models, nullptr, "0.0.0", seed);
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
        Client::Crt::CrtRandom random(static_cast<std::uint32_t>(seed));
        if (random.Next() % 6 == remainder)
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
        Client::Render::Models::ClothWorld cloth{.random = Client::Crt::CrtRandom(crt)};
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

namespace
{
TEST(LoginSceneModelsTest, NativeLoginPosesUpdateTheDurableCarryAndIntermediateDrawsKeepItsHistory)
{
    Client::Scenes::WorldState world;
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
    // La biblioteca sintética de este fixture sólo presta cuerpo/caballo; el cuervo se prueba aparte.
    for (auto& actor : world.Actors())
        actor.darkSpirit.reset();
    tested.Steps(1);
    static_cast<void>(tested.ModelsOfAFrame());
    ASSERT_FALSE(world.PoseCarry().bones.empty());
    ASSERT_TRUE(world.PoseCarry().models.contains(Client::Scenes::LoginLogoModel));
    world.PoseCarry().bones[0][3] = {7, 8, 9};
    static_cast<void>(tested.ModelsOfAFrame());
    EXPECT_EQ(world.PoseCarry().bones[0][3], glm::vec3(7, 8, 9));
    tested.scene->Leave();
    EXPECT_EQ(world.PoseCarry().bones[0][3], glm::vec3(7, 8, 9));
    EXPECT_EQ(tested.scene->Failure(), std::nullopt);
}

// El login de los tests de la pose: el mundo 56 con sus archivos, sin el cuervo, y un paso. Con `cloth`, el flujo
// global de la tela: la rama del 0x50ba30 con la tela y Player.bmd.
void EnterTheLoginWithItsWorld(Harness& tested, Client::Scenes::WorldState& world,
                               Client::Render::Models::ClothWorld* cloth = nullptr)
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

// 0x635206 → 0x510d00 → 0x50ba30 con el del login: la rama con la tela y Player.bmd mira las guardas de la sombra
// (0x50bbff..0x50bc71, CastsShadow: el alpha desde 0,5 y sin +0x3e4; el mundo 56 es el mapa 55, con sombra) y, en un
// frame del original, 0x60cf70 con el 0x20b (0x50bcec) le copia la escala y la posición del personaje
// (WorldState::PlaceShadowModel), que la 0x632/0 y /1 leen al llegar al suelo. Con el alpha en 0,4 o con +0x3e4, no; un
// dibujo intermedio tampoco.
TEST(LoginSceneModelsTest, TheRealBranchPlacesTheShadowModelBehindItsGuards)
{
    constexpr std::uint16_t Shadow = Client::Data::Models::PlayerShadowModel;
    auto world = std::make_unique<Client::Scenes::WorldState>();
    Harness tested;
    Client::Render::Models::ClothWorld cloth;
    ASSERT_NO_FATAL_FAILURE(EnterTheLoginWithItsWorld(tested, *world, &cloth));
    ASSERT_EQ(world->Runtime().map, 55);
    auto& actor = world->Actors()[Client::Scenes::LoginActorSlot];
    ASSERT_TRUE(actor.alive && actor.seen);
    // Sin el cetro: su modelo no está en la biblioteca sintética y lo que cuelga cortaría en 0x50a169.
    for (auto& hand : actor.hands)
        hand.model = Client::Scenes::NoItemModel;
    const auto drawAt = [&tested](Client::Core::Duration clock)
    {
        tested.scene->Render(tested.target.BeginFrame(clock), 0.0f);
        tested.target.Present();
        EXPECT_EQ(tested.scene->Failure(), std::nullopt);
    };
    using Client::Render::Models::OriginalFrameTime;

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

// 0x634b45..0x634b5e: al entrar, 120 mensajes vacíos dejan vacía el área vieja.
TEST(LoginSceneModelsTest, EnteringEmptiesTheOldMessageArea)
{
    Harness tested;
    Client::Scenes::WorldState world;
    world.Runtime().chat.area.rows[0].text = "viejo";
    world.Runtime().chat.area.count = 1;

    ASSERT_NO_FATAL_FAILURE(EnterTheLoginWithItsWorld(tested, world));

    EXPECT_EQ(world.Runtime().chat.area.rows[0].text, "");
    EXPECT_EQ(world.Runtime().chat.area.count, static_cast<std::int32_t>(Client::UI::MessageAreaRows));
}

// Q-C410: 0x6349a0 arma al del login con 0x517270 → 0x511290, que le pone +0x11c en 1 (0x511bf7) y le da su
// buffer de huesos (+0x120); 0x6001b0 posa ahí el cuerpo (0x600259). El cuerpo no llega al buffer global
// (0x68b181c) ni en un frame del original ni en uno intermedio, y sólo el frame del original lo guarda en el propio.
TEST(LoginSceneModelsTest, TheBodyGoesToItsOwnBonesAndNeverToTheGlobalPose)
{
    Client::Scenes::WorldState world;
    Harness tested;
    ASSERT_NO_FATAL_FAILURE(EnterTheLoginWithItsWorld(tested, world));
    auto& actor = world.Actors()[Client::Scenes::LoginActorSlot];
    actor.bodyPose.clear();

    static_cast<void>(tested.ModelsOfAFrame());

    EXPECT_FALSE(world.PoseCarry().models.contains(Client::Data::Models::PlayerSkeletonModel));
    ASSERT_EQ(actor.bodyPose.size(), 48u);
    actor.bodyPose[0][3] = {7, 8, 9};
    // Q-C412: 0x6001b0 deja su escala y su posición en el modelo compartido de Player.bmd.
    EXPECT_EQ(world.PlayerModel().scale, actor.scale);
    EXPECT_EQ(world.PlayerModel().origin, actor.position);

    static_cast<void>(tested.ModelsOfAFrame());

    EXPECT_FALSE(world.PoseCarry().models.contains(Client::Data::Models::PlayerSkeletonModel));
    EXPECT_EQ(actor.bodyPose[0][3], glm::vec3(7, 8, 9));
    EXPECT_EQ(tested.scene->Failure(), std::nullopt);
}

// El cuerpo arranca de sus huesos propios: el hueso 0 dummy conserva lo que dejó el dibujo anterior del personaje,
// no lo que haya en el global. La bota es la primera parte: su vértice (0, 0, 0) del hueso 0 sale de (5; 10; 20) por
// la escala 1,6 y más (10.603; 14.112; 580), a float en cada paso (0x4ebc80); con el global, (70; 110; 130), daría
// (10.715; 14.288; 788).
TEST(LoginSceneModelsTest, TheBodyKeepsTheDummyMatricesOfItsOwnBones)
{
    Client::Scenes::WorldState world;
    Harness tested;
    tested.models.RetainTheDummyRoot();
    ASSERT_NO_FATAL_FAILURE(EnterTheLoginWithItsWorld(tested, world));
    Client::Math::BoneTransform own(1.0f);
    own[3] = {5.0f, 10.0f, 20.0f};
    auto& actor = world.Actors()[Client::Scenes::LoginActorSlot];
    actor.bodyPose.assign(1, own);
    world.PoseCarry().bones.assign(1, Client::Math::BoneTransform(1.0f));
    world.PoseCarry().bones[0][3] = {70.0f, 110.0f, 130.0f};

    const FrameEvent models = tested.ModelsOfAFrame();

    // Con el mundo cargado, el pase no arranca por el personaje: las botas se buscan por su textura.
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

// El meteoro en la escena del login: 0x6046a0 lo suelta en la tabla del mundo con el reloj del dibujo en la ventana
// (ftol(reloj) % 8000 desde 7950), y el paso (0x634d18 → 0x52d220) lo avanza con el de los magos, después del dragón,
// sin cortar: la estela 5f2/0x24 sigue al meteoro (su dueño, el lugar 1) en el paso de las partículas
// (0x634d22 → 0x5446e0), y la 5f2/0x25 y el fuego 5d9/5 salen con ella. Antes, el login se paraba en 0x604427.
TEST(LoginSceneModelsTest, TheLoginDropsTheMeteorOfTheObject89AndKeepsGoing)
{
    constexpr std::int16_t MeteorMarker = 0x59;
    auto world = std::make_unique<Client::Scenes::WorldState>();
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
    Client::Render::Models::ClothWorld cloth;
    const auto entered = tested.Make(FixedSeed, nullptr, &cloth, world.get()).Enter();
    ASSERT_TRUE(entered.HasValue()) << entered.Error();
    for (auto& actor : world->Actors())
        actor.darkSpirit.reset();
    const auto effects = world->Map().effects.Effects();
    ASSERT_TRUE(effects[0].alive) << "el dragón del mapa 55";
    ASSERT_EQ(effects[0].type, Client::World::DragonEffect);
    for (int frame = 0; frame < 4 && !effects[1].alive; ++frame)
    {
        tested.scene->Render(tested.target.BeginFrame(std::chrono::milliseconds{7950} +
                                                      frame * Client::Render::Models::OriginalFrameTime),
                             0.0f);
        tested.target.Present();
    }
    ASSERT_EQ(tested.scene->Failure(), std::nullopt);
    const Client::World::ModelEffect& meteor = effects[1];
    ASSERT_TRUE(meteor.alive);
    EXPECT_EQ(meteor.type, Client::World::FireEffect);
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
                                     [&](const Client::World::Particle& particle) {
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

// El login dibuja los joints del mundo (0x635217 → 0x53c6a0(0), después de los objetos y antes de los efectos) y los
// avanza (0x634d1d → 0x53c670, entre los efectos y las partículas: la cola común le resta 1 a la vida, 0x53c3ba). Un
// tramo del 0x60f armado a mano sale en el pase de los modelos, con su textura, y un rayo de Kundun (0x613/3, sin
// dueño, ancho 50) pierde una vida en un paso.
TEST(LoginSceneModelsTest, TheLoginStepsAndDrawsTheJointsOfTheWorld)
{
    auto world = std::make_unique<Client::Scenes::WorldState>();
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
    Client::Render::Models::ClothWorld cloth;
    const auto entered = tested.Make(FixedSeed, nullptr, &cloth, world.get()).Enter();
    ASSERT_TRUE(entered.HasValue()) << entered.Error();
    for (auto& actor : world->Actors())
        actor.darkSpirit.reset();
    auto& joints = world->DrawTail().joints;
    Client::Scenes::JointState& drawn = joints[0];
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

    tested.scene->Render(tested.target.BeginFrame(Client::Render::Models::OriginalFrameTime), 0.0f);
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
    const Client::World::FishEffectRequest request{.type = 0x613,
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
    ASSERT_TRUE(Client::Scenes::CreateFaunaJoint(joints, request, cloth.random).HasValue());
    const Client::Scenes::JointState& ray = joints[0];
    ASSERT_TRUE(ray.alive);
    ASSERT_EQ(ray.type, 0x613);
    const std::int32_t life = ray.life;

    tested.Steps(1);

    ASSERT_EQ(tested.scene->Failure(), std::nullopt);
    EXPECT_EQ(ray.life, life - 1);
    tested.scene->Leave();
}
} // namespace
