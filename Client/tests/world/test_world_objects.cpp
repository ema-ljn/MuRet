#include "World/WorldObjects.h"

#include "Data/Terrain/TerrainObjects.h"
#include "Render/Models/ObjectState.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <array>
#include <bit>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

// Client::World::CreateWorldObjects, StepWorldObject y DrawOf en los casos que los 276 vectores del
// mapa 55 no tocan (06 § El mundo del login, la revisión de la CL7.2).
namespace
{
using Client::World::WorldObject;

constexpr std::uint8_t Login = 55;
constexpr std::uint8_t Lorencia = 0;

std::vector<WorldObject> Created(std::int16_t type, float x, float y, std::uint8_t map = Login)
{
    Client::Data::Terrain::TerrainObject record;
    record.type = type;
    record.position = {x, y, 0.0f};
    record.scale = 1.0f;
    return Client::World::CreateWorldObjects(std::span(&record, 1), map, Client::World::ObjectScene::Login);
}

// 0x60666c: la posición por 0,000625f en la x87, sin redondear el producto, y ftol. En los múltiplos de
// 1.600 el float sube al entero siguiente; la x87 no.
TEST(WorldObjectsTest, TheSectorIsTheProductWithoutRounding)
{
    EXPECT_EQ(Client::World::SectorX(Created(1, 1600.0f, 0.0f).front()), 0);
    EXPECT_EQ(Client::World::SectorX(Created(1, 14400.0f, 0.0f).front()), 8);
    EXPECT_EQ(Client::World::SectorX(Created(1, 8000.0f, 0.0f).front()), 4);
    const auto edge = Created(1, 25600.0f, 25600.0f);
    ASSERT_EQ(edge.size(), 1u) << "25.600 es el sector 15: se crea";
    EXPECT_EQ(Client::World::SectorX(edge.front()), 15);
    EXPECT_EQ(Client::World::SectorY(edge.front()), 15);
    EXPECT_EQ(Client::World::SectorX(Created(1, -1000.0f, 0.0f).front()), 0) << "ftol trunca hacia el 0";
    EXPECT_EQ(Client::World::SectorX(Created(1, -1600.0f, 0.0f).front()), 0) << "−0,99999…: todavía el 0";
    EXPECT_TRUE(Created(1, -1601.0f, 0.0f).empty());
    EXPECT_TRUE(Created(1, 25700.0f, 0.0f).empty());
}

// Un .obj con un NaN (World3, dos registros del tipo 86): ftol da 0x80000000 y 0x606660 no lo crea.
TEST(WorldObjectsTest, ANotANumberPositionIsNotCreated)
{
    EXPECT_TRUE(Created(0x56, 100.0f, std::numeric_limits<float>::quiet_NaN(), 3).empty());
}

// 0x606660 saca el sector de la posición del archivo, antes de su switch: los remolinos del 55, que el
// caso lleva a (8.600; 25.000; 5.000), quedan en el sector de donde estaban.
TEST(WorldObjectsTest, TheWhirlpoolsKeepTheSectorOfTheFile)
{
    const auto whirlpool = Created(0x56, 100.0f, 100.0f);
    ASSERT_EQ(whirlpool.size(), 1u);
    EXPECT_EQ(whirlpool.front().transform.position.x, 8600.0f);
    EXPECT_EQ(Client::World::SectorX(whirlpool.front()), 0);
    EXPECT_EQ(Client::World::SectorY(whirlpool.front()), 0);
}

// 0x604ab0 sale después del alpha si quedó por debajo de 0,01: no avanza ni corre los casos del mapa.
TEST(WorldObjectsTest, AnObjectFadedOutDoesNotStep)
{
    WorldObject whirlpool = Created(0x56, 100.0f, 100.0f).front();
    whirlpool.alpha = 0.005f;
    whirlpool.targetAlpha = 0.0f;
    const float yaw = whirlpool.transform.angles.z;
    Client::World::StepWorldObject(whirlpool, Login, nullptr);
    EXPECT_LT(whirlpool.alpha, 0.01f);
    EXPECT_EQ(whirlpool.transform.angles.z, yaw) << "el giro del caso 0x56 no corre";
    EXPECT_EQ(whirlpool.state.blendMesh, Client::Render::Models::NoBlendMesh);
}

// 0x5ffce0 sin luz por vértice: la luz del terreno por 0,1 de G y B se guarda en float antes de sumarle
// la del objeto; la de R se suma desde la x87. Con este R las dos cuentas difieren en el último bit.
TEST(WorldObjectsTest, TheRedOfAnUnlitObjectIsNotRoundedFirst)
{
    WorldObject logo = Created(1, 100.0f, 100.0f, Lorencia).front();
    logo.vertexLight = false;
    logo.light = glm::vec3(std::bit_cast<float>(0x3F142C32u));
    const glm::vec3 terrain(std::bit_cast<float>(0x3F01DAADu));
    const auto draw = Client::World::DrawOf(logo, terrain, Lorencia);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(draw.color.x), 0x3F212877u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(draw.color.y), 0x3F212876u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(draw.color.z), 0x3F212876u);
}

// 0x602ed0: la luz de los dos sprites del farol, L = (rand() % 30 + 70) · 0,01f guardado en float, y
// (L; L · 0,5; L · 0,2f), cada producto guardado en float.
TEST(WorldObjectsTest, TheLanternGlowIsOrange)
{
    const auto bits = [](const glm::vec3& light)
    {
        return std::array<std::uint32_t, 3>{std::bit_cast<std::uint32_t>(light.x),
                                            std::bit_cast<std::uint32_t>(light.y),
                                            std::bit_cast<std::uint32_t>(light.z)};
    };
    EXPECT_EQ(bits(Client::World::StreetLightGlowLight(0)),
              (std::array<std::uint32_t, 3>{0x3F333333u, 0x3EB33333u, 0x3E0F5C29u}));
    EXPECT_EQ(bits(Client::World::StreetLightGlowLight(59)),
              (std::array<std::uint32_t, 3>{0x3F7D70A3u, 0x3EFD70A3u, 0x3E4AC083u}));
    EXPECT_EQ(bits(Client::World::StreetLightGlowLight(13)),
              (std::array<std::uint32_t, 3>{0x3F547AE1u, 0x3ED47AE1u, 0x3E29FBE7u}));
}
} // namespace
