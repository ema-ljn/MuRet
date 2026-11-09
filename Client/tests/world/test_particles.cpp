#include "World/Particles.h"

#include "Render/Models/Cloth.h"
#include "Render/Models/OriginalRotation.h"
#include "World/ParticleDrawing.h"

#include "support/ParticleDrawRecorder.h"
#include "support/WorldObjectVectors.h"

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <functional>
#include <ios>
#include <limits>
#include <set>
#include <sstream>
#include <string>

// Client::World::Particles con el aliento del caballo (0x5f2, subtipo 0) y la chispa del cuervo (0x5c6,
// subtipo 5) contra el código real del main.exe: 0x53fe70, 0x5446e0 y 0x548550(0) en 20 casos del aliento
// y 8 de la chispa, de 20 pasos, con las que nacen en pasos al azar (tests/vectors/world55/particles.txt,
// make_particle_vectors.py), bit a bit.
namespace
{
using Client::Render::Models::CrtRandom;
using Client::World::Particle;
using Client::World::Particles;

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

// Los 0x78 bytes de un lugar de 0x743a198 como 30 palabras.
std::array<std::uint32_t, 30> WordsOf(const Particle& particle)
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
    words[24] = Bits(particle.rotationFactor);
    put(27, particle.start);
    return words;
}

TEST(ParticlesTest, TheBreathAndTheSparkMatchTheBinary)
{
    std::ifstream file(Client::Tests::World55VectorsDir() / "particles.txt");
    ASSERT_TRUE(file);
    std::string line;
    ASSERT_TRUE(std::getline(file, line));
    ASSERT_EQ(line, "particles 2");
    ASSERT_TRUE(std::getline(file, line));
    std::size_t cases = 0;
    std::size_t creations = 0;
    std::size_t draws = 0;
    Particles particles;
    CrtRandom random;
    glm::vec2 textureSize{};
    int components = 0;
    std::int32_t particleType = 0;
    std::int32_t subtype = 0;
    float scale = 0.0f;
    std::size_t sparks = 0;
    std::set<std::size_t> listed;
    std::size_t step = 0;
    const auto checkUnlisted = [&]()
    {
        for (std::size_t k = 0; k < Client::World::ParticleCount; ++k)
        {
            if (!listed.contains(k))
            {
                ASSERT_EQ(WordsOf(particles.All()[k]), WordsOf(Particle{})) << "caso " << cases << ": el lugar " << k;
            }
        }
    };
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
            particles = Particles{};
            std::string seed;
            words >> seed;
            random = CrtRandom(Hex(seed));
            particles.Wind().wind = {next(), next()};
            particles.Wind().change = {next(), next()};
            textureSize = {next(), next()};
            std::string type;
            words >> components >> type >> subtype;
            particleType = static_cast<std::int32_t>(Hex(type));
            scale = next();
        }
        else if (kind == "create")
        {
            const float clock = next();
            const glm::vec3 position{next(), next(), next()};
            const glm::vec3 angles{next(), next(), next()};
            const glm::vec3 light{next(), next(), next()};
            ASSERT_TRUE(
                particles.Create(particleType, position, angles, light, subtype, scale, clock, random).has_value())
                << where;
            sparks += particleType == Client::World::SparkParticle ? 1u : 0u;
            ++creations;
        }
        else if (kind == "step")
        {
            ++step;
            listed.clear();
            Client::World::StepParticles(particles, random);
            EXPECT_EQ(Bits(particles.Wind().wind.x), Bits(next())) << where << ": el viento x";
            EXPECT_EQ(Bits(particles.Wind().wind.y), Bits(next())) << where << ": el viento y";
            EXPECT_EQ(Bits(particles.Wind().change.x), Bits(next())) << where << ": el cambio x";
            EXPECT_EQ(Bits(particles.Wind().change.y), Bits(next())) << where << ": el cambio y";
            std::string state;
            words >> state;
            EXPECT_EQ(random.State(), Hex(state)) << where << ": el rand()";
        }
        else if (kind == "slot")
        {
            std::size_t slot = 0;
            words >> slot;
            listed.insert(slot);
            const std::array<std::uint32_t, 30> got = WordsOf(particles.All()[slot]);
            for (std::size_t k = 0; k < got.size(); ++k)
            {
                std::string value;
                words >> value;
                EXPECT_EQ(got[k], Hex(value)) << where << ": el lugar " << slot << ", +0x" << std::hex << (k * 4);
            }
        }
        else if (kind == "draw")
        {
            checkUnlisted();
            std::size_t calls = 0;
            words >> calls;
            // 0x548550(0) sobre el pool entero: la textura de cada lugar es su tipo (0x53fe70 la copia).
            Client::Tests::ParticleDrawRecorder recorder;
            recorder.dimensions.insert_or_assign(particleType, textureSize);
            recorder.materialFlags.insert_or_assign(particleType, static_cast<std::uint8_t>(components));
            const float clock = 0.0f;
            ASSERT_TRUE(Client::World::DrawParticlesChecked(particles, 0, std::cref(clock), recorder).HasValue())
                << where;
            std::size_t read = 0;
            for (const Client::Tests::RecordedParticleDraw& drawn : recorder.draws)
            {
                ASSERT_NE(drawn.blend, Client::Tests::RecordedParticleDraw::Blend::Subtractive) << where;
                ASSERT_TRUE(std::getline(file, line));
                EXPECT_EQ(line, drawn.blend == Client::Tests::RecordedParticleDraw::Blend::Additive ? "blend additive"
                                                                                                    : "blend alpha")
                    << where;
                ASSERT_TRUE(std::getline(file, line));
                std::istringstream call(line);
                std::string label;
                std::string texture;
                call >> label >> texture;
                ASSERT_EQ(label, "billboard") << where;
                EXPECT_EQ(static_cast<std::uint32_t>(drawn.texture), Hex(texture)) << where;
                const auto value = [&call]()
                {
                    std::string bits;
                    call >> bits;
                    return Hex(bits);
                };
                for (int k = 0; k < 3; ++k)
                {
                    EXPECT_EQ(Bits(drawn.position[k]), value()) << where << ": la posición " << k;
                }
                EXPECT_EQ(Bits(drawn.width), value()) << where << ": el ancho";
                EXPECT_EQ(Bits(drawn.height), value()) << where << ": el alto";
                for (int k = 0; k < 3; ++k)
                {
                    EXPECT_EQ(Bits(drawn.light[k]), value()) << where << ": la luz " << k;
                }
                EXPECT_EQ(Bits(drawn.rotation), value()) << where << ": la rotación";
                EXPECT_EQ(Bits(drawn.uv.x), value()) << where;
                EXPECT_EQ(Bits(drawn.uv.y), value()) << where;
                EXPECT_EQ(Bits(drawn.uvSize.x), value()) << where;
                EXPECT_EQ(Bits(drawn.uvSize.y), value()) << where;
                read += 2;
                ++draws;
            }
            EXPECT_EQ(read, calls) << where << ": las llamadas del dibujo";
        }
        if (HasFailure())
        {
            break;
        }
    }
    EXPECT_EQ(cases, 28u);
    EXPECT_GT(creations, 100u);
    EXPECT_GT(sparks, 40u);
    EXPECT_GT(draws, 500u);
}

// 0x53fe70 toma el primer lugar sin el byte vivo, de los 3.000; con todos llenos no crea ni saca rand().
TEST(ParticlesTest, AFullTableCreatesNothingAndTakesNoRand)
{
    Particles particles;
    CrtRandom random(5);
    for (std::size_t k = 0; k < Client::World::ParticleCount; ++k)
    {
        ASSERT_EQ(particles.Create(Client::World::BreathParticle, glm::vec3(0.0f), glm::vec3(0.0f), glm::vec3(3.0f), 0,
                                   1.0f, 0.0f, random),
                  k);
    }
    const std::uint32_t before = random.State();
    EXPECT_FALSE(particles
                     .Create(Client::World::BreathParticle, glm::vec3(0.0f), glm::vec3(0.0f), glm::vec3(3.0f), 0, 1.0f,
                             0.0f, random)
                     .has_value());
    EXPECT_EQ(random.State(), before);
    particles.Clear();
    EXPECT_FALSE(particles.Any());
    EXPECT_EQ(particles.Create(Client::World::BreathParticle, glm::vec3(0.0f), glm::vec3(0.0f), glm::vec3(3.0f), 0,
                               1.0f, 0.0f, random),
              0u);
}

// Los subtipos vecinos a los tres que arma MuRet no se crean ni sacan rand().
TEST(ParticlesTest, UnsupportedTypesAndSubtypesAreNotCreated)
{
    Particles particles;
    CrtRandom random(5);
    EXPECT_FALSE(
        particles.Create(0x5f3, glm::vec3(0.0f), glm::vec3(0.0f), glm::vec3(3.0f), 0, 1.0f, 0.0f, random).has_value());
    EXPECT_FALSE(particles
                     .Create(Client::World::BreathParticle, glm::vec3(0.0f), glm::vec3(0.0f), glm::vec3(3.0f), 1, 1.0f,
                             0.0f, random)
                     .has_value());
    EXPECT_FALSE(particles
                     .Create(Client::World::SparkParticle, glm::vec3(0.0f), glm::vec3(0.0f), glm::vec3(3.0f), 4, 1.0f,
                             0.0f, random)
                     .has_value());
    EXPECT_FALSE(particles
                     .Create(Client::World::CloudParticle, glm::vec3(0.0f), glm::vec3(0.0f), glm::vec3(3.0f), 9, 1.0f,
                             0.0f, random)
                     .has_value());
    EXPECT_EQ(random.State(), CrtRandom(5).State());
}

// _ftol del reloj (0x69fd3c): con un NaN la x87 guarda 0x8000000000000000 y la rotación queda en 0.
TEST(ParticlesTest, ANotANumberClockTurnsNothing)
{
    Particles particles;
    CrtRandom random(5);
    const auto index = particles.Create(Client::World::BreathParticle, glm::vec3(0.0f), glm::vec3(0.0f),
                                        glm::vec3(3.0f), 0, 1.0f, std::numeric_limits<float>::quiet_NaN(), random);
    ASSERT_TRUE(index.has_value());
    EXPECT_EQ(particles.All()[*index].rotation, 0.0f);
}

// 0x6070a0, al entrar, solo apaga el byte vivo de cada lugar (0x607164..0x607174): el viento (0x7609bb8 y
// 0x7609bc4, que solo escribe 0x5446e0) sigue como estaba.
TEST(ParticlesTest, ClearingLeavesTheWind)
{
    Particles particles;
    particles.Wind().wind = {0.5f, -0.3f};
    particles.Wind().change = {0.1f, 0.2f};
    particles.Clear();
    EXPECT_EQ(particles.Wind().wind, glm::vec2(0.5f, -0.3f));
    EXPECT_EQ(particles.Wind().change, glm::vec2(0.1f, 0.2f));
}

// Con +0x4c, el paso suma a la posición la velocidad girada por los ángulos (0x4e98d0: AngleMatrix y
// VectorRotate de la x87, cada suma en 53 bits) antes del caso del tipo. El aliento nace quieto; acá se
// le da una velocidad.
TEST(ParticlesTest, AMovingParticleTurnsItsVelocityByItsAngles)
{
    Particles particles;
    CrtRandom random(9);
    const auto index = particles.Create(Client::World::BreathParticle, {1000.0f, 2000.0f, 300.0f}, {0.0f, 0.0f, 0.0f},
                                        glm::vec3(3.0f), 0, 1.0f, 0.0f, random);
    ASSERT_TRUE(index.has_value());
    Particle& particle = particles.All()[*index];
    particle.velocity = {3.0f, -2.0f, 0.5f};
    particle.angles = {10.0f, 20.0f, 30.0f};
    const glm::vec3 turned = Client::Render::Models::OriginalVectorRotate(
        particle.velocity, Client::Render::Models::OriginalAngleMatrix(particle.angles));
    Client::World::StepParticles(particles, random);
    EXPECT_EQ(Bits(particle.position.x), Bits(static_cast<float>(static_cast<double>(turned.x) + 1000.0)));
    EXPECT_EQ(Bits(particle.position.y), Bits(static_cast<float>(static_cast<double>(turned.y) + 2000.0)));
    const auto moved = static_cast<float>(static_cast<double>(turned.z) + 300.0);
    EXPECT_EQ(Bits(particle.position.z),
              Bits(static_cast<float>((0.0 + static_cast<double>(0.2f)) + static_cast<double>(moved))));
    particle.moves = false;
    const glm::vec3 still = particle.position;
    Client::World::StepParticles(particles, random);
    EXPECT_EQ(particle.position.x, still.x) << "sin +0x4c no se mueve";
}
} // namespace
