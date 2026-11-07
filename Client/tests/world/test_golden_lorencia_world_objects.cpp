#include "World/WorldObjects.h"

#include "Data/Terrain/TerrainObjects.h"
#include "World/WorldObjectRegistrations.h"
#include "World/WorldTerrain.h"

#include "support/GoldenDir.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <map>

// Los 2.870 registros y 109 tipos de Data/world1/EncTerrain1.obj; los expected son la tabla ASM
// cerrada de 0x606660 (06 § Lo propio de Lorencia), no un fixture producido por el port ni emulación.
namespace
{
class GoldenLorenciaWorldObjects : public Client::Tests::GoldenDataTest
{
};

// Oráculos literales para las seis escalas de Tree01/02 de este Data. Contrastados con cociente
// racional exacto y redondeo al binary32 más cercano, fuera del algoritmo de producción.
std::uint32_t ExpectedTreeSpeed(float scale)
{
    const std::map<std::uint32_t, std::uint32_t> speeds{{0x3F2E1480u, 0x3F169692u}, {0x3F333338u, 0x3F124921u},
                                                        {0x3F570A40u, 0x3EF3CF3Au}, {0x3F800000u, 0x3ECCCCCDu},
                                                        {0x3F1EB858u, 0x3F252944u}, {0x3F3D70A8u, 0x3F0A60DBu}};
    const auto found = speeds.find(std::bit_cast<std::uint32_t>(scale));
    EXPECT_NE(found, speeds.end()) << "escala no inventariada";
    return found != speeds.end() ? found->second : 0;
}

TEST_F(GoldenLorenciaWorldObjects, AllFileRecordsKeepTheirOrderAndReceiveTheClosedConstructorRules)
{
    const auto records = Client::World::LoadWorldObjects(m_dataRoot, 0);
    ASSERT_TRUE(records.HasValue()) << records.Error();
    ASSERT_EQ(records.Value().size(), 2870u);
    Client::World::WorldObjectRegistrations registry;
    const auto objects =
        Client::World::CreateWorldObjects(records.Value(), 0, Client::World::ObjectScene::Other, registry);
    ASSERT_EQ(objects.size(), 2870u);
    const std::map<std::int16_t, std::int16_t> blendByType{{52, 1},  {90, 1},  {150, 1}, {98, 2}, {119, 2},
                                                           {105, 3}, {117, 4}, {122, 4}, {118, 8}};
    const std::map<std::int16_t, std::uint32_t> speedByType{
        {59, 0u}, {90, 0x3E99999Au}, {150, 0x3E99999Au}, {96, 0x3E99999Au}, {97, 0x3E99999Au}};
    std::map<std::int16_t, std::size_t> counts;
    std::size_t registered = 0;
    for (std::size_t index = 0; index < objects.size(); ++index)
    {
        SCOPED_TRACE(index);
        const auto& record = records.Value()[index];
        const auto& object = objects[index];
        SCOPED_TRACE(record.type);
        ++counts[record.type];
        EXPECT_EQ(object.type, record.type);
        EXPECT_EQ(object.identity, index + 1);
        EXPECT_EQ(object.transform.position, glm::vec3(record.position[0], record.position[1], record.position[2]));
        EXPECT_EQ(object.basePosition, object.transform.position);
        EXPECT_EQ(object.transform.angles, glm::vec3(record.angle[0], record.angle[1], record.angle[2]));
        EXPECT_EQ(std::bit_cast<std::uint32_t>(object.transform.scale), std::bit_cast<std::uint32_t>(record.scale));
        const auto blend = blendByType.find(record.type);
        EXPECT_EQ(object.state.blendMesh, blend == blendByType.end() ? -1 : blend->second);
        const auto speed = speedByType.find(record.type);
        const std::uint32_t expectedSpeed = record.type == 0 || record.type == 1 ? ExpectedTreeSpeed(record.scale)
                                            : speed != speedByType.end()         ? speed->second
                                                                                 : 0x3E23D70Au;
        EXPECT_EQ(std::bit_cast<std::uint32_t>(object.speed), expectedSpeed);
        const bool tree = record.type == 0 || record.type == 1;
        EXPECT_EQ(object.boundsMinimum, tree ? glm::vec3(-150.0f, -150.0f, 0.0f) : glm::vec3(-40.0f, -40.0f, 0.0f));
        EXPECT_EQ(object.boundsMaximum, tree                 ? glm::vec3(150.0f, 150.0f, 500.0f)
                                        : record.type == 133 ? glm::vec3(40.0f, 40.0f, 160.0f)
                                                             : glm::vec3(40.0f, 40.0f, 80.0f));
        EXPECT_EQ(object.state.hiddenMesh, record.type == 133 ? -2 : -1);
        EXPECT_EQ(object.cullingMargin, record.type == 80 ? -50.0f : -30.0f);
        EXPECT_TRUE(object.alive);
        EXPECT_FALSE(object.seen);
        EXPECT_TRUE(object.vertexLight);
        EXPECT_EQ(object.alpha, 1.0f);
        EXPECT_EQ(object.targetAlpha, 1.0f);
        EXPECT_EQ(object.light, glm::vec3(0.0f));
        if (record.type == 6 || record.type == 133 || record.type == 145 || record.type == 146)
        {
            ASSERT_LT(registered, registry.entries.size());
            EXPECT_EQ(registry.entries[registered].active, 1);
            EXPECT_EQ(registry.entries[registered].object, object.identity);
            ++registered;
        }
    }
    EXPECT_EQ(counts.size(), 109u);
    EXPECT_EQ(counts[0], 80u);
    EXPECT_EQ(counts[1], 23u);
    EXPECT_EQ(counts[6], 36u);
    EXPECT_EQ(counts[133], 53u);
    EXPECT_EQ(counts[145], 18u);
    EXPECT_EQ(counts[146], 3u);
    EXPECT_EQ(registered, 110u);
    EXPECT_EQ(registry.entries[0].object, 107u);
    EXPECT_EQ(registry.entries[1].object, 108u);
    EXPECT_EQ(registry.entries[109].object, 2762u);
    EXPECT_EQ(registry.entries[110].active, 0);
    EXPECT_EQ(registry.nextIdentity, 2871u);
}
} // namespace
