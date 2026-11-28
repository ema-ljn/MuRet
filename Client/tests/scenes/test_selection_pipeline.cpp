#include "FrontEnd/SelectionState.h"
#include "Session/SessionEventApplier.h"

#include "Session/ServerSession.h"
#include "Session/SessionState.h"
#include "Core/Expected.h"
#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Crt/CrtRandom.h"
#include "Tables/TextTable.h"
#include "Tables/PlayerModels.h"
#include "Input/InputState.h"
#include "Input/Key.h"
#include "Network/PacketSerials.h"
#include "Protocol/FrontEnd/CharacterDelete.h"
#include "Render/ModelBatch.h"
#include "Models/Animation.h"
#include "Models/Cloth.h"
#include "Render/Text/FontSet.h"
#include "Render/TextureSlot.h"
#include "Kernel/ActorState.h"
#include "Scenes/CharacterSelectScene.h"
#include "FrontEnd/CharacterSlots.h"
#include "Actors/FullSetScan.h"
#include "Tables/GameData.h"
#include "Scenes/GameDrawTail.h"
#include "Pets/HelperTable.h"
#include "Scenes/IScene.h"
#include "FrontEnd/LoginModels.h"
#include "Scenes/LoginScene.h"
#include "Scenes/SceneContext.h"
#include "Scenes/SceneId.h"
#include "Scenes/SceneManager.h"
#include "Scenes/WorldLoadingScene.h"
#include "Session/WorldEntry.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Ui/GameText.h"
#include "Camera/GameCamera.h"
#include "Effects/ModelEffects.h"
#include "Terrain/TerrainHeight.h"
#include "Terrain/WorldTerrain.h"
#include "Session/ModuleStates.h"
#include "Audio/SoundId.h"
#include "support/TestGameServer.h"

#include "support/Bytes.h"
#include "support/CharacterPackets.h"
#include "support/EffectTextures.h"
#include "support/FakeTextureStore.h"
#include "support/GoldenDir.h"
#include "support/PlayerActors.h"
#include "support/RecordingFrameTarget.h"
#include "support/RecordingSink.h"
#include "support/RecordingTextDrawer.h"
#include "support/SessionHarness.h"
#include "support/SessionPackets.h"
#include "support/UiInput.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// CL7.3 B: el cable falso atraviesa la sesión, el callback de Poll de main y las escenas reales.
// Sin un Step ni un Render entre recepciones, la fotografía de la UI no reemplaza los constructores.
namespace
{
using Client::App::SessionFailure;
using Client::App::SessionState;
using Client::Scenes::CharacterSlotHeight;
using Client::Scenes::CharacterSlots;
using Client::Scenes::SceneId;
using Client::Tests::CharacterEntryFields;

constexpr std::uint8_t LoginMap = 55;
constexpr int SelectionMap = 54;
constexpr std::uint8_t PreservedPetSlot = 4;
constexpr std::uint8_t PendingCreationSlot = 3;

CharacterEntryFields WithoutPet(std::uint8_t slot)
{
    CharacterEntryFields entry{.slot = slot};
    // 04 § La selección de personaje: b4 & 3 == 3, con b9 & 1 == 0, es petNONE.
    entry.equipment[4] = 3;
    return entry;
}

// Sin nada que cuelgue: sin mascota, sin alas (b4 >> 2 & 3 == 3, con b8 & 7 == 0) y con las manos vacías (0xfff:
// b0 y b1 en 0xff, y los nibbles altos de b11 y b12).
CharacterEntryFields Bare(std::uint8_t slot)
{
    CharacterEntryFields entry{.slot = slot};
    entry.equipment[0] = 0xff;
    entry.equipment[1] = 0xff;
    entry.equipment[4] = 0x0f;
    entry.equipment[11] = 0xf0;
    entry.equipment[12] = 0xf0;
    return entry;
}

CharacterEntryFields WithHeightOffsetPet(std::uint8_t slot)
{
    CharacterEntryFields entry{.slot = slot};
    // 04 § La selección de personaje: el equipo que AppearanceOf decodifica como pet 0x1c15.
    entry.equipment[4] = 3;
    entry.equipment[9] = 1;
    return entry;
}

std::vector<std::byte> CreatedReply(std::uint8_t slot)
{
    // 04 § La selección de personaje: F3/01 aceptado, nombre, slot, nivel 1 y clase 0.
    auto bytes = Client::Tests::Bytes({0xc1, 0x13, 0xf3, 0x01, 0x01});
    const auto name = Client::Tests::NameBytes("char0003");
    bytes.insert(bytes.end(), name.begin(), name.end());
    bytes.insert(bytes.end(), {std::byte{slot}, std::byte{1}, std::byte{0}, std::byte{0}});
    return bytes;
}

class SelectionPipelineTest : public Client::Tests::GoldenDataTest
{
protected:
    void SetUp() override
    {
        ASSERT_NO_FATAL_FAILURE(Client::Tests::GoldenDataTest::SetUp());
        auto data = Client::Tables::LoadGameData(m_dataRoot);
        ASSERT_TRUE(data.HasValue()) << data.Error();
        m_data = std::move(data).Value();
        m_models.Set(Client::Tables::PlayerSkeletonModel, {}, Client::Tests::PlayerActionsSkeleton(), {});
        auto terrain = Client::Terrain::LoadWorldTerrain(m_dataRoot, LoginMap);
        ASSERT_TRUE(terrain.HasValue()) << terrain.Error();
        m_loginConstructorHeight = static_cast<float>(Client::Terrain::TerrainHeightAt(
            *terrain.Value().heights, *terrain.Value().attributes, 50.0f, 50.0f, m_camera.fixedHeight));
        m_state.RetainTerrain(LoginMap, std::move(terrain).Value());
        m_cameraInput.map = LoginMap;
        ASSERT_NO_FATAL_FAILURE(m_online.LogIn());
        m_manager.emplace([this](SceneId id) { return MakeScene(id); }, m_target, m_online.clock,
                          [this] { return m_online.session.Poll(); }, m_online.log);
    }

    std::unique_ptr<Client::Scenes::IScene> MakeScene(SceneId id)
    {
        const Client::Scenes::SceneContext context{.dataRoot = m_dataRoot,
                                                   .textures = m_textures,
                                                   .log = m_online.log,
                                                   .clock = &m_online.clock,
                                                   .cloth = &m_cloth,
                                                   .camera = &m_camera,
                                                   .cameraInput = &m_cameraInput,
                                                   .selection = &m_state,
                                                   .world = &m_world,
                                                   .states = &m_states};
        if (id == SceneId::CharacterSelect)
        {
            return std::make_unique<Client::Scenes::CharacterSelectScene>(context, m_data, m_library,
                                                                          &m_online.session);
        }
        if (id == SceneId::Login)
        {
            return std::make_unique<Client::Scenes::LoginScene>(context, m_data, m_models, &m_online.session, "1.0", 1);
        }
        if (id == SceneId::WorldLoading)
        {
            return std::make_unique<Client::Scenes::WorldLoadingScene>(context);
        }
        return nullptr;
    }

    void StartSelect()
    {
        const auto started = m_manager->Start(SceneId::CharacterSelect);
        ASSERT_TRUE(started.HasValue()) << started.Error();
        ASSERT_EQ(m_online.session.State(), SessionState::WaitingCharacterList);
    }

    void ReceiveList(const std::vector<CharacterEntryFields>& entries)
    {
        m_online.socket.reads = {Client::Tests::CharacterListBytes(entries)};
        m_manager->Poll();
        ASSERT_EQ(m_online.session.State(), SessionState::ChoosingCharacter);
        ASSERT_TRUE(m_state.HasReceivedList());
    }

    Client::Tests::FakeTextureStore m_textures;
    Client::Tests::RecordingFrameTarget m_target;
    Client::Tables::GameData m_data;
    Client::Scenes::SingleModelLibrary m_models;
    // Con las texturas de efectos de GameModels, que piden las partículas del mundo 54.
    Client::Tests::WithEffectTextures m_library{m_models};
    Client::Camera::GameCamera m_camera;
    Client::Camera::GameCameraInput m_cameraInput;
    Client::Models::ClothWorld m_cloth;
    Client::Kernel::WorldState m_world;
    Client::Session::ModuleStates m_states;
    Client::Scenes::SelectionState m_state{m_world};
    Client::Tests::RecordingSink m_entrySink;
    Client::Core::Log m_entryLog{m_entrySink, Client::Core::LogLevel::Debug};
    Client::Scenes::SessionEventApplier m_applier{Client::Scenes::WorldEntryResources{.world = m_world,
                                                                                      .selection = m_state,
                                                                                      .dataRoot = {},
                                                                                      .cloth = m_cloth,
                                                                                      .camera = m_camera,
                                                                                      .cameraInput = m_cameraInput,
                                                                                      .models = m_models,
                                                                                      .log = m_entryLog}};
    Client::Tests::LoggedInSession m_online{&m_applier};
    float m_loginConstructorHeight = 0.0f;
    // Se destruye primero: Leave conserva las tablas antes de que desaparezcan el estado y el store.
    std::optional<Client::Scenes::SceneManager> m_manager;
};

TEST_F(SelectionPipelineTest, AListBeforeSelectionEnterIsOutOfTurnAndTheRequestedListIsApplied)
{
    // Antes de Enter todavía no se pidió F3/00: la sesión no acepta esa respuesta.
    m_online.socket.reads = {Client::Tests::CharacterListBytes({{0, "char0001"}})};
    m_manager->Poll();
    EXPECT_EQ(m_online.session.State(), SessionState::LoggedIn);
    EXPECT_FALSE(m_state.HasReceivedList());
    EXPECT_EQ(m_state.Slots()[0].revision, 0u);
    ASSERT_NO_FATAL_FAILURE(StartSelect());
    ASSERT_NO_FATAL_FAILURE(ReceiveList({WithoutPet(0)}));
    EXPECT_EQ(m_state.Slots()[0].revision, 1u);
    EXPECT_EQ(m_state.Map(), LoginMap);
    EXPECT_FALSE(m_state.IsInitialized());
    m_manager->Step(Client::Input::InputState{});
    EXPECT_TRUE(m_state.IsInitialized());
    EXPECT_EQ(m_state.Map(), SelectionMap);
}

TEST_F(SelectionPipelineTest, RestartingTheSessionHoldsSelectionUntilAnotherListExists)
{
    ASSERT_NO_FATAL_FAILURE(StartSelect());
    m_online.socket.reads = {Client::Tests::CharacterListBytes({WithoutPet(0)})};
    m_manager->Poll();
    m_manager->Step(Client::Input::InputState{});
    ASSERT_TRUE(m_state.IsInitialized());
    m_cameraInput.hero.z = 777.0f;
    m_online.session.Start();
    ASSERT_FALSE(m_online.session.Characters().has_value());
    EXPECT_TRUE(m_manager->HoldsTheLastFrame());
    m_manager->Render(0.0f);
    EXPECT_EQ(m_cameraInput.hero.z, 777.0f);
    EXPECT_FALSE(m_state.Failure().has_value());
}

TEST_F(SelectionPipelineTest, SelectionForwardsTheLastFireworkSound)
{
    ASSERT_NO_FATAL_FAILURE(StartSelect());
    ASSERT_NO_FATAL_FAILURE(ReceiveList({WithoutPet(0)}));
    m_manager->Step(Client::Input::InputState{});
    ASSERT_TRUE(m_state.IsInitialized());

    Client::World::ModelEffect& firework = m_world.Map().effects.Effects()[0];
    firework.alive = true;
    firework.type = 0x60d;
    firework.life = 1;
    firework.light = glm::vec3(1);
    firework.transform.position = {1000, 2000, 100};
    m_world.Runtime().lastSoundId.reset();

    m_manager->Step(Client::Input::InputState{});

    EXPECT_EQ(m_world.Runtime().lastSoundId, Client::Audio::SoundId{0x59});
    EXPECT_FALSE(firework.alive);
}

TEST_F(SelectionPipelineTest, MissingWorldTablesAbortTheManagerWithoutInventingAHeight)
{
    ASSERT_NO_FATAL_FAILURE(StartSelect());
    m_state.BorrowTerrain(LoginMap, nullptr);
    m_cameraInput.hero.z = 777.0f;
    m_online.socket.reads = {Client::Tests::CharacterListBytes({{0, "char0001"}})};
    m_manager->Poll();
    ASSERT_TRUE(m_state.Failure().has_value());
    EXPECT_EQ(std::ranges::count(m_online.sink.levels, Client::Core::LogLevel::Error), 1);
    EXPECT_EQ(m_online.session.Failure(), SessionFailure::ApplicationFailed);
    EXPECT_EQ(m_manager->Failure(), m_state.Failure());
    EXPECT_FALSE(m_state.HasReceivedList());
    EXPECT_EQ(m_state.Slots()[0].revision, 0u);
    EXPECT_EQ(m_cameraInput.hero.z, 777.0f);
    EXPECT_TRUE(m_manager->HoldsTheLastFrame());
    m_manager->Render(0.0f);
    EXPECT_EQ(m_cameraInput.hero.z, 777.0f);
}

// 0x634851 dibuja los personajes. 0x63486f dibuja las cintas antes de las juntas de 0x634875.
TEST_F(SelectionPipelineTest, TheSelectorDrawsTheLiveRibbonAfterCharactersAndBeforeJoints)
{
    ASSERT_NO_FATAL_FAILURE(StartSelect());
    ASSERT_NO_FATAL_FAILURE(ReceiveList({Bare(0)}));
    m_manager->Step(Client::Input::InputState{});
    ASSERT_TRUE(m_state.IsInitialized());
    ASSERT_EQ(m_state.Map(), SelectionMap);
    auto& ribbon = m_world.DrawTail().ribbons[0];
    ribbon.alive = true;
    ribbon.life = 3;
    ribbon.count = 3;
    m_online.clock.Advance(Client::Models::OriginalFrameTime);
    m_manager->Step(Client::Input::InputState{});
    // 0x6342f9 pasa por 0x5091d0. El preview de 0x634303 no vuelve a envejecer la cinta.
    ASSERT_EQ(ribbon.life, 2);
    ASSERT_EQ(ribbon.count, 2);
    // La escena publica la luz del personaje aunque su biblioteca no tenga mallas.
    m_models.Clear();

    auto& actor = m_world.Actors()[0];
    ribbon.ownerEquipment = &actor.level;
    ribbon.type = 3;
    ribbon.light = {1.0f, 0.5f, 0.25f};
    ribbon.first[0] = {1, 2, 3};
    ribbon.second[0] = {4, 2, 3};
    ribbon.first[1] = {1, 5, 3};
    ribbon.second[1] = {4, 5, 3};
    auto& joint = m_world.DrawTail().joints[0];
    joint.alive = true;
    joint.type = 0x60f;
    joint.texture = 0x60f;
    joint.count = 1;
    joint.maximumCount = 3;
    joint.drawEnabled = 1;
    joint.blend = 1;
    joint.light = glm::vec3(1.0f);
    joint.points[0][2] = {6, 2, 3};
    joint.points[0][3] = {9, 2, 3};
    joint.points[1][2] = {6, 5, 3};
    joint.points[1][3] = {9, 5, 3};

    m_manager->Render(0.0f);
    ASSERT_FALSE(m_manager->Failure().has_value()) << *m_manager->Failure();
    ASSERT_FALSE(m_target.frames.empty());
    std::vector<std::uint16_t> selectors;
    for (const auto& event : m_target.frames.back().events)
    {
        for (const auto& command : event.modelCommands)
        {
            if (command.vertexCount == 0 || !command.state.texture)
                continue;
            const auto& first = event.modelVertices.at(command.firstVertex).position;
            if (first == ribbon.first[0] || first == joint.points[0][2])
            {
                EXPECT_EQ(command.vertexCount, 6u);
                selectors.push_back(command.state.texture->value);
            }
        }
    }
    EXPECT_EQ(selectors, (std::vector<std::uint16_t>{0x639, 0x60f}));

    // Si la cinta corta, el personaje ya publicó su luz en este mismo dibujo.
    const glm::vec3 drawnLight = actor.drawnLight;
    actor.drawnLight = glm::vec3(-1.0f);
    ribbon.ownerEquipment = nullptr;
    m_online.clock.Advance(Client::Models::OriginalFrameTime);
    m_manager->Render(0.0f);
    ASSERT_TRUE(m_manager->Failure().has_value());
    EXPECT_NE(m_manager->Failure()->find("0x52e5bc"), std::string::npos);
    EXPECT_EQ(actor.drawnLight, drawnLight);
}

TEST_F(SelectionPipelineTest, PollConstructsBeforeLogicAndTheRenderGuardKeepsTheLoginTerrain)
{
    ASSERT_NO_FATAL_FAILURE(StartSelect());
    m_cameraInput.hero = {-1.0f, -2.0f, -3.0f};

    ASSERT_NO_FATAL_FAILURE(ReceiveList({WithoutPet(0)}));

    EXPECT_TRUE(m_target.frames.empty());
    EXPECT_EQ(m_state.Map(), LoginMap);
    EXPECT_EQ(m_cameraInput.map, LoginMap);
    EXPECT_FALSE(m_state.IsInitialized());
    EXPECT_EQ(m_state.Slots()[0].revision, 1u);
    const glm::vec3 constructed(CharacterSlots[0].position, m_loginConstructorHeight);
    EXPECT_EQ(m_state.Slots()[0].position, constructed);
    EXPECT_EQ(m_cameraInput.hero, constructed);
    ASSERT_NE(m_loginConstructorHeight, CharacterSlotHeight);

    m_manager->Render(0.0f);

    ASSERT_EQ(m_target.frames.size(), 1u);
    EXPECT_TRUE(m_target.frames.back().events.empty());
    EXPECT_EQ(m_state.Slots()[0].position, constructed);
    EXPECT_EQ(m_cameraInput.hero, constructed);
    EXPECT_EQ(m_state.Map(), LoginMap);
    EXPECT_FALSE(m_state.IsInitialized());

    m_manager->Step(Client::Input::InputState{});

    EXPECT_TRUE(m_state.IsInitialized());
    EXPECT_EQ(m_state.Map(), SelectionMap);
    EXPECT_EQ(m_cameraInput.map, SelectionMap);
    EXPECT_EQ(m_state.Slots()[0].position, constructed);
    m_manager->Render(0.0f);
    EXPECT_EQ(m_cameraInput.hero.z, CharacterSlotHeight);
    EXPECT_EQ(m_state.Slots()[0].position.z, CharacterSlotHeight);
}

TEST_F(SelectionPipelineTest, RecreatingAndDeletingBeforeAStepSurvivesTheRealLoginTransition)
{
    ASSERT_NO_FATAL_FAILURE(StartSelect());
    ASSERT_NO_FATAL_FAILURE(ReceiveList({WithHeightOffsetPet(0), WithHeightOffsetPet(PreservedPetSlot)}));
    // El historial observado por el dibujo precede a estos callbacks. No se finge haber pasado el
    // cuerpo de helpers de la escena 4, todavía no portado: su corte se prueba por el manager abajo.
    m_state.DrawSelection(m_cameraInput);
    ASSERT_EQ(m_cameraInput.hero.z, CharacterSlotHeight);
    ASSERT_TRUE(m_state.Slots()[PreservedPetSlot].previousHad1c15);

    m_online.session.CreateCharacter("char0003", 0);
    m_online.socket.reads = {CreatedReply(0)};
    m_manager->Poll();
    ASSERT_EQ(m_online.session.State(), SessionState::ChoosingCharacter);
    ASSERT_EQ(m_state.Slots()[0].revision, 2u);
    ASSERT_NE(m_state.Slots()[0].position.z, CharacterSlotHeight);
    EXPECT_EQ(m_cameraInput.hero, m_state.Slots()[0].position);
    EXPECT_FALSE(m_state.Slots()[0].previousHad1c15);
    const glm::vec3 constructed = m_state.Slots()[0].position;

    m_online.session.ChooseCharacter(std::uint8_t{0});
    m_online.session.DeleteCharacter(Client::Protocol::FrontEnd::DeleteCodeFromText("1234"));
    m_online.socket.reads = {Client::Tests::Bytes({0xc1, 0x05, 0xf3, 0x02, 0x01})};
    m_manager->Poll();
    ASSERT_EQ(m_online.session.State(), SessionState::ChoosingCharacter);
    ASSERT_TRUE(m_online.session.Characters().has_value());
    ASSERT_EQ(m_online.session.Characters()->characters.size(), 1u);
    EXPECT_EQ(m_online.session.Characters()->characters.front().slot, PreservedPetSlot);
    EXPECT_EQ(m_state.Slots()[0].revision, 2u);
    EXPECT_EQ(m_state.Slots()[0].position, constructed);

    m_manager->Request(SceneId::Login);

    ASSERT_EQ(m_manager->Current(), SceneId::Login);
    ASSERT_FALSE(m_manager->Failure().has_value());
    EXPECT_EQ(m_online.session.State(), SessionState::ConnectingConnectServer);
    EXPECT_FALSE(m_online.session.Characters().has_value());
    EXPECT_EQ(m_state.Slots()[0].revision, 2u);
    EXPECT_EQ(m_state.Slots()[0].position, constructed);
    EXPECT_EQ(m_cameraInput.hero, constructed);
    EXPECT_TRUE(m_state.Slots()[PreservedPetSlot].previousHad1c15);
    m_manager->Poll();
    EXPECT_EQ(m_state.Slots()[0].revision, 2u);
    EXPECT_EQ(m_cameraInput.hero, constructed);
}

TEST_F(SelectionPipelineTest, AListPetReachesTheHelperBoundaryBeforeActorsParticlesAndCloth)
{
    constexpr int SelectionItemResetRandoms = 3848; // 0x633f91 -> 0x5bc092, antes del reset parcial.
    ASSERT_NO_FATAL_FAILURE(StartSelect());
    ASSERT_NO_FATAL_FAILURE(ReceiveList({WithHeightOffsetPet(0), WithHeightOffsetPet(PreservedPetSlot)}));
    auto& helpers = m_world.Helpers();
    ASSERT_TRUE(helpers[0].alive);
    ASSERT_TRUE(helpers[1].alive);
    ASSERT_EQ(helpers[0].type, 0x110);
    ASSERT_EQ(helpers[0].horse.alpha, 0.0f);
    constexpr std::uint32_t Seed = 16;
    constexpr int Counter = 41;
    m_cloth.random = Client::Crt::CrtRandom(Seed);
    m_cloth.logicSteps = Counter;
    const auto wind = m_cloth.wind;
    const auto animation = m_world.Actors()[0].animation;

    m_manager->Step(Client::Input::InputState{});

    ASSERT_TRUE(m_manager->Failure().has_value());
    EXPECT_NE(m_manager->Failure()->find("0x6342f4"), std::string::npos);
    EXPECT_TRUE(m_manager->HoldsTheLastFrame());
    EXPECT_TRUE(m_state.IsInitialized());
    EXPECT_FLOAT_EQ(helpers[0].horse.alpha, 0.1f);
    EXPECT_FLOAT_EQ(helpers[1].horse.alpha, 0.0f);
    EXPECT_EQ(m_world.Actors()[0].animation.frame, animation.frame);
    // Pasto de la carga del 54, cierre del vaciado de ítems y luego el reset parcial.
    EXPECT_EQ(m_cloth.random.State(),
              Client::Tests::AfterDraws(Seed, 3904 + SelectionItemResetRandoms + 65536).State());
    EXPECT_EQ(m_cloth.logicSteps, Counter);
    EXPECT_EQ(m_cloth.wind, wind);

    m_manager->Step(Client::Input::InputState{});
    EXPECT_FLOAT_EQ(helpers[0].horse.alpha, 0.1f);
    EXPECT_EQ(m_cloth.random.State(),
              Client::Tests::AfterDraws(Seed, 3904 + SelectionItemResetRandoms + 65536).State());
}

TEST_F(SelectionPipelineTest, ANewLoginPreservesAlreadyAppliedEventsAndTheConstructedPool)
{
    ASSERT_NO_FATAL_FAILURE(StartSelect());
    ASSERT_NO_FATAL_FAILURE(ReceiveList({WithHeightOffsetPet(0)}));
    const glm::vec3 constructed = m_state.Slots()[0].position;
    ASSERT_TRUE(m_state.Slots()[0].previousHad1c15);
    m_online.session.CreateCharacter("char0003", 0);
    m_online.socket.reads = {CreatedReply(PendingCreationSlot)};
    // El handler aplica inmediatamente, incluso sin pasar por el callback del manager.
    static_cast<void>(m_online.session.Poll());
    ASSERT_EQ(m_online.session.State(), SessionState::ChoosingCharacter);
    ASSERT_EQ(m_online.session.Characters()->characters.size(), 2u);
    ASSERT_EQ(m_state.Slots()[PendingCreationSlot].revision, 1u);

    m_manager->Request(SceneId::Login);
    ASSERT_EQ(m_manager->Current(), SceneId::Login);
    ASSERT_FALSE(m_manager->Failure().has_value());
    m_manager->Poll();

    EXPECT_EQ(m_state.Slots()[PendingCreationSlot].revision, 1u);
    EXPECT_EQ(m_state.Slots()[0].revision, 1u);
    EXPECT_EQ(m_state.Slots()[0].position, constructed);
    EXPECT_EQ(m_cameraInput.hero, constructed);
    EXPECT_TRUE(m_state.Slots()[0].previousHad1c15);
    EXPECT_TRUE(m_online.eventSink.characters.empty()) << "la sesión usa el aplicador real";
}

TEST_F(SelectionPipelineTest, TwoIdenticalAcceptedCreationsApplyBeforeTheSceneReadsTheSnapshot)
{
    ASSERT_NO_FATAL_FAILURE(StartSelect());
    ASSERT_NO_FATAL_FAILURE(ReceiveList({}));
    for (int reception = 0; reception < 2; ++reception)
    {
        // Cada respuesta tiene su pedido válido y aplica antes de consumir la vista UI.
        m_online.session.CreateCharacter("char0003", 0);
        ASSERT_EQ(m_online.session.State(), SessionState::WaitingCreateResult);
        m_online.socket.reads = {CreatedReply(0)};
        static_cast<void>(m_online.session.Poll());
        ASSERT_EQ(m_online.session.State(), SessionState::ChoosingCharacter);
    }
    ASSERT_EQ(m_online.session.Characters()->characters.size(), 1u);
    ASSERT_EQ(m_state.Slots()[0].revision, 2u);
    ASSERT_TRUE(m_target.frames.empty());

    m_manager->Poll();

    EXPECT_EQ(m_state.Slots()[0].revision, 2u);
    EXPECT_EQ(m_state.Slots()[0].position, (glm::vec3(CharacterSlots[0].position, m_loginConstructorHeight)));
    EXPECT_EQ(m_cameraInput.hero, m_state.Slots()[0].position);
    EXPECT_EQ(m_state.Map(), LoginMap);
    EXPECT_FALSE(m_state.IsInitialized());
    m_manager->Poll();
    EXPECT_EQ(m_state.Slots()[0].revision, 2u);
    m_manager->Step(Client::Input::InputState{});
    EXPECT_EQ(m_state.Slots()[0].revision, 2u);
}

// 0x633f20 deja el 54 como mundo en curso (06, "Al entrar"), en el WorldRuntime del pool durable. La
// escena 3 corre con él: entrar al mundo no lo vuelve a −1 ni toca los registros de la selección. Y
// deja armada la espera (06, "La entrada al mundo", pasos 1 y 2): el mundo cargado (0x633f30), el
// nivel del elegido en el registro propio, el contador en 9.999.999 y la bandera de la escena 3.
TEST_F(SelectionPipelineTest, TheLiveMapIsFiftyFourOnTheSelectionAndStaysForTheWorldLoading)
{
    ASSERT_NO_FATAL_FAILURE(StartSelect());
    ASSERT_NO_FATAL_FAILURE(ReceiveList({WithoutPet(0)}));
    EXPECT_EQ(m_world.Runtime().map, LoginMap);

    m_manager->Step(Client::Input::InputState{});

    ASSERT_TRUE(m_state.IsInitialized());
    EXPECT_EQ(m_world.Runtime().map, SelectionMap);
    const glm::vec3 constructed = m_world.Actors()[0].position;
    m_online.session.ChooseCharacter(std::uint8_t{0});
    Client::Input::InputState enter;
    Client::Tests::TapKey(enter, Client::Input::Key::Enter);

    m_manager->Step(enter);

    ASSERT_EQ(m_manager->Current(), SceneId::WorldLoading);
    ASSERT_FALSE(m_manager->Failure().has_value());
    EXPECT_EQ(m_world.Runtime().map, SelectionMap);
    EXPECT_EQ(m_world.Actors()[0].revision, 1u);
    EXPECT_EQ(m_world.Actors()[0].position, constructed);
    EXPECT_TRUE(m_world.Runtime().drawingEnabled);
    EXPECT_EQ(m_world.Runtime().warpCounter, Client::Kernel::WorldLoadingWait);
    EXPECT_EQ(m_world.Runtime().loadingOnceFlag, 1);
    EXPECT_EQ(m_world.OwnCharacter().level, m_online.session.ChosenCharacter()->level);
}

TEST_F(SelectionPipelineTest, TheCallbackAppliesAnAcceptedReceptionEvenWhenTheSamePollClosesTheSession)
{
    ASSERT_NO_FATAL_FAILURE(StartSelect());
    m_online.socket.reads = {Client::Tests::CharacterListBytes({{0, "char0001"}})};
    m_online.socket.peerClosed = true;

    m_manager->Poll();

    ASSERT_EQ(m_online.session.State(), SessionState::Failed);
    EXPECT_EQ(m_online.session.Failure(), SessionFailure::GameServerLost);
    EXPECT_FALSE(m_state.Failure().has_value());
    EXPECT_TRUE(m_state.HasReceivedList());
    EXPECT_EQ(m_state.Map(), LoginMap);
    EXPECT_FALSE(m_state.IsInitialized());
    EXPECT_EQ(m_state.Slots()[0].revision, 1u);
    const glm::vec3 constructed(CharacterSlots[0].position, m_loginConstructorHeight);
    EXPECT_EQ(m_state.Slots()[0].position, constructed);
    EXPECT_EQ(m_cameraInput.hero, constructed);
    EXPECT_TRUE(m_target.frames.empty());
    m_manager->Poll();
    EXPECT_EQ(m_state.Slots()[0].revision, 1u);
    EXPECT_EQ(m_state.Slots()[0].position, constructed);
}
std::vector<std::byte> UnplaceableInventory()
{
    return Client::Tests::Bytes({0xc2, 0x00, 0x0e, 0xf3, 0x10, 0x01, 0x0c, 0x00, 0x00, 0x14, 0x00, 0x00, 0xe0, 0x00});
}

// Un F3 10 con un ítem en la grilla (el lugar 12, Apple) sigue siendo trabajo: sin Item.bmd el aplicador
// local corta en 0x5bc740, después de la inicialización común, sin acumularlo entre frames.
TEST_F(SelectionPipelineTest, MixedCharacterThenWorldAppliesTheCharacterBeforeTheWorldFailure)
{
    ASSERT_NO_FATAL_FAILURE(StartSelect());
    Client::Network::PacketSerials serials;
    const auto inventory = Client::Tests::FromTheGameServer(UnplaceableInventory(), serials);
    m_online.socket.reads = {Client::Tests::Join({Client::Tests::CharacterListBytes({{0, "char0001"}}), inventory})};
    m_manager->Poll();
    EXPECT_EQ(m_online.session.Failure(), SessionFailure::ApplicationFailed);
    EXPECT_TRUE(m_manager->Failure().has_value());
    EXPECT_EQ(m_state.Slots()[0].revision, 1u);
    EXPECT_TRUE(m_state.HasReceivedList());
    m_manager->Step(Client::Input::InputState{});
    m_manager->Render(0.0f);
    EXPECT_EQ(m_state.Slots()[0].revision, 1u);
    EXPECT_TRUE(m_target.frames.empty());
}

TEST_F(SelectionPipelineTest, MixedWorldFailureBeforeCharacterPreventsTheCharacterConstructor)
{
    ASSERT_NO_FATAL_FAILURE(StartSelect());
    Client::Network::PacketSerials serials;
    const auto inventory = Client::Tests::FromTheGameServer(UnplaceableInventory(), serials);
    m_online.socket.reads = {Client::Tests::Join({inventory, Client::Tests::CharacterListBytes({{0, "char0001"}})})};
    m_manager->Poll();
    EXPECT_EQ(m_online.session.Failure(), SessionFailure::ApplicationFailed);
    EXPECT_TRUE(m_manager->Failure().has_value());
    EXPECT_EQ(m_state.Slots()[0].revision, 0u);
    EXPECT_FALSE(m_state.HasReceivedList());
    EXPECT_FALSE(m_online.session.Characters().has_value());
    EXPECT_TRUE(m_target.frames.empty());
}

// 0x50ba30 con cada personaje de la lista, en 0x50c91e, una vez por frame del original: el dibujo de la
// selección deja en WorldRuntime::fullSetScan lo de 0x505820 con el último, aunque MuRet no supiera qué
// había (06, "El conjunto completo de 0x505820"). Uno sin equipo lleva las partes de la clase, que no son un
// conjunto: [0x743a178] en 0 y [0x743a17c] en 1. Sin las acciones de Player.bmd sale antes (0x50ba87). Sin nada
// que dibuje mallas: las manos FFF conservan1211 (Q-C102), así que también necesitan los huesos de sus anclajes.
TEST_F(SelectionPipelineTest, TheSelectionDrawingLeavesTheScanOfTheLastCharacter)
{
    m_world.Runtime().fullSetScan = std::nullopt;
    ASSERT_NO_FATAL_FAILURE(StartSelect());
    ASSERT_NO_FATAL_FAILURE(ReceiveList({Bare(0)}));
    m_manager->Step(Client::Input::InputState{});
    // El paso común ya necesitó el esqueleto: se retira solo para observar la guarda del dibujo.
    m_models.Clear();
    m_manager->Render(0.0f);
    EXPECT_FALSE(m_world.Runtime().fullSetScan.has_value());

    // Un frame del original más: el del dibujo sin el esqueleto se perdió.
    auto skeleton = Client::Tests::PlayerActionsSkeleton();
    for (auto& action : skeleton.actions)
        action.keyCount = 1;
    skeleton.bones.resize(64);
    for (auto& bone : skeleton.bones)
    {
        bone.actions.resize(skeleton.actions.size());
        for (auto& keys : bone.actions)
            keys.resize(1);
    }
    m_models.Set(Client::Tables::PlayerSkeletonModel, {}, std::move(skeleton), {});
    m_online.clock.Advance(Client::Models::OriginalFrameTime);
    m_manager->Render(0.0f);

    ASSERT_FALSE(m_manager->Failure().has_value()) << *m_manager->Failure();
    ASSERT_TRUE(m_world.Runtime().fullSetScan.has_value());
    EXPECT_EQ(m_world.Runtime().fullSetScan->dword743a178, 0);
    EXPECT_EQ(m_world.Runtime().fullSetScan->byte743a17c, 1);
}

// 06 § Los globos: con la lista, 0x65f4b0 arma los globos (0x63ce70) y los pone al día (0x63cfb0), y 0x65fe00 los
// dibuja antes de las ventanas (0x63cf80): uno por personaje, con el nombre, el rango (0xff: el 488) y la clase con el
// nivel, centrados en la misma caja de 118 columnas, a 16 de distancia en 640 × 480.
TEST_F(SelectionPipelineTest, EachListedCharacterGetsItsBalloonBeforeTheWindows)
{
    ASSERT_NO_FATAL_FAILURE(StartSelect());
    CharacterEntryFields first = Bare(1);
    first.name = "globo1";
    first.level = 37;
    CharacterEntryFields second = Bare(3);
    second.name = "globo3";
    ASSERT_NO_FATAL_FAILURE(ReceiveList({first, second}));
    m_manager->Step(Client::Input::InputState{});
    ASSERT_FALSE(m_manager->Failure().has_value()) << *m_manager->Failure();
    // Sin las mallas: el esqueleto del test no tiene los huesos de la cola de 0x50ba30, y los globos no los miran.
    m_models.Clear();
    m_manager->Render(0.0f);
    ASSERT_FALSE(m_manager->Failure().has_value()) << *m_manager->Failure();

    const Client::Tables::TextTable* texts = m_data.text ? &*m_data.text : nullptr;
    const std::string guild = "(" + std::string(Client::UI::TextAt(texts, 488).value_or("")) + ")";
    const std::string wizard(Client::UI::TextAt(texts, 20).value_or(""));
    ASSERT_FALSE(m_target.frames.empty());
    const std::vector<Client::Tests::FrameEvent>& events = m_target.frames.back().events;
    const auto indexOf = [&events](const auto& predicate)
    { return static_cast<std::size_t>(std::distance(events.begin(), std::ranges::find_if(events, predicate))); };
    const auto quadOf = [](std::uint16_t slot)
    {
        return [slot](const Client::Tests::FrameEvent& event) {
            return event.kind == Client::Tests::FrameEvent::Kind::Quad && event.texture && event.texture->value == slot;
        };
    };
    // La barra empieza con su adorno (deco.tga, 0xe): las cajas (character_ex.tga, 0x13) y los renglones van antes.
    const std::size_t bar = indexOf(quadOf(0xe));
    ASSERT_LT(bar, events.size());
    const auto boxes =
        std::ranges::count_if(events.begin(), events.begin() + static_cast<std::ptrdiff_t>(bar), quadOf(0x13));
    EXPECT_EQ(boxes, 2);
    for (const auto& [name, level] :
         {std::pair<std::string, int>{"globo1", 37}, std::pair<std::string, int>{"globo3", 1}})
    {
        const std::size_t line =
            indexOf([&name](const Client::Tests::FrameEvent& event)
                    { return event.kind == Client::Tests::FrameEvent::Kind::Text && event.text.text == name; });
        ASSERT_LT(line + 2, bar) << name;
        const Client::Tests::RecordedText& drawn = events[line].text;
        EXPECT_TRUE(drawn.windowed);
        EXPECT_EQ(drawn.style.font, Client::Render::Text::FontKind::OtherFace12);
        EXPECT_EQ(drawn.style.colors.text, 0xffffffffu);
        EXPECT_EQ(events[line + 1].text.text, guild);
        EXPECT_EQ(events[line + 2].text.text, wizard + " " + std::to_string(level));
        EXPECT_EQ(events[line + 1].text.x, drawn.x);
        EXPECT_EQ(events[line + 2].text.x, drawn.x);
        EXPECT_EQ(events[line + 1].text.y, drawn.y + 16);
        EXPECT_EQ(events[line + 2].text.y, drawn.y + 32);
    }
}
} // namespace
