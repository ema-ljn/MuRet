#include "test_game_t16_support.h"
#include "App/SessionState.h"
#include "World/GameCamera.h"
#include "Scenes/GameScene.h"
#include "support/Bytes.h"

#include "Data/Tables/GateRecord.h"
#include "Scenes/CharacterSlots.h"
#include "Input/Key.h"
#include "support/CharacterPackets.h"
#include "Network/Packet.h"
#include "Render/Pass3D.h"
#include "Scenes/ActorState.h"
#include "Scenes/SceneId.h"
#include "Scenes/WorldMap.h"
#include "World/TerrainHeight.h"
#include "World/Particles.h"
#include "support/UiInput.h"
#include "support/RecordingFrameTarget.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <vector>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>

namespace
{
using Client::Tests::T16::SceneTest;

class GameT16Journey : public SceneTest
{
protected:
    void WaitForInput()
    {
        m_clock.Advance(std::chrono::seconds{4});
        for (int step = 0; step < 31; ++step)
        {
            Step();
            ASSERT_FALSE(m_scene->Failure().has_value()) << *m_scene->Failure();
        }
        ASSERT_LE(m_world->Runtime().warpCounter, 0);
        static_cast<void>(Sent());
    }

    void ClickCell(std::uint8_t x, std::uint8_t y)
    {
        ASSERT_NO_FATAL_FAILURE(DrawCompleteFrame());
        const auto& terrain = m_world->Map().terrain;
        const float px = static_cast<float>(x) * 100 + 50;
        const float py = static_cast<float>(y) * 100 + 50;
        const float pz = static_cast<float>(
            Client::World::TerrainHeightAt(*terrain.heights, *terrain.attributes, px, py, m_camera.fixedHeight));
        const auto camera = Client::World::GameCameraOf(m_camera, m_cameraInput);
        const auto pass = Client::Render::MakePass3D({640, 480}, Client::Scenes::GameWorldViewport, camera);
        const auto pixel = pass.Project(glm::vec3{px, py, pz});
        ASSERT_TRUE(pixel.has_value());
        ASSERT_GE(pixel->x, 0);
        ASSERT_LT(pixel->x, 640);
        ASSERT_GE(pixel->y, 0);
        ASSERT_LE(pixel->y, 431);
        Client::Tests::MoveMouse(m_input, static_cast<int>(std::round(pixel->x)),
                                 static_cast<int>(std::round(pixel->y)));
        // Publica el rayo real que consumirá MoveHero en el paso siguiente.
        ASSERT_NO_FATAL_FAILURE(DrawCompleteFrame());
        ASSERT_TRUE(m_scene->MouseRay().has_value());
        Client::Tests::PressLeft(m_input);
        Step();
        Client::Tests::ReleaseLeft(m_input);
        m_input.ConsumeEdges();
        ASSERT_FALSE(m_scene->Failure().has_value()) << *m_scene->Failure();
    }

    void EchoValidatedWalk(const Client::Network::Packet& walk)
    {
        const auto wire = walk.Bytes();
        const auto steps = std::to_integer<unsigned int>(wire[5]) & 15u;
        ASSERT_EQ(wire.size(), 6u + (steps + 1u) / 2u);
        ASSERT_EQ(std::to_integer<std::size_t>(wire[1]), wire.size());
        // Decodificador del fakeGS, independiente de WriteWalk/DirectionBetween productivos.
        constexpr std::array<int, 8> DeltaX{-1, 0, 1, 1, 1, 0, -1, -1};
        constexpr std::array<int, 8> DeltaY{-1, -1, -1, 0, 1, 1, 1, 0};
        int x = std::to_integer<int>(wire[3]);
        int y = std::to_integer<int>(wire[4]);
        for (unsigned int step = 0; step < steps; ++step)
        {
            const unsigned int packed = std::to_integer<unsigned int>(wire[6 + step / 2]);
            const unsigned int direction = (step % 2 == 0 ? packed >> 4 : packed) & 15u;
            ASSERT_LT(direction, DeltaX.size());
            x += DeltaX[direction];
            y += DeltaY[direction];
        }
        ASSERT_GE(x, 0);
        ASSERT_LE(x, 255);
        ASSERT_GE(y, 0);
        ASSERT_LE(y, 255);
        const auto direction = std::to_integer<std::uint8_t>(wire[5]) >> 4;
        const auto key = Hero().key;
        Receive(Client::Tests::Bytes({0xc1, 8, 0xd3, static_cast<std::uint8_t>(key >> 8),
                                      static_cast<std::uint8_t>(key), static_cast<std::uint8_t>(x),
                                      static_cast<std::uint8_t>(y), static_cast<std::uint8_t>(direction << 4)}),
                false);
        ASSERT_EQ(m_online->session.State(), Client::App::SessionState::InWorld);
        EXPECT_EQ(Hero().direction, direction);
    }

    void ReturnThroughRealSelection()
    {
        Receive(Client::Tests::Bytes({0xc1, 5, 0xf1, 2, 1}));
        ASSERT_FALSE(m_manager->Failure().has_value()) << *m_manager->Failure();
        ASSERT_EQ(m_online->session.State(), Client::App::SessionState::WaitingCharacterList);
        EXPECT_EQ(m_world->Runtime().map, -1);
        EXPECT_EQ(m_manager->Current(), Client::Scenes::SceneId::Game);
        Step();
        ASSERT_EQ(m_manager->Current(), Client::Scenes::SceneId::CharacterSelect);
        EXPECT_TRUE(m_manager->TakeTheSceneChange());
        const auto requests = Sent();
        EXPECT_TRUE(std::ranges::any_of(
            requests.packets, [](const auto& packet)
            { return packet.Opcode() == 0xf3 && packet.Bytes().size() == 4 && packet.Bytes()[3] == std::byte{0}; }));
        Client::Tests::CharacterEntryFields entry{.slot = 0, .level = 400};
        entry.equipment[4] = 3; // Sin mascota: no alcanza la frontera de helpers de selección.
        Receive(Client::Tests::CharacterListBytes({entry}), false);
        ASSERT_EQ(m_online->session.State(), Client::App::SessionState::ChoosingCharacter);
        Step();
        ASSERT_FALSE(m_manager->Failure().has_value()) << *m_manager->Failure();
        ASSERT_NO_FATAL_FAILURE(DrawCompleteFrame());
        ASSERT_NO_FATAL_FAILURE(ChooseFromTheSelectionFrame());
        Client::Tests::TapKey(m_input, Client::Input::Key::Enter);
        Step();
        ASSERT_EQ(m_manager->Current(), Client::Scenes::SceneId::WorldLoading);
        const auto before = m_target.frames.size();
        Draw();
        ASSERT_EQ(m_target.frames.size(), before + 1);
        EXPECT_TRUE(m_target.frames[before].presented);
        EXPECT_EQ(m_target.frames[before].events.size(), 4u); // Mosaico real recién emitido.
        Step();
        ASSERT_EQ(m_manager->Current(), Client::Scenes::SceneId::Game);
        Step(); // GameScene pide F303 desde su primer paso, con la espera aún activa.
        const auto enter = Sent();
        EXPECT_TRUE(std::ranges::any_of(
            enter.packets, [](const auto& packet)
            { return packet.Opcode() == 0xf3 && packet.Bytes().size() > 4 && packet.Bytes()[3] == std::byte{3}; }));
        ReceiveEntry();
        ASSERT_EQ(m_online->session.State(), Client::App::SessionState::InWorld);
        Step();
        ASSERT_NO_FATAL_FAILURE(DrawCompleteFrame());
        EXPECT_EQ(m_world->Runtime().map, 0);
        EXPECT_TRUE(Hero().alive);
    }

    void ChooseFromTheSelectionFrame()
    {
        const auto& events = m_target.frames.back().events;
        const auto models =
            std::ranges::find_if(events, [](const auto& event)
                                 { return event.kind == Client::Tests::FrameEvent::Kind::Models && event.pass; });
        ASSERT_NE(models, events.end());
        const glm::vec3 chest(Client::Scenes::CharacterSlots[0].position, Client::Scenes::CharacterSlotHeight + 60);
        const auto pixel = models->pass->Project(chest);
        ASSERT_TRUE(pixel.has_value());
        Client::Tests::ClickAt(m_input, static_cast<int>(pixel->x), static_cast<int>(pixel->y));
        Step();
        ASSERT_NE(m_online->session.ChosenCharacter(), nullptr);
        EXPECT_EQ(m_online->session.ChosenCharacter()->slot, 0);
    }

    void ExpectWalkThenGate(std::uint8_t gate, std::uint8_t x, std::uint8_t y)
    {
        const auto start = Hero().position;
        ClickCell(x, y);
        const auto clicked = Sent();
        const auto walk =
            std::ranges::find_if(clicked.packets, [](const auto& packet) { return packet.Opcode() == 0xd3; });
        ASSERT_NE(walk, clicked.packets.end())
            << "target=" << m_world->Runtime().movement.targetX << "," << m_world->Runtime().movement.targetY
            << " hero=" << static_cast<int>(Hero().cellX) << "," << static_cast<int>(Hero().cellY)
            << " window=" << m_world->Runtime().movement.mouseUpdateTime
            << " max=" << m_world->Runtime().movement.mouseUpdateTimeMax;
        // Oráculo literal de 04 § Caminar: los dos tramos elegidos son rectos sobre Data real.
        const auto expected = gate == 23 ? Client::Tests::Bytes({0xc1, 7, 0xd3, 215, 244, 0x52, 0x55})
                                         : Client::Tests::Bytes({0xc1, 8, 0xd3, 151, 6, 0x13, 0x11, 0x10});
        ASSERT_EQ(std::vector<std::byte>(walk->Bytes().begin(), walk->Bytes().end()), expected);
        ASSERT_EQ(Hero().movement, 1);
        ASSERT_NO_FATAL_FAILURE(EchoValidatedWalk(*walk));
        EXPECT_EQ(Hero().position, start);
        bool requested = false;
        for (int step = 0; step < 80 && !requested; ++step)
        {
            Step();
            ASSERT_FALSE(m_scene->Failure().has_value()) << *m_scene->Failure();
            const auto packets = Sent();
            for (const auto& packet : packets.packets)
                if (packet.Opcode() == 0x1c)
                {
                    ASSERT_EQ(packet.Bytes().size(), 6u);
                    EXPECT_EQ(packet.Bytes()[3], std::byte{gate});
                    EXPECT_EQ(packet.Bytes()[4], std::byte{0});
                    EXPECT_EQ(packet.Bytes()[5], std::byte{0});
                    requested = true;
                }
        }
        ASSERT_TRUE(requested);
        EXPECT_NE(Hero().position, start);
        EXPECT_GT(m_world->Runtime().warpCounter, 30);
        m_scene->PrepareFrame();
        EXPECT_TRUE(m_scene->HoldsTheLastFrame());
    }
};

TEST_F(GameT16Journey, RealClicksEchoesAndGateRectanglesTravelToNoriaAndBackBeforeReentry)
{
    // Gate 23 real: 213..217 × 246..247. El fakeGS elige una llegada cercana al gate 25 real.
    Teleport(0, 215, 244);
    ASSERT_EQ(m_online->session.State(), Client::App::SessionState::InWorld);
    static_cast<void>(Sent());
    m_world->OwnCharacter().level = 400;
    OpenManagedScene();
    WaitForInput();
    ASSERT_NO_FATAL_FAILURE(ExpectWalkThenGate(23, 215, 246));
    const auto noriaGate = Client::Data::Tables::ReadGateRecord(*m_data->gates, 25);
    ASSERT_TRUE(noriaGate.has_value());
    ASSERT_EQ(noriaGate->map, 3);
    ASSERT_GT(noriaGate->x1, 2);
    const auto gateCentreX = static_cast<std::uint8_t>((noriaGate->x1 + noriaGate->x2) / 2);
    Teleport(3, gateCentreX, static_cast<std::uint8_t>(noriaGate->y2 + 2), 23);
    ASSERT_EQ(m_online->session.State(), Client::App::SessionState::InWorld);
    ASSERT_EQ(Sent().packets.size(), 1u);
    ASSERT_EQ(m_world->Runtime().map, 3);
    WaitForInput();
    ASSERT_NO_FATAL_FAILURE(ExpectWalkThenGate(25, gateCentreX, noriaGate->y1));
    Teleport(0, Client::Tests::T16::HomeX, Client::Tests::T16::HomeY, 25);
    ASSERT_EQ(m_online->session.State(), Client::App::SessionState::InWorld);
    ASSERT_EQ(Sent().packets.size(), 1u);
    WaitForInput();
    ASSERT_NO_FATAL_FAILURE(DrawCompleteFrame());
    Regen(0);
    ASSERT_EQ(m_online->session.State(), Client::App::SessionState::InWorld);
    EXPECT_TRUE(Sent().packets.empty());
    ASSERT_NO_FATAL_FAILURE(ReturnThroughRealSelection());
}
class GameT16MapWalk : public GameT16Journey, public ::testing::WithParamInterface<int>
{
protected:
    void FinishWalk(std::uint8_t x, std::uint8_t y)
    {
        bool moved = false;
        const auto start = Hero().position;
        for (int step = 0; step < 120 && Hero().movement != 0; ++step)
        {
            const float previousHeight = Hero().position.z;
            Step();
            ASSERT_FALSE(m_scene->Failure().has_value()) << *m_scene->Failure();
            const auto& terrain = m_world->Map().terrain;
            if (Hero().movement != 0)
            {
                EXPECT_FLOAT_EQ(Hero().position.z, static_cast<float>(Client::World::TerrainHeightAt(
                                                       *terrain.heights, *terrain.attributes, Hero().position.x,
                                                       Hero().position.y, m_camera.fixedHeight)));
            }
            else
            {
                // MovePath fija XY al llegar; MoveHero ya no llama 508a80 y conserva la Z anterior.
                EXPECT_FLOAT_EQ(Hero().position.z, previousHeight);
            }
            moved = moved || Hero().position != start;
            if (step % 8 == 0)
            {
                ASSERT_NO_FATAL_FAILURE(DrawCompleteFrame());
            }
        }
        EXPECT_TRUE(moved);
        ASSERT_EQ(Hero().movement, 0);
        EXPECT_EQ(Hero().cellX, x);
        EXPECT_EQ(Hero().cellY, y);
        EXPECT_FLOAT_EQ(Hero().position.x, static_cast<float>(x) * 100 + 50);
        EXPECT_FLOAT_EQ(Hero().position.y, static_cast<float>(y) * 100 + 50);
        ASSERT_NO_FATAL_FAILURE(DrawCompleteFrame());
    }

    void ExpectWalkPacket()
    {
        const auto sent = Sent();
        EXPECT_TRUE(std::ranges::any_of(sent.packets, [](const auto& packet) { return packet.Opcode() == 0xd3; }));
    }
};

// En Lost Tower (World5) el 70..73 no tiene suelo (0x08 del .att); 205..208 × 87..90, al sur de la zona
// segura, es suelo libre.
TEST_P(GameT16MapWalk, RealTerrainPicksAndMovesStraightThenAroundAnElbowBetweenCompleteFrames)
{
    const auto map = static_cast<std::uint8_t>(GetParam());
    const std::uint8_t x = map == 4 ? 205 : 70;
    const std::uint8_t y = map == 4 ? 87 : 70;
    const auto far = [](std::uint8_t cell) { return static_cast<std::uint8_t>(cell + 3); };
    Teleport(map, x, y);
    ASSERT_EQ(m_online->session.State(), Client::App::SessionState::InWorld);
    OpenScene();
    ASSERT_NO_FATAL_FAILURE(WaitForInput());
    ASSERT_NO_FATAL_FAILURE(ClickCell(far(x), y));
    ASSERT_NO_FATAL_FAILURE(ExpectWalkPacket());
    ASSERT_NO_FATAL_FAILURE(FinishWalk(far(x), y));
    ASSERT_NO_FATAL_FAILURE(ClickCell(far(x), far(y)));
    ASSERT_NO_FATAL_FAILURE(ExpectWalkPacket());
    ASSERT_NO_FATAL_FAILURE(FinishWalk(far(x), far(y)));
}

// El Dungeon (World2) no tiene celdas con el bit 0x01 de la zona segura; los demás, sí.
class GameT16SafeZoneWalk : public GameT16MapWalk
{
};
TEST_P(GameT16SafeZoneWalk, RealSafeZoneCellsResetRunWhileTheirLogicalCellAdvances)
{
    const auto map = static_cast<std::uint8_t>(GetParam());
    constexpr std::array<std::uint8_t, 5> SafeX{132, 0, 185, 166, 195};
    constexpr std::array<std::uint8_t, 5> SafeY{89, 0, 30, 81, 72};
    const std::uint8_t x = SafeX[map];
    const std::uint8_t y = SafeY[map];
    Teleport(map, x, y);
    ASSERT_EQ(m_online->session.State(), Client::App::SessionState::InWorld);
    OpenScene();
    ASSERT_NO_FATAL_FAILURE(WaitForInput());
    ASSERT_EQ(Hero().safeZone, 1);
    Hero().run = 39;
    ASSERT_NO_FATAL_FAILURE(ClickCell(static_cast<std::uint8_t>(x + 2), y));
    ASSERT_NO_FATAL_FAILURE(ExpectWalkPacket());
    Step();
    ASSERT_FALSE(m_scene->Failure().has_value()) << *m_scene->Failure();
    EXPECT_EQ(Hero().cellX, x + 1);
    EXPECT_LT(Hero().position.x, static_cast<float>(x + 1) * 100);
    EXPECT_EQ(Hero().safeZone, 1);
    EXPECT_EQ(Hero().run, 0);
    ASSERT_NO_FATAL_FAILURE(FinishWalk(static_cast<std::uint8_t>(x + 2), y));
}

TEST_P(GameT16MapWalk, TheArchivedOuterWallCannotStartAnOutOfBoundsWalk)
{
    Teleport(static_cast<std::uint8_t>(GetParam()), 254, 254);
    ASSERT_EQ(m_online->session.State(), Client::App::SessionState::InWorld);
    OpenScene();
    ASSERT_NO_FATAL_FAILURE(WaitForInput());
    const auto start = Hero().position;
    ASSERT_NO_FATAL_FAILURE(ClickCell(255, 255));
    const auto sent = Sent();
    EXPECT_FALSE(std::ranges::any_of(sent.packets, [](const auto& packet) { return packet.Opcode() == 0xd3; }));
    EXPECT_EQ(Hero().position, start);
    EXPECT_EQ(Hero().movement, 0);
    RecordProperty("border_scope", "real outer wall/picking rejection; no fabricated traversable border");
}

class GameT16KalimaWalk : public GameT16MapWalk
{
};
TEST_P(GameT16KalimaWalk, RealPickingLiteralD3AndEchoAdvanceTheHeroAndEmitNativeFootsteps)
{
    const auto map = static_cast<std::uint8_t>(GetParam());
    Teleport(map, 62, 67);
    ASSERT_FALSE(m_pollFailure) << *m_pollFailure;
    ASSERT_EQ(m_online->session.State(), Client::App::SessionState::InWorld);
    OpenScene();
    ASSERT_NO_FATAL_FAILURE(WaitForInput());
    const auto start = Hero().position;
    ASSERT_NO_FATAL_FAILURE(ClickCell(64, 67));
    const auto sent = Sent();
    const auto walk = std::ranges::find_if(sent.packets, [](const auto& packet) { return packet.Opcode() == 0xd3; });
    ASSERT_NE(walk, sent.packets.end()) << "picked=" << m_world->Runtime().movement.targetX << ","
                                        << m_world->Runtime().movement.targetY
                                        << " time=" << m_world->Runtime().movement.mouseUpdateTime << "/"
                                        << m_world->Runtime().movement.mouseUpdateTimeMax
                                        << " block=" << m_world->Runtime().movement.movementBlocked
                                        << " walls=" << m_world->Map().terrain.attributes->cells[64 * 256 + 64] << ","
                                        << m_world->Map().terrain.attributes->cells[64 * 256 + 65] << ","
                                        << m_world->Map().terrain.attributes->cells[64 * 256 + 66];
    const auto expected = Client::Tests::Bytes({0xc1, 7, 0xd3, 62, 67, 0x32, 0x33});
    ASSERT_EQ(std::vector<std::byte>(walk->Bytes().begin(), walk->Bytes().end()), expected);
    ASSERT_NO_FATAL_FAILURE(EchoValidatedWalk(*walk));
    ASSERT_FALSE(m_pollFailure) << *m_pollFailure;
    EXPECT_EQ(Hero().position, start);
    const auto footsteps = [&]()
    {
        return std::ranges::count_if(m_world->Particles().All(),
                                     [](const auto& p) { return p.alive && p.type == 0x6f4 && p.subtype == 1; });
    };
    const auto before = footsteps();
    Step();
    ASSERT_FALSE(m_scene->Failure()) << *m_scene->Failure();
    EXPECT_NE(Hero().position, start);
    EXPECT_GT(footsteps(), before);
    ASSERT_NO_FATAL_FAILURE(DrawCompleteFrame());
    ASSERT_NO_FATAL_FAILURE(FinishWalk(64, 67));
    EXPECT_EQ(m_world->Map().objects.size(), 2266u);
    bool water = false;
    bool particle = false;
    for (const auto& event : m_target.frames.back().events)
        for (const auto& command : event.modelCommands)
            if (command.vertexCount && command.state.texture)
            {
                water |= command.state.texture->value == 0x23;
                particle |= command.state.texture->value == 0x6f4;
            }
    EXPECT_TRUE(water);
    EXPECT_TRUE(particle);
    RecordProperty("walk_scope", "real picking; literalD3; decoded fakeGS echo; MoveHero;6f4/1; new full frame");
}
INSTANTIATE_TEST_SUITE_P(AllSevenKalimaAliases, GameT16KalimaWalk, ::testing::Values(24, 25, 26, 27, 28, 29, 36),
                         [](const ::testing::TestParamInfo<int>& parameter)
                         { return "Map" + std::to_string(parameter.param); });

INSTANTIATE_TEST_SUITE_P(CompleteSceneMaps, GameT16MapWalk, ::testing::Values(0, 1, 2, 3, 4),
                         [](const ::testing::TestParamInfo<int>& mapParameter)
                         { return "Map" + std::to_string(mapParameter.param); });
INSTANTIATE_TEST_SUITE_P(CompleteSceneMapsWithSafeZone, GameT16SafeZoneWalk, ::testing::Values(0, 2, 3, 4),
                         [](const ::testing::TestParamInfo<int>& mapParameter)
                         { return "Map" + std::to_string(mapParameter.param); });
} // namespace
