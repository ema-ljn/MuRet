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
#include "World/TerrainCulling.h"
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
// segura, es suelo libre. En la Arena (World7), 62..65 × 47..50 es suelo libre fuera de la zona segura, con once
// 0x15 (la V que corre) y cuatro 9 (el sprite del hueso 1) a menos de 1.500. En Atlans (World8), 226..229 ×
// 103..106 es suelo libre fuera de la zona segura, con cinco 0x16 (las burbujas), siete 0x20 y cuatro 0x22 a menos
// de 1.500 y 70 celdas con la capa 2 en el agua cerca. En Icarus (World11), 20..23 × 29..32 es el
// bloque libre con más objetos cerca (76 a menos de 1.500), para que el paso por objeto (0x6063d0) y sus nubes corran.
// En Aida (World34), 104..107 × 165..168 es suelo libre fuera de la zona segura, con 0x29 (la malla 0 de 0x4255f3) y
// 0x46 cerca. Land of Trials (World32) no tiene zona segura; 149..152 × 104..107 es suelo libre con ocho de sus tipos
// con caso a menos de 1.500, y la cámara ve el 3 y el 0x35 que emiten y el 0x34 que se dibuja dos veces. Devil Square
// (World10, también por el 32) no tiene zona segura; 122..125 × 174..177 es suelo libre con 38 del 2, que llueve. Los
// Blood Castle (World12): 12..15 × 90..93 es suelo libre con cuatro 0xd (el sprite que pulsa) y cuatro 0x25 (las
// antorchas) a menos de 1.500. En Castle Siege (World31), 65..68 × 130..133 es suelo libre (sin 0x04 ni 0x08 en el
// .att) fuera de la niebla del 30 y de la zona segura: al final la cámara ve las antorchas 0 de (77, 133) y (67, 148)
// y trece braseros 0x34 (los nueve de (60..61, 136..138), los tres de (59, 149..151) y el de (67, 137)), y el último
// cuadro dibuja sus chispas 6ec/0 y sus caídas 6f4/6 (0x42c9f0). En Crywolf
// (World35), con el estado 0 de la .bss, 128..131 × 30..33 es suelo libre (EncTerrain35.att, por
// Data::Terrain::LoadTerrainAttributes) en la zona segura del fuerte; al final la cámara ve tres 0x29 (los sprites que
// suben y bajan), cuatro 0x38 (los calcos 0x61d/3), 0x4a, 0x49 y los 0x52 ocultos. EncTerrain35.obj no trae ningún
// 0x48: su humo 5f2/33 no sale con la Data del repo (CrywolfObjectServicesTest y CrywolfParticlesTest).
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
    ASSERT_EQ(m_online->session.State(), Client::App::SessionState::InWorld);
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
        // Las chispas de las antorchas (Effect\fantaF.jpg) y las caídas de los braseros (Effect\WATERFALL5.jpg).
        bool spark = false;
        bool fall = false;
        for (const auto& event : m_target.frames.back().events)
            for (const auto& command : event.modelCommands)
                if (command.vertexCount != 0 && command.state.texture)
                {
                    spark |= command.state.texture->value == 0x6ec;
                    fall |= command.state.texture->value == 0x6f4;
                }
        EXPECT_TRUE(spark);
        EXPECT_TRUE(fall);
    }
}

// 0x42a760: la carga del 30 por el 1C manda el C1 04 B9 02 (XOR32) antes del F3 12 de 0x4c6e10.
TEST_F(GameT16Journey, CastleSiegeAsksForItsStateBeforeConfirmingTheTeleport)
{
    static_cast<void>(Sent());
    Teleport(30, 92, 98);
    ASSERT_EQ(m_online->session.State(), Client::App::SessionState::InWorld);
    ASSERT_EQ(m_world->Runtime().map, 30);
    const auto sent = Sent();
    ASSERT_EQ(sent.packets.size(), 2u);
    EXPECT_EQ(std::vector<std::byte>(sent.packets[0].Bytes().begin(), sent.packets[0].Bytes().end()),
              Client::Tests::Bytes({0xc1, 4, 0xb9, 2}));
    EXPECT_EQ(sent.packets[1].Opcode(), 0xf3);
}

// 0x635b3d, 0x5fb570 y 0x6371a0 (Q-C152): en la Arena, con "webzen" en el nombre del héroe, la cámara de
// espectador: yaw −40, far 5.000 y D 3.200 desde el frame siguiente, sin leer la celda del héroe; el trapecio
// es el propio, alrededor de (6.400; 15.400) con la z del héroe y girado −yaw. El cuadro es completo.
TEST_F(GameT16Journey, AWebzenNameWatchesTheArenaFromTheSpectatorCamera)
{
    Teleport(6, 62, 47);
    ASSERT_EQ(m_online->session.State(), Client::App::SessionState::InWorld);
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
    Client::World::TerrainPolygonInput input;
    input.scene = 5;
    input.map = 6;
    input.translation = {6400.0f, 15400.0f, Hero().position.z};
    input.objectPosition = {6400.0f, 15400.0f};
    input.cameraYaw = -40.0f;
    input.webzenProfile = true;
    input.webzenYaw = true;
    const auto expected = Client::World::TerrainPolygonOf(input, m_world->Runtime().terrainPolygonState.effectiveLevel);
    ASSERT_TRUE(expected.HasValue());
    EXPECT_EQ(m_world->Runtime().terrainPolygon.x, expected.Value().x);
    EXPECT_EQ(m_world->Runtime().terrainPolygon.y, expected.Value().y);
}

// El Dungeon (World2) no tiene celdas con el bit 0x01 de la zona segura; los demás, sí.
class GameT16SafeZoneWalk : public GameT16MapWalk
{
};
TEST_P(GameT16SafeZoneWalk, RealSafeZoneCellsResetRunWhileTheirLogicalCellAdvances)
{
    const auto map = static_cast<std::uint8_t>(GetParam());
    // La Arena (World7): 70..72 × 139 es suelo libre de la zona segura. Atlans (World8): la zona segura va de x 11
    // a 34 e y 8 a 28; 13..15 × 17 es suelo libre. Tarkan (World9): la zona segura va de x
    // 155 a 251 e y 28 a 97; 206..208 × 56 es suelo libre. Los Blood Castle (World12): 12..14 × 3.
    constexpr std::array<std::uint8_t, 9> SafeX{132, 0, 185, 166, 195, 0, 70, 13, 206};
    constexpr std::array<std::uint8_t, 9> SafeY{89, 0, 30, 81, 72, 0, 139, 17, 56};
    const bool bloodCastle = map >= 11 && map <= 17;
    const std::uint8_t x = bloodCastle ? 12 : SafeX[map];
    const std::uint8_t y = bloodCastle ? 3 : SafeY[map];
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

// Chaos Castle (World19 por el 18 al 23) entra por el F3 04: el 1C corta en 0x4906a0 (la UI del castillo, CL8).
// El castillo, 23..44 × 75..108, es zona segura en el .att; 30..33 × 90..93 queda en el medio, con los veintidós 3
// de las paredes a la vista. La trampa armada de la .data (0x6c634d) prende el primer 3 visto (0x4055b0), que en el
// dibujo siguiente suelta su 615/2 desde el hueso 1 (0x405d49) y baja la cuenta hasta 4; después el preludio
// (0x405490) elige otros. Las nubes del 7 al 0xc que se ven sueltan sus 622 y se esconden.
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
    ASSERT_EQ(m_online->session.State(), Client::App::SessionState::InWorld);
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
    // Un 3 que disparó baja +0x60 desde 10 en cada frame; el preludio vuelve a elegir alguno de los vistos.
    EXPECT_TRUE(std::ranges::any_of(
        m_world->Map().objects, [](const auto& object)
        { return object.alive && object.type == 3 && object.field60 >= 4 && object.field60 < 10; }));
    // Las nubes vistas (6 a 0xc) sueltan sus 622 una vez y quedan escondidas.
    EXPECT_TRUE(std::ranges::any_of(m_world->Map().objects,
                                    [](const auto& object) {
                                        return object.alive && object.seen && object.type >= 6 && object.type <= 0xc &&
                                               object.state.hiddenMesh == -2;
                                    }));
    // El héroe va con el Angel (0x1f7) y su cromo: 0x609640 con 6 pasa la malla 0 con 0x46 por Chrome01 (0x5c0).
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
