#include "Models/IModelLibrary.h"
#include "MapScripts/CastleSiege/CastleSiegeObjectServices.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Data/Models/Model.h"
#include "Render/ModelBatch.h"
#include "Render/ModelVertex.h"
#include "Math/HalfLifeMath.h"
#include "Math/RoundedMath.h"
#include "Models/Skeleton.h"
#include "Models/Skinning.h"
#include "Models/ModelId.h"
#include "Render/Pass3D.h"
#include "Render/TextureSlot.h"
#include "Actors/Drawing/CharacterDrawing.h"
#include "Scenes/GameDrawTail.h"
#include "Data/Terrain/TerrainHeights.h"
#include "Particles/Sprites.h"
#include "Terrain/SphereFrustum.h"
#include "Particles/KalimaParticleKernels.h"
#include "Particles/ParticleOwner.h"
#include "Particles/Particles.h"
#include "Kernel/WorldObject.h"
#include "Kernel/ObjectPass.h"
#include "Effects/ModelEffects.h"

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>
#include <gtest/gtest.h>

#include <bit>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

// The objects of map 30 (06 § Los objetos de Castle Siege): the draw case 0x42c9f0, with its rand() and its
// particles, and the body case 0x42cc10 (DrawCastleSiegeBody), with the banner, the shifts, the pulse, the Chrome01
// passes, the gates and the shadow with spread 2500.
namespace
{
using Client::Actors::Drawing::CastleSiegeBodyPass;
using Client::Actors::Drawing::DrawCastleSiegeBody;
using Client::Actors::Drawing::DrawnObject;
using Client::Crt::CrtRandom;
using Client::Data::Models::Mesh;
using Client::Data::Models::Model;
using Client::Kernel::GameObjectPass;
using Client::Kernel::WorldObject;
using Client::MapScripts::CastleSiege::StepCastleSiegeObjectFrame;
using Client::Math::RoundedSin;
using Client::Models::IModelLibrary;
using Client::Models::Skeleton;
using Client::Particles::Particle;
using Client::Render::ModelBatch;
using Client::Render::ModelBlend;
using Client::Render::ModelDrawState;
using Client::Render::TextureSlot;

constexpr std::uint8_t CastleSiegeMap = 30;
constexpr std::uint32_t Seed = 7;

struct Fixture
{
    std::unique_ptr<Client::Particles::Particles> particles = std::make_unique<Client::Particles::Particles>();
    CrtRandom random{Seed};
    bool siege = false;

    [[nodiscard]] GameObjectPass Pass()
    {
        GameObjectPass pass;
        pass.map = CastleSiegeMap;
        pass.random = &random;
        pass.worldClock = 12345.5f;
        pass.particles = particles.get();
        pass.siegeActive = siege;
        return pass;
    }

    [[nodiscard]] std::vector<const Particle*> Alive() const
    {
        std::vector<const Particle*> alive;
        for (const Particle& particle : particles->All())
        {
            if (particle.alive)
                alive.push_back(&particle);
        }
        return alive;
    }
};

WorldObject ObjectOf(std::int16_t type)
{
    WorldObject object;
    object.identity = 12;
    object.type = type;
    object.transform.scale = 1.5f;
    object.transform.position = {9200.0f, 9900.0f, 180.0f};
    return object;
}

// The first seed whose first rand() % 3 is 0, or is not, as `zero` asks.
std::uint32_t SeedWhere(bool zero)
{
    for (std::uint32_t seed = 1;; ++seed)
    {
        CrtRandom probe(seed);
        if ((probe.Next() % 3 == 0) == zero)
            return seed;
    }
}

// 0x42ca37..0x42ca53: sin(T · 0.002f) · 0.3f + 0.7f, on the x87 up to the fstp.
TEST(CastleSiegeObjectServicesTest, TheLampPulsesWithTheClockInBothStates)
{
    for (const bool siege : {false, true})
    {
        Fixture fixture;
        fixture.siege = siege;
        WorldObject object = ObjectOf(0x29);
        ASSERT_TRUE(StepCastleSiegeObjectFrame(object, fixture.Pass()).HasValue());
        const double sine = RoundedSin(12345.5 * static_cast<double>(std::bit_cast<float>(0x3b03126fu)));
        EXPECT_FLOAT_EQ(object.state.light,
                        static_cast<float>(sine * static_cast<double>(std::bit_cast<float>(0x3e99999au)) +
                                           static_cast<double>(std::bit_cast<float>(0x3f333333u))));
        EXPECT_EQ(fixture.random.State(), Seed);
        EXPECT_TRUE(fixture.Alive().empty());
    }

    Fixture still;
    GameObjectPass pass = still.Pass();
    pass.worldClock = 0.0f;
    WorldObject object = ObjectOf(0x29);
    ASSERT_TRUE(StepCastleSiegeObjectFrame(object, pass).HasValue());
    EXPECT_EQ(object.state.light, std::bit_cast<float>(0x3f333333u));
}

// 0x42cb61..0x42cbab: without the siege, 0x6f2 + rand() % 2 with subtype 0, scale 1 and white light; the creation
// (0x53fe70) then draws its six rand().
TEST(CastleSiegeObjectServicesTest, TheFountainSplashesOneOfTheTwoSplashesWithoutTheSiege)
{
    bool first = false;
    bool second = false;
    for (std::uint32_t seed = 1; seed <= 8; ++seed)
    {
        Fixture fixture;
        fixture.random = CrtRandom(seed);
        CrtRandom draws(seed);
        const std::int32_t type = Client::Particles::WaterfallSplashFirst + draws.Next() % 2;
        for (int draw = 0; draw < 6; ++draw)
            static_cast<void>(draws.Next());
        WorldObject object = ObjectOf(0x36);
        ASSERT_TRUE(StepCastleSiegeObjectFrame(object, fixture.Pass()).HasValue());
        const auto alive = fixture.Alive();
        ASSERT_EQ(alive.size(), 1u) << seed;
        EXPECT_EQ(alive[0]->type, type);
        EXPECT_EQ(alive[0]->subtype, 0);
        EXPECT_EQ(alive[0]->life, 20);
        EXPECT_EQ(alive[0]->light, glm::vec3(1.0f));
        EXPECT_EQ(alive[0]->typedOwner, Client::Particles::ParticleOwner{});
        EXPECT_EQ(fixture.random.State(), draws.State());
        (type == Client::Particles::WaterfallSplashFirst ? first : second) = true;
    }
    EXPECT_TRUE(first);
    EXPECT_TRUE(second);

    Fixture siege;
    siege.siege = true;
    WorldObject object = ObjectOf(0x36);
    ASSERT_TRUE(StepCastleSiegeObjectFrame(object, siege.Pass()).HasValue());
    EXPECT_TRUE(siege.Alive().empty());
    EXPECT_EQ(siege.random.State(), Seed);
}

// Without the pool, each creation cuts at its call to 0x53fe70, after its rand(): the 0x36 and the 0 draw one and
// the 0x34 none.
TEST(CastleSiegeObjectServicesTest, EachReleaseWithoutThePoolCutsAfterItsDraws)
{
    for (const auto& [type, address, draws] :
         {std::tuple<std::int16_t, std::string_view, int>{std::int16_t{0x36}, "0x42cbab", 1},
          std::tuple<std::int16_t, std::string_view, int>{std::int16_t{0}, "0x42cb52", 1},
          std::tuple<std::int16_t, std::string_view, int>{std::int16_t{0x34}, "0x42caf4", 0}})
    {
        SCOPED_TRACE(type);
        Fixture fixture;
        const std::uint32_t seed = SeedWhere(true);
        fixture.random = CrtRandom(seed);
        GameObjectPass pass = fixture.Pass();
        pass.particles = nullptr;
        WorldObject object = ObjectOf(type);
        const auto cut = StepCastleSiegeObjectFrame(object, pass);
        ASSERT_FALSE(cut.HasValue());
        EXPECT_NE(cut.Error().find(address), std::string::npos) << cut.Error();
        CrtRandom expected(seed);
        for (int draw = 0; draw < draws; ++draw)
            static_cast<void>(expected.Next());
        EXPECT_EQ(fixture.random.State(), expected.State());
    }
}

// 0x42cb10..0x42cb52: the 0 without the siege and the 0x35 with the siege draw a rand() and, with the remainder by 3
// at 0, the spark 0x53fe70(0x6ec, position, angles, (1, 1, 1), 0, scale, 0): the one of the 0x6ed/0, with life 24
// and two more rand() (0x543b82). In the other state they draw nothing.
TEST(CastleSiegeObjectServicesTest, TheTorchesSparkOnceInThreeOnTheirSideOfTheSiege)
{
    for (const auto& [type, siege] : {std::pair<std::int16_t, bool>{std::int16_t{0}, false},
                                      std::pair<std::int16_t, bool>{std::int16_t{0x35}, true}})
    {
        for (const bool zero : {false, true})
        {
            SCOPED_TRACE(std::to_string(type) + (zero ? "/0" : "/1"));
            Fixture fixture;
            fixture.siege = siege;
            const std::uint32_t seed = SeedWhere(zero);
            fixture.random = CrtRandom(seed);
            CrtRandom draws(seed);
            static_cast<void>(draws.Next());
            WorldObject object = ObjectOf(type);
            object.transform.angles = {0.0f, 0.0f, 30.0f};
            ASSERT_TRUE(StepCastleSiegeObjectFrame(object, fixture.Pass()).HasValue());
            const auto alive = fixture.Alive();
            if (!zero)
            {
                EXPECT_TRUE(alive.empty());
                EXPECT_EQ(fixture.random.State(), draws.State());
                continue;
            }
            ASSERT_EQ(alive.size(), 1u);
            const Particle& spark = *alive[0];
            EXPECT_EQ(spark.type, Client::Particles::CrywolfFanta);
            EXPECT_EQ(spark.texture, Client::Particles::CrywolfFanta);
            EXPECT_EQ(spark.subtype, 0);
            EXPECT_EQ(spark.life, 24);
            EXPECT_EQ(spark.scale, 1.5f);
            EXPECT_EQ(spark.light, glm::vec3(1.0f));
            EXPECT_EQ(spark.angles, object.transform.angles);
            EXPECT_EQ(spark.start, object.transform.position);
            EXPECT_EQ(spark.velocity.x,
                      static_cast<float>(static_cast<double>(draws.Next() % 10 - 5) * static_cast<double>(0.4f)));
            EXPECT_EQ(spark.velocity.z,
                      static_cast<float>(static_cast<double>(draws.Next() % 10 + 5) * static_cast<double>(0.2f)));
            EXPECT_EQ(spark.typedOwner, Client::Particles::ParticleOwner{});
            EXPECT_EQ(fixture.random.State(), draws.State());
        }

        Fixture other;
        other.siege = !siege;
        other.random = CrtRandom(SeedWhere(true));
        const std::uint32_t before = other.random.State();
        WorldObject object = ObjectOf(type);
        EXPECT_TRUE(StepCastleSiegeObjectFrame(object, other.Pass()).HasValue());
        EXPECT_EQ(other.random.State(), before);
        EXPECT_TRUE(other.Alive().empty());
    }
}

// 0x42cab9..0x42caf4: the 0x34 without the siege, on every frame and with no rand() of its own, the fall
// 0x53fe70(0x6f4, position, angles, (1, 1, 1), 6, scale, 0), which draws the two of 0x543f04; with the siege, nothing.
TEST(CastleSiegeObjectServicesTest, TheBrazierDropsItsFallOnEveryFrameWithoutTheSiege)
{
    Fixture fixture;
    CrtRandom draws(Seed);
    WorldObject object = ObjectOf(0x34);
    ASSERT_TRUE(StepCastleSiegeObjectFrame(object, fixture.Pass()).HasValue());
    const auto alive = fixture.Alive();
    ASSERT_EQ(alive.size(), 1u);
    const Particle& fall = *alive[0];
    EXPECT_EQ(fall.type, Client::Particles::KalimaWaterfallLast);
    EXPECT_EQ(fall.subtype, Client::Particles::BrazierFallSubtype);
    EXPECT_EQ(fall.life, 30);
    EXPECT_EQ(fall.rotation, static_cast<float>(draws.Next() % 360));
    EXPECT_EQ(fall.scale, static_cast<float>(1.5 + static_cast<double>(std::bit_cast<float>(0x3f19999au))));
    EXPECT_EQ(fall.velocity, glm::vec3(0.0f, 0.0f, static_cast<float>(-12 - draws.Next() % 5)));
    EXPECT_EQ(fall.light, glm::vec3(std::bit_cast<float>(0x3e4ccccdu)));
    EXPECT_EQ(fall.position, object.transform.position);
    EXPECT_EQ(fixture.random.State(), draws.State());
    ASSERT_TRUE(StepCastleSiegeObjectFrame(object, fixture.Pass()).HasValue());
    EXPECT_EQ(fixture.Alive().size(), 2u);

    Fixture siege;
    siege.siege = true;
    EXPECT_TRUE(StepCastleSiegeObjectFrame(object, siege.Pass()).HasValue());
    EXPECT_EQ(siege.random.State(), Seed);
    EXPECT_TRUE(siege.Alive().empty());
}

// 0x42ca5b..0x42caaa: the 0x2a only smokes with the siege: a rand() and, with the remainder by 3 at 0, the smoke
// 0x53fe70(0x5f2, position, angles, (1, 1, 1), 0x15, scale, 0), Crywolf's (0x542370), with its three rand().
TEST(CastleSiegeObjectServicesTest, TheChimneySmokesOnlyDuringTheSiege)
{
    Fixture quiet;
    quiet.random = CrtRandom(SeedWhere(true));
    const std::uint32_t before = quiet.random.State();
    WorldObject object = ObjectOf(0x2a);
    EXPECT_TRUE(StepCastleSiegeObjectFrame(object, quiet.Pass()).HasValue());
    EXPECT_EQ(quiet.random.State(), before);
    EXPECT_TRUE(quiet.Alive().empty());

    for (const bool zero : {false, true})
    {
        SCOPED_TRACE(zero);
        Fixture fixture;
        fixture.siege = true;
        const std::uint32_t seed = SeedWhere(zero);
        fixture.random = CrtRandom(seed);
        CrtRandom draws(seed);
        static_cast<void>(draws.Next());
        ASSERT_TRUE(StepCastleSiegeObjectFrame(object, fixture.Pass()).HasValue());
        const auto alive = fixture.Alive();
        if (!zero)
        {
            EXPECT_TRUE(alive.empty());
            EXPECT_EQ(fixture.random.State(), draws.State());
            continue;
        }
        ASSERT_EQ(alive.size(), 1u);
        const Particle& smoke = *alive[0];
        EXPECT_EQ(smoke.type, Client::Particles::BreathParticle);
        EXPECT_EQ(smoke.subtype, Client::Particles::CrywolfSmokeSubtype);
        EXPECT_EQ(smoke.life, 80);
        const int size = (draws.Next() & 63) + 64;
        EXPECT_EQ(smoke.scale, static_cast<float>(static_cast<double>(size) * 1.5 *
                                                  static_cast<double>(std::bit_cast<float>(0x3ba3d70au))));
        EXPECT_EQ(smoke.rotation, static_cast<float>(draws.Next() % 360));
        static_cast<void>(draws.Next());
        EXPECT_EQ(smoke.light, glm::vec3(1.0f));
        EXPECT_EQ(smoke.position, object.transform.position);
        EXPECT_EQ(fixture.random.State(), draws.State());
    }
}

// The types without a case (the banner, the gates, the ones that only draw) draw no rand() and do not change the
// object; without the CRT's rand(), it cuts before the switch.
TEST(CastleSiegeObjectServicesTest, TheOtherTypesDoNothingAndTheRandomIsRequired)
{
    for (const std::int16_t type : {std::int16_t{0xc}, std::int16_t{0x13}, std::int16_t{0x3d}, std::int16_t{0x42}})
    {
        for (const bool siege : {false, true})
        {
            Fixture fixture;
            fixture.siege = siege;
            WorldObject object = ObjectOf(type);
            const WorldObject before = object;
            ASSERT_TRUE(StepCastleSiegeObjectFrame(object, fixture.Pass()).HasValue());
            EXPECT_EQ(fixture.random.State(), Seed);
            EXPECT_EQ(object.state, before.state);
            EXPECT_TRUE(fixture.Alive().empty());
        }
    }

    Fixture fixture;
    GameObjectPass pass = fixture.Pass();
    pass.random = nullptr;
    WorldObject object = ObjectOf(0x29);
    const auto cut = StepCastleSiegeObjectFrame(object, pass);
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x42c9f0"), std::string::npos);
}

Skeleton OneBone()
{
    Skeleton skeleton;
    Client::Models::SkeletonBone bone;
    bone.parent = -1;
    bone.actions = {{Client::Models::BoneKey{}}};
    bone.actions[0][0].rotation = Client::Math::AngleQuaternion({0.0f, 0.0f, 0.0f});
    skeleton.bones.assign(1, bone);
    skeleton.actions = {
        Client::Models::ActionInfo{.repeats = true, .speed = 0.0f, .keyCount = 1, .lockPositions = false}};
    return skeleton;
}

constexpr std::uint16_t SiegeModel = 0x4c;

// `meshes` meshes of one triangle raised 10 above the floor, each with the texture of its index and the slot
// 0x300 plus the index.
class SiegeModels final : public IModelLibrary
{
public:
    explicit SiegeModels(std::size_t meshes) : m_skeleton(OneBone())
    {
        Mesh mesh;
        mesh.vertices = {{0, {0, 0, 10}}, {0, {10, 0, 10}}, {0, {0, 10, 10}}};
        mesh.normals = {{0, {0, 0, 1}, 0}};
        mesh.texCoords = {{0, 0}};
        mesh.triangles = {{3, {0, 1, 2, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}}};
        mesh.textureName = "wall.jpg";
        m_model.meshes.assign(meshes, mesh);
        for (std::size_t index = 0; index < meshes; ++index)
        {
            m_model.meshes[index].texture = static_cast<std::int16_t>(index);
            m_slots.push_back(TextureSlot{static_cast<std::uint16_t>(0x300 + index)});
        }
    }

    [[nodiscard]] const Model* Find(Client::Models::ModelId model) const override
    {
        return model.Value() == SiegeModel ? &m_model : nullptr;
    }
    [[nodiscard]] const Skeleton* SkeletonOf(Client::Models::ModelId model) const override
    {
        return model.Value() == SiegeModel ? &m_skeleton : nullptr;
    }
    [[nodiscard]] std::span<const TextureSlot> MeshTextures(Client::Models::ModelId model) const override
    {
        return model.Value() == SiegeModel ? std::span<const TextureSlot>(m_slots) : std::span<const TextureSlot>{};
    }

private:
    Skeleton m_skeleton;
    Model m_model;
    std::vector<TextureSlot> m_slots;
};

DrawnObject SiegeObject()
{
    DrawnObject object;
    object.model = SiegeModel;
    object.transform.position = {9200.0f, 9900.0f, 180.0f};
    return object;
}

// 0x4eff8d..0x4eff9f: the map comparison uses the entire signed value.
TEST(CastleSiegeDrawingTest, OnlyTheCastleSiegeSpreadsTheShadowFurther)
{
    const SiegeModels models(1);
    DrawnObject object = SiegeObject();
    object.transform.position = {10.0f, 20.0f, 30.0f};
    std::array<Client::Models::SkinnedMesh, 1> meshes{};
    meshes[0].positions = {{110.0f, 77.0f, 80.0f}, {111.0f, 77.0f, 80.0f}, {110.0f, 78.0f, 80.0f}};
    meshes[0].normals = {{0.0f, 0.0f, 1.0f}};
    ModelBatch batch;

    for (const std::int32_t map : {-1, 0, 2, 30, 31, 34, 286})
    {
        SCOPED_TRACE(map);
        batch.Begin(Client::Render::Pass3D{});
        Client::Actors::Drawing::DrawObjectShadow(batch, models, object, meshes, map);

        ASSERT_EQ(batch.Vertices().size(), 3u);
        const float x = map == 30 ? 0x1.345abcp+6f : 0x1.4dabcp+6f;
        EXPECT_EQ(batch.Vertices()[0].position, glm::vec3(x, 77.0f, 35.0f));
    }

    // 0x4ed73e: the item mesh pass uses 2000 even in Castle Siege.
    batch.Begin(Client::Render::Pass3D{});
    const auto drawn = Client::Actors::Drawing::DrawItemMeshPass(batch, models, object, {}, meshes, 0, 0x21);
    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    ASSERT_EQ(batch.Vertices().size(), 3u);
    EXPECT_EQ(batch.Vertices()[0].position, glm::vec3(0x1.4dabcp+6f, 77.0f, 35.0f));
}

struct Drawn
{
    ModelBatch batch;
    Client::Core::Expected<bool, std::string> result{false};
};

Drawn DrawBody(const SiegeModels& models, const DrawnObject& object, const CastleSiegeBodyPass& pass)
{
    Drawn drawn;
    drawn.batch.Begin(Client::Render::Pass3D{});
    drawn.result = DrawCastleSiegeBody(drawn.batch, models, object, pass, std::chrono::milliseconds{0});
    return drawn;
}

// (ftol(T) % 10000) · 0.0002f.
float Scroll(std::int32_t clock)
{
    return static_cast<float>(static_cast<double>(clock % 10000) *
                              static_cast<double>(std::bit_cast<float>(0x3951b717u)));
}

// 0x42cfd6: the types without a case go to 0x42e3d0, which draws nothing with a map object; the caller draws them
// as always.
TEST(CastleSiegeDrawingTest, TheTypesWithoutACaseAreLeftToTheCommonBody)
{
    const SiegeModels models(1);
    for (const std::int16_t type : {std::int16_t{0}, std::int16_t{0x29}, std::int16_t{0x36}, std::int16_t{0x41}})
    {
        const Drawn drawn = DrawBody(models, SiegeObject(), {.type = type});
        ASSERT_TRUE(drawn.result.HasValue());
        EXPECT_FALSE(drawn.result.Value()) << type;
        EXPECT_TRUE(drawn.batch.Vertices().empty());
    }
}

// 0x42cc32..0x42cc89: the banner, in color 1, with mesh 0 with its texture and mesh 1 with Flag (0x69c).
TEST(CastleSiegeDrawingTest, TheBannerDrawsItsClothWithTheFlag)
{
    const SiegeModels models(2);
    DrawnObject object = SiegeObject();
    object.color = glm::vec3(0.25f);
    const Drawn drawn = DrawBody(models, object, {.type = 0xc});
    ASSERT_TRUE(drawn.result.HasValue());
    EXPECT_TRUE(drawn.result.Value());
    ASSERT_EQ(drawn.batch.Commands().size(), 2u);
    EXPECT_EQ(drawn.batch.Commands()[0].state.texture, TextureSlot{0x300});
    EXPECT_EQ(drawn.batch.Commands()[1].state.texture, TextureSlot{0x69c});
    for (const Client::Render::ModelVertex& vertex : drawn.batch.Vertices())
    {
        EXPECT_FLOAT_EQ(vertex.color.red, 1.0f);
        EXPECT_FLOAT_EQ(vertex.color.green, 1.0f);
        EXPECT_FLOAT_EQ(vertex.color.blue, 1.0f);
    }
}

// 0x42cc9a..0x42ccf7: the 0x11 scrolls mesh 0's V with the clock; 0x42cd0a..0x42cd5b: the 0x33 scrolls the U
// with the negated clock (the remainder takes the dividend's sign).
TEST(CastleSiegeDrawingTest, TheWaterAndTheFallsScrollWithTheClock)
{
    const SiegeModels models(1);
    const Drawn water = DrawBody(models, SiegeObject(), {.type = 0x11, .worldClock = 12345.5f});
    ASSERT_TRUE(water.result.HasValue());
    ASSERT_EQ(water.batch.Vertices().size(), 3u);
    for (const Client::Render::ModelVertex& vertex : water.batch.Vertices())
        EXPECT_EQ(vertex.uv, glm::vec2(0.0f, Scroll(12345)));

    const Drawn falls = DrawBody(models, SiegeObject(), {.type = 0x33, .worldClock = 12345.5f});
    ASSERT_TRUE(falls.result.HasValue());
    ASSERT_EQ(falls.batch.Vertices().size(), 3u);
    EXPECT_LT(Scroll(-12345), 0.0f);
    for (const Client::Render::ModelVertex& vertex : falls.batch.Vertices())
        EXPECT_EQ(vertex.uv, glm::vec2(Scroll(-12345), 0.0f));
}

// 0x42cef9..0x42cf62: the 0x13 without the siege, the body and two passes with Chrome01; with the siege it cuts at
// 0x42ce09.
TEST(CastleSiegeDrawingTest, TheGlowingStoneUsesTheWholeBodyInPeaceAndOnlyMeshThreeInWar)
{
    const SiegeModels models(1);
    const Drawn drawn = DrawBody(models, SiegeObject(), {.type = 0x13});
    ASSERT_TRUE(drawn.result.HasValue());
    ASSERT_EQ(drawn.batch.Vertices().size(), 9u);
    const auto& commands = drawn.batch.Commands();
    ASSERT_GE(commands.size(), 2u);
    EXPECT_EQ(commands.front().state.texture, TextureSlot{0x300});
    EXPECT_EQ(commands.back().state.texture, Client::Actors::Drawing::ChromeTextureSlot);

    const SiegeModels warModels(4);
    const Drawn siege = DrawBody(warModels, SiegeObject(), {.type = 0x13, .siegeActive = true});
    ASSERT_TRUE(siege.result.HasValue());
    ASSERT_EQ(siege.batch.Vertices().size(), 9u);
    ASSERT_GE(siege.batch.Commands().size(), 2u);
    EXPECT_EQ(siege.batch.Commands().front().state.texture, TextureSlot{0x303});
    EXPECT_EQ(siege.batch.Commands().back().state.texture, Client::Actors::Drawing::ChromeTextureSlot);
}

TEST(CastleSiegeDrawingTest, AFragmentEmitsFromItsPosedVerticesInsteadOfDrawingTheGateAgain)
{
    const SiegeModels models(1);
    int calls = 0;
    const CastleSiegeBodyPass pass{
        .type = 0x3d,
        .field04 = 1,
        .emitGateVertices = [&](const DrawnObject& object, std::span<const Client::Models::SkinnedMesh> meshes)
            -> Client::Core::Expected<void, std::string>
        {
            ++calls;
            EXPECT_EQ(object.model, SiegeModel);
            EXPECT_EQ(meshes.size(), 1u);
            if (!meshes.empty())
            {
                EXPECT_EQ(meshes[0].positions.size(), 3u);
            }
            return {};
        }};
    const Drawn drawn = DrawBody(models, SiegeObject(), pass);
    ASSERT_TRUE(drawn.result.HasValue());
    EXPECT_TRUE(drawn.result.Value());
    EXPECT_EQ(calls, 1);
    EXPECT_TRUE(drawn.batch.Vertices().empty());
}

TEST(CastleSiegeDrawingTest, TheCrywolfMissileTailFetchesTheUpdatedGlobalPoseEvenWithoutAModel)
{
    const SiegeModels models(1);
    auto heights = std::make_unique<Client::Data::Terrain::TerrainHeights>();
    auto particles = std::make_unique<Client::Particles::Particles>();
    Client::Particles::FrameSprites sprites;
    std::array<Client::Effects::ModelEffect, 4> effects{};
    effects[3].alive = true;
    effects[3].type = 0xee;
    effects[3].alpha = 1;
    effects[3].transform.scale = 1;
    std::vector<Client::Math::BoneTransform> scratch(1, Client::Math::BoneTransform(0));
    bool posed = false;
    CrtRandom random(Seed);
    const Client::Scenes::ModelEffectDrawResources resources{
        .heights = *heights,
        .onPose = [&](const DrawnObject&) -> Client::Core::Expected<void, std::string>
        {
            scratch.resize(2, Client::Math::BoneTransform(1));
            scratch[1][3] = {100, 200, 300};
            posed = true;
            return {};
        },
        .sprites = &sprites,
        .random = &random,
        .particles = particles.get(),
        .posedBones = [&]() -> std::span<const Client::Math::BoneTransform>
        {
            EXPECT_TRUE(posed);
            return scratch;
        }};
    ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});
    const auto result = Client::Scenes::DrawGameModelEffects(batch, models, effects, Client::Terrain::SphereFrustum{},
                                                             nullptr, {}, 0, resources);
    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_TRUE(batch.Vertices().empty());
    EXPECT_TRUE(effects[3].seen);
    EXPECT_EQ(particles->All()[2].typedOwner,
              (Client::Particles::ParticleOwner{Client::Particles::ParticleOwnerKind::ModelEffect, 3}));
    EXPECT_EQ(sprites.Sprites()[0].position, (glm::vec3{100, 201.5f, 300}));
    EXPECT_TRUE(sprites.Sprites()[9].alive);
    CrtRandom expected(Seed);
    for (int i = 0; i < 16; ++i)
        static_cast<void>(expected.Next());
    EXPECT_EQ(random.State(), expected.State());
}

// 0x42cf7b..0x42cfcb: the gates 0x3d to 0x40 go with RenderBody if +0x4 is at 0; if not, it cuts at 0x4ee770.
TEST(CastleSiegeDrawingTest, TheGatesDrawTheirBodyOnlyWithTheirFieldAtZero)
{
    const SiegeModels models(1);
    for (const std::int16_t gate : {std::int16_t{0x3d}, std::int16_t{0x40}})
    {
        const Drawn closed = DrawBody(models, SiegeObject(), {.type = gate});
        ASSERT_TRUE(closed.result.HasValue());
        EXPECT_TRUE(closed.result.Value());
        EXPECT_EQ(closed.batch.Vertices().size(), 3u);

        const Drawn open = DrawBody(models, SiegeObject(), {.type = gate, .field04 = 1});
        ASSERT_FALSE(open.result.HasValue());
        EXPECT_NE(open.result.Error().find("0x42cfcb"), std::string::npos);
        EXPECT_TRUE(open.batch.Vertices().empty());
    }
}

// 0x42cfe7..0x42d038: the 0x32 and the 0x53 to 0x55, without the siege, the body and its black shadow, flattened
// from the object with spread 2500; with the siege, the body alone.
TEST(CastleSiegeDrawingTest, TheShadowedWallsCastAWiderShadowOnlyWithoutTheSiege)
{
    const SiegeModels models(1);
    const DrawnObject object = SiegeObject();
    for (const std::int16_t type : {std::int16_t{0x32}, std::int16_t{0x53}, std::int16_t{0x55}})
    {
        const Drawn drawn = DrawBody(models, object, {.type = type});
        ASSERT_TRUE(drawn.result.HasValue());
        ASSERT_EQ(drawn.batch.Commands().size(), 2u);
        EXPECT_EQ(drawn.batch.Commands()[1].state, (ModelDrawState{std::nullopt, ModelBlend::Shadow, true}));
        const auto vertices = drawn.batch.Vertices();
        ASSERT_EQ(vertices.size(), 6u);
        for (std::size_t corner = 0; corner < 3; ++corner)
        {
            const glm::vec3 body = vertices[corner].position;
            ASSERT_NE(body.z, object.transform.position.z);
            const glm::vec3 flat = vertices[corner + 3].position;
            EXPECT_EQ(flat, Client::Models::FlattenOnTheGround(body, object.transform.position, 2500.0f));
            EXPECT_NE(flat, Client::Models::FlattenOnTheGround(body, object.transform.position, 2000.0f));
            EXPECT_FLOAT_EQ(vertices[corner + 3].color.red, 0.0f);
        }

        const Drawn siege = DrawBody(models, object, {.type = type, .siegeActive = true});
        ASSERT_TRUE(siege.result.HasValue());
        EXPECT_EQ(siege.batch.Vertices().size(), 3u);
    }
}

TEST(CastleSiegeDrawingTest, AMissingModelIsTakenAsDrawn)
{
    const SiegeModels models(1);
    DrawnObject object = SiegeObject();
    object.model = SiegeModel + 1;
    const Drawn drawn = DrawBody(models, object, {.type = 0x42});
    ASSERT_TRUE(drawn.result.HasValue());
    EXPECT_TRUE(drawn.result.Value());
    EXPECT_TRUE(drawn.batch.Vertices().empty());
}
} // namespace
