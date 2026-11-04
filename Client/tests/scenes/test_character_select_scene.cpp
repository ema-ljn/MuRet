#include "Scenes/CharacterSelectScene.h"

#include "App/SessionState.h"
#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Data/Models/Model.h"
#include "Input/InputState.h"
#include "Input/Key.h"
#include "Network/Crypto/Xor32.h"
#include "Network/Packet.h"
#include "Network/PacketSerials.h"
#include "Protocol/Character/CharacterDelete.h"
#include "Render/Models/HalfLifeMath.h"
#include "Render/Models/Skeleton.h"
#include "Render/ModelVertex.h"
#include "Render/TextureSlot.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/CharacterSlots.h"
#include "Scenes/GameData.h"
#include "Scenes/SceneContext.h"
#include "Scenes/SceneId.h"
#include "Scenes/TextureSets.h"

#include "support/Bytes.h"
#include "support/CharacterPackets.h"
#include "support/FakeTextureStore.h"
#include "support/RecordingFrameTarget.h"
#include "support/RecordingSink.h"
#include "support/SceneDataFiles.h"
#include "support/SameBytes.h"
#include "support/ScenePrinters.h"
#include "support/SessionHarness.h"
#include "support/TestGameServer.h"
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
#include <string_view>
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
    // Con `withWings`, las alas del equipo en 0 (0x1a12) tienen un esqueleto de un hueso que sube 10
    // de una key a la otra, y se dibujan.
    explicit FlatModels(bool withWings = false) : m_withWings(withWings)
    {
        SkeletonBone wingBone;
        wingBone.parent = -1;
        BoneKey low;
        low.rotation = Client::Render::Models::AngleQuaternion({0.0f, 0.0f, 0.0f});
        BoneKey high = low;
        high.position = {0.0f, 0.0f, 10.0f};
        wingBone.actions = {{low, high}};
        m_wings.bones = {wingBone};
        m_wings.actions = {ActionInfo{.repeats = true, .speed = 0.0f, .keyCount = 2, .lockPositions = false}};
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

    // Sin la sombra (0x20b), que tiene sus tests en CharacterDrawingTest: así se cuentan las partes.
    [[nodiscard]] const Model* Find(std::uint16_t model) const override
    {
        return model == 0x20b ? nullptr : &m_part;
    }
    [[nodiscard]] const Skeleton* SkeletonOf(std::uint16_t model) const override
    {
        if (model == 0x20a)
        {
            return &m_body;
        }
        return m_withWings && model == 0x1a12 ? &m_wings : nullptr;
    }
    [[nodiscard]] std::span<const TextureSlot> MeshTextures(std::uint16_t /*model*/) const override
    {
        return {};
    }

private:
    bool m_withWings;
    Skeleton m_body;
    Skeleton m_wings;
    Model m_part;
};

// Dos personajes sintéticos en los lugares 0 y 3; el 3 bloqueado si `locked`.
std::vector<CharacterEntryFields> TwoCharacters(bool locked = false)
{
    return {{0, "char0001"}, {3, "char0002", 1, static_cast<std::uint8_t>(locked ? 0x01 : 0x00)}};
}

struct Harness
{
    explicit Harness(bool withSession = true, bool withWings = false)
        : log(sink, Client::Core::LogLevel::Debug), models(withWings)
    {
        Client::Tests::WriteTextureSet(folder.Path(), SelectionTextureSet);
        Client::Tests::WriteTextureSet(folder.Path(), WindowTextureSet);
        Client::Tests::WriteTextTable(folder.Path(), {{402, "no conecta"},
                                                      {434, "bloqueado"},
                                                      {401, "codigo invalido"},
                                                      {417, "cuenta bloqueada"},
                                                      {439, "char bloqueado"},
                                                      {436, "cuenta del ADM"},
                                                      {437, "el sitio"},
                                                      {471, "espere"},
                                                      {540, "Synthetic A"},
                                                      {559, "Synthetic B"},
                                                      {1654, "del gremio"},
                                                      {1711, "nivel alto"},
                                                      {1712, "borrar %s"},
                                                      {1713, "el codigo"},
                                                      {1714, "borrado"}});
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
        ASSERT_NO_FATAL_FAILURE(ReachTheChoiceWith(TwoCharacters(locked)));
    }

    void ReachTheChoiceWith(const std::vector<CharacterEntryFields>& characters)
    {
        ASSERT_NO_FATAL_FAILURE(Enter());
        online.ReceiveTheList(characters);
        ASSERT_EQ(online.session.State(), SessionState::ChoosingCharacter);
        Step();
    }

    // Un paso con `bytes` del GameServer, en claro.
    void Receive(const std::vector<std::byte>& bytes)
    {
        online.socket.reads = {bytes};
        Step();
    }

    void Tap(Key key)
    {
        Client::Tests::TapKey(input, key);
        Step();
    }

    // Los F3 02 que mandó el cliente, sin el XOR32.
    [[nodiscard]] std::vector<Client::Network::Packet> DeletesSent() const
    {
        Client::Network::PacketSerials serials;
        std::vector<Client::Network::Packet> deletes;
        for (const Client::Network::Packet& packet :
             Client::Tests::AsTheGameServerReceives(online.socket.sent, serials).packets)
        {
            if (packet.Opcode() != 0xF3)
            {
                continue;
            }
            Client::Network::Packet clear = Client::Network::Crypto::Xor32Decode(packet);
            if (clear.Bytes().size() > 3 && clear.Bytes()[3] == std::byte{0x02})
            {
                deletes.push_back(std::move(clear));
            }
        }
        return deletes;
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

// La barra de la selección (0x63ec00): el adorno, la franja sin textura y los cuatro botones.
void ExpectTheBar(const std::vector<FrameEvent>& events, std::size_t first)
{
    ASSERT_GE(events.size(), first + 6);
    EXPECT_EQ(events[first].texture, std::optional<TextureSlot>{TextureSlot{0xe}});
    EXPECT_EQ(events[first + 1].texture, std::nullopt);
    for (std::size_t k = 0; k < 4; ++k)
    {
        EXPECT_EQ(events[first + 2 + k].texture,
                  std::optional<TextureSlot>{TextureSlot{static_cast<std::uint16_t>(0xf + k)}})
            << k;
    }
}

// Los fondos, el fin del 2D, el pase del mundo y los modelos, en el orden de 0x634680, y la UI arriba:
// la barra (0x65fe00).
TEST(CharacterSelectSceneTest, DrawsInTheOrderOfTheOriginal)
{
    Harness tested;
    ASSERT_NO_FATAL_FAILURE(tested.ReachTheChoice());

    const std::vector<FrameEvent> events = tested.Frame();

    ASSERT_EQ(events.size(), 11u);
    EXPECT_EQ(events[0].kind, Kind::Quad);
    EXPECT_EQ(events[0].texture, std::optional<TextureSlot>{TextureSlot{0x717}});
    EXPECT_EQ(events[1].texture, std::optional<TextureSlot>{TextureSlot{0x718}});
    EXPECT_EQ(events[2].kind, Kind::Flush);
    EXPECT_EQ(events[3].kind, Kind::World);
    EXPECT_EQ(events[4].kind, Kind::Models);
    ExpectTheBar(events, 5);
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

// La luz de un personaje en la selección (06 § El color del personaje), con el mouse afuera para
// que nadie lleve el contorno: la de cada lugar, los 18 primeros vértices del lugar 0 y los otros
// del 3.
std::pair<float, float> LightsAfterChoosing(std::uint8_t slot)
{
    Harness tested;
    EXPECT_NO_FATAL_FAILURE(tested.ReachTheChoice());
    tested.ClickAt(tested.OnScreen(slot));
    Client::Tests::MoveMouse(tested.input, 5, 5);
    tested.Step();
    const std::vector<FrameEvent> events = tested.Frame();
    const auto models = std::ranges::find(events, Kind::Models, &FrameEvent::kind);
    EXPECT_NE(models, events.end());
    EXPECT_EQ(models->modelVertices.size(), 36u);
    return {models->modelVertices[0].color.red, models->modelVertices[18].color.red};
}

// 0x50ba30: la base es 0,4 más +0xf4, y el elegido lleva +0xf4 en 1 (0x634680).
TEST(SelectLightTest, ChosenIsBrighter)
{
    const auto [slot0, slot3] = LightsAfterChoosing(3);

    EXPECT_FLOAT_EQ(slot3, 1.4f);
    EXPECT_FLOAT_EQ(slot0, 0.5f);
}

// El personaje del lugar 0 es el "héroe" (DAT_0743a170, el objeto 0 desde el arranque): en la escena
// 4, 0x50ba30 le da 0,5 más +0xf4 (0x50d219), y la traza lo muestra: 1,5 con él elegido.
TEST(SelectLightTest, TheFirstSlotIsTheHero)
{
    const auto [slot0, slot3] = LightsAfterChoosing(0);

    EXPECT_FLOAT_EQ(slot0, 1.5f);
    EXPECT_FLOAT_EQ(slot3, 0.4f);
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

// Si el frame trae la caja de un aviso (0x727, de las ventanas).
bool ShowsANotice(const std::vector<FrameEvent>& events)
{
    return std::ranges::any_of(events, [](const FrameEvent& event)
                               { return event.texture == std::optional<TextureSlot>{TextureSlot{0x727}}; });
}

// Q-C70: en la selección (el modo 4 de la UI), el FD_CLOSE abre el aviso 0x70, como en el login, y
// su OK cierra el juego (Q-C69). La escena no vuelve al login.
TEST(CharacterSelectSceneTest, AFailedSessionShowsTheConnectNoticeAndItsOkQuits)
{
    Harness tested;
    ASSERT_NO_FATAL_FAILURE(tested.ReachTheChoice());

    tested.online.socket.peerClosed = true;
    tested.Step();

    ASSERT_EQ(tested.online.session.State(), SessionState::Failed);
    EXPECT_TRUE(ShowsANotice(tested.Frame()));
    EXPECT_EQ(tested.scene->Next(), std::nullopt);
    EXPECT_FALSE(tested.scene->WantsToQuit());
    Client::Tests::TapKey(tested.input, Key::Enter);
    tested.Step();
    EXPECT_TRUE(tested.scene->WantsToQuit());
}

// Esperando la lista, lo mismo: el aviso se dibuja aunque la escena todavía no dibuje nada más.
TEST(CharacterSelectSceneTest, AFailureBeforeTheListShowsTheNoticeToo)
{
    Harness tested;
    ASSERT_NO_FATAL_FAILURE(tested.Enter());

    tested.online.socket.peerClosed = true;
    tested.Step();

    ASSERT_EQ(tested.online.session.State(), SessionState::Failed);
    EXPECT_TRUE(ShowsANotice(tested.Frame()));
    EXPECT_EQ(tested.scene->Next(), std::nullopt);
}

// Los vértices del segundo pase del contorno, en (0,7; 0,2; 0), y el primero de ellos.
std::vector<Client::Render::ModelVertex> OutlineVertices(const std::vector<FrameEvent>& events)
{
    std::vector<Client::Render::ModelVertex> outline;
    const auto models = std::ranges::find(events, Kind::Models, &FrameEvent::kind);
    if (models == events.end())
    {
        return outline;
    }
    for (const Client::Render::ModelVertex& vertex : models->modelVertices)
    {
        if (vertex.color.red == 0.7f && vertex.color.green == 0.2f && vertex.color.blue == 0.0f)
        {
            outline.push_back(vertex);
        }
    }
    return outline;
}

// El personaje bajo el mouse lleva el contorno (0x510d00 con DAT_006c8e30): las seis mallas de un
// triángulo, en su lugar; el de al lado no, y sin el mouse encima, nadie.
TEST(CharacterSelectSceneTest, TheOneUnderTheMouseIsOutlined)
{
    Harness tested;
    ASSERT_NO_FATAL_FAILURE(tested.ReachTheChoice());
    const glm::vec2 point = tested.OnScreen(3);

    Client::Tests::MoveMouse(tested.input, static_cast<int>(point.x), static_cast<int>(point.y));
    tested.Step();
    const std::vector<Client::Render::ModelVertex> outline = OutlineVertices(tested.Frame());

    ASSERT_EQ(outline.size(), 6u * 3u);
    EXPECT_NEAR(outline[0].position.x, CharacterSlots[3].position.x, 1e-2f);
    EXPECT_NEAR(outline[0].position.y, CharacterSlots[3].position.y, 1e-2f);
    Client::Tests::MoveMouse(tested.input, 5, 5);
    tested.Step();
    EXPECT_TRUE(OutlineVertices(tested.Frame()).empty());
}

// Con el aviso abierto, el mouse es del aviso (Q-C84): nadie lleva el contorno.
TEST(CharacterSelectSceneTest, WithTheNoticeOpenNobodyIsOutlined)
{
    Harness tested;
    ASSERT_NO_FATAL_FAILURE(tested.ReachTheChoice(true));
    const glm::vec2 point = tested.OnScreen(3);
    tested.ClickAt(point);
    tested.PressEnter();

    tested.Step();

    EXPECT_TRUE(OutlineVertices(tested.Frame()).empty());
}

// 0x50ba30 avanza las alas (0x509340 con +0x308: 0,25 keys por frame del original, 1 en las acciones
// 0x1e y 0x1f), y la selección las avanza por tiempo, como la mascota: las alas del lugar 0 (la
// séptima malla, después de las cinco partes y la cabeza) suben.
TEST(CharacterSelectSceneTest, TheWingsMove)
{
    Harness tested(true, true);
    ASSERT_NO_FATAL_FAILURE(tested.ReachTheChoice());
    const auto wingZ = [&tested]
    {
        const std::vector<FrameEvent> events = tested.Frame();
        const auto models = std::ranges::find(events, Kind::Models, &FrameEvent::kind);
        EXPECT_NE(models, events.end());
        EXPECT_EQ(models->modelVertices.size(), 2u * 7u * 3u);
        return models->modelVertices[6u * 3u].position.z;
    };
    const float before = wingZ();

    for (int step = 0; step < 10; ++step)
    {
        tested.Step();
    }

    EXPECT_GT(wingZ(), before);
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

    ASSERT_EQ(events.size(), 11u);
    EXPECT_EQ(events[0].texture, std::optional<TextureSlot>{TextureSlot{0x717}});
    EXPECT_TRUE(events[4].modelVertices.empty());
    ExpectTheBar(events, 5);
}

// Los textos que dibuja el frame, en orden.
std::vector<std::string> TextsOf(const std::vector<FrameEvent>& events)
{
    std::vector<std::string> texts;
    for (const FrameEvent& event : events)
    {
        if (event.kind == Kind::Text)
        {
            texts.push_back(event.text.text);
        }
    }
    return texts;
}

bool Draws(const std::vector<FrameEvent>& events, std::string_view text)
{
    const std::vector<std::string> texts = TextsOf(events);
    return std::ranges::find(texts, text) != texts.end();
}

// Los botones de la barra en 640 × 480: crear en (22, 399), conectar en (509, 399) y borrar en
// (564, 399), de 54 × 30.
constexpr glm::vec2 CreateButton{30.0f, 410.0f};
constexpr glm::vec2 ConnectButton{515.0f, 410.0f};
constexpr glm::vec2 DeleteButton{570.0f, 410.0f};

// La barra no es modal, pero tapa su rectángulo (+0x8de8, 06 § La lógica): un clic encima no elige
// ni saca la elección, y nadie lleva el contorno.
TEST(CharacterSelectSceneTest, TheBarBlocksThePicking)
{
    Harness tested;
    ASSERT_NO_FATAL_FAILURE(tested.ReachTheChoice());
    tested.ClickAt(tested.OnScreen(3));
    ASSERT_NE(tested.online.session.ChosenCharacter(), nullptr);

    tested.ClickAt({300.0f, 420.0f});

    ASSERT_NE(tested.online.session.ChosenCharacter(), nullptr);
    EXPECT_EQ(tested.online.session.ChosenCharacter()->slot, 3);
    EXPECT_TRUE(OutlineVertices(tested.Frame()).empty());
    // Afuera de la barra, el mismo clic sobre nadie saca la elección.
    tested.ClickAt({300.0f, 390.0f});
    EXPECT_EQ(tested.online.session.ChosenCharacter(), nullptr);
}

// El armado de la barra (0x63e820) mira las banderas de la lista una vez: con el bit 0x10 en alguno,
// los textos 436 y 437; sin él, no.
TEST(CharacterSelectSceneTest, ABlockedAccountShowsItsTexts)
{
    Harness blocked;
    ASSERT_NO_FATAL_FAILURE(blocked.ReachTheChoiceWith({{0, "char0001"}, {3, "char0002", 1, 0x10}}));
    const std::vector<FrameEvent> events = blocked.Frame();
    EXPECT_TRUE(Draws(events, "cuenta del ADM"));
    EXPECT_TRUE(Draws(events, "el sitio"));

    Harness free;
    ASSERT_NO_FATAL_FAILURE(free.ReachTheChoiceWith({{0, "char0001"}, {3, "char0002", 1, 0x0f}}));
    EXPECT_FALSE(Draws(free.Frame(), "cuenta del ADM"));
}

// Conectar es lo de Enter (0x633c10): sin elegido está deshabilitado; con uno, entra.
TEST(CharacterSelectSceneTest, TheConnectButtonEnters)
{
    Harness tested;
    ASSERT_NO_FATAL_FAILURE(tested.ReachTheChoice());
    tested.ClickAt(ConnectButton);
    EXPECT_EQ(tested.scene->Next(), std::nullopt);

    tested.ClickAt(tested.OnScreen(3));
    tested.ClickAt(ConnectButton);

    EXPECT_EQ(tested.scene->Next(), std::optional{SceneId::WorldLoading});
}

// La v de arriba del botón de crear en el frame: el cuadro 3 (deshabilitado) baja la textura.
float CreateButtonTop(const std::vector<FrameEvent>& events)
{
    const auto create = std::ranges::find(events, std::optional<TextureSlot>{TextureSlot{0xf}}, &FrameEvent::texture);
    EXPECT_NE(create, events.end());
    if (create == events.end())
    {
        return 0.0f;
    }
    return std::ranges::min(create->vertices, {}, &Client::Render::SpriteVertex::v).v;
}

// 0x63eb40: crear, con un lugar libre de los cinco. Con los cinco, el cuadro deshabilitado.
TEST(CharacterSelectSceneTest, CreateIsDisabledWithFiveCharacters)
{
    Harness two;
    ASSERT_NO_FATAL_FAILURE(two.ReachTheChoice());
    two.Step();
    Harness five;
    ASSERT_NO_FATAL_FAILURE(
        five.ReachTheChoiceWith({{0, "char0001"}, {1, "char0002"}, {2, "char0003"}, {3, "char0004"}, {4, "char0005"}}));
    five.Step();

    EXPECT_GT(CreateButtonTop(five.Frame()), CreateButtonTop(two.Frame()));
}

// 0x63edf0: uno del gremio no se borra (el 0x92), y el aviso se cierra con su OK.
TEST(CharacterSelectSceneTest, AGuildMemberIsNotDeleted)
{
    Harness tested;
    ASSERT_NO_FATAL_FAILURE(tested.ReachTheChoiceWith({{0, "char0001"}, {3, "char0002", 1, 0, 0, {}, 0x80}}));
    tested.ClickAt(tested.OnScreen(3));

    tested.ClickAt(DeleteButton);

    EXPECT_TRUE(Draws(tested.Frame(), "del gremio"));
    tested.Tap(Key::Enter);
    EXPECT_FALSE(ShowsANotice(tested.Frame()));
    EXPECT_TRUE(tested.DeletesSent().empty());
}

// De nivel 80 o más, en un grupo que no se llama como el texto 559, el 0x83. El servidor elegido
// del arnés es el 400, del grupo 20 (el texto 1829, que falta: vacío).
TEST(CharacterSelectSceneTest, AHighLevelOneOutsideTheFreeGroupIsNotDeleted)
{
    Harness tested;
    ASSERT_NO_FATAL_FAILURE(tested.ReachTheChoiceWith({{0, "char0001"}, {3, "char0002", 80}}));
    tested.ClickAt(tested.OnScreen(3));

    tested.ClickAt(DeleteButton);

    EXPECT_TRUE(Draws(tested.Frame(), "nivel alto"));
}

// El borrado entero: la confirmación con el nombre (0x5e), el código (0x5f, solo dígitos y con el
// campo de texto), el F3 02 con el nombre y el código y el "espere" (0x5d), y con la respuesta 1, el
// 0x60: el personaje se va y nadie queda elegido.
TEST(CharacterSelectSceneTest, ADeletionAsksTheCodeAndSendsIt)
{
    Harness tested;
    ASSERT_NO_FATAL_FAILURE(tested.ReachTheChoice());
    tested.ClickAt(tested.OnScreen(3));

    tested.ClickAt(DeleteButton);
    EXPECT_TRUE(Draws(tested.Frame(), "borrar char0002"));
    EXPECT_FALSE(tested.scene->WantsTextInput());
    tested.Tap(Key::Enter);
    EXPECT_TRUE(Draws(tested.Frame(), "el codigo"));
    EXPECT_TRUE(tested.scene->WantsTextInput());
    Client::Tests::TypeText(tested.input, "12a34");
    tested.Step();
    tested.Tap(Key::Enter);

    EXPECT_EQ(tested.online.session.State(), SessionState::WaitingDeleteResult);
    const std::vector<Client::Network::Packet> deletes = tested.DeletesSent();
    ASSERT_EQ(deletes.size(), 1u);
    const auto expected = Client::Protocol::Character::WriteCharacterDelete(
        Client::Tests::NameBytes("char0002"), Client::Protocol::Character::DeleteCodeFromText("1234"));
    EXPECT_TRUE(
        Client::Tests::SameBytes(deletes[0].Bytes(), Client::Network::Crypto::Xor32Decode(expected.packet).Bytes()));
    EXPECT_TRUE(Draws(tested.Frame(), "espere"));
    EXPECT_FALSE(tested.scene->WantsTextInput());

    tested.Receive(Client::Tests::Bytes({0xC1, 0x05, 0xF3, 0x02, 0x01}));

    const std::vector<FrameEvent> events = tested.Frame();
    EXPECT_TRUE(Draws(events, "borrado"));
    EXPECT_EQ(tested.online.session.ChosenCharacter(), nullptr);
    const auto models = std::ranges::find(events, Kind::Models, &FrameEvent::kind);
    ASSERT_NE(models, events.end());
    ASSERT_EQ(models->modelVertices.size(), 6u * 3u);
    EXPECT_NEAR(models->modelVertices[0].position.x, CharacterSlots[0].position.x, 1e-3f);
    tested.Tap(Key::Enter);
    EXPECT_FALSE(ShowsANotice(tested.Frame()));
}

// Las respuestas que no borran: 0 → 0x92, 3 → 0x62 y otra → 0x89 (0x4e2fe9). El personaje queda.
TEST(CharacterSelectSceneTest, ARefusedDeletionShowsItsNotice)
{
    for (const auto& [result, text] : {std::pair{std::uint8_t{0}, std::string_view{"del gremio"}},
                                       std::pair{std::uint8_t{3}, std::string_view{"char bloqueado"}},
                                       std::pair{std::uint8_t{4}, std::string_view{"codigo invalido"}}})
    {
        Harness tested;
        ASSERT_NO_FATAL_FAILURE(tested.ReachTheChoice());
        tested.ClickAt(tested.OnScreen(3));
        tested.ClickAt(DeleteButton);
        tested.Tap(Key::Enter);
        tested.Tap(Key::Enter);

        tested.Receive(Client::Tests::Bytes({0xC1, 0x05, 0xF3, 0x02, result}));

        const std::vector<FrameEvent> events = tested.Frame();
        EXPECT_TRUE(Draws(events, text)) << static_cast<int>(result);
        ASSERT_NE(tested.online.session.ChosenCharacter(), nullptr);
        EXPECT_EQ(tested.online.session.ChosenCharacter()->slot, 3);
        const auto models = std::ranges::find(events, Kind::Models, &FrameEvent::kind);
        ASSERT_NE(models, events.end());
        EXPECT_EQ(models->modelVertices.size(), 2u * 6u * 3u);
    }
}

// Esc en la confirmación o en el código cierra sin mandar nada (0x6436a0).
TEST(CharacterSelectSceneTest, CancellingTheDeletionSendsNothing)
{
    Harness tested;
    ASSERT_NO_FATAL_FAILURE(tested.ReachTheChoice());
    tested.ClickAt(tested.OnScreen(3));
    tested.ClickAt(DeleteButton);
    tested.Tap(Key::Escape);
    EXPECT_FALSE(ShowsANotice(tested.Frame()));

    tested.ClickAt(DeleteButton);
    tested.Tap(Key::Enter);
    tested.Tap(Key::Escape);

    EXPECT_FALSE(ShowsANotice(tested.Frame()));
    EXPECT_FALSE(tested.scene->WantsTextInput());
    EXPECT_TRUE(tested.DeletesSent().empty());
    EXPECT_EQ(tested.online.session.State(), SessionState::ChoosingCharacter);
}

// El que creó la sesión (F3 01 con 1) aparece en su lugar, con las partes de su clase y sin equipo
// (Q-C103), y los otros siguen con su animación.
TEST(CharacterSelectSceneTest, ACreatedOneAppearsInItsSlot)
{
    Harness tested;
    ASSERT_NO_FATAL_FAILURE(tested.ReachTheChoice());
    tested.online.session.CreateCharacter("char0003", 1);
    std::vector<std::byte> created = Client::Tests::Bytes({0xC1, 0x13, 0xF3, 0x01, 0x01});
    const auto name = Client::Tests::NameBytes("char0003");
    created.insert(created.end(), name.begin(), name.end());
    for (const std::uint8_t value : {std::uint8_t{1}, std::uint8_t{1}, std::uint8_t{0}, std::uint8_t{0x20}})
    {
        created.push_back(std::byte{value});
    }

    tested.Receive(created);

    const std::vector<FrameEvent> events = tested.Frame();
    const auto models = std::ranges::find(events, Kind::Models, &FrameEvent::kind);
    ASSERT_NE(models, events.end());
    // Las cinco partes de la clase, sin la cabeza (que va solo con los cascos 0x1012 y otros), de un
    // triángulo cada una.
    ASSERT_EQ(models->modelVertices.size(), 2u * 6u * 3u + 5u * 3u);
    const bool inSlotOne =
        std::ranges::any_of(models->modelVertices,
                            [](const Client::Render::ModelVertex& vertex)
                            {
                                return std::abs(vertex.position.x - CharacterSlots[1].position.x) < 1e-3f &&
                                       std::abs(vertex.position.y - CharacterSlots[1].position.y) < 1e-3f;
                            });
    EXPECT_TRUE(inSlotOne);
    EXPECT_TRUE(
        std::ranges::any_of(tested.sink.lines, [](const std::string& line)
                            { return line.find("la selección tiene ahora 3 personajes") != std::string::npos; }));
}
} // namespace
