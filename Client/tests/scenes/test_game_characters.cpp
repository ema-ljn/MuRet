#include "Scenes/GameCharacters.h"

#include "Data/Terrain/TerrainGrid.h"
#include "Scenes/ActorState.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

// Client::Scenes::DrawGameCharacters (0x510d00 en la escena 5; 06 § El dibujo del juego): el héroe que no se
// dibuja, el corte en el primer personaje vivo y visto (0x50ba30) y el TerrainWall de 0x42da60 al final.
namespace
{
using Client::Scenes::ActorState;

constexpr std::size_t Hero = 42;

// Un TerrainWall con el 0x04 en las cuatro celdas debajo de la puerta (119, 114) y en su fila.
std::unique_ptr<Client::Data::Terrain::TerrainLayer<std::uint16_t>> GateWalls()
{
    auto walls = std::make_unique<Client::Data::Terrain::TerrainLayer<std::uint16_t>>();
    walls->fill(0);
    for (std::uint8_t x = 118; x <= 121; ++x)
    {
        (*walls)[Client::Data::Terrain::CellIndex(x, 115)] = 0x05;
    }
    return walls;
}

// 0x510e75: sin un personaje vivo y visto, ninguno va a 0x50ba30; el héroe sin +0x1d8 & 4 tampoco se toca. Al
// final, 0x42da40 abre las puertas del TerrainWall.
TEST(GameCharactersTest, WithoutASeenCharacterThePassOpensTheGates)
{
    auto actors = std::make_unique<std::array<ActorState, 400>>();
    (*actors)[Hero].alive = true;
    (*actors)[7].seen = true;
    auto walls = GateWalls();

    const auto drawn = Client::Scenes::DrawGameCharacters(*actors, Hero, *walls);

    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    for (std::uint8_t x = 118; x <= 121; ++x)
    {
        EXPECT_EQ((*walls)[Client::Data::Terrain::CellIndex(x, 115)], 0x01) << static_cast<int>(x);
    }
    EXPECT_EQ((*actors)[Hero].field13c, 0.0f);
}

// 0x510e40..0x510e73: el héroe con +0x1d8 & 4 no se dibuja aunque esté vivo y visto; en la escena 5 le quedan
// +0x13c en 1.000 y +0x148, +0x158 y +0x168 en 1. Otro con ese bit no es el héroe y corta.
TEST(GameCharactersTest, TheHeroWithControlBitFourIsNotDrawn)
{
    auto actors = std::make_unique<std::array<ActorState, 400>>();
    ActorState& hero = (*actors)[Hero];
    hero.alive = true;
    hero.seen = true;
    hero.controlCode = Client::Scenes::UndrawnHeroControlBit;
    auto walls = GateWalls();

    ASSERT_TRUE(Client::Scenes::DrawGameCharacters(*actors, Hero, *walls).HasValue());
    EXPECT_EQ(hero.field13c, 1000.0f);
    EXPECT_EQ(hero.field148, 1.0f);
    EXPECT_EQ(hero.field158, 1.0f);
    EXPECT_EQ(hero.field168, 1.0f);

    const auto other = Client::Scenes::DrawGameCharacters(*actors, Hero + 1, *walls);
    ASSERT_FALSE(other.HasValue());
    EXPECT_NE(other.Error().find("0x510ea1"), std::string::npos) << other.Error();
}

// 0x510ea1: el primer personaje vivo y visto, por lugar, va a 0x50ba30, que MuRet no tiene en el juego: corta
// con su lugar, sin abrir las puertas (0x42da60 va después del lazo).
TEST(GameCharactersTest, TheFirstSeenCharacterStopsThePass)
{
    auto actors = std::make_unique<std::array<ActorState, 400>>();
    (*actors)[Hero].alive = true;
    (*actors)[Hero].seen = true;
    (*actors)[300].alive = true;
    (*actors)[300].seen = true;
    auto walls = GateWalls();

    const auto drawn = Client::Scenes::DrawGameCharacters(*actors, Hero, *walls);

    ASSERT_FALSE(drawn.HasValue());
    EXPECT_NE(drawn.Error().find("0x510ea1"), std::string::npos) << drawn.Error();
    EXPECT_NE(drawn.Error().find("personaje 42 "), std::string::npos) << drawn.Error();
    EXPECT_EQ((*walls)[Client::Data::Terrain::CellIndex(119, 115)], 0x05);
}
} // namespace
