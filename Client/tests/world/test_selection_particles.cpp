#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Math/X87.h"
#include "Particles/IParticleTrigonometry.h"
#include "Particles/KalimaParticleKernels.h"
#include "Particles/ParticleOwner.h"
#include "Particles/Particles.h"
#include "Particles/SelectionParticleKernels.h"
#include "WorldObjects/WorldObjects.h"

#include <glm/ext/vector_float3.hpp>
#include <gtest/gtest.h>

#include <algorithm>
#include <bit>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

// The particles of the objects of map 54 (and 33) and of the selection's chosen one's aura: what 0x53fe70 writes
// after its prefix and what 0x5446e0 advances (06 § El mundo de la selección). The expected values repeat the ASM's
// formulas with another CrtRandom of the same seed.
namespace
{
using Client::Crt::CrtRandom;
using Client::Particles::Particle;
using Client::Particles::ParticleError;
using Client::Particles::Particles;

constexpr std::int32_t SelectionScene = 4;
constexpr std::int32_t GameScene = 5;
constexpr double Hundredth = static_cast<double>(std::bit_cast<float>(0x3c23d70au));
constexpr double Twentieth = static_cast<double>(std::bit_cast<float>(0x3d4ccccdu));
constexpr double Tenth = static_cast<double>(std::bit_cast<float>(0x3dcccccdu));
constexpr double Fiftieth = static_cast<double>(std::bit_cast<float>(0x3ca3d70au));

using Client::Math::F32;

const Particle& Create(Particles& particles, std::int32_t type, std::int32_t subtype, float scale, CrtRandom& random,
                       std::int32_t scene = SelectionScene, Client::Particles::ParticleOwner owner = {})
{
    const auto created =
        particles.CreateOwnedChecked(type, {1000.0f, 2000.0f, 300.0f}, {0.0f, 0.0f, 45.0f}, {0.5f, 0.25f, 0.125f},
                                     subtype, scale, 0.0f, random, scene, owner);
    EXPECT_TRUE(created.HasValue());
    EXPECT_TRUE(created.HasValue() && created.Value().has_value());
    return particles.All()[created.HasValue() && created.Value() ? *created.Value() : 0];
}

// The sine of 0x547723 times the 20 of 0x6b86d0: records the argument and returns a fixed product.
class RecordingSine final : public Client::Particles::IParticleTrigonometry
{
public:
    mutable std::vector<double> arguments;
    double product = 3.0;
    Client::Core::Expected<double, ParticleError> SineProductPC53(double argument, float multiplier) const override
    {
        arguments.push_back(argument);
        EXPECT_EQ(multiplier, 20.0f);
        return product;
    }
    Client::Core::Expected<double, ParticleError> CosineProductPC53(double, float) const override
    {
        ADD_FAILURE() << "622/1 no usa el coseno";
        return Client::Core::Unexpected(ParticleError::UnrepresentableTrigonometry);
    }
};

// A 0x5446e0 step with the world clock and the objects of `objects` as owners.
Client::Core::Expected<void, ParticleError> Step(Particles& particles, CrtRandom& random, float clock,
                                                 RecordingSine& sine,
                                                 std::vector<Client::WorldObjects::WorldObject>* objects = nullptr)
{
    const std::int32_t map = 54;
    Client::Particles::ParticleObjectOwners owners;
    if (objects != nullptr)
    {
        owners = [objects](std::uint64_t identity) -> Client::WorldObjects::WorldObject*
        {
            for (auto& object : *objects)
            {
                if (object.identity == identity)
                    return &object;
            }
            return nullptr;
        };
    }
    return Client::Particles::StepParticlesChecked(particles, random, std::cref(clock), &map, sine, {}, owners);
}

// 0x543851..0x54389c: 729/1 is drawn with Impack03 (0x719), with rolled scale and rise.
TEST(SelectionParticlesTest, TheChaseLightRisingSubtypeDrawsWithImpack03AndSixRandomDraws)
{
    auto particles = std::make_unique<Particles>();
    CrtRandom random(7);
    CrtRandom draws(7);
    const Particle& p = Create(*particles, Client::Particles::ChaseLightParticle, 1, 1.0f, random);
    const int scale = draws.Next() % 10 + 20;
    const int rise = draws.Next() % 10 + 20;
    const int life = draws.Next() % 3 + 20;
    const int x = draws.Next() % 50 - 25;
    const int y = draws.Next() % 50 - 25;
    const int z = draws.Next() % 200 - 100;
    EXPECT_EQ(p.type, 0x729);
    EXPECT_EQ(p.texture, 0x719);
    EXPECT_EQ(p.scale, F32(scale * Hundredth));
    EXPECT_EQ(p.verticalSpeed, F32(rise * Twentieth));
    EXPECT_EQ(p.life, life);
    EXPECT_EQ(p.position, glm::vec3(F32(x + 1000.0), F32(y + 2000.0), F32((z + 300.0) + 250.0)));
    EXPECT_EQ(p.light, glm::vec3(0.5f, 0.25f, 0.125f));
    EXPECT_EQ(random.State(), draws.State());
}

// 0x54389e..0x5438c1: 729/0 is the wide light, still, with its own texture.
TEST(SelectionParticlesTest, TheWideChaseLightStaysStillAndDrawsFiveTimes)
{
    auto particles = std::make_unique<Particles>();
    CrtRandom random(11);
    CrtRandom draws(11);
    const Particle& p = Create(*particles, Client::Particles::ChaseLightParticle, 0, 1.0f, random);
    const int scale = draws.Next() % 5 + 20;
    const int life = draws.Next() % 3 + 20;
    const int x = draws.Next() % 50 - 25;
    const int y = draws.Next() % 50 - 25;
    const int z = draws.Next() % 200 - 100;
    EXPECT_EQ(p.texture, 0x729);
    EXPECT_EQ(p.scale, F32(scale * Tenth));
    EXPECT_EQ(p.verticalSpeed, 0.0f);
    EXPECT_EQ(p.life, life);
    EXPECT_EQ(p.position, glm::vec3(F32(x + 1000.0), F32(y + 2000.0), F32((z + 300.0) + 250.0)));
    EXPECT_EQ(random.State(), draws.State());
}

// 0x547880..0x5478cf: with life 10 or more (already decremented) the light rises 1.16; below, it falls 0.862.
TEST(SelectionParticlesTest, TheChaseLightBrightensUntilTenStepsAreLeftAndThenFades)
{
    for (const int life : {11, 10})
    {
        auto particles = std::make_unique<Particles>();
        Particle& p = particles->All()[0];
        p.alive = true;
        p.type = p.texture = Client::Particles::ChaseLightParticle;
        p.subtype = 1;
        p.life = life;
        p.light = glm::vec3(0.5f);
        p.position = {0.0f, 0.0f, 100.0f};
        p.verticalSpeed = 1.25f;
        CrtRandom random(3);
        RecordingSine sine;
        ASSERT_TRUE(Step(*particles, random, 0.0f, sine).HasValue());
        const double factor = life == 11 ? static_cast<double>(std::bit_cast<float>(0x3f947ae1u))
                                         : static_cast<double>(std::bit_cast<float>(0x3f5cb08eu));
        EXPECT_EQ(p.light, glm::vec3(F32(0.5 * factor)));
        EXPECT_EQ(p.position.z, 101.25f);
        EXPECT_TRUE(p.alive);
    }
}

// 0x54150e..0x5415fb: life 33, the scale drops, the turn, the side, the rise and the y drift (with seven of ten); y
// follows the x shift.
TEST(SelectionParticlesTest, TheRisingFlameDrawsItsDriftOnlyWhenTheSeventhOfTenComesOut)
{
    std::vector<bool> drifted;
    for (const std::uint32_t seed : {1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u})
    {
        auto particles = std::make_unique<Particles>();
        CrtRandom random(seed);
        CrtRandom draws(seed);
        const Particle& p = Create(*particles, Client::Particles::FlameParticle, 7, 1.5f, random);
        const float scale = F32(1.5 - (draws.Next() % 20) * Hundredth);
        const int turn = draws.Next() % 360;
        const int side = draws.Next() & 1;
        const float x = F32(side * 0.5 * scale + 1000.0);
        const float rise = F32(((draws.Next() % 100) * Hundredth + static_cast<double>(1.8f)) * scale);
        float drift = 0.0f;
        const bool drifts = draws.Next() % 10 >= 3;
        if (drifts)
            drift = F32(((draws.Next() % 20) * Tenth - 1.0) * scale);
        drifted.push_back(drifts);
        EXPECT_EQ(p.life, 33);
        EXPECT_EQ(p.scale, scale);
        EXPECT_EQ(p.rotation, static_cast<float>(turn - 180));
        EXPECT_EQ(p.start.x, 1000.0f);
        EXPECT_EQ(p.position.x, x);
        EXPECT_EQ(p.position.y, F32((static_cast<double>(x) - 1000.0) + 2000.0));
        EXPECT_EQ(p.verticalSpeed, rise);
        EXPECT_EQ(p.velocity, glm::vec3(0.0f, drift, 0.0f));
        EXPECT_EQ(random.State(), draws.State());
    }
    EXPECT_NE(std::find(drifted.begin(), drifted.end(), true), drifted.end());
    EXPECT_NE(std::find(drifted.begin(), drifted.end(), false), drifted.end());
}

// 0x54143c..0x54149d: Lost Tower's jet flame 5de/0 lives 20, rises straight ((& 127) + 128) · 0.15f and its scale is
// ((& 63) + 64) times the argument times 0.01f: two rand(). 0x545d03: its step is only the common one, which touches
// neither the scale, the rise nor the turn.
TEST(SelectionParticlesTest, TheLostTowerFloorFlameRisesStraightWithTwoDraws)
{
    auto particles = std::make_unique<Particles>();
    CrtRandom random(5);
    CrtRandom draws(5);
    const auto created = particles->CreateChecked(Client::Particles::FlameParticle, {1000.0f, 2000.0f, 300.0f},
                                                  {0.0f, 0.0f, 45.0f}, glm::vec3(1.0f), 0, 1.5f, 0.0f, random);
    ASSERT_TRUE(created.HasValue());
    ASSERT_TRUE(created.Value().has_value());
    const Particle& p = particles->All()[*created.Value()];
    const float rise = F32(((draws.Next() & 127) + 128) * static_cast<double>(std::bit_cast<float>(0x3e19999au)));
    const float scale = F32(((draws.Next() & 63) + 64) * 1.5 * Hundredth);
    EXPECT_EQ(p.subtype, 0);
    EXPECT_EQ(p.life, 20);
    EXPECT_EQ(p.velocity, glm::vec3(0.0f, 0.0f, rise));
    EXPECT_EQ(p.scale, scale);
    EXPECT_EQ(p.position, glm::vec3(1000.0f, 2000.0f, 300.0f));
    EXPECT_EQ(random.State(), draws.State());

    const float rotation = p.rotation;
    CrtRandom stepRandom(3);
    RecordingSine sine;
    ASSERT_TRUE(Step(*particles, stepRandom, 0.0f, sine).HasValue());
    EXPECT_EQ(p.scale, scale);
    EXPECT_EQ(p.velocity, glm::vec3(0.0f, 0.0f, rise));
    EXPECT_EQ(p.rotation, rotation);
    EXPECT_EQ(p.life, 19);
    EXPECT_TRUE(sine.arguments.empty());
}

// 0x545e3f..0x545ea9: with the start left of x (fcomp's C0) it turns +2 and otherwise −2; it rises, shrinks with the
// rise and goes off with the x87 scale at 0 or less; the light falls 0.993.
TEST(SelectionParticlesTest, TheRisingFlameTurnsBySideOfItsStartAndDiesWhenItsScaleIsGone)
{
    for (const float startX : {-5.0f, 5.0f})
    {
        auto particles = std::make_unique<Particles>();
        Particle& p = particles->All()[0];
        p.alive = true;
        p.type = p.texture = Client::Particles::FlameParticle;
        p.subtype = 7;
        p.life = 20;
        p.moves = false;
        p.start.x = startX;
        p.position = {0.0f, 0.0f, 10.0f};
        p.rotation = 30.0f;
        p.verticalSpeed = 2.0f;
        p.scale = 1.0f;
        p.light = glm::vec3(1.0f);
        CrtRandom random(3);
        RecordingSine sine;
        ASSERT_TRUE(Step(*particles, random, 0.0f, sine).HasValue());
        EXPECT_EQ(p.rotation, startX < 0.0f ? 32.0f : 28.0f) << startX;
        EXPECT_EQ(p.position.z, 12.0f);
        EXPECT_EQ(p.scale, F32(1.0 - 2.0 * static_cast<double>(std::bit_cast<float>(0x3c2c7692u))));
        EXPECT_EQ(p.light, glm::vec3(F32(static_cast<double>(std::bit_cast<float>(0x3f7e3870u)))));
        EXPECT_TRUE(p.alive);
    }
    auto particles = std::make_unique<Particles>();
    Particle& p = particles->All()[0];
    p.alive = true;
    p.type = p.texture = Client::Particles::FlameParticle;
    p.subtype = 7;
    p.life = 20;
    p.moves = false;
    p.verticalSpeed = 95.0f;
    p.scale = 1.0f;
    CrtRandom random(3);
    RecordingSine sine;
    ASSERT_TRUE(Step(*particles, random, 0.0f, sine).HasValue());
    EXPECT_LE(p.scale, 0.0f);
    EXPECT_FALSE(p.alive);
}

// 0x5435cf..0x54363b: 622/1 lives 30, rolls its phase, place, scale and turn (seven rand()) and adds the scale
// argument to the turn; the velocity stays at 0.
TEST(SelectionParticlesTest, TheOwnedMistDrawsSevenTimesAndAddsTheScaleArgumentToItsTurn)
{
    auto particles = std::make_unique<Particles>();
    CrtRandom random(5);
    CrtRandom draws(5);
    const Client::Particles::ParticleOwner owner{Client::Particles::ParticleOwnerKind::WorldObject, 77};
    const Particle& p = Create(*particles, Client::Particles::CloudParticle, 1, 0.75f, random, SelectionScene, owner);
    const int phase = draws.Next() % 1000;
    const int x = draws.Next() % 500 - 250;
    const int y = draws.Next() % 500 - 250;
    const int z = draws.Next() % 20 + 20;
    const int start = draws.Next() % 100;
    const int scale = draws.Next() % 20 + 180;
    const int turn = draws.Next() % 30;
    EXPECT_EQ(p.life, 30);
    EXPECT_EQ(p.verticalSpeed, static_cast<float>(phase));
    EXPECT_EQ(p.position, glm::vec3(F32(x + 1000.0), F32(y + 2000.0), F32(z + 300.0)));
    EXPECT_EQ(p.start.z, p.position.z);
    EXPECT_EQ(p.start.y, F32(start * Hundredth));
    EXPECT_EQ(p.scale, F32(scale * Hundredth));
    EXPECT_EQ(p.rotationFactor, F32(turn * Hundredth + 0.75));
    EXPECT_EQ(p.velocity, glm::vec3(0.0f));
    EXPECT_EQ(p.typedOwner, owner);
    EXPECT_EQ(random.State(), draws.State());
}

// 0x5476f0..0x547757: the seen owner renews the life to 50; the height is the clock's sine times 20 over the start's,
// and the turn, the clock times the factor times 0.02 plus the phase.
TEST(SelectionParticlesTest, TheOwnedMistFloatsWithTheWorldClockWhileItsOwnerIsSeen)
{
    std::vector<Client::WorldObjects::WorldObject> objects(1);
    objects[0].identity = 77;
    objects[0].seen = true;
    objects[0].state.hiddenMesh = Client::WorldObjects::HiddenObject;
    auto particles = std::make_unique<Particles>();
    Particle& p = particles->All()[0];
    p.alive = true;
    p.type = p.texture = Client::Particles::CloudParticle;
    p.subtype = 1;
    p.life = 2;
    p.moves = false;
    p.typedOwner = {Client::Particles::ParticleOwnerKind::WorldObject, 77};
    p.verticalSpeed = 500.0f;
    p.start = {0.0f, 0.25f, 310.0f};
    p.rotationFactor = 1.5f;
    CrtRandom random(3);
    RecordingSine sine;
    const float clock = 1234.0f;
    ASSERT_TRUE(Step(*particles, random, clock, sine, &objects).HasValue());
    EXPECT_EQ(p.life, 50);
    EXPECT_TRUE(p.alive);
    EXPECT_EQ(objects[0].state.hiddenMesh, Client::WorldObjects::HiddenObject);
    ASSERT_EQ(sine.arguments.size(), 1u);
    EXPECT_EQ(sine.arguments[0], (1234.0 + 500.0) * static_cast<double>(std::bit_cast<float>(0x3951b717u)));
    EXPECT_EQ(p.position.z, F32(3.0 + 310.0));
    EXPECT_EQ(p.rotation, F32((1234.0 * 1.5) * Fiftieth + 0.25));
}

// 0x547703..0x547711: unseen and without life, the mist goes off and its owner emits again (+0x58 at 0); it still
// finishes its height and turn.
TEST(SelectionParticlesTest, TheOwnedMistLetsItsOwnerEmitAgainWhenItDiesUnseen)
{
    std::vector<Client::WorldObjects::WorldObject> objects(1);
    objects[0].identity = 77;
    objects[0].seen = false;
    objects[0].state.hiddenMesh = Client::WorldObjects::HiddenObject;
    auto particles = std::make_unique<Particles>();
    Particle& p = particles->All()[0];
    p.alive = true;
    p.type = p.texture = Client::Particles::CloudParticle;
    p.subtype = 1;
    p.life = 1;
    p.moves = false;
    p.typedOwner = {Client::Particles::ParticleOwnerKind::WorldObject, 77};
    CrtRandom random(3);
    RecordingSine sine;
    ASSERT_TRUE(Step(*particles, random, 10.0f, sine, &objects).HasValue());
    EXPECT_FALSE(p.alive);
    EXPECT_EQ(p.life, 0);
    EXPECT_EQ(objects[0].state.hiddenMesh, 0);
    EXPECT_EQ(sine.arguments.size(), 1u);
}

// 0x54327b..0x5435cf: Icarus's clouds 622 from 0 to 5 share /1's creation (seven rand()); in the step, table 0x54848c
// turns 1 and 4 forward, 2 and 5 backward, and leaves 0's and 3's turn alone.
TEST(SelectionParticlesTest, TheIcarusCloudsShareTheMistAndTurnBySubtype)
{
    for (std::int32_t subtype = 0; subtype <= 5; ++subtype)
    {
        SCOPED_TRACE(subtype);
        std::vector<Client::WorldObjects::WorldObject> objects(1);
        objects[0].identity = 77;
        objects[0].seen = true;
        auto particles = std::make_unique<Particles>();
        CrtRandom random(5);
        const Client::Particles::ParticleOwner owner{Client::Particles::ParticleOwnerKind::WorldObject, 77};
        Particle& p = const_cast<Particle&>(
            Create(*particles, Client::Particles::CloudParticle, subtype, 0.75f, random, GameScene, owner));
        EXPECT_EQ(p.life, 30);
        CrtRandom draws(5);
        for (int draw = 0; draw < 7; ++draw)
            static_cast<void>(draws.Next());
        EXPECT_EQ(random.State(), draws.State());
        p.rotation = 9.0f;
        p.start.y = 0.25f;
        p.rotationFactor = 1.5f;
        RecordingSine sine;
        ASSERT_TRUE(Step(*particles, random, 1234.0f, sine, &objects).HasValue());
        const double turned = (1234.0 * 1.5) * Fiftieth;
        if (subtype == 1 || subtype == 4)
            EXPECT_EQ(p.rotation, F32(turned + 0.25));
        else if (subtype == 2 || subtype == 5)
            EXPECT_EQ(p.rotation, F32(-turned + 0.25));
        else
            EXPECT_EQ(p.rotation, 9.0f);
    }
}

// Without the owner object among the map's, the step cuts with MissingOwner before writing the particle.
TEST(SelectionParticlesTest, TheOwnedMistNeedsItsOwnerAmongTheMapObjects)
{
    std::vector<Client::WorldObjects::WorldObject> objects(1);
    objects[0].identity = 5;
    auto particles = std::make_unique<Particles>();
    Particle& p = particles->All()[0];
    p.alive = true;
    p.type = p.texture = Client::Particles::CloudParticle;
    p.subtype = 1;
    p.life = 4;
    p.moves = false;
    p.typedOwner = {Client::Particles::ParticleOwnerKind::WorldObject, 77};
    p.position.z = 9.0f;
    CrtRandom random(3);
    RecordingSine sine;
    const auto stepped = Step(*particles, random, 10.0f, sine, &objects);
    ASSERT_FALSE(stepped.HasValue());
    EXPECT_EQ(stepped.Error(), ParticleError::MissingOwner);
    EXPECT_EQ(p.life, 3);
    EXPECT_EQ(p.position.z, 9.0f);
    EXPECT_TRUE(sine.arguments.empty());
}

// 0x543f04 of 6f0: the objects' /2 adds 1.6 (0x6b8bfc) to the argument's scale.
TEST(SelectionParticlesTest, TheObjectWaterfallAddsItsScaleArgument)
{
    auto particles = std::make_unique<Particles>();
    CrtRandom random(9);
    CrtRandom draws(9);
    const Particle& p = Create(*particles, Client::Particles::KalimaWaterfallFirst, 2, 0.5f, random, GameScene);
    const int turn = draws.Next() % 360;
    const int fall = draws.Next() % 5;
    EXPECT_EQ(p.life, 30);
    EXPECT_EQ(p.rotation, static_cast<float>(turn));
    EXPECT_EQ(p.velocity.z, static_cast<float>(-7 - fall));
    EXPECT_EQ(p.scale, F32(0.5 + static_cast<double>(1.6f)));
    EXPECT_EQ(p.light, glm::vec3(0.2f));
    EXPECT_EQ(random.State(), draws.State());
}

// 0x543f49..0x543fae of 6f1: /2 lives 40 and adds the argument to its scale; in the selection, light 0.25.
TEST(SelectionParticlesTest, TheObjectWaterfallMistLivesFortyAndAddsItsScaleArgument)
{
    auto particles = std::make_unique<Particles>();
    CrtRandom random(13);
    CrtRandom draws(13);
    const Particle& p = Create(*particles, Client::Particles::KalimaWaterfallMist, 2, 0.5f, random, SelectionScene);
    const int turn = draws.Next() % 360;
    const int size = draws.Next() % 6 + 6;
    const int fall = draws.Next() % 3;
    const int x = draws.Next() % 20 - 10;
    const int y = draws.Next() % 20 - 10;
    const int z = draws.Next() % 40 - 20;
    EXPECT_EQ(p.life, 40);
    EXPECT_EQ(p.rotation, static_cast<float>(turn));
    EXPECT_EQ(p.scale, F32(size * Tenth + 0.5));
    EXPECT_EQ(p.velocity.z, static_cast<float>(-3 - fall));
    EXPECT_EQ(p.light, glm::vec3(0.25f));
    EXPECT_EQ(p.position, glm::vec3(F32(x + 1000.0), F32(y + 2000.0), F32(z + 300.0)));
    EXPECT_EQ(random.State(), draws.State());
}

// 0x54417f..0x544149 of 6f2: /4 rolls turn and scale, then lives 20, rises and rolls the scale again with the
// argument added.
TEST(SelectionParticlesTest, TheObjectSplashDrawsItsScaleTwice)
{
    auto particles = std::make_unique<Particles>();
    CrtRandom random(17);
    CrtRandom draws(17);
    const Particle& p = Create(*particles, Client::Particles::WaterfallSplashFirst, 4, 0.5f, random, GameScene);
    const int turn = draws.Next() % 360;
    static_cast<void>(draws.Next());
    const int rise = draws.Next() % 5 + 2;
    const int size = draws.Next() % 10 + 10;
    const int x = draws.Next() % 40 - 20;
    const int y = draws.Next() % 40 - 20;
    const int z = draws.Next() % 20 - 10;
    EXPECT_EQ(p.life, 20);
    EXPECT_EQ(p.rotation, static_cast<float>(turn));
    EXPECT_EQ(p.velocity.z, static_cast<float>(rise));
    EXPECT_EQ(p.scale, F32(size * static_cast<double>(0.02f) + 0.5));
    EXPECT_EQ(p.position, glm::vec3(F32(x + 1000.0), F32(y + 2000.0), F32(z + 300.0)));
    EXPECT_EQ(random.State(), draws.State());
}

TEST(SelectionParticlesTest, OwnedCreationRejectsOtherFamilies)
{
    auto particles = std::make_unique<Particles>();
    CrtRandom random(1);
    const auto created = particles->CreateOwnedChecked(Client::Particles::CloudParticle, {}, {}, {}, 6, 1.0f, 0.0f,
                                                       random, SelectionScene);
    ASSERT_FALSE(created.HasValue());
    EXPECT_EQ(created.Error(), ParticleError::UnsupportedSubtype);
    EXPECT_FALSE(particles->All()[0].alive);
    EXPECT_EQ(random.State(), 1u);
}
} // namespace
