#include "Crt/CrtRandom.h"
#include "Models/ObjectState.h"
#include "MapScripts/Dungeon/DungeonObjectStep.h"
#include "Effects/ModelEffects.h"
#include "WorldObjects/ObjectModelStepState.h"
#include "Particles/Particles.h"
#include "Terrain/TerrainLighting.h"
#include "WorldObjects/WorldObjectStepServices.h"
#include "WorldObjects/WorldObjects.h"
#include "Audio/SoundId.h"

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <bit>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <vector>

// The cases of 0x604ab0 of the Dungeon (map 1, table 0x605bc0), of Lost Tower (map 4, 0x605c50 with type − 3) and
// of the Arena (map 6, 0x6054fd), and the kick of 0x52f550 (06 § Los objetos del Dungeon y de Lost Tower, § Los
// objetos de la Arena).
namespace
{
using Client::Crt::CrtRandom;
using Client::Effects::ModelEffects;
using Client::MapScripts::Dungeon::KickObject;
using Client::MapScripts::Dungeon::StepArenaObject;
using Client::MapScripts::Dungeon::StepDungeonObject;
using Client::MapScripts::Dungeon::StepLostTowerObject;
using Client::Particles::Particles;
using Client::Terrain::TerrainLighting;
using Client::WorldObjects::ObjectModelStepState;
using Client::WorldObjects::ObjectStepGlobals;
using Client::WorldObjects::WorldObject;

WorldObject Object(std::int16_t type)
{
    WorldObject object;
    object.type = type;
    object.transform.position = {1000.0f, 1500.0f, 20.0f};
    object.transform.angles = {0.0f, 0.0f, 0.0f};
    object.light = {0.25f, 0.5f, 0.75f};
    return object;
}

CrtRandom After(std::uint32_t seed, int draws)
{
    CrtRandom random(seed);
    for (int draw = 0; draw < draws; ++draw)
        static_cast<void>(random.Next());
    return random;
}

// The first seed whose first rand() meets (or does not meet) the condition.
std::uint32_t SeedWhere(bool (*condition)(int))
{
    for (std::uint32_t seed = 1;; ++seed)
    {
        CrtRandom random(seed);
        if (condition(random.Next()))
            return seed;
    }
}

// 0x604ff1..0x60502b: the type's model+0x88 at 1 and the V at the signed remainder of ftol(clock) by 1000 times
// −0.001f (0x6ba1a4). Without the models' state it cuts before writing.
TEST(DungeonObjectsTest, TheMapOneWaterfallsMarkTheirModelAndScrollTheirV)
{
    for (const std::int16_t type : {std::int16_t{0x16}, std::int16_t{0x17}, std::int16_t{0x18}})
    {
        SCOPED_TRACE(type);
        ObjectModelStepState models;
        ObjectStepGlobals globals;
        globals.worldClock = 12345.0f;
        globals.modelState = &models;
        auto object = Object(type);
        CrtRandom random(7);
        ASSERT_TRUE(StepDungeonObject(object, random, globals).HasValue());
        EXPECT_EQ(models.unlitMeshes.at(type), 1);
        EXPECT_EQ(object.state.uv.y,
                  static_cast<float>(345.0 * static_cast<double>(std::bit_cast<float>(0xba83126fu))));
        EXPECT_EQ(object.state.uv.x, 0.0f);
        EXPECT_EQ(random.State(), 7u);

        globals.worldClock = -2500.0f;
        ASSERT_TRUE(StepDungeonObject(object, random, globals).HasValue());
        EXPECT_EQ(object.state.uv.y,
                  static_cast<float>(-500.0 * static_cast<double>(std::bit_cast<float>(0xba83126fu))))
            << "el resto firmado, sin negar el ftol";

        globals.modelState = nullptr;
        auto bare = Object(type);
        const auto cut = StepDungeonObject(bare, random, globals);
        ASSERT_FALSE(cut.HasValue());
        EXPECT_NE(cut.Error().find("0x605003"), std::string::npos) << cut.Error();
        EXPECT_EQ(bare.state.uv.y, 0.0f);
    }
}

// 0x6059dd (through map 1's table): the 0x27, the 0x28 and the 0x33 leave +0x58 at −2 with no rand().
TEST(DungeonObjectsTest, TheMapOneHiddenTypesOnlyStoreTheirHiddenMesh)
{
    for (const std::int16_t type : {std::int16_t{0x27}, std::int16_t{0x28}, std::int16_t{0x33}})
    {
        SCOPED_TRACE(type);
        auto object = Object(type);
        CrtRandom random(7);
        ASSERT_TRUE(StepDungeonObject(object, random, ObjectStepGlobals{}).HasValue());
        EXPECT_EQ(object.state.hiddenMesh, Client::WorldObjects::HiddenObject);
        EXPECT_EQ(random.State(), 7u);
    }
    auto other = Object(0x30);
    CrtRandom random(7);
    ASSERT_TRUE(StepDungeonObject(other, random, ObjectStepGlobals{}).HasValue());
    EXPECT_EQ(other.state.hiddenMesh, Client::Models::ObjectState{}.hiddenMesh);
}

// 0x605034 and 0x605052: 0x52f360(0, object, (0; −30; 240)) and (0; 0; 190), the class 0 fires. Without the
// particle pool it cuts with the call's address.
TEST(DungeonObjectsTest, TheMapOneFiresAreClassZeroAtTheirNativeOffsets)
{
    struct Fire
    {
        std::int16_t type;
        glm::vec3 offset;
        const char* address;
    };
    for (const Fire& fire :
         {Fire{0x29, {0.0f, -30.0f, 240.0f}, "0x605043"}, Fire{0x2a, {0.0f, 0.0f, 190.0f}, "0x60505e"}})
    {
        SCOPED_TRACE(fire.type);
        auto lighting = std::make_unique<TerrainLighting>();
        auto particles = std::make_unique<Particles>();
        ObjectStepGlobals globals;
        globals.dynamicLighting = lighting.get();
        globals.particles = particles.get();
        auto object = Object(fire.type);
        CrtRandom random(5);
        ASSERT_TRUE(StepDungeonObject(object, random, globals).HasValue());

        auto expectedLighting = std::make_unique<TerrainLighting>();
        auto expectedParticles = std::make_unique<Particles>();
        ObjectStepGlobals direct;
        direct.dynamicLighting = expectedLighting.get();
        direct.particles = expectedParticles.get();
        CrtRandom expected(5);
        ASSERT_TRUE(
            Client::WorldObjects::EmitObjectFire(0, Object(fire.type), fire.offset, expected, direct).HasValue());
        EXPECT_EQ(random.State(), expected.State());
        EXPECT_EQ(lighting->cells, expectedLighting->cells);
        ASSERT_TRUE(particles->All().front().alive);
        EXPECT_EQ(particles->All().front().position, expectedParticles->All().front().position);

        ObjectStepGlobals missing;
        CrtRandom again(5);
        const auto cut = StepDungeonObject(object, again, missing);
        ASSERT_FALSE(cut.HasValue());
        EXPECT_NE(cut.Error().find(fire.address), std::string::npos) << cut.Error();
    }
}

// 0x604fb4..0x604fe3: with rand() % 3 at 0, 0x518cf0(0xda, position, angles, light +0xf4); always after, +0x58 at
// −2. Without the effect table, the rand() that asks for it cuts before hiding.
TEST(DungeonObjectsTest, TheMapOneDripDropsAStoneOneTimeInThreeAndAlwaysHides)
{
    const std::uint32_t drops = SeedWhere([](int value) { return value % 3 == 0; });
    const std::uint32_t stays = SeedWhere([](int value) { return value % 3 != 0; });
    {
        ModelEffects effects;
        ObjectStepGlobals globals;
        globals.effects = &effects;
        auto object = Object(0x34);
        CrtRandom random(drops);
        ASSERT_TRUE(StepDungeonObject(object, random, globals).HasValue());
        EXPECT_EQ(object.state.hiddenMesh, Client::WorldObjects::HiddenObject);
        ASSERT_TRUE(effects.Effects()[0].alive);
        EXPECT_EQ(effects.Effects()[0].type, Client::Effects::FallingStoneEffect);
        EXPECT_EQ(effects.Effects()[0].light, object.light);
        EXPECT_FALSE(effects.Effects()[1].alive);
        EXPECT_EQ(random.State(), After(drops, 7).State()) << "el % 3 y los seis de 0x51b80a";
    }
    {
        ModelEffects effects;
        ObjectStepGlobals globals;
        globals.effects = &effects;
        auto object = Object(0x34);
        CrtRandom random(stays);
        ASSERT_TRUE(StepDungeonObject(object, random, globals).HasValue());
        EXPECT_EQ(object.state.hiddenMesh, Client::WorldObjects::HiddenObject);
        EXPECT_FALSE(effects.Effects()[0].alive);
        EXPECT_EQ(random.State(), After(stays, 1).State());
        CrtRandom bare(stays);
        ASSERT_TRUE(StepDungeonObject(object, bare, ObjectStepGlobals{}).HasValue())
            << "sin piedra no hace falta la tabla";
    }
    auto object = Object(0x34);
    CrtRandom random(drops);
    const auto cut = StepDungeonObject(object, random, ObjectStepGlobals{});
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x604fdb"), std::string::npos) << cut.Error();
    EXPECT_EQ(object.state.hiddenMesh, Client::Models::ObjectState{}.hiddenMesh);
    EXPECT_EQ(random.State(), After(drops, 1).State());
}

// 0x605404 (the 3 and the 4) and 0x60542f (the 0x13 and the 0x14, with +0x64 at 4): the U at −ftol(clock) negated
// in 32 bits, its signed remainder of 1000 and 0.001f (0x6b89c8). 0x605461: the 0x12 and the 0x17 take +0x64 to 1.
TEST(DungeonObjectsTest, TheLostTowerWaterScrollsItsUAndTheChromeTypesBlend)
{
    for (const std::int16_t type : {std::int16_t{3}, std::int16_t{4}, std::int16_t{0x13}, std::int16_t{0x14}})
    {
        SCOPED_TRACE(type);
        ObjectStepGlobals globals;
        globals.worldClock = 12345.0f;
        auto object = Object(type);
        CrtRandom random(7);
        ASSERT_TRUE(StepLostTowerObject(object, random, globals).HasValue());
        EXPECT_EQ(object.state.uv.x,
                  static_cast<float>(-345.0 * static_cast<double>(std::bit_cast<float>(0x3a83126fu))));
        EXPECT_EQ(object.state.blendMesh, type >= 0x13 ? 4 : Client::Models::ObjectState{}.blendMesh);
        EXPECT_EQ(random.State(), 7u);
    }
    for (const std::int16_t type : {std::int16_t{0x12}, std::int16_t{0x17}})
    {
        auto object = Object(type);
        CrtRandom random(7);
        ASSERT_TRUE(StepLostTowerObject(object, random, ObjectStepGlobals{}).HasValue());
        EXPECT_EQ(object.state.blendMesh, 1);
        EXPECT_EQ(object.state.uv.x, 0.0f);
    }
    // ftol(-2147483648) = 0x80000000: the neg leaves it unchanged and the remainder is −648.
    ObjectStepGlobals wrap;
    wrap.worldClock = -2147483648.0f;
    auto object = Object(3);
    CrtRandom random(7);
    ASSERT_TRUE(StepLostTowerObject(object, random, wrap).HasValue());
    EXPECT_EQ(object.state.uv.x, static_cast<float>(-648.0 * static_cast<double>(std::bit_cast<float>(0x3a83126fu))));
    // NaN gives 0 (OriginalFtolLow); infinity is not credited and cuts.
    wrap.worldClock = std::numeric_limits<float>::quiet_NaN();
    ASSERT_TRUE(StepLostTowerObject(object, random, wrap).HasValue());
    EXPECT_EQ(object.state.uv.x, 0.0f);
    wrap.worldClock = std::numeric_limits<float>::infinity();
    const auto cut = StepLostTowerObject(object, random, wrap);
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x60540a"), std::string::npos) << cut.Error();
}

// 0x6054fd..0x605546: the Arena's 0x15 takes the blendMesh to 3 before the ftol and scrolls the V with the negated
// ftol; the 0x26 only hides; the other types change nothing. None draws a rand().
TEST(DungeonObjectsTest, TheArenaScrollsTheVOfItsWaterAndHidesItsMarkers)
{
    ObjectStepGlobals globals;
    globals.worldClock = 12345.0f;
    auto water = Object(0x15);
    ASSERT_TRUE(StepArenaObject(water, globals).HasValue());
    EXPECT_EQ(water.state.blendMesh, 3);
    EXPECT_EQ(water.state.uv.y, static_cast<float>(-345.0 * static_cast<double>(std::bit_cast<float>(0x3a83126fu))));
    EXPECT_EQ(water.state.uv.x, 0.0f);
    EXPECT_EQ(water.state.hiddenMesh, WorldObject{}.state.hiddenMesh);

    auto marker = Object(0x26);
    ASSERT_TRUE(StepArenaObject(marker, globals).HasValue());
    EXPECT_EQ(marker.state.hiddenMesh, -2);
    EXPECT_EQ(marker.state.uv, glm::vec2(0.0f));

    for (const std::int16_t type : {std::int16_t{9}, std::int16_t{0x14}, std::int16_t{0x16}})
    {
        SCOPED_TRACE(type);
        auto other = Object(type);
        ASSERT_TRUE(StepArenaObject(other, globals).HasValue());
        EXPECT_EQ(other.state.blendMesh, WorldObject{}.state.blendMesh);
        EXPECT_EQ(other.state.hiddenMesh, WorldObject{}.state.hiddenMesh);
        EXPECT_EQ(other.state.uv, glm::vec2(0.0f));
    }

    auto stepped = Object(0x15);
    CrtRandom random(7);
    ASSERT_TRUE(Client::WorldObjects::StepWorldObjectChecked(stepped, 6, nullptr, random, globals).HasValue());
    EXPECT_EQ(stepped.state.uv.y, water.state.uv.y);
    EXPECT_EQ(random.State(), 7u);

    // With the ftol outside what is credited it cuts after the blendMesh.
    globals.worldClock = std::numeric_limits<float>::infinity();
    auto cut = Object(0x15);
    const auto failed = StepArenaObject(cut, globals);
    ASSERT_FALSE(failed.HasValue());
    EXPECT_NE(failed.Error().find("0x60552a"), std::string::npos) << failed.Error();
    EXPECT_EQ(cut.state.blendMesh, 3);
    EXPECT_EQ(cut.state.uv.y, 0.0f);
}

// 0x60546f: the 0x18 leaves +0x58 at −2 and, with rand() & 63 at 0, 0x518cf0(0x5de, …, light +0xf4); the jet
// opens and closes its own record. 0x6059dd: the 0x19 only hides.
TEST(DungeonObjectsTest, TheLostTowerJetHidesAndSometimesSpitsTheFlame)
{
    const std::uint32_t spits = SeedWhere([](int value) { return (value & 63) == 0; });
    const std::uint32_t quiet = SeedWhere([](int value) { return (value & 63) != 0; });
    {
        ModelEffects effects;
        std::uint8_t depth = 1;
        ObjectStepGlobals globals;
        globals.effects = &effects;
        globals.ownRecordDepth = &depth;
        auto object = Object(0x18);
        CrtRandom random(spits);
        ASSERT_TRUE(StepLostTowerObject(object, random, globals).HasValue());
        EXPECT_EQ(object.state.hiddenMesh, Client::WorldObjects::HiddenObject);
        ASSERT_TRUE(effects.Effects()[0].alive);
        EXPECT_EQ(effects.Effects()[0].type, Client::Effects::FlameJetEffect);
        EXPECT_EQ(depth, 1);
        EXPECT_EQ(random.State(), After(spits, 1).State()) << "la cuenta abierta no gasta al cerrar";
    }
    {
        auto object = Object(0x18);
        CrtRandom random(quiet);
        ASSERT_TRUE(StepLostTowerObject(object, random, ObjectStepGlobals{}).HasValue());
        EXPECT_EQ(object.state.hiddenMesh, Client::WorldObjects::HiddenObject);
        EXPECT_EQ(random.State(), After(quiet, 1).State());
    }
    auto object = Object(0x18);
    CrtRandom random(spits);
    const auto cut = StepLostTowerObject(object, random, ObjectStepGlobals{});
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x6054a9"), std::string::npos) << cut.Error();
    EXPECT_EQ(object.state.hiddenMesh, Client::WorldObjects::HiddenObject) << "se oculta antes del rand()";

    auto pillar = Object(0x19);
    CrtRandom none(7);
    ASSERT_TRUE(StepLostTowerObject(pillar, none, ObjectStepGlobals{}).HasValue());
    EXPECT_EQ(pillar.state.hiddenMesh, Client::WorldObjects::HiddenObject);
    EXPECT_EQ(none.State(), 7u);
}

struct Kick
{
    glm::vec3 hero{1030.0f, 1520.0f, 0.0f};
    std::uint8_t action = 0xd;
    std::vector<std::int32_t> sounds;
    ObjectStepGlobals globals;

    Kick()
    {
        globals.heroPosition = &hero;
        globals.heroAction = &action;
        globals.sound = [this](Client::Audio::SoundId sound) { sounds.push_back(sound.Value()); };
    }
};

// 0x52f594..0x52f5fc: under 50 (√(dy · dy as float + dx · dx)), the speed (dx · −0.4; dy · −0.4; 0), the turn
// dy · −4 at +0x28 and dx · −4 at +0x2c, and the 0x7c. 0x52f608..0x52f6a1: everything times 0.6 and added to the
// position (the x with the unrounded product) and to the angles.
TEST(DungeonObjectsTest, TheKickPushesTheObjectAwayFromTheHeroAndDecays)
{
    for (const std::uint8_t action : {std::uint8_t{0xd}, std::uint8_t{0x21}})
    {
        SCOPED_TRACE(static_cast<int>(action));
        Kick kick;
        kick.action = action;
        auto object = Object(0x26);
        object.transform.angles = {1.0f, 2.0f, 3.0f};
        ASSERT_TRUE(KickObject(object, kick.globals).HasValue());
        const double push = static_cast<double>(std::bit_cast<float>(0xbecccccdu));
        const double decay = static_cast<double>(std::bit_cast<float>(0x3f19999au));
        const auto pushX = static_cast<double>(static_cast<float>(30.0 * push)) * decay;
        const auto pushY = static_cast<float>(static_cast<double>(static_cast<float>(20.0 * push)) * decay);
        EXPECT_EQ(object.velocity.x, static_cast<float>(pushX));
        EXPECT_EQ(object.velocity.y, pushY);
        EXPECT_EQ(object.velocity.z, 0.0f);
        const auto spinX = static_cast<float>(static_cast<double>(static_cast<float>(20.0 * -4.0)) * decay);
        const auto spinY = static_cast<float>(static_cast<double>(static_cast<float>(30.0 * -4.0)) * decay);
        EXPECT_EQ(object.copiedAngles.x, spinX);
        EXPECT_EQ(object.copiedAngles.y, spinY);
        EXPECT_EQ(object.transform.position.x, static_cast<float>(pushX + 1000.0));
        EXPECT_EQ(object.transform.position.y, static_cast<float>(static_cast<double>(pushY) + 1500.0));
        EXPECT_EQ(object.transform.angles.x, static_cast<float>(static_cast<double>(spinX) + 1.0));
        EXPECT_EQ(object.transform.angles.y, static_cast<float>(static_cast<double>(spinY) + 2.0));
        EXPECT_EQ(object.transform.angles.z, 3.0f);
        EXPECT_EQ(kick.sounds, std::vector<std::int32_t>{0x7c});
    }
}

// Outside actions 0xd..0x21, at 50 or more, or with the speed's x at 0.1 or more, it only decays. NaN in the speed
// lets it kick (an unordered fcomp sets C0).
TEST(DungeonObjectsTest, TheKickNeedsTheActionTheReachAndAStillObject)
{
    const auto decayed = [](float value)
    { return static_cast<float>(static_cast<double>(value) * static_cast<double>(std::bit_cast<float>(0x3f19999au))); };
    {
        Kick kick;
        kick.action = 0xc;
        auto object = Object(0x27);
        object.velocity = {0.05f, 0.0f, 2.0f};
        ASSERT_TRUE(KickObject(object, kick.globals).HasValue());
        EXPECT_EQ(object.velocity.z, decayed(2.0f));
        EXPECT_TRUE(kick.sounds.empty());
    }
    {
        Kick kick;
        kick.action = 0x22;
        auto object = Object(0x27);
        ASSERT_TRUE(KickObject(object, kick.globals).HasValue());
        EXPECT_TRUE(kick.sounds.empty());
    }
    {
        Kick kick;
        kick.hero = {1030.0f, 1540.0f, 0.0f};
        auto object = Object(0x27);
        object.transform.position = {1000.0f, 1490.0f, 20.0f}; // dx 30, dy 50: √3400 > 50
        ASSERT_TRUE(KickObject(object, kick.globals).HasValue());
        EXPECT_TRUE(kick.sounds.empty());
        kick.hero = {1000.0f, 1540.0f, 0.0f}; // dy exactly 50: not less
        ASSERT_TRUE(KickObject(object, kick.globals).HasValue());
        EXPECT_TRUE(kick.sounds.empty());
    }
    {
        Kick kick;
        auto object = Object(0x27);
        object.velocity.x = std::bit_cast<float>(0x3dcccccdu);
        ASSERT_TRUE(KickObject(object, kick.globals).HasValue());
        EXPECT_TRUE(kick.sounds.empty());
        object.velocity.x = std::numeric_limits<float>::quiet_NaN();
        ASSERT_TRUE(KickObject(object, kick.globals).HasValue());
        EXPECT_EQ(kick.sounds, std::vector<std::int32_t>{0x7c});
    }
}

// Without the published hero it cuts before reading; without an emitter, after the kick's stores and before the
// decay.
TEST(DungeonObjectsTest, TheKickCutsWithoutItsBorrowedBindings)
{
    auto object = Object(0x26);
    const auto cut = KickObject(object, ObjectStepGlobals{});
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x52f556"), std::string::npos) << cut.Error();
    EXPECT_EQ(object.velocity, glm::vec3(0.0f));

    Kick kick;
    kick.globals.sound = nullptr;
    const auto silent = KickObject(object, kick.globals);
    ASSERT_FALSE(silent.HasValue());
    EXPECT_NE(silent.Error().find("0x52f5fc"), std::string::npos) << silent.Error();
    EXPECT_EQ(object.velocity.x, static_cast<float>(30.0 * static_cast<double>(std::bit_cast<float>(0xbecccccdu))));
    EXPECT_EQ(object.transform.position, glm::vec3(1000.0f, 1500.0f, 20.0f));

    // StepLostTowerObject takes the 0x26 and the 0x27 to the kick.
    Kick routed;
    auto ball = Object(0x27);
    CrtRandom random(7);
    ASSERT_TRUE(StepLostTowerObject(ball, random, routed.globals).HasValue());
    EXPECT_EQ(routed.sounds, std::vector<std::int32_t>{0x7c});
    EXPECT_EQ(random.State(), 7u);
}
} // namespace
