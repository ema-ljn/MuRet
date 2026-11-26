#include "Scenes/CharacterSelectScene.h"

#include "Session/ServerSession.h"
#include "Core/Log.h"
#include "Core/ILogSink.h"
#include "Input/InputState.h"
#include "Input/Key.h"
#include "Session/CharacterEvent.h"
#include "App/SessionState.h"
#include "Session/WorldEvent.h"
#include "Network/PacketSerials.h"
#include "Network/LinkState.h"
#include "Protocol/FrontEnd/CharacterList.h"
#include "Models/Cloth.h"
#include "Render/SpriteBatch.h"
#include "Tables/GameData.h"
#include "Scenes/GameModels.h"
#include "Scenes/GameScene.h"
#include "Scenes/InventoryMoves.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/OwnInventory.h"
#include "Session/SessionEventApplier.h"
#include "Scenes/SceneContext.h"
#include "Scenes/SceneId.h"
#include "Scenes/SelectionState.h"
#include "Session/WorldEntry.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "World/GameCamera.h"
#include "World/ItemState.h"

#include "support/Bytes.h"
#include "support/CharacterPackets.h"
#include "support/FakeTextureStore.h"
#include "support/GoldenDir.h"
#include "support/RecordingFrameTarget.h"
#include "support/RecordingSink.h"
#include "support/RecordingTextDrawer.h"
#include "support/SessionHarness.h"
#include "support/TestGameServer.h"
#include "support/UiInput.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
bool DrawsPart(const Client::Tests::RecordedFrame& frame, const Client::Scenes::GameModels& models, std::uint16_t part)
{
    const auto textures = models.MeshTextures(part);
    for (const auto& event : frame.events)
        for (const auto& command : event.modelCommands)
            if (command.vertexCount > 0 && std::ranges::find(textures, command.state.texture) != textures.end())
                return true;
    return false;
}

class CharacterFirstWorldEntryTest : public Client::Tests::GoldenDataTest
{
protected:
    void Load()
    {
        auto loaded = Client::Tables::LoadGameData(m_dataRoot);
        ASSERT_TRUE(loaded.HasValue()) << loaded.Error();
        m_data = std::move(loaded).Value();
        const auto loadedModels = m_models.Load(m_dataRoot, m_textures, m_log);
        ASSERT_TRUE(loadedModels.HasValue()) << loadedModels.Error();
        m_applier.emplace(Resources());
        m_online.eventSink.onCharacter =
            [this](const Client::App::CharacterEvent& event, const Client::App::ServerSession& session)
        { return m_applier->ApplyCharacter(event, session); };
        m_online.eventSink.onWorld = [this](const Client::App::WorldEvent& event, Client::App::ServerSession& session)
        { return m_applier->ApplyWorld(event, session); };
        m_online.eventSink.onLogoutResponse = [this] { m_applier->ClearLogoutRequest(); };
    }

    Client::Scenes::SceneContext Context()
    {
        return {.dataRoot = m_dataRoot,
                .textures = m_textures,
                .log = m_log,
                .clock = &m_online.clock,
                .cloth = &m_cloth,
                .input = &m_input,
                .camera = &m_camera,
                .cameraInput = &m_cameraInput,
                .selection = &m_selection,
                .world = m_world.get(),
                .textMeasure = &m_letters};
    }

    void CreateAndChoose(std::uint8_t characterClass = 0, std::uint8_t slot = 3, bool firstSelection = true)
    {
        Client::Scenes::CharacterSelectScene choose(Context(), m_data, m_models, &m_online.session,
                                                    &m_models.TextureSlots());
        if (firstSelection)
        {
            ASSERT_NO_FATAL_FAILURE(m_online.LogIn());
        }
        ASSERT_TRUE(choose.Enter().HasValue());
        if (firstSelection)
        {
            m_online.ReceiveTheList({});
        }
        choose.Step(m_input);
        ASSERT_EQ(choose.Failure(), std::nullopt);
        const std::string_view name = characterClass == 1 ? "knight" : "newchar";
        m_online.session.CreateCharacter(name, characterClass);
        auto reply = Client::Tests::Bytes({0xc1, 0x13, 0xf3, 1, 1});
        const auto encodedName = Client::Tests::NameBytes(name);
        reply.insert(reply.end(), encodedName.begin(), encodedName.end());
        const auto suffix = Client::Tests::Bytes({slot, 1, 0, characterClass << 4});
        reply.insert(reply.end(), suffix.begin(), suffix.end());
        m_online.socket.reads = {std::move(reply)};
        ASSERT_TRUE(m_online.session.Poll().HasValue());
        choose.Step(m_input);
        ASSERT_EQ(choose.Failure(), std::nullopt);
        choose.Render(m_target.BeginFrame(m_online.clock.Now()), 0.0f);
        m_target.Present();
        ASSERT_EQ(choose.Failure(), std::nullopt);
        m_online.session.ChooseCharacter(slot);
        Client::Tests::TapKey(m_input, Client::Input::Key::Enter);
        choose.Step(m_input);
        ASSERT_EQ(choose.Next(), Client::Scenes::SceneId::WorldLoading);
        m_input.ConsumeEdges();
        choose.Leave();
    }

    void ReturnToSelection()
    {
        m_online.session.RequestLogout(1);
        m_online.socket.reads = {
            Client::Tests::FromTheGameServer(Client::Tests::Bytes({0xc1, 5, 0xf1, 2, 1}), m_serverSends)};
        ASSERT_TRUE(m_online.session.Poll().HasValue());
        m_game->Leave();
        m_game.reset();
        m_scene = Client::Scenes::SceneId::CharacterSelect;
        Client::Tests::CharacterEntryFields knight{.slot = 0, .name = "knight", .classByte = 0x10};
        // Manos, alas y mascota vacías en la fotografía de la lista; no toca el registro propio.
        knight.equipment[0] = 0xff;
        knight.equipment[1] = 0xff;
        knight.equipment[4] = 0x0f;
        knight.equipment[11] = 0xf0;
        knight.equipment[12] = 0xf0;
        m_online.ReceiveTheList({knight});
        ASSERT_EQ(m_online.session.State(), Client::App::SessionState::ChoosingCharacter);
    }

    Client::Scenes::WorldEntryResources Resources()
    {
        return {.world = *m_world,
                .selection = m_selection,
                .dataRoot = m_dataRoot,
                .cloth = m_cloth,
                .camera = m_camera,
                .cameraInput = m_cameraInput,
                .models = m_models,
                .items = &m_data.items->table,
                .itemSetTables = &*m_data.itemSets,
                .log = m_log,
                .textMeasure = &m_letters,
                .scene = [this] { return std::optional(m_scene); },
                .texts = &*m_data.text};
    }

    void EnterWorld()
    {
        m_scene = Client::Scenes::SceneId::Game;
        m_game.emplace(Context(), &m_online.session, &m_models, &m_data.items->table, &m_models.TextureSlots(),
                       &*m_data.gates, &*m_data.skills, &*m_data.text);
        ASSERT_TRUE(m_game->Enter().HasValue());
        m_game->Step(m_input);
        ASSERT_EQ(m_game->Failure(), std::nullopt);
        constexpr std::size_t ReplySize = 0x34;
        std::vector<std::byte> reply(ReplySize, std::byte{0});
        constexpr std::array<std::byte, 8> Prefix{std::byte{0xc1}, std::byte{ReplySize}, std::byte{0xf3}, std::byte{3},
                                                  std::byte{135},  std::byte{125},       std::byte{0},    std::byte{0}};
        std::ranges::copy(Prefix, reply.begin());
        m_online.socket.reads = {Client::Tests::FromTheGameServer(reply, m_serverSends)};
        const auto entered = m_online.session.Poll();
        ASSERT_TRUE(entered.HasValue()) << entered.Error();
        ASSERT_EQ(m_online.session.State(), Client::App::SessionState::InWorld);
    }

    void ReceiveInventory(bool withBackpack)
    {
        auto reply = Client::Tests::Bytes({0xc2, 0, 6, 0xf3, 0x10, 0});
        if (withBackpack)
            reply = Client::Tests::Bytes({0xc2, 0, 14, 0xf3, 0x10, 1, 12, 0, 0, 1, 0, 0, 0xe0, 0});
        m_online.socket.reads = {Client::Tests::FromTheGameServer(reply, m_serverSends)};
        ASSERT_TRUE(m_online.session.Poll().HasValue());
        ASSERT_EQ(m_online.session.State(), Client::App::SessionState::InWorld);
    }

    void DrawFrames()
    {
        constexpr int Frames = 8;
        for (int frame = 0; frame < Frames; ++frame)
        {
            m_online.clock.Advance(std::chrono::milliseconds(47));
            m_game->Step(m_input);
            m_game->PrepareFrame();
            m_game->Render(m_target.BeginFrame(m_online.clock.Now()), 0.0f);
            m_target.Present();
            ASSERT_EQ(m_game->Failure(), std::nullopt);
        }
    }

    std::unique_ptr<Client::Scenes::WorldState> m_world = std::make_unique<Client::Scenes::WorldState>();
    Client::Scenes::SelectionState m_selection{*m_world};
    Client::Models::ClothWorld m_cloth;
    Client::World::GameCamera m_camera;
    Client::World::GameCameraInput m_cameraInput;
    Client::Input::InputState m_input;
    Client::Tests::FakeTextureStore m_textures;
    Client::Tests::RecordingSink m_sink;
    Client::Core::Log m_log{m_sink, Client::Core::LogLevel::Debug};
    Client::Tests::LoggedInSession m_online;
    Client::Tests::RecordingFrameTarget m_target;
    Client::Render::SpriteBatch m_textBatch;
    Client::Tests::RecordingTextDrawer m_letters{m_textBatch};
    Client::Tables::GameData m_data;
    Client::Scenes::GameModels m_models;
    std::optional<Client::Scenes::SessionEventApplier> m_applier;
    std::optional<Client::Scenes::GameScene> m_game;
    Client::Network::PacketSerials m_serverSends;
    Client::Scenes::SceneId m_scene = Client::Scenes::SceneId::CharacterSelect;
};

class FirstCreatedWorldEntryTest : public CharacterFirstWorldEntryTest, public ::testing::WithParamInterface<bool>
{
};

// 633f91 -> 5bbe10 precede al primer F303. F310 vacío o sólo mochila no vuelve a vestir el cuerpo.
TEST_P(FirstCreatedWorldEntryTest, TheFirstCreatedWizardDrawsItsClassBodyWithoutEquippedItems)
{
    ASSERT_NO_FATAL_FAILURE(Load());
    ASSERT_NO_FATAL_FAILURE(CreateAndChoose());
    ASSERT_NO_FATAL_FAILURE(EnterWorld());
    ASSERT_NO_FATAL_FAILURE(ReceiveInventory(GetParam()));

    constexpr std::array<std::uint16_t, 5> WizardBody{0x2212, 0x221a, 0x2222, 0x222a, 0x2232};
    const auto& hero = m_world->Actors()[m_world->Runtime().hero];
    for (std::size_t part = 0; part < WizardBody.size(); ++part)
        EXPECT_EQ(hero.body[part].model, WizardBody[part]);

    ASSERT_NO_FATAL_FAILURE(DrawFrames());

    EXPECT_TRUE(hero.alive);
    EXPECT_TRUE(hero.seen);
    EXPECT_GT(hero.alpha, 0.0f);
    for (const auto part : WizardBody)
        EXPECT_TRUE(DrawsPart(m_target.frames.back(), m_models, part)) << "parte " << part;
}

INSTANTIATE_TEST_SUITE_P(EmptyOrBackpackOnly, FirstCreatedWorldEntryTest, ::testing::Bool());

// Regresión del reporte: caballero equipado -> selección -> crear mago -> F303 del lugar nuevo.
TEST_F(CharacterFirstWorldEntryTest, ASecondCreatedWizardEntersAfterAnEquippedKnight)
{
    ASSERT_NO_FATAL_FAILURE(Load());
    ASSERT_NO_FATAL_FAILURE(CreateAndChoose(1, 0));
    ASSERT_NO_FATAL_FAILURE(EnterWorld());
    m_online.socket.reads = {Client::Tests::FromTheGameServer(
        Client::Tests::Bytes({0xc2, 0, 14, 0xf3, 0x10, 1, 0, 1, 0, 25, 0, 0, 0, 0}), m_serverSends)};
    ASSERT_TRUE(m_online.session.Poll().HasValue());
    ASSERT_EQ(m_online.session.State(), Client::App::SessionState::InWorld);
    const Client::World::ItemState& weapon = m_world->OwnCharacter().equipment[0];
    ASSERT_EQ(weapon.id, 1);
    ASSERT_EQ(weapon.durability, 25);
    ASSERT_NO_FATAL_FAILURE(DrawFrames());
    ASSERT_NO_FATAL_FAILURE(ReturnToSelection());
    ASSERT_NO_FATAL_FAILURE(CreateAndChoose(0, 1, false));
    EXPECT_EQ(weapon.id, 0xffff);
    EXPECT_EQ(weapon.durability, 25);

    ASSERT_NO_FATAL_FAILURE(EnterWorld());
    ASSERT_NO_FATAL_FAILURE(ReceiveInventory(true));
    ASSERT_NO_FATAL_FAILURE(DrawFrames());

    const auto& hero = m_world->Actors()[m_world->Runtime().hero];
    ASSERT_NE(m_online.session.ChosenCharacter(), nullptr);
    EXPECT_EQ(m_online.session.ChosenCharacter()->slot, 1);
    EXPECT_EQ(hero.characterClass, 0);
    EXPECT_TRUE(hero.alive);
    EXPECT_TRUE(hero.seen);
    EXPECT_GT(hero.alpha, 0.0f);
    for (const auto part : std::array<std::uint16_t, 5>{0x2212, 0x221a, 0x2222, 0x222a, 0x2232})
    {
        EXPECT_TRUE(DrawsPart(m_target.frames.back(), m_models, part));
    }
}

TEST_F(CharacterFirstWorldEntryTest, TheServerListResponseReturnsTheHeldItemBeforeReleasingItsActor)
{
    ASSERT_NO_FATAL_FAILURE(Load());
    ASSERT_NO_FATAL_FAILURE(CreateAndChoose());
    ASSERT_NO_FATAL_FAILURE(EnterWorld());
    ASSERT_NO_FATAL_FAILURE(ReceiveInventory(true));
    ASSERT_NO_FATAL_FAILURE(DrawFrames());
    const auto resources = Resources();
    const auto services = Client::Scenes::InventoryServicesOf(resources);
    const auto hero = m_world->Runtime().hero;
    m_world->Runtime().ui.logoutRequested = true;
    m_online.session.RequestLogout(2);
    ASSERT_TRUE(
        Client::Scenes::PickUpFromTheGrid({.world = *m_world, .random = m_cloth.random, .services = services}, 0)
            .HasValue());
    ASSERT_GT(m_world->Runtime().cursorItemCount, 0);
    m_online.socket.reads = {
        Client::Tests::FromTheGameServer(Client::Tests::Bytes({0xc1, 5, 0xf1, 2, 2}), m_serverSends)};

    const auto returned = m_online.session.Poll();

    ASSERT_TRUE(returned.HasValue()) << returned.Error();
    EXPECT_EQ(m_online.session.State(), Client::App::SessionState::Idle);
    EXPECT_EQ(m_online.connection.State(), Client::Network::LinkState::Closed);
    EXPECT_EQ(m_world->Runtime().orderedScene, Client::Scenes::SceneId::Login);
    EXPECT_EQ(m_world->Runtime().cursorItemCount, 0);
    EXPECT_EQ(m_world->Inventory().cells[0].id, 0x1c00u);
    EXPECT_FALSE(m_world->Runtime().ui.logoutRequested);
    EXPECT_FALSE(m_world->Actors()[hero].alive);
}
} // namespace
