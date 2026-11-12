#include "Scenes/CharacterSelectScene.h"

#include "App/SessionState.h"
#include "App/WindowState.h"
#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Data/Models/Model.h"
#include "Data/Models/PlayerModels.h"
#include "Data/Text/FilterWords.h"
#include "Input/InputState.h"
#include "Input/Key.h"
#include "Network/Crypto/Xor32.h"
#include "Network/Packet.h"
#include "Network/PacketSerials.h"
#include "Protocol/Character/CharacterCreate.h"
#include "Protocol/Character/CharacterDelete.h"
#include "Render/Models/HalfLifeMath.h"
#include "Render/Models/Skeleton.h"
#include "Render/ModelVertex.h"
#include "Render/Pass3D.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/TextureSlot.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/CharacterSlots.h"
#include "Scenes/CreatePreview.h"
#include "Scenes/DarkSpirit.h"
#include "Scenes/HelperTable.h"
#include "World/Particles.h"
#include "Data/Terrain/TerrainGrid.h"
#include "World/ParticleOwner.h"
#include "World/SelectionParticleKernels.h"
#include "Scenes/GameData.h"
#include "Scenes/SceneCameras.h"
#include "Scenes/SceneContext.h"
#include "Scenes/SceneId.h"
#include "Scenes/SelectionState.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "Scenes/TextureMetadata.h"
#include "Scenes/TextureSets.h"
#include "UI/CreateCharacterWindow.h"
#include "World/GameCamera.h"
#include "World/TerrainCulling.h"
#include "World/WorldTerrain.h"

#include "support/Bytes.h"
#include "support/CharacterPackets.h"
#include "support/EffectTextures.h"
#include "support/FakeClock.h"
#include "support/FakeTextureStore.h"
#include "support/GateFiles.h"
#include "support/ItemFiles.h"
#include "support/ItemSetFiles.h"
#include "support/GoldenDir.h"
#include "support/RecordingFrameTarget.h"
#include "support/RecordingSink.h"
#include "support/PlayerActors.h"
#include "support/SceneDataFiles.h"
#include "support/SkillFiles.h"
#include "support/SameBytes.h"
#include "support/ScenePrinters.h"
#include "support/SessionHarness.h"
#include "support/SyntheticModels.h"
#include "support/TestGameServer.h"
#include "support/TemporaryFolder.h"
#include "support/UiInput.h"

#include <gtest/gtest.h>

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/geometric.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
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
using Client::App::PixelSize;
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
        if (m_withRaven && model == Client::Scenes::DarkSpiritModel)
        {
            return &m_body;
        }
        return m_withWings && model == 0x1a12 ? &m_wings : nullptr;
    }
    [[nodiscard]] std::span<const TextureSlot> MeshTextures(std::uint16_t /*model*/) const override
    {
        return {};
    }
    // Las texturas de efectos de GameModels: las piden las partículas y los sprites del mundo 54.
    [[nodiscard]] std::optional<Client::Scenes::TextureMetadata>
    TextureMetadataOf(std::uint32_t selector) const override
    {
        return Client::Tests::EffectTextureMetadata(selector);
    }
    [[nodiscard]] std::optional<TextureSlot> TextureSlotOf(std::uint32_t selector) const override
    {
        return Client::Tests::EffectTextureSlot(selector);
    }
    [[nodiscard]] std::optional<glm::vec2> TextureSize(TextureSlot slot) const override
    {
        return Client::Tests::IsEffectTexture(slot.value) ? std::optional(glm::vec2(32.0f, 32.0f)) : std::nullopt;
    }

public:
    void EnableRaven()
    {
        m_withRaven = true;
    }

private:
    bool m_withWings;
    bool m_withRaven = false;
    Skeleton m_body;
    Skeleton m_wings;
    Model m_part;
};

// Dos personajes sintéticos en los lugares 0 y 3; el 3 bloqueado si `locked`.
std::vector<CharacterEntryFields> TwoCharacters(bool locked = false)
{
    return {{0, "char0001"}, {3, "char0002", 1, static_cast<std::uint8_t>(locked ? 0x01 : 0x00)}};
}

void RetainLoginTerrain(Client::Scenes::SelectionState& selection)
{
    constexpr std::uint8_t LoginMap = 55;
    auto terrain = Client::World::LoadWorldTerrain(Client::Tests::RepoDataDir(), LoginMap);
    ASSERT_TRUE(terrain.HasValue()) << terrain.Error();
    selection.RetainTerrain(LoginMap, std::move(terrain).Value());
}

struct Harness
{
    explicit Harness(bool withSession = true, bool withWings = false, bool withTheMouse = false,
                     Client::Scenes::SelectionState* selectionState = nullptr,
                     Client::Scenes::WorldState* worldState = nullptr,
                     Client::Render::Models::ClothWorld* sharedCloth = nullptr, bool withItemTable = true)
        : log(sink, Client::Core::LogLevel::Debug), models(withWings), selection(selectionState),
          sharedRandom(sharedCloth)
    {
        Client::Tests::WriteTextureSet(folder.Path(), SelectionTextureSet);
        Client::Tests::WriteTextureSet(folder.Path(), WindowTextureSet);
        Client::Tests::WriteTextTable(
            folder.Path(),
            {{402, "no conecta"},     {434, "bloqueado"},      {401, "codigo invalido"}, {417, "cuenta bloqueada"},
             {439, "char bloqueado"}, {436, "cuenta del ADM"}, {437, "el sitio"},        {471, "espere"},
             {540, "Synthetic A"},    {559, "Synthetic B"},    {1654, "del gremio"},     {1711, "nivel alto"},
             {1712, "borrar %s"},     {1713, "el codigo"},     {1714, "borrado"},        {390, "corto"},
             {391, "simbolos"},       {457, "Synthetic Op"},   {458, "Synthetic Srv"},   {396, "limite"},
             {1715, "filtrado"},      {1716, "rechazado"}});
        Client::Tests::WriteFilterFile(folder.Path(), {"GM"}, Client::Data::Text::FilterNameChecksumKey,
                                       "Local/FilterName.bmd");
        Client::Tests::WriteSceneFile(folder.Path(), "Gate.bmd", Client::Tests::GateTableBytes({}));
        Client::Tests::WriteSceneFile(folder.Path(), "Local/Item.bmd", Client::Tests::EmptyItemTableBytes());
        Client::Tests::WriteEmptyItemSetFiles(folder.Path());
        Client::Tests::WriteEmptySkillFile(folder.Path());
        auto loaded = Client::Scenes::LoadGameData(folder.Path());
        EXPECT_TRUE(loaded.HasValue());
        if (loaded.HasValue())
        {
            data = std::move(loaded).Value();
        }
        if (!withItemTable)
        {
            data.items.reset();
        }
        scene.emplace(
            SceneContext{.dataRoot = selection != nullptr ? Client::Tests::RepoDataDir() : folder.Path(),
                         .textures = store,
                         .log = log,
                         .clock = selection != nullptr ? &online.clock : nullptr,
                         .cloth = sharedCloth,
                         .input = withTheMouse ? &input : nullptr,
                         .camera = selection != nullptr || worldState != nullptr ? &camera : nullptr,
                         .cameraInput = selection != nullptr || worldState != nullptr ? &cameraInput : nullptr,
                         .selection = selection,
                         .world = worldState},
            data, models, withSession ? &online.session : nullptr);
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
        return CharacterPacketsSent(0x02);
    }

    // Los F3 de `subcode` que mandó el cliente, sin el XOR32.
    [[nodiscard]] std::vector<Client::Network::Packet> CharacterPacketsSent(std::uint8_t subcode) const
    {
        Client::Network::PacketSerials serials;
        std::vector<Client::Network::Packet> found;
        for (const Client::Network::Packet& packet :
             Client::Tests::AsTheGameServerReceives(online.socket.sent, serials).packets)
        {
            if (packet.Opcode() != 0xF3)
            {
                continue;
            }
            Client::Network::Packet clear = Client::Network::Crypto::Xor32Decode(packet);
            if (clear.Bytes().size() > 3 && clear.Bytes()[3] == std::byte{subcode})
            {
                found.push_back(std::move(clear));
            }
        }
        return found;
    }

    // La lista con las clases que se pueden crear hasta `creatable` (pkt[4]).
    void ReachTheChoiceWithClasses(std::uint8_t creatable)
    {
        ASSERT_NO_FATAL_FAILURE(Enter());
        const std::vector<CharacterEntryFields> two = TwoCharacters();
        online.socket.reads = {
            Client::Tests::CharacterListBytes(two, static_cast<std::uint8_t>(two.size()), creatable)};
        static_cast<void>(online.session.Poll());
        ASSERT_EQ(online.session.State(), SessionState::ChoosingCharacter);
        Step();
    }

    void Type(const std::string& text)
    {
        Client::Tests::TypeText(input, text);
        Step();
    }

    void Step()
    {
        ASSERT_NO_FATAL_FAILURE(Poll());
        scene->Step(input);
        input.ConsumeEdges();
        ++steps;
    }

    // El fixture de escena aplica las copias del collector; el pipeline síncrono real se prueba aparte.
    void Poll()
    {
        static_cast<void>(online.session.Poll());
        if (selection != nullptr)
        {
            const auto applied = selection->Apply(online.eventSink.TakeCharacterEvents(), camera, cameraInput,
                                                  sharedRandom, models.SkeletonOf(0x20a));
            ASSERT_TRUE(applied.HasValue()) << applied.Error();
        }
    }

    std::vector<FrameEvent> Frame(float alpha = 0.0f)
    {
        auto& canvas = target.BeginFrame(std::chrono::milliseconds{40} * steps);
        scene->Render(canvas, alpha);
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
    Client::World::GameCamera camera;
    Client::World::GameCameraInput cameraInput;
    Client::Scenes::SelectionState* selection;
    Client::Render::Models::ClothWorld* sharedRandom;
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

// 0x6342c0 y 0x634680 salen antes del 0x33: la escena no dibuja nada.
TEST(CharacterSelectSceneTest, DrawsNothingBeforeTheList)
{
    Harness tested;
    ASSERT_NO_FATAL_FAILURE(tested.Enter());
    tested.Step();

    EXPECT_TRUE(tested.Frame().empty());
}

// Y sin el verdadero de 0x634680, 0x637700 no presenta el frame (0x6385e1..0x6385f7): el clear no se ve y
// queda el último del login. La escena retiene el frame hasta que un paso vio la lista, como el login con
// el connect (Q-C85): la lista que llegó en un Poll sin paso todavía no puso a los personajes.
TEST(CharacterSelectSceneTest, HoldsTheLastFrameUntilAStepSawTheList)
{
    Harness tested;
    ASSERT_NO_FATAL_FAILURE(tested.Enter());
    EXPECT_TRUE(tested.scene->HoldsTheLastFrame());
    tested.Step();
    EXPECT_TRUE(tested.scene->HoldsTheLastFrame());

    tested.online.ReceiveTheList(TwoCharacters());
    ASSERT_EQ(tested.online.session.State(), SessionState::ChoosingCharacter);
    EXPECT_TRUE(tested.scene->HoldsTheLastFrame());
    tested.Step();
    EXPECT_FALSE(tested.scene->HoldsTheLastFrame());
}

// Con un aviso abierto antes de la lista, MuRet lo muestra (AFailureBeforeTheListShowsTheNoticeToo): el
// frame no se retiene. Sin sesión no hay lista que esperar.
TEST(CharacterSelectSceneTest, ANoticeOrNoSessionDoesNotHoldTheFrame)
{
    Harness failed;
    ASSERT_NO_FATAL_FAILURE(failed.Enter());
    failed.online.socket.peerClosed = true;
    failed.Step();
    ASSERT_EQ(failed.online.session.State(), SessionState::Failed);
    EXPECT_FALSE(failed.scene->HoldsTheLastFrame());

    Harness offline(false);
    ASSERT_TRUE(offline.scene->Enter().HasValue());
    EXPECT_FALSE(offline.scene->HoldsTheLastFrame());
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

// 0x634980: después de la UI (0x65fe00), el cursor del juego en el mouse del WndProc, con la mezcla
// normal y en blanco. Antes de la lista, 0x634680 sale antes de llegar ahí: sin cursor.
TEST(CharacterSelectSceneTest, DrawsTheGameCursorAfterTheUi)
{
    Harness tested(true, false, true);
    Client::Tests::MoveMouse(tested.input, 100, 200);
    ASSERT_NO_FATAL_FAILURE(tested.Enter());
    tested.Step();
    EXPECT_TRUE(std::ranges::none_of(tested.Frame(), [](const FrameEvent& event)
                                     { return event.texture == std::optional<TextureSlot>{TextureSlot{2}}; }));

    tested.online.ReceiveTheList(TwoCharacters());
    tested.Step();
    const std::vector<FrameEvent> events = tested.Frame();

    ASSERT_EQ(events.size(), 12u);
    ExpectTheBar(events, 5);
    const FrameEvent& cursor = events.back();
    EXPECT_EQ(cursor.texture, std::optional<TextureSlot>{TextureSlot{2}});
    EXPECT_EQ(cursor.blend, Client::Render::BlendMode::Alpha);
    EXPECT_TRUE(cursor.alphaTest);
    for (const auto& vertex : cursor.vertices)
    {
        EXPECT_EQ(vertex.color, Client::Render::Color{});
    }
    // (100, 200) de 640 × 480 es (100, 200) del WndProc: el quad va de x 98 a 122.
    EXPECT_EQ(cursor.vertices.front().x, 98.0f);
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

// Sin la lista y con el aviso abierto (la sesión cayó), el aviso lleva el cursor encima: el del sistema
// está escondido, y sin este no se vería dónde está el mouse.
TEST(CharacterSelectSceneTest, TheNoticeBeforeTheListHasTheGameCursor)
{
    Harness tested(true, false, true);
    Client::Tests::MoveMouse(tested.input, 100, 200);
    ASSERT_NO_FATAL_FAILURE(tested.Enter());
    tested.online.socket.peerClosed = true;
    tested.Step();
    ASSERT_EQ(tested.online.session.State(), SessionState::Failed);
    ASSERT_FALSE(tested.scene->HoldsTheLastFrame());

    const std::vector<FrameEvent> events = tested.Frame();

    ASSERT_TRUE(ShowsANotice(events));
    ASSERT_FALSE(events.empty());
    EXPECT_EQ(events.back().texture, std::optional<TextureSlot>{TextureSlot{2}});
    EXPECT_EQ(events.back().vertices.front().x, 98.0f);
}

// El 0x70 que se abre en un paso se atiende desde el siguiente: un Enter de ese paso no cierra el
// juego antes de que se vea el aviso; con la lista y antes de ella.
TEST(CharacterSelectSceneTest, TheFailureNoticeIsSeenEvenWithAnEnterInItsStep)
{
    for (const bool withTheList : {true, false})
    {
        Harness tested;
        if (withTheList)
        {
            ASSERT_NO_FATAL_FAILURE(tested.ReachTheChoice());
        }
        else
        {
            ASSERT_NO_FATAL_FAILURE(tested.Enter());
        }
        tested.online.socket.peerClosed = true;
        tested.Tap(Key::Enter);

        EXPECT_FALSE(tested.scene->WantsToQuit()) << withTheList;
        EXPECT_TRUE(ShowsANotice(tested.Frame())) << withTheList;
        tested.Tap(Key::Enter);
        EXPECT_TRUE(tested.scene->WantsToQuit()) << withTheList;
    }
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

// Con el aviso abierto, la UI tiene el mouse (+0x8de8) y 0x634680 no llama a 0x59ec80: el contorno
// queda en el último que estuvo bajo el mouse, aunque el mouse se vaya.
TEST(CharacterSelectSceneTest, WithTheNoticeOpenTheOutlineStaysWhereItWas)
{
    Harness tested;
    ASSERT_NO_FATAL_FAILURE(tested.ReachTheChoice(true));
    const glm::vec2 point = tested.OnScreen(3);
    tested.ClickAt(point);
    tested.PressEnter();
    ASSERT_TRUE(ShowsANotice(tested.Frame()));

    Client::Tests::MoveMouse(tested.input, 5, 5);
    tested.Step();

    const std::vector<Client::Render::ModelVertex> outline = OutlineVertices(tested.Frame());
    ASSERT_EQ(outline.size(), 6u * 3u);
    EXPECT_NEAR(outline[0].position.x, CharacterSlots[3].position.x, 1e-2f);
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

// La respuesta del F3 01 con 1: char0003 de clase 1 (0x20) y nivel 1, en `slot`.
std::vector<std::byte> CreatedBytes(std::uint8_t slot)
{
    std::vector<std::byte> created = Client::Tests::Bytes({0xC1, 0x13, 0xF3, 0x01, 0x01});
    const auto name = Client::Tests::NameBytes("char0003");
    created.insert(created.end(), name.begin(), name.end());
    for (const std::uint8_t value : {slot, std::uint8_t{1}, std::uint8_t{0}, std::uint8_t{0x20}})
    {
        created.push_back(std::byte{value});
    }
    return created;
}

// El quad del botón de conectar (0x11): con uno elegido y sin el mouse, el cuadro normal.
FrameEvent ConnectQuad(const std::vector<FrameEvent>& events)
{
    const auto connect = std::ranges::find(events, std::optional<TextureSlot>{TextureSlot{0x11}}, &FrameEvent::texture);
    EXPECT_NE(connect, events.end());
    return connect == events.end() ? FrameEvent{} : *connect;
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
// ni saca la elección (el contorno queda en el último, OverTheBarTheOutlineStaysWhereItWas).
TEST(CharacterSelectSceneTest, TheBarBlocksThePicking)
{
    Harness tested;
    ASSERT_NO_FATAL_FAILURE(tested.ReachTheChoice());
    tested.ClickAt(tested.OnScreen(3));
    ASSERT_NE(tested.online.session.ChosenCharacter(), nullptr);

    tested.ClickAt({300.0f, 420.0f});

    ASSERT_NE(tested.online.session.ChosenCharacter(), nullptr);
    EXPECT_EQ(tested.online.session.ChosenCharacter()->slot, 3);
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

// El trapecio que deja el dibujo de la selección: el de su punto fijo en el 54, con su cámara.
Client::World::TerrainPolygon SelectionPolygon()
{
    return Client::World::TerrainPolygonOf(Client::World::CullingScene::CharacterSelect, 54,
                                           Client::Scenes::SelectionCullingPoint,
                                           Client::Scenes::SelectionCamera().angles.z);
}

// 0x63475c: cada frame del original, después de la cámara (0x6346ca), 0x5fbb00 deja en los globales el trapecio
// del punto fijo de la selección (0x6346b5..0x6346c3), que no es ningún personaje, y la transición del nivel
// efectivo (0x5fb731) con el nivel que pidió la cámara, en 0. Un dibujo intermedio, al mismo reloj, no escribe.
TEST(CharacterSelectSceneTest, EachFrameLeavesTheTrapezoidOfItsFixedPoint)
{
    Client::Scenes::WorldState world;
    Client::Scenes::SelectionState selection(world);
    ASSERT_NO_FATAL_FAILURE(RetainLoginTerrain(selection));
    Harness tested(true, false, false, &selection, &world);
    ASSERT_NO_FATAL_FAILURE(tested.ReachTheChoice());
    world.Runtime().terrainPolygonState.effectiveLevel = 3;

    static_cast<void>(tested.Frame());

    const Client::World::TerrainPolygon expected = SelectionPolygon();
    EXPECT_EQ(world.Runtime().terrainPolygonState.effectiveLevel, 0);
    EXPECT_EQ(world.Runtime().terrainPolygon.x, expected.x);
    EXPECT_EQ(world.Runtime().terrainPolygon.y, expected.y);
    EXPECT_FALSE(world.Runtime().terrainPolygon.everything);

    world.Runtime().terrainPolygon = {};
    world.Runtime().terrainPolygonState.effectiveLevel = 3;
    static_cast<void>(tested.Frame());
    EXPECT_EQ(world.Runtime().terrainPolygon.x, (std::array<float, 4>{})) << "un dibujo intermedio no escribe";
    EXPECT_EQ(world.Runtime().terrainPolygonState.effectiveLevel, 3);
    EXPECT_EQ(tested.scene->Failure(), std::nullopt);
}

// 0x6342f9 recorre los 400 lugares, también un vivo que no vino en la lista de cinco.
TEST(CharacterSelectSceneTest, TheCommonStepVisitsLivingActorsOutsideTheListAndStopsAtTheirNativeCut)
{
    Client::Scenes::WorldState world;
    Client::Scenes::SelectionState selection(world);
    Client::Render::Models::ClothWorld cloth;
    ASSERT_NO_FATAL_FAILURE(RetainLoginTerrain(selection));
    Harness tested(true, false, false, &selection, &world, &cloth);
    ASSERT_NO_FATAL_FAILURE(tested.ReachTheChoiceWith({}));
    ASSERT_FALSE(tested.scene->Failure().has_value());
    auto& other = world.Actors()[7];
    other.alive = true;
    other.type = 0xf1;
    constexpr std::uint32_t Seed = 16;
    cloth.random = Client::Render::Models::CrtRandom(Seed);
    cloth.logicSteps = 77;
    const auto wind = cloth.wind;
    const float windBase = cloth.windBase;

    tested.Step();

    ASSERT_TRUE(tested.scene->Failure().has_value());
    EXPECT_NE(tested.scene->Failure()->find("0x4fb910"), std::string::npos) << *tested.scene->Failure();
    EXPECT_EQ(cloth.random.State(), Client::Tests::AfterDraws(Seed, 16).State());
    EXPECT_EQ(cloth.logicSteps, 77);
    EXPECT_EQ(cloth.wind, wind);
    EXPECT_FLOAT_EQ(cloth.windBase, windBase);
    const auto stopped = cloth.random.State();
    tested.Step();
    EXPECT_EQ(cloth.random.State(), stopped);
    EXPECT_TRUE(tested.Frame().empty());
}

TEST(CharacterSelectSceneTest, TheFirstNativeStepConstructsBothPreviewsAndResetsOnlyEightOwnLevels)
{
    Client::Scenes::WorldState world;
    Client::Scenes::SelectionState selection(world);
    Client::Render::Models::ClothWorld cloth;
    auto& own = world.OwnCharacter();
    own.byte6a = 3;
    own.words6c.fill(7);
    for (auto& item : own.equipment)
    {
        item.id = 9;
        item.scalar1Low = 31;
        item.flags1b = 17;
    }
    ASSERT_NO_FATAL_FAILURE(RetainLoginTerrain(selection));
    Harness tested(true, false, false, &selection, &world, &cloth);

    ASSERT_NO_FATAL_FAILURE(tested.ReachTheChoiceWith({}));

    ASSERT_FALSE(tested.scene->Failure().has_value());
    const auto& preview = tested.scene->PreviewActor();
    EXPECT_TRUE(preview.alive);
    EXPECT_EQ(preview.type, 0xa9u);
    EXPECT_EQ(preview.byte88, 0u);
    EXPECT_EQ(preview.revision, 2u);
    EXPECT_EQ(preview.stepStamp, 0) << "637f91 incrementa después del constructor";
    EXPECT_EQ(preview.animation.action, 1u);
    EXPECT_FLOAT_EQ(preview.animation.frame, 0.6f);
    EXPECT_EQ(own.byte6a, 0u);
    EXPECT_EQ(own.words6c, (std::array<std::uint32_t, 16>{}));
    for (std::size_t slot = 0; slot < own.equipment.size(); ++slot)
    {
        EXPECT_EQ(own.equipment[slot].id, 9u);
        EXPECT_EQ(own.equipment[slot].scalar1Low, slot < 8 ? 0u : 31u);
        EXPECT_EQ(own.equipment[slot].flags1b, 17u);
    }
    // Constructores56, propio3848, preview19, partículas2 y finalmente tela9; antes, los 65.536 del pasto
    // (0x5f8300) de la carga del 54 (0x61a560), que no crea objetos con rand().
    EXPECT_EQ(cloth.random.State(), Client::Tests::AfterDraws(1, 3934 + 65536).State());
}

TEST(CharacterSelectSceneTest, TheFirstSelectionHeroNeedsItemBmdBeforeAnyWorldEntry)
{
    Client::Scenes::WorldState world;
    Client::Scenes::SelectionState selection(world);
    Client::Render::Models::ClothWorld cloth;
    ASSERT_NO_FATAL_FAILURE(RetainLoginTerrain(selection));
    Harness tested(true, false, false, &selection, &world, &cloth, false);

    CharacterEntryFields hero;
    hero.equipment[4] = 3; // petNONE: esta prueba llega a las tablas del actor, no al helper6342f4.
    ASSERT_NO_FATAL_FAILURE(tested.ReachTheChoiceWith({hero}));

    ASSERT_TRUE(tested.scene->Failure().has_value());
    EXPECT_NE(tested.scene->Failure()->find("0x54efe0"), std::string::npos) << *tested.scene->Failure();
    EXPECT_EQ(world.OwnCharacter().equipment[7].id, 0u);
    EXPECT_FLOAT_EQ(tested.scene->PreviewActor().animation.frame, 0.0f) << "el corte precede 0x634303";
}

TEST(CharacterSelectSceneTest, TheLoadedItemTableLetsTheFirstHeroReachThePreviewStep)
{
    Client::Scenes::WorldState world;
    Client::Scenes::SelectionState selection(world);
    Client::Render::Models::ClothWorld cloth;
    ASSERT_NO_FATAL_FAILURE(RetainLoginTerrain(selection));
    Harness tested(true, false, false, &selection, &world, &cloth);

    CharacterEntryFields hero;
    hero.equipment[4] = 3;
    ASSERT_NO_FATAL_FAILURE(tested.ReachTheChoiceWith({hero}));

    ASSERT_FALSE(tested.scene->Failure().has_value());
    EXPECT_FLOAT_EQ(tested.scene->PreviewActor().animation.frame, 0.6f);
    EXPECT_EQ(world.OwnCharacter().equipment[7].id, 0u);
    EXPECT_TRUE(world.Actors()[0].seen);
}

TEST(CharacterSelectSceneTest, ThePreviewAdvancesClosedTwiceUnderItsWindowAndOnceUnderANotice)
{
    Client::Scenes::WorldState world;
    Client::Scenes::SelectionState selection(world);
    Client::Render::Models::ClothWorld cloth;
    ASSERT_NO_FATAL_FAILURE(RetainLoginTerrain(selection));
    Harness tested(true, true, false, &selection, &world, &cloth);
    ASSERT_NO_FATAL_FAILURE(tested.ReachTheChoiceWith({}));
    EXPECT_FLOAT_EQ(tested.scene->PreviewActor().animation.frame, 0.6f);
    tested.Step();
    EXPECT_FLOAT_EQ(tested.scene->PreviewActor().animation.frame, 1.2f);
    tested.ClickAt(CreateButton);
    const float opened = tested.scene->PreviewActor().animation.frame;
    tested.Step();
    EXPECT_FLOAT_EQ(tested.scene->PreviewActor().animation.frame, opened + 1.2f);
    tested.Type("abc");
    tested.Tap(Key::Enter);
    ASSERT_TRUE(Draws(tested.Frame(), "corto"));
    const float notice = tested.scene->PreviewActor().animation.frame;

    tested.Step();

    ASSERT_FALSE(tested.scene->Failure().has_value());
    EXPECT_FLOAT_EQ(tested.scene->PreviewActor().animation.frame, notice + 0.6f);
}

TEST(CharacterSelectSceneTest, ThePreviewHeadBelongsToTheDurableSelectionBeforeTheNextList)
{
    Client::Scenes::WorldState world;
    Client::Scenes::SelectionState selection(world);
    Client::Render::Models::ClothWorld cloth;
    selection.PreviewActor().headTurn.angles = {17.0f, 23.0f};
    selection.PreviewActor().headTurn.target = {61.0f, 83.0f};
    ASSERT_NO_FATAL_FAILURE(RetainLoginTerrain(selection));
    Harness tested(true, false, false, &selection, &world, &cloth);

    ASSERT_NO_FATAL_FAILURE(tested.Enter());

    EXPECT_EQ(&tested.scene->PreviewActor(), &selection.PreviewActor());
    EXPECT_EQ(tested.scene->PreviewActor().headTurn.angles, (glm::vec2{17.0f, 23.0f}));
    EXPECT_EQ(tested.scene->PreviewActor().headTurn.target, (glm::vec2{61.0f, 83.0f}));
}

TEST(CharacterSelectSceneTest, ThePreviewSortsBeforeBothGlobalWindsAndTheStepCounter)
{
    Client::Scenes::WorldState world;
    Client::Scenes::SelectionState selection(world);
    Client::Render::Models::ClothWorld cloth;
    ASSERT_NO_FATAL_FAILURE(RetainLoginTerrain(selection));
    Harness tested(true, false, false, &selection, &world, &cloth);
    ASSERT_NO_FATAL_FAILURE(tested.ReachTheChoiceWith({}));
    constexpr std::uint32_t Seed = 16;
    cloth.random = Client::Render::Models::CrtRandom(Seed);
    cloth.logicSteps = 41;
    auto& preview = selection.PreviewActor();
    preview.animation.action = 1;
    preview.animation.frame = 0;
    preview.headTurn = {};

    tested.Step();

    ASSERT_FALSE(tested.scene->Failure());
    // Rand18..20:26176,32288,30626. La cabeza sortea antes de ambos vientos.
    EXPECT_EQ(preview.headTurn.target, (glm::vec2{328.0f, 346.0f}));
    EXPECT_FLOAT_EQ(preview.headTurn.angles.x, 353.6f);
    EXPECT_FLOAT_EQ(preview.headTurn.angles.y, 357.2f);
    EXPECT_EQ(cloth.random.State(), Client::Tests::AfterDraws(Seed, 32).State());
    EXPECT_EQ(cloth.logicSteps, 42);
    EXPECT_NE(world.Particles().Wind().wind, glm::vec2(0.0f));
}

TEST(CharacterSelectSceneTest, RavenSparksAgeAfterThePreviewAndTheirSlotsAreReused)
{
    Client::Scenes::WorldState world;
    Client::Scenes::SelectionState selection(world);
    Client::Render::Models::ClothWorld cloth;
    ASSERT_NO_FATAL_FAILURE(RetainLoginTerrain(selection));
    Harness tested(true, false, false, &selection, &world, &cloth);
    tested.models.EnableRaven();
    CharacterEntryFields lord{.slot = 4, .classByte = 0x80};
    lord.equipment[1] = 5;
    lord.equipment[12] = 0xa0;
    lord.equipment[4] = 3;

    ASSERT_NO_FATAL_FAILURE(tested.ReachTheChoiceWith({lord}));

    ASSERT_FALSE(tested.scene->Failure());
    const auto particles = world.Particles().All();
    ASSERT_TRUE(particles[0].alive);
    EXPECT_EQ(particles[0].type, Client::World::SparkParticle);
    EXPECT_EQ(particles[0].life, 9) << "nace con10 y634315 la mueve en el mismo paso";
    world.Actors()[4].flags78 |= 0x1000;
    for (int step = 0; step < 12; ++step)
    {
        tested.Step();
        ASSERT_FALSE(tested.scene->Failure());
    }
    EXPECT_FALSE(world.Particles().Any());
    const auto reused = world.Particles().Create(Client::World::SparkParticle, {}, {}, {},
                                                 Client::World::RavenSparkSubtype, 1.0f, 0.0f, cloth.random);
    ASSERT_TRUE(reused.has_value());
    EXPECT_EQ(*reused, 0u);
}

TEST(CharacterSelectSceneTest, ASelectionHelperKeepsTheSceneFourAlphaPrefixBeforeTheMissingBody)
{
    Client::Scenes::WorldState world;
    Client::Scenes::SelectionState selection(world);
    Client::Render::Models::ClothWorld cloth;
    ASSERT_NO_FATAL_FAILURE(RetainLoginTerrain(selection));
    Harness tested(true, false, false, &selection, &world, &cloth);
    ASSERT_NO_FATAL_FAILURE(tested.ReachTheChoiceWith({}));
    auto& helpers = world.Helpers();
    helpers[0].horse.alpha = 0.5f;
    helpers[1].alive = true;
    helpers[1].type = 0xc6;
    helpers[1].rider = 0; // muerto: 44402a solo aplica ese guard en escena5.
    helpers[2].alive = true;
    helpers[2].horse.alpha = 0.25f;
    auto& spark = world.Particles().All()[0];
    spark.alive = true;
    spark.type = Client::World::SparkParticle;
    spark.subtype = Client::World::RavenSparkSubtype;
    spark.life = 7;
    constexpr std::uint32_t Seed = 16;
    cloth.random = Client::Render::Models::CrtRandom(Seed);
    cloth.logicSteps = 41;

    tested.Step();

    EXPECT_EQ(cloth.random.State(), Seed);
    EXPECT_EQ(cloth.logicSteps, 41);
    EXPECT_EQ(spark.life, 7);
    EXPECT_FLOAT_EQ(helpers[0].horse.alpha, 0.5f);
    EXPECT_FLOAT_EQ(helpers[1].horse.alpha, 0.1f);
    EXPECT_TRUE(helpers[1].alive);
    EXPECT_FLOAT_EQ(helpers[2].horse.alpha, 0.25f);
    ASSERT_TRUE(tested.scene->Failure());
    EXPECT_NE(tested.scene->Failure()->find("0x6342f4"), std::string::npos);
}

TEST(CharacterSelectSceneTest, AParticleBindingFailureKeepsItsPrefixAndSkipsClothAndLaterSlots)
{
    Client::Scenes::WorldState world;
    Client::Scenes::SelectionState selection(world);
    Client::Render::Models::ClothWorld cloth;
    ASSERT_NO_FATAL_FAILURE(RetainLoginTerrain(selection));
    Harness tested(true, false, false, &selection, &world, &cloth);
    ASSERT_NO_FATAL_FAILURE(tested.ReachTheChoiceWith({}));
    auto particles = world.Particles().All();
    for (std::size_t slot = 0; slot < 3; ++slot)
    {
        particles[slot].alive = true;
        particles[slot].type = Client::World::SparkParticle;
        particles[slot].subtype = Client::World::RavenSparkSubtype;
        particles[slot].life = 5;
    }
    particles[1].type = 0xbeef;
    particles[1].moves = true;
    particles[1].position = {1.0f, 2.0f, 3.0f};
    particles[1].velocity = {4.0f, 0.0f, 0.0f};
    selection.PreviewActor().animation.frame = 0;
    selection.PreviewActor().headTurn = {};
    constexpr std::uint32_t Seed = 16;
    cloth.random = Client::Render::Models::CrtRandom(Seed);
    cloth.logicSteps = 41;

    tested.Step();

    EXPECT_EQ(particles[0].life, 4);
    EXPECT_EQ(particles[1].life, 4);
    EXPECT_EQ(particles[1].position, (glm::vec3{5.0f, 2.0f, 3.0f}));
    EXPECT_EQ(particles[2].life, 5);
    EXPECT_EQ(cloth.random.State(), Client::Tests::AfterDraws(Seed, 23).State());
    EXPECT_EQ(cloth.logicSteps, 41);
    ASSERT_TRUE(tested.scene->Failure());
    EXPECT_NE(tested.scene->Failure()->find("0x634315"), std::string::npos);
}

// ADR 0009: si 0x5fb570 no puede con la D de la cámara (no entra en un int64), el frame se corta antes del pase
// del mundo, la escena queda en Failure con la dirección y los globales quedan como estaban.
TEST(CharacterSelectSceneTest, ATrapezoidErrorCutsTheFrame)
{
    Client::Scenes::WorldState world;
    Client::Scenes::SelectionState selection(world);
    ASSERT_NO_FATAL_FAILURE(RetainLoginTerrain(selection));
    Harness tested(true, false, false, &selection, &world);
    ASSERT_NO_FATAL_FAILURE(tested.ReachTheChoice());
    tested.camera.distance = std::numeric_limits<float>::quiet_NaN();
    world.Runtime().terrainPolygonState.effectiveLevel = 3;

    const std::vector<FrameEvent> events = tested.Frame();

    ASSERT_TRUE(tested.scene->Failure().has_value());
    EXPECT_NE(tested.scene->Failure()->find("0x5fb570"), std::string::npos) << *tested.scene->Failure();
    EXPECT_EQ(std::ranges::find(events, Kind::Models, &FrameEvent::kind), events.end());
    EXPECT_EQ(world.Runtime().terrainPolygon.x, (std::array<float, 4>{}));
    EXPECT_EQ(world.Runtime().terrainPolygonState.effectiveLevel, 3);
    EXPECT_TRUE(tested.Frame().empty()) << "cortada, no vuelve a dibujar";
}

// F3 02 desactiva la vista desde Poll: sin otro Step desaparece, pero el registro CPU sigue vivo.
TEST(CharacterSelectSceneTest, ADeletedOneDisappearsWhenPollIsFollowedByRenderWithoutStep)
{
    constexpr std::uint8_t DeletedSlot = 1;
    Client::Scenes::WorldState world;
    Client::Scenes::SelectionState selection(world);
    ASSERT_NO_FATAL_FAILURE(RetainLoginTerrain(selection));
    Harness tested(true, true, false, &selection);
    ASSERT_NO_FATAL_FAILURE(tested.ReachTheChoiceWith({{DeletedSlot, "char0001"}}));
    const auto before = tested.Frame();
    const auto visible = std::ranges::find(before, Kind::Models, &FrameEvent::kind);
    ASSERT_NE(visible, before.end());
    ASSERT_FALSE(visible->modelVertices.empty());
    const auto retained = selection.Slots()[DeletedSlot];
    tested.online.session.ChooseCharacter(DeletedSlot);
    tested.online.session.DeleteCharacter(Client::Protocol::Character::DeleteCodeFromText("1234"));
    tested.online.socket.reads = {Client::Tests::Bytes({0xc1, 0x05, 0xf3, 0x02, 0x01})};

    ASSERT_NO_FATAL_FAILURE(tested.Poll());

    ASSERT_EQ(tested.online.session.State(), SessionState::ChoosingCharacter);
    ASSERT_TRUE(tested.online.session.Characters()->characters.empty());
    EXPECT_EQ(selection.Slots()[DeletedSlot].revision, retained.revision);
    EXPECT_EQ(selection.Slots()[DeletedSlot].position, retained.position);
    const auto after = tested.Frame();
    const auto hidden = std::ranges::find(after, Kind::Models, &FrameEvent::kind);
    ASSERT_NE(hidden, after.end());
    EXPECT_TRUE(hidden->modelVertices.empty());
    EXPECT_EQ(selection.Slots()[DeletedSlot].revision, retained.revision);
    EXPECT_EQ(selection.Slots()[DeletedSlot].position, retained.position);
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
        // Los dos siguen: seis mallas cada uno, con el contorno (dos pases más) en el 3, el último
        // que estuvo bajo el mouse.
        const auto models = std::ranges::find(events, Kind::Models, &FrameEvent::kind);
        ASSERT_NE(models, events.end());
        EXPECT_EQ(models->modelVertices.size(), 2u * 6u * 3u + 2u * 6u * 3u);
        EXPECT_EQ(OutlineVertices(events).size(), 6u * 3u);
    }
}

// Esc en la confirmación o en el código cierra sin mandar nada (0x6436a0).
TEST(CharacterSelectSceneTest, CancellingTheDeletionSendsNothing)
{
    Harness tested;
    ASSERT_NO_FATAL_FAILURE(tested.ReachTheChoice());
    tested.ClickAt(tested.OnScreen(3));
    tested.ClickAt(DeleteButton);
    ASSERT_TRUE(Draws(tested.Frame(), "borrar char0002"));
    tested.Tap(Key::Escape);
    EXPECT_FALSE(ShowsANotice(tested.Frame()));

    tested.ClickAt(DeleteButton);
    tested.Tap(Key::Enter);
    ASSERT_TRUE(Draws(tested.Frame(), "el codigo"));
    ASSERT_TRUE(tested.scene->WantsTextInput());
    tested.Tap(Key::Escape);

    EXPECT_FALSE(ShowsANotice(tested.Frame()));
    EXPECT_FALSE(tested.scene->WantsTextInput());
    EXPECT_TRUE(tested.DeletesSent().empty());
    EXPECT_EQ(tested.online.session.State(), SessionState::ChoosingCharacter);
}

// El que creó la sesión (F3 01 con 1) aparece en su lugar, con las partes de su clase y sin equipo
// (Q-C103), y los otros siguen con su animación.
TEST(CharacterSelectSceneTest, ACreatedOneAppearsWhenPollIsFollowedByRenderWithoutStep)
{
    Harness tested;
    ASSERT_NO_FATAL_FAILURE(tested.ReachTheChoiceWith({}));
    tested.online.session.CreateCharacter("char0003", 0);
    tested.online.socket.reads = {CreatedBytes(1)};
    static_cast<void>(tested.online.session.Poll());
    ASSERT_EQ(tested.online.session.State(), SessionState::ChoosingCharacter);
    const auto events = tested.Frame();
    const auto models = std::ranges::find(events, Kind::Models, &FrameEvent::kind);
    ASSERT_NE(models, events.end());
    EXPECT_FALSE(models->modelVertices.empty());
}

// Dos F3 01 idénticos construyen dos veces: el segundo reinicia la pose aunque SameEntry dé igual.
// Los vértices del mundo del slot 1 permiten observarlo sin que Hero ni la cámara expliquen el cambio.
TEST(CharacterSelectSceneTest, AnIdenticalCreationRestartsItsVisualWhenPollIsFollowedByRenderWithoutStep)
{
    constexpr int SelectionMap = 54;
    constexpr std::uint8_t CreatedSlot = 1;
    constexpr int AnimationSteps = 5;
    constexpr auto StepTime = std::chrono::milliseconds{40};
    Client::Scenes::WorldState world;
    Client::Scenes::SelectionState selection(world);
    ASSERT_NO_FATAL_FAILURE(RetainLoginTerrain(selection));
    Harness tested(true, true, false, &selection);
    ASSERT_NO_FATAL_FAILURE(tested.ReachTheChoiceWith({}));
    ASSERT_TRUE(selection.IsInitialized());
    ASSERT_EQ(selection.Map(), SelectionMap);
    const auto createWithoutStep = [&tested]
    {
        tested.online.session.CreateCharacter("char0003", 1);
        tested.online.socket.reads = {CreatedBytes(CreatedSlot)};
        ASSERT_NO_FATAL_FAILURE(tested.Poll());
        ASSERT_EQ(tested.online.session.State(), SessionState::ChoosingCharacter);
        ASSERT_TRUE(tested.online.session.Characters().has_value());
        ASSERT_EQ(tested.online.session.Characters()->characters.size(), 1u);
        ASSERT_TRUE(tested.online.session.Characters()->characters.front().withoutEquipment);
    };
    const auto vertices = [&tested]
    {
        const auto events = tested.Frame();
        const auto models = std::ranges::find(events, Kind::Models, &FrameEvent::kind);
        EXPECT_NE(models, events.end());
        return models != events.end() ? models->modelVertices : std::vector<Client::Render::ModelVertex>{};
    };
    ASSERT_NO_FATAL_FAILURE(createWithoutStep());
    ASSERT_EQ(selection.Slots()[CreatedSlot].revision, 1u);
    const auto fresh = vertices();
    ASSERT_EQ(fresh.size(), 5u * 3u);
    for (int step = 0; step < AnimationSteps; ++step)
    {
        tested.online.clock.Advance(StepTime);
        tested.Step();
    }
    const auto advanced = vertices();
    ASSERT_EQ(advanced.size(), fresh.size());
    ASSERT_LT(advanced.front().position.z, fresh.front().position.z);

    ASSERT_NO_FATAL_FAILURE(createWithoutStep());
    ASSERT_EQ(selection.Slots()[CreatedSlot].revision, 2u);
    const auto restarted = vertices();
    ASSERT_EQ(restarted.size(), fresh.size());
    for (std::size_t vertex = 0; vertex < fresh.size(); ++vertex)
    {
        EXPECT_EQ(restarted[vertex].position, fresh[vertex].position) << vertex;
    }
}

TEST(CharacterSelectSceneTest, ADuplicatedSlotDrawsOnlyItsLastEntryAcrossRepeatedSyncs)
{
    const CharacterEntryFields first{.slot = 0, .name = "char0001"};
    const CharacterEntryFields last{.slot = 0, .name = "char0002", .classByte = 0x40};
    Harness duplicated;
    Harness single;
    ASSERT_NO_FATAL_FAILURE(duplicated.ReachTheChoiceWith({first, last}));
    ASSERT_NO_FATAL_FAILURE(single.ReachTheChoiceWith({last}));
    for (int frame = 0; frame < 2; ++frame)
    {
        const auto twice = duplicated.Frame();
        const auto once = single.Frame();
        const auto actual = std::ranges::find(twice, Kind::Models, &FrameEvent::kind);
        const auto expected = std::ranges::find(once, Kind::Models, &FrameEvent::kind);
        ASSERT_NE(actual, twice.end());
        ASSERT_NE(expected, once.end());
        ASSERT_EQ(actual->modelVertices.size(), expected->modelVertices.size());
        for (std::size_t vertex = 0; vertex < actual->modelVertices.size(); ++vertex)
        {
            EXPECT_EQ(actual->modelVertices[vertex].position, expected->modelVertices[vertex].position);
        }
    }
}

TEST(CharacterSelectSceneTest, ACreatedOneAppearsInItsSlot)
{
    Harness tested;
    ASSERT_NO_FATAL_FAILURE(tested.ReachTheChoice());
    tested.online.session.CreateCharacter("char0003", 1);

    tested.Receive(CreatedBytes(1));

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
    // Una vez: la línea es del cambio, no de cada paso.
    tested.Step();
    EXPECT_EQ(std::ranges::count_if(tested.sink.lines, [](const std::string& line)
                                    { return line.find("la selección tiene ahora") != std::string::npos; }),
              1);
    EXPECT_EQ(std::ranges::count_if(tested.sink.lines, [](const std::string& line)
                                    { return line.find("la selección tiene 2 personajes") != std::string::npos; }),
              1);
}
// La sesión termina con la respuesta del F3 02 y el cierre del GS en la misma lectura: el aviso 0x70
// de la falla queda arriba y su OK cierra el juego; la respuesta no lo tapa.
TEST(CharacterSelectSceneTest, AResultThatComesWithTheCloseLeavesTheFailureNotice)
{
    Harness tested;
    ASSERT_NO_FATAL_FAILURE(tested.ReachTheChoice());
    tested.ClickAt(tested.OnScreen(3));
    tested.ClickAt(DeleteButton);
    tested.Tap(Key::Enter);
    tested.Tap(Key::Enter);
    ASSERT_EQ(tested.online.session.State(), SessionState::WaitingDeleteResult);

    tested.online.socket.reads = {Client::Tests::Bytes({0xC1, 0x05, 0xF3, 0x02, 0x04})};
    tested.online.socket.peerClosed = true;
    tested.Step();

    ASSERT_EQ(tested.online.session.State(), SessionState::Failed);
    const std::vector<FrameEvent> events = tested.Frame();
    EXPECT_TRUE(Draws(events, "no conecta"));
    EXPECT_FALSE(Draws(events, "codigo invalido"));
    tested.Tap(Key::Enter);
    EXPECT_TRUE(tested.scene->WantsToQuit());
}

// Un aviso que se abre en un paso se atiende desde el siguiente: un Enter de ese paso no lo cierra
// antes de que se vea.
TEST(CharacterSelectSceneTest, TheResultNoticeIsSeenEvenWithAnEnterInItsStep)
{
    Harness tested;
    ASSERT_NO_FATAL_FAILURE(tested.ReachTheChoice());
    tested.ClickAt(tested.OnScreen(3));
    tested.ClickAt(DeleteButton);
    tested.Tap(Key::Enter);
    tested.Tap(Key::Enter);

    tested.online.socket.reads = {Client::Tests::Bytes({0xC1, 0x05, 0xF3, 0x02, 0x04})};
    tested.Tap(Key::Enter);

    EXPECT_TRUE(Draws(tested.Frame(), "codigo invalido"));
}

// 0x511290 deja a un personaje nuevo sin gremio (+0x368 en 0xff): el que se crea en la sesión se
// puede borrar, con la confirmación.
TEST(CharacterSelectSceneTest, ACreatedOneCanBeDeleted)
{
    Harness tested;
    ASSERT_NO_FATAL_FAILURE(tested.ReachTheChoice());
    tested.online.session.CreateCharacter("char0003", 1);
    tested.Receive(CreatedBytes(1));
    tested.ClickAt(tested.OnScreen(1));
    ASSERT_NE(tested.online.session.ChosenCharacter(), nullptr);
    ASSERT_EQ(tested.online.session.ChosenCharacter()->slot, 1);

    tested.ClickAt(DeleteButton);

    EXPECT_TRUE(Draws(tested.Frame(), "borrar char0003"));
}

// Los que siguen en la lista guardan su animación cuando entra otro: las alas del lugar 0 siguen
// donde iban, como en una selección en la que no pasó nada.
TEST(CharacterSelectSceneTest, TheOthersKeepTheirAnimation)
{
    const auto wingZ = [](Harness& tested)
    {
        const std::vector<FrameEvent> events = tested.Frame();
        const auto models = std::ranges::find(events, Kind::Models, &FrameEvent::kind);
        EXPECT_NE(models, events.end());
        return models == events.end() ? 0.0f : models->modelVertices[6u * 3u].position.z;
    };
    Harness quiet(true, true);
    ASSERT_NO_FATAL_FAILURE(quiet.ReachTheChoice());
    Harness busy(true, true);
    ASSERT_NO_FATAL_FAILURE(busy.ReachTheChoice());
    for (int step = 0; step < 5; ++step)
    {
        quiet.Step();
        busy.Step();
    }

    quiet.Step();
    busy.online.session.CreateCharacter("char0003", 1);
    busy.Receive(CreatedBytes(1));

    EXPECT_FLOAT_EQ(wingZ(busy), wingZ(quiet));
}

// El que se crea en un lugar ocupado lo pisa (Q-C103): sale con las partes de su clase, sin equipo.
TEST(CharacterSelectSceneTest, ACreatedOneReplacesTheOneInItsSlot)
{
    Harness tested;
    ASSERT_NO_FATAL_FAILURE(tested.ReachTheChoice());
    tested.online.session.CreateCharacter("char0003", 1);

    tested.Receive(CreatedBytes(3));

    const std::vector<FrameEvent> events = tested.Frame();
    const auto models = std::ranges::find(events, Kind::Models, &FrameEvent::kind);
    ASSERT_NE(models, events.end());
    EXPECT_EQ(models->modelVertices.size(), 6u * 3u + 5u * 3u);
}

// Con un aviso abierto, los botones de la barra no tienen el mouse (0x65fc30 se lo da al aviso): el
// de borrar, que se acaba de apretar, vuelve a su cuadro normal debajo de la confirmación.
TEST(CharacterSelectSceneTest, TheBarLetsGoOfTheMouseUnderANotice)
{
    Harness tested;
    ASSERT_NO_FATAL_FAILURE(tested.ReachTheChoice());
    tested.ClickAt(tested.OnScreen(3));
    tested.ClickAt(DeleteButton);
    tested.Step();

    const std::vector<FrameEvent> events = tested.Frame();
    const auto remove = std::ranges::find(events, std::optional<TextureSlot>{TextureSlot{0x12}}, &FrameEvent::texture);
    ASSERT_NE(remove, events.end());
    EXPECT_FLOAT_EQ(std::ranges::min(remove->vertices, {}, &Client::Render::SpriteVertex::v).v,
                    std::ranges::min(ConnectQuad(events).vertices, {}, &Client::Render::SpriteVertex::v).v);
}

// Con el mouse sobre la barra, el contorno se queda en el último (0x59ec80 no corre).
TEST(CharacterSelectSceneTest, OverTheBarTheOutlineStaysWhereItWas)
{
    Harness tested;
    ASSERT_NO_FATAL_FAILURE(tested.ReachTheChoice());
    const glm::vec2 point = tested.OnScreen(3);
    Client::Tests::MoveMouse(tested.input, static_cast<int>(point.x), static_cast<int>(point.y));
    tested.Step();

    Client::Tests::MoveMouse(tested.input, 300, 420);
    tested.Step();

    EXPECT_EQ(OutlineVertices(tested.Frame()).size(), 6u * 3u);
    Client::Tests::MoveMouse(tested.input, 300, 390);
    tested.Step();
    EXPECT_TRUE(OutlineVertices(tested.Frame()).empty());
}
// Si el frame trae la ventana de crear: el fondo del nombre, cha_id (0xc).
bool ShowsTheCreateWindow(const std::vector<FrameEvent>& events)
{
    return std::ranges::any_of(events, [](const FrameEvent& event)
                               { return event.texture == std::optional<TextureSlot>{TextureSlot{0xc}}; });
}

// El botón de crear de la barra abre la ventana (0x65f690(+0x8498)), arriba de la barra y con el
// campo del nombre, que tiene el teclado.
TEST(CharacterSelectSceneTest, TheCreateButtonOpensTheWindow)
{
    Harness tested;
    ASSERT_NO_FATAL_FAILURE(tested.ReachTheChoice());
    EXPECT_FALSE(ShowsTheCreateWindow(tested.Frame()));

    tested.ClickAt(CreateButton);

    const std::vector<FrameEvent> events = tested.Frame();
    EXPECT_TRUE(ShowsTheCreateWindow(events));
    EXPECT_TRUE(tested.scene->WantsTextInput());
    const auto bar = std::ranges::find(events, std::optional<TextureSlot>{TextureSlot{0xe}}, &FrameEvent::texture);
    const auto window = std::ranges::find(events, std::optional<TextureSlot>{TextureSlot{0xc}}, &FrameEvent::texture);
    EXPECT_LT(bar, window);
}

// 0x63d990 con un nombre que pasa: el F3 01 con el nombre y la clase (el Dark Knight, marcado
// aunque pkt[4] en 0 lo deje deshabilitado), la ventana se cierra y se abre el 0x5d. Con 1, el
// 0x5d se cierra y el nuevo entra.
TEST(CharacterSelectSceneTest, ACreationSendsF301AndTheNewOneEnters)
{
    Harness tested;
    ASSERT_NO_FATAL_FAILURE(tested.ReachTheChoice());
    tested.ClickAt(CreateButton);
    tested.Type("char0003");

    tested.Tap(Key::Enter);

    EXPECT_EQ(tested.online.session.State(), SessionState::WaitingCreateResult);
    const std::vector<Client::Network::Packet> creates = tested.CharacterPacketsSent(0x01);
    ASSERT_EQ(creates.size(), 1u);
    const auto expected = Client::Protocol::Character::WriteCharacterCreate(Client::Tests::NameBytes("char0003"), 1);
    EXPECT_TRUE(
        Client::Tests::SameBytes(creates[0].Bytes(), Client::Network::Crypto::Xor32Decode(expected.packet).Bytes()));
    std::vector<FrameEvent> events = tested.Frame();
    EXPECT_FALSE(ShowsTheCreateWindow(events));
    EXPECT_TRUE(Draws(events, "espere"));
    EXPECT_FALSE(tested.scene->WantsTextInput());

    tested.Receive(CreatedBytes(1));

    events = tested.Frame();
    EXPECT_FALSE(ShowsANotice(events));
    const auto models = std::ranges::find(events, Kind::Models, &FrameEvent::kind);
    ASSERT_NE(models, events.end());
    EXPECT_EQ(models->modelVertices.size(), 2u * 6u * 3u + 5u * 3u);
}

// Los chequeos del nombre (0x63d990): uno corto da el 0x79 y uno con una palabra de FilterName.bmd
// el 0x72, arriba de la ventana, que se queda con el nombre; el OK del aviso vuelve a ella, y no
// sale nada.
TEST(CharacterSelectSceneTest, ARefusedNameKeepsTheWindow)
{
    Harness tested;
    ASSERT_NO_FATAL_FAILURE(tested.ReachTheChoice());
    tested.ClickAt(CreateButton);
    tested.Type("abc");
    tested.Tap(Key::Enter);
    std::vector<FrameEvent> events = tested.Frame();
    EXPECT_TRUE(Draws(events, "corto"));
    EXPECT_TRUE(ShowsTheCreateWindow(events));

    tested.Tap(Key::Enter);
    events = tested.Frame();
    EXPECT_FALSE(ShowsANotice(events));
    EXPECT_TRUE(Draws(events, "abc"));
    tested.Type("GMx");
    tested.Tap(Key::Enter);
    EXPECT_TRUE(Draws(tested.Frame(), "filtrado"));

    EXPECT_TRUE(tested.CharacterPacketsSent(0x01).empty());
    EXPECT_EQ(tested.online.session.State(), SessionState::ChoosingCharacter);
}

// Con 0 o 2, la ventana vuelve a abrirse, vacía, con el 0x36 o el 0x37 arriba (0x4c2290).
TEST(CharacterSelectSceneTest, ARefusedCreationReopensTheWindow)
{
    for (const auto& [result, text] : {std::pair{std::uint8_t{0}, std::string_view{"rechazado"}},
                                       std::pair{std::uint8_t{2}, std::string_view{"limite"}}})
    {
        Harness tested;
        ASSERT_NO_FATAL_FAILURE(tested.ReachTheChoice());
        tested.ClickAt(CreateButton);
        tested.Type("char0003");
        tested.Tap(Key::Enter);

        tested.Receive(Client::Tests::Bytes({0xC1, 0x05, 0xF3, 0x01, result}));

        std::vector<FrameEvent> events = tested.Frame();
        EXPECT_TRUE(Draws(events, text)) << static_cast<int>(result);
        EXPECT_TRUE(ShowsTheCreateWindow(events));
        EXPECT_FALSE(Draws(events, "char0003"));
        tested.Tap(Key::Enter);
        events = tested.Frame();
        EXPECT_FALSE(ShowsANotice(events));
        EXPECT_TRUE(ShowsTheCreateWindow(events));
    }
}

// 0x63d7c0 con pkt[4]: con 2, la clase 2 se elige y la 4 no; el F3 01 lleva la elegida.
TEST(CharacterSelectSceneTest, TheClassesFollowTheList)
{
    Harness tested;
    ASSERT_NO_FATAL_FAILURE(tested.ReachTheChoiceWithClasses(2));
    tested.ClickAt(CreateButton);
    const Client::UI::CreateWindowLayout layout = Client::UI::LayoutCreateWindow(PixelSize{640, 480}, 2);
    tested.ClickAt({static_cast<float>(layout.classes[2].x + 5), static_cast<float>(layout.classes[2].y + 5)});
    tested.ClickAt({static_cast<float>(layout.classes[4].x + 5), static_cast<float>(layout.classes[4].y + 5)});
    tested.Type("char0003");

    tested.Tap(Key::Enter);

    const std::vector<Client::Network::Packet> creates = tested.CharacterPacketsSent(0x01);
    ASSERT_EQ(creates.size(), 1u);
    EXPECT_EQ(creates[0].Bytes().back(), std::byte{0x20});
}

// Con un aviso del nombre arriba, los botones de la ventana sueltan el mouse: el OK de la ventana,
// con el mouse encima cuando se apretó Enter, vuelve a su cuadro normal, como el OK del aviso.
TEST(CharacterSelectSceneTest, UnderANoticeTheWindowLetsGoOfTheMouse)
{
    Harness tested;
    ASSERT_NO_FATAL_FAILURE(tested.ReachTheChoice());
    tested.ClickAt(CreateButton);
    const Client::UI::CreateWindowLayout layout = Client::UI::LayoutCreateWindow(PixelSize{640, 480}, 1);
    Client::Tests::MoveMouse(tested.input, layout.ok.x + 5, layout.ok.y + 5);
    tested.Step();
    tested.Tap(Key::Enter);
    tested.Step();

    std::vector<float> tops;
    for (const FrameEvent& event : tested.Frame())
    {
        if (event.texture == std::optional<TextureSlot>{TextureSlot{0x724}})
        {
            tops.push_back(std::ranges::min(event.vertices, {}, &Client::Render::SpriteVertex::v).v);
        }
    }
    ASSERT_EQ(tops.size(), 2u);
    EXPECT_FLOAT_EQ(tops[0], tops[1]);
}

// Con la ventana abierta, Esc la cierra y Enter es de ella: no entra al mundo con el elegido.
TEST(CharacterSelectSceneTest, TheWindowTakesEnterAndEsc)
{
    Harness tested;
    ASSERT_NO_FATAL_FAILURE(tested.ReachTheChoice());
    tested.ClickAt(tested.OnScreen(3));
    tested.ClickAt(CreateButton);

    tested.Tap(Key::Enter);
    EXPECT_EQ(tested.scene->Next(), std::nullopt);
    EXPECT_TRUE(Draws(tested.Frame(), "corto"));
    tested.Tap(Key::Enter);
    tested.Tap(Key::Escape);

    EXPECT_FALSE(ShowsTheCreateWindow(tested.Frame()));
    EXPECT_FALSE(tested.scene->WantsTextInput());
    EXPECT_EQ(tested.scene->Next(), std::nullopt);
}
// La respuesta del F3 01 con 0 y el cierre del GS en la misma lectura: queda el 0x70 de la falla, y
// la ventana no se vuelve a abrir sobre una sesión caída.
TEST(CharacterSelectSceneTest, ACreationResultWithTheCloseLeavesTheFailureNotice)
{
    Harness tested;
    ASSERT_NO_FATAL_FAILURE(tested.ReachTheChoice());
    tested.ClickAt(CreateButton);
    tested.Type("char0003");
    tested.Tap(Key::Enter);
    ASSERT_EQ(tested.online.session.State(), SessionState::WaitingCreateResult);

    tested.online.socket.reads = {Client::Tests::Bytes({0xC1, 0x05, 0xF3, 0x01, 0x00})};
    tested.online.socket.peerClosed = true;
    tested.Step();

    const std::vector<FrameEvent> events = tested.Frame();
    EXPECT_TRUE(Draws(events, "no conecta"));
    EXPECT_FALSE(Draws(events, "rechazado"));
    EXPECT_FALSE(ShowsTheCreateWindow(events));
    tested.Tap(Key::Enter);
    EXPECT_TRUE(tested.scene->WantsToQuit());
}

// El aviso de la respuesta del F3 01 se atiende desde el paso que sigue: un Enter del paso en que
// llega no lo cierra antes de que se vea.
TEST(CharacterSelectSceneTest, TheCreationNoticeIsSeenEvenWithAnEnterInItsStep)
{
    Harness tested;
    ASSERT_NO_FATAL_FAILURE(tested.ReachTheChoice());
    tested.ClickAt(CreateButton);
    tested.Type("char0003");
    tested.Tap(Key::Enter);

    tested.online.socket.reads = {Client::Tests::Bytes({0xC1, 0x05, 0xF3, 0x01, 0x00})};
    tested.Tap(Key::Enter);

    EXPECT_TRUE(Draws(tested.Frame(), "rechazado"));
}

// Las dos listas van a su lugar: un nombre que empieza con '/' se saltea Filter.bmd, no FilterName.bmd.
TEST(CharacterSelectSceneTest, ASlashNameStillMeetsFilterName)
{
    Harness tested;
    ASSERT_NO_FATAL_FAILURE(tested.ReachTheChoice());
    tested.ClickAt(CreateButton);
    tested.Type("/GMxx");

    tested.Tap(Key::Enter);

    EXPECT_TRUE(Draws(tested.Frame(), "filtrado"));
    EXPECT_TRUE(tested.CharacterPacketsSent(0x01).empty());
}

// Q-C118: con un intento más largo antes, 0x447ca0 ve sus restos en el buffer: "abcde" detrás de
// "abcdef g" da el 0x7a. Y con un aviso arriba, lo que se escribe sigue yendo al nombre.
TEST(CharacterSelectSceneTest, TheBufferOfAnEarlierTryCounts)
{
    Harness tested;
    ASSERT_NO_FATAL_FAILURE(tested.ReachTheChoice());
    tested.ClickAt(CreateButton);
    tested.Type("abcdef g");
    tested.Tap(Key::Enter);
    ASSERT_TRUE(Draws(tested.Frame(), "filtrado"));
    tested.Type("\b\b\b");
    tested.Tap(Key::Enter);
    ASSERT_FALSE(ShowsANotice(tested.Frame()));
    ASSERT_TRUE(Draws(tested.Frame(), "abcde"));

    tested.Tap(Key::Enter);

    EXPECT_TRUE(Draws(tested.Frame(), "simbolos"));
    EXPECT_TRUE(tested.CharacterPacketsSent(0x01).empty());
}

// El nombre que se escribe no va al log, ni el que no pasa ni el que se manda: ni el de la escena
// ni el de la sesión.
TEST(CharacterSelectSceneTest, TheNameIsNotLogged)
{
    Harness tested;
    ASSERT_NO_FATAL_FAILURE(tested.ReachTheChoice());
    tested.ClickAt(CreateButton);
    tested.Type("xGMx");
    tested.Tap(Key::Enter);
    tested.Tap(Key::Enter);
    tested.Type("\b\b\b\bchar0009");
    tested.Tap(Key::Enter);
    ASSERT_EQ(tested.online.session.State(), SessionState::WaitingCreateResult);

    for (const std::vector<std::string>* lines : {&tested.sink.lines, &tested.online.sink.lines})
    {
        for (const std::string& line : *lines)
        {
            EXPECT_EQ(line.find("xGMx"), std::string::npos) << line;
            EXPECT_EQ(line.find("char0009"), std::string::npos) << line;
        }
    }
}
// Un NewFace de la clase `characterClass` en Logo\ (el modelo de los tests, con la textura de
// chasellight, que el juego de la selección ya escribió) para la vista previa.
void WriteNewFace(const Harness& tested, int characterClass)
{
    Client::Data::Models::Model face = Client::Tests::SampleModel();
    face.meshes[0].textureName = "chasellight.jpg";
    // Todos los vértices en la raíz: la malla se mueve entera, y sus aristas solo cambian con la
    // escala del objeto.
    for (auto& vertex : face.meshes[0].vertices)
    {
        vertex.bone = 0;
    }
    Client::Tests::WriteSceneFile(tested.folder.Path(), "Logo/NewFace0" + std::to_string(characterClass + 1) + ".bmd",
                                  Client::Tests::EncryptedBmd(Client::Tests::BmdContent(face)));
}

// Los Models del frame, en orden.
std::vector<FrameEvent> ModelPasses(const std::vector<FrameEvent>& events)
{
    std::vector<FrameEvent> passes;
    std::ranges::copy_if(events, std::back_inserter(passes),
                         [](const FrameEvent& event) { return event.kind == Kind::Models; });
    return passes;
}

// 0x63e460: con la ventana abierta, después de su base, un pase 3D propio con el viewport de 410 ×
// 335 desde la caja y la cámara de la vista previa, con el NewFace de la clase elegida; después, lo
// demás de la ventana.
TEST(CharacterSelectSceneTest, TheWindowDrawsThePreviewInItsOwnPass)
{
    Harness tested;
    ASSERT_NO_FATAL_FAILURE(WriteNewFace(tested, 1));
    ASSERT_NO_FATAL_FAILURE(tested.ReachTheChoice());
    EXPECT_EQ(ModelPasses(tested.Frame()).size(), 1u);

    tested.ClickAt(CreateButton);

    const std::vector<FrameEvent> events = tested.Frame();
    const std::vector<FrameEvent> passes = ModelPasses(events);
    ASSERT_EQ(passes.size(), 2u);
    const Client::Render::Pass3D expected =
        Client::Render::MakePass3D(PixelSize{640, 480},
                                   Client::Scenes::PreviewViewport(
                                       Client::UI::LayoutCreateWindow(PixelSize{640, 480}, 1).box, PixelSize{640, 480}),
                                   Client::Scenes::PreviewCamera());
    ASSERT_TRUE(passes[1].pass.has_value());
    EXPECT_EQ(passes[1].pass->viewport, expected.viewport);
    EXPECT_EQ(passes[1].pass->view, expected.view);
    EXPECT_FALSE(passes[1].modelVertices.empty());
    // La base de la ventana antes del pase, y el fondo del nombre después.
    const auto preview = std::ranges::find_if(
        events, [](const FrameEvent& event)
        { return event.kind == Kind::Models && !event.modelVertices.empty() && event.pass->viewport.z == 410; });
    const auto back = std::ranges::find(events, std::optional<TextureSlot>{TextureSlot{0xc}}, &FrameEvent::texture);
    // La base: el negro de toda la pantalla, sin textura (0x669c70(W, H, −1, 0)).
    const auto base = std::ranges::find_if(
        events, [](const FrameEvent& event)
        { return event.kind == Kind::Quad && !event.texture && event.vertices[0].color.alpha == 128.0f / 255.0f; });
    ASSERT_NE(preview, events.end());
    ASSERT_NE(base, events.end());
    EXPECT_LT(base, preview);
    EXPECT_LT(preview, back);
}

// La vista previa sigue a la clase de la ventana (0x63d7c0 → 0x63e3e0), con su escala: la del Elf es
// 9,1 y la del Dark Knight 6,05, en la misma pose (la acción 1 desde el principio).
TEST(CharacterSelectSceneTest, ThePreviewFollowsTheClass)
{
    Harness tested;
    ASSERT_NO_FATAL_FAILURE(WriteNewFace(tested, 1));
    ASSERT_NO_FATAL_FAILURE(WriteNewFace(tested, 2));
    ASSERT_NO_FATAL_FAILURE(tested.ReachTheChoiceWithClasses(2));
    tested.ClickAt(CreateButton);
    // El largo de la primera arista: no cambia con el giro, solo con la escala.
    const auto spread = [](const FrameEvent& pass)
    { return glm::length(pass.modelVertices[1].position - pass.modelVertices[0].position); };
    const std::vector<FrameEvent> knight = ModelPasses(tested.Frame());
    ASSERT_EQ(knight.size(), 2u);
    ASSERT_FALSE(knight[1].modelVertices.empty());
    const float knightSpread = spread(knight[1]);

    const Client::UI::CreateWindowLayout layout = Client::UI::LayoutCreateWindow(PixelSize{640, 480}, 2);
    tested.ClickAt({static_cast<float>(layout.classes[2].x + 5), static_cast<float>(layout.classes[2].y + 5)});

    const std::vector<FrameEvent> elf = ModelPasses(tested.Frame());
    ASSERT_EQ(elf.size(), 2u);
    ASSERT_FALSE(elf[1].modelVertices.empty());
    EXPECT_NEAR(spread(elf[1]) / knightSpread, 9.1f / 6.05f, 1e-3f);
}

// Leave suelta también los slots de la vista previa (0x61cc50, desde 0x6f8).
TEST(CharacterSelectSceneTest, LeaveReleasesThePreviewSlots)
{
    Harness tested;
    ASSERT_NO_FATAL_FAILURE(WriteNewFace(tested, 1));
    ASSERT_NO_FATAL_FAILURE(tested.Enter());

    tested.scene->Leave();

    EXPECT_NE(std::ranges::find(tested.store.released, std::uint16_t{0x6f8}), tested.store.released.end());
}

// Con un aviso arriba la vista previa no avanza, y se dibuja quieta: sin interpolar un paso que no
// va a dar, que la haría temblar.
TEST(CharacterSelectSceneTest, UnderANoticeThePreviewStaysStill)
{
    Harness tested;
    ASSERT_NO_FATAL_FAILURE(WriteNewFace(tested, 1));
    ASSERT_NO_FATAL_FAILURE(tested.ReachTheChoice());
    tested.ClickAt(CreateButton);
    tested.Step();
    // Con la ventana arriba, avanza, y el dibujo interpola entre dos pasos.
    const std::vector<FrameEvent> moving = ModelPasses(tested.Frame(0.0f));
    const std::vector<FrameEvent> moved = ModelPasses(tested.Frame(0.9f));
    ASSERT_EQ(moving.size(), 2u);
    ASSERT_EQ(moved.size(), 2u);
    ASSERT_FALSE(moving[1].modelVertices.empty());
    EXPECT_NE(moving[1].modelVertices[0].position, moved[1].modelVertices[0].position);
    tested.Type("abc");
    tested.Tap(Key::Enter);
    ASSERT_TRUE(Draws(tested.Frame(), "corto"));
    tested.Step();

    const std::vector<FrameEvent> early = ModelPasses(tested.Frame(0.0f));
    const std::vector<FrameEvent> late = ModelPasses(tested.Frame(0.9f));

    ASSERT_EQ(early.size(), 2u);
    ASSERT_EQ(late.size(), 2u);
    ASSERT_EQ(early[1].modelVertices.size(), late[1].modelVertices.size());
    for (std::size_t i = 0; i < early[1].modelVertices.size(); ++i)
    {
        EXPECT_EQ(early[1].modelVertices[i].position, late[1].modelVertices[i].position) << i;
    }
}

// La vista previa saluda desde la entrada (0x633f20 → … → 0x63e3e0 con el Dark Knight): un paso
// después de abrir la ventana, la misma pose que elegir otra clase y volver al Dark Knight, que la
// arman de nuevo con la acción 1 y la avanzan.
TEST(CharacterSelectSceneTest, ThePreviewGreetsFromTheStart)
{
    const auto firstVertex = [](Harness& tested)
    {
        const std::vector<FrameEvent> passes = ModelPasses(tested.Frame());
        EXPECT_EQ(passes.size(), 2u);
        return passes.size() == 2 && !passes[1].modelVertices.empty() ? passes[1].modelVertices[0].position
                                                                      : glm::vec3(0.0f);
    };
    const Client::UI::CreateWindowLayout layout = Client::UI::LayoutCreateWindow(PixelSize{640, 480}, 1);
    Harness opened;
    ASSERT_NO_FATAL_FAILURE(WriteNewFace(opened, 0));
    ASSERT_NO_FATAL_FAILURE(WriteNewFace(opened, 1));
    ASSERT_NO_FATAL_FAILURE(opened.ReachTheChoiceWithClasses(1));
    opened.ClickAt(CreateButton);
    opened.Step();
    Harness rechosen;
    ASSERT_NO_FATAL_FAILURE(WriteNewFace(rechosen, 0));
    ASSERT_NO_FATAL_FAILURE(WriteNewFace(rechosen, 1));
    ASSERT_NO_FATAL_FAILURE(rechosen.ReachTheChoiceWithClasses(1));
    rechosen.ClickAt(CreateButton);
    rechosen.ClickAt({static_cast<float>(layout.classes[0].x + 5), static_cast<float>(layout.classes[0].y + 5)});
    rechosen.ClickAt({static_cast<float>(layout.classes[1].x + 5), static_cast<float>(layout.classes[1].y + 5)});

    EXPECT_EQ(firstVertex(opened), firstVertex(rechosen));
}
} // namespace

namespace
{
// Dos personajes con las alas 0 (0x1a12) y sin mascota: sin el cuerpo del ayudante (0x444059).
std::vector<CharacterEntryFields> TwoWingedCharacters()
{
    auto characters = TwoCharacters();
    for (auto& character : characters)
        character.equipment[4] = 3;
    return characters;
}

// Lo que cuelga del personaje (las alas) va al buffer global en un frame del original y no en uno intermedio.
TEST(CharacterSelectSceneTest, SelectionPosesUseTheSharedWorldCarryOnlyOnNativeFrames)
{
    Client::Scenes::WorldState world;
    Client::Scenes::SelectionState selection(world);
    Client::Render::Models::ClothWorld cloth;
    ASSERT_NO_FATAL_FAILURE(RetainLoginTerrain(selection));
    Harness tested(true, true, false, &selection, &world, &cloth);
    ASSERT_NO_FATAL_FAILURE(tested.ReachTheChoiceWith(TwoWingedCharacters()));
    static_cast<void>(tested.Frame());
    ASSERT_TRUE(world.PoseCarry().models.contains(std::uint16_t{0x1a12}));
    ASSERT_FALSE(world.PoseCarry().bones.empty());
    world.PoseCarry().bones[0][3] = {7, 8, 9};
    static_cast<void>(tested.Frame());
    EXPECT_EQ(world.PoseCarry().bones[0][3], glm::vec3(7, 8, 9));
    EXPECT_EQ(tested.scene->Failure(), std::nullopt);
}

// Q-C410: la lista (0x4c20a0) arma cada personaje con 0x517270 → 0x511290, que le pone +0x11c en 1 (0x511bf7) y le
// da su buffer de huesos (+0x120); 0x6001b0 posa ahí el cuerpo (0x600259). El cuerpo no llega al buffer global
// (0x68b181c) ni en un frame del original ni en uno intermedio, y sólo el frame del original lo guarda en el propio.
// Las alas, que cuelgan, sí van al global.
TEST(CharacterSelectSceneTest, TheBodyGoesToItsOwnBonesAndNeverToTheGlobalPose)
{
    Client::Scenes::WorldState world;
    Client::Scenes::SelectionState selection(world);
    Client::Render::Models::ClothWorld cloth;
    ASSERT_NO_FATAL_FAILURE(RetainLoginTerrain(selection));
    Harness tested(true, true, false, &selection, &world, &cloth);
    ASSERT_NO_FATAL_FAILURE(tested.ReachTheChoiceWith(TwoWingedCharacters()));
    constexpr std::array<std::size_t, 2> Slots{0, 3};
    for (const std::size_t slot : Slots)
    {
        ASSERT_TRUE(world.Actors()[slot].seen);
        world.Actors()[slot].bodyPose.clear();
    }

    static_cast<void>(tested.Frame());

    EXPECT_FALSE(world.PoseCarry().models.contains(Client::Data::Models::PlayerSkeletonModel));
    EXPECT_TRUE(world.PoseCarry().models.contains(std::uint16_t{0x1a12}));
    // Q-C412: el último de la lista deja su escala y su posición en el modelo compartido de Player.bmd.
    EXPECT_EQ(world.PlayerModel().scale, world.Actors()[3].scale);
    EXPECT_EQ(world.PlayerModel().origin, world.Actors()[3].position);
    for (const std::size_t slot : Slots)
    {
        SCOPED_TRACE(slot);
        ASSERT_EQ(world.Actors()[slot].bodyPose.size(), 48u);
        world.Actors()[slot].bodyPose[0][3] = {7, 8, 9};
    }

    static_cast<void>(tested.Frame());

    EXPECT_FALSE(world.PoseCarry().models.contains(Client::Data::Models::PlayerSkeletonModel));
    for (const std::size_t slot : Slots)
        EXPECT_EQ(world.Actors()[slot].bodyPose[0][3], glm::vec3(7, 8, 9));
    EXPECT_EQ(tested.scene->Failure(), std::nullopt);
}

// Q-C410: 0x63e3e0 arma la vista previa con 0x511290 (0x63e409), que le pone +0x11c en 1 y le da su buffer
// (+0x120); 0x63e637 → 0x50ba30 → 0x602e90 → 0x5ffdf0 la posa ahí (0x5fff13..0x5fff4c). No llega al global, y un
// dibujo intermedio, que interpola otra pose, no toca el propio.
TEST(CharacterSelectSceneTest, ThePreviewGoesToItsOwnBonesAndNeverToTheGlobalPose)
{
    Client::Scenes::WorldState world;
    Client::Scenes::SelectionState selection(world);
    Client::Render::Models::ClothWorld cloth;
    ASSERT_NO_FATAL_FAILURE(RetainLoginTerrain(selection));
    Harness tested(true, false, false, &selection, &world, &cloth);
    ASSERT_NO_FATAL_FAILURE(WriteNewFace(tested, 1));
    ASSERT_NO_FATAL_FAILURE(tested.ReachTheChoiceWith({}));
    tested.ClickAt(CreateButton);
    Client::Scenes::ActorState& preview = selection.PreviewActor();
    preview.bodyPose.clear();

    const std::vector<FrameEvent> passes = ModelPasses(tested.Frame());

    const auto drawn = std::ranges::find_if(passes, [](const FrameEvent& pass)
                                            { return pass.pass.has_value() && pass.pass->viewport.z == 410; });
    ASSERT_NE(drawn, passes.end());
    EXPECT_FALSE(drawn->modelVertices.empty());
    EXPECT_FALSE(world.PoseCarry().models.contains(preview.type));
    ASSERT_FALSE(preview.bodyPose.empty());
    preview.bodyPose[0][3] = {7, 8, 9};

    static_cast<void>(tested.Frame(0.9f));

    EXPECT_EQ(preview.bodyPose[0][3], glm::vec3(7, 8, 9));
    EXPECT_FALSE(world.PoseCarry().models.contains(preview.type));
    EXPECT_EQ(tested.scene->Failure(), std::nullopt);
}

// 0x6347e9..0x6348dc: con el mundo 54, el elegido vivo lleva su luz a 1 y la suma al buffer dinámico en su
// lugar (0x5f9780, radio 1) antes del terreno; después de los personajes, su luz queda en 0,15 y presta las
// dos 0x729 (/0 y /1) de escala 1 y sin dueño. Un dibujo intermedio de MuRet (sin frame del original) no
// cambia el pool del paso.
TEST(CharacterSelectSceneTest, TheChosenOneGlowsAndRaisesItsTwoChaseLightsOnNativeFrames)
{
    Client::Scenes::WorldState world;
    Client::Scenes::SelectionState selection(world);
    Client::Render::Models::ClothWorld cloth;
    ASSERT_NO_FATAL_FAILURE(RetainLoginTerrain(selection));
    Harness tested(true, false, false, &selection, &world, &cloth);
    auto characters = TwoCharacters();
    for (auto& character : characters)
        character.equipment[4] = 3; // Sin mascota: sin el cuerpo del ayudante (0x444059).
    ASSERT_NO_FATAL_FAILURE(tested.ReachTheChoiceWith(characters));
    tested.online.session.ChooseCharacter(std::uint8_t{3});
    ASSERT_NE(tested.online.session.ChosenCharacter(), nullptr);
    tested.Step();
    ASSERT_FALSE(tested.scene->Failure().has_value()) << *tested.scene->Failure();
    ASSERT_EQ(world.Runtime().map, 54);

    static_cast<void>(tested.Frame());

    ASSERT_FALSE(tested.scene->Failure().has_value()) << *tested.scene->Failure();
    const auto& chosen = world.Actors()[3];
    EXPECT_EQ(chosen.light, glm::vec3(std::bit_cast<float>(0x3e19999au)));
    EXPECT_EQ(world.Actors()[0].light, glm::vec3(0.0f));
    const auto chaseLights = [&world]
    {
        std::vector<const Client::World::Particle*> found;
        for (const auto& particle : world.Particles().All())
        {
            if (particle.alive && particle.type == Client::World::ChaseLightParticle)
                found.push_back(&particle);
        }
        return found;
    };
    const auto lights = chaseLights();
    ASSERT_EQ(lights.size(), 2u);
    EXPECT_EQ(lights[0]->subtype, 0);
    EXPECT_EQ(lights[1]->subtype, 1);
    EXPECT_EQ(lights[1]->texture, Client::World::ChaseLightImpactTexture);
    for (const auto* light : lights)
    {
        EXPECT_EQ(light->light, glm::vec3(std::bit_cast<float>(0x3e19999au)));
        EXPECT_EQ(light->typedOwner, Client::World::ParticleOwner{});
    }
    const auto cellX = static_cast<std::uint8_t>(chosen.position.x / 100.0f);
    const auto cellY = static_cast<std::uint8_t>(chosen.position.y / 100.0f);
    const auto& lit = world.DynamicLighting().Lighting().cells[Client::Data::Terrain::CellIndex(cellX, cellY)];
    EXPECT_GT(lit[0], 0.0f);

    // El mismo reloj: un dibujo intermedio sobre las copias.
    static_cast<void>(tested.Frame());
    ASSERT_FALSE(tested.scene->Failure().has_value()) << *tested.scene->Failure();
    EXPECT_EQ(chaseLights().size(), 2u);
}
} // namespace
