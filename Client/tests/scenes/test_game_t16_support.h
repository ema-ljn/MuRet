#pragma once

#include "App/ServerSession.h"
#include "Scenes/ActorState.h"
#include "Scenes/SceneManager.h"
#include "Scenes/CharacterSelectScene.h"
#include "Scenes/WorldLoadingScene.h"
#include "Scenes/IScene.h"
#include "Scenes/SceneId.h"
#include "App/SessionState.h"
#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Input/InputState.h"
#include "Network/PacketSerials.h"
#include "Network/Crypto/Xor32.h"
#include "Render/Models/Cloth.h"
#include "Scenes/GameData.h"
#include "Scenes/LoadingScene.h"
#include "Scenes/FaunaModels.h"
#include "Scenes/GameItemRenderState.h"
#include "Scenes/GameModels.h"
#include "Scenes/GameScene.h"
#include "Scenes/SceneContext.h"
#include "Scenes/SelectionState.h"
#include "Scenes/SessionEventApplier.h"
#include "Scenes/WorldEntry.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "World/GameCamera.h"
#include "support/Bytes.h"
#include "support/FakeClock.h"
#include "support/FakeTextureStore.h"
#include "support/GoldenDir.h"
#include "support/RecordingFrameTarget.h"
#include "support/RecordingSink.h"
#include "support/SessionHarness.h"
#include "support/TestGameServer.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Datos, modelos, escena y aplicación reales. Sólo transporte, reloj y GPU son falsos declarados.
// Todo pool grande vive en heap: también se ejecuta con el stack predeterminado de Windows.
namespace Client::Tests::T16
{
inline constexpr std::uint8_t HomeX = 135;
inline constexpr std::uint8_t HomeY = 125;
inline constexpr auto FrameTime = std::chrono::milliseconds{47};

// Fronteras de la transición real, no éxitos de kernels. ASM: 06/08, H2 T12–T16.
inline std::string_view TeleportBoundary(int map)
{
    if (map == 5)
        return "Data\\World6\\TerrainHeight.OZB no está";
    if (map == 35)
        return "Data\\World36\\EncTerrain36.att no está";
    if (map >= 18 && map <= 23)
        return "0x4906a0";
    return {};
}

class SceneTest : public GoldenDataTest
{
protected:
    void SetUp() override
    {
        GoldenDataTest::SetUp();
        if (HasFatalFailure())
            return;
        auto data = Scenes::LoadGameData(m_dataRoot);
        ASSERT_TRUE(data.HasValue()) << data.Error();
        m_data = std::move(data).Value();
        // Mismos inicializadores de WinMain y del avance 1, sin inventar el offset BMD.
        Scenes::InitializeGameItemRenderState(m_world->Runtime().itemRender, m_cloth->random);
        Scenes::LoadingScene loading(Context(), m_models.get());
        const auto entered = loading.Enter();
        ASSERT_TRUE(entered.HasValue()) << entered.Error();
        loading.Step(m_input);
        ASSERT_FALSE(loading.WantsToQuit());
        ASSERT_EQ(loading.Progress(), 1);
        ASSERT_GT(m_models->Count(), 0u);
        ASSERT_TRUE(m_world->FaunaResources().PrefixOffset().has_value());
        loading.Leave();
        m_world->OwnCharacter().ClearEquipmentForEntry();
        m_applier = std::make_unique<Scenes::SessionEventApplier>(Resources());
        m_online = std::make_unique<LoggedInSession>(m_applier.get());
        ASSERT_NO_FATAL_FAILURE(m_online->LogIn());
        ReceiveEntry();
        ASSERT_EQ(m_online->session.State(), App::SessionState::InWorld);
        m_online->socket.sent.clear();
    }

    Scenes::WorldEntryResources Resources()
    {
        return {*m_world,  *m_selection,          m_dataRoot,         *m_cloth, m_camera, m_cameraInput,
                *m_models, &m_data->items->table, &*m_data->itemSets, m_log,    &m_clock};
    }

    void Receive(std::vector<std::byte> plain, bool encrypted = true)
    {
        m_online->socket.reads = {encrypted ? FromTheGameServer(plain, m_incoming) : std::move(plain)};
        if (m_manager)
        {
            m_manager->Poll();
            m_pollFailure = m_manager->Failure();
            return;
        }
        const auto polled = m_online->session.Poll();
        m_pollFailure = polled.HasValue() ? std::nullopt : std::optional{polled.Error()};
    }

    void ReceiveEntry()
    {
        constexpr std::size_t EntrySize = 0x34;
        std::vector<std::byte> reply(EntrySize);
        reply[0] = std::byte{0xc1};
        reply[1] = std::byte{EntrySize};
        reply[2] = std::byte{0xf3};
        reply[3] = std::byte{3};
        reply[4] = std::byte{HomeX};
        reply[5] = std::byte{HomeY};
        Receive(std::move(reply));
    }

    void Teleport(std::uint8_t map, std::uint8_t x = HomeX, std::uint8_t y = HomeY, std::uint8_t gate = 1)
    {
        Receive(Bytes({0xc1, 8, 0x1c, gate, map, x, y, 1}));
    }

    void Regen(std::uint8_t map, std::uint8_t x = HomeX, std::uint8_t y = HomeY)
    {
        constexpr std::size_t RegenSize = 24;
        std::vector<std::byte> reply(RegenSize);
        reply[0] = std::byte{0xc1};
        reply[1] = std::byte{RegenSize};
        reply[2] = std::byte{0xf3};
        reply[3] = std::byte{4};
        reply[4] = std::byte{x};
        reply[5] = std::byte{y};
        reply[6] = std::byte{map};
        reply[8] = std::byte{70};
        reply[10] = std::byte{80};
        reply[12] = std::byte{90};
        Receive(std::move(reply));
    }

    Scenes::SceneContext Context()
    {
        return {.dataRoot = m_dataRoot,
                .textures = m_textures,
                .log = m_log,
                .clock = &m_clock,
                .cloth = m_cloth.get(),
                .input = &m_input,
                .camera = &m_camera,
                .cameraInput = &m_cameraInput,
                .selection = m_selection.get(),
                .world = m_world.get()};
    }

    std::unique_ptr<Scenes::GameScene> MakeGame()
    {
        return std::make_unique<Scenes::GameScene>(Context(), &m_online->session, m_models.get(), &m_data->items->table,
                                                   &m_models->TextureSlots(), &*m_data->gates, &*m_data->skills);
    }

    std::unique_ptr<Scenes::IScene> MakeScene(Scenes::SceneId id)
    {
        m_scene = nullptr;
        if (id == Scenes::SceneId::CharacterSelect)
            return std::make_unique<Scenes::CharacterSelectScene>(Context(), *m_data, *m_models, &m_online->session,
                                                                  &m_models->TextureSlots());
        if (id == Scenes::SceneId::WorldLoading)
            return std::make_unique<Scenes::WorldLoadingScene>(Context());
        if (id != Scenes::SceneId::Game)
            return nullptr;
        auto scene = MakeGame();
        m_scene = scene.get();
        return scene;
    }

    void OpenScene()
    {
        m_ownedScene = MakeGame();
        m_scene = m_ownedScene.get();
        ASSERT_TRUE(m_scene->Enter().HasValue());
    }

    void OpenManagedScene()
    {
        m_manager = std::make_unique<Scenes::SceneManager>(
            [this](Scenes::SceneId id) { return MakeScene(id); }, m_target, m_clock,
            [this]() { return m_online->session.Poll(); }, m_log,
            [this]() { return std::exchange(m_world->Runtime().orderedScene, std::nullopt); });
        ASSERT_TRUE(m_manager->Start(Scenes::SceneId::Game).HasValue());
    }

    void Step()
    {
        if (m_manager)
            m_manager->Step(m_input);
        else
            m_scene->Step(m_input);
        m_input.ConsumeEdges();
        m_clock.Advance(FrameTime);
    }

    void Draw()
    {
        if (m_manager)
        {
            if (!m_manager->HoldsTheLastFrame())
                m_manager->Render(0);
            return;
        }
        m_scene->PrepareFrame();
        if (m_scene->HoldsTheLastFrame())
            return;
        auto& canvas = m_target.BeginFrame(m_clock.Now());
        m_scene->Render(canvas, 0);
        m_target.Present();
    }

    void DrawCompleteFrame()
    {
        const auto before = m_target.frames.size();
        if (m_manager)
        {
            ASSERT_FALSE(m_manager->HoldsTheLastFrame()) << m_manager->Failure().value_or("");
        }
        Draw();
        if (!m_manager)
        {
            ASSERT_FALSE(m_scene->HoldsTheLastFrame());
        }
        ASSERT_EQ(m_target.frames.size(), before + 1);
        const auto& frame = m_target.frames[before];
        EXPECT_TRUE(frame.presented);
        EXPECT_EQ(frame.clock, m_clock.Now());
        const bool selection = m_manager && m_manager->Current() == Scenes::SceneId::CharacterSelect;
        EXPECT_TRUE(std::ranges::any_of(frame.events,
                                        [selection](const auto& event)
                                        {
                                            return selection ? event.kind == FrameEvent::Kind::World && event.pass &&
                                                                   event.world.map == 54
                                                             : event.kind == FrameEvent::Kind::Models && event.pass &&
                                                                   !event.modelVertices.empty();
                                        }))
            << "mapa=" << m_world->Runtime().map << " nuevo frame=" << before;
        if (m_manager)
        {
            ASSERT_FALSE(m_manager->Failure().has_value()) << *m_manager->Failure();
        }
        else
        {
            ASSERT_FALSE(m_scene->Failure().has_value()) << *m_scene->Failure();
        }
    }

    Scenes::ActorState& Hero()
    {
        return m_world->Actors()[m_world->Runtime().hero];
    }

    GameServerReceived Sent()
    {
        auto received = AsTheGameServerReceives(m_online->socket.sent, m_outgoing);
        m_online->socket.sent.clear();
        for (auto& packet : received.packets)
            packet = Network::Crypto::Xor32Decode(std::move(packet));
        return received;
    }

    RecordingSink m_sink;
    Core::Log m_log{m_sink, Core::LogLevel::Debug};
    FakeTextureStore m_textures;
    FakeClock m_clock;
    std::unique_ptr<Render::Models::ClothWorld> m_cloth = std::make_unique<Render::Models::ClothWorld>();
    std::unique_ptr<Scenes::WorldState> m_world = std::make_unique<Scenes::WorldState>();
    std::unique_ptr<Scenes::SelectionState> m_selection = std::make_unique<Scenes::SelectionState>(*m_world);
    std::unique_ptr<Scenes::GameModels> m_models = std::make_unique<Scenes::GameModels>();
    World::GameCamera m_camera;
    World::GameCameraInput m_cameraInput;
    Input::InputState m_input;
    std::optional<Scenes::GameData> m_data;
    std::optional<std::string> m_pollFailure;
    Network::PacketSerials m_incoming;
    Network::PacketSerials m_outgoing;
    std::unique_ptr<Scenes::SessionEventApplier> m_applier;
    std::unique_ptr<LoggedInSession> m_online;
    RecordingFrameTarget m_target;
    std::unique_ptr<Scenes::GameScene> m_ownedScene;
    Scenes::GameScene* m_scene = nullptr;
    std::unique_ptr<Scenes::SceneManager> m_manager;
};
} // namespace Client::Tests::T16
