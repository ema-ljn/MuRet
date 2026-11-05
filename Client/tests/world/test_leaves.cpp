#include "World/Leaves.h"

#include "Data/Terrain/TerrainAttributes.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Data/Terrain/TerrainHeights.h"
#include "Render/Models/Cloth.h"
#include "World/TerrainHeight.h"

#include "support/WorldObjectVectors.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

// Client::World::Leaves con las brasas del login y Client::World::TerrainHeightAt contra el código real del
// main.exe: 0x530490 (con 0x42b1e0 y 0x5303a0), 0x5306a0 y 0x5f93c0, en 3 casos de 150 pasos sobre un mapa
// de alturas de una fórmula (tests/vectors/world55/leaves.txt, make_leaf_vectors.py), bit a bit.
namespace
{
using Client::Render::Models::CrtRandom;
using Client::World::Leaf;
using Client::World::Leaves;

std::uint32_t Bits(float value)
{
    return std::bit_cast<std::uint32_t>(value);
}

std::uint32_t Hex(const std::string& word)
{
    return static_cast<std::uint32_t>(std::stoul(word, nullptr, 16));
}

float FromBits(const std::string& word)
{
    return std::bit_cast<float>(Hex(word));
}

// Los 0x78 bytes de un lugar de 0x75e34b8 como 30 palabras.
std::array<std::uint32_t, 30> WordsOf(const Leaf& leaf)
{
    std::array<std::uint32_t, 30> words{};
    const auto put = [&words](std::size_t at, const glm::vec3& value)
    {
        for (int k = 0; k < 3; ++k)
        {
            words[at + static_cast<std::size_t>(k)] = Bits(value[k]);
        }
    };
    words[0] = leaf.alive ? 1u : 0u;
    words[1] = static_cast<std::uint32_t>(leaf.type);
    words[4] = Bits(leaf.scale);
    put(5, leaf.position);
    put(8, leaf.angles);
    put(11, leaf.light);
    put(21, leaf.velocity);
    put(24, leaf.spin);
    put(27, leaf.start);
    return words;
}

std::uint32_t Crc32(const std::vector<std::uint8_t>& bytes)
{
    std::uint32_t crc = 0xffffffffu;
    for (const std::uint8_t byte : bytes)
    {
        crc ^= byte;
        for (int k = 0; k < 8; ++k)
        {
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

std::uint32_t TableCrc(const Leaves& leaves)
{
    std::vector<std::uint8_t> bytes;
    for (std::size_t k = 0; k < Client::World::LoginEmberCount; ++k)
    {
        for (const std::uint32_t word : WordsOf(leaves.All()[k]))
        {
            for (int b = 0; b < 4; ++b)
            {
                bytes.push_back(static_cast<std::uint8_t>(word >> (8 * b)));
            }
        }
    }
    return Crc32(bytes);
}

// El mapa de alturas y los atributos de make_leaf_vectors.py: la altura de la fila i y la columna j es
// 100 + ((i·a + j·b + (i·j) % 97) % 512) · 0,5, y el atributo lleva 0x40 donde (i·c + j·d) % 53 es 0.
struct Terrain
{
    std::unique_ptr<Client::Data::Terrain::TerrainHeights> heights =
        std::make_unique<Client::Data::Terrain::TerrainHeights>();
    std::unique_ptr<Client::Data::Terrain::TerrainAttributes> attributes =
        std::make_unique<Client::Data::Terrain::TerrainAttributes>();

    void Build(int a, int b, int c, int d)
    {
        constexpr int Side = 256;
        for (int i = 0; i < Side; ++i)
        {
            for (int j = 0; j < Side; ++j)
            {
                const auto index = static_cast<std::size_t>((i * Side) + j);
                heights->cells[index] = static_cast<float>(100.0 + (((i * a) + (j * b) + ((i * j) % 97)) % 512) * 0.5);
                attributes->cells[index] = ((i * c) + (j * d)) % 53 == 0 ? 0x40 : 0;
            }
        }
    }
};

TEST(LeavesTest, ThreeCasesMatchTheBinary)
{
    std::ifstream file(Client::Tests::World55VectorsDir() / "leaves.txt");
    ASSERT_TRUE(file);
    std::string line;
    ASSERT_TRUE(std::getline(file, line));
    ASSERT_EQ(line, "leaves 1");
    ASSERT_TRUE(std::getline(file, line));
    Terrain terrain;
    Leaves leaves;
    CrtRandom random;
    Client::World::EmberWorld world;
    std::size_t cases = 0;
    std::size_t step = 0;
    std::size_t heights = 0;
    std::size_t quads = 0;
    while (std::getline(file, line) && line != "end")
    {
        std::istringstream words(line);
        std::string kind;
        words >> kind;
        const auto next = [&words]()
        {
            std::string value;
            words >> value;
            return FromBits(value);
        };
        const std::string where = "caso " + std::to_string(cases) + ", paso " + std::to_string(step);
        if (kind == "case")
        {
            ++cases;
            step = 0;
            leaves = Leaves{};
            std::string seed;
            words >> seed;
            random = CrtRandom(Hex(seed));
            world.hero = {next(), next(), next()};
            world.cameraY = next();
            int a = 0;
            int b = 0;
            int c = 0;
            int d = 0;
            words >> a >> b >> c >> d;
            terrain.Build(a, b, c, d);
            world.heights = terrain.heights.get();
            world.attributes = terrain.attributes.get();
        }
        else if (kind == "height")
        {
            const float x = next();
            const float y = next();
            std::string expected;
            words >> expected;
            const double got = Client::World::TerrainHeightAt(*terrain.heights, *terrain.attributes, x, y);
            EXPECT_EQ(std::bit_cast<std::uint64_t>(got), std::stoull(expected, nullptr, 16))
                << where << ": la altura en " << x << ", " << y << " (" << got << ")";
            ++heights;
        }
        else if (kind == "step")
        {
            ++step;
            Client::World::StepLoginEmbers(leaves, world, random);
            std::string state;
            std::string crc;
            words >> state >> crc;
            EXPECT_EQ(random.State(), Hex(state)) << where << ": el rand()";
            EXPECT_EQ(TableCrc(leaves), Hex(crc)) << where << ": el CRC de la tabla";
        }
        else if (kind == "slot")
        {
            std::size_t slot = 0;
            words >> slot;
            const std::array<std::uint32_t, 30> got = WordsOf(leaves.All()[slot]);
            for (std::size_t k = 0; k < got.size(); ++k)
            {
                std::string value;
                words >> value;
                EXPECT_EQ(got[k], Hex(value)) << where << ": el lugar " << slot << ", palabra " << k;
            }
        }
        else if (kind == "draw")
        {
            const auto expectLine = [&file, &line, &where](const std::string& start)
            {
                EXPECT_TRUE(std::getline(file, line));
                EXPECT_EQ(line.substr(0, start.size()), start) << where;
            };
            expectLine("blend additive");
            expectLine("glColor3f 3f800000 3f800000 3f800000");
            for (const Leaf& leaf : leaves.All())
            {
                if (!leaf.alive)
                {
                    continue;
                }
                const Client::World::LeafQuad quad = Client::World::LeafQuadOf(leaf);
                expectLine("bind 635");
                ASSERT_TRUE(std::getline(file, line));
                std::istringstream translate(line);
                std::string name;
                translate >> name;
                EXPECT_EQ(name, "glTranslatef") << where;
                for (int k = 0; k < 3; ++k)
                {
                    std::string value;
                    translate >> value;
                    EXPECT_EQ(Bits(quad.translation[k]), Hex(value)) << where << ": la traslación " << k;
                }
                expectLine("glBegin 7");
                for (std::size_t corner = 0; corner < 4; ++corner)
                {
                    ASSERT_TRUE(std::getline(file, line));
                    std::istringstream uv(line);
                    uv >> name;
                    EXPECT_EQ(name, "glTexCoord2f") << where;
                    for (int k = 0; k < 2; ++k)
                    {
                        std::string value;
                        uv >> value;
                        EXPECT_EQ(Bits(quad.uv[corner][k]), Hex(value)) << where << ": la UV " << corner;
                    }
                    ASSERT_TRUE(std::getline(file, line));
                    std::istringstream vertex(line);
                    vertex >> name;
                    EXPECT_EQ(name, "glVertex3fv") << where;
                    for (int k = 0; k < 3; ++k)
                    {
                        std::string value;
                        vertex >> value;
                        EXPECT_EQ(Bits(quad.corners[corner][k]), Hex(value)) << where << ": la esquina " << corner;
                    }
                }
                expectLine("glEnd");
                ++quads;
            }
        }
        if (HasFailure())
        {
            break;
        }
    }
    EXPECT_EQ(cases, 3u);
    EXPECT_EQ(heights, 3u * (40u + 15u));
    EXPECT_GT(quads, 300u);
}

// 0x5f93c0 con x o y negativos: 0; fuera de la grilla, 1.200. La fila sale del producto de la y por 0,01
// en 53 bits: 25.600 · 0,01f es 255,99999 (en float, 256), la fila 255 y la fracción 1, que va hasta la
// fila 0 (la vuelta): la altura de (100, 0), 100,5. Con 25.601, la fila 256, fuera.
TEST(TerrainHeightTest, OutsideTheGridIsFixed)
{
    Terrain terrain;
    terrain.Build(1, 1, 1, 1);
    EXPECT_EQ(Client::World::TerrainHeightAt(*terrain.heights, *terrain.attributes, -1.0f, 100.0f), 0.0);
    EXPECT_EQ(Client::World::TerrainHeightAt(*terrain.heights, *terrain.attributes, 100.0f, -1.0f), 0.0);
    EXPECT_EQ(Client::World::TerrainHeightAt(*terrain.heights, *terrain.attributes, 100.0f, 25600.0f), 100.5);
    EXPECT_EQ(Client::World::TerrainHeightAt(*terrain.heights, *terrain.attributes, 100.0f, 25601.0f),
              static_cast<double>(Client::World::FixedTerrainHeight));
}

// 0x5f9424: la y se guarda con fst y ftol trunca lo que queda en la pila. 20.000 · 0,01f es 199,9999955:
// la fila 199 (en float sería la 200), y el 0x40 de su atributo da 1.200.
TEST(TerrainHeightTest, TheRowComesFromTheUnroundedProduct)
{
    Terrain terrain;
    terrain.Build(1, 1, 1, 1);
    for (std::uint16_t& attribute : terrain.attributes->cells)
    {
        attribute = 0;
    }
    terrain.attributes->cells[(199 * 256) + 50] = 0x40;
    EXPECT_EQ(Client::World::TerrainHeightAt(*terrain.heights, *terrain.attributes, 5050.0f, 20000.0f),
              static_cast<double>(Client::World::FixedTerrainHeight));
}

// 0x5f9436: la fila por 256 más la columna, en 32 bits y con signo. Con la fila 2^24 (y = 1.677.721.728)
// da la vuelta a la columna (la 50, con x 5.000), dentro de la grilla y en la fila 0; con la fila 2^24 − 1 (y
// = 1.677.721.600) queda negativo y el original lee antes de la tabla de atributos (0x5f9451): MuRet da 1.200.
TEST(TerrainHeightTest, TheCellIndexWrapsInThirtyTwoBits)
{
    Terrain terrain;
    terrain.Build(1, 1, 1, 1);
    for (std::uint16_t& attribute : terrain.attributes->cells)
    {
        attribute = 0;
    }
    EXPECT_EQ(Client::World::TerrainHeightAt(*terrain.heights, *terrain.attributes, 5000.0f, 1677721728.0f),
              static_cast<double>(terrain.heights->cells[50]));
    EXPECT_EQ(Client::World::TerrainHeightAt(*terrain.heights, *terrain.attributes, 5000.0f, 1677721600.0f),
              static_cast<double>(Client::World::FixedTerrainHeight));
}

// 0x5303e8: en el suelo, el rojo baja 0,05; con `test ah, 0x41` muere también cuando queda justo en 0. La
// brasa del lugar 0 está en el suelo (z 0, el terreno arriba de 100) con el rojo en 0,05.
TEST(LeavesTest, AnEmberOnTheGroundDiesWhenItsRedReachesZero)
{
    Terrain terrain;
    terrain.Build(3, 5, 7, 11);
    Leaves leaves;
    for (Leaf& leaf : leaves.All())
    {
        leaf.alive = true;
        leaf.position = {5050.0f, 5050.0f, 0.0f};
        leaf.light = {1.0f, 0.0f, 0.0f};
    }
    leaves.All()[0].light.x = 0.05f;
    const Client::World::EmberWorld world{.hero = glm::vec3(0.0f),
                                          .cameraY = 0.0f,
                                          .heights = terrain.heights.get(),
                                          .attributes = terrain.attributes.get()};
    CrtRandom random(1);
    Client::World::StepLoginEmbers(leaves, world, random);
    EXPECT_FALSE(leaves.All()[0].alive) << "el rojo en 0";
    EXPECT_TRUE(leaves.All()[1].alive) << "el rojo en 0,95";
    EXPECT_EQ(random.State(), CrtRandom(1).State()) << "en el suelo no saca rand()";
}
} // namespace
