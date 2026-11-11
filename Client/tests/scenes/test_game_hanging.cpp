#include "Scenes/GameHanging.h"

#include "Data/Models/Model.h"
#include "Render/ModelBatch.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/HalfLifeMath.h"
#include "Render/Models/Pose.h"
#include "Render/Models/Skeleton.h"
#include "Render/Pass3D.h"
#include "Render/TextureSlot.h"
#include "Scenes/ActorState.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/FaunaDrawing.h"
#include "Scenes/FaunaJointCreation.h"
#include "Scenes/GameDrawTail.h"
#include "World/IFishStepServices.h"
#include "World/Particles.h"
#include "World/Sprites.h"

#include <gtest/gtest.h>

#include <glm/ext/vector_float3.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// DrawGameHanging (0x50db57..0x50e9fa y 0x509340) sobre un cuerpo sintético: el hueso k del cuerpo y de
// cada modelo que cuelga está en (0, 0, k) sin giro, así cada punto deja ver de qué hueso sale. Los
// esperados salen del ASM de c32bbd5f, sin ejecutar el original.
namespace
{
using Client::Scenes::ActorState;
using Client::Scenes::GameHangingPass;

constexpr std::uint16_t Player = 0x20a;
constexpr std::uint16_t Sword = 0x212;
constexpr std::uint16_t BladeSword = 0x21d; // el destello a −150 de 0x50e921
constexpr std::uint16_t Sword224 = 0x224;
constexpr std::uint16_t Sword223 = 0x223;
constexpr std::uint16_t Bolts = 0xa19;
constexpr std::uint16_t Staff08 = 0xc19;
constexpr std::uint16_t Helper02 = 0x1c13;
constexpr std::uint16_t MuSign = 0x12a;
constexpr std::uint16_t Flare = 0x5ac;
constexpr std::uint16_t Shiny = 0x5fd;
constexpr std::size_t BoneCount = 48;
constexpr float PlayerSecondActionSpeed = 0.75f;

// El rand() de la CRT con la semilla 1 después de `count` llamadas.
std::uint32_t AfterRandoms(std::size_t count)
{
    Client::Render::Models::CrtRandom random{1};
    for (std::size_t call = 0; call < count; ++call)
        static_cast<void>(random.Next());
    return random.State();
}

double D(float value)
{
    return static_cast<double>(value);
}

float F32(double value)
{
    return static_cast<float>(value);
}

// La primera semilla con la que `condition` acepta los sorteos de un CrtRandom nuevo.
std::uint32_t SeedWhere(const std::function<bool(Client::Render::Models::CrtRandom&)>& condition)
{
    for (std::uint32_t seed = 1; seed < 100000; ++seed)
    {
        Client::Render::Models::CrtRandom random(seed);
        if (condition(random))
            return seed;
    }
    ADD_FAILURE() << "ninguna semilla cumple la condición";
    return 1;
}

// Un CrtRandom con la semilla `seed` después de `count` llamadas.
Client::Render::Models::CrtRandom Skipped(std::uint32_t seed, std::size_t count)
{
    Client::Render::Models::CrtRandom random(seed);
    for (std::size_t call = 0; call < count; ++call)
        static_cast<void>(random.Next());
    return random;
}

void ExpectSameJoints(std::span<const Client::Scenes::JointState> actual,
                      std::span<const Client::Scenes::JointState> expected)
{
    ASSERT_EQ(actual.size(), expected.size());
    for (std::size_t k = 0; k < actual.size(); ++k)
    {
        SCOPED_TRACE(k);
        const auto& a = actual[k];
        const auto& e = expected[k];
        ASSERT_EQ(a.alive, e.alive);
        if (!a.alive)
            continue;
        EXPECT_EQ(a.type, e.type);
        EXPECT_EQ(a.subtype, e.subtype);
        EXPECT_EQ(a.texture, e.texture);
        EXPECT_EQ(a.blend, e.blend);
        EXPECT_EQ(a.width, e.width);
        EXPECT_EQ(a.speed, e.speed);
        EXPECT_EQ(a.life, e.life);
        EXPECT_EQ(a.light, e.light);
        EXPECT_EQ(a.position, e.position);
        EXPECT_EQ(a.angles, e.angles);
        EXPECT_EQ(a.targetPosition, e.targetPosition);
        EXPECT_EQ(a.ownerContext, e.ownerContext);
        EXPECT_EQ(a.count, e.count);
        EXPECT_EQ(a.maximumCount, e.maximumCount);
        EXPECT_EQ(a.repeatedTexture, e.repeatedTexture);
        EXPECT_EQ(a.points, e.points);
    }
}

void ExpectSameParticles(const Client::World::Particles& actual, const Client::World::Particles& expected)
{
    for (std::size_t k = 0; k < Client::World::ParticleCount; ++k)
    {
        const auto& a = actual.All()[k];
        const auto& e = expected.All()[k];
        ASSERT_EQ(a.alive, e.alive) << k;
        if (!a.alive)
            continue;
        SCOPED_TRACE(k);
        EXPECT_EQ(a.type, e.type);
        EXPECT_EQ(a.subtype, e.subtype);
        EXPECT_EQ(a.position, e.position);
        EXPECT_EQ(a.angles, e.angles);
        EXPECT_EQ(a.light, e.light);
        EXPECT_EQ(a.scale, e.scale);
        EXPECT_EQ(a.life, e.life);
        EXPECT_EQ(a.velocity, e.velocity);
        EXPECT_EQ(a.verticalSpeed, e.verticalSpeed);
        EXPECT_EQ(a.field38, e.field38);
    }
}

// [ebp+0xc] de 0x50ba30 con el primer rand() de la semilla 1 (41): (41 % 30 + 70) · 0,01 en la x87.
float FirstLum()
{
    return static_cast<float>(static_cast<double>(81.0f) * static_cast<double>(0.01f));
}

class HangingModels final : public Client::Scenes::IModelLibrary
{
public:
    explicit HangingModels(std::set<std::uint16_t> items) : m_items(std::move(items))
    {
        using namespace Client::Render::Models;
        SkeletonBone bone;
        bone.parent = -1;
        // La acción 1 tiene dos keys: lo que cuelga en ella puede avanzar.
        bone.actions = {{BoneKey{}}, {BoneKey{}, BoneKey{}}};
        m_skeleton.bones.assign(BoneCount, bone);
        for (std::size_t index = 0; index < BoneCount; ++index)
        {
            for (auto& keys : m_skeleton.bones[index].actions)
            {
                for (auto& key : keys)
                    key.position = {0.0f, 0.0f, static_cast<float>(index)};
            }
        }
        m_skeleton.actions = {
            ActionInfo{.repeats = true, .speed = 0.0f, .keyCount = 1, .lockPositions = false},
            ActionInfo{.repeats = true, .speed = PlayerSecondActionSpeed, .keyCount = 2, .lockPositions = false}};
        Client::Data::Models::Mesh mesh;
        mesh.vertices = {{0, {0, 0, 0}}, {0, {1, 0, 0}}, {0, {0, 1, 0}}};
        mesh.normals = {{0, {0, 0, 1}, 0}};
        mesh.texCoords = {{0, 0}};
        mesh.triangles = {{3, {0, 1, 2, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}}};
        mesh.textureName = "item.jpg";
        m_model.meshes = {mesh};
    }

    [[nodiscard]] const Client::Data::Models::Model* Find(std::uint16_t model) const override
    {
        return m_items.contains(model) ? &m_model : nullptr;
    }
    [[nodiscard]] const Client::Render::Models::Skeleton* SkeletonOf(std::uint16_t model) const override
    {
        return model == Player || m_items.contains(model) ? &m_skeleton : nullptr;
    }
    [[nodiscard]] std::span<const Client::Render::TextureSlot> MeshTextures(std::uint16_t) const override
    {
        return m_texture;
    }

private:
    std::set<std::uint16_t> m_items;
    Client::Render::Models::Skeleton m_skeleton;
    Client::Data::Models::Model m_model;
    std::array<Client::Render::TextureSlot, 1> m_texture{Client::Render::TextureSlot{0x300}};
};

class GameHangingTest : public testing::Test
{
protected:
    void SetUp() override
    {
        m_batch.Begin(Client::Render::Pass3D{});
        for (std::size_t index = 0; index < BoneCount; ++index)
        {
            Client::Render::Models::BoneTransform bone(1.0f);
            bone[3] = {0.0f, 0.0f, static_cast<float>(index)};
            m_body.push_back(bone);
        }
        m_actor.type = Player;
        m_actor.word376 = 0xffff;
        m_actor.scale = 1.0f;
        m_actor.alpha = 1.0f;
        m_actor.position = {150.0f, 150.0f, 10.0f};
        m_actor.drawnLight = {0.5f, 0.5f, 0.5f};
        m_actor.wings.model = 0xffff;
        m_actor.pet.model = 0xffff;
        for (auto& hand : m_actor.hands)
            hand.model = 0xffff;
        m_actor.hands[0].bone = 0x21;
        m_actor.hands[1].bone = 0x2a;
    }

    GameHangingPass Pass(const Client::Scenes::IModelLibrary& models)
    {
        return {.batch = m_batch,
                .models = models,
                .body = m_body,
                .random = m_random,
                .carry = &m_carry,
                .sprites = &m_sprites,
                .joints = m_joints,
                .particles = &m_particles,
                .logicSteps = 1};
    }

    std::vector<Client::World::Sprite> LiveSprites()
    {
        std::vector<Client::World::Sprite> live;
        for (const auto& sprite : m_sprites.Sprites())
        {
            if (sprite.alive)
                live.push_back(sprite);
        }
        return live;
    }

    ActorState m_actor;
    std::vector<Client::Render::Models::BoneTransform> m_body;
    Client::Render::ModelBatch m_batch;
    Client::Render::Models::CrtRandom m_random{1};
    Client::Scenes::FaunaDrawCarry m_carry;
    Client::World::FrameSprites m_sprites;
    std::vector<Client::Scenes::JointState> m_joints = std::vector<Client::Scenes::JointState>(8);
    Client::World::Particles m_particles;
};

// Sin nada que cuelgue quedan el rand() de 0x50db57 y, fuera de la espalda, el cierre de +0x38e antes de
// cada mano. Sin buffer ni sprites no hace falta nada.
TEST_F(GameHangingTest, WithNothingHangingOnlyTheLightAndTheTwoHandBytesSpendRandoms)
{
    const HangingModels models({});
    GameHangingPass pass = Pass(models);
    pass.carry = nullptr;
    pass.sprites = nullptr;

    const auto result = Client::Scenes::DrawGameHanging(m_actor, pass);

    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_TRUE(m_batch.Vertices().empty());
    EXPECT_EQ(m_random.State(), AfterRandoms(3));
}

// 0x50ddde..0x50de60 con '4' en +0x38e (0x509926): el arma de la espalda usa la parte de las alas, con la
// velocidad de la acción 1 del jugador, y 0x509340 le deja la key anterior en 2 y el frame en 3,2.
// Al volver, 0x50de5a repone solo el frame de las alas: la key y la acción anteriores quedan (Q-C296).
TEST_F(GameHangingTest, TheBackWeaponLeavesTheWingsWithTheirFrameButItsPreviousKey)
{
    const HangingModels models({Sword});
    m_actor.safeZone = 1;
    m_actor.hands[0].model = Sword;
    m_actor.byte38e = 0x34;
    m_actor.animation.action = 0x30;
    m_actor.animation.frame = 6.0f;
    m_actor.wings.animation = {.action = 0, .frame = 7.5f, .previousAction = 9, .previousKey = 5};

    const auto result = Client::Scenes::DrawGameHanging(m_actor, Pass(models));

    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(m_actor.wings.bone, 0x2f);
    EXPECT_EQ(m_actor.wings.speed, PlayerSecondActionSpeed);
    EXPECT_EQ(m_actor.wings.animation.frame, 7.5f);
    EXPECT_EQ(m_actor.wings.animation.previousKey, 2);
    EXPECT_EQ(m_actor.wings.animation.previousAction, 9);
    // A la espalda no corre el lazo de las manos: el de 0x50db57, +0x38e en 0x509340 y la luz de 0x509a92.
    EXPECT_EQ(m_random.State(), AfterRandoms(3));
    ASSERT_EQ(m_batch.Vertices().size(), 3u);
    EXPECT_EQ(m_batch.Vertices()[0].position, glm::vec3(130.0f, 155.0f, 97.0f));
}

// 0x50dcda..0x50dd70 fuera de la espalda: las saetas de la izquierda van al hueso de las alas con el lugar
// de los arcos (70, 0, 90 y −10, 5, 10), y el lazo de las manos no las vuelve a dibujar.
TEST_F(GameHangingTest, BoltsInTheLeftHandHangOnTheBackOutsideTheSafeZone)
{
    const HangingModels models({Bolts});
    m_actor.hands[1].model = Bolts;

    const auto result = Client::Scenes::DrawGameHanging(m_actor, Pass(models));

    ASSERT_TRUE(result.HasValue()) << result.Error();
    ASSERT_EQ(m_batch.Vertices().size(), 3u);
    EXPECT_EQ(m_batch.Vertices()[0].position, glm::vec3(140.0f, 155.0f, 67.0f));
    // 0x50db57, +0x38e y la luz en 0x509340, y +0x38e antes de cada mano.
    EXPECT_EQ(m_random.State(), AfterRandoms(5));
}

// 0x50e896..0x50e9ea: el destello de 0x21d a −150 del hueso de la mano, con la luz del nivel 7
// (0,5, 0,4 y 0,3 de [ebp+0xc]). Los rojos (+0x375 desde 6) y +0x1d6 en 4 no lo llevan.
TEST_F(GameHangingTest, TheHandSwordFlareUsesTheLevelLight)
{
    const HangingModels models({BladeSword});
    m_actor.hands[0].model = BladeSword;
    m_actor.hands[0].level = 7;

    const auto result = Client::Scenes::DrawGameHanging(m_actor, Pass(models));

    ASSERT_TRUE(result.HasValue()) << result.Error();
    // Con el nivel 7, 0x60b3d0 dibuja la malla y su pasada del brillo.
    ASSERT_EQ(m_batch.Vertices().size(), 6u);
    EXPECT_EQ(m_batch.Vertices()[0].position, glm::vec3(150.0f, 150.0f, 43.0f));
    const auto sprites = LiveSprites();
    ASSERT_EQ(sprites.size(), 1u);
    const float lum = FirstLum();
    EXPECT_EQ(sprites[0].texture, Flare);
    EXPECT_EQ(sprites[0].position, glm::vec3(150.0f, 0.0f, 43.0f));
    EXPECT_EQ(sprites[0].scale, 1.4f);
    EXPECT_EQ(sprites[0].light, glm::vec3(lum * 0.5f, lum * 0.4f, lum * 0.3f));
    // 0x50db57, +0x38e de la mano y de 0x509340, la luz de 0x509a92 y +0x38e de la izquierda.
    EXPECT_EQ(m_random.State(), AfterRandoms(5));

    for (const bool red : {true, false})
    {
        SCOPED_TRACE(red);
        m_sprites.Clear();
        m_actor.pk = static_cast<std::uint8_t>(red ? 6 : 0);
        m_actor.level = static_cast<std::uint16_t>(red ? 0 : 4);
        ASSERT_TRUE(Client::Scenes::DrawGameHanging(m_actor, Pass(models)).HasValue());
        EXPECT_TRUE(LiveSprites().empty());
    }
}

// 0x50f6d6..0x50f74b: Staff08 lleva Shiny02 en el hueso 27 y destellos del 28 al 37 de su propia pose,
// que 0x509340 dejó en el buffer global (0x5092d0): su raíz es el hueso 33 del cuerpo. 0x607b60 le saca
// un rand() más.
TEST_F(GameHangingTest, TheStaffSpritesComeFromItsPoseInTheGlobalBuffer)
{
    const HangingModels models({Staff08});
    m_actor.hands[0].model = Staff08;

    const auto result = Client::Scenes::DrawGameHanging(m_actor, Pass(models));

    ASSERT_TRUE(result.HasValue()) << result.Error();
    const auto sprites = LiveSprites();
    ASSERT_EQ(sprites.size(), 11u);
    const float lum = FirstLum();
    const glm::vec3 light{lum * 0.4f, lum * 0.6f, lum};
    for (std::size_t index = 0; index < sprites.size(); ++index)
    {
        SCOPED_TRACE(index);
        EXPECT_EQ(sprites[index].texture, index == 0 ? Shiny : Flare);
        EXPECT_EQ(sprites[index].scale, index == 0 ? 2.0f : 1.5f);
        EXPECT_EQ(sprites[index].position, glm::vec3(150.0f, 150.0f, static_cast<float>(10 + 33 + 27 + index)));
        EXPECT_EQ(sprites[index].light, light);
    }
    EXPECT_EQ(m_random.State(), AfterRandoms(6));
}

// 0x50a005..0x50a138: el 0x227 en la acción 0x8c, fuera del mapa 10 y con un rand() par, corre el hueso 1 de su pose
// (rand() % 30 − 15 en x e y, +20 en z) y suelta cuatro veces el joint 0x617/0 (escala 10, sin dueño) y la partícula
// 0x5c5/0 blanca, cada par con los ángulos (rand() % 60 + 150; 0; z del personaje). En el mapa 10 no saca el rand().
TEST_F(GameHangingTest, TheAction8cSwordThrowsFourSparksFromItsFirstBone)
{
    constexpr std::uint16_t Sword227 = 0x227;
    // 0x50db57, +0x38e de la mano y de 0x509340 y la luz de 0x509a92, antes del rand() & 1.
    constexpr std::size_t Before = 4;
    const std::uint32_t seed = SeedWhere(
        [](Client::Render::Models::CrtRandom& random)
        {
            for (std::size_t call = 0; call < Before; ++call)
                static_cast<void>(random.Next());
            return (random.Next() & 1) == 0;
        });
    const HangingModels models({Sword227});
    m_actor.hands[0].model = Sword227;
    m_actor.animation.action = 0x8c;
    m_actor.angles = {0.0f, 0.0f, 135.0f};
    m_random = Client::Render::Models::CrtRandom{seed};

    const auto result = Client::Scenes::DrawGameHanging(m_actor, Pass(models));

    ASSERT_TRUE(result.HasValue()) << result.Error();
    Client::Render::Models::CrtRandom draws = Skipped(seed, Before + 1);
    // El hueso 1 de la espada, colgada del 0x21 de la mano: (150; 150; 10 + 33 + 1).
    glm::vec3 point{150.0f, 150.0f, 44.0f};
    point.x = F32(static_cast<double>(draws.Next() % 30) - 15.0 + D(point.x));
    point.y = F32(static_cast<double>(draws.Next() % 30) - 15.0 + D(point.y));
    point.z = 64.0f;
    std::vector<Client::Scenes::JointState> joints(m_joints.size());
    Client::World::Particles particles;
    for (int spark = 0; spark < 4; ++spark)
    {
        glm::vec3 angles{static_cast<float>(draws.Next() % 60 + 150), 0.0f, 135.0f};
        const Client::World::FishEffectRequest request{0x617, point, point, angles, 0, nullptr, 10.0f, -1, 0, 0, -1};
        ASSERT_TRUE(Client::Scenes::CreateFaunaJoint(joints, request, draws).HasValue());
        ASSERT_TRUE(particles.CreateChecked(0x5c5, point, angles, glm::vec3(1.0f), 0, 1.0f, 0.0f, draws).HasValue());
    }
    static_cast<void>(draws.Next()); // +0x38e de la izquierda.
    EXPECT_EQ(m_random.State(), draws.State());
    ExpectSameJoints(m_joints, joints);
    ExpectSameParticles(m_particles, particles);
    for (std::size_t k = 0; k < 4; ++k)
    {
        SCOPED_TRACE(k);
        EXPECT_EQ(m_joints[k].type, 0x617);
        EXPECT_EQ(m_joints[k].position, point);
        EXPECT_EQ(m_joints[k].angles.z, 135.0f);
        EXPECT_EQ(m_particles.All()[k].type, 0x5c5);
        EXPECT_EQ(m_particles.All()[k].position, point);
    }
    EXPECT_FALSE(m_joints[4].alive);

    m_random = Client::Render::Models::CrtRandom{seed};
    GameHangingPass pass = Pass(models);
    pass.map = 10;
    ASSERT_TRUE(Client::Scenes::DrawGameHanging(m_actor, pass).HasValue());
    EXPECT_EQ(m_random.State(), Skipped(seed, Before + 1).State());
}

// 0x50af03..0x50b0a0: el arco 0xa27, con un rand() impar, suelta la chispa 0x5c6/0xb de 0,8 en el hueso 4 y en el 12
// de su pose, con la luz (0,8; 0,8; 0,2) y los ángulos del personaje; con uno par, no. Los cinco destellos van igual.
TEST_F(GameHangingTest, TheBowSparksFromBones4And12WithHalfTheRandoms)
{
    constexpr std::uint16_t Bow = 0xa27;
    constexpr std::size_t Before = 4;
    for (const bool sparks : {true, false})
    {
        SCOPED_TRACE(sparks);
        const std::uint32_t seed = SeedWhere(
            [sparks](Client::Render::Models::CrtRandom& random)
            {
                for (std::size_t call = 0; call < Before; ++call)
                    static_cast<void>(random.Next());
                return ((random.Next() & 1) == 1) == sparks;
            });
        const HangingModels models({Bow});
        m_actor.hands[0].model = Bow;
        m_actor.angles = {0.0f, 0.0f, 60.0f};
        m_random = Client::Render::Models::CrtRandom{seed};
        m_particles = Client::World::Particles{};
        m_sprites.Clear();

        const auto result = Client::Scenes::DrawGameHanging(m_actor, Pass(models));

        ASSERT_TRUE(result.HasValue()) << result.Error();
        Client::Render::Models::CrtRandom draws = Skipped(seed, Before + 1);
        Client::World::Particles particles;
        if (sparks)
        {
            for (const float z : {47.0f, 55.0f})
                ASSERT_TRUE(particles
                                .CreateChecked(0x5c6, {150.0f, 150.0f, z}, m_actor.angles, {0.8f, 0.8f, 0.2f}, 11, 0.8f,
                                               0.0f, draws)
                                .HasValue());
        }
        static_cast<void>(draws.Next()); // +0x38e de la izquierda.
        EXPECT_EQ(m_random.State(), draws.State());
        ExpectSameParticles(m_particles, particles);
        EXPECT_EQ(LiveSprites().size(), 5u);
    }
}

// 0x50f454..0x50f676: el bastón 0xc18 en la mano lleva Spark02 y Shiny03 a −145 y, después, cuatro chispas 0x5c5/1
// corridas (rand() % 20 − 10; rand() % 20 − 100; rand() % 20 − 10) con la luz (l; l · 0,6; l · 0,4) y los ángulos del
// personaje, y de 0 a −180 un destello de 1 en (0; y + 60; 0) con tres de cada cuatro rand(), con (l; l · 0,2; l ·
// 0,1).
TEST_F(GameHangingTest, TheStaffC18SparksFourTimesAndFlaresAlongTheHand)
{
    constexpr std::uint16_t Staff = 0xc18;
    const HangingModels models({Staff});
    m_actor.hands[0].model = Staff;
    m_actor.angles = {0.0f, 0.0f, 30.0f};

    const auto result = Client::Scenes::DrawGameHanging(m_actor, Pass(models));

    ASSERT_TRUE(result.HasValue()) << result.Error();
    const float lum = FirstLum();
    const glm::vec3 light{lum, lum * 0.6f, lum * 0.4f};
    // 0x50db57, +0x38e de la mano y de 0x509340 y la luz de 0x509a92.
    Client::Render::Models::CrtRandom draws = Skipped(1, 4);
    Client::World::Particles particles;
    for (int spark = 0; spark < 4; ++spark)
    {
        const auto x = static_cast<float>(draws.Next() % 20 - 10);
        const auto y = static_cast<float>(draws.Next() % 20 - 10 - 90);
        const auto z = static_cast<float>(draws.Next() % 20 - 10);
        const glm::vec3 point{150.0f + x, 150.0f + y, 43.0f + z};
        ASSERT_TRUE(particles.CreateChecked(0x5c5, point, m_actor.angles, light, 1, 1.0f, 0.0f, draws).HasValue());
    }
    std::vector<float> flares;
    for (int y = 0; y > -200; y -= 20)
    {
        if ((draws.Next() & 3) < 3)
            flares.push_back(static_cast<float>(y + 60));
    }
    static_cast<void>(draws.Next()); // +0x38e de la izquierda.
    EXPECT_EQ(m_random.State(), draws.State());
    ExpectSameParticles(m_particles, particles);
    for (std::size_t k = 0; k < 4; ++k)
    {
        EXPECT_EQ(m_particles.All()[k].subtype, 1);
        EXPECT_GE(m_particles.All()[k].life, 8);
        EXPECT_LE(m_particles.All()[k].life, 15);
    }
    const auto sprites = LiveSprites();
    ASSERT_EQ(sprites.size(), 2 + flares.size());
    EXPECT_EQ(sprites[0].texture, 0x5c5);
    EXPECT_EQ(sprites[0].position, glm::vec3(150.0f, 5.0f, 43.0f));
    EXPECT_EQ(sprites[0].scale, 3.0f);
    EXPECT_EQ(sprites[0].light, light);
    EXPECT_EQ(sprites[1].texture, 0x5fe);
    for (std::size_t k = 0; k < flares.size(); ++k)
    {
        SCOPED_TRACE(k);
        EXPECT_EQ(sprites[2 + k].texture, Flare);
        EXPECT_EQ(sprites[2 + k].position, glm::vec3(150.0f, 150.0f + flares[k], 43.0f));
        EXPECT_EQ(sprites[2 + k].scale, 1.0f);
        EXPECT_EQ(sprites[2 + k].light, glm::vec3(lum, lum * 0.2f, lum * 0.1f));
    }
}

// 0x50ed00..0x50ee71: la espada 0x224 en la mano lleva Shiny02 en (0; −20; 15) y suelta desde ahí tres joints 0x614/10
// sin dueño hasta (0; −133; 7), con los ángulos del personaje y el ancho v · 20 + 20, con v = (sin(reloj · 0,004f) +
// 1) · 0,3f.
TEST_F(GameHangingTest, TheSword224ThrowsThreeBeamsAlongTheBlade)
{
    const HangingModels models({Sword224});
    m_actor.hands[0].model = Sword224;
    m_actor.angles = {0.0f, 0.0f, 90.0f};
    GameHangingPass pass = Pass(models);
    pass.worldClock = 0.0f;

    const auto result = Client::Scenes::DrawGameHanging(m_actor, pass);

    ASSERT_TRUE(result.HasValue()) << result.Error();
    // Con el reloj en 0: v = 0,3f y el ancho 0,3f · 20 + 20.
    const float v = F32(1.0 * D(0.3f));
    const float width = F32(D(v) * 20.0 + 20.0);
    glm::vec3 first{150.0f, 130.0f, 58.0f};
    glm::vec3 second{150.0f, 17.0f, 50.0f};
    glm::vec3 angles = m_actor.angles;
    Client::Render::Models::CrtRandom draws = Skipped(1, 4);
    std::vector<Client::Scenes::JointState> joints(m_joints.size());
    for (int beam = 0; beam < 3; ++beam)
    {
        const Client::World::FishEffectRequest request{0x614, first, second, angles, 10, nullptr, width, -1, 0, 0, -1};
        ASSERT_TRUE(Client::Scenes::CreateFaunaJoint(joints, request, draws).HasValue());
    }
    static_cast<void>(draws.Next()); // +0x38e de la izquierda.
    EXPECT_EQ(m_random.State(), draws.State());
    ExpectSameJoints(m_joints, joints);
    for (std::size_t k = 0; k < 3; ++k)
    {
        SCOPED_TRACE(k);
        EXPECT_EQ(m_joints[k].type, 0x614);
        EXPECT_EQ(m_joints[k].subtype, 10);
        EXPECT_EQ(m_joints[k].width, width);
        EXPECT_EQ(m_joints[k].position, second);
    }
    EXPECT_FALSE(m_joints[3].alive);
    ASSERT_EQ(LiveSprites().size(), 1u);
    EXPECT_EQ(LiveSprites()[0].texture, Shiny);
    EXPECT_EQ(LiveSprites()[0].position, first);
}

// 0x50eb43..0x50ecf0: la espada 0x223. Dos joints 0x610/4 de la mano, con el personaje como dueño y sus ángulos, sin
// rand() ni sprites. Con el reloj en 0, el ancho es 20.
TEST_F(GameHangingTest, TheSword223LeavesTwoTrailsOwnedByTheCharacter)
{
    const HangingModels models({Sword223});
    m_actor.hands[0].model = Sword223;
    m_actor.angles = {0.0f, 0.0f, 90.0f};
    GameHangingPass pass = Pass(models);
    pass.worldClock = 0.0f;

    const auto result = Client::Scenes::DrawGameHanging(m_actor, pass);

    ASSERT_TRUE(result.HasValue()) << result.Error();
    // (0; −20; −40) a (0; −160; −10) y (0; −10; 28) a (0; −145; 18) del hueso 0x21: (150 + x; 150 + y; 43 + z).
    const std::array<std::pair<glm::vec3, glm::vec3>, 2> edges{{
        {{150.0f, 130.0f, 3.0f}, {150.0f, -10.0f, 33.0f}},
        {{150.0f, 140.0f, 71.0f}, {150.0f, 5.0f, 61.0f}},
    }};
    glm::vec3 angles = m_actor.angles;
    std::vector<Client::Scenes::JointState> joints(m_joints.size());
    Client::Render::Models::CrtRandom unused(1);
    for (auto [first, second] : edges)
    {
        const Client::World::FishEffectRequest request{0x610, first, second, angles, 4, &m_actor, 20.0f, -1, 0, 0, -1};
        ASSERT_TRUE(Client::Scenes::CreateFaunaJoint(joints, request, unused).HasValue());
    }
    EXPECT_EQ(unused.State(), Client::Render::Models::CrtRandom(1).State());
    EXPECT_EQ(m_random.State(), AfterRandoms(5));
    ExpectSameJoints(m_joints, joints);
    for (std::size_t k = 0; k < edges.size(); ++k)
    {
        SCOPED_TRACE(k);
        EXPECT_EQ(m_joints[k].type, 0x610);
        EXPECT_EQ(m_joints[k].subtype, 4);
        EXPECT_EQ(m_joints[k].width, 20.0f);
        EXPECT_EQ(m_joints[k].ownerContext, &m_actor);
        EXPECT_EQ(m_joints[k].targetPosition, edges[k].second);
    }
    EXPECT_FALSE(m_joints[2].alive);
    EXPECT_TRUE(LiveSprites().empty());
}

// 0x50b411..0x50b763: el bastón 0xc1e. Spark03 en (0; p − 170; p) del hueso 0 de su pose, que queda en +0x40 del
// personaje y donde nace el joint 0x618/0x11 con el personaje como dueño; otra p sin efecto y la escala t; un destello,
// dos Shiny02 y la chispa 0x5c6/0xb de 2 con la luz (1; 0,4; 1); y seis Shiny02 en los huesos 2 y 1 con (a; 0,1; a),
// girados g y g + 90. Con el reloj en 0, a = 0,5 − 0,1f y g = 0.
TEST_F(GameHangingTest, TheStaffC1eLeavesItsPointOnTheCharacterAndAJointThatFollowsIt)
{
    constexpr std::uint16_t Staff = 0xc1e;
    const HangingModels models({Staff});
    m_actor.hands[0].model = Staff;
    m_actor.angles = {0.0f, 0.0f, 45.0f};

    const auto result = Client::Scenes::DrawGameHanging(m_actor, Pass(models));

    ASSERT_TRUE(result.HasValue()) << result.Error();
    Client::Render::Models::CrtRandom draws = Skipped(1, 4);
    const float p = F32(static_cast<double>(draws.Next() % 60) * D(0.05f) - D(1.5f));
    const float s = F32(static_cast<double>(draws.Next() % 15) * D(0.05f) + D(1.8f));
    // El hueso 0 del bastón, colgado del 0x21 de la mano: (150; 150; 43).
    glm::vec3 point{150.0f, F32(D(F32(D(p) - 170.0)) + 150.0), F32(D(p) + 43.0)};
    static_cast<void>(draws.Next() % 60);
    const float t = F32(static_cast<double>(draws.Next() % 15) * D(0.05f) + 1.0);
    glm::vec3 angles = m_actor.angles;
    std::vector<Client::Scenes::JointState> joints(m_joints.size());
    const Client::World::FishEffectRequest request{0x618, point, point, angles, 0x11, &m_actor, 30.0f, -1, 0, 0, -1};
    Client::Render::Models::CrtRandom unused(1);
    ASSERT_TRUE(Client::Scenes::CreateFaunaJoint(joints, request, unused).HasValue());
    const glm::vec3 violet{1.0f, 0.4f, 1.0f};
    Client::World::Particles particles;
    ASSERT_TRUE(particles.CreateChecked(0x5c6, point, angles, violet, 11, 2.0f, 0.0f, draws).HasValue());
    static_cast<void>(draws.Next()); // +0x38e de la izquierda.
    EXPECT_EQ(m_random.State(), draws.State());
    EXPECT_EQ(m_actor.effectOrigin, point);
    ExpectSameJoints(m_joints, joints);
    EXPECT_EQ(m_joints[0].type, 0x618);
    EXPECT_EQ(m_joints[0].subtype, 0x11);
    EXPECT_EQ(m_joints[0].position, point);
    EXPECT_EQ(m_joints[0].ownerContext, &m_actor);
    EXPECT_EQ(m_joints[0].life, 12);
    ExpectSameParticles(m_particles, particles);
    const auto sprites = LiveSprites();
    ASSERT_EQ(sprites.size(), 10u);
    EXPECT_EQ(sprites[0].texture, 0x5c6);
    EXPECT_EQ(sprites[0].position, point);
    EXPECT_EQ(sprites[0].scale, s);
    EXPECT_EQ(sprites[0].light, glm::vec3(0.4f));
    EXPECT_EQ(sprites[1].texture, Flare);
    EXPECT_EQ(sprites[1].scale, t);
    EXPECT_EQ(sprites[1].light, violet);
    EXPECT_EQ(sprites[2].texture, Shiny);
    EXPECT_EQ(sprites[2].scale, t);
    EXPECT_EQ(sprites[2].rotation, 0.0f);
    EXPECT_EQ(sprites[3].scale, F32(D(t) - D(0.3f)));
    EXPECT_EQ(sprites[3].rotation, 90.0f);
    const float a = F32(0.5 - D(0.1f));
    const std::array<glm::vec3, 3> shines{glm::vec3{150.0f, 160.0f, 45.0f}, glm::vec3{110.0f, 140.0f, 44.0f},
                                          glm::vec3{-10.0f, 145.0f, 44.0f}};
    const std::array<float, 6> scales{1.5f, 1.2f, 1.0f, 0.7f, 1.2f, 1.0f};
    for (std::size_t k = 0; k < 6; ++k)
    {
        SCOPED_TRACE(k);
        EXPECT_EQ(sprites[4 + k].texture, Shiny);
        EXPECT_EQ(sprites[4 + k].position, shines[k / 2]);
        EXPECT_EQ(sprites[4 + k].scale, scales[k]);
        EXPECT_EQ(sprites[4 + k].light, glm::vec3(a, 0.1f, a));
        EXPECT_EQ(sprites[4 + k].rotation, k % 2 == 0 ? 0.0f : 90.0f);
    }
}

// 0x50df80..0x50e05d: Helper02 cuelga del hueso 34 a (20, 0, 0) a media key por frame, y después
// 0x4ec110 con (20, 0, 15) en el mismo hueso lleva un destello rojo de 1,5.
TEST_F(GameHangingTest, Helper02HangsFromBone34WithItsRedFlare)
{
    const HangingModels models({Helper02});
    m_actor.pet.model = Helper02;

    const auto result = Client::Scenes::DrawGameHanging(m_actor, Pass(models));

    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(m_actor.pet.bone, 0x22);
    EXPECT_EQ(m_actor.pet.speed, 0.5f);
    ASSERT_EQ(m_batch.Vertices().size(), 3u);
    EXPECT_EQ(m_batch.Vertices()[0].position, glm::vec3(170.0f, 150.0f, 44.0f));
    const auto sprites = LiveSprites();
    ASSERT_EQ(sprites.size(), 1u);
    EXPECT_EQ(sprites[0].texture, Flare);
    EXPECT_EQ(sprites[0].position, glm::vec3(170.0f, 150.0f, 59.0f));
    EXPECT_EQ(sprites[0].scale, 1.5f);
    EXPECT_EQ(sprites[0].light, glm::vec3(FirstLum() * 0.5f, 0.0f, 0.0f));
    EXPECT_EQ(m_random.State(), AfterRandoms(5));
}

// 0x50df22: en los mapas 18 a 23 no cuelgan ni las alas ni la mascota.
TEST_F(GameHangingTest, TheEventMapsSkipTheWingsAndThePet)
{
    const HangingModels models({Helper02});
    m_actor.pet.model = Helper02;
    GameHangingPass pass = Pass(models);
    pass.map = 20;

    const auto result = Client::Scenes::DrawGameHanging(m_actor, pass);

    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_TRUE(m_batch.Vertices().empty());
    EXPECT_TRUE(LiveSprites().empty());
    EXPECT_EQ(m_random.State(), AfterRandoms(3));
}

// 0x413fe0, 0x4142f0 y 0x414470: con el CtlCode en 0x20, +0x360 pasa a 5 y la capa nueva dibuja MuSign en el
// hueso 20 del cuerpo, corrido (70, −5, 0) y girado 45° en z, sin rand(): el pase base (luz por vértice) y el
// cromo aditivo en (1; 0,5; 0). Su pose queda en el buffer global, y las alas se posan después.
TEST_F(GameHangingTest, AGameMasterCarriesTheSignOnBone20)
{
    const HangingModels models({MuSign});
    m_actor.controlCode = 0x20;

    const auto result = Client::Scenes::DrawGameHanging(m_actor, Pass(models));

    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(m_actor.byte360, 5);
    ASSERT_TRUE(m_actor.layer.has_value());
    EXPECT_EQ(m_actor.layer->animation.action, 0);
    EXPECT_EQ(m_random.State(), AfterRandoms(3));
    // (70, −5, 0) desde el hueso 20 en (0, 0, 20), más el lugar (150, 150, 10).
    const glm::vec3 origin{220.0f, 145.0f, 30.0f};
    ASSERT_TRUE(m_carry.models.contains(MuSign));
    EXPECT_EQ(m_carry.models.at(MuSign).position, origin);
    EXPECT_EQ(m_carry.models.at(MuSign).angles, glm::vec3(0.0f, 0.0f, 45.0f));
    const auto vertices = m_batch.Vertices();
    ASSERT_EQ(vertices.size(), 6u);
    for (const std::size_t first : {std::size_t{0}, std::size_t{3}})
    {
        EXPECT_NEAR(vertices[first].position.x, origin.x, 1e-3f);
        EXPECT_NEAR(vertices[first].position.y, origin.y, 1e-3f);
        EXPECT_NEAR(vertices[first].position.z, origin.z, 1e-3f);
        // El (1, 0, 0) de la malla, girado 45° en z.
        EXPECT_NEAR(vertices[first + 1].position.x, origin.x + 0.70710677f, 1e-3f);
        EXPECT_NEAR(vertices[first + 1].position.y, origin.y + 0.70710677f, 1e-3f);
    }
    EXPECT_NEAR(vertices[3].color.red, 1.0f, 1e-6f);
    EXPECT_NEAR(vertices[3].color.green, 0.5f, 1e-6f);
    EXPECT_NEAR(vertices[3].color.blue, 0.0f, 1e-6f);
}

// La capa queda en el registro: con +0x360 en 5 y sin ser GM se sigue dibujando, y 0x4ec200 la avanza 0,5
// por frame del original (entre frames, como quedó). Sin capa y con el 5 de un GM anterior, la vuelve a crear
// (Q-C301).
TEST_F(GameHangingTest, TheSignStaysInTheRecordAndAdvancesOnlyOnOriginalFrames)
{
    const HangingModels models({MuSign});
    m_actor.byte360 = 5;
    m_actor.layer = Client::Scenes::GameMasterLayer{.animation = {.action = 1}};
    GameHangingPass pass = Pass(models);
    pass.originalFrame = false;

    ASSERT_TRUE(Client::Scenes::DrawGameHanging(m_actor, pass).HasValue());
    ASSERT_TRUE(m_actor.layer.has_value());
    EXPECT_EQ(m_actor.layer->animation.frame, 0.0f);

    pass.originalFrame = true;
    ASSERT_TRUE(Client::Scenes::DrawGameHanging(m_actor, pass).HasValue());
    ASSERT_TRUE(m_actor.layer.has_value());
    EXPECT_EQ(m_actor.layer->animation.action, 1);
    EXPECT_EQ(m_actor.layer->animation.frame, 0.5f);

    m_actor.layer.reset();
    ASSERT_TRUE(Client::Scenes::DrawGameHanging(m_actor, pass).HasValue());
    ASSERT_TRUE(m_actor.layer.has_value());
    EXPECT_EQ(m_actor.layer->animation.action, 0);
    EXPECT_EQ(m_actor.byte360, 5);
}

// Un GM con la capa de otro tipo la suelta (0x414340 vuelve +0x360 a 0) y crea la 5.
TEST_F(GameHangingTest, AGameMasterReplacesALayerOfAnotherKind)
{
    const HangingModels models({MuSign});
    m_actor.controlCode = 8;
    m_actor.byte360 = 2;
    m_actor.layer = Client::Scenes::GameMasterLayer{.animation = {.action = 1, .frame = 0.75f}};

    ASSERT_TRUE(Client::Scenes::DrawGameHanging(m_actor, Pass(models)).HasValue());

    EXPECT_EQ(m_actor.byte360, 5);
    ASSERT_TRUE(m_actor.layer.has_value());
    EXPECT_EQ(m_actor.layer->animation.action, 0);
    EXPECT_EQ(m_actor.layer->animation.frame, 0.0f);
}

// 0x414470 y 0x5ffdf0 en Chaos Castle: sin +0x1a1 del héroe la capa se dibuja como en otro mapa; con él, cada frame
// del original le baja 0,15f al alpha guardado (un dibujo intermedio no), lo dibuja con ese alpha y, cuando queda
// por debajo de 0,01, ni la posa ni la dibuja.
TEST_F(GameHangingTest, TheSignFadesInChaosCastleWithTheHerosFlag)
{
    const HangingModels models({MuSign});
    m_actor.byte360 = 5;
    m_actor.layer = Client::Scenes::GameMasterLayer{};
    GameHangingPass pass = Pass(models);
    pass.map = 20;

    ASSERT_TRUE(Client::Scenes::DrawGameHanging(m_actor, pass).HasValue());
    ASSERT_TRUE(m_actor.layer.has_value());
    EXPECT_EQ(m_actor.layer->alpha, 1.0f);
    EXPECT_EQ(m_batch.Vertices().size(), 6u);

    pass.heroByte1a1 = 1;
    pass.originalFrame = false;
    ASSERT_TRUE(Client::Scenes::DrawGameHanging(m_actor, pass).HasValue());
    EXPECT_EQ(m_actor.layer->alpha, 1.0f);
    pass.originalFrame = true;
    float expected = 1.0f;
    for (int frame = 0; frame < 6; ++frame)
    {
        SCOPED_TRACE(frame);
        const std::size_t before = m_batch.Vertices().size();
        ASSERT_TRUE(Client::Scenes::DrawGameHanging(m_actor, pass).HasValue());
        expected = static_cast<float>(static_cast<double>(expected) - static_cast<double>(0.15f));
        EXPECT_EQ(m_actor.layer->alpha, expected);
        EXPECT_GT(m_batch.Vertices().size(), before);
    }
    const std::size_t drawn = m_batch.Vertices().size();
    m_carry.models.clear();
    ASSERT_TRUE(Client::Scenes::DrawGameHanging(m_actor, pass).HasValue());
    EXPECT_LT(m_actor.layer->alpha, 0.01f);
    EXPECT_EQ(m_batch.Vertices().size(), drawn);
    EXPECT_FALSE(m_carry.models.contains(MuSign));
    const float faded = m_actor.layer->alpha;
    ASSERT_TRUE(Client::Scenes::DrawGameHanging(m_actor, pass).HasValue());
    EXPECT_EQ(m_actor.layer->alpha, faded);
}

// Lo que todavía corta, con su dirección y después de lo ya dibujado: la capa de un GM por el nombre sin MuSign
// (0x414470), el arma del evento de Blood Castle (0x50de84), las capas del 6 al 13 (0x4140d5) y lo que no es un
// jugador (0x50e060).
TEST_F(GameHangingTest, TheUnportedCasesStopWithTheirAddress)
{
    {
        const HangingModels models({});
        m_actor.hands[0].model = 0xffff;
        constexpr std::string_view Name = "xwebzenx";
        for (std::size_t index = 0; index < Name.size(); ++index)
            m_actor.name[index] = static_cast<std::byte>(Name[index]);
        const auto result = Client::Scenes::DrawGameHanging(m_actor, Pass(models));
        ASSERT_FALSE(result.HasValue());
        EXPECT_NE(result.Error().find("0x414470"), std::string::npos) << result.Error();
        EXPECT_EQ(m_actor.byte360, 5);
        m_actor.name = {};
    }
    {
        const HangingModels models({MuSign});
        GameHangingPass pass = Pass(models);
        pass.map = 11;
        const auto result = Client::Scenes::DrawGameHanging(m_actor, pass);
        ASSERT_FALSE(result.HasValue());
        EXPECT_NE(result.Error().find("0x50de84"), std::string::npos) << result.Error();
        m_actor.layer.reset();
        m_actor.byte360 = 6;
        const auto other = Client::Scenes::DrawGameHanging(m_actor, Pass(models));
        ASSERT_FALSE(other.HasValue());
        EXPECT_NE(other.Error().find("0x4140d5"), std::string::npos) << other.Error();
        m_actor.byte360 = 0;
    }
    {
        const HangingModels models({});
        m_actor.type = 0x20b;
        m_random = Client::Render::Models::CrtRandom{1};
        const auto result = Client::Scenes::DrawGameHanging(m_actor, Pass(models));
        ASSERT_FALSE(result.HasValue());
        EXPECT_NE(result.Error().find("0x50e060"), std::string::npos) << result.Error();
        EXPECT_EQ(m_random.State(), AfterRandoms(1));
    }
}

// Entre frames del original (0x509340 con la animación como quedó), lo que cuelga no avanza.
TEST_F(GameHangingTest, BetweenOriginalFramesTheHangingAnimationDoesNotAdvance)
{
    const HangingModels models({Helper02});
    m_actor.pet.model = Helper02;
    m_actor.pet.animation = {.action = 1, .frame = 0.25f, .previousAction = 0, .previousKey = 0};
    GameHangingPass pass = Pass(models);
    pass.originalFrame = false;

    ASSERT_TRUE(Client::Scenes::DrawGameHanging(m_actor, pass).HasValue());
    EXPECT_EQ(m_actor.pet.animation.frame, 0.25f);

    pass.originalFrame = true;
    ASSERT_TRUE(Client::Scenes::DrawGameHanging(m_actor, pass).HasValue());
    EXPECT_EQ(m_actor.pet.animation.frame, 0.75f);
}
} // namespace
