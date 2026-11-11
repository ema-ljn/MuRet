#include "test_game_t16_support.h"
#include "App/SessionState.h"
#include "Scenes/WorldMap.h"
#include "Render/ModelBatch.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <string>

namespace
{
class GameT16SceneMatrix : public Client::Tests::T16::SceneTest, public ::testing::WithParamInterface<int>
{
};

// La entrada se aplica en la sesión; no saltea mapas ni fabrica un terreno para pasar el paso.
// Un corte se registra antes de reclamar recta/codo/borde/safeZone de esa fila.
TEST_P(GameT16SceneMatrix, RealEntryReachesACompleteSceneFrameOrAnExplicitMapBoundary)
{
    const auto map = static_cast<std::uint8_t>(GetParam());
    const bool kalima = (map >= 24 && map <= 29) || map == 36;
    // World25 tiene objetos hasta 12941/12209: Home queda fuera. 64,64 es la entrada poblada.
    Teleport(map, kalima ? 64 : Client::Tests::T16::HomeX, kalima ? 64 : Client::Tests::T16::HomeY);
    RecordProperty("entry_cells", kalima ? "64,64" : "135,125");
    const auto transition = Client::Tests::T16::TeleportBoundary(map);
    if (!transition.empty())
    {
        ASSERT_TRUE(m_pollFailure.has_value());
        EXPECT_NE(m_pollFailure->find(transition), std::string::npos) << *m_pollFailure;
        if (map == 5 || map == 35)
        {
            const std::string expected = map == 5 ? "Data\\World6\\TerrainHeight.OZB file not found."
                                                  : "Data\\World36\\EncTerrain36.att file not found.";
            EXPECT_EQ(m_world->Runtime().fatalDataMessage, expected);
            RecordProperty("blocked_phase", "fatal data: " + expected);
        }
        else
        {
            EXPECT_FALSE(m_world->Runtime().fatalDataMessage.has_value());
            RecordProperty("blocked_phase", "unsupported 1C: " + std::string(transition));
        }
        return;
    }
    ASSERT_EQ(m_online->session.State(), Client::App::SessionState::InWorld);
    OpenScene();
    ASSERT_NO_FATAL_FAILURE(Step());
    if (kalima)
    {
        ASSERT_FALSE(m_scene->Failure().has_value()) << *m_scene->Failure();
    }
    if (!m_scene->Failure())
    {
        if (map <= 4 || (map >= 8 && map <= 17) || map == 30 || map == 31 || map == 32 || map == 33 || map == 34 ||
            kalima)
        {
            ASSERT_NO_FATAL_FAILURE(DrawCompleteFrame());
        }
        else
        {
            Draw();
        }
    }
    if (kalima)
    {
        ASSERT_FALSE(m_scene->Failure().has_value()) << *m_scene->Failure();
        EXPECT_EQ(m_world->Map().objects.size(), 2266u);
        EXPECT_TRUE(std::ranges::any_of(m_world->Map().objects, [](const auto& object)
                                        { return object.alive && object.seen && object.type == 40; }));
        ASSERT_FALSE(m_target.frames.empty());
        EXPECT_TRUE(m_target.frames.back().presented);
        bool water = false;
        bool particle = false;
        for (const auto& event : m_target.frames.back().events)
            for (const auto& command : event.modelCommands)
                if (command.vertexCount && command.state.texture)
                {
                    water |= command.state.texture->value == 0x23;
                    particle |= command.state.texture->value == 0x6f1;
                }
        EXPECT_TRUE(water);
        EXPECT_TRUE(particle);
        RecordProperty("blocked_phase", "none on populated entry frame; cycle covered by KalimaLiveObjectsTest");
        return;
    }
    if (map <= 4 || (map >= 6 && map <= 17) || (map >= 30 && map <= 34))
    {
        ASSERT_FALSE(m_scene->Failure().has_value()) << *m_scene->Failure();
        ASSERT_FALSE(m_target.frames.empty());
        EXPECT_TRUE(m_target.frames.back().presented);
        EXPECT_TRUE(m_scene->MouseRay().has_value());
        RecordProperty("blocked_phase", "none on entry frame; locomotion has separate cases");
        return;
    }
    ASSERT_TRUE(m_scene->Failure().has_value()) << "declarar una fila implementada requiere probar su caminata";
    RecordProperty("blocked_phase", *m_scene->Failure());
    EXPECT_NE(m_scene->Failure()->find("0x6374ce"), std::string::npos) << *m_scene->Failure();
}

INSTANTIATE_TEST_SUITE_P(AllMaps0Through36, GameT16SceneMatrix, ::testing::Range(0, 37),
                         [](const ::testing::TestParamInfo<int>& mapParameter)
                         { return "Map" + std::to_string(mapParameter.param); });
} // namespace
