#include "Scenes/CastleSiegeObjectServices.h"

#include "Core/Expected.h"
#include "Data/Models/Model.h"
#include "Render/ModelBatch.h"
#include "Render/ModelVertex.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/HalfLifeMath.h"
#include "Render/Models/Skeleton.h"
#include "Render/Models/Skinning.h"
#include "Render/Pass3D.h"
#include "Render/TextureSlot.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/WorldObjectsDrawing.h"
#include "World/KalimaParticleKernels.h"
#include "World/ParticleOwner.h"
#include "World/Particles.h"
#include "World/WorldObjects.h"

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>
#include <gtest/gtest.h>

#include <bit>
#include <chrono>
#include <cmath>
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

// Los objetos del mapa 30 (06 § Los objetos de Castle Siege): el caso del dibujo 0x42c9f0, con sus rand() y sus
// partículas, y el del cuerpo 0x42cc10 (DrawCastleSiegeBody), con la bandera, los corrimientos, el pulso, los pases de
// Chrome01, las puertas y la sombra con el spread 2500.
namespace
{
using Client::Data::Models::Mesh;
using Client::Data::Models::Model;
using Client::Render::ModelBatch;
using Client::Render::ModelBlend;
using Client::Render::ModelDrawState;
using Client::Render::TextureSlot;
using Client::Render::Models::CrtRandom;
using Client::Render::Models::Skeleton;
using Client::Scenes::CastleSiegeBodyPass;
using Client::Scenes::DrawCastleSiegeBody;
using Client::Scenes::DrawnObject;
using Client::Scenes::GameObjectPass;
using Client::Scenes::IModelLibrary;
using Client::Scenes::StepCastleSiegeObjectFrame;
using Client::World::Particle;
using Client::World::WorldObject;

constexpr std::uint8_t CastleSiegeMap = 30;
constexpr std::uint32_t Seed = 7;

struct Fixture
{
    std::unique_ptr<Client::World::Particles> particles = std::make_unique<Client::World::Particles>();
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

// El primer rand() que cae (o no cae) en el resto por 3 que pide `zero`.
std::uint32_t SeedWhere(bool zero)
{
    for (std::uint32_t seed = 1;; ++seed)
    {
        CrtRandom probe(seed);
        if ((probe.Next() % 3 == 0) == zero)
            return seed;
    }
}

// 0x42ca37..0x42ca53: sin(T · 0,002f) · 0,3f + 0,7f, en la x87 hasta el fstp.
TEST(CastleSiegeObjectServicesTest, TheLampPulsesWithTheClockInBothStates)
{
    for (const bool siege : {false, true})
    {
        Fixture fixture;
        fixture.siege = siege;
        WorldObject object = ObjectOf(0x29);
        ASSERT_TRUE(StepCastleSiegeObjectFrame(object, fixture.Pass()).HasValue());
        const double sine = std::sin(12345.5 * static_cast<double>(std::bit_cast<float>(0x3b03126fu)));
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

// 0x42cb61..0x42cbab: sin el asedio, 0x6f2 + rand() % 2 con el subtipo 0, la escala 1 y la luz blanca; el alta
// (0x53fe70) saca después sus seis rand().
TEST(CastleSiegeObjectServicesTest, TheFountainSplashesOneOfTheTwoSplashesWithoutTheSiege)
{
    bool first = false;
    bool second = false;
    for (std::uint32_t seed = 1; seed <= 8; ++seed)
    {
        Fixture fixture;
        fixture.random = CrtRandom(seed);
        CrtRandom draws(seed);
        const std::int32_t type = Client::World::WaterfallSplashFirst + draws.Next() % 2;
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
        EXPECT_EQ(alive[0]->typedOwner, Client::World::ParticleOwner{});
        EXPECT_EQ(fixture.random.State(), draws.State());
        (type == Client::World::WaterfallSplashFirst ? first : second) = true;
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

// Sin el pool, cada alta corta en su llamada a 0x53fe70, después de sus rand(): el 0x36 y el 0 sacan uno y el 0x34,
// ninguno.
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

// 0x42cb10..0x42cb52: el 0 sin el asedio y el 0x35 con el asedio sacan un rand() y, con el resto por 3 en 0, la chispa
// 0x53fe70(0x6ec, posición, ángulos, (1, 1, 1), 0, escala, 0): la de la 6ed/0, con vida 24 y dos rand() más
// (0x543b82). En el otro estado no sacan nada.
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
            EXPECT_EQ(spark.type, Client::World::CrywolfFanta);
            EXPECT_EQ(spark.texture, Client::World::CrywolfFanta);
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
            EXPECT_EQ(spark.typedOwner, Client::World::ParticleOwner{});
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

// 0x42cab9..0x42caf4: el 0x34 sin el asedio, en cada frame y sin rand() propio, la caída
// 0x53fe70(0x6f4, posición, ángulos, (1, 1, 1), 6, escala, 0), que saca los dos de 0x543f04; con el asedio, nada.
TEST(CastleSiegeObjectServicesTest, TheBrazierDropsItsFallOnEveryFrameWithoutTheSiege)
{
    Fixture fixture;
    CrtRandom draws(Seed);
    WorldObject object = ObjectOf(0x34);
    ASSERT_TRUE(StepCastleSiegeObjectFrame(object, fixture.Pass()).HasValue());
    const auto alive = fixture.Alive();
    ASSERT_EQ(alive.size(), 1u);
    const Particle& fall = *alive[0];
    EXPECT_EQ(fall.type, Client::World::KalimaWaterfallLast);
    EXPECT_EQ(fall.subtype, Client::World::BrazierFallSubtype);
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

// 0x42ca5b..0x42caaa: el 0x2a solo humea con el asedio: un rand() y, con el resto por 3 en 0, el humo
// 0x53fe70(0x5f2, posición, ángulos, (1, 1, 1), 0x15, escala, 0), el de Crywolf (0x542370), con sus tres rand().
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
        EXPECT_EQ(smoke.type, Client::World::BreathParticle);
        EXPECT_EQ(smoke.subtype, Client::World::CrywolfSmokeSubtype);
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

// Los tipos sin caso (la bandera, las puertas, los que solo dibujan) no sacan rand() ni cambian el objeto; sin el
// rand() de la CRT, corta antes del switch.
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

// 0x4eff8d..0x4eff9f: el 30 aplasta la sombra con 2500; los demás mapas, con 2000.
TEST(CastleSiegeDrawingTest, OnlyTheCastleSiegeSpreadsTheShadowFurther)
{
    EXPECT_EQ(Client::Render::Models::ShadowSpreadOf(30), 2500.0f);
    for (const std::int32_t map : {0, 2, 29, 31, 34})
        EXPECT_EQ(Client::Render::Models::ShadowSpreadOf(map), 2000.0f) << map;
}

// Un esqueleto de un hueso en el origen, con una acción de una key.
Skeleton OneBone()
{
    Skeleton skeleton;
    Client::Render::Models::SkeletonBone bone;
    bone.parent = -1;
    bone.actions = {{Client::Render::Models::BoneKey{}}};
    bone.actions[0][0].rotation = Client::Render::Models::AngleQuaternion({0.0f, 0.0f, 0.0f});
    skeleton.bones.assign(1, bone);
    skeleton.actions = {
        Client::Render::Models::ActionInfo{.repeats = true, .speed = 0.0f, .keyCount = 1, .lockPositions = false}};
    return skeleton;
}

constexpr std::uint16_t SiegeModel = 0x4c;

// `meshes` mallas de un triángulo levantado 10 sobre el piso, cada una con la textura de su índice y el slot
// 0x300 más el índice.
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

    [[nodiscard]] const Model* Find(std::uint16_t model) const override
    {
        return model == SiegeModel ? &m_model : nullptr;
    }
    [[nodiscard]] const Skeleton* SkeletonOf(std::uint16_t model) const override
    {
        return model == SiegeModel ? &m_skeleton : nullptr;
    }
    [[nodiscard]] std::span<const TextureSlot> MeshTextures(std::uint16_t model) const override
    {
        return model == SiegeModel ? std::span<const TextureSlot>(m_slots) : std::span<const TextureSlot>{};
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

// (ftol(T) % 10.000) · 0,0002f.
float Scroll(std::int32_t clock)
{
    return static_cast<float>(static_cast<double>(clock % 10000) *
                              static_cast<double>(std::bit_cast<float>(0x3951b717u)));
}

// 0x42cfd6: los tipos sin caso van a 0x42e3d0, que con un objeto del mapa no dibuja nada; el que llama los
// dibuja como siempre.
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

// 0x42cc32..0x42cc89: la bandera, en color 1, con la malla 0 con su textura y la malla 1 con Flag (0x69c).
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

// 0x42cc9a..0x42ccf7: el 0x11 corre la V de la malla 0 con el reloj; 0x42cd0a..0x42cd5b: el 0x33 corre la U
// con el reloj negado (el resto lleva el signo del dividendo).
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

// 0x42cef9..0x42cf62: el 0x13 sin el asedio, el cuerpo y dos pases con Chrome01; con el asedio corta en 0x42ce09.
TEST(CastleSiegeDrawingTest, TheGlowingStoneGoesTwiceWithChromeOnlyWithoutTheSiege)
{
    const SiegeModels models(1);
    const Drawn drawn = DrawBody(models, SiegeObject(), {.type = 0x13});
    ASSERT_TRUE(drawn.result.HasValue());
    ASSERT_EQ(drawn.batch.Vertices().size(), 9u);
    const auto& commands = drawn.batch.Commands();
    ASSERT_GE(commands.size(), 2u);
    EXPECT_EQ(commands.front().state.texture, TextureSlot{0x300});
    EXPECT_EQ(commands.back().state.texture, Client::Scenes::ChromeTextureSlot);

    const Drawn siege = DrawBody(models, SiegeObject(), {.type = 0x13, .siegeActive = true});
    ASSERT_FALSE(siege.result.HasValue());
    EXPECT_NE(siege.result.Error().find("0x42ce09"), std::string::npos);
    EXPECT_TRUE(siege.batch.Vertices().empty());
}

// 0x42cf7b..0x42cfcb: las puertas 0x3d a 0x40 van con RenderBody si +0x4 está en 0; si no, corta en 0x4ee770.
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

// 0x42cfe7..0x42d038: el 0x32 y los 0x53 a 0x55, sin el asedio, el cuerpo y su sombra negra, aplastada desde el
// objeto con el spread 2500; con el asedio, el cuerpo solo.
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
            EXPECT_EQ(flat, Client::Render::Models::FlattenOnTheGround(body, object.transform.position, 2500.0f));
            EXPECT_NE(flat, Client::Render::Models::FlattenOnTheGround(body, object.transform.position, 2000.0f));
            EXPECT_FLOAT_EQ(vertices[corner + 3].color.red, 0.0f);
        }

        const Drawn siege = DrawBody(models, object, {.type = type, .siegeActive = true});
        ASSERT_TRUE(siege.result.HasValue());
        EXPECT_EQ(siege.batch.Vertices().size(), 3u);
    }
}

// Sin el modelo en la biblioteca, el caso no dibuja nada pero lo da por dibujado.
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
