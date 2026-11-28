#include "test_game_t16_support.h"
#include "Session/SessionState.h"
#include "CastleSiege/CastleSiegeObjectStep.h"
#include "Camera/GameCamera.h"
#include "Scenes/GameScene.h"
#include "support/Bytes.h"

#include "Tables/GateRecord.h"
#include "FrontEnd/CharacterSlots.h"
#include "Input/Key.h"
#include "support/CharacterPackets.h"
#include "Network/Packet.h"
#include "Render/ModelFog.h"
#include "Render/Pass3D.h"
#include "Kernel/ActorState.h"
#include "Kernel/SceneId.h"
#include "Kernel/WorldMap.h"
#include "Terrain/TerrainCulling.h"
#include "Terrain/TerrainHeight.h"
#include "Particles/Particles.h"
#include "support/UiInput.h"
#include "support/RecordingFrameTarget.h"
#include "Movement/GameMovementState.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <vector>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

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
        HideTheSiegePanel();
    }

    void ClickCell(std::uint8_t x, std::uint8_t y)
    {
        ASSERT_NO_FATAL_FAILURE(DrawCompleteFrame());
        const auto& terrain = m_states->terrain.current;
        const float px = static_cast<float>(x) * 100 + 50;
        const float py = static_cast<float>(y) * 100 + 50;
        const float pz = static_cast<float>(
            Client::Terrain::TerrainHeightAt(*terrain.heights, *terrain.attributes, px, py, m_camera.fixedHeight));
        const auto camera = Client::Camera::GameCameraOf(m_camera, m_cameraInput);
        const auto pass = Client::Render::MakePass3D({640, 480}, Client::Scenes::GameWorldViewport, camera);
        const auto pixel = pass.Project(glm::vec3{px, py, pz});
        ASSERT_TRUE(pixel.has_value());
        ASSERT_GE(pixel->x, 0);
        ASSERT_LT(pixel->x, 640);
        ASSERT_GE(pixel->y, 0);
        ASSERT_LE(pixel->y, 431);
        Client::Tests::MoveMouse(m_input, static_cast<int>(std::round(pixel->x)),
                                 static_cast<int>(std::round(pixel->y)));
        // Publishes the real ray that MoveHero consumes on the next step.
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
        // The fakeGS decoder, independent of the production WriteWalk/DirectionBetween.
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
        ASSERT_EQ(m_online->session.State(), Client::Session::SessionState::InWorld);
        EXPECT_EQ(Hero().direction, direction);
    }

    void ReturnThroughRealSelection()
    {
        Receive(Client::Tests::Bytes({0xc1, 5, 0xf1, 2, 1}));
        ASSERT_FALSE(m_manager->Failure().has_value()) << *m_manager->Failure();
        ASSERT_EQ(m_online->session.State(), Client::Session::SessionState::WaitingCharacterList);
        EXPECT_EQ(m_world->Runtime().map, -1);
        EXPECT_EQ(m_manager->Current(), Client::Kernel::SceneId::Game);
        Step();
        ASSERT_EQ(m_manager->Current(), Client::Kernel::SceneId::CharacterSelect);
        EXPECT_TRUE(m_manager->TakeTheSceneChange());
        const auto requests = Sent();
        EXPECT_TRUE(std::ranges::any_of(requests.packets,
                                        [](const auto& packet)
                                        {
                                            return packet.Opcode() == Client::Network::Opcode{0xf3} &&
                                                   packet.Bytes().size() == 4 && packet.Bytes()[3] == std::byte{0};
                                        }));
        Client::Tests::CharacterEntryFields entry{.slot = 0, .level = 400};
        entry.equipment[4] = 3; // No pet: it does not reach the boundary of the selection helpers.
        Receive(Client::Tests::CharacterListBytes({entry}), false);
        ASSERT_EQ(m_online->session.State(), Client::Session::SessionState::ChoosingCharacter);
        Step();
        ASSERT_FALSE(m_manager->Failure().has_value()) << *m_manager->Failure();
        ASSERT_NO_FATAL_FAILURE(DrawCompleteFrame());
        ASSERT_NO_FATAL_FAILURE(ChooseFromTheSelectionFrame());
        Client::Tests::TapKey(m_input, Client::Input::Key::Enter);
        Step();
        ASSERT_EQ(m_manager->Current(), Client::Kernel::SceneId::WorldLoading);
        const auto before = m_target.frames.size();
        Draw();
        ASSERT_EQ(m_target.frames.size(), before + 1);
        EXPECT_TRUE(m_target.frames[before].presented);
        EXPECT_EQ(m_target.frames[before].events.size(), 4u); // Real mosaic just emitted.
        Step();
        ASSERT_EQ(m_manager->Current(), Client::Kernel::SceneId::Game);
        Step(); // GameScene asks for F303 from its first step, with the wait still active.
        const auto enter = Sent();
        EXPECT_TRUE(std::ranges::any_of(enter.packets,
                                        [](const auto& packet)
                                        {
                                            return packet.Opcode() == Client::Network::Opcode{0xf3} &&
                                                   packet.Bytes().size() > 4 && packet.Bytes()[3] == std::byte{3};
                                        }));
        ReceiveEntry();
        ASSERT_EQ(m_online->session.State(), Client::Session::SessionState::InWorld);
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
        const glm::vec3 chest(Client::FrontEnd::CharacterSlots[0].position, Client::FrontEnd::CharacterSlotHeight + 60);
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
        const auto walk = std::ranges::find_if(clicked.packets, [](const auto& packet)
                                               { return packet.Opcode() == Client::Network::Opcode{0xd3}; });
        ASSERT_NE(walk, clicked.packets.end())
            << "target=" << m_world->Runtime().movement.targetX << "," << m_world->Runtime().movement.targetY
            << " hero=" << static_cast<int>(Hero().cellX) << "," << static_cast<int>(Hero().cellY)
            << " window=" << m_world->Runtime().mouse.updateTime << " max=" << m_world->Runtime().mouse.updateTimeMax;
        // Literal oracle from 04 § Caminar: the two chosen stretches are straight on real Data.
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
                if (packet.Opcode() == Client::Network::Opcode{0x1c})
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
    // Real gate 23: 213..217 × 246..247. The fakeGS picks an arrival near the real gate 25.
    Teleport(0, 215, 244);
    ASSERT_EQ(m_online->session.State(), Client::Session::SessionState::InWorld);
    static_cast<void>(Sent());
    m_world->OwnCharacter().level = 400;
    OpenManagedScene();
    WaitForInput();
    ASSERT_NO_FATAL_FAILURE(ExpectWalkThenGate(23, 215, 246));
    const auto noriaGate = Client::Tables::ReadGateRecord(*m_data->gates, 25);
    ASSERT_TRUE(noriaGate.has_value());
    ASSERT_EQ(noriaGate->map, 3);
    ASSERT_GT(noriaGate->x1, 2);
    const auto gateCentreX = static_cast<std::uint8_t>((noriaGate->x1 + noriaGate->x2) / 2);
    Teleport(3, gateCentreX, static_cast<std::uint8_t>(noriaGate->y2 + 2), 23);
    ASSERT_EQ(m_online->session.State(), Client::Session::SessionState::InWorld);
    ASSERT_EQ(Sent().packets.size(), 1u);
    ASSERT_EQ(m_world->Runtime().map, 3);
    WaitForInput();
    ASSERT_NO_FATAL_FAILURE(ExpectWalkThenGate(25, gateCentreX, noriaGate->y1));
    Teleport(0, Client::Tests::T16::HomeX, Client::Tests::T16::HomeY, 25);
    ASSERT_EQ(m_online->session.State(), Client::Session::SessionState::InWorld);
    ASSERT_EQ(Sent().packets.size(), 1u);
    WaitForInput();
    ASSERT_NO_FATAL_FAILURE(DrawCompleteFrame());
    Regen(0);
    ASSERT_EQ(m_online->session.State(), Client::Session::SessionState::InWorld);
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
            const auto& terrain = m_states->terrain.current;
            if (Hero().movement != 0)
            {
                EXPECT_FLOAT_EQ(Hero().position.z, static_cast<float>(Client::Terrain::TerrainHeightAt(
                                                       *terrain.heights, *terrain.attributes, Hero().position.x,
                                                       Hero().position.y, m_camera.fixedHeight)));
            }
            else
            {
                // MovePath sets XY on arrival; MoveHero no longer calls 0x508a80 and keeps the previous Z.
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
        EXPECT_TRUE(std::ranges::any_of(sent.packets, [](const auto& packet)
                                        { return packet.Opcode() == Client::Network::Opcode{0xd3}; }));
    }
};

// In Lost Tower (World5) 70..73 has no floor (0x08 of the .att); 205..208 × 87..90, south of the safe zone, is free
// floor. In the Arena (World7), 62..65 × 47..50 is free floor outside the safe zone, with eleven 0x15 (the running V)
// and four 9 (the bone 1 sprite) within 1500. In Atlans (World8), 226..229 × 103..106 is free floor outside the safe
// zone, with five 0x16 (the bubbles), seven 0x20 and four 0x22 within 1500 and 70 cells with layer 2 in the water
// nearby. In Icarus (World11), 20..23 × 29..32 is the free block with the most objects nearby (76 within 1500), so
// that the per-object step (0x6063d0) and its clouds run. In Aida (World34), 104..107 × 165..168 is free floor
// outside the safe zone, with 0x29 (the mesh 0 of 0x4255f3) and 0x46 nearby. Land of Trials (World32) has no safe
// zone; 149..152 × 104..107 is free floor with eight of its types with a case within 1500, and the camera sees the 3
// and the 0x35 that emit and the 0x34 that is drawn twice. Devil Square (World10, also via 32) has no safe zone;
// 122..125 × 174..177 is free floor with 38 of the 2, which rains. The Blood Castles (World12): 12..15 × 90..93 is
// free floor with four 0xd (the pulsing sprite) and four 0x25 (the torches) within 1500. In Castle Siege (World31),
// 65..68 × 130..133 is free floor (no 0x04 or 0x08 in the .att) outside the fog of 30, which stays off, and outside
// the safe zone: at the end the camera sees the 0 torches at (77, 133) and (67, 148) and thirteen 0x34 braziers (the
// nine at (60..61, 136..138), the three at (59, 149..151) and the one at (67, 137)), and the last frame draws their
// sparks 0x6ec/0 and their falls 0x6f4/6 (0x42c9f0). In Crywolf (World35), with state 0 of the .bss, 128..131 ×
// 30..33 is free floor (EncTerrain35.att, via Data::Terrain::LoadTerrainAttributes) in the fort's safe zone; at the
// end the camera sees three 0x29 (the sprites that go up and down), four 0x38 (the decals 0x61d/3), 0x4a, 0x49 and
// the hidden 0x52. EncTerrain35.obj has no 0x48: its smoke 0x5f2/33 does not come out with the repo's Data
// (CrywolfObjectServicesTest and CrywolfParticlesTest).
TEST_P(GameT16MapWalk, RealTerrainPicksAndMovesStraightThenAroundAnElbowBetweenCompleteFrames)
{
    const auto map = static_cast<std::uint8_t>(GetParam());
    const bool devilSquare = map == 9 || map == 32;
    const bool bloodCastle = map >= 11 && map <= 17;
    const auto start = [map, devilSquare, bloodCastle](std::uint8_t lostTower, std::uint8_t arena, std::uint8_t atlans,
                                                       std::uint8_t icarus, std::uint8_t siege, std::uint8_t trials,
                                                       std::uint8_t aida, std::uint8_t devil, std::uint8_t blood,
                                                       std::uint8_t crywolf) -> std::uint8_t
    {
        if (map == 30)
            return siege;
        if (map == 4)
            return lostTower;
        if (map == 6)
            return arena;
        if (map == 7)
            return atlans;
        if (devilSquare)
            return devil;
        if (map == 10)
            return icarus;
        if (bloodCastle)
            return blood;
        if (map == 31)
            return trials;
        if (map == 34)
            return crywolf;
        return map == 33 ? aida : 70;
    };
    const std::uint8_t x = start(205, 62, 226, 20, 65, 149, 104, 122, 12, 128);
    const std::uint8_t y = start(87, 47, 103, 29, 130, 104, 165, 174, 90, 30);
    const auto far = [](std::uint8_t cell) { return static_cast<std::uint8_t>(cell + 3); };
    Teleport(map, x, y);
    ASSERT_EQ(m_online->session.State(), Client::Session::SessionState::InWorld);
    OpenScene();
    ASSERT_NO_FATAL_FAILURE(WaitForInput());
    ASSERT_NO_FATAL_FAILURE(ClickCell(far(x), y));
    ASSERT_NO_FATAL_FAILURE(ExpectWalkPacket());
    ASSERT_NO_FATAL_FAILURE(FinishWalk(far(x), y));
    ASSERT_NO_FATAL_FAILURE(ClickCell(far(x), far(y)));
    ASSERT_NO_FATAL_FAILURE(ExpectWalkPacket());
    ASSERT_NO_FATAL_FAILURE(FinishWalk(far(x), far(y)));
    if (map == 6 || map == 7 || devilSquare || bloodCastle || map == 30 || map == 31 || map == 33 || map == 34)
    {
        const auto types = map == 6      ? std::array<std::int16_t, 2>{9, 0x15}
                           : map == 30   ? std::array<std::int16_t, 2>{0, 0x34}
                           : map == 7    ? std::array<std::int16_t, 2>{0x16, 0x20}
                           : devilSquare ? std::array<std::int16_t, 2>{2, 2}
                           : bloodCastle ? std::array<std::int16_t, 2>{0xd, 0x25}
                           : map == 31   ? std::array<std::int16_t, 2>{0x34, 0x35}
                           : map == 34   ? std::array<std::int16_t, 2>{0x29, 0x38}
                                         : std::array<std::int16_t, 2>{0x29, 0x46};
        for (const std::int16_t type : types)
            EXPECT_TRUE(std::ranges::any_of(m_world->Map().objects, [type](const auto& object)
                                            { return object.alive && object.seen && object.type == type; }))
                << type;
    }
    if (map == 30)
    {
        // The torches' sparks (Effect\fantaF.jpg) and the braziers' falls (Effect\WATERFALL5.jpg).
        bool spark = false;
        bool fall = false;
        for (const auto& event : m_target.frames.back().events)
            for (const auto& command : event.modelCommands)
                if (command.vertexCount != 0 && command.state.texture)
                {
                    spark |= command.state.texture->value == 0x6ec;
                    fall |= command.state.texture->value == 0x6f4;
                }
        // Outside the rectangle the fog stays off on every stretch.
        for (const auto& event : m_target.frames.back().events)
            for (const auto& command : event.modelCommands)
                EXPECT_FALSE(command.fog.has_value()) << command.firstVertex;
        EXPECT_TRUE(spark);
        EXPECT_TRUE(fall);
    }
}

// 0x42a760: loading 30 through the 1C sends the C1 04 B9 02 (XOR32) before 0x4c6e10's F3 12.
TEST_F(GameT16Journey, CastleSiegeAsksForItsStateBeforeConfirmingTheTeleport)
{
    static_cast<void>(Sent());
    Teleport(30, 92, 98);
    ASSERT_EQ(m_online->session.State(), Client::Session::SessionState::InWorld);
    ASSERT_EQ(m_world->Runtime().map, 30);
    const auto sent = Sent();
    ASSERT_EQ(sent.packets.size(), 2u);
    EXPECT_EQ(std::vector<std::byte>(sent.packets[0].Bytes().begin(), sent.packets[0].Bytes().end()),
              Client::Tests::Bytes({0xc1, 4, 0xb9, 2}));
    EXPECT_EQ(sent.packets[1].Opcode(), Client::Network::Opcode{0xf3});
}

// 0x63741d..0x637490 and 0x6375ea..0x63763c (Q-C334, Q-C390): in Castle Siege, with the hero in the fog rectangle,
// the whole world pass, the eye pass too (0x6213e0), carries 0x42b0a0's linear black fog from 2000 to 2700. 165 ×
// 191 is free floor inside, in the southwest, with the 0 and the 0x34 more than 2200 away.
TEST_F(GameT16Journey, CastleSiegeFogsTheWholeWorldPassInsideItsRectangle)
{
    Teleport(30, 165, 191);
    ASSERT_EQ(m_online->session.State(), Client::Session::SessionState::InWorld);
    OpenScene();
    ASSERT_NO_FATAL_FAILURE(WaitForInput());
    ASSERT_TRUE(Client::CastleSiege::InTheCastleSiegeFog(Hero().position));
    const auto before = m_target.frames.size();
    ASSERT_NO_FATAL_FAILURE(DrawCompleteFrame());
    constexpr Client::Render::ModelFog Fog{.color = glm::vec3(0.0f), .start = 2000.0f, .end = 2700.0f};
    std::size_t fogged = 0;
    std::size_t passes = 0;
    for (const auto& event : m_target.frames[before].events)
    {
        if (event.kind != Client::Tests::FrameEvent::Kind::Models)
            continue;
        ++passes;
        for (const auto& command : event.modelCommands)
        {
            if (command.vertexCount == 0)
                continue;
            EXPECT_EQ(command.fog, Fog) << "pase " << passes << " desde el vértice " << command.firstVertex;
            ++fogged;
        }
    }
    // The world pass and the eye pass.
    EXPECT_EQ(passes, 2u);
    EXPECT_GT(fogged, 0u);
}

// 0x635b3d, 0x5fb570 and 0x6371a0 (Q-C152): in the Arena, with "webzen" in the hero's name, the spectator camera:
// yaw −40, far 5000 and D 3200 from the next frame, without reading the hero's cell; the trapezoid is its own, around
// (6400; 15400) with the hero's z and rotated −yaw. The frame is complete.
TEST_F(GameT16Journey, AWebzenNameWatchesTheArenaFromTheSpectatorCamera)
{
    Teleport(6, 62, 47);
    ASSERT_EQ(m_online->session.State(), Client::Session::SessionState::InWorld);
    constexpr std::string_view Name = "xwebzenx";
    Hero().name.fill(std::byte{0});
    for (std::size_t index = 0; index < Name.size(); ++index)
        Hero().name[index] = static_cast<std::byte>(Name[index]);
    OpenScene();
    ASSERT_NO_FATAL_FAILURE(WaitForInput());
    ASSERT_NO_FATAL_FAILURE(DrawCompleteFrame());
    EXPECT_TRUE(m_cameraInput.webzen);
    EXPECT_EQ(m_camera.yaw, -40.0f);
    EXPECT_EQ(m_camera.farPlane, 5000.0f);
    EXPECT_EQ(m_camera.distance, 3200.0f);
    Client::Terrain::TerrainPolygonInput input;
    input.scene = 5;
    input.map = 6;
    input.translation = {6400.0f, 15400.0f, Hero().position.z};
    input.objectPosition = {6400.0f, 15400.0f};
    input.cameraYaw = -40.0f;
    input.webzenProfile = true;
    input.webzenYaw = true;
    const auto expected = Client::Terrain::TerrainPolygonOf(input, m_states->camera.trapezoidLevel.effectiveLevel);
    ASSERT_TRUE(expected.HasValue());
    EXPECT_EQ(m_states->camera.trapezoid.x, expected.Value().x);
    EXPECT_EQ(m_states->camera.trapezoid.y, expected.Value().y);
}

// The Dungeon (World2) has no cells with the safe zone bit 0x01; the others do.
class GameT16SafeZoneWalk : public GameT16MapWalk
{
};
TEST_P(GameT16SafeZoneWalk, RealSafeZoneCellsResetRunWhileTheirLogicalCellAdvances)
{
    const auto map = static_cast<std::uint8_t>(GetParam());
    // The Arena (World7): 70..72 × 139 is free floor of the safe zone. Atlans (World8): the safe zone goes from x 11 to
    // 34 and y 8 to 28; 13..15 × 17 is free floor. Tarkan (World9): the safe zone goes from x 155 to 251 and y 28 to
    // 97; 206..208 × 56 is free floor. The Blood Castles (World12): 12..14 × 3.
    constexpr std::array<std::uint8_t, 9> SafeX{132, 0, 185, 166, 195, 0, 70, 13, 206};
    constexpr std::array<std::uint8_t, 9> SafeY{89, 0, 30, 81, 72, 0, 139, 17, 56};
    const bool bloodCastle = map >= 11 && map <= 17;
    const std::uint8_t x = bloodCastle ? 12 : SafeX[map];
    const std::uint8_t y = bloodCastle ? 3 : SafeY[map];
    Teleport(map, x, y);
    ASSERT_EQ(m_online->session.State(), Client::Session::SessionState::InWorld);
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
    ASSERT_EQ(m_online->session.State(), Client::Session::SessionState::InWorld);
    OpenScene();
    ASSERT_NO_FATAL_FAILURE(WaitForInput());
    const auto start = Hero().position;
    ASSERT_NO_FATAL_FAILURE(ClickCell(255, 255));
    const auto sent = Sent();
    EXPECT_FALSE(std::ranges::any_of(sent.packets, [](const auto& packet)
                                     { return packet.Opcode() == Client::Network::Opcode{0xd3}; }));
    EXPECT_EQ(Hero().position, start);
    EXPECT_EQ(Hero().movement, 0);
    RecordProperty("border_scope", "real outer wall/picking rejection; no fabricated traversable border");
}

// Chaos Castle (World19 for 18 to 23) enters through the F3 04: the 1C stops at 0x4c7b34 (the commands, CL9.4). The
// castle, 23..44 × 75..108, is safe zone in the .att; 30..33 × 90..93 is in the middle, with the twenty-two 3 of the
// walls in view. The armed trap of the .data (0x6c634d) lights the first 3 seen (0x4055b0), which on the next drawing
// releases its 0x615/2 from bone 1 (0x405d49) and lowers the count to 4; then the prelude (0x405490) chooses others.
// The clouds from 7 to 0xc in view release their 0x622 and hide.
class GameT16ChaosCastleWalk : public GameT16MapWalk
{
};
TEST_P(GameT16ChaosCastleWalk, RegenWalksInsideTheCastleAndTheArmedTrapFires)
{
    const auto map = static_cast<std::uint8_t>(GetParam());
    constexpr std::uint8_t X = 30;
    constexpr std::uint8_t Y = 90;
    Regen(map, X, Y);
    ASSERT_FALSE(m_pollFailure) << *m_pollFailure;
    ASSERT_EQ(m_online->session.State(), Client::Session::SessionState::InWorld);
    ASSERT_EQ(m_world->Runtime().map, map);
    OpenScene();
    ASSERT_NO_FATAL_FAILURE(WaitForInput());
    ASSERT_EQ(Hero().safeZone, 1);
    ASSERT_NO_FATAL_FAILURE(ClickCell(X + 3, Y));
    ASSERT_NO_FATAL_FAILURE(ExpectWalkPacket());
    ASSERT_NO_FATAL_FAILURE(FinishWalk(X + 3, Y));
    ASSERT_NO_FATAL_FAILURE(ClickCell(X + 3, Y + 3));
    ASSERT_NO_FATAL_FAILURE(ExpectWalkPacket());
    ASSERT_NO_FATAL_FAILURE(FinishWalk(X + 3, Y + 3));
    EXPECT_EQ(m_world->Runtime().chaosCastleTrapArmed, 0);
    // A 3 that fired lowers +0x60 from 10 each frame; the prelude picks one of the seen ones again.
    EXPECT_TRUE(std::ranges::any_of(
        m_world->Map().objects, [](const auto& object)
        { return object.alive && object.type == 3 && object.field60 >= 4 && object.field60 < 10; }));
    // The seen clouds (6 to 0xc) release their 0x622 once and stay hidden.
    EXPECT_TRUE(std::ranges::any_of(m_world->Map().objects,
                                    [](const auto& object) {
                                        return object.alive && object.seen && object.type >= 6 && object.type <= 0xc &&
                                               object.state.hiddenMesh == -2;
                                    }));
    // The hero goes with the Angel (0x1f7) and its chrome: 0x609640 with 6 passes mesh 0 with 0x46 through Chrome01
    // (0x5c0).
    ASSERT_FALSE(m_target.frames.empty());
    bool chrome = false;
    for (const auto& event : m_target.frames.back().events)
        for (const auto& command : event.modelCommands)
            chrome |= command.vertexCount != 0 && command.state.texture && command.state.texture->value == 0x5c0;
    EXPECT_TRUE(chrome);
    RecordProperty("walk_scope", "F304 entry; real picking; D3 echo; armed trap 615/2; Angel body; full frames");
}
INSTANTIATE_TEST_SUITE_P(AllSixChaosCastles, GameT16ChaosCastleWalk, ::testing::Values(18, 19, 20, 21, 22, 23),
                         [](const ::testing::TestParamInfo<int>& parameter)
                         { return "Map" + std::to_string(parameter.param); });

class GameT16KalimaWalk : public GameT16MapWalk
{
};
TEST_P(GameT16KalimaWalk, RealPickingLiteralD3AndEchoAdvanceTheHeroAndEmitNativeFootsteps)
{
    const auto map = static_cast<std::uint8_t>(GetParam());
    Teleport(map, 62, 67);
    ASSERT_FALSE(m_pollFailure) << *m_pollFailure;
    ASSERT_EQ(m_online->session.State(), Client::Session::SessionState::InWorld);
    OpenScene();
    ASSERT_NO_FATAL_FAILURE(WaitForInput());
    const auto start = Hero().position;
    ASSERT_NO_FATAL_FAILURE(ClickCell(64, 67));
    const auto sent = Sent();
    const auto walk = std::ranges::find_if(sent.packets, [](const auto& packet)
                                           { return packet.Opcode() == Client::Network::Opcode{0xd3}; });
    ASSERT_NE(walk, sent.packets.end()) << "picked=" << m_world->Runtime().movement.targetX << ","
                                        << m_world->Runtime().movement.targetY
                                        << " time=" << m_world->Runtime().mouse.updateTime << "/"
                                        << m_world->Runtime().mouse.updateTimeMax
                                        << " block=" << m_world->Runtime().movement.movementBlocked
                                        << " walls=" << m_states->terrain.current.attributes->cells[64 * 256 + 64]
                                        << "," << m_states->terrain.current.attributes->cells[64 * 256 + 65] << ","
                                        << m_states->terrain.current.attributes->cells[64 * 256 + 66];
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

INSTANTIATE_TEST_SUITE_P(CompleteSceneMaps, GameT16MapWalk,
                         ::testing::Values(0, 1, 2, 3, 4, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 30, 31, 32, 33,
                                           34),
                         [](const ::testing::TestParamInfo<int>& mapParameter)
                         { return "Map" + std::to_string(mapParameter.param); });
INSTANTIATE_TEST_SUITE_P(CompleteSceneMapsWithSafeZone, GameT16SafeZoneWalk,
                         ::testing::Values(0, 2, 3, 4, 6, 7, 8, 11, 12, 13, 14, 15, 16, 17),
                         [](const ::testing::TestParamInfo<int>& mapParameter)
                         { return "Map" + std::to_string(mapParameter.param); });
} // namespace
