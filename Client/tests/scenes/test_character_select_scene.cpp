#include "Network/Xor32Packet.h"
#include "Models/IModelLibrary.h"
#include "Scenes/CharacterSelectScene.h"

#include "Session/SessionState.h"
#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Core/PixelSize.h"
#include "Crt/CrtRandom.h"
#include "Data/Models/Bmd.h"
#include "Data/Models/Model.h"
#include "Tables/PlayerModels.h"
#include "Tables/FilterWords.h"
#include "Input/InputState.h"
#include "Input/Key.h"
#include "Network/Packet.h"
#include "Network/PacketSerials.h"
#include "Protocol/FrontEnd/CharacterCreate.h"
#include "Protocol/FrontEnd/CharacterDelete.h"
#include "Math/HalfLifeMath.h"
#include "Models/LogicalAccess.h"
#include "Models/Animation.h"
#include "Models/Skeleton.h"
#include "Models/Skinning.h"
#include "Render/ModelVertex.h"
#include "Render/Pass3D.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/TextureSlot.h"
#include "FrontEnd/CharacterSlots.h"
#include "FrontEnd/CreatePreview.h"
#include "Pets/DarkSpirit.h"
#include "Pets/HelperTable.h"
#include "Particles/Particles.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Particles/ParticleOwner.h"
#include "Particles/SelectionParticleKernels.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Tables/GameData.h"
#include "Camera/SceneCameras.h"
#include "Scenes/SceneContext.h"
#include "Kernel/SceneId.h"
#include "Pets/PetHelperSteps.h"
#include "FrontEnd/SelectionState.h"
#include "Kernel/OwnInventory.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Session/ModuleStates.h"
#include "Session/WorldEntry.h"
#include "Protocol/Outgoing.h"
#include "Models/TextureMetadata.h"
#include "Models/ModelId.h"
#include "Assets/TextureSets.h"
#include "FrontEnd/CreateCharacterWindow.h"
#include "FrontEnd/EscapeWindow.h"
#include "Ui/MessageArea.h"
#include "FrontEnd/OptionsWindow.h"
#include "Ui/Widget.h"
#include "Camera/GameCamera.h"
#include "Terrain/TerrainCulling.h"
#include "Terrain/WorldTerrain.h"

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
#include "Chat/ChatRuntime.h"
#include "Chat/ChatInput.h"
#include "Social/SocialState.h"
#include "Ui/GameUiState.h"
#include "CastleSiege/CastleSiegeState.h"
#include "Ui/PanelRegistry.h"

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
#include <fstream>
#include <ios>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Client::Scenes::CharacterSelectScene: scene 4 with a ServerSession over a FakeSocket (support/SessionHarness.h), a
// one-triangle model library and the RecordingFrameTarget. The list is synthetic (support/CharacterPackets.h). Each
// character's draw against the trace is in GoldenCharacterDrawing.
namespace
{
using Client::Assets::SelectionTextureSet;
using Client::Assets::WindowTextureSet;
using Client::Core::PixelSize;
using Client::Data::Models::Mesh;
using Client::Data::Models::Model;
using Client::FrontEnd::CharacterSlotHeight;
using Client::FrontEnd::CharacterSlots;
using Client::Input::InputState;
using Client::Input::Key;
using Client::Kernel::SceneId;
using Client::Models::ActionInfo;
using Client::Models::BoneKey;
using Client::Models::IModelLibrary;
using Client::Models::Skeleton;
using Client::Models::SkeletonBone;
using Client::Render::TextureSlot;
using Client::Scenes::CharacterSelectScene;
using Client::Scenes::SceneContext;
using Client::Session::SessionState;
using Client::Tables::GameData;
using Client::Tests::CharacterEntryFields;
using Client::Tests::FakeTextureStore;
using Client::Tests::FrameEvent;
using Client::Tests::LoggedInSession;
using Client::Tests::RecordingFrameTarget;
using Client::Tests::TemporaryFolder;

using Kind = FrameEvent::Kind;

// Player.bmd with 48 bones and four actions of two equal keys, which advance one key per step: in the 3, the standing
// one, all at the origin; in the others, 100 higher. After the first step, the pose is the current action's, without
// blending with the previous one. And a one-triangle model, of bone 0, for any other number.
class FlatModels final : public IModelLibrary
{
public:
    // With `withWings`, the wings of equipment 0 (0x1a12) have a one-bone skeleton that rises 10 from one key to the
    // other, and are drawn.
    explicit FlatModels(bool withWings = false) : m_withWings(withWings)
    {
        SkeletonBone wingBone;
        wingBone.parent = -1;
        BoneKey low;
        low.rotation = Client::Math::AngleQuaternion({0.0f, 0.0f, 0.0f});
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
            key.rotation = Client::Math::AngleQuaternion({0.0f, 0.0f, 0.0f});
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

    // Without the shadow (0x20b), which has its tests in CharacterDrawingTest: so the parts can be counted.
    [[nodiscard]] const Model* Find(Client::Models::ModelId model) const override
    {
        return model.Value() == 0x20b ? nullptr : &m_part;
    }
    [[nodiscard]] const Skeleton* SkeletonOf(Client::Models::ModelId model) const override
    {
        if (model.Value() == 0x20a)
        {
            return &m_body;
        }
        if (m_withRaven && model.Value() == Client::Pets::DarkSpiritModel)
        {
            return &m_body;
        }
        if (m_withHelpers &&
            (model.Value() == Client::Pets::UniriaModel || model.Value() == Client::Pets::DinorantModel))
        {
            return &m_body;
        }
        return m_withWings && model.Value() == 0x1a12 ? &m_wings : nullptr;
    }
    // The wings, which hang, go with their texture: 0x60b3d0 (DrawGameHanging) asks for it. The body, without.
    [[nodiscard]] std::span<const TextureSlot> MeshTextures(Client::Models::ModelId model) const override
    {
        if (m_withWings && model.Value() == 0x1a12)
            return m_wingTextures;
        return {};
    }
    // GameModels' effect textures: the particles and the sprites of world 54 ask for them.
    [[nodiscard]] std::optional<Client::Models::TextureMetadata>
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

    // The body's skeleton also for the Uniria and the Dinorant (Skill\Rider01.bmd and Rider02.bmd).
    void EnableHelpers()
    {
        m_withHelpers = true;
    }

private:
    bool m_withWings;
    bool m_withRaven = false;
    bool m_withHelpers = false;
    std::array<TextureSlot, 1> m_wingTextures{TextureSlot{0x300}};
    Skeleton m_body;
    Skeleton m_wings;
    Model m_part;
};

// Two synthetic characters in slots 0 and 3; the 3 locked if `locked`.
std::vector<CharacterEntryFields> TwoCharacters(bool locked = false)
{
    return {{0, "char0001"}, {3, "char0002", 1, static_cast<std::uint8_t>(locked ? 0x01 : 0x00)}};
}

void RetainLoginTerrain(Client::FrontEnd::SelectionState& selection)
{
    constexpr std::uint8_t LoginMap = 55;
    auto terrain = Client::Terrain::LoadWorldTerrain(Client::Tests::RepoDataDir(), LoginMap);
    ASSERT_TRUE(terrain.HasValue()) << terrain.Error();
    selection.RetainTerrain(LoginMap, std::move(terrain).Value());
}

// Six pixels per byte: the tests' texts do not reach the 256 of 0x555190's cut.
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
    explicit Harness(bool withSession = true, bool withWings = false, bool withTheMouse = false,
                     Client::FrontEnd::SelectionState* selectionState = nullptr,
                     Client::Kernel::WorldState* worldState = nullptr,
                     Client::Models::ClothWorld* sharedCloth = nullptr, bool withItemTable = true,
                     const Client::Scenes::WorldEntryResources* worldEntry = nullptr)
        : log(sink, Client::Core::LogLevel::Debug), models(withWings), selection(selectionState),
          sharedRandom(sharedCloth)
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
                                                      {1714, "borrado"},
                                                      {390, "corto"},
                                                      {391, "simbolos"},
                                                      {457, "Synthetic Op"},
                                                      {458, "Synthetic Srv"},
                                                      {396, "limite"},
                                                      {1715, "filtrado"},
                                                      {1716, "rechazado"},
                                                      {1240, "General %d, Gladiador %d"}});
        Client::Tests::WriteFilterFile(folder.Path(), {"GM"}, Client::Tables::FilterNameChecksumKey,
                                       "Local/FilterName.bmd");
        Client::Tests::WriteSceneFile(folder.Path(), "Gate.bmd", Client::Tests::GateTableBytes({}));
        Client::Tests::WriteSceneFile(folder.Path(), "Local/Item.bmd", Client::Tests::EmptyItemTableBytes());
        Client::Tests::WriteEmptyItemSetFiles(folder.Path());
        Client::Tests::WriteEmptySkillFile(folder.Path());
        auto loaded = Client::Tables::LoadGameData(folder.Path());
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
                         .world = worldState,
                         .textMeasure = &letters,
                         .worldEntry = worldEntry,
                         .states = &states},
            data, models, withSession ? &online.session : nullptr);
    }

    // The login accepted and Enter: the scene asked for the list.
    void Enter()
    {
        ASSERT_NO_FATAL_FAILURE(online.LogIn());
        ASSERT_TRUE(scene->Enter().HasValue());
    }

    // And the list, and a step: the characters are in their places.
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

    // A step with `bytes` from the GameServer, in clear.
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

    // The F3 02 the client sent, without the XOR32.
    [[nodiscard]] std::vector<Client::Network::Packet> DeletesSent() const
    {
        return CharacterPacketsSent(0x02);
    }

    // The F3 of `subcode` the client sent, without the XOR32.
    [[nodiscard]] std::vector<Client::Network::Packet> CharacterPacketsSent(std::uint8_t subcode) const
    {
        Client::Network::PacketSerials serials;
        std::vector<Client::Network::Packet> found;
        for (const Client::Network::Packet& packet :
             Client::Tests::AsTheGameServerReceives(online.socket.sent, serials).packets)
        {
            if (packet.Opcode() != Client::Network::Opcode{0xF3})
            {
                continue;
            }
            Client::Network::Packet clear = Client::Network::Xor32Decode(packet);
            if (clear.Bytes().size() > 3 && clear.Bytes()[3] == std::byte{subcode})
            {
                found.push_back(std::move(clear));
            }
        }
        return found;
    }

    // The list with the classes that can be created up to `creatable` (pkt[4]).
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

    // The scene fixture applies the collector's copies; the real synchronous pipeline is tested apart.
    void Poll()
    {
        static_cast<void>(online.session.Poll());
        if (selection != nullptr)
        {
            const auto applied = selection->Apply(online.eventSink.TakeCharacterEvents(), camera, cameraInput,
                                                  sharedRandom, models.SkeletonOf(Client::Models::ModelId{0x20a}));
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

    // Where the chest of the character in slot `slot` lands on screen, with the last frame's pass.
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
    LetterMeasure letters;
    FakeTextureStore store;
    Client::Tests::RecordingSink sink;
    Client::Core::Log log;
    GameData data;
    FlatModels models;
    LoggedInSession online;
    Client::Camera::GameCamera camera;
    Client::Camera::GameCameraInput cameraInput;
    Client::FrontEnd::SelectionState* selection;
    Client::Models::ClothWorld* sharedRandom;
    RecordingFrameTarget target;
    InputState input;
    std::int64_t steps = 0;
    Client::Session::ModuleStates states;
    std::optional<CharacterSelectScene> scene;
};

// 0x634c60 sends the F3 00 on going to scene 4: here, the scene on entering.
TEST(CharacterSelectSceneTest, EnterRequestsTheList)
{
    Harness tested;

    ASSERT_NO_FATAL_FAILURE(tested.Enter());

    EXPECT_EQ(tested.online.session.State(), SessionState::WaitingCharacterList);
}

// 0x6342c0 and 0x634680 leave before the 0x33: the scene draws nothing.
TEST(CharacterSelectSceneTest, DrawsNothingBeforeTheList)
{
    Harness tested;
    ASSERT_NO_FATAL_FAILURE(tested.Enter());
    tested.Step();

    EXPECT_TRUE(tested.Frame().empty());
}

// And without 0x634680's true, 0x637700 does not present the frame (0x6385e1..0x6385f7): the clear is not seen and
// the login's last one stays. The scene holds the frame until a step saw the list, like the login with the connect
// (Q-C85): the list that arrived in a Poll without a step has not placed the characters yet.
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

// With a notice open before the list, MuRet shows it (AFailureBeforeTheListShowsTheNoticeToo): the frame is not held.
// Without a session there is no list to wait for.
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

// The selection bar (0x63ec00): the ornament, the untextured strip and the four buttons.
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

// The backgrounds, the end of the 2D, the world pass and the models, in 0x634680's order, and the UI on top: the bar
// (0x65fe00).
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

// 0x634980: after the UI (0x65fe00), the game cursor at the WndProc's mouse, with the normal blend and in white.
// Before the list, 0x634680 leaves before getting there: no cursor.
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
    // (100, 200) of 640 × 480 is the WndProc's (100, 200): the quad goes from x 98 to 122.
    EXPECT_EQ(cursor.vertices.front().x, 98.0f);
}

// Each character at its place from 0x4c20a0, at z = 163, with its turn: each part's triangle starts at the
// character's position.
TEST(CharacterSelectSceneTest, PlacesEachCharacterInItsSlot)
{
    Harness tested;
    ASSERT_NO_FATAL_FAILURE(tested.ReachTheChoice());

    const std::vector<FrameEvent> events = tested.Frame();

    const auto models = std::ranges::find(events, Kind::Models, &FrameEvent::kind);
    ASSERT_NE(models, events.end());
    // Six one-triangle meshes per character: with equipment 0, the helmet is the 0x1012, which shows the class's head
    // (0x512930).
    ASSERT_EQ(models->modelVertices.size(), 2u * 6u * 3u);
    for (const auto& [first, slot] : {std::pair{0u, 0u}, std::pair{18u, 3u}})
    {
        const glm::vec3 position = models->modelVertices[first].position;
        EXPECT_NEAR(position.x, CharacterSlots[slot].position.x, 1e-3f) << slot;
        EXPECT_NEAR(position.y, CharacterSlots[slot].position.y, 1e-3f) << slot;
        EXPECT_NEAR(position.z, CharacterSlotHeight, 1e-3f) << slot;
    }
}

// The click picks the one under the mouse (the picking with the pass's ray), and one over nobody, none (0x6342c0's
// -1).
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

// A character's light in the selection (06 § El color del personaje), with the mouse outside so that nobody has the
// outline: each slot's, the first 18 vertices of slot 0 and the others of slot 3.
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

// 0x50ba30: the base is 0.4 plus +0xf4, and the chosen one has +0xf4 at 1 (0x634680).
TEST(SelectLightTest, ChosenIsBrighter)
{
    const auto [slot0, slot3] = LightsAfterChoosing(3);

    EXPECT_FLOAT_EQ(slot3, 1.4f);
    EXPECT_FLOAT_EQ(slot0, 0.5f);
}

// Slot 0's character is the "hero" (DAT_0743a170, object 0 since startup): in scene 4, 0x50ba30 gives it 0.5 plus
// +0xf4 (0x50d219), and the trace shows it: 1.5 with it chosen.
TEST(SelectLightTest, TheFirstSlotIsTheHero)
{
    const auto [slot0, slot3] = LightsAfterChoosing(0);

    EXPECT_FLOAT_EQ(slot0, 1.5f);
    EXPECT_FLOAT_EQ(slot3, 0.4f);
}

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

// Q-C45: a short Enter, pressed and released in the same step, also enters. The original loses it if it falls between
// two GetAsyncKeyState reads; MuRet sees it, as in the login.
TEST(CharacterSelectSceneTest, AShortEnterAlsoEnters)
{
    Harness tested;
    ASSERT_NO_FATAL_FAILURE(tested.ReachTheChoice());
    tested.ClickAt(tested.OnScreen(0));

    Client::Tests::TapKey(tested.input, Key::Enter);
    tested.Step();

    EXPECT_EQ(tested.scene->Next(), SceneId::WorldLoading);
}

// The double click on one picks it and enters (Q-C99: 0x640940's own clock).
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

// With bit 0x1 of the flags, notice 0x84 and no entry.
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

// 0x6342c0 does nothing before the list: a click from then is not the first of a double click.
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

// With the notice open, the scene handles only the notice (Q-C84): a click on another one does not pick it.
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

// If the frame has a notice's box (0x727, from the windows).
bool ShowsANotice(const std::vector<FrameEvent>& events)
{
    return std::ranges::any_of(events, [](const FrameEvent& event)
                               { return event.texture == std::optional<TextureSlot>{TextureSlot{0x727}}; });
}

// Q-C70: in the selection (the UI's mode 4), FD_CLOSE opens notice 0x70, as in the login, and its OK closes the game
// (Q-C69). The scene does not go back to the login.
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

// Without the list and with the notice open (the session fell), the notice has the cursor on top: the system one is
// hidden, and without this one the mouse could not be seen.
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

// The 0x70 that opens in a step is handled from the next one: an Enter of that step does not close the game before
// the notice is seen; with the list and before it.
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

// Waiting for the list, the same: the notice is drawn even though the scene does not draw anything else yet.
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

// The vertices of the outline's second pass, at (0.7, 0.2, 0), and the first of them.
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

// The character under the mouse has the outline (0x510d00 with DAT_006c8e30): the six one-triangle meshes, in its
// place; the one next to it does not, and without the mouse over, nobody.
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

// With the notice open, the UI has the mouse (+0x8de8) and 0x634680 does not call 0x59ec80: the outline stays on the
// last one under the mouse, even if the mouse leaves.
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

// 0x50ba30 advances the wings (0x509340 with +0x308: 0.25 keys per frame of the original, 1 in actions 0x1e and 0x1f),
// and the selection advances them by time, like the pet: slot 0's wings (the seventh mesh, after the five parts and
// the head) rise.
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

// The F3 01 reply with 1: char0003 of class 1 (0x20) and level 1, in `slot`.
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

// The connect button's quad (0x11): with one chosen and without the mouse, the normal frame.
FrameEvent ConnectQuad(const std::vector<FrameEvent>& events)
{
    const auto connect = std::ranges::find(events, std::optional<TextureSlot>{TextureSlot{0x11}}, &FrameEvent::texture);
    EXPECT_NE(connect, events.end());
    return connect == events.end() ? FrameEvent{} : *connect;
}

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

// The bar's buttons in 640 × 480: create at (22, 399), connect at (509, 399) and delete at (564, 399), 54 × 30.
constexpr glm::vec2 CreateButton{30.0f, 410.0f};
constexpr glm::vec2 ConnectButton{515.0f, 410.0f};
constexpr glm::vec2 DeleteButton{570.0f, 410.0f};

// The bar is not modal, but covers its rectangle (+0x8de8, 06 § La lógica): a click on it neither picks nor clears
// the choice (the outline stays on the last one, OverTheBarTheOutlineStaysWhereItWas).
TEST(CharacterSelectSceneTest, TheBarBlocksThePicking)
{
    Harness tested;
    ASSERT_NO_FATAL_FAILURE(tested.ReachTheChoice());
    tested.ClickAt(tested.OnScreen(3));
    ASSERT_NE(tested.online.session.ChosenCharacter(), nullptr);

    tested.ClickAt({300.0f, 420.0f});

    ASSERT_NE(tested.online.session.ChosenCharacter(), nullptr);
    EXPECT_EQ(tested.online.session.ChosenCharacter()->slot, 3);
    tested.ClickAt({300.0f, 390.0f});
    EXPECT_EQ(tested.online.session.ChosenCharacter(), nullptr);
}

// The bar's build (0x63e820) looks at the list's flags once: with bit 0x10 in any, texts 436 and 437; without it,
// not.
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

// Connect is Enter's (0x633c10): without a chosen one it is disabled; with one, it enters.
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

// The top v of the create button in the frame: frame 3 (disabled) lowers the texture.
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

// 0x63eb40: create, with a free place of the five. With the five, the disabled frame.
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

// 0x63edf0: a guild member is not deleted (the 0x92), and the notice closes with its OK.
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

// Level 80 or more, in a group not named like text 559, the 0x83. The harness's chosen server is 400, of group 20
// (text 1829, which is missing: empty).
TEST(CharacterSelectSceneTest, AHighLevelOneOutsideTheFreeGroupIsNotDeleted)
{
    Harness tested;
    ASSERT_NO_FATAL_FAILURE(tested.ReachTheChoiceWith({{0, "char0001"}, {3, "char0002", 80}}));
    tested.ClickAt(tested.OnScreen(3));

    tested.ClickAt(DeleteButton);

    EXPECT_TRUE(Draws(tested.Frame(), "nivel alto"));
}

// The whole deletion: the confirmation with the name (0x5e), the code (0x5f, digits only and with the text field),
// the F3 02 with the name and the code and the "wait" (0x5d), and with reply 1, the 0x60: the character leaves and
// nobody stays chosen.
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
    const auto expected = Client::Protocol::FrontEnd::WriteCharacterDelete(
        Client::Tests::NameBytes("char0002"), Client::Protocol::FrontEnd::DeleteCodeFromText("1234"));
    EXPECT_TRUE(Client::Tests::SameBytes(deletes[0].Bytes(), Client::Network::Xor32Decode(expected.packet).Bytes()));
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

// The trapezoid the selection's draw leaves: that of its fixed point on the 54, with its camera.
Client::Terrain::TerrainPolygon SelectionPolygon()
{
    return Client::Terrain::TerrainPolygonOf(Client::Terrain::CullingScene::CharacterSelect, 54,
                                             Client::Camera::SelectionCullingPoint,
                                             Client::Camera::SelectionCamera().angles.z);
}

// 0x63475c: each frame of the original, after the camera (0x6346ca), 0x5fbb00 leaves in the globals the trapezoid of
// the selection's fixed point (0x6346b5..0x6346c3), which is no character, and the effective level's transition
// (0x5fb731) with the level the camera asked for, at 0. An intermediate draw, at the same clock, does not write.
TEST(CharacterSelectSceneTest, EachFrameLeavesTheTrapezoidOfItsFixedPoint)
{
    Client::Kernel::WorldState world;
    Client::FrontEnd::SelectionState selection(world);
    ASSERT_NO_FATAL_FAILURE(RetainLoginTerrain(selection));
    Harness tested(true, false, false, &selection, &world);
    ASSERT_NO_FATAL_FAILURE(tested.ReachTheChoice());
    tested.states.camera.trapezoidLevel.effectiveLevel = 3;

    static_cast<void>(tested.Frame());

    const Client::Terrain::TerrainPolygon expected = SelectionPolygon();
    EXPECT_EQ(tested.states.camera.trapezoidLevel.effectiveLevel, 0);
    EXPECT_EQ(tested.states.camera.trapezoid.x, expected.x);
    EXPECT_EQ(tested.states.camera.trapezoid.y, expected.y);
    EXPECT_FALSE(tested.states.camera.trapezoid.everything);

    tested.states.camera.trapezoid = {};
    tested.states.camera.trapezoidLevel.effectiveLevel = 3;
    static_cast<void>(tested.Frame());
    EXPECT_EQ(tested.states.camera.trapezoid.x, (std::array<float, 4>{})) << "un dibujo intermedio no escribe";
    EXPECT_EQ(tested.states.camera.trapezoidLevel.effectiveLevel, 3);
    EXPECT_EQ(tested.scene->Failure(), std::nullopt);
}

// 0x6342f9 walks the 400 places, also a living one that did not come in the list of five. Its model, not loaded, is
// 0x5769ad8's zero record: 0x4f9580 does not advance the frame and 0x505de0 builds the box and leaves.
TEST(CharacterSelectSceneTest, TheCommonStepVisitsLivingActorsOutsideTheList)
{
    Client::Kernel::WorldState world;
    Client::FrontEnd::SelectionState selection(world);
    Client::Models::ClothWorld cloth;
    ASSERT_NO_FATAL_FAILURE(RetainLoginTerrain(selection));
    Harness tested(true, false, false, &selection, &world, &cloth);
    ASSERT_NO_FATAL_FAILURE(tested.ReachTheChoiceWith({}));
    ASSERT_FALSE(tested.scene->Failure().has_value());
    auto& other = world.Actors()[7];
    other.alive = true;
    other.type = 0xf1;
    constexpr std::uint32_t Seed = 16;
    cloth.random = Client::Crt::CrtRandom(Seed);
    cloth.logicSteps = 77;

    tested.Step();

    EXPECT_EQ(tested.scene->Failure(), std::nullopt);
    // The 0xf1 falls (0x4fb910, without rand(): the login's floor raises it and +0xe4 bounces) and its frame stays at
    // 0.
    EXPECT_EQ(other.floatE4, static_cast<float>(-6.0 * static_cast<double>(-0.4f)));
    EXPECT_EQ(other.animation.frame, 0.0f);
    EXPECT_EQ(cloth.logicSteps, 78);
    // The step's 32 rand() without it and the 24 of its step.
    EXPECT_EQ(cloth.random.State(), Client::Tests::AfterDraws(Seed, 32 + 24).State());
}

TEST(CharacterSelectSceneTest, TheFirstNativeStepConstructsBothPreviewsAndResetsOnlyEightOwnLevels)
{
    Client::Kernel::WorldState world;
    Client::FrontEnd::SelectionState selection(world);
    Client::Models::ClothWorld cloth;
    auto& own = world.OwnCharacter();
    own.byte6a = 3;
    own.words6c.fill(7);
    for (auto& item : own.equipment)
    {
        item.id = 9;
        item.occupied = 1;
        item.scalar1Low = 31;
        item.flags1b = 17;
    }
    for (auto& item : world.Inventory().cells)
    {
        item.id = 9;
        item.occupied = 1;
        item.flags1b = 17;
    }
    world.Inventory().secondCells[0].id = 7;
    world.Inventory().secondCells[0].occupied = 2;
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
        EXPECT_EQ(own.equipment[slot].id, Client::Kernel::EmptyOwnItemId);
        EXPECT_EQ(own.equipment[slot].occupied, 0u);
        EXPECT_EQ(own.equipment[slot].scalar1Low, slot < 8 ? 0u : 31u);
        EXPECT_EQ(own.equipment[slot].flags1b, 17u);
    }
    for (const auto& item : world.Inventory().cells)
    {
        EXPECT_EQ(item.id, Client::Kernel::EmptyOwnItemId);
        EXPECT_EQ(item.occupied, 0);
        EXPECT_EQ(item.flags1b, 17u);
    }
    EXPECT_EQ(world.Inventory().secondCells[0].id, 7u);
    EXPECT_EQ(world.Inventory().secondCells[0].occupied, 2);
    // Constructors 56, emptying 3848, own record 3848, preview 19, particles 2 and finally cloth 9; before, the 65536
    // of the grass (0x5f8300) of the 54's load (0x61a560), which creates no objects with rand().
    EXPECT_EQ(cloth.random.State(), Client::Tests::AfterDraws(1, 3934 + 3848 + 65536).State());
}

// 0x5bc0f1/0x5bc102: both masks are queried in order; the only active mode sends a 31 before the trade.
TEST(CharacterSelectSceneTest, TheFirstStepClosesEitherSiegeWeaponBetweenInventoryAndTradeReset)
{
    for (const std::uint8_t mode : {std::uint8_t{1}, std::uint8_t{2}})
    {
        auto world = std::make_unique<Client::Kernel::WorldState>();
        Client::FrontEnd::SelectionState selection(*world);
        Client::Models::ClothWorld cloth;
        ASSERT_NO_FATAL_FAILURE(RetainLoginTerrain(selection));
        FlatModels models;
        Client::Camera::GameCamera camera;
        Client::Camera::GameCameraInput cameraInput;
        Client::Tests::RecordingSink sink;
        Client::Core::Log log(sink, Client::Core::LogLevel::Debug);
        Client::Scenes::WorldEntryResources resources{.world = *world,
                                                      .selection = selection,
                                                      .dataRoot = {},
                                                      .cloth = cloth,
                                                      .camera = camera,
                                                      .cameraInput = cameraInput,
                                                      .models = models,
                                                      .log = log};
        auto& runtime = world->Runtime();
        runtime.panels.delegated.siegeWeapon = mode;
        runtime.panels.delegated.siegeGate = 1;
        runtime.castleSiege.siegeBattleUi.weaponChoice = 2;
        runtime.social.trade.changed = 1;
        runtime.social.trade.ownGrid[0].id = 7;
        runtime.social.trade.ownGrid[0].occupied = 1;
        world->Inventory().cells[0].id = 9;
        world->Inventory().cells[0].occupied = 1;
        world->OwnCharacter().equipment[0].scalar1Low = 31;
        std::vector<Client::Protocol::Outgoing> sent;
        resources.sendGamePacket = [&](Client::Protocol::Outgoing packet)
        {
            EXPECT_EQ(runtime.panels.delegated.siegeWeapon, mode); // The 31 precedes 0x44dcf0.
            EXPECT_EQ(world->Inventory().cells[0].id, Client::Kernel::EmptyOwnItemId);
            EXPECT_EQ(world->Inventory().cells[0].occupied, 0);
            EXPECT_EQ(runtime.social.trade.changed, 1);
            EXPECT_EQ(runtime.social.trade.ownGrid[0].id, 7);
            EXPECT_EQ(world->OwnCharacter().equipment[0].scalar1Low, 31u); // The own reset comes after.
            sent.push_back(std::move(packet));
        };
        Harness tested(true, false, false, &selection, world.get(), &cloth, true, &resources);

        ASSERT_NO_FATAL_FAILURE(tested.ReachTheChoiceWith({}));

        ASSERT_FALSE(tested.scene->Failure().has_value());
        ASSERT_EQ(sent.size(), 1u);
        EXPECT_TRUE(Client::Tests::SameBytes(sent.front().packet.Bytes(), Client::Tests::Bytes({0xc1, 3, 0x31})));
        EXPECT_EQ(runtime.panels.delegated.siegeWeapon, 0);
        EXPECT_EQ(runtime.castleSiege.siegeBattleUi.weaponChoice, 0);
        EXPECT_EQ(runtime.panels.delegated.siegeGate, 1); // 0x5bbe10 only queries the two weapons.
        EXPECT_EQ(runtime.social.trade.changed, 0);
        EXPECT_EQ(runtime.social.trade.ownGrid[0].id, Client::Kernel::EmptyOwnItemId);
        EXPECT_EQ(world->OwnCharacter().equipment[0].scalar1Low, 0u);
        EXPECT_EQ(cloth.random.State(), Client::Tests::AfterDraws(1, 3934 + 3848 + 65536).State());
    }
}

// 0x634199..0x634227: the first step with the list clears the dialog fields (0x77f3b68..0x77f3b6a), opens the chat
// field, empties the old area with 120 entries and, with the two numbers of pkt[5] (the low and the high), writes the
// 1240 as system and without a name.
TEST(CharacterSelectSceneTest, TheFirstStepEmptiesTheMessagesAndWritesTheAccountNotice)
{
    Client::Kernel::WorldState world;
    Client::FrontEnd::SelectionState selection(world);
    Client::Models::ClothWorld cloth;
    ASSERT_NO_FATAL_FAILURE(RetainLoginTerrain(selection));
    Harness tested(true, false, false, &selection, &world, &cloth);
    Client::Kernel::WorldRuntime& runtime = world.Runtime();
    runtime.chat.area.rows[3].text = "viejo";
    runtime.chat.area.count = 4;
    runtime.chatInput.dialogFieldOn = true;
    runtime.chatInput.otherInput = true;
    runtime.social.moneyField = 1;

    ASSERT_NO_FATAL_FAILURE(tested.Enter());
    tested.online.socket.reads = {Client::Tests::CharacterListBytes({}, 0, 0, 0x21)};
    tested.Step();

    ASSERT_FALSE(tested.scene->Failure().has_value()) << *tested.scene->Failure();
    EXPECT_FALSE(runtime.chatInput.dialogFieldOn);
    EXPECT_FALSE(runtime.chatInput.otherInput);
    EXPECT_EQ(runtime.social.moneyField, 0);
    EXPECT_TRUE(runtime.ui.chatOpen);
    const Client::Ui::MessageAreaState& area = runtime.chat.area;
    ASSERT_EQ(area.count, static_cast<std::int32_t>(Client::Ui::MessageAreaRows));
    EXPECT_EQ(area.rows[3].text, "");
    EXPECT_EQ(area.rows.back().name, "");
    EXPECT_EQ(area.rows.back().text, "General 1, Gladiador 2");
    EXPECT_EQ(area.rows.back().type, Client::Ui::SystemMessage);
}

// With pkt[5] at 0, only the empty entries.
TEST(CharacterSelectSceneTest, WithoutNumbersThereIsNoAccountNotice)
{
    Client::Kernel::WorldState world;
    Client::FrontEnd::SelectionState selection(world);
    Client::Models::ClothWorld cloth;
    ASSERT_NO_FATAL_FAILURE(RetainLoginTerrain(selection));
    Harness tested(true, false, false, &selection, &world, &cloth);

    ASSERT_NO_FATAL_FAILURE(tested.ReachTheChoiceWith({}));

    ASSERT_FALSE(tested.scene->Failure().has_value()) << *tested.scene->Failure();
    EXPECT_TRUE(world.Runtime().ui.chatOpen);
    EXPECT_EQ(world.Runtime().chat.area.rows.back().text, "");
}

// 0x633f91 empties slot 7 before 0x4fd5f0 queries its item at 0x54efe0.
TEST(CharacterSelectSceneTest, TheFirstSelectionEmptiesTheEquipmentBeforeTheBodyReadsItemBmd)
{
    Client::Kernel::WorldState world;
    Client::FrontEnd::SelectionState selection(world);
    Client::Models::ClothWorld cloth;
    ASSERT_NO_FATAL_FAILURE(RetainLoginTerrain(selection));
    Harness tested(true, false, false, &selection, &world, &cloth, false);

    CharacterEntryFields hero;
    hero.equipment[4] = 3; // Pet NONE: this test reaches the actor's tables, not the helper 0x6342f4.
    ASSERT_NO_FATAL_FAILURE(tested.ReachTheChoiceWith({hero}));

    ASSERT_FALSE(tested.scene->Failure().has_value());
    EXPECT_EQ(world.OwnCharacter().equipment[7].id, Client::Kernel::EmptyOwnItemId);
    EXPECT_FLOAT_EQ(tested.scene->PreviewActor().animation.frame, 0.6f);
}

TEST(CharacterSelectSceneTest, TheLoadedItemTableLetsTheFirstHeroReachThePreviewStep)
{
    Client::Kernel::WorldState world;
    Client::FrontEnd::SelectionState selection(world);
    Client::Models::ClothWorld cloth;
    ASSERT_NO_FATAL_FAILURE(RetainLoginTerrain(selection));
    Harness tested(true, false, false, &selection, &world, &cloth);

    CharacterEntryFields hero;
    hero.equipment[4] = 3;
    ASSERT_NO_FATAL_FAILURE(tested.ReachTheChoiceWith({hero}));

    ASSERT_FALSE(tested.scene->Failure().has_value());
    EXPECT_FLOAT_EQ(tested.scene->PreviewActor().animation.frame, 0.6f);
    EXPECT_EQ(world.OwnCharacter().equipment[7].id, Client::Kernel::EmptyOwnItemId);
    EXPECT_TRUE(world.Actors()[0].seen);
}

TEST(CharacterSelectSceneTest, ThePreviewAdvancesClosedTwiceUnderItsWindowAndOnceUnderANotice)
{
    Client::Kernel::WorldState world;
    Client::FrontEnd::SelectionState selection(world);
    Client::Models::ClothWorld cloth;
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
    Client::Kernel::WorldState world;
    Client::FrontEnd::SelectionState selection(world);
    Client::Models::ClothWorld cloth;
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
    Client::Kernel::WorldState world;
    Client::FrontEnd::SelectionState selection(world);
    Client::Models::ClothWorld cloth;
    ASSERT_NO_FATAL_FAILURE(RetainLoginTerrain(selection));
    Harness tested(true, false, false, &selection, &world, &cloth);
    ASSERT_NO_FATAL_FAILURE(tested.ReachTheChoiceWith({}));
    constexpr std::uint32_t Seed = 16;
    cloth.random = Client::Crt::CrtRandom(Seed);
    cloth.logicSteps = 41;
    auto& preview = selection.PreviewActor();
    preview.animation.action = 1;
    preview.animation.frame = 0;
    preview.headTurn = {};

    tested.Step();

    ASSERT_FALSE(tested.scene->Failure());
    // Rand 18..20: 26176, 32288, 30626. The head draws before both winds.
    EXPECT_EQ(preview.headTurn.target, (glm::vec2{328.0f, 346.0f}));
    EXPECT_FLOAT_EQ(preview.headTurn.angles.x, 353.6f);
    EXPECT_FLOAT_EQ(preview.headTurn.angles.y, 357.2f);
    EXPECT_EQ(cloth.random.State(), Client::Tests::AfterDraws(Seed, 32).State());
    EXPECT_EQ(cloth.logicSteps, 42);
    EXPECT_NE(world.Particles().Wind().wind, glm::vec2(0.0f));
}

TEST(CharacterSelectSceneTest, RavenSparksAgeAfterThePreviewAndTheirSlotsAreReused)
{
    Client::Kernel::WorldState world;
    Client::FrontEnd::SelectionState selection(world);
    Client::Models::ClothWorld cloth;
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
    EXPECT_EQ(particles[0].type, Client::Particles::SparkParticle);
    EXPECT_EQ(particles[0].life, 9) << "nace con10 y634315 la mueve en el mismo paso";
    world.Actors()[4].flags78 |= 0x1000;
    for (int step = 0; step < 12; ++step)
    {
        tested.Step();
        ASSERT_FALSE(tested.scene->Failure());
    }
    EXPECT_FALSE(world.Particles().Any());
    const auto reused = world.Particles().Create(Client::Particles::SparkParticle, {}, {}, {},
                                                 Client::Particles::RavenSparkSubtype, 1.0f, 0.0f, cloth.random);
    ASSERT_TRUE(reused.has_value());
    EXPECT_EQ(*reused, 0u);
}

// Q-C293: 0x6342f4 passes the ten helpers through 0x444010 in scene 4, without scene 5's dead rider guard (0x44402a):
// rider 0's Uniria, dead, takes its place and its action, alpha at 1 (with the rider's +0x80 at 0), speed 0.34 and
// the rider's alive byte (0x444ac0), and the selection goes on.
TEST(CharacterSelectSceneTest, ASelectionHelperRunsItsBodyWithoutTheGameGuard)
{
    Client::Kernel::WorldState world;
    Client::FrontEnd::SelectionState selection(world);
    Client::Models::ClothWorld cloth;
    ASSERT_NO_FATAL_FAILURE(RetainLoginTerrain(selection));
    Harness tested(true, false, false, &selection, &world, &cloth);
    tested.models.EnableHelpers();
    ASSERT_NO_FATAL_FAILURE(tested.ReachTheChoiceWith({}));
    auto& helpers = world.Helpers();
    helpers[0].horse.alpha = 0.5f;
    helpers[1].alive = true;
    helpers[1].type = Client::Pets::UniriaModel;
    helpers[1].rider = 0;
    ASSERT_FALSE(world.Actors()[0].alive);

    tested.Step();

    ASSERT_FALSE(tested.scene->Failure()) << *tested.scene->Failure();
    EXPECT_FLOAT_EQ(helpers[0].horse.alpha, 0.5f);
    EXPECT_FALSE(helpers[1].alive);
    EXPECT_EQ(helpers[1].horse.alpha, 1.0f);
    EXPECT_EQ(helpers[1].position, world.Actors()[0].position);
    EXPECT_EQ(helpers[1].horse.speed, std::bit_cast<float>(0x3eae147bu));
}

// 0x63486a: 0x4453b0 in scene 4 draws the character's Uniria with the scale at 1 (0x4452f1, not 0x443d70's 0.9) and
// 0x5ffce0's color with the terrain light of the 54, without stopping.
TEST(CharacterSelectSceneTest, TheSelectionDrawsTheUniriaOfItsCharacter)
{
    Client::Kernel::WorldState world;
    Client::FrontEnd::SelectionState selection(world);
    Client::Models::ClothWorld cloth;
    ASSERT_NO_FATAL_FAILURE(RetainLoginTerrain(selection));
    Harness tested(true, false, false, &selection, &world, &cloth);
    tested.models.EnableHelpers();
    // Nothing in the hands and no wings: 0x50ba30's tail draws what hangs, and the test model has no items.
    CharacterEntryFields hero;
    hero.equipment[0] = 0xff;
    hero.equipment[1] = 0xff;
    hero.equipment[4] = 0x0f;
    hero.equipment[11] = 0xf0;
    hero.equipment[12] = 0xf0;
    ASSERT_NO_FATAL_FAILURE(tested.ReachTheChoiceWith({hero}));
    ASSERT_FALSE(tested.scene->Failure()) << *tested.scene->Failure();
    auto& uniria = world.Helpers()[1];
    uniria.alive = true;
    uniria.type = Client::Pets::UniriaModel;
    uniria.rider = 0;
    uniria.scale = 0.9f;
    uniria.horse.alpha = 1.0f;
    uniria.position = world.Actors()[0].position;

    static_cast<void>(tested.Frame());

    ASSERT_FALSE(tested.scene->Failure()) << *tested.scene->Failure();
    EXPECT_TRUE(uniria.seen);
    EXPECT_EQ(uniria.scale, 1.0f);
}

// A helper 0x444010 cannot move stops at 0x6342f4 → 0x444083 before the pool, the preview and the particles
// (0x634315): it spends no rand() and does not advance the counter, and the alpha that rose (0x4e9980) stays.
TEST(CharacterSelectSceneTest, ASelectionHelperFailureKeepsItsPrefixBeforeThePool)
{
    Client::Kernel::WorldState world;
    Client::FrontEnd::SelectionState selection(world);
    Client::Models::ClothWorld cloth;
    ASSERT_NO_FATAL_FAILURE(RetainLoginTerrain(selection));
    Harness tested(true, false, false, &selection, &world, &cloth);
    ASSERT_NO_FATAL_FAILURE(tested.ReachTheChoiceWith({}));
    auto& helpers = world.Helpers();
    helpers[0].horse.alpha = 0.5f;
    helpers[1].alive = true;
    helpers[1].type = 0xae;
    helpers[1].rider = 0;
    auto& spark = world.Particles().All()[0];
    spark.alive = true;
    spark.type = Client::Particles::SparkParticle;
    spark.subtype = Client::Particles::RavenSparkSubtype;
    spark.life = 7;
    constexpr std::uint32_t Seed = 16;
    cloth.random = Client::Crt::CrtRandom(Seed);
    cloth.logicSteps = 41;

    tested.Step();

    EXPECT_EQ(cloth.random.State(), Seed);
    EXPECT_EQ(cloth.logicSteps, 41);
    EXPECT_EQ(spark.life, 7);
    EXPECT_FLOAT_EQ(helpers[0].horse.alpha, 0.5f);
    EXPECT_TRUE(helpers[1].alive);
    EXPECT_EQ(helpers[1].horse.alpha, std::bit_cast<float>(0x3dcccccdu));
    ASSERT_TRUE(tested.scene->Failure());
    EXPECT_NE(tested.scene->Failure()->find("0x6342f4 → 0x444083"), std::string::npos) << *tested.scene->Failure();
}

TEST(CharacterSelectSceneTest, AParticleBindingFailureKeepsItsPrefixAndSkipsClothAndLaterSlots)
{
    Client::Kernel::WorldState world;
    Client::FrontEnd::SelectionState selection(world);
    Client::Models::ClothWorld cloth;
    ASSERT_NO_FATAL_FAILURE(RetainLoginTerrain(selection));
    Harness tested(true, false, false, &selection, &world, &cloth);
    ASSERT_NO_FATAL_FAILURE(tested.ReachTheChoiceWith({}));
    auto particles = world.Particles().All();
    for (std::size_t slot = 0; slot < 3; ++slot)
    {
        particles[slot].alive = true;
        particles[slot].type = Client::Particles::SparkParticle;
        particles[slot].subtype = Client::Particles::RavenSparkSubtype;
        particles[slot].life = 5;
    }
    particles[1].type = 0xbeef;
    particles[1].moves = true;
    particles[1].position = {1.0f, 2.0f, 3.0f};
    particles[1].velocity = {4.0f, 0.0f, 0.0f};
    selection.PreviewActor().animation.frame = 0;
    selection.PreviewActor().headTurn = {};
    constexpr std::uint32_t Seed = 16;
    cloth.random = Client::Crt::CrtRandom(Seed);
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

// ADR 0009: if 0x5fb570 cannot handle the camera's D (it does not fit in an int64), the frame is cut before the world
// pass, the scene stays in Failure with the address and the globals stay as they were.
TEST(CharacterSelectSceneTest, ATrapezoidErrorCutsTheFrame)
{
    Client::Kernel::WorldState world;
    Client::FrontEnd::SelectionState selection(world);
    ASSERT_NO_FATAL_FAILURE(RetainLoginTerrain(selection));
    Harness tested(true, false, false, &selection, &world);
    ASSERT_NO_FATAL_FAILURE(tested.ReachTheChoice());
    tested.camera.distance = std::numeric_limits<float>::quiet_NaN();
    tested.states.camera.trapezoidLevel.effectiveLevel = 3;

    const std::vector<FrameEvent> events = tested.Frame();

    ASSERT_TRUE(tested.scene->Failure().has_value());
    EXPECT_NE(tested.scene->Failure()->find("0x5fb570"), std::string::npos) << *tested.scene->Failure();
    EXPECT_EQ(std::ranges::find(events, Kind::Models, &FrameEvent::kind), events.end());
    EXPECT_EQ(tested.states.camera.trapezoid.x, (std::array<float, 4>{}));
    EXPECT_EQ(tested.states.camera.trapezoidLevel.effectiveLevel, 3);
    EXPECT_TRUE(tested.Frame().empty()) << "cortada, no vuelve a dibujar";
}

// F3 02 deactivates the view from Poll: without another Step it disappears, but the CPU record stays alive.
TEST(CharacterSelectSceneTest, ADeletedOneDisappearsWhenPollIsFollowedByRenderWithoutStep)
{
    constexpr std::uint8_t DeletedSlot = 1;
    Client::Kernel::WorldState world;
    Client::FrontEnd::SelectionState selection(world);
    ASSERT_NO_FATAL_FAILURE(RetainLoginTerrain(selection));
    Harness tested(true, true, false, &selection);
    ASSERT_NO_FATAL_FAILURE(tested.ReachTheChoiceWith({{DeletedSlot, "char0001"}}));
    const auto before = tested.Frame();
    const auto visible = std::ranges::find(before, Kind::Models, &FrameEvent::kind);
    ASSERT_NE(visible, before.end());
    ASSERT_FALSE(visible->modelVertices.empty());
    const auto retained = selection.Slots()[DeletedSlot];
    tested.online.session.ChooseCharacter(DeletedSlot);
    tested.online.session.DeleteCharacter(Client::Protocol::FrontEnd::DeleteCodeFromText("1234"));
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

// The replies that do not delete: 0 → 0x92, 3 → 0x62 and another → 0x89 (0x4e2fe9). The character stays.
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
        // Both stay: six meshes each, with the outline (two more passes) on the 3, the last one under the mouse.
        const auto models = std::ranges::find(events, Kind::Models, &FrameEvent::kind);
        ASSERT_NE(models, events.end());
        EXPECT_EQ(models->modelVertices.size(), 2u * 6u * 3u + 2u * 6u * 3u);
        EXPECT_EQ(OutlineVertices(events).size(), 6u * 3u);
    }
}

// Esc in the confirmation or in the code closes without sending anything (0x6436a0).
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

// The one the session created (F3 01 with 1) appears in its place, with its class's parts and without equipment
// (Q-C103), and the others keep their animation.
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

// Two identical F3 01 build twice: the second restarts the pose even though SameEntry says they are equal.
// Slot 1's world vertices let it be observed without Hero or the camera explaining the change.
TEST(CharacterSelectSceneTest, AnIdenticalCreationRestartsItsVisualWhenPollIsFollowedByRenderWithoutStep)
{
    constexpr int SelectionMap = 54;
    constexpr std::uint8_t CreatedSlot = 1;
    constexpr int AnimationSteps = 5;
    constexpr auto StepTime = std::chrono::milliseconds{40};
    Client::Kernel::WorldState world;
    Client::FrontEnd::SelectionState selection(world);
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
    // The class's five parts, without the head (which goes only with the 0x1012 helmets and others), one triangle each.
    ASSERT_EQ(models->modelVertices.size(), 2u * 6u * 3u + 5u * 3u);
    const bool inSlotOne =
        std::ranges::any_of(models->modelVertices,
                            [](const Client::Render::ModelVertex& vertex)
                            {
                                return std::abs(vertex.position.x - CharacterSlots[1].position.x) < 1e-3f &&
                                       std::abs(vertex.position.y - CharacterSlots[1].position.y) < 1e-3f;
                            });
    EXPECT_TRUE(inSlotOne);
    // Once: the line belongs to the change, not to each step.
    tested.Step();
    EXPECT_EQ(std::ranges::count_if(tested.sink.lines, [](const std::string& line)
                                    { return line.find("la selección tiene ahora") != std::string::npos; }),
              1);
    EXPECT_EQ(std::ranges::count_if(tested.sink.lines, [](const std::string& line)
                                    { return line.find("la selección tiene 2 personajes") != std::string::npos; }),
              1);
}
// The session ends with the F3 02 reply and the GS close in the same read: the failure's 0x70 notice stays on top
// and its OK closes the game; the reply does not cover it.
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

// A notice that opens in a step is handled from the next one: an Enter of that step does not close it before it is
// seen.
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

// 0x511290 leaves a new character without a guild (+0x368 at 0xff): the one created in the session can be deleted,
// with the confirmation.
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

// Those still in the list keep their animation when another enters: slot 0's wings stay where they were going, as in
// a selection where nothing happened.
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

// The one created in an occupied place overwrites it (Q-C103): it comes out with its class's parts, without
// equipment.
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

// With a notice open, the bar's buttons do not have the mouse (0x65fc30 gives it to the notice): delete, just
// pressed, returns to its normal frame under the confirmation.
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

// With the mouse over the bar, the outline stays on the last one (0x59ec80 does not run).
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
// If the frame has the create window: the name's background, cha_id (0xc).
bool ShowsTheCreateWindow(const std::vector<FrameEvent>& events)
{
    return std::ranges::any_of(events, [](const FrameEvent& event)
                               { return event.texture == std::optional<TextureSlot>{TextureSlot{0xc}}; });
}

// The bar's create button opens the window (0x65f690(+0x8498)), above the bar and with the name field, which has the
// keyboard.
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

// 0x63d990 with a name that passes: the F3 01 with the name and the class (the Dark Knight, marked even though pkt[4]
// at 0 leaves it disabled), the window closes and the 0x5d opens. With 1, the 0x5d closes and the new one enters.
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
    const auto expected = Client::Protocol::FrontEnd::WriteCharacterCreate(Client::Tests::NameBytes("char0003"), 1);
    EXPECT_TRUE(Client::Tests::SameBytes(creates[0].Bytes(), Client::Network::Xor32Decode(expected.packet).Bytes()));
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

// The name checks (0x63d990): a short one gives the 0x79 and one with a FilterName.bmd word the 0x72, above the
// window, which keeps the name; the notice's OK returns to it, and nothing is sent.
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

// With 0 or 2, the window opens again, empty, with the 0x36 or the 0x37 on top (0x4c2290).
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

// 0x63d7c0 with pkt[4]: with 2, class 2 can be picked and 4 cannot; the F3 01 carries the chosen one.
TEST(CharacterSelectSceneTest, TheClassesFollowTheList)
{
    Harness tested;
    ASSERT_NO_FATAL_FAILURE(tested.ReachTheChoiceWithClasses(2));
    tested.ClickAt(CreateButton);
    const Client::FrontEnd::CreateWindowLayout layout = Client::FrontEnd::LayoutCreateWindow(PixelSize{640, 480}, 2);
    tested.ClickAt({static_cast<float>(layout.classes[2].x + 5), static_cast<float>(layout.classes[2].y + 5)});
    tested.ClickAt({static_cast<float>(layout.classes[4].x + 5), static_cast<float>(layout.classes[4].y + 5)});
    tested.Type("char0003");

    tested.Tap(Key::Enter);

    const std::vector<Client::Network::Packet> creates = tested.CharacterPacketsSent(0x01);
    ASSERT_EQ(creates.size(), 1u);
    EXPECT_EQ(creates[0].Bytes().back(), std::byte{0x20});
}

// With a name notice on top, the window's buttons let go of the mouse: the window's OK, with the mouse over it when
// Enter was pressed, returns to its normal frame, like the notice's OK.
TEST(CharacterSelectSceneTest, UnderANoticeTheWindowLetsGoOfTheMouse)
{
    Harness tested;
    ASSERT_NO_FATAL_FAILURE(tested.ReachTheChoice());
    tested.ClickAt(CreateButton);
    const Client::FrontEnd::CreateWindowLayout layout = Client::FrontEnd::LayoutCreateWindow(PixelSize{640, 480}, 1);
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

// With the window open, Esc closes it and Enter is its own: it does not enter the world with the chosen one.
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
// The F3 01 reply with 0 and the GS close in the same read: the failure's 0x70 stays, and the window does not open
// again over a fallen session.
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

// The F3 01 reply's notice is handled from the next step: an Enter of the step it arrives in does not close it
// before it is seen.
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

// Both lists go to their place: a name that starts with '/' skips Filter.bmd, not FilterName.bmd.
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

// Q-C118: with a longer try before, 0x447ca0 sees its leftovers in the buffer: "abcde" after "abcdef g" gives the
// 0x7a. And with a notice on top, what is typed still goes to the name.
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
// A NewFace of class `characterClass` in Logo\ (the tests' model, with the chasellight texture, which the
// selection's set already wrote) for the preview.
void WriteNewFace(const Harness& tested, int characterClass)
{
    Client::Data::Models::Model face = Client::Tests::SampleModel();
    face.meshes[0].textureName = "chasellight.jpg";
    // All vertices at the root: the mesh moves whole, and its edges only change with the object's scale.
    for (auto& vertex : face.meshes[0].vertices)
    {
        vertex.bone = 0;
    }
    Client::Tests::WriteSceneFile(tested.folder.Path(), "Logo/NewFace0" + std::to_string(characterClass + 1) + ".bmd",
                                  Client::Tests::EncryptedBmd(Client::Tests::BmdContent(face)));
}

std::vector<FrameEvent> ModelPasses(const std::vector<FrameEvent>& events)
{
    std::vector<FrameEvent> passes;
    std::ranges::copy_if(events, std::back_inserter(passes),
                         [](const FrameEvent& event) { return event.kind == Kind::Models; });
    return passes;
}

// 0x63e460: with the window open, after its base, its own 3D pass with the 410 × 335 viewport from the box and the
// preview's camera, with the chosen class's NewFace; then, the rest of the window.
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
    const Client::Render::Pass3D expected = Client::Render::MakePass3D(
        PixelSize{640, 480},
        Client::FrontEnd::PreviewViewport(Client::FrontEnd::LayoutCreateWindow(PixelSize{640, 480}, 1).box,
                                          PixelSize{640, 480}),
        Client::FrontEnd::PreviewCamera());
    ASSERT_TRUE(passes[1].pass.has_value());
    EXPECT_EQ(passes[1].pass->viewport, expected.viewport);
    EXPECT_EQ(passes[1].pass->view, expected.view);
    EXPECT_FALSE(passes[1].modelVertices.empty());
    const auto preview = std::ranges::find_if(
        events, [](const FrameEvent& event)
        { return event.kind == Kind::Models && !event.modelVertices.empty() && event.pass->viewport.z == 410; });
    const auto back = std::ranges::find(events, std::optional<TextureSlot>{TextureSlot{0xc}}, &FrameEvent::texture);
    // The base: the full-screen black, untextured (0x669c70(W, H, −1, 0)).
    const auto base = std::ranges::find_if(
        events, [](const FrameEvent& event)
        { return event.kind == Kind::Quad && !event.texture && event.vertices[0].color.alpha == 128.0f / 255.0f; });
    ASSERT_NE(preview, events.end());
    ASSERT_NE(base, events.end());
    EXPECT_LT(base, preview);
    EXPECT_LT(preview, back);
}

// The preview follows the window's class (0x63d7c0 → 0x63e3e0), with its scale: the Elf's is 9.1 and the Dark
// Knight's 6.05, in the same pose (action 1 from the start).
TEST(CharacterSelectSceneTest, ThePreviewFollowsTheClass)
{
    Harness tested;
    ASSERT_NO_FATAL_FAILURE(WriteNewFace(tested, 1));
    ASSERT_NO_FATAL_FAILURE(WriteNewFace(tested, 2));
    ASSERT_NO_FATAL_FAILURE(tested.ReachTheChoiceWithClasses(2));
    tested.ClickAt(CreateButton);
    // The first edge's length: it does not change with the turn, only with the scale.
    const auto spread = [](const FrameEvent& pass)
    { return glm::length(pass.modelVertices[1].position - pass.modelVertices[0].position); };
    const std::vector<FrameEvent> knight = ModelPasses(tested.Frame());
    ASSERT_EQ(knight.size(), 2u);
    ASSERT_FALSE(knight[1].modelVertices.empty());
    const float knightSpread = spread(knight[1]);

    const Client::FrontEnd::CreateWindowLayout layout = Client::FrontEnd::LayoutCreateWindow(PixelSize{640, 480}, 2);
    tested.ClickAt({static_cast<float>(layout.classes[2].x + 5), static_cast<float>(layout.classes[2].y + 5)});

    const std::vector<FrameEvent> elf = ModelPasses(tested.Frame());
    ASSERT_EQ(elf.size(), 2u);
    ASSERT_FALSE(elf[1].modelVertices.empty());
    EXPECT_NEAR(spread(elf[1]) / knightSpread, 9.1f / 6.05f, 1e-3f);
}

// Leave also releases the preview's slots (0x61cc50, from 0x6f8).
TEST(CharacterSelectSceneTest, LeaveReleasesThePreviewSlots)
{
    Harness tested;
    ASSERT_NO_FATAL_FAILURE(WriteNewFace(tested, 1));
    ASSERT_NO_FATAL_FAILURE(tested.Enter());

    tested.scene->Leave();

    EXPECT_NE(std::ranges::find(tested.store.released, std::uint16_t{0x6f8}), tested.store.released.end());
}

// With a notice on top the preview does not advance, and it is drawn still: without interpolating a step it will not
// take, which would make it shake.
TEST(CharacterSelectSceneTest, UnderANoticeThePreviewStaysStill)
{
    Harness tested;
    ASSERT_NO_FATAL_FAILURE(WriteNewFace(tested, 1));
    ASSERT_NO_FATAL_FAILURE(tested.ReachTheChoice());
    tested.ClickAt(CreateButton);
    tested.Step();
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

// The preview greets from the entry (0x633f20 → … → 0x63e3e0 with the Dark Knight): in the step that opens the
// window, the same pose as picking another class and going back to the Dark Knight, which rebuild it with action 1
// and advance it. The bar's click opens it and 0x65fc30 handles it in the same step, because the bar goes before in
// the list's copy: it already advances once.
TEST(CharacterSelectSceneTest, ThePreviewGreetsFromTheStart)
{
    const auto firstVertex = [](Harness& tested)
    {
        const std::vector<FrameEvent> passes = ModelPasses(tested.Frame());
        EXPECT_EQ(passes.size(), 2u);
        return passes.size() == 2 && !passes[1].modelVertices.empty() ? passes[1].modelVertices[0].position
                                                                      : glm::vec3(0.0f);
    };
    const Client::FrontEnd::CreateWindowLayout layout = Client::FrontEnd::LayoutCreateWindow(PixelSize{640, 480}, 1);
    Harness opened;
    ASSERT_NO_FATAL_FAILURE(WriteNewFace(opened, 0));
    ASSERT_NO_FATAL_FAILURE(WriteNewFace(opened, 1));
    ASSERT_NO_FATAL_FAILURE(opened.ReachTheChoiceWithClasses(1));
    opened.ClickAt(CreateButton);
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
// Two characters with wings 0 (0x1a12), without a pet (without the helper's body, 0x444059) and with empty hands
// (0xfff: b0 and b1 at 0xff and the high nibbles of b11 and b12), which FlatModels does not have.
std::vector<CharacterEntryFields> TwoWingedCharacters()
{
    auto characters = TwoCharacters();
    for (auto& character : characters)
    {
        character.equipment[0] = 0xff;
        character.equipment[1] = 0xff;
        character.equipment[4] = 3;
        character.equipment[11] = 0xf0;
        character.equipment[12] = 0xf0;
    }
    return characters;
}

TEST(CharacterSelectSceneTest, SelectionPosesUseTheSharedWorldCarryOnlyOnNativeFrames)
{
    Client::Kernel::WorldState world;
    Client::FrontEnd::SelectionState selection(world);
    Client::Models::ClothWorld cloth;
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

// F1/02 and F3/00 can be dispatched in the same Poll, before scene 4's Enter.
// The new list already built the actors: Enter must not wait for another reply.
TEST(CharacterSelectSceneTest, AListReceivedBeforeSceneEntryDoesNotLeaveTheLastFrameHeld)
{
    auto world = std::make_unique<Client::Kernel::WorldState>();
    Client::FrontEnd::SelectionState selection(*world);
    Client::Models::ClothWorld cloth;
    ASSERT_NO_FATAL_FAILURE(RetainLoginTerrain(selection));
    Harness tested(true, false, false, &selection, world.get(), &cloth);
    ASSERT_NO_FATAL_FAILURE(tested.online.LogIn());
    selection.BeginSelection();
    tested.online.session.RequestCharacterList();
    auto characters = TwoCharacters();
    for (auto& character : characters)
        character.equipment[4] = 3; // No pet: FlatModels does not lend Helper01.bmd.
    tested.online.ReceiveTheList(characters);
    ASSERT_NO_FATAL_FAILURE(tested.Poll());
    ASSERT_TRUE(selection.HasReceivedList());
    const auto revision = world->Actors()[3].revision;

    ASSERT_TRUE(tested.scene->Enter().HasValue());
    tested.Step();

    EXPECT_EQ(tested.scene->Failure(), std::nullopt);
    EXPECT_TRUE(selection.HasReceivedList());
    EXPECT_TRUE(selection.IsInitialized());
    EXPECT_FALSE(tested.scene->HoldsTheLastFrame());
    EXPECT_EQ(world->Actors()[3].revision, revision);
    EXPECT_EQ(tested.CharacterPacketsSent(0x00).size(), 1u);
}

// Q-C410: the list (0x4c20a0) builds each character with 0x517270 → 0x511290, which sets its +0x11c to 1 (0x511bf7)
// and gives it its bone buffer (+0x120); 0x6001b0 poses the body there (0x600259). The body reaches the global buffer
// (0x68b181c) neither in a frame of the original nor in an intermediate one, and only the original's frame stores it
// in its own. The wings, which hang, do go to the global one.
TEST(CharacterSelectSceneTest, TheBodyGoesToItsOwnBonesAndNeverToTheGlobalPose)
{
    Client::Kernel::WorldState world;
    Client::FrontEnd::SelectionState selection(world);
    Client::Models::ClothWorld cloth;
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

    EXPECT_FALSE(world.PoseCarry().models.contains(Client::Tables::PlayerSkeletonModel));
    EXPECT_TRUE(world.PoseCarry().models.contains(std::uint16_t{0x1a12}));
    // Q-C412: the last one of the list leaves its scale and its position in Player.bmd's shared model.
    EXPECT_EQ(world.PlayerModel().scale, world.Actors()[3].scale);
    EXPECT_EQ(world.PlayerModel().origin, world.Actors()[3].position);
    for (const std::size_t slot : Slots)
    {
        SCOPED_TRACE(slot);
        ASSERT_EQ(world.Actors()[slot].bodyPose.size(), 48u);
        world.Actors()[slot].bodyPose[0][3] = {7, 8, 9};
    }

    static_cast<void>(tested.Frame());

    EXPECT_FALSE(world.PoseCarry().models.contains(Client::Tables::PlayerSkeletonModel));
    for (const std::size_t slot : Slots)
        EXPECT_EQ(world.Actors()[slot].bodyPose[0][3], glm::vec3(7, 8, 9));
    EXPECT_EQ(tested.scene->Failure(), std::nullopt);
}

// 0x50bc05..0x50bcec in the selection (0x634851 → 0x510d00 → 0x50ba30, with world 54): the shadow of each list
// character copies its scale and position to the shared 0x20b (0x60cf70), in list order: the last one's stays. With
// alpha below 0.5 (0x50bc05) it has no shadow and does not copy it. An intermediate frame does not touch it.
TEST(CharacterSelectSceneTest, TheShadowsOfTheListPlaceTheSharedShadowModel)
{
    using Client::Models::OriginalFrameTime;
    constexpr std::uint16_t Shadow = Client::Tables::PlayerShadowModel;
    auto world = std::make_unique<Client::Kernel::WorldState>();
    Client::FrontEnd::SelectionState selection(*world);
    Client::Models::ClothWorld cloth;
    ASSERT_NO_FATAL_FAILURE(RetainLoginTerrain(selection));
    Harness tested(true, true, false, &selection, world.get(), &cloth);
    ASSERT_NO_FATAL_FAILURE(tested.ReachTheChoiceWith(TwoWingedCharacters()));
    ASSERT_EQ(world->Runtime().map, 54);
    const auto drawAt = [&tested](Client::Core::Duration clock)
    {
        auto& canvas = tested.target.BeginFrame(clock);
        tested.scene->Render(canvas, 0.0f);
        tested.target.Present();
        EXPECT_FALSE(tested.scene->Failure().has_value()) << *tested.scene->Failure();
    };

    drawAt(OriginalFrameTime);

    // Slot 3 is the last of the list; its tail (DrawCharacterTail) left it scene 4's light of what hangs.
    EXPECT_EQ(world->Actors()[3].drawnLight, glm::vec3(0.4f));
    EXPECT_EQ(world->ModelPlacement(Shadow).origin, world->Actors()[3].position);
    EXPECT_EQ(world->ModelPlacement(Shadow).scale, world->Actors()[3].scale);

    world->PlaceModel(Shadow, {});
    drawAt(OriginalFrameTime);
    EXPECT_EQ(world->ModelPlacement(Shadow).scale, 0.0f);

    world->Actors()[3].alpha = 0.4f;
    drawAt(OriginalFrameTime * 2);
    ASSERT_NE(world->Actors()[0].position, world->Actors()[3].position);
    EXPECT_EQ(world->ModelPlacement(Shadow).origin, world->Actors()[0].position);
    EXPECT_EQ(world->ModelPlacement(Shadow).scale, world->Actors()[0].scale);
}

// 0x50db57..0x510a7a: with world 54 the list runs the same tail of 0x50ba30 as the game (DrawCharacterTail). The
// light of what hangs (+0x3b4, 0x50db94) is [ebp−0x44] plus +0xf4: scene 4's fixed 0.4 (0x50cc29), or the 0.5 the
// hero's color branch leaves there (0x50d231). An intermediate frame takes no rand().
TEST(CharacterSelectSceneTest, TheListRunsTheTailOfTheGameWithTheLightOfSceneFour)
{
    Client::Kernel::WorldState world;
    Client::FrontEnd::SelectionState selection(world);
    Client::Models::ClothWorld cloth;
    ASSERT_NO_FATAL_FAILURE(RetainLoginTerrain(selection));
    Harness tested(true, true, false, &selection, &world, &cloth);
    ASSERT_NO_FATAL_FAILURE(tested.ReachTheChoiceWith(TwoWingedCharacters()));

    static_cast<void>(tested.Frame());

    ASSERT_FALSE(tested.scene->Failure().has_value()) << *tested.scene->Failure();
    // Slot 0 is the published hero: 0x50d231 overwrites its [ebp−0x44] with 0.5 (the selection's trace: Helper02
    // with 1.5 on the chosen one). The others, 0.4 (0x50cc32).
    EXPECT_EQ(world.Actors()[0].drawnLight, glm::vec3(0.5f));
    EXPECT_EQ(world.Actors()[3].drawnLight, glm::vec3(0.4f));
    const auto random = cloth.random.State();
    static_cast<void>(tested.Frame());
    ASSERT_FALSE(tested.scene->Failure().has_value()) << *tested.scene->Failure();
    EXPECT_EQ(cloth.random.State(), random);
}

// The rand() drawn from `before` to `after` (the CRT's LCG), up to 100000.
int DrawsBetween(Client::Crt::CrtRandom before, std::uint32_t after)
{
    for (int draws = 0; draws <= 100000; ++draws)
    {
        if (before.State() == after)
            return draws;
        static_cast<void>(before.Next());
    }
    return -1;
}

// 0x50cca9..0x50d211 in scene 4: the published hero (0x743a170, slot 0 since WinMain) opens and closes the own record
// for its color on each frame of the original, even though the scene then gives it 0.5 plus +0xf4 (0x50d219). With
// the record opened by another it does not close it; closed, 3848 more rand() in the draw.
TEST(CharacterSelectSceneTest, TheHeroOfTheListClosesTheOwnRecordForItsColor)
{
    using Client::Models::OriginalFrameTime;
    Client::Kernel::WorldState world;
    Client::FrontEnd::SelectionState selection(world);
    Client::Models::ClothWorld cloth;
    ASSERT_NO_FATAL_FAILURE(RetainLoginTerrain(selection));
    Harness tested(true, true, false, &selection, &world, &cloth);
    ASSERT_NO_FATAL_FAILURE(tested.ReachTheChoiceWith(TwoWingedCharacters()));
    ASSERT_EQ(world.Runtime().hero, 0u);
    const auto drawAt = [&tested](Client::Core::Duration clock)
    {
        auto& canvas = tested.target.BeginFrame(clock);
        tested.scene->Render(canvas, 0.0f);
        tested.target.Present();
    };
    drawAt(OriginalFrameTime);
    ASSERT_FALSE(tested.scene->Failure().has_value()) << *tested.scene->Failure();

    world.Runtime().ownRecordDepth = 1;
    const Client::Crt::CrtRandom beforeHeld = cloth.random;
    drawAt(OriginalFrameTime * 2);
    const int held = DrawsBetween(beforeHeld, cloth.random.State());
    EXPECT_EQ(world.Runtime().ownRecordDepth, 1);

    world.Runtime().ownRecordDepth = 0;
    const Client::Crt::CrtRandom beforeClosed = cloth.random;
    drawAt(OriginalFrameTime * 3);
    const int closed = DrawsBetween(beforeClosed, cloth.random.State());

    ASSERT_FALSE(tested.scene->Failure().has_value()) << *tested.scene->Failure();
    ASSERT_GE(held, 0);
    constexpr int Close = Client::Models::OwnRecordCloseRandoms;
    EXPECT_LT(held, Close);
    EXPECT_GE(closed, Close);
    // The 54's objects and effects take a few rand() more or less from one frame to another.
    EXPECT_NEAR(closed - held, Close, 16);
    EXPECT_EQ(world.Runtime().ownRecordDepth, 0);
}

// 0x510abd: the raven of a Dark Lord in the list (EquipSelectionActor) is drawn at the end of its 0x50ba30 (0x420df0
// → 0x4182f0), also in the selection, with its trapezoid: in a frame of the original it leaves its pose in the global
// buffer.
TEST(CharacterSelectSceneTest, TheRavenOfALordInTheListIsDrawnAfterHim)
{
    Client::Kernel::WorldState world;
    Client::FrontEnd::SelectionState selection(world);
    Client::Models::ClothWorld cloth;
    ASSERT_NO_FATAL_FAILURE(RetainLoginTerrain(selection));
    Harness tested(true, true, false, &selection, &world, &cloth);
    tested.models.EnableRaven();
    CharacterEntryFields lord{.slot = 4, .classByte = 0x80};
    lord.equipment[0] = 0xff;
    lord.equipment[11] = 0xf0;
    lord.equipment[1] = 5;
    lord.equipment[12] = 0xa0;
    lord.equipment[4] = 3;
    ASSERT_NO_FATAL_FAILURE(tested.ReachTheChoiceWith({lord}));
    ASSERT_TRUE(world.Actors()[4].darkSpirit.has_value());
    ASSERT_FALSE(world.PoseCarry().models.contains(Client::Pets::DarkSpiritModel));
    // It spawns at the center of cell (0, 0), out of view, and flies toward its owner: here it has arrived.
    world.Actors()[4].darkSpirit->position = world.Actors()[4].position + glm::vec3(0.0f, 0.0f, 250.0f);

    static_cast<void>(tested.Frame());

    ASSERT_FALSE(tested.scene->Failure().has_value()) << *tested.scene->Failure();
    EXPECT_TRUE(world.PoseCarry().models.contains(Client::Pets::DarkSpiritModel));
    EXPECT_FALSE(world.Actors()[4].darkSpirit->pose.empty());
}

// Q-C410: 0x63e3e0 builds the preview with 0x511290 (0x63e409), which sets its +0x11c to 1 and gives it its buffer
// (+0x120); 0x63e637 → 0x50ba30 → 0x602e90 → 0x5ffdf0 poses it there (0x5fff13..0x5fff4c). It does not reach the
// global one, and an intermediate draw, which interpolates another pose, does not touch its own.
TEST(CharacterSelectSceneTest, ThePreviewGoesToItsOwnBonesAndNeverToTheGlobalPose)
{
    Client::Kernel::WorldState world;
    Client::FrontEnd::SelectionState selection(world);
    Client::Models::ClothWorld cloth;
    ASSERT_NO_FATAL_FAILURE(RetainLoginTerrain(selection));
    Harness tested(true, false, false, &selection, &world, &cloth);
    ASSERT_NO_FATAL_FAILURE(WriteNewFace(tested, 1));
    ASSERT_NO_FATAL_FAILURE(tested.ReachTheChoiceWith({}));
    tested.ClickAt(CreateButton);
    Client::Kernel::ActorState& preview = selection.PreviewActor();
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

// 0x63e637 → 0x50ba30 → 0x5ffdf0 (0x50baed) and 0x602e90 (0x50bd81), always with the second argument at 1: 0x4eb860
// with the flag at 0 leaves in +0x120 the bones in the model, without the class's scale (6.05 with the 1) or the
// position, and 0x4ebc80 runs with Translate at 1: the per-vertex light (0x4ecc30 with +0x44) takes each normal only
// rotated by its bone, unscaled.
TEST(CharacterSelectSceneTest, ThePreviewPosesInItsModelAndLightsItsUnscaledNormals)
{
    Client::Kernel::WorldState world;
    Client::FrontEnd::SelectionState selection(world);
    Client::Models::ClothWorld cloth;
    ASSERT_NO_FATAL_FAILURE(RetainLoginTerrain(selection));
    Harness tested(true, false, false, &selection, &world, &cloth);
    ASSERT_NO_FATAL_FAILURE(WriteNewFace(tested, 1));
    ASSERT_NO_FATAL_FAILURE(tested.ReachTheChoiceWith({}));
    tested.ClickAt(CreateButton);
    Client::Kernel::ActorState& preview = selection.PreviewActor();
    preview.bodyPose.clear();

    const std::vector<FrameEvent> passes = ModelPasses(tested.Frame());

    ASSERT_EQ(tested.scene->Failure(), std::nullopt);
    ASSERT_EQ(preview.scale, 6.05f);
    const auto drawn = std::ranges::find_if(passes, [](const FrameEvent& pass)
                                            { return pass.pass.has_value() && pass.pass->viewport.z == 410; });
    ASSERT_NE(drawn, passes.end());
    // The NewFace's two bones that are not dummy, without the scale: each column of the rotation, of length 1.
    ASSERT_GE(preview.bodyPose.size(), 3u);
    for (std::size_t bone = 0; bone < 2; ++bone)
        for (int column = 0; column < 3; ++column)
            EXPECT_NEAR(glm::length(glm::vec3(preview.bodyPose[bone][column])), 1.0f, 1e-5f) << bone << ", " << column;
    // The selection draws the NewFace02.bmd of the repo's Data. Each corner of its mesh 0 goes with the model's color
    // times the light of its normal rotated by those bones (SkinMesh, unscaled): the same base in all.
    std::ifstream file(Client::Tests::RepoDataDir() / "Logo" / "NewFace02.bmd", std::ios::binary);
    const std::vector<char> raw{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    const auto face = Client::Data::Models::LoadBmd(std::as_bytes(std::span(raw)));
    ASSERT_TRUE(face.HasValue());
    ASSERT_FALSE(face.Value().meshes.empty());
    const Client::Data::Models::Mesh& mesh = face.Value().meshes[0];
    Client::Models::SkinnedMesh unscaled;
    ASSERT_TRUE(Client::Models::SkinMesh(mesh, preview.bodyPose, {}, unscaled));
    ASSERT_FALSE(mesh.triangles.empty());
    ASSERT_GE(drawn->modelVertices.size(), 3 * mesh.triangles.size());
    const auto lightOf = [&](std::size_t triangle, std::size_t corner)
    {
        const auto normal = static_cast<std::size_t>(mesh.triangles[triangle].normals[corner]);
        return Client::Models::NormalLight(unscaled.normals[normal]);
    };
    const float base = drawn->modelVertices[0].color.red / lightOf(0, 0);
    for (std::size_t triangle = 0; triangle < mesh.triangles.size(); ++triangle)
        for (std::size_t corner = 0; corner < 3; ++corner)
        {
            const float expected = base * lightOf(triangle, corner);
            EXPECT_NEAR(drawn->modelVertices[3 * triangle + corner].color.red, expected, 1e-5f * expected)
                << triangle << ", " << corner;
        }
}

// 0x6347e9..0x6348dc: with world 54, the chosen living one takes its light to 1 and adds it to the dynamic buffer at
// its place (0x5f9780, radius 1) before the terrain; after the characters, its light stays at 0.15 and it lends the
// two 0x729 (/0 and /1) of scale 1 and without owner. A MuRet intermediate draw (without a frame of the original)
// does not change the step's pool.
TEST(CharacterSelectSceneTest, TheChosenOneGlowsAndRaisesItsTwoChaseLightsOnNativeFrames)
{
    Client::Kernel::WorldState world;
    Client::FrontEnd::SelectionState selection(world);
    Client::Models::ClothWorld cloth;
    ASSERT_NO_FATAL_FAILURE(RetainLoginTerrain(selection));
    Harness tested(true, true, false, &selection, &world, &cloth);
    ASSERT_NO_FATAL_FAILURE(tested.ReachTheChoiceWith(TwoWingedCharacters()));
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
        std::vector<const Client::Particles::Particle*> found;
        for (const auto& particle : world.Particles().All())
        {
            if (particle.alive && particle.type == Client::Particles::ChaseLightParticle)
                found.push_back(&particle);
        }
        return found;
    };
    const auto lights = chaseLights();
    ASSERT_EQ(lights.size(), 2u);
    EXPECT_EQ(lights[0]->subtype, 0);
    EXPECT_EQ(lights[1]->subtype, 1);
    EXPECT_EQ(lights[1]->texture, Client::Particles::ChaseLightImpactTexture);
    for (const auto* light : lights)
    {
        EXPECT_EQ(light->light, glm::vec3(std::bit_cast<float>(0x3e19999au)));
        EXPECT_EQ(light->typedOwner, Client::Particles::ParticleOwner{});
    }
    const auto cellX = static_cast<std::uint8_t>(chosen.position.x / 100.0f);
    const auto cellY = static_cast<std::uint8_t>(chosen.position.y / 100.0f);
    const auto& lit =
        tested.states.terrain.dynamicLight.Lighting().cells[Client::Data::Terrain::CellIndex(cellX, cellY)];
    EXPECT_GT(lit[0], 0.0f);

    // The same clock: an intermediate draw on the copies.
    static_cast<void>(tested.Frame());
    ASSERT_FALSE(tested.scene->Failure().has_value()) << *tested.scene->Failure();
    EXPECT_EQ(chaseLights().size(), 2u);
}

// The F1 02 the client sent, without the XOR32 (the account's F1 01 goes before).
std::vector<Client::Network::Packet> LogoutsSent(const Harness& tested)
{
    Client::Network::PacketSerials serials;
    std::vector<Client::Network::Packet> found;
    for (const Client::Network::Packet& packet :
         Client::Tests::AsTheGameServerReceives(tested.online.socket.sent, serials).packets)
    {
        if (packet.Opcode() != Client::Network::Opcode{0xF1})
        {
            continue;
        }
        Client::Network::Packet clear = Client::Network::Xor32Decode(packet);
        if (clear.Bytes().size() > 3 && clear.Bytes()[3] == std::byte{0x02})
        {
            found.push_back(std::move(clear));
        }
    }
    return found;
}

bool DrawsTheSlot(const std::vector<FrameEvent>& events, TextureSlot slot)
{
    return std::ranges::find(events, std::optional<TextureSlot>{slot}, &FrameEvent::texture) != events.end();
}

// The selection's Esc window: Esc opens it with the scene free (0x6344ce), and with it open Enter does not enter
// (0x634341). Its 1 (0x647e08) sends the C3 F1 02 02 with two CRT rand() (0x648241 and the serial of 0x5767913,
// Models/LogicalAccess) and closes the Esc window and the bar. A click on the frame, outside the buttons, does
// nothing. Going back to the list, with the GS reply, belongs to CL9 (Q-C79).
TEST(CharacterSelectSceneTest, TheEscapeWindowAsksToJoinAnotherServer)
{
    static constexpr TextureSlot EscapeButtons{0x72f};
    constexpr TextureSlot Bar{0xf};
    const auto openTheWindow = [](Harness& tested)
    {
        ASSERT_NO_FATAL_FAILURE(tested.ReachTheChoice());
        tested.ClickAt(tested.OnScreen(3));
        ASSERT_NE(tested.online.session.ChosenCharacter(), nullptr);
        tested.Tap(Key::Escape);
        ASSERT_TRUE(DrawsTheSlot(tested.Frame(), EscapeButtons));
        tested.Tap(Key::Enter);
        ASSERT_EQ(tested.scene->Next(), std::nullopt);
    };
    const auto layoutOf = [](const Harness& tested)
    {
        const auto line = tested.store.Find(TextureSlot{0x72d});
        EXPECT_TRUE(line.has_value());
        return Client::FrontEnd::LayoutEscapeWindow(PixelSize{640, 480}, 10, line ? line->texture.textureHeight : 1);
    };
    Client::Models::ClothWorld joinedCloth;
    Harness joined(true, false, false, nullptr, nullptr, &joinedCloth);
    ASSERT_NO_FATAL_FAILURE(openTheWindow(joined));
    Client::Models::ClothWorld stayedCloth;
    Harness stayed(true, false, false, nullptr, nullptr, &stayedCloth);
    ASSERT_NO_FATAL_FAILURE(openTheWindow(stayed));
    const Client::FrontEnd::EscapeWindowLayout layout = layoutOf(joined);
    const Client::Ui::Rect server = layout.buttons[1];

    joined.ClickAt({static_cast<float>(server.x + 5), static_cast<float>(server.y + 5)});
    stayed.ClickAt({static_cast<float>(layout.frame.x + 2), static_cast<float>(layout.frame.y + 2)});

    const std::vector<Client::Network::Packet> sent = LogoutsSent(joined);
    ASSERT_EQ(sent.size(), 1u);
    EXPECT_TRUE(Client::Tests::SameBytes(sent[0].Bytes(), Client::Tests::Bytes({0xC1, 0x05, 0xF1, 0x02, 0x02})));
    EXPECT_TRUE(LogoutsSent(stayed).empty());
    EXPECT_EQ(DrawsBetween(stayedCloth.random, joinedCloth.random.State()), 2);
    const std::vector<FrameEvent> left = joined.Frame();
    EXPECT_FALSE(DrawsTheSlot(left, EscapeButtons));
    EXPECT_FALSE(DrawsTheSlot(left, Bar));
    const std::vector<FrameEvent> open = stayed.Frame();
    EXPECT_TRUE(DrawsTheSlot(open, EscapeButtons));
    EXPECT_TRUE(DrawsTheSlot(open, Bar));
    EXPECT_EQ(joined.online.session.State(), SessionState::ChoosingCharacter);
    EXPECT_EQ(joined.scene->Next(), std::nullopt);
}

// The selection's options (0x647e75, in mode 4 between the Esc window and the bar): the Esc window's 2 opens them on
// top. With them open, Enter does not enter (0x634341, +0x7f4). The volume rises with a click to the right of its
// knob (0x6447d9), and Esc closes them without opening the Esc window again.
TEST(CharacterSelectSceneTest, TheOptionsOpenFromTheEscapeWindow)
{
    static constexpr TextureSlot EscapeButtons{0x72f};
    static constexpr TextureSlot Checks{0x731};
    Client::Kernel::WorldState world;
    Harness tested(true, false, false, nullptr, &world);
    ASSERT_NO_FATAL_FAILURE(tested.ReachTheChoice());
    tested.ClickAt(tested.OnScreen(3));
    ASSERT_NE(tested.online.session.ChosenCharacter(), nullptr);
    const auto line = tested.store.Find(TextureSlot{0x72d});
    ASSERT_TRUE(line.has_value());
    const int lineHeight = line->texture.textureHeight;
    const Client::FrontEnd::EscapeWindowLayout escape =
        Client::FrontEnd::LayoutEscapeWindow(PixelSize{640, 480}, 10, lineHeight);
    const Client::FrontEnd::OptionsWindowLayout options =
        Client::FrontEnd::LayoutOptionsWindow(PixelSize{640, 480}, lineHeight);

    tested.Tap(Key::Escape);
    const Client::Ui::Rect button = escape.buttons[2];
    tested.ClickAt({static_cast<float>(button.x + 5), static_cast<float>(button.y + 5)});
    // The three checkboxes and the close button, from op1_b_all like the Esc window's, which are no longer drawn.
    std::vector<FrameEvent> frame = tested.Frame();
    EXPECT_EQ(std::ranges::count(frame, std::optional<TextureSlot>{Checks}, &FrameEvent::texture), 3);
    EXPECT_EQ(std::ranges::count(frame, std::optional<TextureSlot>{EscapeButtons}, &FrameEvent::texture), 1);
    tested.Tap(Key::Enter);
    EXPECT_EQ(tested.scene->Next(), std::nullopt);

    ASSERT_EQ(world.Runtime().ui.volume, 5);
    const Client::Ui::Point volume = options.sliders[0];
    tested.ClickAt({static_cast<float>(volume.x + 90), static_cast<float>(volume.y + 5)});
    EXPECT_EQ(world.Runtime().ui.volume, 6);

    tested.Tap(Key::Escape);
    frame = tested.Frame();
    EXPECT_FALSE(DrawsTheSlot(frame, Checks));
    EXPECT_FALSE(DrawsTheSlot(frame, EscapeButtons));
}
} // namespace
