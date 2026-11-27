#include "Models/IModelLibrary.h"
#include "Scenes/MonsterBody.h"

#include "Core/Expected.h"
#include "Core/IClock.h"
#include "Crt/CrtRandom.h"
#include "Data/Models/Model.h"
#include "Render/ModelBatch.h"
#include "Models/Cloth.h"
#include "Math/HalfLifeMath.h"
#include "Math/RoundedMath.h"
#include "Models/Skeleton.h"
#include "Models/Skinning.h"
#include "Render/Pass3D.h"
#include "Render/TextureSlot.h"
#include "Kernel/ActorState.h"
#include "Actors/Drawing/CharacterDrawing.h"
#include "Scenes/FaunaJointCreation.h"
#include "Scenes/GameDrawTail.h"
#include "Scenes/MonsterBodyPasses.h"
#include "Actors/Drawing/VertexEffects.h"
#include "World/IFishStepServices.h"
#include "World/ModelEffects.h"
#include "World/Particles.h"

#include <gtest/gtest.h>

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

// Los casos por tipo y por mapa de 0x6002e0 con un monstruo o un NPC (MonsterBody.h): vectores literales del ASM
// c32bbd5f, sin ejecutar el original. Un modelo sintético de siete mallas de un triángulo, cada una con su slot (0x300
// más su índice), deja ver qué pase dibuja qué malla con qué textura, qué color y qué mezcla.
namespace
{
using Client::Math::RoundedSin;
using Client::Render::ModelBlend;
using Client::Scenes::ActorState;

constexpr std::size_t MeshCount = 7;
constexpr std::size_t Triangle = 3;
constexpr std::size_t WholeBody = MeshCount * Triangle;

std::uint32_t AfterRandoms(std::size_t count)
{
    Client::Crt::CrtRandom random{1};
    for (std::size_t call = 0; call < count; ++call)
        static_cast<void>(random.Next());
    return random.State();
}

// La tabla de joints del original (0x74a8d78, 500 lugares).
constexpr std::size_t KundunJointTable = 500;

// Gemela de 0x43f1a6..0x43f243, escrita aparte: 0x56 vueltas con su rand() % 10, los ángulos (−10, 0, (r + 10) · i)
// y el punto (x, y, z + 200) para el 0x613/3 y el 0x6ee/3 de ancho 50, sin dueño ni blanco.
std::unique_ptr<std::vector<Client::Scenes::JointState>> KundunFallingRays(const glm::vec3& position,
                                                                           Client::Crt::CrtRandom& random)
{
    auto joints = std::make_unique<std::vector<Client::Scenes::JointState>>(KundunJointTable);
    glm::vec3 angles{-10.0f, 0.0f, 0.0f};
    glm::vec3 point{};
    for (std::int32_t ray = 0; ray < 0x56; ++ray)
    {
        const std::int32_t spread = random.Next() % 10;
        point = {position.x, position.y, static_cast<float>(static_cast<double>(position.z) + 200.0)};
        angles.z = static_cast<float>((static_cast<double>(spread) + 10.0) * static_cast<double>(ray));
        for (const std::int32_t type : {0x613, 0x6ee})
        {
            const Client::World::FishEffectRequest request{.type = type,
                                                           .firstPosition = point,
                                                           .secondPosition = point,
                                                           .angles = angles,
                                                           .subtype = 3,
                                                           .ownerContext = nullptr,
                                                           .scale = 50.0f,
                                                           .key = 0,
                                                           .skill = 0,
                                                           .skillSerial = 0,
                                                           .target = -1};
            const auto created = Client::Scenes::CreateFaunaJoint(*joints, request, random);
            EXPECT_TRUE(created.HasValue()) << created.Error();
        }
    }
    return joints;
}

void ExpectSameJoints(std::span<const Client::Scenes::JointState> actual,
                      std::span<const Client::Scenes::JointState> expected)
{
    ASSERT_EQ(actual.size(), expected.size());
    for (std::size_t slot = 0; slot < actual.size(); ++slot)
    {
        SCOPED_TRACE(slot);
        const auto& got = actual[slot];
        const auto& want = expected[slot];
        EXPECT_EQ(got.alive, want.alive);
        EXPECT_EQ(got.type, want.type);
        EXPECT_EQ(got.subtype, want.subtype);
        EXPECT_EQ(got.position, want.position);
        EXPECT_EQ(got.targetPosition, want.targetPosition);
        EXPECT_EQ(got.angles, want.angles);
        EXPECT_EQ(got.width, want.width);
        EXPECT_EQ(got.life, want.life);
        EXPECT_EQ(got.light, want.light);
        EXPECT_EQ(got.ownerContext, want.ownerContext);
    }
}

class BodyModels final : public Client::Models::IModelLibrary
{
public:
    BodyModels()
    {
        using namespace Client::Math;
        using namespace Client::Models;
        SkeletonBone bone;
        bone.parent = -1;
        bone.actions = {{BoneKey{}}};
        bone.actions[0][0].rotation = AngleQuaternion({0.0f, 0.0f, 0.0f});
        m_skeleton.bones.assign(1, bone);
        m_skeleton.actions = {ActionInfo{.repeats = true, .speed = 0.0f, .keyCount = 1, .lockPositions = false}};
        for (std::size_t index = 0; index < MeshCount; ++index)
        {
            Client::Data::Models::Mesh mesh;
            const auto x = static_cast<float>(index);
            mesh.vertices = {{0, {x, 0, 0}}, {0, {x + 1, 0, 0}}, {0, {x, 1, 0}}};
            mesh.normals = {{0, {0, 0, 1}, 0}};
            mesh.texCoords = {{0, 0}};
            mesh.triangles = {{3, {0, 1, 2, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}}};
            mesh.textureName = "monster.jpg";
            m_model.meshes.push_back(mesh);
            m_textures[index] = Client::Render::TextureSlot{static_cast<std::uint16_t>(0x300 + index)};
        }
    }

    [[nodiscard]] const Client::Data::Models::Model* Find(std::uint16_t) const override
    {
        return &m_model;
    }
    [[nodiscard]] const Client::Models::Skeleton* SkeletonOf(std::uint16_t) const override
    {
        return &m_skeleton;
    }
    [[nodiscard]] std::span<const Client::Render::TextureSlot> MeshTextures(std::uint16_t) const override
    {
        return m_textures;
    }

    void SetTextureName(std::size_t mesh, std::string name)
    {
        m_model.meshes[mesh].textureName = std::move(name);
    }

private:
    Client::Models::Skeleton m_skeleton;
    Client::Data::Models::Model m_model;
    std::array<Client::Render::TextureSlot, MeshCount> m_textures{};
};

class MonsterBodyTest : public testing::Test
{
protected:
    void SetUp() override
    {
        m_batch.Begin(Client::Render::Pass3D{});
        m_actor = std::make_unique<ActorState>();
        m_actor->alive = true;
        m_actor->alpha = 1.0f;
        m_actor->scale = 1.0f;
        m_actor->dword58 = -1;
        m_actor->dword64 = -1;
        m_actor->blendMeshLight = 1.0f;
    }

    // 0x6002e0 con el color que dejó 0x5ffce0 en (0,2; 0,4; 0,6).
    Client::Core::Expected<void, std::string> Draw(std::uint16_t type, std::int32_t map, std::uint16_t special = 0,
                                                   float worldClock = 0.0f, bool originalFrame = true)
    {
        m_actor->type = type;
        Client::Scenes::DrawnObject object;
        object.model = type;
        object.transform = m_transform;
        object.color = {0.2f, 0.4f, 0.6f};
        object.alpha = m_actor->alpha;
        m_meshes = Client::Scenes::SkinObjectMeshes(m_models, object, m_bones, m_boneScale);
        Client::Scenes::MonsterBodyModel model{.batch = m_batch,
                                               .models = m_models,
                                               .object = object,
                                               .meshes = m_meshes,
                                               .frameClock = Client::Core::Duration{},
                                               .worldClock = worldClock,
                                               .boneScale = m_boneScale};
        Client::Scenes::MonsterBodyFrame frame{.model = model,
                                               .actor = *m_actor,
                                               .bones = m_bones,
                                               .map = map,
                                               .special = special,
                                               .worldClock = worldClock,
                                               .random = m_cloth.random,
                                               .cloth = m_cloth,
                                               .siegeActive = m_siegeActive,
                                               .lastSoundId = &m_sound,
                                               .originalFrame = originalFrame,
                                               .cameraShake = &m_shake,
                                               .effects = m_effects.get(),
                                               .particles = m_particles.get(),
                                               .terrainHeight = m_terrainHeight,
                                               .joints = m_joints};
        auto result = Client::Scenes::DrawMonsterBody(frame);
        m_color = model.object.color;
        return result;
    }

    // 0x4ee770 repetido sobre las mallas de este dibujo en tablas gemelas, con el rand() desde `state`: las mismas
    // altas, los mismos rand(). Devuelve cuántos creó.
    std::size_t ExpectTheTwinVertexEffects(std::uint32_t state, std::int32_t mesh, std::int32_t type,
                                           std::int32_t subtype)
    {
        auto effects = std::make_unique<Client::World::ModelEffects>();
        auto particles = std::make_unique<Client::World::Particles>();
        Client::Crt::CrtRandom random{state};
        const auto emitted =
            Client::Scenes::EmitVertexEffectsOf(*m_models.Find(m_actor->type), m_transform.scale, m_meshes,
                                                {.mesh = mesh, .type = type, .subtype = subtype},
                                                {.effects = effects.get(), .particles = particles.get()}, random);
        EXPECT_TRUE(emitted.HasValue()) << emitted.Error();
        EXPECT_EQ(m_cloth.random.State(), random.State());
        std::size_t made = 0;
        for (std::size_t slot = 0; slot < effects->Effects().size(); ++slot)
        {
            const auto& twin = effects->Effects()[slot];
            const auto& real = m_effects->Effects()[slot];
            EXPECT_EQ(real.alive, twin.alive) << slot;
            if (!twin.alive || !real.alive)
                continue;
            ++made;
            EXPECT_EQ(real.type, twin.type) << slot;
            EXPECT_EQ(real.subtype, twin.subtype) << slot;
            EXPECT_EQ(real.transform.position, twin.transform.position) << slot;
            EXPECT_EQ(real.transform.angles, twin.transform.angles) << slot;
            EXPECT_EQ(real.velocity, twin.velocity) << slot;
            EXPECT_EQ(real.light, twin.light) << slot;
            EXPECT_EQ(real.life, twin.life) << slot;
        }
        for (std::size_t slot = 0; slot < particles->All().size(); ++slot)
        {
            const auto& twin = particles->All()[slot];
            const auto& real = m_particles->All()[slot];
            EXPECT_EQ(real.alive, twin.alive) << slot;
            if (!twin.alive || !real.alive)
                continue;
            ++made;
            EXPECT_EQ(real.type, twin.type) << slot;
            EXPECT_EQ(real.subtype, twin.subtype) << slot;
            EXPECT_EQ(real.position, twin.position) << slot;
            EXPECT_EQ(real.light, twin.light) << slot;
        }
        return made;
    }

    const Client::Render::ModelDrawCommand& CommandAt(std::size_t vertex) const
    {
        for (const auto& command : m_batch.Commands())
        {
            if (command.operation == Client::Render::ModelStateOperation::Draw && vertex >= command.firstVertex &&
                vertex < command.firstVertex + command.vertexCount)
                return command;
        }
        ADD_FAILURE() << "ningún dibujo tiene el vértice " << vertex;
        return m_batch.Commands().front();
    }

    std::optional<std::uint16_t> TextureAt(std::size_t vertex) const
    {
        const auto texture = CommandAt(vertex).state.texture;
        if (!texture)
            return std::nullopt;
        return texture->value;
    }

    void ExpectColor(std::size_t vertex, float red, float green, float blue) const
    {
        SCOPED_TRACE(vertex);
        const auto& color = m_batch.Vertices()[vertex].color;
        EXPECT_FLOAT_EQ(color.red, red);
        EXPECT_FLOAT_EQ(color.green, green);
        EXPECT_FLOAT_EQ(color.blue, blue);
    }

    BodyModels m_models;
    Client::Render::ModelBatch m_batch;
    std::unique_ptr<ActorState> m_actor;
    Client::Models::ClothWorld m_cloth;
    std::optional<std::int32_t> m_sound;
    float m_shake = 0.0f;
    bool m_siegeActive = false;
    float m_boneScale = 1.0f;
    // La escala y la posición que 0x5ffdf0 copió al modelo (+0x68 y +0x6c): las del objeto del dibujo.
    Client::Models::ObjectTransform m_transform{};
    std::unique_ptr<Client::World::ModelEffects> m_effects = std::make_unique<Client::World::ModelEffects>();
    std::unique_ptr<Client::World::Particles> m_particles = std::make_unique<Client::World::Particles>();
    // La tabla de joints (0x74a8d78): vacía salvo en Kundun al morir.
    std::vector<Client::Scenes::JointState> m_joints;
    std::vector<Client::Math::BoneTransform> m_bones{Client::Math::BoneTransform(1.0f)};
    std::function<double(float, float)> m_terrainHeight{};
    std::vector<Client::Models::SkinnedMesh> m_meshes;
    glm::vec3 m_color{};
};

TEST_F(MonsterBodyTest, ALorenciaSpiderDrawsTheCommonBodyWithTheModelColor)
{
    const auto result = Draw(0x15b, 0);

    ASSERT_TRUE(result.HasValue()) << result.Error();
    // 0x60235e: RenderBody(2) con la textura de cada malla.
    ASSERT_EQ(m_batch.Vertices().size(), WholeBody);
    ExpectColor(0, 0.2f, 0.4f, 0.6f);
    EXPECT_EQ(TextureAt(0), 0x300);
    EXPECT_EQ(TextureAt(18), 0x306);
    EXPECT_EQ(m_cloth.random.State(), AfterRandoms(0));
}

TEST_F(MonsterBodyTest, ADeviasMonsterWithTheSecondFlagDrawsItsBodyTwiceInItsColor)
{
    m_actor->flags78 = 2;

    const auto result = Draw(0x15e, 2);

    ASSERT_TRUE(result.HasValue()) << result.Error();
    // 0x600eb3: (0,3; 0,5; 1); 0x602365: el segundo RenderBody(2) con el blendMesh en −2 y la luz en 1.
    ASSERT_EQ(m_batch.Vertices().size(), 2 * WholeBody);
    ExpectColor(0, std::bit_cast<float>(0x3e99999au), 0.5f, 1.0f);
    ExpectColor(WholeBody, std::bit_cast<float>(0x3e99999au), 0.5f, 1.0f);
}

TEST_F(MonsterBodyTest, TheFirstFlagWinsOverTheSecondWithItsOwnColor)
{
    m_actor->flags78 = 3;

    const auto result = Draw(0x15b, 0);

    ASSERT_TRUE(result.HasValue()) << result.Error();
    ExpectColor(0, std::bit_cast<float>(0x3e99999au), 1.0f, std::bit_cast<float>(0x3f4ccccdu));
}

TEST_F(MonsterBodyTest, TheEarlyColorsGoByMap)
{
    // 0x60044d: el 0x171 en el 10; 0x600475: el 0x164 en el 9; en Lorencia y Devias, el del modelo.
    ASSERT_TRUE(Draw(0x171, 10).HasValue());
    ExpectColor(0, std::bit_cast<float>(0x3ca3d70au), std::bit_cast<float>(0x3d4ccccdu),
                std::bit_cast<float>(0x3e19999au));
    m_batch.Begin(Client::Render::Pass3D{});
    ASSERT_TRUE(Draw(0x164, 9).HasValue());
    ExpectColor(0, 0.0f, std::bit_cast<float>(0x3e99999au), 1.0f);
    for (const std::int32_t map : {0, 2})
    {
        m_batch.Begin(Client::Render::Pass3D{});
        ASSERT_TRUE(Draw(0x164, map).HasValue());
        ExpectColor(0, 0.2f, 0.4f, 0.6f);
    }
    // 0x60049b: el 0x16d con el cuarto argumento.
    m_batch.Begin(Client::Render::Pass3D{});
    ASSERT_TRUE(Draw(0x16d, 2, 0x43).HasValue());
    ExpectColor(0, 0.0f, 0.0f, 1.0f);
}

TEST_F(MonsterBodyTest, The16cDrawsAChromeBodyAndThePlainOneWithoutTheTail)
{
    m_actor->flags78 = 2;

    const auto result = Draw(0x16c, 0);

    ASSERT_TRUE(result.HasValue()) << result.Error();
    // 0x6004f7: (0,4; 0,6; 1) con Chrome01 en cada malla; 0x600538: con 1, la propia. Sale sin el +0x78 & 2.
    ASSERT_EQ(m_batch.Vertices().size(), 2 * WholeBody);
    ExpectColor(0, std::bit_cast<float>(0x3ecccccdu), std::bit_cast<float>(0x3f19999au), 1.0f);
    EXPECT_EQ(TextureAt(0), 0x5c0);
    EXPECT_EQ(TextureAt(18), 0x5c0);
    ExpectColor(WholeBody, 1.0f, 1.0f, 1.0f);
    EXPECT_EQ(TextureAt(WholeBody), 0x300);
}

TEST_F(MonsterBodyTest, TheKalimaTrapAddsTheWaterAndTheSameTrapInLorenciaDoesNot)
{
    m_cloth.waterTextureFrame = 5;
    constexpr float Clock = 1000.0f;

    for (const std::int32_t map : {24, 36})
    {
        SCOPED_TRACE(map);
        m_batch.Begin(Client::Render::Pass3D{});
        const auto result = Draw(0x27, map, 0, Clock);
        ASSERT_TRUE(result.HasValue()) << result.Error();
        // 0x4394c4: RenderBody(2); 0x439570: con sin(T · 0,002f) · 0,1f + 0,3f, el agua con el alpha en 0,3.
        ASSERT_EQ(m_batch.Vertices().size(), 2 * WholeBody);
        ExpectColor(0, 0.2f, 0.4f, 0.6f);
        const auto water = static_cast<float>(
            RoundedSin(static_cast<double>(Clock) * static_cast<double>(std::bit_cast<float>(0x3b03126fu))) *
                static_cast<double>(std::bit_cast<float>(0x3dcccccdu)) +
            static_cast<double>(std::bit_cast<float>(0x3e99999au)));
        EXPECT_NEAR(m_color.x, water, 1e-6f);
        EXPECT_EQ(TextureAt(WholeBody), 0x41 + 5);
        EXPECT_EQ(CommandAt(WholeBody).state.blend, ModelBlend::Alpha);
        EXPECT_FLOAT_EQ(m_batch.Vertices()[WholeBody].color.alpha, std::bit_cast<float>(0x3e99999au));
    }
    for (const std::int32_t map : {0, 2})
    {
        m_batch.Begin(Client::Render::Pass3D{});
        ASSERT_TRUE(Draw(0x27, map, 0, Clock).HasValue());
        EXPECT_EQ(m_batch.Vertices().size(), WholeBody);
    }
}

TEST_F(MonsterBodyTest, TheKalima173GoesDarkAndLeavesTheColorInOne)
{
    m_actor->flags78 = 2;

    const auto result = Draw(0x173, 25);

    ASSERT_TRUE(result.HasValue()) << result.Error();
    // 0x43900f: con el color en 0, RenderBody(2); 0x439657: en 1, y 0x602365 con ese color.
    ASSERT_EQ(m_batch.Vertices().size(), 2 * WholeBody);
    ExpectColor(0, 0.0f, 0.0f, 0.0f);
    ExpectColor(WholeBody, 1.0f, 1.0f, 1.0f);
}

TEST_F(MonsterBodyTest, TheC5DrawsEveryMeshRedWithTheSubtypeOne)
{
    m_actor->dword58 = 2;
    m_actor->subtype = 1;

    const auto result = Draw(0xc5, 2);

    ASSERT_TRUE(result.HasValue()) << result.Error();
    // 0x43904c: (1; 0,1; 0,1) y RenderBody(2) con la malla oculta en −1.
    ASSERT_EQ(m_batch.Vertices().size(), WholeBody);
    ExpectColor(0, 1.0f, std::bit_cast<float>(0x3dcccccdu), std::bit_cast<float>(0x3dcccccdu));
}

TEST_F(MonsterBodyTest, The17cWithSubtypeOneDrawsFourAdditiveMeshes)
{
    m_actor->subtype = 1;
    constexpr float Clock = 2500.0f;

    const auto result = Draw(0x17c, 0, 0, Clock);

    ASSERT_TRUE(result.HasValue()) << result.Error();
    // 0x6010f4..0x601080: la malla 0 con 0x6f6, la 1 con 0x6f5 y la 2 dos veces, aditivas.
    ASSERT_EQ(m_batch.Vertices().size(), 4 * Triangle);
    EXPECT_EQ(TextureAt(0), 0x6f6);
    EXPECT_EQ(TextureAt(3), 0x6f5);
    EXPECT_EQ(TextureAt(6), 0x302);
    EXPECT_EQ(TextureAt(9), 0x302);
    EXPECT_EQ(CommandAt(0).state.blend, ModelBlend::Additive);
    EXPECT_FLOAT_EQ(m_batch.Vertices()[9].position.x, 2.0f);
}

TEST_F(MonsterBodyTest, TheSpecialCasesSwapTheirTexturesOnlyWithTheFourthArgument)
{
    // 0x6012d8..0x601330: el 0x180 con el cuarto argumento, la malla 0 con 0x62f y la 2 con 0x631.
    ASSERT_TRUE(Draw(0x180, 2, 0x43).HasValue());
    ASSERT_EQ(m_batch.Vertices().size(), 2 * Triangle);
    EXPECT_EQ(TextureAt(0), 0x62f);
    EXPECT_EQ(TextureAt(3), 0x631);
    EXPECT_FLOAT_EQ(m_batch.Vertices()[3].position.x, 2.0f);
    // Sin él, el común.
    m_batch.Begin(Client::Render::Pass3D{});
    ASSERT_TRUE(Draw(0x180, 2).HasValue());
    EXPECT_EQ(m_batch.Vertices().size(), WholeBody);
    // 0x6013b4: el 0x182, RenderBody(2) sin la malla 1 y con 0x62e.
    m_batch.Begin(Client::Render::Pass3D{});
    ASSERT_TRUE(Draw(0x182, 0, 0x4a).HasValue());
    ASSERT_EQ(m_batch.Vertices().size(), WholeBody - Triangle);
    EXPECT_EQ(TextureAt(0), 0x62e);
    ExpectColor(0, 1.0f, 1.0f, 1.0f);
}

TEST_F(MonsterBodyTest, The186GoesByItsFourthArgument)
{
    // Sin él: (0,4; 0,3; 0,5) con 0x624 y después el común con 0,5.
    ASSERT_TRUE(Draw(0x186, 0).HasValue());
    ASSERT_EQ(m_batch.Vertices().size(), 2 * WholeBody);
    EXPECT_EQ(TextureAt(0), 0x624);
    ExpectColor(0, std::bit_cast<float>(0x3ecccccdu), std::bit_cast<float>(0x3e99999au), 0.5f);
    ExpectColor(WholeBody, 0.5f, 0.5f, 0.5f);
    EXPECT_EQ(TextureAt(WholeBody), 0x300);
    // Con otro: (0,1; 1; 0,8) y el común.
    m_batch.Begin(Client::Render::Pass3D{});
    ASSERT_TRUE(Draw(0x186, 2, 0x43).HasValue());
    ASSERT_EQ(m_batch.Vertices().size(), WholeBody);
    ExpectColor(0, std::bit_cast<float>(0x3dcccccdu), 1.0f, std::bit_cast<float>(0x3f4ccccdu));
}

TEST_F(MonsterBodyTest, The18eDrawsAfterTwentyFiveStepsAndDrawsTheCounterRandoms)
{
    m_cloth.logicSteps = 100;
    m_actor->stepStamp = 75;

    const auto result = Draw(0x18e, 0);

    ASSERT_TRUE(result.HasValue()) << result.Error();
    // 0x601b3e..0x601bac: RenderBody(2), el cromo con 1 y el brillo con (0,3; 0,3; 1), los dos con Chrome01.
    ASSERT_EQ(m_batch.Vertices().size(), 3 * WholeBody);
    EXPECT_EQ(CommandAt(WholeBody).state.blend, ModelBlend::Additive);
    EXPECT_EQ(CommandAt(2 * WholeBody).state.blend, ModelBlend::Additive);
    EXPECT_EQ(TextureAt(0), 0x300);
    // El cierre del contador de pasos (0x7d3a0f0): cuatro rand().
    EXPECT_EQ(m_cloth.random.State(), AfterRandoms(4));
    EXPECT_FALSE(m_sound.has_value());
}

// 0x601bd3..0x601c0d: con menos de 25 pasos, el 0x18e no se dibuja: pide el sonido 0x87 y 0x4ee770(0, 0x10a, 0, 0, 0)
// sobre las mallas del dibujo (la malla 0: rand() & 1 y el 0x10b/0, rand() % 10 y el 0x10a/0 en cada esquina), y sigue
// en la cola.
TEST_F(MonsterBodyTest, The18eBeforeTwentyFiveStepsAsksItsSoundAndLeavesItsStones)
{
    m_cloth.logicSteps = 100;
    m_actor->stepStamp = 76;
    m_transform.position = {100.0f, 200.0f, 30.0f};

    const auto result = Draw(0x18e, 2);

    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_TRUE(m_batch.Vertices().empty());
    EXPECT_EQ(m_sound, 0x87);
    EXPECT_TRUE(m_actor->alive);
    EXPECT_GT(ExpectTheTwinVertexEffects(AfterRandoms(4), 0, 0x10a, 0), 0u);
}

// La esquina que ancla las tablas gemelas de ExpectTheTwinVertexEffects, con valores a mano: 0x4ee770(0, 0x10a, 0)
// sobre una esquina sola con la semilla 8. r0 = 64 es par (0x4ee835): el 0x10b/0, que 0x518cf0 deja en el /1 con un
// rand() más (nueve); r10 = 4670 da 0 % 10 (0x4ee86a): el 0x10a/0 (ocho). 19 rand(), [0] el 0x10b/1 y [1] el 0x10a/0,
// los dos sobre la esquina. Con la tabla llena, 0x518cf0 sale sin sortear: sólo r0 y r1.
TEST(MonsterBodyStonesTest, OneCornerOfTheFallingStonesWithTheSeedEight)
{
    Client::Data::Models::Model model;
    Client::Data::Models::Mesh mesh;
    mesh.triangles.assign(1, {1, {0, 0, 0, 0}});
    model.meshes.push_back(mesh);
    const glm::vec3 corner{100.0f, 200.0f, 30.0f};
    std::vector<Client::Models::SkinnedMesh> posed(1);
    posed[0].positions = {corner};
    const auto after = [](std::size_t count)
    {
        Client::Crt::CrtRandom random{8};
        for (std::size_t draw = 0; draw < count; ++draw)
            static_cast<void>(random.Next());
        return random.State();
    };
    auto effects = std::make_unique<Client::World::ModelEffects>();
    Client::Crt::CrtRandom random{8};
    const auto emitted = Client::Scenes::EmitVertexEffectsOf(model, 1.0f, posed, {.mesh = 0, .type = 0x10a},
                                                             {.effects = effects.get()}, random);
    ASSERT_TRUE(emitted.HasValue()) << emitted.Error();
    EXPECT_EQ(random.State(), after(19));
    const auto made = effects->Effects();
    ASSERT_TRUE(made[0].alive);
    EXPECT_EQ(made[0].type, 0x10b);
    EXPECT_EQ(made[0].subtype, 1);
    EXPECT_EQ(made[0].life, 39); // r2 = 311: 7 + 32 (0x51bb38).
    ASSERT_TRUE(made[1].alive);
    EXPECT_EQ(made[1].type, 0x10a);
    EXPECT_EQ(made[1].subtype, 0);
    EXPECT_EQ(made[1].life, 45); // r12 = 14413: 13 + 32.
    EXPECT_FALSE(made[2].alive);
    // La esquina con la z 50 más (0x51bafb..0x51bb35).
    EXPECT_EQ(made[0].transform.position, glm::vec3(100.0f, 200.0f, 80.0f));
    EXPECT_EQ(made[1].transform.position, glm::vec3(100.0f, 200.0f, 80.0f));

    for (auto& effect : effects->Effects())
        effect.alive = true;
    Client::Crt::CrtRandom full{8};
    ASSERT_TRUE(Client::Scenes::EmitVertexEffectsOf(model, 1.0f, posed, {.mesh = 0, .type = 0x10a},
                                                    {.effects = effects.get()}, full)
                    .HasValue());
    EXPECT_EQ(full.State(), after(2));
}

// 0x601be5 y 0x601c21: al morir, el 0x18e y el 0x18f dejan el byte vivo en 0, piden el 0x87 y 0x4ee770 con el 0x10a y
// el 0x10c (el /0: la luz en 0,2f, rand() % 12 y el 0x10d/0, rand() % 50 y el 0x10c/0), sin el contador. Entre frames
// del original, ni el sonido ni los efectos.
TEST_F(MonsterBodyTest, TheDying18eAnd18fDieWithoutTheCounter)
{
    m_cloth.openCounterAccesses = 0;
    for (const std::uint16_t type : {std::uint16_t{0x18e}, std::uint16_t{0x18f}})
    {
        SCOPED_TRACE(type);
        m_actor->alive = true;
        m_actor->animation.action = 6;
        m_sound.reset();
        m_effects = std::make_unique<Client::World::ModelEffects>();
        const auto before = m_cloth.random.State();
        const auto result = Draw(type, 0);
        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_FALSE(m_actor->alive);
        EXPECT_EQ(m_sound, 0x87);
        ExpectTheTwinVertexEffects(before, 0, type == 0x18e ? 0x10a : 0x10c, 0);
    }
    m_sound.reset();
    m_actor->alive = true;
    m_effects = std::make_unique<Client::World::ModelEffects>();
    const auto before = m_cloth.random.State();
    ASSERT_TRUE(Draw(0x18f, 0, 0, 0.0f, false).HasValue());
    EXPECT_FALSE(m_sound.has_value());
    EXPECT_EQ(m_cloth.random.State(), before);
    EXPECT_TRUE(std::ranges::none_of(m_effects->Effects(), [](const auto& effect) { return effect.alive; }));
}

TEST_F(MonsterBodyTest, The177AddsTheMesh6TextureAndTheScrollingChrome)
{
    constexpr float Clock = 1234.0f;

    const auto result = Draw(0x177, 2, 0, Clock);

    ASSERT_TRUE(result.HasValue()) << result.Error();
    // El común y 0x602391: la malla 0 con IndexTexture[6] y la 3 cromada.
    ASSERT_EQ(m_batch.Vertices().size(), WholeBody + 2 * Triangle);
    EXPECT_EQ(TextureAt(WholeBody), 0x306);
    EXPECT_FLOAT_EQ(m_batch.Vertices()[WholeBody].position.x, 0.0f);
    EXPECT_FLOAT_EQ(m_batch.Vertices()[WholeBody + Triangle].position.x, 3.0f);
    EXPECT_EQ(CommandAt(WholeBody + Triangle).state.blend, ModelBlend::Additive);
}

TEST_F(MonsterBodyTest, The1f3AddsAnAdditiveMeshZero)
{
    const auto result = Draw(0x1f3, 0);

    ASSERT_TRUE(result.HasValue()) << result.Error();
    ASSERT_EQ(m_batch.Vertices().size(), WholeBody + Triangle);
    EXPECT_EQ(CommandAt(WholeBody).state.blend, ModelBlend::Additive);
    EXPECT_EQ(TextureAt(WholeBody), 0x300);
}

TEST_F(MonsterBodyTest, TheUnportedBranchesCutWithTheirAddress)
{
    struct Case
    {
        std::uint16_t type;
        std::int32_t map;
        const char* address;
    };
    // La clase de los bordes del 0x183 después de su malla 0; la cadena; los tipos que ninguna alta da.
    constexpr std::array cases{Case{0x183, 0, "0x6011f1"}, Case{0x110, 0, "0x6004ed"}, Case{0x69, 0, "0x600a4a"},
                               Case{0x12a, 2, "0x600654"}, Case{0x111, 0, "0x60246a"}, Case{0x11d, 0, "0x43ed20"},
                               Case{0x13d, 2, "0x442e10"}};
    for (const Case& value : cases)
    {
        SCOPED_TRACE(value.address);
        m_batch.Begin(Client::Render::Pass3D{});
        const auto result = Draw(value.type, value.map);
        ASSERT_FALSE(result.HasValue());
        EXPECT_EQ(result.Error().rfind(value.address, 0), 0u) << result.Error();
    }
    // Fuera de su mapa, los del 30 y el 34 son el común.
    for (const std::uint16_t type : {std::uint16_t{0x19b}, std::uint16_t{0x1b4}})
    {
        m_batch.Begin(Client::Render::Pass3D{});
        const auto result = Draw(type, 2);
        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_EQ(m_batch.Vertices().size(), WholeBody);
    }
}

// 42e701..42e730 vuelve con AL=0: el caller conserva el RenderBody común después del propio.
TEST_F(MonsterBodyTest, TheCastleStatueDrawsTwiceBecauseItsBodyCaseReturnsFalse)
{
    const auto result = Draw(0x19b, 30);
    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(m_batch.Vertices().size(), WholeBody * 2);
}

// 42e731..42e738 y42e966: sin batalla el19c devuelve1 sin tocar el modelo ni dibujar.
TEST_F(MonsterBodyTest, TheCastleGuardConsumesItsBodyPassOutsideTheBattle)
{
    const auto result = Draw(0x19c, 30);
    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_TRUE(m_batch.Vertices().empty());
}

TEST_F(MonsterBodyTest, TheCastleLifeModelSelectsMeshesByItsUnsignedByte93)
{
    m_actor->byte93 = 4;
    const auto result = Draw(0x1a8, 30);
    ASSERT_TRUE(result.HasValue()) << result.Error();
    ASSERT_EQ(m_batch.Vertices().size(), Triangle * 3);
    EXPECT_EQ(TextureAt(0), 0x300);
    EXPECT_EQ(TextureAt(Triangle), 0x302);
    EXPECT_EQ(TextureAt(Triangle * 2), 0x301);
    EXPECT_FLOAT_EQ(m_actor->blendMeshLight, 0.4f);
    ExpectColor(Triangle * 2, 0.2f, 0.2f, 0.2f);
}

TEST_F(MonsterBodyTest, TheCastleStatueDeathPreservesItsFalseReturnAfterSoundAndVertexEffects)
{
    m_actor->animation.action = 6;
    for (auto& effect : m_effects->Effects())
        effect.alive = true;
    const auto result = Draw(0x19b, 30);
    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_FALSE(m_actor->alive);
    EXPECT_EQ(m_sound, 0x87);
    EXPECT_EQ(m_cloth.random.State(), AfterRandoms(9));
    EXPECT_EQ(m_batch.Vertices().size(), WholeBody);
}

TEST_F(MonsterBodyTest, TheCastleGuardDeathEmitsDebrisAndConsumesItsBodyPass)
{
    m_siegeActive = true;
    m_actor->animation.action = 6;
    for (auto& effect : m_effects->Effects())
        effect.alive = true;
    const auto result = Draw(0x19c, 30);
    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_FALSE(m_actor->alive);
    EXPECT_EQ(m_sound, 0x1e8);
    EXPECT_EQ(m_cloth.random.State(), AfterRandoms(6));
    EXPECT_TRUE(m_batch.Vertices().empty());
}

TEST_F(MonsterBodyTest, TheCastleWaterUsesTheProtectedFrameThenTheSignedClockRemainder)
{
    m_cloth.waterTextureFrame = 7;
    const auto result = Draw(0x1ff, 30, 0, 1000);
    ASSERT_TRUE(result.HasValue()) << result.Error();
    ASSERT_EQ(m_batch.Vertices().size(), WholeBody * 2);
    EXPECT_EQ(TextureAt(0), 0x48);
    // 1000*0.01f =9.999999776..., ftol trunca9.
    EXPECT_EQ(TextureAt(WholeBody), 0x4a);
    ExpectColor(0, 0.3f, 0.3f, 1);
    ExpectColor(WholeBody, 0.3f, 1, 0.3f);
}

TEST_F(MonsterBodyTest, TheCastleCatapultSelectsTheProjectileBoneByItsFrameWindow)
{
    m_bones.resize(45, Client::Math::BoneTransform(1));
    m_bones[12][3][0] = 10;
    m_bones[12][3][1] = 20;
    m_bones[12][3][2] = 30;
    m_bones[44][3][0] = 40;
    m_bones[44][3][1] = 50;
    m_bones[44][3][2] = 60;
    struct Case
    {
        std::uint8_t action;
        float frame;
        float projectileX;
    };
    const std::array cases{Case{0, 14, 10},
                           Case{1, 1, 10},
                           Case{1, 2, 0},
                           Case{1, 13, 0},
                           Case{1, 14, 40},
                           Case{1, 22, 10},
                           Case{1, std::numeric_limits<float>::quiet_NaN(), 10}};
    for (const auto& value : cases)
    {
        SCOPED_TRACE(value.frame);
        m_batch.Begin(Client::Render::Pass3D{});
        m_actor->animation.action = value.action;
        m_actor->animation.frame = value.frame;
        const auto result = Draw(0x1f8, 30);
        ASSERT_TRUE(result.HasValue()) << result.Error();
        const bool projectile = value.projectileX != 0;
        ASSERT_EQ(m_batch.Vertices().size(), WholeBody * (projectile ? 2u : 1u));
        if (projectile)
        {
            EXPECT_EQ(m_batch.Vertices()[WholeBody].position.x, value.projectileX);
        }
    }
}

TEST_F(MonsterBodyTest, TheCastleGuardDrawsItsTwoOverlaysAndRestoresWhite)
{
    m_siegeActive = true;
    const auto result = Draw(0x19c, 30);
    ASSERT_TRUE(result.HasValue()) << result.Error();
    ASSERT_EQ(m_batch.Vertices().size(), WholeBody * 3);
    ExpectColor(WholeBody, 0.3f, 0.3f, 0.3f);
    ExpectColor(WholeBody * 2, 0.2f, 0.2f, 0.5f);
    EXPECT_EQ(m_color, glm::vec3(1));
    EXPECT_EQ(TextureAt(WholeBody), 0x5c0);
    EXPECT_EQ(TextureAt(WholeBody * 2), 0x5c0);
}

TEST_F(MonsterBodyTest, TheCastleProjectileRebuildsItsPreludeColorAfterTheCatapultFlagTint)
{
    m_bones.resize(45, Client::Math::BoneTransform(1));
    m_actor->flags78 = 1;
    const auto result = Draw(0x1f8, 30);
    ASSERT_TRUE(result.HasValue()) << result.Error();
    ASSERT_EQ(m_batch.Vertices().size(), WholeBody * 2);
    ExpectColor(0, 0.3f, 1, 0.5f);
    ExpectColor(WholeBody, 0.2f, 0.4f, 0.6f);
}

TEST_F(MonsterBodyTest, TheCastleProjectileUsesTheActorsSharedBoneScale)
{
    m_bones.resize(45, Client::Math::BoneTransform(1));
    m_boneScale = 1.2f;
    const auto result = Draw(0x1f8, 30);
    ASSERT_TRUE(result.HasValue()) << result.Error();
    ASSERT_EQ(m_batch.Vertices().size(), WholeBody * 2);
    EXPECT_FLOAT_EQ(m_batch.Vertices()[WholeBody + 1].position.x, 1.2f * 1.3f);
}

TEST_F(MonsterBodyTest, TheEdgeClassOf183OnlyCutsWhenItMarksTheSilhouette)
{
    // 0x457e00: sin llegar a 0x457740 no junta ninguna arista, no saca rand() y solo queda la malla 0.
    struct Case
    {
        const char* what;
        float alpha;
        std::int32_t hidden;
        std::int32_t blend;
        const char* texture;
    };
    const std::array quiet{
        Case{"alpha bajo 0,01f", std::bit_cast<float>(0x3c23d709u), -1, -1, "monster.jpg"},
        Case{"alpha NaN", std::bit_cast<float>(0x7fc00000u), -1, -1, "monster.jpg"},
        Case{"+0x58 en -2", 1.0f, -2, -1, "monster.jpg"},
        Case{"+0x64 en -2", 1.0f, -1, -2, "monster.jpg"},
        Case{"+0x58 en 1 como int16", 1.0f, 0x10001, -1, "monster.jpg"},
        Case{"+0x64 en 1", 1.0f, -1, 1, "monster.jpg"},
        Case{"la malla 1 de cuatro componentes", 1.0f, -1, -1, "monster.tga"},
    };
    for (const std::int32_t map : {0, 2, 7})
    {
        SCOPED_TRACE(map);
        for (const Case& value : quiet)
        {
            SCOPED_TRACE(value.what);
            m_actor->alpha = value.alpha;
            m_actor->dword58 = value.hidden;
            m_actor->dword64 = value.blend;
            m_models.SetTextureName(1, value.texture);
            m_batch.Begin(Client::Render::Pass3D{});
            const auto result = Draw(0x183, map);
            ASSERT_TRUE(result.HasValue()) << result.Error();
            ASSERT_EQ(m_batch.Vertices().size(), Triangle);
            EXPECT_EQ(TextureAt(0), 0x300);
            EXPECT_EQ(m_cloth.random.State(), AfterRandoms(0));
        }
        // Con 0x457740, corta después de la malla 0 y antes del primer rand() de 0x458000.
        m_actor->alpha = 1.0f;
        m_actor->dword58 = -1;
        m_actor->dword64 = -1;
        m_models.SetTextureName(1, "monster.jpg");
        m_batch.Begin(Client::Render::Pass3D{});
        const auto result = Draw(0x183, map);
        ASSERT_FALSE(result.HasValue());
        EXPECT_EQ(result.Error().rfind("0x6011f1", 0), 0u) << result.Error();
        EXPECT_EQ(m_batch.Vertices().size(), Triangle);
        EXPECT_EQ(m_cloth.random.State(), AfterRandoms(0));
    }
}

// sin(T · rate) · factor + base en double, como la x87 con PC53 (el seno de la CRT basta en los relojes de las
// pruebas).
double WaveAt(float clock, std::uint32_t rate, std::uint32_t factor, std::uint32_t base)
{
    return RoundedSin(static_cast<double>(clock) * static_cast<double>(std::bit_cast<float>(rate))) *
               static_cast<double>(std::bit_cast<float>(factor)) +
           static_cast<double>(std::bit_cast<float>(base));
}

TEST_F(MonsterBodyTest, TheLateChainMonstersOf43ed20DrawTheirPassesOnEveryMap)
{
    constexpr float Clock = 4321.0f;
    for (const std::int32_t map : {0, 2, 24})
    {
        SCOPED_TRACE(map);
        // 0x43ed71: el 0x191, RenderBody(2) sin la malla oculta y la malla 0 con 0x48 y Chrome01.
        m_actor->dword58 = 3;
        m_batch.Begin(Client::Render::Pass3D{});
        auto result = Draw(0x191, map, 0, Clock);
        ASSERT_TRUE(result.HasValue()) << result.Error();
        ASSERT_EQ(m_batch.Vertices().size(), WholeBody + Triangle);
        EXPECT_EQ(TextureAt(WholeBody), 0x5c0);
        m_actor->dword58 = -1;
        // 0x43f519: el 0x193, RenderBody(2) y la malla 0 cromada con (0,5; 0,1; 0).
        m_batch.Begin(Client::Render::Pass3D{});
        result = Draw(0x193, map, 0, Clock);
        ASSERT_TRUE(result.HasValue()) << result.Error();
        ASSERT_EQ(m_batch.Vertices().size(), WholeBody + Triangle);
        ExpectColor(0, 0.2f, 0.4f, 0.6f);
        EXPECT_FLOAT_EQ(m_batch.Vertices()[WholeBody + 1].position.x, 1.0f);
        EXPECT_EQ(m_color, glm::vec3(0.5f, std::bit_cast<float>(0x3dcccccdu), 0.0f));
        // 0x43f5be: el 0x194, las mallas 0 y 2 cromadas.
        m_batch.Begin(Client::Render::Pass3D{});
        result = Draw(0x194, map, 0, Clock);
        ASSERT_TRUE(result.HasValue()) << result.Error();
        ASSERT_EQ(m_batch.Vertices().size(), WholeBody + 2 * Triangle);
        EXPECT_FLOAT_EQ(m_batch.Vertices()[WholeBody + Triangle].position.x, 2.0f);
        // 0x43f687: el 0x195 sin el subtipo 9: la malla 0 aditiva, las 1 a 3 y las 0 y 2 cromadas con (0,1; 0,6; 1).
        m_actor->subtype = 0;
        m_batch.Begin(Client::Render::Pass3D{});
        result = Draw(0x195, map, 0, Clock);
        ASSERT_TRUE(result.HasValue()) << result.Error();
        ASSERT_EQ(m_batch.Vertices().size(), 6 * Triangle);
        EXPECT_EQ(CommandAt(0).state.blend, ModelBlend::Additive);
        ExpectColor(Triangle, 0.2f, 0.4f, 0.6f);
        EXPECT_EQ(m_color, glm::vec3(std::bit_cast<float>(0x3dcccccdu), std::bit_cast<float>(0x3f19999au), 1.0f));
        // Con el 9: (0,3; 0,1; 0,1), la malla 1 dos veces más (cromada al alpha 0,5 y con 0x240) y (1; 0,6; 0,1).
        m_actor->subtype = 9;
        m_batch.Begin(Client::Render::Pass3D{});
        result = Draw(0x195, map, 0, Clock);
        ASSERT_TRUE(result.HasValue()) << result.Error();
        ASSERT_EQ(m_batch.Vertices().size(), 8 * Triangle);
        ExpectColor(Triangle, std::bit_cast<float>(0x3e99999au), std::bit_cast<float>(0x3dcccccdu),
                    std::bit_cast<float>(0x3dcccccdu));
        EXPECT_EQ(m_color, glm::vec3(1.0f, std::bit_cast<float>(0x3f19999au), std::bit_cast<float>(0x3dcccccdu)));
        m_actor->subtype = 0;
        // 0x43f882: el 0x196 con (0,5; sin(T · 0,002f) · 0,3f + 0,7f; 0,5) y la malla 0 cromada al alpha 0,8, que
        // guarda en el color del modelo el suyo por el alpha (0x4ed3fd).
        m_batch.Begin(Client::Render::Pass3D{});
        result = Draw(0x196, map, 0, Clock);
        ASSERT_TRUE(result.HasValue()) << result.Error();
        ASSERT_EQ(m_batch.Vertices().size(), WholeBody + Triangle);
        const float fade = std::bit_cast<float>(0x3f4ccccdu);
        EXPECT_FLOAT_EQ(m_color.x, 0.5f * fade);
        EXPECT_NEAR(m_color.y, static_cast<float>(WaveAt(Clock, 0x3b03126fu, 0x3e99999au, 0x3f333333u)) * fade, 1e-6f);
        // 0x43f945: el 0x197, las mallas 0, 1, 3 y 5, las 2 y 4 con la onda y, sin el subtipo 9, otra vez con 2 (con
        // él, las 0, 1 y 3 con 0x48), y la 6; +0x68 queda en sin(T · 0,002f) · 0,1f + 0,05f.
        for (const std::int32_t subtype : {0, 9})
        {
            m_actor->subtype = subtype;
            m_actor->blendMeshLight = 0.25f;
            m_batch.Begin(Client::Render::Pass3D{});
            result = Draw(0x197, map, 0, Clock);
            ASSERT_TRUE(result.HasValue()) << result.Error();
            ASSERT_EQ(m_batch.Vertices().size(), (subtype == 9 ? 10 : 9) * Triangle);
            EXPECT_FLOAT_EQ(m_batch.Vertices()[3 * Triangle].position.x, 5.0f);
            EXPECT_EQ(CommandAt(4 * Triangle).state.blend, ModelBlend::Additive);
            EXPECT_NEAR(m_actor->blendMeshLight,
                        static_cast<float>(WaveAt(Clock, 0x3b03126fu, 0x3dcccccdu, 0x3d4ccccdu)), 1e-6f);
            EXPECT_FLOAT_EQ(m_batch.Vertices()[m_batch.Vertices().size() - Triangle].position.x, 6.0f);
        }
        m_actor->subtype = 0;
        m_actor->blendMeshLight = 1.0f;
    }
    EXPECT_EQ(m_cloth.random.State(), AfterRandoms(0));
}

TEST_F(MonsterBodyTest, The185WavesItsMesh1AlongItsNormals)
{
    constexpr float Clock = 2024.0f;
    for (const std::int32_t map : {0, 2, 24})
    {
        SCOPED_TRACE(map);
        // 0x60157c: RenderBody(2) sin la malla 1 y la malla 1 dos veces con 0x402, al alpha 0,5 y al suyo: cada esquina
        // corrida por su normal (0, 0, 1) en sin((931 · v + ftol(T)) · 0,007f) · 28 (0x4ed775).
        m_batch.Begin(Client::Render::Pass3D{});
        const auto result = Draw(0x185, map, 0, Clock);
        ASSERT_TRUE(result.HasValue()) << result.Error();
        ASSERT_EQ(m_batch.Vertices().size(), WholeBody + Triangle);
        EXPECT_FLOAT_EQ(m_batch.Vertices()[WholeBody - Triangle].color.alpha, 0.5f);
        for (std::size_t corner = 0; corner < Triangle; ++corner)
        {
            SCOPED_TRACE(corner);
            const double phase = static_cast<double>(931 * static_cast<int>(corner) + 2024);
            const auto z =
                static_cast<float>(RoundedSin(phase * static_cast<double>(std::bit_cast<float>(0x3be56042u))) * 28.0);
            for (const std::size_t first : {WholeBody - Triangle, WholeBody})
            {
                EXPECT_NEAR(m_batch.Vertices()[first + corner].position.z, z, 1e-5f);
            }
        }
        EXPECT_FLOAT_EQ(m_batch.Vertices()[WholeBody - Triangle].position.x, 1.0f);
        EXPECT_FLOAT_EQ(m_batch.Vertices()[WholeBody - Triangle + 1].position.x, 2.0f);
    }
}

// 0x43ed4a..0x43ed6c y sus copias: al morir con el alpha por debajo de 0,5 (o NaN), los 0x191..0x197 piden
// 0x4ee770(0, 0x607, 0, 0, 0) antes de dibujarse: rand() % 30 por esquina de la malla 0 y, con 0, la partícula 0x607/2.
// Con el alpha en 0,5, o entre frames del original, nada.
TEST_F(MonsterBodyTest, TheFadingDeathOf43ed20LeavesItsParticlesBeforeDrawing)
{
    m_actor->animation.action = 6;
    m_transform.position = {500.0f, 600.0f, 70.0f};
    // Con la semilla 16, el primer rand() es 90: la primera esquina suelta su partícula.
    m_cloth.random = Client::Crt::CrtRandom{16};
    std::size_t made = 0;
    for (const float alpha : {0.25f, std::numeric_limits<float>::quiet_NaN()})
    {
        m_actor->alpha = alpha;
        for (const std::uint16_t type : {std::uint16_t{0x191}, std::uint16_t{0x193}, std::uint16_t{0x194},
                                         std::uint16_t{0x195}, std::uint16_t{0x196}, std::uint16_t{0x197}})
        {
            SCOPED_TRACE(type);
            m_batch.Begin(Client::Render::Pass3D{});
            m_particles = std::make_unique<Client::World::Particles>();
            const auto before = m_cloth.random.State();
            const auto result = Draw(type, 2);
            ASSERT_TRUE(result.HasValue()) << result.Error();
            made += ExpectTheTwinVertexEffects(before, 0, 0x607, 0);
        }
    }
    EXPECT_GT(made, 0u);
    // Con el alpha en 0,5 o fuera de los frames del original, sin rand().
    m_particles = std::make_unique<Client::World::Particles>();
    for (const bool original : {true, false})
    {
        m_actor->alpha = original ? 0.5f : 0.25f;
        m_batch.Begin(Client::Render::Pass3D{});
        const auto before = m_cloth.random.State();
        ASSERT_TRUE(Draw(0x193, 0, 0, 0.0f, original).HasValue());
        EXPECT_EQ(m_batch.Vertices().size(), WholeBody + Triangle);
        EXPECT_EQ(m_cloth.random.State(), before);
    }
    EXPECT_TRUE(std::ranges::none_of(m_particles->All(), [](const auto& particle) { return particle.alive; }));
}

// 0x439d90 al final del 0x197 y del 0x195/9: con +0x110 en 0x21 o 0x22, 0x4ee770(6, 0x10a, 1 o 2, 0, 0) sobre la malla
// 6 del dibujo (el 0x10b/2 o /3 en cada esquina, sin rand() propios: los ocho de cada alta), +0x110 en 0 y el sonido
// 0x88. Entre frames del original, sólo +0x110 en 0.
TEST_F(MonsterBodyTest, TheStruckGlowLeavesItsStonesOnMeshSix)
{
    m_transform.position = {300.0f, 400.0f, 50.0f};
    for (const std::uint16_t type : {std::uint16_t{0x197}, std::uint16_t{0x195}})
    {
        for (const std::uint8_t strike : {std::uint8_t{0x21}, std::uint8_t{0x22}})
        {
            SCOPED_TRACE(::testing::Message() << type << ", " << int{strike});
            m_actor->subtype = 9;
            m_actor->byte110 = strike;
            m_sound.reset();
            m_effects = std::make_unique<Client::World::ModelEffects>();
            m_batch.Begin(Client::Render::Pass3D{});
            const auto before = m_cloth.random.State();
            ASSERT_TRUE(Draw(type, 2).HasValue());
            EXPECT_EQ(m_actor->byte110, 0);
            EXPECT_EQ(m_sound, 0x88);
            EXPECT_EQ(ExpectTheTwinVertexEffects(before, 6, 0x10a, strike == 0x21 ? 1 : 2), Triangle);
            EXPECT_EQ(m_effects->Effects()[0].type, 0x10b);
            EXPECT_EQ(m_effects->Effects()[0].subtype, strike == 0x21 ? 2 : 3);
            EXPECT_EQ(m_effects->Effects()[0].transform.position.x, m_meshes[6].positions[0].x);
        }
    }
    m_actor->byte110 = 0x21;
    m_sound.reset();
    m_effects = std::make_unique<Client::World::ModelEffects>();
    const auto before = m_cloth.random.State();
    ASSERT_TRUE(Draw(0x197, 2, 0, 0.0f, false).HasValue());
    EXPECT_EQ(m_actor->byte110, 0);
    EXPECT_FALSE(m_sound.has_value());
    EXPECT_EQ(m_cloth.random.State(), before);
    EXPECT_FALSE(m_effects->Effects()[0].alive);
}

// 0x50baed → 0x5ffdf0(Kundun, 1) → 0x600177: el cuerpo de un monstruo pasa por 0x4ebc80 con Translate en 1, que gira
// cada normal por el hueso sin la escala (0x5fcbd0 en 0x68c53bc, 0x4ebf70) y no la escala nunca. El cromo de las mallas
// 1 a 6 (0x4ecc30 con 0x44) lee esa normal (0x4ed149) y, con 0x4 y el reloj en 0, u = 0,5 · nz y v = 0,5 · ny
// (0x4ed309..0x4ed32c): con la escala 1,5 y la normal (0, 0, 1), (0,5; 0), no (0,75; 0).
TEST_F(MonsterBodyTest, KundunsChromeReadsTheNormalsWithoutTheScaleOfItsBody)
{
    m_transform.scale = 1.5f;
    m_actor->scale = 1.5f;
    m_actor->dword60 = 100;

    const auto result = Draw(0x192, 0);

    ASSERT_TRUE(result.HasValue()) << result.Error();
    ASSERT_EQ(m_batch.Vertices().size(), WholeBody + 6 * Triangle);
    for (std::size_t corner = WholeBody; corner < m_batch.Vertices().size(); ++corner)
    {
        SCOPED_TRACE(corner);
        EXPECT_EQ(m_batch.Vertices()[corner].uv, glm::vec2(0.5f, 0.0f));
    }
}

TEST_F(MonsterBodyTest, KundunDrawsItsChromeUntilItFalls)
{
    for (const std::int32_t map : {0, 2, 24})
    {
        SCOPED_TRACE(map);
        // 0x43f279: con +0x60 en 100, el alpha guardado en 1, RenderBody(2) y las mallas 1 a 6 cromadas.
        m_actor->dword60 = 100;
        m_actor->alpha = 0.75f;
        m_actor->animation = {.action = 0, .frame = 4.0f};
        m_batch.Begin(Client::Render::Pass3D{});
        auto result = Draw(0x192, map);
        ASSERT_TRUE(result.HasValue()) << result.Error();
        ASSERT_EQ(m_batch.Vertices().size(), WholeBody + 6 * Triangle);
        EXPECT_FLOAT_EQ(m_actor->alpha, 1.0f);
        EXPECT_EQ(m_color, glm::vec3(std::bit_cast<float>(0x3ecccccdu), std::bit_cast<float>(0x3ecccccdu),
                                     std::bit_cast<float>(0x3e99999au)));
        // 0x43f3fb: en la acción 5 con el frame en (3,5; 6), las mallas 1, 2, 3 y 5 aditivas.
        m_actor->animation.action = 5;
        m_batch.Begin(Client::Render::Pass3D{});
        ASSERT_TRUE(Draw(0x192, map).HasValue());
        ASSERT_EQ(m_batch.Vertices().size(), WholeBody + 10 * Triangle);
        EXPECT_EQ(CommandAt(WholeBody + 6 * Triangle).state.blend, ModelBlend::Additive);
        m_actor->animation.frame = 7.0f;
        m_batch.Begin(Client::Render::Pass3D{});
        ASSERT_TRUE(Draw(0x192, map).HasValue());
        EXPECT_EQ(m_batch.Vertices().size(), WholeBody + 6 * Triangle);
        // 0x43f182: al morir desde el frame 8, el sonido 0x1de, +0x60 en 0x5a, los 0x56 rayos 0x613/3 y 0x6ee/3 de
        // 0x530860 desde (x, y, z + 200), el temblor y el cuerpo (0x43f26f: +0x60 ya está en 0x5a).
        m_sound.reset();
        m_actor->animation = {.action = 6, .frame = 8.0f};
        m_actor->position = {12800.5f, 6400.25f, std::bit_cast<float>(0x4215999au)}; // z: 37,4f
        m_cloth.random = Client::Crt::CrtRandom{1};
        m_joints.assign(KundunJointTable, Client::Scenes::JointState{});
        m_batch.Begin(Client::Render::Pass3D{});
        result = Draw(0x192, map);
        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_EQ(m_sound, 0x1de);
        EXPECT_EQ(m_actor->dword60, 0x5a);
        EXPECT_EQ(m_batch.Vertices().size(), WholeBody + 6 * Triangle);
        Client::Crt::CrtRandom oracle{1};
        const auto twin = KundunFallingRays(m_actor->position, oracle);
        ExpectSameJoints(m_joints, *twin);
        EXPECT_EQ(std::ranges::count_if(m_joints, [](const auto& joint) { return joint.alive; }), 2 * 0x56);
        // 0x43f20d: el primer rayo sale con el giro 0 desde 37,4f + 200 (0x436d6666 en float).
        EXPECT_EQ(m_joints[0].type, 0x613);
        EXPECT_EQ(m_joints[1].type, 0x6ee);
        EXPECT_EQ(m_joints[1].subtype, 3);
        EXPECT_EQ(std::bit_cast<std::uint32_t>(m_joints[0].targetPosition.z), 0x436d6666u);
        EXPECT_EQ(m_joints[0].angles.z, 0.0f);
        // 0x43f249: el temblor con el rand() que sigue a los rayos.
        const auto rayShake = static_cast<float>(static_cast<double>((oracle.Next() % 8) - 8) *
                                                 static_cast<double>(std::bit_cast<float>(0x3dcccccdu)));
        EXPECT_FLOAT_EQ(m_shake, rayShake);
        EXPECT_EQ(m_cloth.random.State(), oracle.State());
        m_joints.clear();
        // Con +0x60 en 0x5a y el frame hasta 14,8, el cuerpo.
        m_actor->animation.frame = std::bit_cast<float>(0x416ccccdu);
        m_batch.Begin(Client::Render::Pass3D{});
        ASSERT_TRUE(Draw(0x192, map).HasValue());
        EXPECT_EQ(m_batch.Vertices().size(), WholeBody + 6 * Triangle);
        // 0x43edff: pasado 14,8, el sonido 0x1dd, +0x60 en 10, la malla 0 y el temblor en (rand() % 8 − 8) · 0,1f;
        // después, los pedazos en el hueso 4 (KundunScattersItsPiecesWhenItFalls): con uno solo, corta.
        m_actor->animation.frame = 15.0f;
        m_cloth.random = Client::Crt::CrtRandom{1};
        m_batch.Begin(Client::Render::Pass3D{});
        result = Draw(0x192, map);
        ASSERT_FALSE(result.HasValue());
        EXPECT_EQ(result.Error().rfind("0x43ee90", 0), 0u) << result.Error();
        EXPECT_EQ(m_sound, 0x1dd);
        EXPECT_EQ(m_actor->dword60, 10);
        ASSERT_EQ(m_batch.Vertices().size(), Triangle);
        Client::Crt::CrtRandom replica{1};
        const auto shake = static_cast<float>(static_cast<double>((replica.Next() % 8) - 8) *
                                              static_cast<double>(std::bit_cast<float>(0x3dcccccdu)));
        EXPECT_FLOAT_EQ(m_shake, shake);
        EXPECT_EQ(m_cloth.random.State(), AfterRandoms(1));
        // Con +0x60 en 10, nada.
        m_batch.Begin(Client::Render::Pass3D{});
        ASSERT_TRUE(Draw(0x192, map).HasValue());
        EXPECT_TRUE(m_batch.Vertices().empty());
    }
}

// 0x43ee84..0x43f144, en todos los mapas: pasado 14,8, después del temblor, Kundun suelta en el hueso 4 sus ocho
// pedazos (0x518cf0 con el punto de 0x4ec110, sus ángulos y su luz, él de dueño, la clave −130 (−10 el 0x122) y +0x89
// de la tabla) y el 0x128/2 en su posición, que vuelve a entrar como el 0x127 con él de dueño. El punto: el
// desplazamiento de la tabla más la traslación del hueso (10, 20, 30), por la escala del modelo (2) y más su origen
// (1.000, 2.000, 300), calculados aparte. Con la altura del terreno en x/2 + y/4 en ese punto (0x5f93c0), la partida
// del /2 y el /3 en altura + 130 − 80 y la del /4 en altura + 10; cada /2 y /3 saca su rand() % 2 después del rand()
// del temblor. Sin la altura, corta el primero (0x43eeb9 → 0x51e26e).
TEST_F(MonsterBodyTest, KundunScattersItsPiecesWhenItFalls)
{
    struct Piece
    {
        std::int16_t type;
        std::int32_t subtype;
        std::uint8_t byte89;
        glm::vec3 position;
    };
    // (desplazamiento + (10, 20, 30)) · 2 + (1.000, 2.000, 300).
    const std::array<Piece, 8> pieces{{{0x124, 3, 3, {1098.0f, 2025.0f, 359.0f}},
                                       {0x11d, 3, 4, {1068.0f, 2025.0f, 425.0f}},
                                       {0x11e, 3, 5, {1068.0f, 2023.0f, 295.0f}},
                                       {0x11f, 2, 6, {1019.0f, 2048.0f, 361.0f}},
                                       {0x120, 3, 1, {1015.0f, 1996.0f, 468.0f}},
                                       {0x121, 3, 2, {1011.0f, 1991.0f, 254.0f}},
                                       {0x122, 4, 2, {748.0f, 1733.0f, 360.0f}},
                                       {0x123, 5, 2, {750.0f, 1734.0f, 360.0f}}}};
    const auto ground = [](float x, float y)
    { return (static_cast<double>(x) * 0.5) + (static_cast<double>(y) * 0.25); };
    for (const std::int32_t map : {0, 2, 24})
    {
        SCOPED_TRACE(map);
        m_effects = std::make_unique<Client::World::ModelEffects>();
        m_actor->dword60 = 0x5a;
        m_actor->animation = {.action = 6, .frame = 15.0f};
        m_actor->position = {1000.0f, 2000.0f, 300.0f};
        m_actor->angles = {0.0f, 0.0f, 30.0f};
        m_actor->light = {0.5f, 0.25f, 1.0f};
        m_transform = {.position = m_actor->position, .angles = m_actor->angles, .scale = 2.0f};
        m_bones.assign(5, Client::Math::BoneTransform(1.0f));
        m_bones[4][3] = glm::vec3(10.0f, 20.0f, 30.0f);
        m_terrainHeight = ground;
        m_cloth.random = Client::Crt::CrtRandom{1};
        m_batch.Begin(Client::Render::Pass3D{});

        const auto result = Draw(0x192, map);

        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_EQ(m_cloth.random.State(), AfterRandoms(7));
        Client::Crt::CrtRandom replica{1};
        static_cast<void>(replica.Next());
        const auto effects = m_effects->Effects();
        for (std::size_t slot = 0; slot < pieces.size(); ++slot)
        {
            SCOPED_TRACE(slot);
            const Piece& expected = pieces[slot];
            const Client::World::ModelEffect& piece = effects[slot];
            ASSERT_TRUE(piece.alive);
            EXPECT_EQ(piece.type, expected.type);
            EXPECT_EQ(piece.subtype, expected.subtype);
            EXPECT_EQ(piece.byte89, expected.byte89);
            EXPECT_EQ(piece.transform.position, expected.position);
            EXPECT_EQ(piece.ownerContext, m_actor.get());
            EXPECT_EQ(piece.disturbanceState, 0xffff);
            EXPECT_EQ(piece.transform.angles, m_actor->angles);
            EXPECT_EQ(piece.light, m_actor->light);
            EXPECT_EQ(piece.transform.scale, 2.0f);
            const double floor = ground(expected.position.x, expected.position.y);
            if (expected.subtype == 2 || expected.subtype == 3)
            {
                const auto rocking = static_cast<float>(replica.Next() % 2);
                EXPECT_EQ(piece.copiedAngles.x, expected.subtype == 2 ? 0.0f : rocking);
                EXPECT_EQ(piece.start, glm::vec3(expected.position.x, expected.position.y,
                                                 static_cast<float>(floor + 130.0 - 80.0)));
                EXPECT_EQ(piece.life, 500);
            }
            if (expected.subtype == 4)
            {
                EXPECT_EQ(piece.start,
                          glm::vec3(expected.position.x, expected.position.y, static_cast<float>(floor + 10.0)));
            }
        }
        EXPECT_EQ(m_cloth.random.State(), replica.State());
        const Client::World::ModelEffect& call = effects[8];
        ASSERT_TRUE(call.alive);
        EXPECT_EQ(call.type, 0x128);
        EXPECT_EQ(call.subtype, 2);
        EXPECT_EQ(call.life, 40);
        EXPECT_EQ(call.ownerContext, nullptr);
        EXPECT_EQ(call.transform.position, m_actor->position);
        const Client::World::ModelEffect& echo = effects[9];
        ASSERT_TRUE(echo.alive);
        EXPECT_EQ(echo.type, 0x127);
        EXPECT_EQ(echo.ownerContext, &call);
        EXPECT_EQ(echo.life, 200);
        EXPECT_FALSE(effects[10].alive);

        m_effects = std::make_unique<Client::World::ModelEffects>();
        m_terrainHeight = {};
        m_actor->dword60 = 0x5a;
        m_cloth.random = Client::Crt::CrtRandom{1};
        m_batch.Begin(Client::Render::Pass3D{});
        const auto ungrounded = Draw(0x192, map);
        ASSERT_FALSE(ungrounded.HasValue());
        EXPECT_EQ(ungrounded.Error().rfind("0x43eeb9: 0x51e26e", 0), 0u) << ungrounded.Error();
        m_bones.assign(1, Client::Math::BoneTransform(1.0f));
        m_transform = {};
    }
}

// 0x43edc1..0x43ee7e entre frames del original (ADR 0005): Kundun, pasado 14,8 con +0x60 en 0x5a, dibuja su malla 0 y
// deja +0x60 en 10 en la copia que dibuja, pero no tiembla, no suelta los pedazos ni el 0x128, no pide el sonido y no
// saca rand(): eso lo hace una vez, en el frame del original.
TEST_F(MonsterBodyTest, KundunFallsOnlyInTheOriginalFrame)
{
    for (const std::int32_t map : {0, 2, 24})
    {
        SCOPED_TRACE(map);
        m_effects = std::make_unique<Client::World::ModelEffects>();
        m_actor->dword60 = 0x5a;
        m_actor->animation = {.action = 6, .frame = 15.0f};
        m_bones.assign(5, Client::Math::BoneTransform(1.0f));
        m_terrainHeight = [](float, float) { return 50.0; };
        m_cloth.random = Client::Crt::CrtRandom{1};
        m_shake = 0.25f;
        m_sound.reset();
        m_batch.Begin(Client::Render::Pass3D{});

        const auto between = Draw(0x192, map, 0, 0.0f, false);

        ASSERT_TRUE(between.HasValue()) << between.Error();
        EXPECT_EQ(m_batch.Vertices().size(), Triangle);
        EXPECT_EQ(m_actor->dword60, 10);
        EXPECT_EQ(m_cloth.random.State(), AfterRandoms(0));
        EXPECT_EQ(m_shake, 0.25f);
        EXPECT_FALSE(m_sound.has_value());
        for (const auto& effect : m_effects->Effects())
            EXPECT_FALSE(effect.alive);

        m_actor->dword60 = 0x5a;
        const auto original = Draw(0x192, map);
        ASSERT_TRUE(original.HasValue()) << original.Error();
        EXPECT_EQ(m_sound, 0x1dd);
        EXPECT_NE(m_shake, 0.25f);
        EXPECT_EQ(m_cloth.random.State(), AfterRandoms(7));
        EXPECT_TRUE(m_effects->Effects()[9].alive);
        m_bones.assign(1, Client::Math::BoneTransform(1.0f));
    }
}

TEST_F(MonsterBodyTest, TheFlaggedLateChainMonstersAddAWiderChromeShell)
{
    constexpr float Clock = 777.0f;
    for (const std::int32_t map : {0, 2, 24})
    {
        SCOPED_TRACE(map);
        // 0x43fc25: con +0x78 & 0x100, (v · 0,1; v · 0,3; v · 0,6) y 0x60cd60: RenderBody(0x44) a 1,3 alrededor de los
        // huesos.
        m_actor->flags78 = 0x100;
        m_batch.Begin(Client::Render::Pass3D{});
        auto result = Draw(0x193, map, 0, Clock);
        ASSERT_TRUE(result.HasValue()) << result.Error();
        ASSERT_EQ(m_batch.Vertices().size(), 2 * WholeBody + Triangle);
        const double v = WaveAt(Clock, 0x3a83126fu, 0x3e4ccccdu, 0x3f000000u);
        const auto part = [v](std::uint32_t factor)
        { return static_cast<float>(v * static_cast<double>(std::bit_cast<float>(factor))); };
        EXPECT_NEAR(m_color.x, part(0x3dcccccdu), 1e-6f);
        EXPECT_NEAR(m_color.y, part(0x3e99999au), 1e-6f);
        EXPECT_NEAR(m_color.z, part(0x3f19999au), 1e-6f);
        EXPECT_FLOAT_EQ(m_batch.Vertices()[WholeBody + 2 * Triangle].position.x, 1.3f);
        // Con & 8, (v · 0,1; v · 0,6; v · 0,3); con +0x78 & 2 también, el cuerpo de 0x602365 sobre esas mallas.
        m_actor->flags78 = 0xa;
        m_batch.Begin(Client::Render::Pass3D{});
        result = Draw(0x193, map, 0, Clock);
        ASSERT_TRUE(result.HasValue()) << result.Error();
        ASSERT_EQ(m_batch.Vertices().size(), 3 * WholeBody + Triangle);
        EXPECT_NEAR(m_color.y, part(0x3f19999au), 1e-6f);
        EXPECT_NEAR(m_color.z, part(0x3e99999au), 1e-6f);
        EXPECT_FLOAT_EQ(m_batch.Vertices()[2 * WholeBody + 2 * Triangle].position.x, 1.3f);
        // Con & 0x4000, 0x60cd60 no dibuja.
        m_actor->flags78 = 0x4100;
        m_batch.Begin(Client::Render::Pass3D{});
        ASSERT_TRUE(Draw(0x193, map, 0, Clock).HasValue());
        EXPECT_EQ(m_batch.Vertices().size(), WholeBody + Triangle);
    }
}

TEST_F(MonsterBodyTest, TheMonstersOf442e10DrawTheirPassesOnEveryMap)
{
    constexpr float Clock = 12345.0f;
    const double glow = std::min(WaveAt(Clock, 0x3a9d4952u, 0x3f4ccccdu, 0x3fa66666u),
                                 static_cast<double>(std::bit_cast<float>(0x3fa66666u)));
    const auto drift = static_cast<float>(2345.0 * static_cast<double>(std::bit_cast<float>(0x3951b717u)));
    for (const std::int32_t map : {0, 2, 31})
    {
        SCOPED_TRACE(map);
        // 0x442e3d: el 0x1a3, RenderBody(2) sin la malla 1 y la 1 con (0,6; 0,4; 0,4); el color vuelve.
        m_batch.Begin(Client::Render::Pass3D{});
        auto result = Draw(0x1a3, map, 0, Clock);
        ASSERT_TRUE(result.HasValue()) << result.Error();
        ASSERT_EQ(m_batch.Vertices().size(), WholeBody);
        ExpectColor(WholeBody - Triangle, std::bit_cast<float>(0x3f19999au), std::bit_cast<float>(0x3ecccccdu),
                    std::bit_cast<float>(0x3ecccccdu));
        EXPECT_FLOAT_EQ(m_batch.Vertices()[WholeBody - Triangle].position.x, 1.0f);
        EXPECT_EQ(m_color, glm::vec3(0.2f, 0.4f, 0.6f));
        // 0x442eda: el 0x1a4, la malla 0 sin luz con el color por f y la U y la V corridas, aditiva, y la 1.
        m_batch.Begin(Client::Render::Pass3D{});
        result = Draw(0x1a4, map, 0, Clock);
        ASSERT_TRUE(result.HasValue()) << result.Error();
        ASSERT_EQ(m_batch.Vertices().size(), 3 * Triangle);
        ExpectColor(0, static_cast<float>(glow * static_cast<double>(0.2f)),
                    static_cast<float>(glow * static_cast<double>(0.4f)),
                    static_cast<float>(glow * static_cast<double>(0.6f)));
        EXPECT_FLOAT_EQ(m_batch.Vertices()[0].uv.x, drift);
        EXPECT_FLOAT_EQ(m_batch.Vertices()[0].uv.y, drift);
        EXPECT_EQ(CommandAt(Triangle).state.blend, ModelBlend::Additive);
        ExpectColor(2 * Triangle, 0.2f, 0.4f, 0.6f);
        EXPECT_FLOAT_EQ(m_batch.Vertices()[2 * Triangle].position.x, 1.0f);
        // 0x442fe9: el 0x1a5, RenderBody(2) sin la malla 0, la 0 con 1 y cromada.
        m_batch.Begin(Client::Render::Pass3D{});
        result = Draw(0x1a5, map, 0, Clock);
        ASSERT_TRUE(result.HasValue()) << result.Error();
        ASSERT_EQ(m_batch.Vertices().size(), WholeBody + Triangle);
        EXPECT_FLOAT_EQ(m_batch.Vertices()[0].position.x, 1.0f);
        ExpectColor(WholeBody - Triangle, 1.0f, 1.0f, 1.0f);
        // 0x4430a9: el 0x1a6, la malla 1 dos veces, la 0 al alpha del seno y la 2; con el cuarto argumento, cromadas.
        m_batch.Begin(Client::Render::Pass3D{});
        result = Draw(0x1a6, map, 0, Clock);
        ASSERT_TRUE(result.HasValue()) << result.Error();
        ASSERT_EQ(m_batch.Vertices().size(), 4 * Triangle);
        EXPECT_EQ(CommandAt(Triangle).state.blend, ModelBlend::Additive);
        EXPECT_FLOAT_EQ(m_batch.Vertices()[2 * Triangle].color.alpha,
                        static_cast<float>(WaveAt(Clock, 0x3a9d4952u, 0x3e4ccccdu, 0x3f4ccccdu)));
        EXPECT_FLOAT_EQ(m_batch.Vertices()[3 * Triangle].position.x, 2.0f);
        m_batch.Begin(Client::Render::Pass3D{});
        result = Draw(0x1a6, map, 0x43, Clock);
        ASSERT_TRUE(result.HasValue()) << result.Error();
        ASSERT_EQ(m_batch.Vertices().size(), 6 * Triangle);
        ExpectColor(0, static_cast<float>(glow * static_cast<double>(std::bit_cast<float>(0x3dcccccdu))),
                    static_cast<float>(glow),
                    static_cast<float>(static_cast<double>(
                        static_cast<float>(glow * static_cast<double>(std::bit_cast<float>(0x3dcccccdu))))));
        EXPECT_EQ(TextureAt(Triangle), 0x5c0);
        EXPECT_EQ(TextureAt(5 * Triangle), 0x5c0);
        EXPECT_EQ(m_color, glm::vec3(1.0f));
        // 0x4432b0: el 0x1a7, las mallas 0 y 1 (cromadas con el cuarto argumento).
        m_batch.Begin(Client::Render::Pass3D{});
        ASSERT_TRUE(Draw(0x1a7, map, 0, Clock).HasValue());
        ASSERT_EQ(m_batch.Vertices().size(), 2 * Triangle);
        ExpectColor(
            0, 0.1f, static_cast<float>(0.4 * static_cast<double>(std::bit_cast<float>(0x3f19999au))),
            static_cast<float>(static_cast<double>(0.6f) * static_cast<double>(std::bit_cast<float>(0x3f4ccccdu))));
        ExpectColor(Triangle, 0.5f, std::bit_cast<float>(0x3f4ccccdu), std::bit_cast<float>(0x3f19999au));
        m_batch.Begin(Client::Render::Pass3D{});
        ASSERT_TRUE(Draw(0x1a7, map, 0x43, Clock).HasValue());
        ASSERT_EQ(m_batch.Vertices().size(), 4 * Triangle);
        EXPECT_EQ(TextureAt(Triangle), 0x5c0);
        // 0x4433de: el 0x1a9, RenderBody(2) sin la malla 5, la 5 con 0x63b y con la suya al alpha 1, la 4 y las 0, 1 y
        // 3 cromadas y la 0 con 0x63a.
        m_actor->alpha = 0.75f;
        m_batch.Begin(Client::Render::Pass3D{});
        result = Draw(0x1a9, map, 0, Clock);
        ASSERT_TRUE(result.HasValue()) << result.Error();
        ASSERT_EQ(m_batch.Vertices().size(), WholeBody + 6 * Triangle);
        EXPECT_FLOAT_EQ(m_actor->alpha, 1.0f);
        EXPECT_EQ(TextureAt(WholeBody - Triangle), 0x63b);
        EXPECT_EQ(TextureAt(WholeBody), 0x305);
        EXPECT_EQ(TextureAt(WholeBody + 5 * Triangle), 0x63a);
        EXPECT_EQ(m_color, glm::vec3(0.2f, 0.4f, 0.6f));
        m_actor->alpha = 1.0f;
    }
    EXPECT_EQ(m_cloth.random.State(), AfterRandoms(0));
}

TEST_F(MonsterBodyTest, The428df0CasesDrawLooseMeshesOnEveryMap)
{
    constexpr float Clock = 12345.0f;
    for (const std::int32_t map : {0, 2, 33})
    {
        SCOPED_TRACE(map);
        // 0x428e1b: el 0x1bb, la malla 0 con 1 y con (1, 0, 0) y 0x46, la 1, y la 2 con 1 y con (0,5; 0; 0) y 0x46.
        m_batch.Begin(Client::Render::Pass3D{});
        ASSERT_TRUE(Draw(0x1bb, map, 0, Clock).HasValue());
        ASSERT_EQ(m_batch.Vertices().size(), 5 * Triangle);
        ExpectColor(0, 1.0f, 1.0f, 1.0f);
        EXPECT_EQ(CommandAt(3).state.blend, ModelBlend::Additive);
        EXPECT_FLOAT_EQ(m_batch.Vertices()[6].position.x, 1.0f);
        EXPECT_FLOAT_EQ(m_batch.Vertices()[12].position.x, 2.0f);
        // 0x428f39: el 0x1b8 con 0,9, las mallas 0 a 2 y la 3 dos veces.
        m_batch.Begin(Client::Render::Pass3D{});
        ASSERT_TRUE(Draw(0x1b8, map, 0, Clock).HasValue());
        ASSERT_EQ(m_batch.Vertices().size(), 5 * Triangle);
        ExpectColor(0, std::bit_cast<float>(0x3f666666u), std::bit_cast<float>(0x3f666666u),
                    std::bit_cast<float>(0x3f666666u));
        EXPECT_FLOAT_EQ(m_batch.Vertices()[12].position.x, 3.0f);
        // 0x429047: el 0x1b7, la malla 0 sin luz con la U y la V en −(12345 % 10.000) · 0,0003f, con 0x46, y la 1.
        m_batch.Begin(Client::Render::Pass3D{});
        ASSERT_TRUE(Draw(0x1b7, map, 0, Clock).HasValue());
        ASSERT_EQ(m_batch.Vertices().size(), 3 * Triangle);
        const auto scroll = static_cast<float>(-2345.0 * static_cast<double>(std::bit_cast<float>(0x399d4952u)));
        EXPECT_FLOAT_EQ(m_batch.Vertices()[0].uv.x, scroll);
        EXPECT_FLOAT_EQ(m_batch.Vertices()[0].uv.y, scroll);
        EXPECT_FLOAT_EQ(m_batch.Vertices()[6].position.x, 1.0f);
    }
}

TEST_F(MonsterBodyTest, TheMap34MonstersGoThrough4339d0)
{
    // 0x4339fa: el 0x1b0 con 1 y sin la malla 5.
    ASSERT_TRUE(Draw(0x1b0, 34).HasValue());
    ASSERT_EQ(m_batch.Vertices().size(), WholeBody - Triangle);
    ExpectColor(0, 1.0f, 1.0f, 1.0f);
    EXPECT_FLOAT_EQ(m_batch.Vertices()[15].position.x, 6.0f);
    // 0x433a4b: el 0x1ab, cada malla con 2 y con 0x240 y 0x6b1.
    m_batch.Begin(Client::Render::Pass3D{});
    ASSERT_TRUE(Draw(0x1ab, 34).HasValue());
    ASSERT_EQ(m_batch.Vertices().size(), 2 * WholeBody);
    EXPECT_EQ(TextureAt(0), 0x300);
    EXPECT_EQ(TextureAt(3), 0x6b1);
    EXPECT_EQ(CommandAt(3).state.blend, ModelBlend::Additive);
    EXPECT_FLOAT_EQ(m_batch.Vertices()[39].position.x, 6.0f);
    // 0x433c75: el 0x1b4 sin el cuarto argumento, sin la malla 5; con él, (0,5; 0,5; 0,6) y el cromo.
    m_batch.Begin(Client::Render::Pass3D{});
    ASSERT_TRUE(Draw(0x1b4, 34).HasValue());
    EXPECT_EQ(m_batch.Vertices().size(), WholeBody - Triangle);
    m_batch.Begin(Client::Render::Pass3D{});
    ASSERT_TRUE(Draw(0x1b4, 34, 0x43).HasValue());
    ASSERT_EQ(m_batch.Vertices().size(), 2 * WholeBody);
    ExpectColor(0, 0.5f, 0.5f, std::bit_cast<float>(0x3f19999au));
    EXPECT_EQ(TextureAt(3), 0x5c0);
    // 0x43404c: el 0x171 con (0,6; 0,8; 0,6) y el cromo; en Lorencia y Devias, el común.
    m_batch.Begin(Client::Render::Pass3D{});
    ASSERT_TRUE(Draw(0x171, 34).HasValue());
    ASSERT_EQ(m_batch.Vertices().size(), 2 * WholeBody);
    ExpectColor(0, std::bit_cast<float>(0x3f19999au), std::bit_cast<float>(0x3f4ccccdu),
                std::bit_cast<float>(0x3f19999au));
    for (const std::int32_t map : {0, 2})
    {
        m_batch.Begin(Client::Render::Pass3D{});
        ASSERT_TRUE(Draw(0x171, map).HasValue());
        EXPECT_EQ(m_batch.Vertices().size(), WholeBody);
    }
}

TEST_F(MonsterBodyTest, The1afOfMap34KeepsItsUAndLeavesMesh2Unlit)
{
    constexpr float Clock = 12345.0f;
    const auto u = static_cast<float>(2345.0 * static_cast<double>(std::bit_cast<float>(0x3a03126fu)));
    // 0x433b2f: cada malla con 2 y la U guardada; desde la 2, el alpha en 0,3; al final el alpha en 1. Con
    // +0x78 & 2, el cuerpo de 0x602365 corre la U en la malla 2, que sigue sin luz (+0x88 del modelo en 2).
    m_actor->alpha = 0.75f;
    m_actor->flags78 = 2;
    const auto result = Draw(0x1af, 34, 0, Clock);
    ASSERT_TRUE(result.HasValue()) << result.Error();
    ASSERT_EQ(m_batch.Vertices().size(), 2 * WholeBody);
    EXPECT_FLOAT_EQ(m_actor->textureU, u);
    EXPECT_FLOAT_EQ(m_actor->alpha, 1.0f);
    EXPECT_EQ(m_actor->dword64, -1);
    EXPECT_FLOAT_EQ(m_batch.Vertices()[0].color.alpha, 0.75f);
    // La 2 con +0x64 en −2: aditiva, con la luz de +0x68 (0x4ecc30 mezcla toda malla con el blendMesh en −2).
    EXPECT_EQ(CommandAt(2 * Triangle).state.blend, ModelBlend::Additive);
    EXPECT_FLOAT_EQ(m_batch.Vertices()[3 * Triangle].color.alpha, std::bit_cast<float>(0x3e99999au));
    EXPECT_FLOAT_EQ(m_batch.Vertices()[0].uv.x, 0.0f);
    EXPECT_FLOAT_EQ(m_batch.Vertices()[2 * Triangle].uv.x, u);
    EXPECT_FLOAT_EQ(m_batch.Vertices()[WholeBody + 2 * Triangle].uv.x, u);
    EXPECT_FLOAT_EQ(m_batch.Vertices()[WholeBody].uv.x, 0.0f);
    // Fuera del 34, el común.
    for (const std::int32_t map : {0, 2})
    {
        m_actor->textureU = 0.0f;
        m_actor->flags78 = 0;
        m_batch.Begin(Client::Render::Pass3D{});
        ASSERT_TRUE(Draw(0x1af, map, 0, Clock).HasValue());
        EXPECT_EQ(m_batch.Vertices().size(), WholeBody);
        EXPECT_FLOAT_EQ(m_actor->textureU, 0.0f);
    }
    EXPECT_EQ(m_cloth.random.State(), AfterRandoms(0));
}

TEST_F(MonsterBodyTest, TheCastleTrapGoesThroughTheSiegeCase)
{
    const auto result = Draw(0x33, 30, 0, 500.0f);

    ASSERT_TRUE(result.HasValue()) << result.Error();
    // 0x42cc10 con el 0x33: RenderBody(0x42) con la U corrida, aditivo.
    ASSERT_EQ(m_batch.Vertices().size(), WholeBody);
    EXPECT_EQ(CommandAt(0).state.blend, ModelBlend::Additive);
}
} // namespace
