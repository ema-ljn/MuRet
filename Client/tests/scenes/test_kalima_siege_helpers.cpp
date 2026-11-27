#include "Scenes/KalimaSiegeHelpers.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Data/Terrain/TerrainHeights.h"
#include "Math/X87.h"
#include "Render/ModelBatch.h"
#include "Math/HalfLifeMath.h"
#include "Math/RoundedMath.h"
#include "Render/Pass3D.h"
#include "Render/TextureSlot.h"
#include "Kernel/ActorState.h"
#include "Actors/CharacterStep.h"
#include "Scenes/GameDrawTail.h"
#include "World/KalimaWorld.h"
#include "World/ModelEffects.h"
#include "World/Particles.h"
#include "World/Sprites.h"

#include <gtest/gtest.h>

#include <glm/ext/vector_float3.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Los helpers de efectos por tipo de 0x510a1e (0x43c150 y 0x42dfe0) sobre un personaje ya posado: el hueso k en
// (0, 0, k) sobre (1000, 2000, 100) deja ver de qué hueso sale cada sprite. Vectores literales del ASM c32bbd5f, sin
// ejecutar el original. Kalima (24 y 36), Lorencia (0), Devias (2) y el castillo (30).
namespace
{
using Client::Crt::CrtRandom;
using Client::Kernel::ActorState;
using Client::Math::RoundedCos;
using Client::Math::RoundedSin;
using Client::Scenes::KalimaSiegeHelperPass;

constexpr std::uint32_t Seed = 1;
constexpr float Clock = 1234.0f;

using Client::Math::D;
using Client::Math::F32;

class KalimaSiegeHelperTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_actor = std::make_unique<ActorState>();
        m_actor->alive = true;
        m_actor->seen = true;
        m_actor->scale = 1.0f;
        m_actor->position = {1000.0f, 2000.0f, 100.0f};
        m_actor->angles = {0.0f, 0.0f, 90.0f};
        m_actor->light = {0.5f, 0.5f, 0.5f};
        m_actor->blendMeshLight = 0.6f;
        for (auto& hand : m_actor->hands)
        {
            hand.model = 0x123;
            hand.level = 3;
        }
        m_actor->bodyPose.assign(120, Client::Math::BoneTransform(1.0f));
        for (std::size_t bone = 0; bone < m_actor->bodyPose.size(); ++bone)
            m_actor->bodyPose[bone][3] = glm::vec3(0.0f, 0.0f, static_cast<float>(bone));
        m_sprites = std::make_unique<Client::World::FrameSprites>();
        m_particles = std::make_unique<Client::World::Particles>();
        m_effects = std::make_unique<Client::World::ModelEffects>();
        m_heights = std::make_unique<Client::Data::Terrain::TerrainHeights>();
        m_heights->cells.fill(0.0f);
        m_joints.assign(32, Client::Scenes::JointState{});
        m_batch.Begin(Client::Render::Pass3D{});
        m_random = CrtRandom{Seed};
        m_kalima.reset();
        m_shake = 0.0f;
        m_sound.reset();
        m_siege = false;
    }

    ActorState& Actor(std::uint16_t type, std::uint8_t action = 0, float frame = 0.0f)
    {
        m_actor->type = type;
        m_actor->animation.action = action;
        m_actor->animation.frame = frame;
        return *m_actor;
    }

    KalimaSiegeHelperPass Pass(std::int32_t map)
    {
        return {.batch = m_batch,
                .actor = *m_actor,
                .random = m_random,
                .sprites = m_sprites.get(),
                .particles = m_particles.get(),
                .joints = m_joints,
                .effects = m_effects.get(),
                .kalima = m_kalima.get(),
                .cameraShake = &m_shake,
                .heights = m_heights.get(),
                .sound = &m_sound,
                .worldClock = Clock,
                .map = map,
                .siegeActive = m_siege};
    }

    Client::Core::Expected<bool, std::string> Draw(std::int32_t map)
    {
        return Client::Scenes::DrawKalimaSiegeHelpers(Pass(map));
    }

    void FillParticles()
    {
        for (auto& particle : m_particles->All())
            particle.alive = true;
    }

    void FillJoints()
    {
        for (auto& joint : m_joints)
            joint.alive = true;
    }

    void FillEffects()
    {
        for (auto& effect : m_effects->Effects())
            effect.alive = true;
    }

    std::vector<Client::World::Sprite> Sprites()
    {
        std::vector<Client::World::Sprite> live;
        for (const auto& sprite : m_sprites->Sprites())
        {
            if (sprite.alive)
                live.push_back(sprite);
        }
        return live;
    }

    glm::vec3 Bone(std::size_t bone, glm::vec3 offset = {}) const
    {
        return m_actor->position + glm::vec3(offset.x, offset.y, offset.z + static_cast<float>(bone));
    }

    static std::uint32_t After(std::size_t draws)
    {
        CrtRandom random{Seed};
        for (std::size_t draw = 0; draw < draws; ++draw)
            static_cast<void>(random.Next());
        return random.State();
    }

    // El humo 0x5f2/13 (0x541c18) en el lugar 0 con los ángulos del personaje, contra un pool gemelo con el rand()
    // desde `state`; devuelve el rand() de después.
    [[nodiscard]] std::uint32_t ExpectTheEmber(const glm::vec3& point, const glm::vec3& light, float scale,
                                               std::uint32_t state) const
    {
        auto twin = std::make_unique<Client::World::Particles>();
        CrtRandom expected{state};
        const auto replayed = twin->CreateChecked(0x5f2, point, m_actor->angles, light, 13, scale, Clock, expected);
        EXPECT_TRUE(replayed.HasValue() && replayed.Value() == std::optional<std::size_t>{0});
        const Client::World::Particle& ember = m_particles->All()[0];
        const Client::World::Particle& reference = twin->All()[0];
        EXPECT_TRUE(ember.alive);
        EXPECT_EQ(ember.type, 0x5f2);
        EXPECT_EQ(ember.subtype, 13);
        EXPECT_EQ(ember.position, point);
        EXPECT_EQ(ember.angles, m_actor->angles);
        EXPECT_EQ(ember.light, light);
        EXPECT_EQ(ember.life, 20);
        EXPECT_EQ(ember.scale, reference.scale);
        EXPECT_EQ(ember.rotation, reference.rotation);
        EXPECT_EQ(ember.verticalSpeed, reference.verticalSpeed);
        EXPECT_FALSE(m_particles->All()[1].alive);
        return expected.State();
    }

    static void ExpectCut(const Client::Core::Expected<bool, std::string>& result, std::string_view address,
                          std::string_view what)
    {
        ASSERT_FALSE(result.HasValue());
        EXPECT_NE(result.Error().find(address), std::string::npos) << result.Error();
        EXPECT_NE(result.Error().find(what), std::string::npos) << result.Error();
    }

    std::unique_ptr<ActorState> m_actor;
    CrtRandom m_random{Seed};
    std::unique_ptr<Client::World::FrameSprites> m_sprites;
    std::unique_ptr<Client::World::Particles> m_particles;
    std::unique_ptr<Client::World::ModelEffects> m_effects;
    std::unique_ptr<Client::World::KalimaWorld> m_kalima;
    std::unique_ptr<Client::Data::Terrain::TerrainHeights> m_heights;
    std::vector<Client::Scenes::JointState> m_joints;
    Client::Render::ModelBatch m_batch;
    float m_shake = 0.0f;
    std::optional<std::int32_t> m_sound;
    bool m_siege = false;
};

// Un tipo sin helper devuelve false sin tocar nada (la cadena sigue); los del castillo, fuera del 30, también.
TEST_F(KalimaSiegeHelperTest, OtherTypesAndTheCastleOnesOutsideTheCastleGoOn)
{
    for (const std::int32_t map : {0, 2, 24, 30})
    {
        for (const std::uint16_t type : {std::uint16_t{0x15b}, std::uint16_t{0x190}, std::uint16_t{0x198}})
        {
            SCOPED_TRACE(testing::Message() << map << ' ' << type);
            Actor(type);
            const auto result = Draw(map);
            ASSERT_TRUE(result.HasValue()) << result.Error();
            EXPECT_FALSE(result.Value());
        }
    }
    for (const std::int32_t map : {0, 2})
    {
        for (const std::uint16_t type :
             {std::uint16_t{0x19c}, std::uint16_t{0x1a1}, std::uint16_t{0x1a8}, std::uint16_t{0x1fd}})
        {
            Actor(type);
            const auto result = Draw(map);
            ASSERT_TRUE(result.HasValue()) << result.Error();
            EXPECT_FALSE(result.Value());
        }
    }
    EXPECT_TRUE(Sprites().empty());
    EXPECT_EQ(m_random.State(), Seed);
    // Sin la tabla de sprites corta solo el que la usa.
    m_sprites.reset();
    Actor(0x15b);
    EXPECT_TRUE(Draw(0).HasValue());
    Actor(0x193);
    ExpectCut(Draw(24), "0x510a1e", "0x193");
}

// 0x43c17f..0x43c22a, el 0xc5: en la acción 1, con +0x60 en 0, el agujero 0x6ae del hueso 8 con sus ángulos, escala 3
// y un rand() (0x543bf1: vida 30, luz 0,1f y giro rand() % 360); +0x60 cuenta de 0 a 4. Con la tabla llena, sin rand().
// En Kalima y en Lorencia.
TEST_F(KalimaSiegeHelperTest, TheKalimaGateCountsItsParticle)
{
    for (const std::int32_t map : {24, 0})
    {
        SCOPED_TRACE(map);
        SetUp();
        ActorState& gate = Actor(0xc5, 0);
        auto result = Draw(map);
        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_TRUE(result.Value());
        EXPECT_EQ(gate.dword60, 0);

        gate.animation.action = 1;
        gate.angles = {0.0f, 0.0f, 90.0f};
        result = Draw(map);
        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_EQ(gate.dword60, 1);
        CrtRandom draws{Seed};
        const int turn = draws.Next() % 360;
        EXPECT_EQ(m_random.State(), draws.State());
        const Client::World::Particle& hole = m_particles->All()[0];
        ASSERT_TRUE(hole.alive);
        EXPECT_EQ(hole.type, 0x6ae);
        EXPECT_EQ(hole.position, Bone(8));
        EXPECT_EQ(hole.angles, gate.angles);
        EXPECT_EQ(hole.scale, 3.0f);
        EXPECT_EQ(hole.life, 30);
        EXPECT_EQ(hole.light, glm::vec3(0.1f));
        EXPECT_EQ(hole.rotation, static_cast<float>(turn));
        EXPECT_FALSE(m_particles->All()[1].alive);

        FillParticles();
        for (const std::int32_t next : {2, 3, 4, 0, 1})
        {
            result = Draw(map);
            ASSERT_TRUE(result.HasValue()) << result.Error();
            EXPECT_EQ(gate.dword60, next);
        }
        EXPECT_EQ(m_random.State(), draws.State());
        EXPECT_TRUE(Sprites().empty());
    }
}

// 0x43c233..0x43c646, el 0x191: fuera de la acción 6, cinco vueltas por los huesos 74..78 y 62..66 con dos 0x5ac y un
// rand() & 1 cada uno (en 0, la burbuja 0x607/1: 0x43c332 arriba, 0x43c3bf abajo), los ojos del hueso 8 y los huesos 22
// y 15; siempre, las estrellas de los huesos 51..55 y 29..33.
TEST_F(KalimaSiegeHelperTest, TheFirstKalimaHelperDrawsItsStarsAndDrawsItsRandoms)
{
    Actor(0x191, 6);
    ASSERT_TRUE(Draw(24).HasValue());
    auto sprites = Sprites();
    ASSERT_EQ(sprites.size(), 10u);
    const double lum = RoundedSin(D(Clock) * D(0.001f)) * D(0.2f) + D(0.4f);
    const glm::vec3 spine{F32(lum * D(0.1f)), F32(D(F32(lum)) * D(0.4f)), F32(lum)};
    EXPECT_EQ(sprites[0].texture, 0x5ac);
    EXPECT_EQ(sprites[0].position, Bone(51));
    EXPECT_EQ(sprites[0].scale, 2.0f);
    EXPECT_EQ(sprites[0].rotation, Clock);
    EXPECT_EQ(sprites[0].light, spine);
    EXPECT_EQ(sprites[1].position, Bone(29));
    EXPECT_EQ(sprites[1].scale, 1.5f);
    EXPECT_EQ(sprites[9].position, Bone(33));
    EXPECT_EQ(m_random.State(), Seed);

    SetUp();
    const ActorState& helper = Actor(0x191, 0);
    FillParticles();
    ASSERT_TRUE(Draw(24).HasValue());
    sprites = Sprites();
    ASSERT_EQ(sprites.size(), 36u);
    EXPECT_EQ(sprites[0].position, Bone(74));
    EXPECT_EQ(sprites[0].scale, 1.5f);
    EXPECT_EQ(sprites[0].light, glm::vec3(F32(0.25 * D(0.1f)), F32(0.25 * D(0.4f)), 0.25f));
    EXPECT_EQ(sprites[2].position, Bone(62));
    EXPECT_EQ(sprites[18].position, Bone(66));
    EXPECT_EQ(sprites[18].scale, F32(1.5 - 4.0 * D(0.2f)));
    EXPECT_EQ(sprites[18].light.z, 1.25f);
    // Los ojos: el 0x5ca blanco con +0x68 y el 0x5fd azul; los huesos 22 y 15, azules los dos.
    EXPECT_EQ(sprites[20].texture, 0x5ca);
    EXPECT_EQ(sprites[20].position, Bone(8, {2.0f, 2.0f, 0.0f}));
    EXPECT_EQ(sprites[20].light, glm::vec3(0.6f));
    const glm::vec3 blue{F32(D(0.6f) * D(0.1f)), F32(D(0.6f) * D(0.1f)), 0.6f};
    EXPECT_EQ(sprites[21].texture, 0x5fd);
    EXPECT_EQ(sprites[21].light, blue);
    EXPECT_EQ(sprites[22].position, Bone(22));
    EXPECT_EQ(sprites[22].light, blue);
    EXPECT_EQ(sprites[24].position, Bone(15));
    EXPECT_EQ(sprites[26].position, Bone(51));
    EXPECT_EQ(m_random.State(), After(10));
    EXPECT_EQ(helper.dword60, 0);

    // Con lugar en el pool, cada rand() & 1 en 0 suelta en su hueso la burbuja 0x607/1 de 1, con los ángulos del
    // personaje y la luz 1 de su alta (0x542d96), y dos rand() más: la primera, la del hueso 75 (41 y 18467 son
    // impares, 6334 par).
    SetUp();
    Actor(0x191, 0);
    ASSERT_TRUE(Draw(2).HasValue());
    EXPECT_EQ(Sprites().size(), 36u);
    auto twin = std::make_unique<Client::World::Particles>();
    CrtRandom expected{Seed};
    std::size_t made = 0;
    for (std::size_t round = 0; round < 5; ++round)
    {
        for (const std::size_t bone : {74 + round, 62 + round})
        {
            if ((expected.Next() & 1) != 0)
                continue;
            SCOPED_TRACE(bone);
            const auto replayed =
                twin->CreateChecked(0x607, Bone(bone), m_actor->angles, glm::vec3(0.5f), 1, 1.0f, Clock, expected);
            ASSERT_TRUE(replayed.HasValue() && replayed.Value() == std::optional<std::size_t>{made});
            const Client::World::Particle& bubble = m_particles->All()[made];
            const Client::World::Particle& reference = twin->All()[made];
            EXPECT_TRUE(bubble.alive);
            EXPECT_EQ(bubble.type, 0x607);
            EXPECT_EQ(bubble.subtype, 1);
            EXPECT_EQ(bubble.position, Bone(bone));
            EXPECT_EQ(bubble.light, glm::vec3(1.0f));
            EXPECT_EQ(bubble.life, reference.life);
            EXPECT_EQ(bubble.scale, reference.scale);
            ++made;
        }
    }
    EXPECT_EQ(m_particles->All()[0].position, Bone(75));
    EXPECT_FALSE(m_particles->All()[made].alive);
    EXPECT_EQ(m_random.State(), expected.State());
}

// 0x43c647..0x43c9d0, Kundun (0x192): en la acción 6 suelta la mano izquierda; si no, los ojos de los huesos 8 y 9,
// el 0x62b del hueso 100 y un rand() & 1 para la partícula 0x5f2/13.
TEST_F(KalimaSiegeHelperTest, KundunDropsItsLeftHandOrLightsItsStaff)
{
    ActorState& kundun = Actor(0x192, 6);
    ASSERT_TRUE(Draw(24).HasValue());
    EXPECT_EQ(kundun.hands[1].model, 0xffff);
    EXPECT_EQ(kundun.hands[1].level, 0xff);
    EXPECT_EQ(kundun.hands[0].model, 0x123);
    EXPECT_TRUE(Sprites().empty());

    SetUp();
    Actor(0x192, 0).word8a = 7;
    FillParticles();
    ASSERT_TRUE(Draw(0).HasValue());
    const auto sprites = Sprites();
    ASSERT_EQ(sprites.size(), 3u);
    const float red = F32(RoundedSin(D(Clock) * D(0.003f)) * D(0.2f) + D(0.8f));
    EXPECT_EQ(sprites[0].position, Bone(8));
    EXPECT_EQ(sprites[0].light, glm::vec3(red, 0.0f, 0.0f));
    EXPECT_EQ(sprites[0].scale, 0.4f);
    EXPECT_EQ(sprites[1].position, Bone(9));
    EXPECT_EQ(sprites[2].texture, 0x62b);
    EXPECT_EQ(sprites[2].position, Bone(100));
    EXPECT_EQ(sprites[2].scale, F32(RoundedSin(D(Clock) * D(0.001f)) * D(0.3f) + D(1.2f)));
    EXPECT_EQ(m_actor->word8a, 0);
    EXPECT_EQ(m_random.State(), After(1));

    // El humo 0x5f2/13 con lugar: 41 es impar, 18467 también; la tercera vez (6334) lo suelta en el bastón con la luz 1
    // y escala 1 (tres rand() más).
    SetUp();
    Actor(0x192, 0);
    ASSERT_TRUE(Draw(0).HasValue());
    ASSERT_TRUE(Draw(0).HasValue());
    EXPECT_FALSE(m_particles->Any());
    const auto third = Draw(0);
    ASSERT_TRUE(third.HasValue()) << third.Error();
    EXPECT_EQ(m_random.State(), ExpectTheEmber(Bone(100), glm::vec3(1.0f), 1.0f, After(3)));
}

// 0x43c7a8..0x43c8e3 (Q-C879): el pulso de la acción 5 avanza con cada dibujo. +0x8a sube pasado el frame 4 y baja
// hasta 10 pasado el 10; +0x89 sube 3 antes de la mitad de las ventanas (4, 6) y (8, 10), baja 3 después si pasa de 3
// y fuera de ellas vuelve a 0. El 0x62d crece con los dos.
TEST_F(KalimaSiegeHelperTest, KundunsPulseFollowsTheDrawsOfItsFifthAction)
{
    ActorState& kundun = Actor(0x192, 5);
    kundun.dword60 = 0x65;
    kundun.word8a = 10;
    FillParticles();
    const double halo = RoundedSin(D(Clock) * D(0.001f)) * D(0.3f) + D(1.2f);
    struct Beat
    {
        float frame;
        std::uint8_t heldBeat;
        std::int16_t pulse;
        std::uint8_t beat;
    };
    const std::array<Beat, 5> beats{Beat{4.5f, 0, 11, 3}, Beat{5.5f, 3, 12, 3}, Beat{7.0f, 3, 13, 0},
                                    Beat{11.0f, 0, 12, 0}, Beat{9.5f, 7, 13, 4}};
    for (std::size_t draw = 0; draw < beats.size(); ++draw)
    {
        const Beat& beat = beats[draw];
        SCOPED_TRACE(beat.frame);
        kundun.animation.frame = beat.frame;
        kundun.byte89 = beat.heldBeat;
        ASSERT_TRUE(Draw(0).HasValue());
        EXPECT_EQ(kundun.word8a, beat.pulse);
        EXPECT_EQ(kundun.byte89, beat.beat);
        const float pulse = F32(static_cast<double>(beat.pulse) * D(0.15f));
        const float scale = F32(static_cast<double>(beat.beat) * D(0.15f) + D(pulse));
        const auto sprites = Sprites();
        ASSERT_EQ(sprites.size(), 4 * (draw + 1));
        EXPECT_EQ(sprites.back().texture, 0x62d);
        EXPECT_EQ(sprites.back().scale, F32(halo * D(scale)));
    }
    // El rand() & 1 de la partícula y el & 7 del temblor, en cada dibujo.
    EXPECT_EQ(m_random.State(), After(2 * beats.size()));
}

// La acción 5 de Kundun: antes del frame 2, +0x60 en 100 y el sonido 0x1db (CL10) una sola vez; el pulso de +0x8a y
// +0x89 agranda el 0x62d; pasado el 4, +0x60 en 0x65 y, en Kalima, 0x437860 en su celda lógica; pasado el 3, el temblor
// (rand() & 7 − 8) · 0,2; en Kalima entre el 3 y el 14, dos efectos 9 que caen.
TEST_F(KalimaSiegeHelperTest, KundunRoarsShakesAndDropsStonesInKalima)
{
    ActorState& kundun = Actor(0x192, 5, 1.0f);
    FillParticles();
    ASSERT_TRUE(Draw(0).HasValue());
    EXPECT_EQ(m_sound, std::optional<std::int32_t>{0x1db});
    EXPECT_EQ(kundun.dword60, 100);
    EXPECT_EQ(kundun.word8a, 10);
    auto sprites = Sprites();
    ASSERT_EQ(sprites.size(), 4u);
    const double halo = RoundedSin(D(Clock) * D(0.001f)) * D(0.3f) + D(1.2f);
    EXPECT_EQ(sprites[3].texture, 0x62d);
    EXPECT_EQ(sprites[3].scale, F32(halo * D(F32(10.0 * D(0.15f)))));
    EXPECT_EQ(m_shake, 0.0f);
    m_sound.reset();
    ASSERT_TRUE(Draw(0).HasValue());
    EXPECT_FALSE(m_sound.has_value());

    for (const std::int32_t map : {0, 2, 24, 36})
    {
        SCOPED_TRACE(map);
        SetUp();
        const bool kalima = map == 24 || map == 36;
        if (kalima)
            m_kalima = std::make_unique<Client::World::KalimaWorld>(map);
        ActorState& roaring = Actor(0x192, 5, 5.0f);
        roaring.dword60 = 100;
        roaring.cellX = 40;
        roaring.cellY = 50;
        FillParticles();
        // Lo que sacan, en su orden: el & 1, las celdas y 0x437860, el & 7 y los dos efectos.
        CrtRandom expected{Seed};
        static_cast<void>(expected.Next());
        auto kalimaCopy = kalima ? m_kalima->CloneForFrame() : nullptr;
        ActorState cells = roaring;
        if (kalima)
        {
            const auto cell = Client::Actors::ReadLogicalCell(cells, expected);
            ASSERT_TRUE(kalimaCopy->Disturb(cell.x, cell.y, 2, 2000, expected).HasValue());
        }
        const float shake = F32(static_cast<double>((expected.Next() & 7) - 8) * D(0.2f));
        auto effects = std::make_unique<Client::World::ModelEffects>();
        std::vector<glm::vec3> stones;
        if (kalima)
        {
            for (int stone = 0; stone < 2; ++stone)
            {
                const float turn = static_cast<float>(expected.Next() % 360);
                const double distance = static_cast<double>(expected.Next() % 600 + 200);
                const glm::vec3 position{F32(RoundedSin(D(turn)) * distance + 1000.0),
                                         F32(RoundedCos(D(turn)) * distance + 2000.0), 900.0f};
                stones.push_back(position);
                static_cast<void>(effects->CreateKalimaEffect(position, {}, {0.3f, 0.8f, 1.0f}, expected));
            }
        }

        const auto result = Draw(map);

        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_EQ(roaring.dword60, 0x65);
        EXPECT_EQ(m_shake, shake);
        EXPECT_EQ(m_random.State(), expected.State());
        EXPECT_FALSE(m_sound.has_value());
        std::vector<glm::vec3> made;
        for (const auto& effect : m_effects->Effects())
        {
            if (effect.alive)
                made.push_back(effect.transform.position);
        }
        EXPECT_EQ(made, stones);
    }

    // Sin el temblor de la cámara corta, después del rand().
    SetUp();
    Actor(0x192, 5, 3.5f).dword60 = 0x65;
    FillParticles();
    KalimaSiegeHelperPass pass = Pass(2);
    pass.cameraShake = nullptr;
    ExpectCut(Client::Scenes::DrawKalimaSiegeHelpers(pass), "0x43cec4", "0x7d325c8");
}

// Las acciones 3 y 4 de Kundun: el efecto 0x128 (0x518cf0, Scenes/KundunEffects), sin rand(); con la tabla llena,
// nada. En la 4, pasado el 6, veinticuatro joints 0x6ee/0xe y, en Kalima, 0x437860; pasado el 9, otra vez 0x437860;
// pasado el 6, el temblor (rand() & 15 − 16) · 0,1. El hueso 49 va en (5, 7, 49), fuera del eje del objeto en
// (1000, 2000, 100): el /0 toma la x y la y del objeto, (1000, 2000, 149), y el /1 las del hueso, (1005, 2007, 400).
TEST_F(KalimaSiegeHelperTest, KundunAttacksCallTheirEffectsAndCutInTheirJoints)
{
    constexpr std::size_t HandBone = 49;
    const glm::vec3 hand{5.0f, 7.0f, 49.0f};
    std::uint32_t drawn = 0;
    for (const bool full : {false, true})
    {
        SCOPED_TRACE(full);
        SetUp();
        m_actor->bodyPose[HandBone][3] = hand;
        ActorState& kundun = Actor(0x192, 3, 1.0f);
        FillParticles();
        if (full)
            FillEffects();
        ASSERT_TRUE(Draw(24).HasValue());
        EXPECT_EQ(kundun.dword60, 100);
        kundun.animation.frame = 3.5f;
        const auto result = Draw(24);
        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_EQ(kundun.dword60, 0x65);
        if (full)
        {
            EXPECT_EQ(m_random.State(), drawn);
            continue;
        }
        drawn = m_random.State();
        // 0x43cff8..0x43d051: la x y la y del objeto con la z del hueso 49; sus ángulos y su luz, el /0 y la clave −1.
        const auto& call = m_effects->Effects()[0];
        ASSERT_TRUE(call.alive);
        EXPECT_EQ(call.type, 0x128);
        EXPECT_EQ(call.subtype, 0);
        EXPECT_EQ(call.life, 30);
        EXPECT_EQ(call.transform.position, (glm::vec3{1000.0f, 2000.0f, 149.0f}));
        EXPECT_EQ(call.transform.angles, kundun.angles);
        EXPECT_EQ(call.light, kundun.light);
        EXPECT_FALSE(call.hasOwner);
        EXPECT_EQ(call.disturbanceState, 0xffff);
        EXPECT_FALSE(m_effects->Effects()[1].alive);
    }

    // 0x43d09b..0x43d0fd: el /1 en el hueso 49 a 400 de altura.
    SetUp();
    m_actor->bodyPose[HandBone][3] = hand;
    Actor(0x192, 4, 5.5f).dword60 = 100;
    FillParticles();
    {
        const auto result = Draw(2);
        ASSERT_TRUE(result.HasValue()) << result.Error();
        const auto& call = m_effects->Effects()[0];
        ASSERT_TRUE(call.alive);
        EXPECT_EQ(call.type, 0x128);
        EXPECT_EQ(call.subtype, 1);
        EXPECT_EQ(call.life, 30);
        EXPECT_EQ(call.transform.position, (glm::vec3{1005.0f, 2007.0f, 400.0f}));
    }

    // 0x43d19f: los veinticuatro joints 0x6ee/0xe (0x53349a: quietos, vida 10, un punto y la x de los ángulos en 20)
    // desde el hueso 49 a 400 de altura; fuera de Kalima, el temblor y nada más.
    SetUp();
    ActorState& burst = Actor(0x192, 4, 6.5f);
    burst.dword60 = 0x65;
    FillParticles();
    {
        CrtRandom expected{Seed};
        static_cast<void>(expected.Next());
        static_cast<void>(expected.Next());
        const auto result = Draw(0);
        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_EQ(burst.dword60, 0x66);
        EXPECT_EQ(std::ranges::count_if(m_joints, [](const Client::Scenes::JointState& joint) { return joint.alive; }),
                  24);
        for (std::size_t turn = 0; turn < 24; ++turn)
        {
            const Client::Scenes::JointState& joint = m_joints[turn];
            EXPECT_EQ(joint.type, 0x6ee);
            EXPECT_EQ(joint.subtype, 0xe);
            EXPECT_EQ(joint.angles, glm::vec3(20.0f, 0.0f, 30.0f * static_cast<float>(turn)));
            EXPECT_EQ(joint.position.z, 400.0f);
            EXPECT_EQ(joint.life, 10);
            EXPECT_EQ(joint.maximumCount, 1);
            EXPECT_EQ(joint.width, 100.0f);
        }
        EXPECT_EQ(m_random.State(), expected.State());
    }

    for (const std::int32_t map : {2, 24})
    {
        SCOPED_TRACE(map);
        SetUp();
        const bool kalima = map == 24;
        if (kalima)
            m_kalima = std::make_unique<Client::World::KalimaWorld>(map);
        ActorState& striking = Actor(0x192, 4, 9.5f);
        striking.dword60 = 0x65;
        striking.cellX = 12;
        striking.cellY = 34;
        FillParticles();
        FillJoints();
        CrtRandom expected{Seed};
        static_cast<void>(expected.Next());
        if (kalima)
        {
            auto copy = m_kalima->CloneForFrame();
            ActorState cells = striking;
            for (int disturb = 0; disturb < 2; ++disturb)
            {
                const auto cell = Client::Actors::ReadLogicalCell(cells, expected);
                ASSERT_TRUE(copy->Disturb(cell.x, cell.y, 2, 2000, expected).HasValue());
            }
        }
        const float shake = F32(static_cast<double>((expected.Next() & 15) - 16) * D(0.1f));

        const auto result = Draw(map);

        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_EQ(striking.dword60, 0x67);
        EXPECT_EQ(m_shake, shake);
        EXPECT_EQ(m_random.State(), expected.State());
    }
}

// 0x43dc8a..0x43e07f, el 0x193: doce sprites, sin rand(); en la acción 6 nada.
TEST_F(KalimaSiegeHelperTest, TheThirdKalimaHelperDrawsTwelveSprites)
{
    Actor(0x193, 0);
    ASSERT_TRUE(Draw(24).HasValue());
    const auto sprites = Sprites();
    ASSERT_EQ(sprites.size(), 12u);
    const float lum = F32(RoundedSin(D(Clock) * D(0.003f)) * D(0.2f) + D(0.8f));
    const float pale = F32(D(lum) * D(0.9f));
    EXPECT_EQ(sprites[0].texture, 0x5ca);
    EXPECT_EQ(sprites[0].position, Bone(6, {20.0f, 30.0f, -7.0f}));
    EXPECT_EQ(sprites[0].light, glm::vec3(lum, pale, pale));
    EXPECT_EQ(sprites[1].light, glm::vec3(lum, F32(D(lum) * D(0.3f)), F32(D(lum) * D(0.1f))));
    EXPECT_EQ(sprites[2].position, Bone(6, {20.0f, 30.0f, 7.0f}));
    EXPECT_EQ(sprites[4].position, Bone(13, {0.0f, 0.0f, 7.0f}));
    EXPECT_EQ(sprites[5].light, glm::vec3(lum, F32(D(lum) * D(0.5f)), F32(D(lum) * D(0.1f))));
    EXPECT_EQ(sprites[5].scale, 0.7f);
    EXPECT_EQ(sprites[9].position, Bone(25, {-30.0f, -25.0f, 0.0f}));
    EXPECT_EQ(sprites[11].position, Bone(33, {30.0f, 25.0f, 0.0f}));
    EXPECT_EQ(m_random.State(), Seed);

    SetUp();
    Actor(0x193, 6);
    const auto dead = Draw(24);
    ASSERT_TRUE(dead.HasValue());
    EXPECT_TRUE(dead.Value());
    EXPECT_TRUE(Sprites().empty());
}

// 0x43e080..0x43e3f2, el 0x194: ocho sprites con dos rand() % 360 y, en la acción 3, los doce joints 0x614/7 entre
// los huesos 10..16 y 31..37, sin rand() (0x5315b7).
TEST_F(KalimaSiegeHelperTest, TheFourthKalimaHelperTurnsItsGlowsAndChainsItsBones)
{
    Actor(0x194, 0);
    ASSERT_TRUE(Draw(24).HasValue());
    auto sprites = Sprites();
    ASSERT_EQ(sprites.size(), 8u);
    EXPECT_EQ(sprites[0].position, Bone(9, {4.0f, 0.0f, 5.0f}));
    EXPECT_EQ(sprites[4].texture, 0x5ac);
    EXPECT_EQ(sprites[4].position, Bone(3));
    EXPECT_EQ(sprites[4].scale, 1.7f);
    EXPECT_EQ(sprites[4].light, glm::vec3(0.7f, 0.6f, 1.0f));
    EXPECT_EQ(sprites[5].rotation, 41.0f);
    EXPECT_EQ(sprites[7].rotation, static_cast<float>(18467 % 360));
    EXPECT_EQ(m_random.State(), After(2));

    SetUp();
    Actor(0x194, 3);
    ASSERT_TRUE(Draw(36).HasValue());
    EXPECT_EQ(std::ranges::count_if(m_joints, [](const Client::Scenes::JointState& joint) { return joint.alive; }), 12);
    EXPECT_EQ(m_joints[0].type, 0x614);
    EXPECT_EQ(m_joints[0].subtype, 7);
    EXPECT_EQ(m_joints[0].position, Bone(10));
    EXPECT_EQ(m_joints[0].targetPosition, Bone(11));
    EXPECT_EQ(m_joints[0].maximumCount, 3);
    EXPECT_EQ(m_joints[0].life, 1);
    EXPECT_TRUE(m_joints[0].repeatedTexture);
    EXPECT_EQ(m_joints[6].position, Bone(31));
    EXPECT_EQ(m_joints[11].targetPosition, Bone(37));
    EXPECT_EQ(m_random.State(), After(2));
    SetUp();
    Actor(0x194, 3);
    FillJoints();
    ASSERT_TRUE(Draw(36).HasValue());
    EXPECT_EQ(m_random.State(), After(2));
}

// 0x43e3f3..0x43e605, el 0x195: en la acción 6 suelta las dos manos; el 0x91 (+0x376) es rojo y saca rand() & 1 para
// el humo 0x5f2/13 (0x43e4c0); los demás, blancos y sin rand().
TEST_F(KalimaSiegeHelperTest, TheFifthKalimaHelperIsRedForItsMonster)
{
    ActorState& helper = Actor(0x195, 6);
    ASSERT_TRUE(Draw(24).HasValue());
    EXPECT_EQ(helper.hands[0].model, 0xffff);
    EXPECT_EQ(helper.hands[1].model, 0xffff);
    EXPECT_EQ(helper.hands[1].level, 3);

    SetUp();
    Actor(0x195, 0).word376 = 0x90;
    ASSERT_TRUE(Draw(24).HasValue());
    auto sprites = Sprites();
    ASSERT_EQ(sprites.size(), 3u);
    const double wave = RoundedSin(D(Clock) * D(0.001f));
    EXPECT_EQ(sprites[0].texture, 0x62b);
    EXPECT_EQ(sprites[0].position, Bone(0, {0.0f, 0.0f, 30.0f}));
    EXPECT_EQ(sprites[0].scale, F32(wave * D(0.3f) + D(1.2f)));
    EXPECT_EQ(sprites[0].light, glm::vec3(1.0f));
    EXPECT_EQ(sprites[1].position, Bone(28, {5.0f, 0.0f, 0.0f}));
    EXPECT_EQ(sprites[2].position, Bone(29, {5.0f, 0.0f, 0.0f}));
    EXPECT_EQ(m_random.State(), Seed);

    SetUp();
    Actor(0x195, 0).word376 = 0x91;
    ASSERT_TRUE(Draw(24).HasValue()); // 41, impar
    sprites = Sprites();
    EXPECT_EQ(sprites[0].scale, F32(wave * D(0.3f) + 2.0));
    EXPECT_EQ(sprites[0].light, glm::vec3(1.0f, 0.0f, 0.0f));
    ASSERT_TRUE(Draw(24).HasValue()); // 18467
    EXPECT_FALSE(m_particles->Any());
    const auto third = Draw(24); // 6334, par: el humo rojo en el punto (0, 0, 30) del hueso 0, de 1
    ASSERT_TRUE(third.HasValue()) << third.Error();
    EXPECT_EQ(m_random.State(),
              ExpectTheEmber(Bone(0, {0.0f, 0.0f, 30.0f}), glm::vec3(1.0f, 0.0f, 0.0f), 1.0f, After(3)));
}

// 0x43e606..0x43e923, el 0x196: la partícula 0x5ac/0 del hueso 60 (la neblina de 0x5435cf, con sus rand()) y, en las
// acciones 0 y 1, rand() % 50 por cada hueso de 63 a 67; en 0, dos rand() % 360 y el joint 0x610/5 hacia el 60
// (0x43e904: con el skill 0, ancho 20; vida 50, velocidad 3, 8 puntos, la mezcla 4 y la segunda cara; sin rand()).
TEST_F(KalimaSiegeHelperTest, TheSixthKalimaHelperBeatsAndSparks)
{
    ActorState& heart = Actor(0x196, 2);
    ASSERT_TRUE(Draw(24).HasValue());
    const double beat = RoundedSin(D(Clock) * D(0.002f)) * D(0.3f) + D(0.6f);
    CrtRandom expected{Seed};
    auto mist = std::make_unique<Client::World::Particles>();
    ASSERT_TRUE(
        mist->CreateChecked(0x5ac, Bone(60), heart.angles, glm::vec3(F32(beat)), 0, 1.1f, Clock, expected).HasValue());
    EXPECT_EQ(m_random.State(), expected.State());
    const auto made = std::ranges::find_if(m_particles->All(), [](const auto& particle) { return particle.alive; });
    ASSERT_NE(made, m_particles->All().end());
    EXPECT_EQ(made->type, 0x5ac);
    EXPECT_EQ(made->position, Bone(60));
    EXPECT_EQ(made->light, glm::vec3(F32(beat)));

    SetUp();
    Actor(0x196, 4);
    FillParticles();
    ASSERT_TRUE(Draw(24).HasValue());
    const auto sprites = Sprites();
    ASSERT_EQ(sprites.size(), 10u);
    const double lum = RoundedSin(D(Clock) * D(0.002f)) * D(0.3f) + D(0.6f);
    const float tenth = F32(lum * D(0.1f));
    EXPECT_EQ(sprites[2].texture, 0x5ca);
    EXPECT_EQ(sprites[2].position, Bone(60));
    EXPECT_EQ(sprites[2].light, glm::vec3(tenth, F32(lum), tenth));
    EXPECT_EQ(sprites[2].rotation, F32(D(Clock) * D(0.1f)));
    EXPECT_EQ(sprites[3].rotation, F32(D(Clock) * D(-0.1f)));
    EXPECT_EQ(sprites[4].position, Bone(10, {5.0f, 0.0f, 0.0f}));
    EXPECT_EQ(sprites[4].light, glm::vec3(0.1f, 0.4f, 1.0f));
    EXPECT_EQ(sprites[9].position, Bone(67));
    EXPECT_EQ(m_random.State(), Seed);

    // 41, 18467 y 6334 no dan 0 % 50; 26500 sí (el hueso 66), y saca dos más para los ángulos.
    SetUp();
    Actor(0x196, 0);
    FillParticles();
    const auto sparked = Draw(24);
    ASSERT_TRUE(sparked.HasValue()) << sparked.Error();
    CrtRandom turns{After(4)};
    const auto pitch = static_cast<float>(turns.Next() % 360);
    const auto yaw = static_cast<float>(turns.Next() % 360);
    const Client::Scenes::JointState& spark = m_joints[0];
    EXPECT_TRUE(spark.alive);
    EXPECT_EQ(spark.type, 0x610);
    EXPECT_EQ(spark.subtype, 5);
    EXPECT_EQ(spark.position, Bone(66));
    EXPECT_EQ(spark.targetPosition, Bone(60));
    EXPECT_EQ(spark.angles.x, pitch);
    EXPECT_EQ(spark.angles.z, yaw);
    EXPECT_EQ(spark.width, 20.0f);
    EXPECT_EQ(spark.life, 50);
    EXPECT_EQ(spark.speed, 3.0f);
    EXPECT_EQ(spark.maximumCount, 8);
    EXPECT_EQ(spark.blend, 4);
    EXPECT_EQ(spark.drawEnabled, 2);
    EXPECT_EQ(spark.category, 2);
    EXPECT_EQ(spark.light, glm::vec3(1.0f));
    EXPECT_FALSE(m_joints[1].alive);
    // El hueso 67 saca su rand() % 50 (que no da 0).
    EXPECT_EQ(m_random.State(), After(7));
    SetUp();
    Actor(0x196, 1);
    FillParticles();
    FillJoints();
    ASSERT_TRUE(Draw(24).HasValue());
    EXPECT_EQ(m_random.State(), After(7));
}

// 0x43e924..0x43ebef, el 0x197: rojo con el subtipo 9 (+0x4) y azul con otro; seis vueltas de dos partículas por los
// huesos 41 y 51 (0x5da/1 o 0x5dc/12). La 0x5da/1 (0x540d1b) vive 3 y saca dos rand(): el empuje ((rand() & 7) + 32) ·
// −0,3f en y y el giro. En la acción 6 suelta las dos manos.
TEST_F(KalimaSiegeHelperTest, TheSeventhKalimaHelperIsRedOrBlue)
{
    Actor(0x197, 0).subtype = 9;
    ASSERT_TRUE(Draw(36).HasValue());
    EXPECT_EQ(m_random.State(), After(24));
    auto sprites = Sprites();
    ASSERT_EQ(sprites.size(), 4u);
    const float lum = F32(RoundedSin(D(Clock) * D(0.003f)) * D(0.2f) + D(0.8f));
    const float pale = F32(D(lum) * D(0.9f));
    EXPECT_EQ(sprites[0].position, Bone(33));
    EXPECT_EQ(sprites[0].light, glm::vec3(lum, pale, pale));
    EXPECT_EQ(sprites[3].position, Bone(34));
    CrtRandom red{Seed};
    for (std::size_t slot = 0; slot < 12; ++slot)
    {
        const auto& particle = m_particles->All()[slot];
        EXPECT_EQ(particle.type, 0x5da);
        EXPECT_EQ(particle.subtype, 1);
        EXPECT_EQ(particle.life, 3);
        EXPECT_EQ(particle.light, glm::vec3(lum, pale, pale));
        const float offset = -40.0f - static_cast<float>(slot / 2) * 24.0f;
        EXPECT_EQ(particle.position, Bone(slot % 2 == 0 ? 41 : 51, {0.0f, offset, 0.0f}));
        EXPECT_EQ(particle.scale, F32(D(7.2f) / static_cast<double>((slot / 2) / 2 + 6)));
        EXPECT_EQ(particle.velocity, glm::vec3(0.0f, F32(static_cast<double>((red.Next() & 7) + 32) * -D(0.3f)), 0.0f));
        EXPECT_EQ(particle.rotation, static_cast<float>(red.Next() % 360));
    }
    EXPECT_FALSE(m_particles->All()[12].alive);

    SetUp();
    Actor(0x197, 0).subtype = 2;
    ASSERT_TRUE(Draw(36).HasValue());
    EXPECT_EQ(m_random.State(), After(48));
    CrtRandom expected{Seed};
    for (std::size_t slot = 0; slot < 12; ++slot)
    {
        const auto& particle = m_particles->All()[slot];
        EXPECT_TRUE(particle.alive);
        EXPECT_EQ(particle.type, 0x5dc);
        EXPECT_EQ(particle.subtype, 12);
        EXPECT_EQ(particle.life, 24);
        EXPECT_EQ(particle.light, glm::vec3(pale, pale, lum));
        const float offset = -40.0f - static_cast<float>(slot / 2) * 24.0f;
        EXPECT_EQ(particle.position, Bone(slot % 2 == 0 ? 41 : 51, {0.0f, offset, 0.0f}));
        static_cast<void>(expected.Next());
        static_cast<void>(expected.Next());
        const float scale = F32(D(7.2f) / static_cast<double>((slot / 2) / 2 + 6) * 0.5);
        EXPECT_EQ(particle.scale, F32((expected.Next() % 16 + 150) * D(scale) * D(0.012f)));
        static_cast<void>(expected.Next());
    }
    EXPECT_FALSE(m_particles->All()[12].alive);
    sprites = Sprites();
    EXPECT_EQ(sprites[0].light, glm::vec3(0.0f, pale, lum));
    EXPECT_EQ(sprites[2].light, glm::vec3(F32(D(lum) * D(0.1f)), F32(D(lum) * D(0.3f)), lum));

    SetUp();
    Actor(0x197, 0);
    FillParticles();
    ASSERT_TRUE(Draw(36).HasValue());
    EXPECT_EQ(m_random.State(), Seed);

    SetUp();
    ActorState& dead = Actor(0x197, 6);
    ASSERT_TRUE(Draw(36).HasValue());
    EXPECT_EQ(dead.hands[0].model, 0xffff);
    EXPECT_EQ(dead.hands[1].model, 0xffff);
}

// 0x42a1b0 con el modo 0x80 (0x620b10): los dos calcos 0x61e con la luz en v y después en v · (1 − v); con otro modo
// (0x620a90), la luz por v. La luz es la que recibe: la del objeto (+0xf4) queda pisada (Q-C876).
TEST_F(KalimaSiegeHelperTest, TheCastleGlowMultipliesTheLightItReceives)
{
    const float lum = F32(RoundedSin(D(Clock) * D(0.0015f)) * D(0.3f) + D(0.7f));
    const float rest = F32((1.0 - D(lum)) * D(lum));

    ActorState& dark = Actor(0x19c);
    ASSERT_TRUE(Draw(30).HasValue());
    EXPECT_EQ(dark.light, glm::vec3(rest));
    ASSERT_FALSE(m_batch.Commands().empty());
    EXPECT_EQ(m_batch.Commands().front().state.blend, Client::Render::ModelBlend::Subtractive);
    EXPECT_EQ(m_batch.Commands().front().state.texture, std::optional{Client::Render::TextureSlot{0x61e}});
    EXPECT_FLOAT_EQ(m_batch.Vertices().front().color.red, lum);
    EXPECT_FLOAT_EQ(m_batch.Vertices().back().color.red, rest);
    EXPECT_EQ(m_batch.Vertices().size() % 2, 0u);
    const std::size_t half = m_batch.Vertices().size() / 2;
    EXPECT_FLOAT_EQ(m_batch.Vertices()[half - 1].color.green, lum);
    EXPECT_FLOAT_EQ(m_batch.Vertices()[half].color.green, rest);
    // Sobre el piso (0) más 5.
    EXPECT_EQ(m_batch.Vertices().front().position.z, 5.0f);

    SetUp();
    m_siege = true;
    ActorState& lit = Actor(0x19c);
    ASSERT_TRUE(Draw(30).HasValue());
    EXPECT_EQ(lit.light, glm::vec3(0.5f));
    EXPECT_EQ(m_batch.Commands().front().state.blend, Client::Render::ModelBlend::Additive);
    EXPECT_FLOAT_EQ(m_batch.Vertices().front().color.red, F32(D(lum) * D(0.3f)));
    EXPECT_FLOAT_EQ(m_batch.Vertices().front().color.green, F32(D(lum) * D(0.2f)));
    EXPECT_FLOAT_EQ(m_batch.Vertices().back().color.blue, 0.0f);
    EXPECT_EQ(m_random.State(), Seed);

    SetUp();
    m_heights.reset();
    Actor(0x19c);
    ExpectCut(Draw(30), "0x42a248", "0x5fb2f0");
}

// 0x42e095..0x42e184, el 0x1a8: con +0x93 en 5 o más, +0x68 late y el calco aditivo; si no, el 0x5ac 50 más arriba
// y el 0x5fd con rand() % 360.
TEST_F(KalimaSiegeHelperTest, TheCastleCrownGlowsOrSparkles)
{
    ActorState& crown = Actor(0x1a8);
    crown.byte93 = 5;
    ASSERT_TRUE(Draw(30).HasValue());
    EXPECT_EQ(crown.blendMeshLight, F32(RoundedSin(D(Clock) * D(0.001f)) * 0.3 + D(0.3f)));
    EXPECT_EQ(m_batch.Commands().front().state.blend, Client::Render::ModelBlend::Additive);
    EXPECT_TRUE(Sprites().empty());

    SetUp();
    Actor(0x1a8).byte93 = 4;
    ASSERT_TRUE(Draw(30).HasValue());
    const auto sprites = Sprites();
    ASSERT_EQ(sprites.size(), 2u);
    EXPECT_EQ(sprites[0].texture, 0x5ac);
    EXPECT_EQ(sprites[0].position, glm::vec3(1000.0f, 2000.0f, 150.0f));
    EXPECT_EQ(sprites[0].scale, 4.0f);
    EXPECT_EQ(sprites[0].light, glm::vec3(0.8f, 0.6f, 0.3f));
    EXPECT_EQ(sprites[1].texture, 0x5fd);
    EXPECT_EQ(sprites[1].rotation, 41.0f);
    EXPECT_EQ(m_random.State(), After(1));
    EXPECT_TRUE(m_batch.Vertices().empty());
}

// 0x42e185..0x42e2dc, el 0x1a1: con el asedio, +0x42c en 1, tres sprites (uno con rand() % 360) y el calco
// sustractivo con su luz; sin él, +0x42c en 0 y +0x58 en −2. 0x42e2dd, el 0x1fd: la z desde +0xd8 (100 más abajo sin
// el asedio) y +0x68 late.
TEST_F(KalimaSiegeHelperTest, TheCastleSwitchesFollowTheSiege)
{
    m_siege = true;
    ActorState& tower = Actor(0x1a1);
    ASSERT_TRUE(Draw(30).HasValue());
    EXPECT_EQ(tower.byte42c, 1);
    const auto sprites = Sprites();
    ASSERT_EQ(sprites.size(), 3u);
    EXPECT_EQ(sprites[0].position,
              glm::vec3(1000.0f, 2000.0f, F32((RoundedSin(D(Clock) * D(0.001f)) + 1.0) * 60.0 + 101.0)));
    EXPECT_EQ(sprites[1].scale, 1.5f);
    EXPECT_EQ(sprites[2].position, Bone(2));
    EXPECT_EQ(m_batch.Commands().front().state.blend, Client::Render::ModelBlend::Subtractive);
    EXPECT_NE(tower.light, glm::vec3(0.5f));
    EXPECT_EQ(m_random.State(), After(1));

    SetUp();
    ActorState& idle = Actor(0x1a1);
    ASSERT_TRUE(Draw(30).HasValue());
    EXPECT_EQ(idle.byte42c, 0);
    EXPECT_EQ(idle.dword58, -2);
    EXPECT_TRUE(Sprites().empty());

    for (const bool siege : {true, false})
    {
        SetUp();
        m_siege = siege;
        ActorState& wall = Actor(0x1fd);
        wall.floatD8 = 300.0f;
        ASSERT_TRUE(Draw(30).HasValue());
        EXPECT_EQ(wall.position.z, siege ? 300.0f : 200.0f);
        EXPECT_EQ(wall.byte42c, siege ? 1 : 0);
        EXPECT_EQ(wall.blendMeshLight, F32(RoundedSin(D(Clock) * D(0.001f)) * D(0.3f) + 0.5));
    }
}
} // namespace
