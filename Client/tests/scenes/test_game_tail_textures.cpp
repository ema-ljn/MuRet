#include "Scenes/GameModels.h"

#include "Core/Expected.h"
#include "Data/Models/ModelFiles.h"
#include "Data/Models/Model.h"
#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Data/Textures/Sampling.h"
#include "Data/Textures/Texture.h"
#include "Data/Textures/TextureSet.h"
#include "Render/Billboard.h"
#include "Render/ITextureStore.h"
#include "Render/ModelBatch.h"
#include "Render/Models/Cloth.h"
#include "Render/TextureSlot.h"
#include "Scenes/FaunaJointCreation.h"
#include "Scenes/FaunaObject.h"
#include "Scenes/GameDrawTail.h"
#include "Scenes/TextureMetadata.h"
#include "World/IFishStepServices.h"
#include "World/Sprites.h"

#include "support/FakeTextureStore.h"
#include "support/GoldenDir.h"
#include "support/RecordingSink.h"

#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

// Bindings acreditados por ASM y sus archivos físicos de Data: un lote con el selector correcto
// no basta. Cada handle debe resolver a píxeles subidos, metadata y sampling de la llamada nativa.
namespace
{
using Client::Data::Textures::Filter;
using Client::Data::Textures::PixelFormat;
using Client::Data::Textures::Wrap;

struct Resource
{
    std::uint16_t selector;
    const char* path;
    int width;
    int height;
    PixelFormat format;
    Filter filter;
    Wrap wrap;
};

// 61bd9f..61bdb4: FontTest, slot1, NEAREST/CLAMP; 621799 y621889 lo pasan a621610→620780.
constexpr Resource DamageAtlas{1, "Interface\\FontTest.tga", 256, 32, PixelFormat::Rgba, Filter::Nearest, Wrap::Clamp};
constexpr std::array<Resource, 5> JointResources{{
    {0x610, "Effect\\Flare02.jpg", 64, 64, PixelFormat::Rgb, Filter::Linear, Wrap::Repeat},
    {0x613, "Effect\\JointSpirit01.jpg", 256, 32, PixelFormat::Rgb, Filter::Linear, Wrap::Clamp},
    {0x617, "Effect\\Spark01.jpg", 8, 4, PixelFormat::Rgb, Filter::Linear, Wrap::Clamp},
    {0x618, "Effect\\JointLaser01.jpg", 64, 16, PixelFormat::Rgb, Filter::Linear, Wrap::Clamp},
    {0x697, "Effect\\ScolTail.jpg", 64, 16, PixelFormat::Rgb, Filter::Linear, Wrap::Clamp},
}};

class RejectDamageAtlas final : public Client::Render::ITextureStore
{
public:
    [[nodiscard]] Client::Core::Expected<void, std::string>
    Load(std::span<const Client::Data::Textures::LoadedTexture> entries) override
    {
        if (std::ranges::any_of(entries, [](const auto& entry) { return entry.slot == 1; }))
            return Client::Core::Unexpected<std::string>("el atlas de daño no pudo subirse");
        return physical.Load(entries);
    }
    void Release(Client::Render::TextureSlot slot) override
    {
        physical.Release(slot);
    }

    Client::Tests::FakeTextureStore physical;
};

class GameTailTextureTest : public Client::Tests::GoldenDataTest
{
protected:
    void SetUp() override
    {
        Client::Tests::GoldenDataTest::SetUp();
        ASSERT_FALSE(HasFatalFailure());
        const auto loaded = m_models.Load(m_dataRoot, m_store, m_log);
        ASSERT_TRUE(loaded.HasValue()) << loaded.Error();
    }

    void ExpectResource(const Resource& expected) const
    {
        SCOPED_TRACE(expected.path);
        const auto metadata = m_models.TextureMetadataOf(expected.selector);
        ASSERT_TRUE(metadata.has_value());
        const auto handle = m_models.TextureSlotOf(expected.selector);
        ASSERT_TRUE(handle.has_value());
        EXPECT_EQ(handle->value, expected.selector);
        const auto uploaded = m_store.Find(*handle);
        ASSERT_TRUE(uploaded.has_value());
        EXPECT_EQ(uploaded->texture.width, expected.width);
        EXPECT_EQ(uploaded->texture.height, expected.height);
        EXPECT_EQ(uploaded->texture.textureWidth, expected.width);
        EXPECT_EQ(uploaded->texture.textureHeight, expected.height);
        EXPECT_EQ(uploaded->texture.format, expected.format);
        EXPECT_EQ(uploaded->sampling.filter, expected.filter);
        EXPECT_EQ(uploaded->sampling.wrap, expected.wrap);
        EXPECT_EQ(metadata->size, (glm::vec2{static_cast<float>(expected.width), static_cast<float>(expected.height)}));
        EXPECT_EQ(metadata->components, Client::Data::Textures::BytesPerPixel(expected.format));
        // Referencia independiente de las tablas de producción: la ruta literal del callsite del ASM.
        const std::array<Client::Data::Textures::TextureSetEntry, 1> native{Client::Data::Textures::TextureSetEntry{
            expected.path, expected.selector, {expected.filter, expected.wrap}}};
        const auto decoded = Client::Data::Textures::LoadTextureSet(m_dataRoot, native);
        ASSERT_TRUE(decoded.HasValue()) << Client::Data::Textures::Describe(decoded.Error());
        ASSERT_EQ(decoded.Value().size(), 1u);
        EXPECT_EQ(uploaded->texture.pixels, decoded.Value()[0].texture.pixels);
        EXPECT_FALSE(uploaded->texture.pixels.empty());
    }

    Client::Tests::RecordingSink m_sink;
    Client::Core::Log m_log{m_sink, Client::Core::LogLevel::Debug};
    Client::Tests::FakeTextureStore m_store;
    Client::Scenes::GameModels m_models;
};

TEST_F(GameTailTextureTest, DamageGlyphBindingsResolveToTheUploadedNativeFontTestAtlas)
{
    ASSERT_NO_FATAL_FAILURE(ExpectResource(DamageAtlas));
    std::array<Client::Scenes::DamageNumberState, 2> damage{};
    for (auto& number : damage)
    {
        number.alive = true;
        number.position = {100, 200, 100};
        number.scale = 16;
        number.speed = 2;
        number.light = {1, 0.5f, 0.25f};
    }
    damage[0].number = 12;
    damage[1].number = -1;
    Client::Render::ModelBatch batch;
    const auto view = Client::Render::ViewRows(glm::mat4(1.0f));

    const auto drawn = Client::Scenes::DrawDamage(batch, damage, 0, view);

    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    EXPECT_EQ(batch.Vertices().size(), 18u);
    for (const auto& command : batch.Commands())
    {
        if (command.vertexCount == 0)
            continue;
        ASSERT_TRUE(command.state.texture.has_value());
        EXPECT_EQ(command.state.texture->value, DamageAtlas.selector);
        const auto handle = m_models.TextureSlotOf(command.state.texture->value);
        ASSERT_TRUE(handle.has_value());
        EXPECT_TRUE(m_store.Find(*handle).has_value());
    }
}

TEST_F(GameTailTextureTest, SupportedFaunaJointBindingsResolveToTheirUploadedNativePixels)
{
    struct Case
    {
        std::int32_t type;
        std::int32_t subtype;
        std::size_t resource;
    };
    constexpr std::array cases{Case{0x610, 8, 0}, Case{0x10f, 1, 1}, Case{0x10f, 3, 1},
                               Case{0x617, 0, 2}, Case{0x618, 4, 3}, Case{0x697, 0, 4}};
    for (const Case& probe : cases)
    {
        SCOPED_TRACE(probe.type);
        SCOPED_TRACE(probe.subtype);
        std::array<Client::Scenes::JointState, 1> joints{};
        Client::Scenes::FaunaObject owner;
        owner.active = 1;
        owner.position = {100, 200, 100};
        owner.previousPosition = {90, 190, 100};
        owner.light = {1, 1, 1};
        glm::vec3 first = owner.position;
        glm::vec3 second = owner.previousPosition;
        glm::vec3 angles{};
        Client::Render::Models::CrtRandom random(1);
        const Client::World::FishEffectRequest request{.type = probe.type,
                                                       .firstPosition = first,
                                                       .secondPosition = second,
                                                       .angles = angles,
                                                       .subtype = probe.subtype,
                                                       .ownerContext = &owner,
                                                       .scale = 30,
                                                       .key = -1,
                                                       .skill = 0,
                                                       .skillSerial = 0,
                                                       .target = -1};
        const auto created = Client::Scenes::CreateFaunaJoint(joints, request, random);
        ASSERT_TRUE(created.HasValue()) << created.Error();
        ASSERT_TRUE(created.Value().has_value());
        auto& joint = joints[*created.Value()];
        ASSERT_EQ(joint.texture, JointResources[probe.resource].selector);
        ASSERT_NO_FATAL_FAILURE(ExpectResource(JointResources[probe.resource]));
        // Un segmento válido del contrato de dibujo; este test no simula el movimiento de la fauna.
        joint.count = 1;
        for (std::size_t point = 0; point < 4; ++point)
        {
            joint.points[0][point] = {static_cast<float>(point & 1u), static_cast<float>(point >> 1u), 0};
            joint.points[1][point] = joint.points[0][point] + glm::vec3{0, 0, 1};
        }
        Client::World::FrameSprites sprites;
        const Client::Scenes::JointDrawResources resources{random, sprites};
        Client::Render::ModelBatch batch;

        const auto drawn = Client::Scenes::DrawJoints(batch, joints, 0, resources);

        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
        EXPECT_FALSE(batch.Vertices().empty());
        ASSERT_TRUE(batch.State().texture.has_value());
        EXPECT_EQ(batch.State().texture->value, JointResources[probe.resource].selector);
        for (const auto& sprite : sprites.Sprites())
        {
            if (!sprite.alive)
                continue;
            const auto handle = m_models.TextureSlotOf(sprite.texture);
            ASSERT_TRUE(handle.has_value());
            EXPECT_TRUE(m_store.Find(*handle).has_value());
        }
    }
}

TEST_F(GameTailTextureTest, ARejectedDamageAtlasNeverPublishesAHandleOrMetadata)
{
    RejectDamageAtlas store;
    Client::Scenes::GameModels models;

    const auto loaded = models.Load(m_dataRoot, store, m_log);

    ASSERT_TRUE(loaded.HasValue()) << loaded.Error(); // política previa de GameModels: avisa y sigue
    EXPECT_FALSE(models.TextureMetadataOf(1).has_value());
    EXPECT_FALSE(models.TextureSlotOf(1).has_value());
    EXPECT_FALSE(store.physical.Find(Client::Render::TextureSlot{1}).has_value());
    // Los recursos que sí subieron mantienen su handle y su almacenamiento físico.
    const auto spark = models.TextureSlotOf(0x617);
    ASSERT_TRUE(spark.has_value());
    EXPECT_TRUE(store.physical.Find(*spark).has_value());
}
} // namespace

TEST_F(GameTailTextureTest, ExtendedTailBindingsUseTheNativeFilesAndSampling)
{
    // ASM61d511/61d9b4/61d9ef/61da0b/61da2a/61da9d/61dba2/61dcc3.
    constexpr std::array<Resource, 8> resources{{
        {0x5da, "Effect\\Fire02.jpg", 64, 64, PixelFormat::Rgb, Filter::Linear, Wrap::Clamp},
        {0x5bb, "Effect\\blur01.jpg", 32, 64, PixelFormat::Rgb, Filter::Nearest, Wrap::Clamp},
        {0x5bc, "Effect\\motion_blur.jpg", 32, 64, PixelFormat::Rgb, Filter::Nearest, Wrap::Clamp},
        {0x5bd, "Effect\\motion_blur_r.jpg", 32, 64, PixelFormat::Rgb, Filter::Nearest, Wrap::Clamp},
        {0x639, "Effect\\blur02.jpg", 64, 128, PixelFormat::Rgb, Filter::Nearest, Wrap::Clamp},
        {0x614, "Effect\\JointThunder01.jpg", 256, 32, PixelFormat::Rgb, Filter::Linear, Wrap::Repeat},
        {0x61c, "Effect\\motion_blur_r2.jpg", 64, 128, PixelFormat::Rgb, Filter::Linear, Wrap::Repeat},
        {0x62c, "Effect\\NSkill.jpg", 32, 32, PixelFormat::Rgb, Filter::Linear, Wrap::Clamp},
    }};
    for (const auto& resource : resources)
        ASSERT_NO_FATAL_FAILURE(ExpectResource(resource));
}

TEST_F(GameTailTextureTest, RidingSpearModelAndMeshTexturesComeFromTheNativeSkillFile)
{
    // 6161b5:60da30(10f,"Data\\Skill\\","RidingSpear",1);61619f folderSkill.
    const auto expected = Client::Data::Models::LoadModelFile(m_dataRoot, {0x10f, "Skill\\", "RidingSpear", 1});
    ASSERT_TRUE(expected.HasValue());
    const auto* model = m_models.Find(0x10f);
    ASSERT_NE(model, nullptr);
    EXPECT_EQ(model->name, expected.Value().name);
    EXPECT_EQ(model->meshes.size(), expected.Value().meshes.size());
    ASSERT_NE(m_models.SkeletonOf(0x10f), nullptr);
    const auto textures = m_models.MeshTextures(0x10f);
    ASSERT_EQ(textures.size(), model->meshes.size());
    ASSERT_FALSE(textures.empty());
    for (const auto slot : textures)
        EXPECT_TRUE(m_store.Find(slot).has_value());
}
