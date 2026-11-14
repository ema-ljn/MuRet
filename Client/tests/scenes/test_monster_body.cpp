#include "Scenes/MonsterBody.h"

#include "Core/Expected.h"
#include "Core/IClock.h"
#include "Data/Models/Model.h"
#include "Render/ModelBatch.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/HalfLifeMath.h"
#include "Render/Models/Skeleton.h"
#include "Render/Models/Skinning.h"
#include "Render/Pass3D.h"
#include "Render/TextureSlot.h"
#include "Scenes/ActorState.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/MonsterBodyPasses.h"

#include <gtest/gtest.h>

#include <glm/ext/vector_float3.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

// Los casos por tipo y por mapa de 0x6002e0 con un monstruo o un NPC (MonsterBody.h): vectores literales del ASM
// c32bbd5f, sin ejecutar el original. Un modelo sintético de siete mallas de un triángulo, cada una con su slot (0x300
// más su índice), deja ver qué pase dibuja qué malla con qué textura, qué color y qué mezcla.
namespace
{
using Client::Render::ModelBlend;
using Client::Scenes::ActorState;

constexpr std::size_t MeshCount = 7;
constexpr std::size_t Triangle = 3;
constexpr std::size_t WholeBody = MeshCount * Triangle;

std::uint32_t AfterRandoms(std::size_t count)
{
    Client::Render::Models::CrtRandom random{1};
    for (std::size_t call = 0; call < count; ++call)
        static_cast<void>(random.Next());
    return random.State();
}

class BodyModels final : public Client::Scenes::IModelLibrary
{
public:
    BodyModels()
    {
        using namespace Client::Render::Models;
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
    [[nodiscard]] const Client::Render::Models::Skeleton* SkeletonOf(std::uint16_t) const override
    {
        return &m_skeleton;
    }
    [[nodiscard]] std::span<const Client::Render::TextureSlot> MeshTextures(std::uint16_t) const override
    {
        return m_textures;
    }

private:
    Client::Render::Models::Skeleton m_skeleton;
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
        object.color = {0.2f, 0.4f, 0.6f};
        object.alpha = m_actor->alpha;
        m_meshes = Client::Scenes::SkinObjectMeshes(m_models, object, m_bones);
        Client::Scenes::MonsterBodyModel model{.batch = m_batch,
                                               .models = m_models,
                                               .object = object,
                                               .meshes = m_meshes,
                                               .frameClock = Client::Core::Duration{}};
        Client::Scenes::MonsterBodyFrame frame{.model = model,
                                               .actor = *m_actor,
                                               .bones = m_bones,
                                               .map = map,
                                               .special = special,
                                               .worldClock = worldClock,
                                               .random = m_cloth.random,
                                               .cloth = m_cloth,
                                               .siegeActive = false,
                                               .lastSoundId = &m_sound,
                                               .originalFrame = originalFrame,
                                               .cameraShake = &m_shake};
        auto result = Client::Scenes::DrawMonsterBody(frame);
        m_color = model.object.color;
        return result;
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
    Client::Render::Models::ClothWorld m_cloth;
    std::optional<std::int32_t> m_sound;
    float m_shake = 0.0f;
    std::vector<Client::Render::Models::BoneTransform> m_bones{Client::Render::Models::BoneTransform(1.0f)};
    std::vector<Client::Render::Models::SkinnedMesh> m_meshes;
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
            std::sin(static_cast<double>(Clock) * static_cast<double>(std::bit_cast<float>(0x3b03126fu))) *
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

TEST_F(MonsterBodyTest, The18eBeforeTwentyFiveStepsAsksItsSoundAndCutsTheEffect)
{
    m_cloth.logicSteps = 100;
    m_actor->stepStamp = 76;

    const auto result = Draw(0x18e, 2);

    ASSERT_FALSE(result.HasValue());
    EXPECT_NE(result.Error().find("0x4ee770"), std::string::npos);
    EXPECT_TRUE(m_batch.Vertices().empty());
    EXPECT_EQ(m_sound, 0x87);
    EXPECT_TRUE(m_actor->alive);
    EXPECT_EQ(m_cloth.random.State(), AfterRandoms(4));
}

TEST_F(MonsterBodyTest, TheDying18eAnd18fDieWithoutTheCounter)
{
    m_cloth.openCounterAccesses = 0;
    for (const std::uint16_t type : {std::uint16_t{0x18e}, std::uint16_t{0x18f}})
    {
        SCOPED_TRACE(type);
        m_actor->alive = true;
        m_actor->animation.action = 6;
        m_sound.reset();
        const auto result = Draw(type, 0);
        ASSERT_FALSE(result.HasValue());
        EXPECT_NE(result.Error().find("0x4ee770"), std::string::npos);
        EXPECT_FALSE(m_actor->alive);
        EXPECT_EQ(m_sound, 0x87);
    }
    EXPECT_EQ(m_cloth.random.State(), AfterRandoms(0));
    // Entre frames del original, sin el sonido.
    m_sound.reset();
    m_actor->alive = true;
    ASSERT_FALSE(Draw(0x18f, 0, 0, 0.0f, false).HasValue());
    EXPECT_FALSE(m_sound.has_value());
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
    // La clase de los bordes del 0x183 y la onda del 0x185 y del 0x197 después de lo que dibujan; la cadena; los tipos
    // que ninguna alta da.
    constexpr std::array cases{Case{0x183, 0, "0x6011f1"},  Case{0x185, 2, "0x4ed775"},  Case{0x197, 2, "0x4ed775"},
                               Case{0x19b, 30, "0x42e3d0"}, Case{0x1ff, 30, "0x42e3d0"}, Case{0x110, 0, "0x6004ed"},
                               Case{0x69, 0, "0x600a4a"},   Case{0x12a, 2, "0x600654"},  Case{0x111, 0, "0x60246a"},
                               Case{0x11d, 0, "0x43ed20"},  Case{0x13d, 2, "0x442e10"}};
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

// sin(T · rate) · factor + base en double, como la x87 con PC53 (el seno de la CRT basta en los relojes de las
// pruebas).
double WaveAt(float clock, std::uint32_t rate, std::uint32_t factor, std::uint32_t base)
{
    return std::sin(static_cast<double>(clock) * static_cast<double>(std::bit_cast<float>(rate))) *
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
        // 0x43f945: el 0x197, las mallas 0, 1, 3 y 5 y +0x68 en 1 (0,5 con el subtipo 9) antes de la onda.
        for (const std::int32_t subtype : {0, 9})
        {
            m_actor->subtype = subtype;
            m_actor->blendMeshLight = 0.25f;
            m_batch.Begin(Client::Render::Pass3D{});
            result = Draw(0x197, map, 0, Clock);
            ASSERT_FALSE(result.HasValue());
            EXPECT_EQ(result.Error().rfind("0x4ed775", 0), 0u) << result.Error();
            ASSERT_EQ(m_batch.Vertices().size(), 4 * Triangle);
            EXPECT_FLOAT_EQ(m_batch.Vertices()[3 * Triangle].position.x, 5.0f);
            EXPECT_FLOAT_EQ(m_actor->blendMeshLight, subtype == 9 ? 0.5f : 1.0f);
        }
        m_actor->subtype = 0;
        m_actor->blendMeshLight = 1.0f;
    }
    EXPECT_EQ(m_cloth.random.State(), AfterRandoms(0));
}

TEST_F(MonsterBodyTest, TheFadingDeathOf43ed20CutsBeforeDrawing)
{
    m_actor->animation.action = 6;
    m_actor->alpha = 0.25f;
    for (const std::uint16_t type : {std::uint16_t{0x191}, std::uint16_t{0x195}, std::uint16_t{0x197}})
    {
        SCOPED_TRACE(type);
        m_batch.Begin(Client::Render::Pass3D{});
        const auto result = Draw(type, 2);
        ASSERT_FALSE(result.HasValue());
        EXPECT_NE(result.Error().find("0x4ee770(0, 0x607)"), std::string::npos) << result.Error();
        EXPECT_TRUE(m_batch.Vertices().empty());
    }
    // Con el alpha en 0,5, dibuja.
    m_actor->alpha = 0.5f;
    m_batch.Begin(Client::Render::Pass3D{});
    ASSERT_TRUE(Draw(0x193, 0).HasValue());
    EXPECT_EQ(m_batch.Vertices().size(), WholeBody + Triangle);
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
        // 0x43f182: al morir desde el frame 8, el sonido 0x1de, +0x60 en 0x5a y el primer rand() de los joints.
        m_sound.reset();
        m_actor->animation = {.action = 6, .frame = 8.0f};
        m_cloth.random = Client::Render::Models::CrtRandom{1};
        m_batch.Begin(Client::Render::Pass3D{});
        result = Draw(0x192, map);
        ASSERT_FALSE(result.HasValue());
        EXPECT_EQ(result.Error().rfind("0x43f20d", 0), 0u) << result.Error();
        EXPECT_EQ(m_sound, 0x1de);
        EXPECT_EQ(m_actor->dword60, 0x5a);
        EXPECT_TRUE(m_batch.Vertices().empty());
        EXPECT_EQ(m_cloth.random.State(), AfterRandoms(1));
        // Con +0x60 en 0x5a y el frame hasta 14,8, el cuerpo.
        m_actor->animation.frame = std::bit_cast<float>(0x416ccccdu);
        m_batch.Begin(Client::Render::Pass3D{});
        ASSERT_TRUE(Draw(0x192, map).HasValue());
        EXPECT_EQ(m_batch.Vertices().size(), WholeBody + 6 * Triangle);
        // 0x43edff: pasado 14,8, el sonido 0x1dd, +0x60 en 10, la malla 0 y el temblor en (rand() % 8 − 8) · 0,1f.
        m_actor->animation.frame = 15.0f;
        m_cloth.random = Client::Render::Models::CrtRandom{1};
        m_batch.Begin(Client::Render::Pass3D{});
        result = Draw(0x192, map);
        ASSERT_FALSE(result.HasValue());
        EXPECT_EQ(result.Error().rfind("0x43eeb9", 0), 0u) << result.Error();
        EXPECT_EQ(m_sound, 0x1dd);
        EXPECT_EQ(m_actor->dword60, 10);
        ASSERT_EQ(m_batch.Vertices().size(), Triangle);
        Client::Render::Models::CrtRandom replica{1};
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
