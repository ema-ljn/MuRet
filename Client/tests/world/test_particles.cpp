#include "World/Particles.h"

#include "Render/Models/Cloth.h"
#include "Render/Models/OriginalRotation.h"
#include "World/ParticleDrawing.h"
#include "World/PortableParticleTrigonometry.h"
#include "World/TerrainLighting.h"
#include "World/WorldObjectStepServices.h"

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
#include <memory>
#include <optional>
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
        particles.Create(0x5f3, glm::vec3(0.0f), glm::vec3(0.0f), glm::vec3(3.0f), 1, 1.0f, 0.0f, random).has_value());
    EXPECT_FALSE(particles
                     .Create(Client::World::BreathParticle, glm::vec3(0.0f), glm::vec3(0.0f), glm::vec3(3.0f), 1, 1.0f,
                             0.0f, random)
                     .has_value());
    EXPECT_FALSE(particles
                     .Create(Client::World::SparkParticle, glm::vec3(0.0f), glm::vec3(0.0f), glm::vec3(3.0f), 4, 1.0f,
                             0.0f, random)
                     .has_value());
    EXPECT_FALSE(particles
                     .Create(Client::World::CloudParticle, glm::vec3(0.0f), glm::vec3(0.0f), glm::vec3(3.0f), 11, 1.0f,
                             0.0f, random)
                     .has_value());
    EXPECT_EQ(random.State(), CrtRandom(5).State());
}

// 0x54271f..0x542838, el polvo 5f3/0 del galope del caballo: vida 32, (rand() & 15) − 8 en x y en y,
// ((rand() & 31) + 32) · escala · 0,01f en la x87, el empuje (0; 3; 0) girado por los ángulos del pedido y el giro x en
// rand() % 360. Con la semilla 1: 41, 18.467, 6.334 y 26.500, cuatro rand().
TEST(ParticlesTest, TheGallopDustIsBornAroundItsPlaceWithFourRandoms)
{
    Particles particles;
    CrtRandom random(1);
    const auto index = particles.CreateChecked(Client::World::DustParticle, {1000.5f, 2000.25f, 300.0f},
                                               {0.0f, 0.0f, 0.0f}, glm::vec3(1.0f), 0, 1.5f, 0.0f, random);
    ASSERT_TRUE(index.HasValue());
    ASSERT_EQ(index.Value(), std::optional<std::size_t>{0});
    const Particle& dust = particles.All()[0];
    EXPECT_TRUE(dust.alive);
    EXPECT_EQ(dust.type, Client::World::DustParticle);
    EXPECT_EQ(dust.texture, Client::World::DustParticle);
    EXPECT_EQ(dust.life, 32);
    EXPECT_EQ(dust.position, (glm::vec3{1001.5f, 1995.25f, 300.0f}));
    EXPECT_EQ(Bits(dust.scale), 0x3f6e147bu);
    EXPECT_EQ(dust.velocity, (glm::vec3{0.0f, 3.0f, 0.0f}));
    EXPECT_EQ(dust.angles, (glm::vec3{220.0f, 0.0f, 0.0f}));
    EXPECT_EQ(dust.light, glm::vec3(1.0f));
    EXPECT_EQ(dust.rotation, 0.0f);
    CrtRandom expected(1);
    for (int draw = 0; draw < 4; ++draw)
        static_cast<void>(expected.Next());
    EXPECT_EQ(random.State(), expected.State());

    const glm::vec3 turn{0.0f, 0.0f, 45.0f};
    const auto turned = particles.CreateChecked(Client::World::DustParticle, glm::vec3(0.0f), turn, glm::vec3(1.0f), 0,
                                                1.0f, 0.0f, random);
    ASSERT_TRUE(turned.HasValue());
    ASSERT_EQ(turned.Value(), std::optional<std::size_t>{1});
    EXPECT_EQ(particles.All()[1].velocity, Client::Render::Models::OriginalVectorRotate(
                                               {0.0f, 3.0f, 0.0f}, Client::Render::Models::OriginalAngleMatrix(turn)));
}

// 0x546b66..0x546bfb después del movimiento común: la luz por la vida y 0,03125, la escala más 0,08f, la posición más
// la velocidad otra vez (Q-C402), el freno 0,9f y la z en el terreno (0x5f93c0) más el alto de la textura por la
// escala por 0,5. Con los ángulos en 0 el giro común es la identidad: la y sube 3 dos veces. El viento saca sus dos
// rand().
TEST(ParticlesTest, TheGallopDustMovesTwiceBrakesAndSitsOnTheGround)
{
    Particles particles;
    CrtRandom random(1);
    ASSERT_TRUE(particles
                    .CreateChecked(Client::World::DustParticle, {1000.5f, 2000.25f, 300.0f}, glm::vec3(0.0f),
                                   glm::vec3(1.0f), 0, 1.5f, 0.0f, random)
                    .HasValue());
    Particle& dust = particles.All()[0];
    dust.angles = glm::vec3(0.0f);
    Client::World::PortableParticleTrigonometry trigonometry;
    const float clock = 0.0f;
    const std::int32_t map = 0;
    glm::vec2 asked{};
    std::int32_t textureAsked = 0;
    const auto stepped = Client::World::StepParticlesChecked(
        particles, random, std::cref(clock), &map, trigonometry,
        [&asked](float x, float y)
        {
            asked = {x, y};
            return 250.0;
        },
        {}, {},
        [&textureAsked](std::int32_t type) -> std::optional<float>
        {
            textureAsked = type;
            return 64.0f;
        });

    ASSERT_TRUE(stepped.HasValue());
    EXPECT_TRUE(dust.alive);
    EXPECT_EQ(dust.life, 31);
    EXPECT_EQ(dust.light, glm::vec3(0.96875f));
    EXPECT_EQ(Bits(dust.scale), 0x3f8147aeu);
    EXPECT_EQ(dust.position.x, 1001.5f);
    EXPECT_EQ(dust.position.y, 2001.25f);
    EXPECT_EQ(Bits(dust.position.z), 0x438d28f6u);
    EXPECT_EQ(dust.velocity, (glm::vec3{0.0f, std::bit_cast<float>(0x402cccccu), 0.0f}));
    EXPECT_EQ(asked, (glm::vec2{1001.5f, 2001.25f}));
    EXPECT_EQ(textureAsked, Client::World::DustParticle);
    CrtRandom expected(1);
    for (int draw = 0; draw < 6; ++draw)
        static_cast<void>(expected.Next());
    EXPECT_EQ(random.State(), expected.State());
}

// Sin la altura del terreno o sin el alto de la textura, el paso de la 5f3 corta con la luz, la escala, la posición
// y el freno ya escritos.
TEST(ParticlesTest, TheGallopDustNeedsTheGroundAndItsTexture)
{
    for (const bool withGround : {false, true})
    {
        SCOPED_TRACE(withGround);
        Particles particles;
        CrtRandom random(1);
        ASSERT_TRUE(particles
                        .CreateChecked(Client::World::DustParticle, {1000.5f, 2000.25f, 300.0f}, glm::vec3(0.0f),
                                       glm::vec3(1.0f), 0, 1.5f, 0.0f, random)
                        .HasValue());
        Particle& dust = particles.All()[0];
        dust.angles = glm::vec3(0.0f);
        Client::World::PortableParticleTrigonometry trigonometry;
        const float clock = 0.0f;
        const std::int32_t map = 0;
        const Client::World::ParticleTerrainHeight ground =
            withGround ? Client::World::ParticleTerrainHeight([](float, float) { return 250.0; })
                       : Client::World::ParticleTerrainHeight{};
        const auto stepped =
            Client::World::StepParticlesChecked(particles, random, std::cref(clock), &map, trigonometry, ground, {}, {},
                                                [](std::int32_t) { return std::optional<float>{}; });

        ASSERT_FALSE(stepped.HasValue());
        EXPECT_EQ(stepped.Error(), withGround ? Client::World::ParticleError::MissingTextureSize
                                              : Client::World::ParticleError::MissingTerrainHeight);
        EXPECT_EQ(Bits(dust.scale), 0x3f8147aeu);
        EXPECT_EQ(dust.position, (glm::vec3{1001.5f, 2001.25f, 300.0f}));
        EXPECT_EQ(dust.velocity.y, std::bit_cast<float>(0x402cccccu));
    }
}

// 0x548a51: la 5f3 pide 0x620c10 (SRC_ALPHA / ONE_MINUS_SRC_ALPHA) y va al billboard común, con el giro de +0x44
// (en 0: el giro x de la creación no cuenta) y el tamaño de su imagen por la escala.
TEST(ParticlesTest, TheGallopDustDrawsBlendedOnTheCommonBillboard)
{
    Particle dust;
    dust.alive = true;
    dust.type = dust.texture = Client::World::DustParticle;
    dust.scale = 0.5f;
    dust.angles = {220.0f, 0.0f, 0.0f};
    dust.light = glm::vec3(0.75f);
    dust.position = {1.0f, 2.0f, 3.0f};

    const auto drawn = Client::Tests::DrawOneParticle(dust, glm::vec2(64.0f), std::uint8_t{4});

    ASSERT_TRUE(drawn.HasValue());
    ASSERT_TRUE(drawn.Value().has_value());
    EXPECT_EQ(drawn.Value()->blend, Client::Tests::RecordedParticleDraw::Blend::Blended);
    EXPECT_EQ(drawn.Value()->texture, Client::World::DustParticle);
    EXPECT_EQ(drawn.Value()->width, 32.0f);
    EXPECT_EQ(drawn.Value()->height, 32.0f);
    EXPECT_EQ(drawn.Value()->rotation, 0.0f);
    EXPECT_EQ(drawn.Value()->position, (glm::vec3{1.0f, 2.0f, 3.0f}));
    EXPECT_EQ(drawn.Value()->light, glm::vec3(0.75f));
}

// 0x542a60 con el subtipo 0: la explosión 5ed del terremoto nace con vida 20 y la escala del pedido, sin rand() (el
// sonido 0x41ed10(0x40, 0, 0) de 0x542a82 lo emite quien la crea). 0x5459a4 y 0x545c91..0x545cd6: después de la vida
// y del movimiento común, el cuadro (20 − vida) / 2 en +0x48 y la luz dinámica de radio 4 (f · 0,5f; f · 0,3f;
// f · 0,1f) con f = vida · 0,05f retenido en la x87. El viento saca sus dos rand().
TEST(ParticlesTest, TheEarthquakeExplosionCountsItsFramesAndLightsTheGround)
{
    Particles particles;
    CrtRandom random(1);
    const auto index = particles.CreateChecked(Client::World::ExplosionParticle, {1000.5f, 2000.25f, 300.0f},
                                               glm::vec3(0.0f), glm::vec3(1.0f), 0, 0.5f, 0.0f, random);
    ASSERT_TRUE(index.HasValue());
    ASSERT_EQ(index.Value(), std::optional<std::size_t>{0});
    Particle& explosion = particles.All()[0];
    EXPECT_TRUE(explosion.alive);
    EXPECT_EQ(explosion.type, Client::World::ExplosionParticle);
    EXPECT_EQ(explosion.texture, Client::World::ExplosionParticle);
    EXPECT_EQ(explosion.subtype, 0);
    EXPECT_EQ(explosion.life, 20);
    EXPECT_EQ(explosion.scale, 0.5f);
    EXPECT_EQ(explosion.position, (glm::vec3{1000.5f, 2000.25f, 300.0f}));
    EXPECT_EQ(explosion.velocity, glm::vec3(0.0f));
    EXPECT_EQ(explosion.light, glm::vec3(1.0f));
    EXPECT_EQ(random.State(), 1u);

    Client::World::PortableParticleTrigonometry trigonometry;
    const float clock = 0.0f;
    const std::int32_t map = 0;
    CrtRandom expectedRandom(1);
    for (const std::int32_t life : {19, 18, 17})
    {
        SCOPED_TRACE(life);
        auto lighting = std::make_unique<Client::World::TerrainLighting>();
        const auto stepped = Client::World::StepParticlesChecked(particles, random, std::cref(clock), &map,
                                                                 trigonometry, {}, {}, {}, {}, lighting.get());
        ASSERT_TRUE(stepped.HasValue());
        EXPECT_EQ(explosion.life, life);
        EXPECT_EQ(std::bit_cast<std::int32_t>(explosion.field48), (20 - life) / 2);
        EXPECT_EQ(explosion.position, (glm::vec3{1000.5f, 2000.25f, 300.0f}));
        const double glow = static_cast<double>(life) * static_cast<double>(std::bit_cast<float>(0x3d4ccccdu));
        const glm::vec3 color{static_cast<float>(glow * 0.5),
                              static_cast<float>(glow * static_cast<double>(std::bit_cast<float>(0x3e99999au))),
                              static_cast<float>(glow * static_cast<double>(std::bit_cast<float>(0x3dcccccdu)))};
        auto expectedLighting = std::make_unique<Client::World::TerrainLighting>();
        ASSERT_TRUE(
            Client::World::AddDynamicLight(expectedLighting.get(), explosion.position, color, 4, "test").HasValue());
        EXPECT_EQ(lighting->cells, expectedLighting->cells);
        static_cast<void>(expectedRandom.Next());
        static_cast<void>(expectedRandom.Next());
        EXPECT_EQ(random.State(), expectedRandom.State());
    }

    // Sin la luz dinámica, corta con el cuadro ya escrito.
    const auto blind = Client::World::StepParticlesChecked(particles, random, std::cref(clock), &map, trigonometry);
    ASSERT_FALSE(blind.HasValue());
    EXPECT_EQ(blind.Error(), Client::World::ParticleError::MissingDynamicLight);
    EXPECT_EQ(explosion.life, 16);
    EXPECT_EQ(std::bit_cast<std::int32_t>(explosion.field48), 2);
}

// 0x548644 → 0x54864b: la 5ed no pide otra mezcla que la de su material (con el byte en 4, 0x620a00(0)), va sin giro
// y con la celda del cuadro en el atlas de 4 × 4: la u del resto cuadro % 4 y la v del cociente cuadro / 4, cada una
// · 0,25f + 0,005f, y el tamaño (0,24f; 0,24f).
TEST(ParticlesTest, TheEarthquakeExplosionDrawsItsFrameFromAFourByFourAtlas)
{
    Particle explosion;
    explosion.alive = true;
    explosion.type = explosion.texture = Client::World::ExplosionParticle;
    explosion.scale = 0.5f;
    explosion.angles = {30.0f, 0.0f, 0.0f};
    explosion.rotation = 12.0f;
    explosion.light = glm::vec3(1.0f);
    explosion.position = {1.0f, 2.0f, 3.0f};
    const double inset = static_cast<double>(std::bit_cast<float>(0x3ba3d70au));
    for (const std::int32_t frame : {0, 6, 9})
    {
        SCOPED_TRACE(frame);
        explosion.field48 = std::bit_cast<float>(frame);
        const auto drawn = Client::Tests::DrawOneParticle(explosion, glm::vec2(64.0f), std::uint8_t{4});
        ASSERT_TRUE(drawn.HasValue());
        ASSERT_TRUE(drawn.Value().has_value());
        EXPECT_EQ(drawn.Value()->blend, Client::Tests::RecordedParticleDraw::Blend::Alpha);
        EXPECT_EQ(drawn.Value()->texture, Client::World::ExplosionParticle);
        EXPECT_EQ(drawn.Value()->rotation, 0.0f);
        EXPECT_EQ(drawn.Value()->position, (glm::vec3{1.0f, 2.0f, 3.0f}));
        EXPECT_EQ(drawn.Value()->uv, (glm::vec2{static_cast<float>((frame % 4) * 0.25 + inset),
                                                static_cast<float>((frame / 4) * 0.25 + inset)}));
        EXPECT_EQ(drawn.Value()->uvSize, glm::vec2(std::bit_cast<float>(0x3e75c28fu)));
    }
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
