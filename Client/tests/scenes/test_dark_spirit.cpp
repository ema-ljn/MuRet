#include "Scenes/DarkSpirit.h"

#include "Data/Terrain/TerrainAttributes.h"
#include "Data/Terrain/TerrainHeights.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/HalfLifeMath.h"
#include "Render/Models/OriginalRotation.h"
#include "Render/Models/Skeleton.h"
#include "World/Particles.h"
#include "World/TerrainCulling.h"
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
#include <utility>
#include <vector>

// Client::Scenes::DarkSpirit, el cuervo, contra el código real del main.exe: el constructor 0x4167b0 y el
// paso 0x416a20 con la chispa de 0x53fe70 (el 0x5c6, subtipo 5) y el grito, en 8 casos (las celdas (0, 0)
// y (13, 27), naciendo en la celda o junto al dueño, el dueño con 0x4000 y con 0x1000, el contador del
// grito múltiplo de 60, el rumbo justo en 180 y 300 por debajo del dueño), bit a bit (tests/vectors/world55/raven.txt,
// make_raven_vectors.py).
namespace
{
using Client::Render::Models::CrtRandom;
using Client::Scenes::DarkSpirit;

constexpr std::size_t Bones = 77;
const glm::vec3 OwnerPosition{10603.0f, 14112.0f, 580.0f};

std::uint32_t Bits(float value)
{
    return std::bit_cast<std::uint32_t>(value);
}

std::uint32_t Hex(const std::string& word)
{
    return static_cast<std::uint32_t>(std::stoul(word, nullptr, 16));
}

// Las acciones de DarkSpirit.bmd: 6, 4, 4 (con lock_positions) y 4 keys, todas repitiendo.
Client::Render::Models::Skeleton RavenSkeleton()
{
    struct Action
    {
        std::int16_t keys = 0;
        bool lock = false;
    };
    Client::Render::Models::Skeleton skeleton;
    for (const Action& action : std::array<Action, 4>{{{6, false}, {4, false}, {4, true}, {4, false}}})
    {
        skeleton.actions.push_back(Client::Render::Models::ActionInfo{
            .repeats = true, .speed = 0.0f, .keyCount = action.keys, .lockPositions = action.lock});
    }
    return skeleton;
}

// La pose de mentira del último dibujo de make_raven_vectors.py: sin giro, el hueso i en
// (1.000 + 7i; 2.000 − 3i; 300 + 5i).
std::vector<Client::Render::Models::OriginalMatrix> FakePose()
{
    std::vector<Client::Render::Models::OriginalMatrix> pose(Bones);
    for (std::size_t i = 0; i < Bones; ++i)
    {
        const auto k = static_cast<float>(i);
        pose[i] = {1.0f, 0.0f, 0.0f, 1000.0f + (7.0f * k), 0.0f, 1.0f, 0.0f, 2000.0f - (3.0f * k),
                   0.0f, 0.0f, 1.0f, 300.0f + (5.0f * k)};
    }
    return pose;
}

// Los 0x78 bytes de un lugar de 0x743a198.
std::array<std::uint32_t, 30> WordsOf(const Client::World::Particle& particle)
{
    std::array<std::uint32_t, 30> words{};
    const auto put = [&words](std::size_t at, const glm::vec3& value)
    {
        for (int k = 0; k < 3; ++k)
        {
            words[at + static_cast<std::size_t>(k)] = Bits(value[k]);
        }
    };
    words[0] = particle.alive ? 1u : 0u;
    words[1] = static_cast<std::uint32_t>(particle.type);
    words[2] = static_cast<std::uint32_t>(particle.texture);
    words[3] = static_cast<std::uint32_t>(particle.subtype);
    words[4] = Bits(particle.scale);
    put(5, particle.position);
    put(8, particle.angles);
    put(11, particle.light);
    words[15] = static_cast<std::uint32_t>(particle.life);
    words[16] = particle.owner;
    words[17] = Bits(particle.rotation);
    words[18] = Bits(particle.field48);
    words[19] = particle.moves ? 1u : 0u;
    words[20] = Bits(particle.verticalSpeed);
    put(21, particle.velocity);
    put(27, particle.start);
    return words;
}

// El mapa de alturas de make_raven_vectors.py (el de make_leaf_vectors.py): 100 + ((i·a + j·b + (i·j)
// % 97) % 512) · 0,5, sin atributos.
struct Terrain
{
    std::unique_ptr<Client::Data::Terrain::TerrainHeights> heights =
        std::make_unique<Client::Data::Terrain::TerrainHeights>();
    std::unique_ptr<Client::Data::Terrain::TerrainAttributes> attributes =
        std::make_unique<Client::Data::Terrain::TerrainAttributes>();

    void Build(int a, int b)
    {
        constexpr int Side = 256;
        for (int i = 0; i < Side; ++i)
        {
            for (int j = 0; j < Side; ++j)
            {
                const auto index = static_cast<std::size_t>((i * Side) + j);
                heights->cells[index] = static_cast<float>(100.0 + (((i * a) + (j * b) + ((i * j) % 97)) % 512) * 0.5);
                attributes->cells[index] = 0;
            }
        }
    }
};

TEST(DarkSpiritTest, TheConstructorAndTheStepMatchTheBinary)
{
    std::ifstream file(Client::Tests::World55VectorsDir() / "raven.txt");
    ASSERT_TRUE(file);
    std::string line;
    ASSERT_TRUE(std::getline(file, line));
    ASSERT_EQ(line, "raven 1");
    const Client::Render::Models::Skeleton skeleton = RavenSkeleton();
    Terrain terrain;
    CrtRandom random;
    DarkSpirit spirit;
    Client::Scenes::DarkSpiritOwner owner;
    std::int32_t counter = 0;
    std::string start;
    auto particles = std::make_unique<Client::World::Particles>();
    std::size_t cases = 0;
    std::size_t steps = 0;
    std::size_t sparks = 0;
    std::size_t shouts = 0;
    std::size_t step = 0;
    bool sparked = false;
    while (std::getline(file, line) && line != "end")
    {
        std::istringstream words(line);
        std::string kind;
        words >> kind;
        const auto next = [&words]()
        {
            std::string value;
            words >> value;
            return std::bit_cast<float>(Hex(value));
        };
        const std::string where = "caso " + std::to_string(cases) + ", paso " + std::to_string(step);
        if (kind == "case")
        {
            ++cases;
            step = 0;
            std::string seed;
            std::string flags;
            std::string count;
            int cellX = 0;
            int cellY = 0;
            int a = 0;
            int b = 0;
            int total = 0;
            words >> seed >> cellX >> cellY >> start >> flags >> count >> a >> b >> total;
            terrain.Build(a, b);
            random = CrtRandom(Hex(seed));
            const double ground = Client::World::TerrainHeightAt(*terrain.heights, *terrain.attributes,
                                                                 static_cast<float>((cellX * 100) + 50),
                                                                 static_cast<float>((cellY * 100) + 50));
            spirit = Client::Scenes::CreateDarkSpirit(cellX, cellY, ground, random);
            owner = Client::Scenes::DarkSpiritOwner{.position = OwnerPosition, .flags = Hex(flags)};
            counter = static_cast<std::int32_t>(Hex(count));
        }
        else if (kind == "ctor")
        {
            std::string state;
            words >> state;
            EXPECT_EQ(random.State(), Hex(state)) << where << ": el rand() del constructor";
            for (int k = 0; k < 3; ++k)
            {
                EXPECT_EQ(Bits(spirit.position[k]), Bits(next())) << where << ": la posición " << k;
            }
            EXPECT_EQ(Bits(spirit.scale), Bits(next())) << where;
            for (int k = 0; k < 3; ++k)
            {
                EXPECT_EQ(Bits(spirit.light[k]), Bits(next())) << where << ": la luz";
            }
            EXPECT_EQ(Bits(spirit.speed), Bits(next())) << where;
            EXPECT_EQ(Bits(spirit.alpha), Bits(next())) << where;
            int state8 = 0;
            int action = 0;
            words >> state8 >> action;
            EXPECT_EQ(spirit.state, state8) << where;
            EXPECT_EQ(spirit.animation.action, action) << where;
            EXPECT_EQ(Bits(spirit.animation.frame), Bits(next())) << where;
            std::string far;
            words >> far;
            EXPECT_EQ(spirit.far, static_cast<std::int32_t>(Hex(far))) << where;
            if (start == "near")
            {
                spirit.position = glm::vec3(OwnerPosition.x, OwnerPosition.y,
                                            static_cast<float>(static_cast<double>(OwnerPosition.z) + 250.0));
            }
            if (start == "yaw180")
            {
                spirit.angles.z = 180.0f;
            }
            if (start == "low")
            {
                spirit.position = glm::vec3(OwnerPosition.x, OwnerPosition.y,
                                            static_cast<float>(static_cast<double>(OwnerPosition.z) - 300.0));
            }
            spirit.pose = FakePose();
        }
        else if (kind == "step")
        {
            if (sparked)
            {
                EXPECT_FALSE(particles->All()[0].alive) << where << ": nació una chispa que el original no";
            }
            ++step;
            ++steps;
            particles->Clear();
            const bool shout =
                Client::Scenes::StepDarkSpirit(spirit, skeleton, owner, 0x37, *particles, 0.0f, random, counter).shouts;
            sparked = true;
            std::string state;
            words >> state;
            EXPECT_EQ(random.State(), Hex(state)) << where << ": el rand()";
            for (int k = 0; k < 3; ++k)
            {
                EXPECT_EQ(Bits(spirit.position[k]), Bits(next())) << where << ": la posición " << k;
            }
            for (int k = 0; k < 3; ++k)
            {
                EXPECT_EQ(Bits(spirit.angles[k]), Bits(next())) << where << ": los ángulos " << k;
            }
            for (int k = 0; k < 3; ++k)
            {
                EXPECT_EQ(Bits(spirit.direction[k]), Bits(next())) << where << ": la dirección " << k;
            }
            EXPECT_EQ(Bits(spirit.speed), Bits(next())) << where << ": la velocidad";
            int state8 = 0;
            int action = 0;
            words >> state8 >> action;
            EXPECT_EQ(spirit.state, state8) << where << ": el estado";
            EXPECT_EQ(spirit.animation.action, action) << where << ": la acción";
            EXPECT_EQ(Bits(spirit.animation.frame), Bits(next())) << where << ": el frame";
            int far = 0;
            std::string sound;
            words >> far >> sound;
            EXPECT_EQ(spirit.far, far) << where << ": el contador";
            EXPECT_EQ(shout, sound == "1d1") << where << ": el grito";
            shouts += shout ? 1u : 0u;
        }
        else if (kind == "spark")
        {
            sparked = false;
            const std::array<std::uint32_t, 30> got = WordsOf(particles->All()[0]);
            for (std::size_t k = 0; k < got.size(); ++k)
            {
                std::string value;
                words >> value;
                EXPECT_EQ(got[k], Hex(value)) << where << ": la chispa, palabra " << k;
            }
            ++sparks;
        }
        if (HasFailure())
        {
            break;
        }
    }
    EXPECT_EQ(cases, 8u);
    EXPECT_EQ(steps, 1740u);
    EXPECT_EQ(sparks, 1600u);
    EXPECT_GE(shouts, 1u);
}

// Los huesos propios del dueño de make_raven_safe_zone_vectors.py: el giro de 0,6 y 0,8 sobre z y el hueso i en
// (−40 + 3i; 25 − 2i; 90 + 4i).
std::vector<Client::Render::Models::BoneTransform> OwnerPose()
{
    constexpr std::size_t OwnerBones = 60;
    std::vector<Client::Render::Models::BoneTransform> pose(OwnerBones);
    for (std::size_t i = 0; i < OwnerBones; ++i)
    {
        const auto k = static_cast<float>(i);
        const std::array<float, 12> rows{0.6f, -0.8f, 0.0f, -40.0f + (3.0f * k), 0.8f, 0.6f, 0.0f, 25.0f - (2.0f * k),
                                         0.0f, 0.0f,  1.0f, 90.0f + (4.0f * k)};
        for (int row = 0; row < 3; ++row)
        {
            for (int column = 0; column < 4; ++column)
            {
                pose[i][column][row] = rows[static_cast<std::size_t>((row * 4) + column)];
            }
        }
    }
    return pose;
}

// Los tramos de la zona segura de un caso: "desde:valor".
std::uint8_t ZoneAt(const std::vector<std::pair<std::size_t, int>>& zones, std::size_t step)
{
    int zone = 0;
    for (const auto& [from, value] : zones)
    {
        if (step >= from)
        {
            zone = value;
        }
    }
    return static_cast<std::uint8_t>(zone);
}

// 0x416a20 en la zona segura (Q-C412 a Q-C414), contra el código real emulado (raven_safe_zone.txt, de
// make_raven_safe_zone_vectors.py): entra en la zona desde lejos y baja (el estado 4) acelerando hasta que el
// contador de lejanía lo trae, se posa (el 3) en el hueso 37 del dueño con la escala y la posición del modelo
// compartido, sale de la zona (el 0) y vuelve; se posa enseguida desde junto al dueño; con la mascota en 0x1a04
// en el mapa 0 vuela a 350; y +0x3e2 en 2 no es la zona segura. Bit a bit, paso por paso.
TEST(DarkSpiritTest, TheSafeZoneStepMatchesTheBinary)
{
    std::ifstream file(Client::Tests::World55VectorsDir() / "raven_safe_zone.txt");
    ASSERT_TRUE(file);
    std::string line;
    ASSERT_TRUE(std::getline(file, line));
    ASSERT_EQ(line, "raven-safe-zone 1");
    const Client::Render::Models::Skeleton skeleton = RavenSkeleton();
    const std::vector<Client::Render::Models::BoneTransform> ownerPose = OwnerPose();
    Terrain terrain;
    terrain.Build(3, 5);
    CrtRandom random;
    DarkSpirit spirit;
    Client::Scenes::DarkSpiritOwner owner;
    std::uint8_t map = 0;
    std::string start;
    std::vector<std::pair<std::size_t, int>> zones;
    auto particles = std::make_unique<Client::World::Particles>();
    std::size_t cases = 0;
    std::size_t steps = 0;
    std::size_t step = 0;
    std::array<std::size_t, 5> seenStates{};
    while (std::getline(file, line) && line != "end")
    {
        std::istringstream words(line);
        std::string kind;
        words >> kind;
        const auto next = [&words]()
        {
            std::string value;
            words >> value;
            return std::bit_cast<float>(Hex(value));
        };
        const std::string where = "caso " + std::to_string(cases) + ", paso " + std::to_string(step);
        if (kind == "case")
        {
            ++cases;
            step = 0;
            std::string seed;
            std::string mapWord;
            std::string pet;
            int cellX = 0;
            int cellY = 0;
            words >> seed >> mapWord >> cellX >> cellY >> start >> pet;
            const float scale = next();
            glm::vec3 origin{};
            for (int k = 0; k < 3; ++k)
            {
                origin[k] = next();
            }
            int total = 0;
            words >> total;
            zones.clear();
            std::string zone;
            while (words >> zone)
            {
                const std::size_t colon = zone.find(':');
                zones.emplace_back(std::stoul(zone.substr(0, colon)), std::stoi(zone.substr(colon + 1)));
            }
            map = static_cast<std::uint8_t>(Hex(mapWord));
            random = CrtRandom(Hex(seed));
            const double ground = Client::World::TerrainHeightAt(*terrain.heights, *terrain.attributes,
                                                                 static_cast<float>((cellX * 100) + 50),
                                                                 static_cast<float>((cellY * 100) + 50));
            spirit = Client::Scenes::CreateDarkSpirit(cellX, cellY, ground, random);
            owner = Client::Scenes::DarkSpiritOwner{.position = OwnerPosition,
                                                    .flags = 0,
                                                    .angles = {3.0f, 0.0f, 188.0f},
                                                    .pet = static_cast<std::uint16_t>(Hex(pet)),
                                                    .safeZone = 0,
                                                    .pose = ownerPose,
                                                    .modelScale = scale,
                                                    .modelOrigin = origin};
        }
        else if (kind == "ctor")
        {
            std::string state;
            words >> state;
            EXPECT_EQ(random.State(), Hex(state)) << where << ": el rand() del constructor";
            for (int k = 0; k < 3; ++k)
            {
                EXPECT_EQ(Bits(spirit.position[k]), Bits(next())) << where << ": la posición " << k;
            }
            for (int k = 0; k < 3; ++k)
            {
                EXPECT_EQ(Bits(spirit.angles[k]), Bits(next())) << where << ": los ángulos " << k;
            }
            EXPECT_EQ(Bits(spirit.speed), Bits(next())) << where;
            if (start == "near")
            {
                spirit.position = glm::vec3(OwnerPosition.x, OwnerPosition.y,
                                            static_cast<float>(static_cast<double>(OwnerPosition.z) + 250.0));
            }
            spirit.pose = FakePose();
        }
        else if (kind == "step")
        {
            owner.safeZone = ZoneAt(zones, step);
            ++step;
            ++steps;
            particles->Clear();
            const bool shout =
                Client::Scenes::StepDarkSpirit(spirit, skeleton, owner, map, *particles, 0.0f, random, 0x12345677)
                    .shouts;
            std::string state;
            words >> state;
            EXPECT_EQ(random.State(), Hex(state)) << where << ": el rand()";
            for (int k = 0; k < 3; ++k)
            {
                EXPECT_EQ(Bits(spirit.position[k]), Bits(next())) << where << ": la posición " << k;
            }
            for (int k = 0; k < 3; ++k)
            {
                EXPECT_EQ(Bits(spirit.angles[k]), Bits(next())) << where << ": los ángulos " << k;
            }
            for (int k = 0; k < 3; ++k)
            {
                EXPECT_EQ(Bits(spirit.direction[k]), Bits(next())) << where << ": la dirección " << k;
            }
            EXPECT_EQ(Bits(spirit.speed), Bits(next())) << where << ": la velocidad";
            int state8 = 0;
            int action = 0;
            words >> state8 >> action;
            EXPECT_EQ(spirit.state, state8) << where << ": el estado";
            EXPECT_EQ(spirit.animation.action, action) << where << ": la acción";
            EXPECT_EQ(Bits(spirit.animation.frame), Bits(next())) << where << ": el frame";
            int far = 0;
            std::string sound;
            words >> far >> sound;
            EXPECT_EQ(spirit.far, far) << where << ": el contador";
            EXPECT_EQ(shout, sound == "1d1") << where << ": el grito";
            if (spirit.state < seenStates.size())
            {
                ++seenStates[spirit.state];
            }
        }
        if (HasFailure())
        {
            break;
        }
    }
    EXPECT_EQ(cases, 4u);
    EXPECT_EQ(steps, 610u);
    EXPECT_GT(seenStates[3], 100u) << "posado";
    EXPECT_GT(seenStates[4], 100u) << "bajando";
}

// 0x415be0 guarda la altura del terreno en float (0x415ea7) y 0x4169ec le suma 300 desde ahí: f(f(h) +
// 300), no f(h + 300). Las celdas van con `and 0xff` (0x415c25): la 259 es la 3.
TEST(DarkSpiritTest, TheBirthRoundsTheGroundFirstAndWrapsTheCells)
{
    CrtRandom random(1);
    const DarkSpirit spirit = Client::Scenes::CreateDarkSpirit(259, 1, 450.00003069877624, random);
    EXPECT_EQ(spirit.position.x, 350.0f);
    EXPECT_EQ(spirit.position.y, 150.0f);
    EXPECT_EQ(spirit.position.z, 750.0f) << "f(f(h) + 300); con f(h + 300) daría 750,00006";
}

// 0x418365: el cuervo que no pasa el trapecio va a la velocidad 3 y, salvo en el estado 4, al 0.
TEST(DarkSpiritTest, OutOfTheViewItSlowsDownAndForgetsItsState)
{
    DarkSpirit spirit;
    spirit.position = {50.0f, 50.0f, 750.0f};
    EXPECT_TRUE(Client::Scenes::DarkSpiritSeen(spirit, Client::World::TerrainPolygon{.everything = true}));
    const auto far =
        Client::World::TerrainPolygonOf(Client::World::CullingScene::Game, 0, {14350.0f, 12050.0f, 165.0f}, -45.0f);
    EXPECT_FALSE(Client::Scenes::DarkSpiritSeen(spirit, far));
    spirit.state = 1;
    Client::Scenes::MissTheView(spirit);
    EXPECT_EQ(spirit.speed, 3.0f);
    EXPECT_EQ(spirit.state, 0);
    spirit.state = 4;
    Client::Scenes::MissTheView(spirit);
    EXPECT_EQ(spirit.state, 4);
}

// 0x417791: con el estado más allá de la tabla de 0x417f0c, la acción al dar la vuelta es la 0.
TEST(DarkSpiritTest, AStateBeyondTheTableTakesActionZero)
{
    const Client::Render::Models::Skeleton skeleton = RavenSkeleton();
    auto particles = std::make_unique<Client::World::Particles>();
    DarkSpirit spirit;
    spirit.position = OwnerPosition + glm::vec3(0.0f, 0.0f, 350.0f);
    spirit.state = 6;
    spirit.animation.action = 1;
    spirit.animation.frame = 3.9f;
    CrtRandom random(5);
    (void)Client::Scenes::StepDarkSpirit(spirit, skeleton,
                                         Client::Scenes::DarkSpiritOwner{.position = OwnerPosition, .flags = 0}, 0x37,
                                         *particles, 0.0f, random, 1);
    EXPECT_EQ(spirit.animation.action, 0);
}

// Q-C131: en el mapa 0x37 ronda a 350 sobre el dueño; en los demás, a 250. La altura media sobre el
// dueño, desde junto a él y pasada la subida, con la misma semilla.
TEST(DarkSpiritTest, ItFliesHigherInTheLoginMap)
{
    const Client::Render::Models::Skeleton skeleton = RavenSkeleton();
    const auto meanHeight = [&skeleton](std::uint8_t map)
    {
        auto particles = std::make_unique<Client::World::Particles>();
        DarkSpirit spirit;
        spirit.position = OwnerPosition + glm::vec3(0.0f, 0.0f, 250.0f);
        CrtRandom random(77);
        double sum = 0.0;
        int count = 0;
        for (int step = 0; step < 3000; ++step)
        {
            particles->Clear();
            (void)Client::Scenes::StepDarkSpirit(spirit, skeleton,
                                                 Client::Scenes::DarkSpiritOwner{.position = OwnerPosition, .flags = 0},
                                                 map, *particles, 0.0f, random, 1);
            if (step >= 500)
            {
                sum += static_cast<double>(spirit.position.z - OwnerPosition.z);
                ++count;
            }
        }
        return sum / count;
    };
    const double login = meanHeight(0x37);
    const double other = meanHeight(0);
    EXPECT_GT(login, 350.0);
    EXPECT_LT(login, 500.0);
    EXPECT_GT(login - other, 75.0);
}
} // namespace
