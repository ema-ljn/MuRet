#include "Scenes/CharacterSelectScene.h"

#include "App/SessionState.h"
#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Data/Models/Model.h"
#include "Input/InputState.h"
#include "Input/Key.h"
#include "Render/Models/HalfLifeMath.h"
#include "Render/Models/Skeleton.h"
#include "Render/TextureSlot.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/CharacterSlots.h"
#include "Scenes/GameData.h"
#include "Scenes/SceneContext.h"
#include "Scenes/SceneId.h"
#include "Scenes/TextureSets.h"

#include "support/CharacterPackets.h"
#include "support/FakeTextureStore.h"
#include "support/RecordingFrameTarget.h"
#include "support/RecordingSink.h"
#include "support/SceneDataFiles.h"
#include "support/ScenePrinters.h"
#include "support/SessionHarness.h"
#include "support/TemporaryFolder.h"
#include "support/UiInput.h"

#include <gtest/gtest.h>

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

// Client::Scenes::CharacterSelectScene: la escena 4 con una ServerSession sobre un FakeSocket
// (support/SessionHarness.h), una biblioteca de modelos de un triángulo y el RecordingFrameTarget.
// La lista es sintética (support/CharacterPackets.h). El dibujo de cada personaje contra la traza
// está en GoldenCharacterDrawing.
namespace
{
using Client::App::SessionState;
using Client::Data::Models::Mesh;
using Client::Data::Models::Model;
using Client::Input::InputState;
using Client::Input::Key;
using Client::Render::TextureSlot;
using Client::Render::Models::ActionInfo;
using Client::Render::Models::BoneKey;
using Client::Render::Models::Skeleton;
using Client::Render::Models::SkeletonBone;
using Client::Scenes::CharacterSelectScene;
using Client::Scenes::CharacterSlotHeight;
using Client::Scenes::CharacterSlots;
using Client::Scenes::GameData;
using Client::Scenes::IModelLibrary;
using Client::Scenes::SceneContext;
using Client::Scenes::SceneId;
using Client::Scenes::SelectionTextureSet;
using Client::Scenes::WindowTextureSet;
using Client::Tests::CharacterEntryFields;
using Client::Tests::FakeTextureStore;
using Client::Tests::FrameEvent;
using Client::Tests::LoggedInSession;
using Client::Tests::RecordingFrameTarget;
using Client::Tests::TemporaryFolder;

using Kind = FrameEvent::Kind;

// Player.bmd con 48 huesos y cuatro acciones de dos keys iguales, que avanzan una key por paso: en la
// 3, la de parado, todos en el origen; en las otras, 100 más arriba. Después del primer paso, la pose
// es la de la acción de ahora, sin la mezcla con la anterior. Y un modelo de un triángulo, del hueso
// 0, para cualquier otro número.
class FlatModels final : public IModelLibrary
{
public:
    FlatModels()
    {
        SkeletonBone bone;
        bone.parent = -1;
        for (std::size_t action = 0; action < 4; ++action)
        {
            BoneKey key;
            key.rotation = Client::Render::Models::AngleQuaternion({0.0f, 0.0f, 0.0f});
            key.position = {0.0f, 0.0f, action == 3 ? 0.0f : 100.0f};
            bone.actions.push_back({key, key});
        }
        m_body.bones.assign(48, bone);
        m_body.actions.assign(4, ActionInfo{.repeats = true, .speed = 1.0f, .keyCount = 2, .lockPositions = false});
        Mesh mesh;
        mesh.vertices = {{0, {0, 0, 0}}, {0, {1, 0, 0}}, {0, {0, 1, 0}}};
        mesh.normals = {{0, {0, 0, 1}, 0}};
        mesh.texCoords = {{0, 0}};
        mesh.triangles = {{3, {0, 1, 2, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}}};
        mesh.textureName = "part.jpg";
        m_part.meshes = {mesh};
    }

    [[nodiscard]] const Model* Find(std::uint16_t /*model*/) const override
    {
        return &m_part;
    }
    [[nodiscard]] const Skeleton* SkeletonOf(std::uint16_t model) const override
    {
        return model == 0x20a ? &m_body : nullptr;
    }
    [[nodiscard]] std::span<const TextureSlot> MeshTextures(std::uint16_t /*model*/) const override
    {
        return {};
    }

private:
    Skeleton m_body;
    Model m_part;
};

// Dos personajes sintéticos en los lugares 0 y 3; el 3 bloqueado si `locked`.
std::vector<CharacterEntryFields> TwoCharacters(bool locked = false)
{
    return {{0, "char0001"}, {3, "char0002", 1, static_cast<std::uint8_t>(locked ? 0x01 : 0x00)}};
}

struct Harness
{
    explicit Harness(bool withSession = true) : log(sink, Client::Core::LogLevel::Debug)
    {
        Client::Tests::WriteTextureSet(folder.Path(), SelectionTextureSet);
        Client::Tests::WriteTextureSet(folder.Path(), WindowTextureSet);
        Client::Tests::WriteTextTable(folder.Path(), {{434, "bloqueado"}});
        auto loaded = Client::Scenes::LoadGameData(folder.Path());
        EXPECT_TRUE(loaded.HasValue());
        if (loaded.HasValue())
        {
            data = std::move(loaded).Value();
        }
        scene.emplace(SceneContext{folder.Path(), store, log}, data, models, withSession ? &online.session : nullptr);
    }

    // El login aceptado y Enter: la escena pidió la lista.
    void Enter()
    {
        ASSERT_NO_FATAL_FAILURE(online.LogIn());
        ASSERT_TRUE(scene->Enter().HasValue());
    }

    // Y la lista, y un paso: los personajes están en sus lugares.
    void ReachTheChoice(bool locked = false)
    {
        ASSERT_NO_FATAL_FAILURE(Enter());
        online.ReceiveTheList(TwoCharacters(locked));
        ASSERT_EQ(online.session.State(), SessionState::ChoosingCharacter);
        Step();
    }

    void Step()
    {
        online.session.Poll();
        scene->Step(input);
        input.ConsumeEdges();
        ++steps;
    }

    std::vector<FrameEvent> Frame()
    {
        auto& canvas = target.BeginFrame(std::chrono::milliseconds{40} * steps);
        scene->Render(canvas, 0.0f);
        target.Present();
        return target.frames.back().events;
    }

    // Dónde cae en la pantalla el pecho del personaje del lugar `slot`, con el pase del último frame.
    [[nodiscard]] glm::vec2 OnScreen(std::uint8_t slot)
    {
        const std::vector<FrameEvent> events = Frame();
        const auto drawn = std::ranges::find(events, Kind::Models, &FrameEvent::kind);
        EXPECT_NE(drawn, events.end());
        const glm::vec3 chest(CharacterSlots[slot].position, CharacterSlotHeight + 60.0f);
        const std::optional<glm::vec2> projected = drawn->pass->Project(chest);
        EXPECT_TRUE(projected.has_value());
        return projected.value_or(glm::vec2(0.0f));
    }

    void ClickAt(glm::vec2 point)
    {
        Client::Tests::ClickAt(input, static_cast<int>(point.x), static_cast<int>(point.y));
        Step();
    }

    void PressEnter()
    {
        Client::Tests::PressKey(input, Key::Enter);
        Step();
        Client::Tests::ReleaseKey(input, Key::Enter);
        Step();
    }

    TemporaryFolder folder{"character-select-scene"};
    FakeTextureStore store;
    Client::Tests::RecordingSink sink;
    Client::Core::Log log;
    GameData data;
    FlatModels models;
    LoggedInSession online;
    RecordingFrameTarget target;
    InputState input;
    std::int64_t steps = 0;
    std::optional<CharacterSelectScene> scene;
};

// 0x634c60 manda el F3 00 al pasar a la escena 4: acá, la escena al entrar.
TEST(CharacterSelectSceneTest, EnterRequestsTheList)
{
    Harness tested;

    ASSERT_NO_FATAL_FAILURE(tested.Enter());

    EXPECT_EQ(tested.online.session.State(), SessionState::WaitingCharacterList);
}

// 0x6342c0 y 0x634680 salen antes del 0x33: el frame queda con el clear y nada más.
TEST(CharacterSelectSceneTest, DrawsNothingBeforeTheList)
{
    Harness tested;
    ASSERT_NO_FATAL_FAILURE(tested.Enter());
    tested.Step();

    EXPECT_TRUE(tested.Frame().empty());
}

// Los fondos, el fin del 2D, el pase del mundo y los modelos, en el orden de 0x634680.
TEST(CharacterSelectSceneTest, DrawsInTheOrderOfTheOriginal)
{
    Harness tested;
    ASSERT_NO_FATAL_FAILURE(tested.ReachTheChoice());

    const std::vector<FrameEvent> events = tested.Frame();

    ASSERT_EQ(events.size(), 5u);
    EXPECT_EQ(events[0].kind, Kind::Quad);
    EXPECT_EQ(events[0].texture, std::optional<TextureSlot>{TextureSlot{0x717}});
    EXPECT_EQ(events[1].texture, std::optional<TextureSlot>{TextureSlot{0x718}});
    EXPECT_EQ(events[2].kind, Kind::Flush);
    EXPECT_EQ(events[3].kind, Kind::World);
    EXPECT_EQ(events[4].kind, Kind::Models);
}

// Cada personaje en su lugar de 0x4c20a0, a z = 163, con su giro: el triángulo de cada parte empieza
// en la posición del personaje.
TEST(CharacterSelectSceneTest, PlacesEachCharacterInItsSlot)
{
    Harness tested;
    ASSERT_NO_FATAL_FAILURE(tested.ReachTheChoice());

    const std::vector<FrameEvent> events = tested.Frame();

    const auto models = std::ranges::find(events, Kind::Models, &FrameEvent::kind);
    ASSERT_NE(models, events.end());
    // Seis mallas de un triángulo por personaje: con el equipo en 0, el casco es el 0x1012, que deja
    // ver la cabeza de la clase (0x512930).
    ASSERT_EQ(models->modelVertices.size(), 2u * 6u * 3u);
    for (const auto& [first, slot] : {std::pair{0u, 0u}, std::pair{18u, 3u}})
    {
        const glm::vec3 position = models->modelVertices[first].position;
        EXPECT_NEAR(position.x, CharacterSlots[slot].position.x, 1e-3f) << slot;
        EXPECT_NEAR(position.y, CharacterSlots[slot].position.y, 1e-3f) << slot;
        EXPECT_NEAR(position.z, CharacterSlotHeight, 1e-3f) << slot;
    }
}

// El clic elige al que está bajo el mouse (el picking con el rayo del pase), y uno sobre nadie, a
// ninguno (el -1 de 0x6342c0).
TEST(CharacterSelectSceneTest, AClickChoosesTheOneUnderTheMouse)
{
    Harness tested;
    ASSERT_NO_FATAL_FAILURE(tested.ReachTheChoice());

    tested.ClickAt(tested.OnScreen(3));

    ASSERT_NE(tested.online.session.ChosenCharacter(), nullptr);
    EXPECT_EQ(tested.online.session.ChosenCharacter()->slot, 3);
    tested.ClickAt({5.0f, 5.0f});
    EXPECT_EQ(tested.online.session.ChosenCharacter(), nullptr);
}

// 0x50ba30 y 0x634680: la luz de cada uno es 0,4 y la del elegido 1,4.
TEST(SelectLightTest, ChosenIsBrighter)
{
    Harness tested;
    ASSERT_NO_FATAL_FAILURE(tested.ReachTheChoice());
    tested.ClickAt(tested.OnScreen(0));

    const std::vector<FrameEvent> events = tested.Frame();

    const auto models = std::ranges::find(events, Kind::Models, &FrameEvent::kind);
    ASSERT_NE(models, events.end());
    ASSERT_EQ(models->modelVertices.size(), 36u);
    EXPECT_FLOAT_EQ(models->modelVertices[0].color.red, 1.4f);
    EXPECT_FLOAT_EQ(models->modelVertices[18].color.red, 0.4f);
}

// Enter con uno elegido va a la carga del mundo; sin elegido, nada.
TEST(CharacterSelectSceneTest, EnterGoesToWorldLoading)
{
    Harness tested;
    ASSERT_NO_FATAL_FAILURE(tested.ReachTheChoice());
    tested.PressEnter();
    EXPECT_EQ(tested.scene->Next(), std::nullopt);

    tested.ClickAt(tested.OnScreen(0));
    tested.PressEnter();

    EXPECT_EQ(tested.scene->Next(), SceneId::WorldLoading);
}

// Q-C45: un Enter corto, apretado y soltado en el mismo paso, también entra. El original lo pierde
// si cae entre dos lecturas de GetAsyncKeyState; MuRet lo ve, como en el login.
TEST(CharacterSelectSceneTest, AShortEnterAlsoEnters)
{
    Harness tested;
    ASSERT_NO_FATAL_FAILURE(tested.ReachTheChoice());
    tested.ClickAt(tested.OnScreen(0));

    Client::Tests::TapKey(tested.input, Key::Enter);
    tested.Step();

    EXPECT_EQ(tested.scene->Next(), SceneId::WorldLoading);
}

// El doble clic sobre uno lo elige y entra (Q-C99: el reloj propio de 0x640940).
TEST(CharacterSelectSceneTest, ADoubleClickEnters)
{
    Harness tested;
    ASSERT_NO_FATAL_FAILURE(tested.ReachTheChoice());
    const glm::vec2 point = tested.OnScreen(3);

    tested.ClickAt(point);
    tested.ClickAt(point);

    EXPECT_EQ(tested.scene->Next(), SceneId::WorldLoading);
    EXPECT_EQ(tested.online.session.ChosenCharacter()->slot, 3);
}

// Con el bit 0x1 de las banderas, el aviso 0x84 y no se entra.
TEST(CharacterSelectSceneTest, ALockedCharacterShowsTheNotice)
{
    Harness tested;
    ASSERT_NO_FATAL_FAILURE(tested.ReachTheChoice(true));
    tested.ClickAt(tested.OnScreen(3));

    tested.PressEnter();

    EXPECT_EQ(tested.scene->Next(), std::nullopt);
    const std::vector<FrameEvent> events = tested.Frame();
    EXPECT_TRUE(std::ranges::any_of(events, [](const FrameEvent& event)
                                    { return event.texture == std::optional<TextureSlot>{TextureSlot{0x727}}; }));
}

// 0x6342c0 no hace nada antes de la lista: un clic de entonces no es el primero de un doble clic.
TEST(CharacterSelectSceneTest, AClickBeforeTheListIsNotHalfADoubleClick)
{
    Harness shown;
    ASSERT_NO_FATAL_FAILURE(shown.ReachTheChoice());
    const glm::vec2 point = shown.OnScreen(3);
    Harness tested;
    ASSERT_NO_FATAL_FAILURE(tested.Enter());
    tested.ClickAt(point);
    tested.online.ReceiveTheList(TwoCharacters());
    tested.Step();
    (void)tested.Frame();

    tested.ClickAt(point);

    ASSERT_NE(tested.online.session.ChosenCharacter(), nullptr);
    EXPECT_EQ(tested.online.session.ChosenCharacter()->slot, 3);
    EXPECT_EQ(tested.scene->Next(), std::nullopt);
}

// Con el aviso abierto, la escena atiende solo el aviso (Q-C84): un clic sobre otro no lo elige.
TEST(CharacterSelectSceneTest, WithTheNoticeOpenAClickDoesNotChoose)
{
    Harness tested;
    ASSERT_NO_FATAL_FAILURE(tested.ReachTheChoice(true));
    const glm::vec2 other = tested.OnScreen(0);
    tested.ClickAt(tested.OnScreen(3));
    tested.PressEnter();

    tested.ClickAt(other);

    ASSERT_NE(tested.online.session.ChosenCharacter(), nullptr);
    EXPECT_EQ(tested.online.session.ChosenCharacter()->slot, 3);
}

// Si la sesión falla (el servidor cerró), la escena vuelve al login.
TEST(CharacterSelectSceneTest, AFailedSessionGoesBackToTheLogin)
{
    Harness tested;
    ASSERT_NO_FATAL_FAILURE(tested.ReachTheChoice());

    tested.online.socket.peerClosed = true;
    tested.Step();

    EXPECT_EQ(tested.online.session.State(), SessionState::Failed);
    EXPECT_EQ(tested.scene->Next(), SceneId::Login);
}

// Leave suelta las de la escena y las de las ventanas.
TEST(CharacterSelectSceneTest, LeaveReleasesItsSlots)
{
    Harness tested;
    ASSERT_NO_FATAL_FAILURE(tested.Enter());

    tested.scene->Leave();

    for (const std::uint16_t slot :
         {std::uint16_t{0xc}, std::uint16_t{0x717}, std::uint16_t{0x729}, std::uint16_t{0x727}})
    {
        EXPECT_NE(std::ranges::find(tested.store.released, slot), tested.store.released.end()) << slot;
    }
}

// Sin servidor, los fondos y el pase, sin personajes.
TEST(CharacterSelectSceneTest, WithoutASessionOnlyDraws)
{
    Harness tested(false);
    ASSERT_TRUE(tested.scene->Enter().HasValue());
    tested.Step();

    const std::vector<FrameEvent> events = tested.Frame();

    ASSERT_EQ(events.size(), 5u);
    EXPECT_EQ(events[0].texture, std::optional<TextureSlot>{TextureSlot{0x717}});
    EXPECT_TRUE(events[4].modelVertices.empty());
}
} // namespace
