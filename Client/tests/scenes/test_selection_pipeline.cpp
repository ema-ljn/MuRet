#include "Scenes/SelectionState.h"
#include "Scenes/SessionEventApplier.h"

#include "App/ServerSession.h"
#include "App/SessionState.h"
#include "Core/Expected.h"
#include "Core/ILogSink.h"
#include "Input/InputState.h"
#include "Input/Key.h"
#include "Network/PacketSerials.h"
#include "Protocol/Character/CharacterDelete.h"
#include "Render/Models/Cloth.h"
#include "Scenes/CharacterSelectScene.h"
#include "Scenes/CharacterSlots.h"
#include "Scenes/GameData.h"
#include "Scenes/IScene.h"
#include "Scenes/LoginModels.h"
#include "Scenes/LoginScene.h"
#include "Scenes/SceneContext.h"
#include "Scenes/SceneId.h"
#include "Scenes/SceneManager.h"
#include "Scenes/WorldLoadingScene.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "World/GameCamera.h"
#include "World/TerrainHeight.h"
#include "World/WorldTerrain.h"
#include "support/TestGameServer.h"

#include "support/Bytes.h"
#include "support/CharacterPackets.h"
#include "support/FakeTextureStore.h"
#include "support/GoldenDir.h"
#include "support/RecordingFrameTarget.h"
#include "support/SessionHarness.h"
#include "support/SessionPackets.h"
#include "support/UiInput.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
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
        auto terrain = Client::World::LoadWorldTerrain(m_dataRoot, LoginMap);
        ASSERT_TRUE(terrain.HasValue()) << terrain.Error();
        m_loginConstructorHeight = static_cast<float>(Client::World::TerrainHeightAt(
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
                                                   .world = &m_world};
        if (id == SceneId::CharacterSelect)
        {
            return std::make_unique<Client::Scenes::CharacterSelectScene>(context, m_data, m_models, &m_online.session);
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
    Client::Scenes::GameData m_data;
    Client::Scenes::SingleModelLibrary m_models;
    Client::World::GameCamera m_camera;
    Client::World::GameCameraInput m_cameraInput;
    Client::Render::Models::ClothWorld m_cloth;
    Client::Scenes::WorldState m_world;
    Client::Scenes::SelectionState m_state{m_world};
    Client::Scenes::SessionEventApplier m_applier{m_state, m_camera, m_cameraInput};
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
    ASSERT_NO_FATAL_FAILURE(ReceiveList({{0, "char0001"}}));
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
    m_online.socket.reads = {Client::Tests::CharacterListBytes({{0, "char0001"}})};
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

TEST_F(SelectionPipelineTest, PollConstructsBeforeLogicAndTheRenderGuardKeepsTheLoginTerrain)
{
    ASSERT_NO_FATAL_FAILURE(StartSelect());
    m_cameraInput.hero = {-1.0f, -2.0f, -3.0f};

    ASSERT_NO_FATAL_FAILURE(ReceiveList({{0, "char0001"}}));

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

TEST_F(SelectionPipelineTest, RecreatingAndDeletingBeforeAnotherStepSurvivesTheRealLoginTransition)
{
    ASSERT_NO_FATAL_FAILURE(StartSelect());
    ASSERT_NO_FATAL_FAILURE(ReceiveList({WithHeightOffsetPet(0), WithHeightOffsetPet(PreservedPetSlot)}));
    m_manager->Step(Client::Input::InputState{});
    ASSERT_TRUE(m_state.IsInitialized());
    m_manager->Render(0.0f);
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
    m_online.session.DeleteCharacter(Client::Protocol::Character::DeleteCodeFromText("1234"));
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
    ASSERT_NO_FATAL_FAILURE(ReceiveList({{0, "char0001"}}));
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
    EXPECT_EQ(m_world.Runtime().warpCounter, Client::Scenes::WorldLoadingWait);
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
// F310 vacío sigue siendo trabajo: el aplicador local lo rechaza, sin acumularlo entre frames.
TEST_F(SelectionPipelineTest, MixedCharacterThenWorldAppliesTheCharacterBeforeTheWorldFailure)
{
    ASSERT_NO_FATAL_FAILURE(StartSelect());
    Client::Network::PacketSerials serials;
    const auto inventory =
        Client::Tests::FromTheGameServer(Client::Tests::Bytes({0xc2, 0x00, 0x06, 0xf3, 0x10, 0x00}), serials);
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
    const auto inventory =
        Client::Tests::FromTheGameServer(Client::Tests::Bytes({0xc2, 0x00, 0x06, 0xf3, 0x10, 0x00}), serials);
    m_online.socket.reads = {Client::Tests::Join({inventory, Client::Tests::CharacterListBytes({{0, "char0001"}})})};
    m_manager->Poll();
    EXPECT_EQ(m_online.session.Failure(), SessionFailure::ApplicationFailed);
    EXPECT_TRUE(m_manager->Failure().has_value());
    EXPECT_EQ(m_state.Slots()[0].revision, 0u);
    EXPECT_FALSE(m_state.HasReceivedList());
    EXPECT_FALSE(m_online.session.Characters().has_value());
    EXPECT_TRUE(m_target.frames.empty());
}
} // namespace
