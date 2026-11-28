#include "Models/IModelLibrary.h"
#include "Models/MeshTraits.h"
#include "Actors/Drawing/CharacterDrawing.h"
#include "Models/SkinningBuffers.h"

#include "Crt/CrtRandom.h"
#include "Data/Models/Model.h"
#include "Tables/PlayerModels.h"
#include "Kernel/CharacterAppearance.h"
#include "Render/ModelBatch.h"
#include "Render/ModelVertex.h"
#include "Models/Cloth.h"
#include "Math/HalfLifeMath.h"
#include "Math/OriginalRotation.h"
#include "Models/PassColors.h"
#include "Models/Pose.h"
#include "Math/RoundedMath.h"
#include "Models/Skeleton.h"
#include "Models/Skinning.h"
#include "Models/ModelId.h"
#include "Render/Pass3D.h"
#include "Render/SpriteVertex.h"
#include "Render/TextureSlot.h"
#include "Actors/Drawing/ScepterGlow.h"
#include "World/Sprites.h"

#include <gtest/gtest.h>

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <ios>
#include <limits>
#include <numbers>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

// Actors/Drawing/CharacterDrawing.h: the skin and hair slot of each class (0x50d41c) and the hidden mesh (06 § El
// modelo en el binario). The whole draw goes against the trace in GoldenCharacterDrawing.
namespace
{
using Client::Actors::Drawing::DrawCharacter;
using Client::Actors::Drawing::DrawDarkHorse;
using Client::Actors::Drawing::DrawnCharacter;
using Client::Actors::Drawing::DrawnObject;
using Client::Actors::Drawing::DrawObject;
using Client::Actors::Drawing::MeshSlotOf;
using Client::Actors::Drawing::SkinIndexOf;
using Client::Data::Models::Mesh;
using Client::Data::Models::Model;
using Client::Math::RoundedCos;
using Client::Math::RoundedSin;
using Client::Models::ActionInfo;
using Client::Models::BoneKey;
using Client::Models::IModelLibrary;
using Client::Models::Skeleton;
using Client::Models::SkeletonBone;
using Client::Render::ModelBatch;
using Client::Render::ModelBlend;
using Client::Render::ModelDrawState;
using Client::Render::TextureSlot;

// A skeleton of `bones` bones without a parent, all at the origin, with an action of one key.
Skeleton FlatSkeleton(std::size_t bones)
{
    Skeleton skeleton;
    SkeletonBone bone;
    bone.parent = -1;
    bone.actions = {{BoneKey{}}};
    bone.actions[0][0].rotation = Client::Math::AngleQuaternion({0.0f, 0.0f, 0.0f});
    skeleton.bones.assign(bones, bone);
    skeleton.actions = {ActionInfo{.repeats = true, .speed = 0.0f, .keyCount = 1, .lockPositions = false}};
    return skeleton;
}

// A model of one mesh with a triangle, hanging from bone 0.
Model OneTriangle()
{
    Mesh mesh;
    mesh.vertices = {{0, {0, 0, 0}}, {0, {1, 0, 0}}, {0, {0, 1, 0}}};
    mesh.normals = {{0, {0, 0, 1}, 0}};
    mesh.texCoords = {{0, 0}};
    mesh.triangles = {{3, {0, 1, 2, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}}};
    mesh.textureName = "pet.jpg";
    Model model;
    model.meshes = {mesh};
    return model;
}

// Player.bmd with the 48 bones that hang things, and two pets of one triangle.
class TwoPets final : public IModelLibrary
{
public:
    TwoPets() : m_body(FlatSkeleton(48)), m_pet(FlatSkeleton(1)), m_model(OneTriangle()) {}

    [[nodiscard]] const Model* Find(Client::Models::ModelId model) const override
    {
        return model.Value() == 0x1c12 || model.Value() == 0x1c13 ? &m_model : nullptr;
    }
    [[nodiscard]] const Skeleton* SkeletonOf(Client::Models::ModelId model) const override
    {
        if (model.Value() == 0x20a)
        {
            return &m_body;
        }
        return model.Value() == 0x1c12 || model.Value() == 0x1c13 ? &m_pet : nullptr;
    }
    [[nodiscard]] std::span<const TextureSlot> MeshTextures(Client::Models::ModelId /*model*/) const override
    {
        return {};
    }

private:
    Skeleton m_body;
    Skeleton m_pet;
    Model m_model;
};

// Player.bmd and, for `drawn`, a model of `meshes` meshes of one triangle hanging from bone 0, with the texture
// `texture` in `slot`; the others are not there.
class OneModel final : public IModelLibrary
{
public:
    explicit OneModel(std::uint16_t drawn, std::string texture = "part.jpg", std::size_t meshes = 1,
                      TextureSlot slot = TextureSlot{0x300})
        : m_body(FlatSkeleton(48)), m_item(FlatSkeleton(1)), m_model(OneTriangle()), m_drawn(drawn)
    {
        m_model.meshes[0].textureName = std::move(texture);
        m_model.meshes.resize(meshes, m_model.meshes[0]);
        // Each mesh with the texture of its index, like all of the lab's.
        for (std::size_t index = 0; index < meshes; ++index)
        {
            m_model.meshes[index].texture = static_cast<std::int16_t>(index);
        }
        m_slots.assign(meshes, slot);
    }

    [[nodiscard]] const Model* Find(Client::Models::ModelId model) const override
    {
        return Has(model.Value()) ? &m_model : nullptr;
    }
    [[nodiscard]] const Skeleton* SkeletonOf(Client::Models::ModelId model) const override
    {
        if (model.Value() == 0x20a)
        {
            return &m_body;
        }
        return Has(model.Value()) ? &m_item : nullptr;
    }
    [[nodiscard]] std::span<const TextureSlot> MeshTextures(Client::Models::ModelId model) const override
    {
        if (model.Value() == m_drawn)
        {
            return m_slots;
        }
        const auto other = std::ranges::find(m_others, model.Value(), &Other::model);
        return other != m_others.end() ? std::span<const TextureSlot>(other->slots) : std::span<const TextureSlot>{};
    }
    // Read from the meshes as they are now, as a load would: a test may change a texture name after building.
    [[nodiscard]] std::span<const Client::Models::MeshTraits> MeshTraitsOf(Client::Models::ModelId model) const override
    {
        if (!Has(model.Value()))
            return {};
        m_traits = Client::Models::ReadMeshTraits(m_model);
        for (const std::size_t mesh : m_withoutShine)
            m_traits[mesh].shines = false;
        return m_traits;
    }
    // As 0x60ff50 marks it at the end of the load.
    void WithoutShine(std::size_t mesh)
    {
        m_withoutShine.push_back(mesh);
    }

    // The same model also as `model`, with all its meshes in `slot`: to see in which order they go.
    void AlsoAs(std::uint16_t model, TextureSlot slot)
    {
        m_others.push_back(Other{model, std::vector<TextureSlot>(m_slots.size(), slot)});
    }

    // The items' skeleton, to give its bone 0 a pose of its own.
    Skeleton& ItemSkeleton()
    {
        return m_item;
    }

    // To build a broken mesh.
    Mesh& FirstMesh()
    {
        return m_model.meshes[0];
    }
    Mesh& MeshAt(std::size_t index)
    {
        return m_model.meshes[index];
    }

private:
    struct Other
    {
        std::uint16_t model = 0;
        std::vector<TextureSlot> slots;
    };
    mutable std::vector<Client::Models::MeshTraits> m_traits;
    std::vector<std::size_t> m_withoutShine;

    [[nodiscard]] bool Has(std::uint16_t model) const
    {
        return model == m_drawn || std::ranges::find(m_others, model, &Other::model) != m_others.end();
    }

    Skeleton m_body;
    Skeleton m_item;
    Model m_model;
    std::uint16_t m_drawn;
    std::vector<TextureSlot> m_slots;
    std::vector<Other> m_others;
};

ModelBatch Drawn(const OneModel& models, const DrawnCharacter& character)
{
    ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});
    DrawCharacter(batch, models, character, std::chrono::milliseconds{0});
    return batch;
}

void ExpectColor(const Client::Render::ModelVertex& vertex, float red, float green, float blue)
{
    EXPECT_FLOAT_EQ(vertex.color.red, red);
    EXPECT_FLOAT_EQ(vertex.color.green, green);
    EXPECT_FLOAT_EQ(vertex.color.blue, blue);
}

// 0x60cf70 with its mode 1, the character under the mouse: before each part, two untextured additive passes (0x60cd60
// with 0x40), in (0.1, 0.03, 0) at 1.2 of the bone (+0x94) and in (0.7, 0.2, 0) at 1.2 − 0.02; then the part, as
// always.
TEST(CharacterDrawingTest, AnOutlinedPartGoesTwiceAroundItsBoneFirst)
{
    DrawnCharacter character;
    character.appearance = Client::Kernel::ClassAppearance(0, 0);
    character.outlined = true;
    const OneModel models(character.appearance.bodyParts[1].model);

    const ModelBatch batch = Drawn(models, character);

    ASSERT_EQ(batch.Commands().size(), 2u);
    EXPECT_EQ(batch.Commands()[0].state, (ModelDrawState{std::nullopt, ModelBlend::Additive, true}));
    EXPECT_EQ(batch.Commands()[0].vertexCount, 6u);
    EXPECT_EQ(batch.Commands()[1].state, (ModelDrawState{TextureSlot{0x300}, ModelBlend::Opaque, true}));
    const auto vertices = batch.Vertices();
    ASSERT_EQ(vertices.size(), 9u);
    ExpectColor(vertices[0], 0.1f, 0.03f, 0.0f);
    EXPECT_FLOAT_EQ(vertices[1].position.x, 1.2f);
    ExpectColor(vertices[3], 0.7f, 0.2f, 0.0f);
    EXPECT_FLOAT_EQ(vertices[4].position.x, 1.2f - 0.02f);
    ExpectColor(vertices[6], 0.4f, 0.4f, 0.4f);
    EXPECT_FLOAT_EQ(vertices[7].position.x, 1.0f);
}

TEST(CharacterDrawingTest, PublishesPartBoxAtTheBodyPartDrawBoundary)
{
    DrawnCharacter character;
    character.appearance = Client::Kernel::ClassAppearance(0, 0);
    for (auto& part : character.appearance.bodyParts)
        part.model = 0x1234;
    int published = 0;
    character.onPartBox = [&published](const glm::vec3&) { ++published; };
    const auto draw = [&character](const OneModel& models, Client::Actors::Drawing::CharacterDrawPass pass)
    {
        ModelBatch batch;
        batch.Begin(Client::Render::Pass3D{});
        DrawCharacter(batch, models, character, {}, pass);
        return batch.Commands().size();
    };

    const OneModel missing(0x5678);
    EXPECT_EQ(draw(missing, Client::Actors::Drawing::CharacterDrawPass::Parts), 0u);
    EXPECT_EQ(published, character.appearance.bodyParts.size());

    published = 0;
    const OneModel zeroMeshes(0x1234, "part.jpg", 0);
    EXPECT_EQ(draw(zeroMeshes, Client::Actors::Drawing::CharacterDrawPass::Parts), 0u);
    EXPECT_EQ(published, character.appearance.bodyParts.size());

    published = 0;
    const OneModel hiddenMesh(0x1234, "part.jpg", 1, TextureSlot{0x12d});
    EXPECT_EQ(draw(hiddenMesh, Client::Actors::Drawing::CharacterDrawPass::Parts), 0u);
    EXPECT_EQ(published, character.appearance.bodyParts.size());

    published = 0;
    character.alpha = 0.01f;
    const OneModel models(0x1234);
    EXPECT_EQ(draw(models, Client::Actors::Drawing::CharacterDrawPass::Parts), 0u);
    EXPECT_EQ(published, 0);

    character.alpha = std::nextafter(0.01f, 1.0f);
    EXPECT_GT(draw(models, Client::Actors::Drawing::CharacterDrawPass::Parts), 0u);
    EXPECT_EQ(published, character.appearance.bodyParts.size());

    published = 0;
    character.alpha = std::numeric_limits<float>::quiet_NaN();
    EXPECT_EQ(draw(models, Client::Actors::Drawing::CharacterDrawPass::Parts), 0u);
    EXPECT_EQ(published, 0);

    published = 0;
    character.alpha = 1.0f;
    EXPECT_EQ(draw(models, Client::Actors::Drawing::CharacterDrawPass::Shadow), 0u);
    EXPECT_EQ(published, 0);

    DrawnCharacter hanging;
    hanging.appearance.pet = std::uint16_t{0x1c13};
    hanging.onPartBox = [&published](const glm::vec3&) { ++published; };
    const TwoPets attachedModels;
    ModelBatch hangingBatch;
    hangingBatch.Begin(Client::Render::Pass3D{});
    DrawCharacter(hangingBatch, attachedModels, hanging, {}, Client::Actors::Drawing::CharacterDrawPass::Hanging);
    EXPECT_EQ(published, 0);
}

// The two passes walk every mesh of the part, and the part goes after them (0x60cf70).
TEST(CharacterDrawingTest, TheOutlineGoesBeforeEveryMeshOfThePart)
{
    DrawnCharacter character;
    character.appearance = Client::Kernel::ClassAppearance(0, 0);
    character.outlined = true;
    const OneModel models(character.appearance.bodyParts[1].model, "part.jpg", 2);

    const ModelBatch batch = Drawn(models, character);

    ASSERT_EQ(batch.Commands().size(), 2u);
    EXPECT_EQ(batch.Commands()[0].state.blend, ModelBlend::Additive);
    EXPECT_EQ(batch.Commands()[0].vertexCount, 2u * 2u * 3u);
    EXPECT_EQ(batch.Commands()[1].vertexCount, 2u * 3u);
}

// The hidden mesh (0x12d, the one ModelTextureSlots sends there by its name) is not drawn, and neither is its outline.
TEST(CharacterDrawingTest, TheHiddenMeshHasNoOutline)
{
    DrawnCharacter character;
    character.appearance = Client::Kernel::ClassAppearance(0, 0);
    character.outlined = true;
    const OneModel models(character.appearance.bodyParts[1].model, "hide.jpg", 1, TextureSlot{0x12d});

    const ModelBatch batch = Drawn(models, character);

    EXPECT_TRUE(batch.Commands().empty());
}

// 0x4ecc30 with 0x40 without 0x2: a four-component mesh (an OZT) does not go in the outline.
TEST(CharacterDrawingTest, TheOutlineSkipsAFourComponentMesh)
{
    DrawnCharacter character;
    character.appearance = Client::Kernel::ClassAppearance(0, 0);
    character.outlined = true;
    const OneModel models(character.appearance.bodyParts[1].model, "part.tga");

    const ModelBatch batch = Drawn(models, character);

    ASSERT_EQ(batch.Commands().size(), 1u);
    EXPECT_EQ(batch.Commands()[0].state.blend, ModelBlend::Alpha);
}

// The outline belongs to the six parts (0x60cf70 in the loop of 0x50ba30): what hangs, through 0x509340, does not carry
// it.
TEST(CharacterDrawingTest, WhatHangsIsNotOutlined)
{
    DrawnCharacter character;
    character.appearance = Client::Kernel::ClassAppearance(0, 0);
    character.appearance.rightHand = Client::Kernel::WornItem{.model = 0x212};
    character.outlined = true;
    const OneModel models(0x212);

    const ModelBatch batch = Drawn(models, character);

    ASSERT_EQ(batch.Commands().size(), 1u);
    EXPECT_EQ(batch.Commands()[0].state.blend, ModelBlend::Opaque);
}

// The hands' loop of 0x50ba30 skips the bolts (0xa19), the arrows (0xa21) and the raven (0x1c17): they do not go in the
// hand. The bolts of the right and the arrows of the left do not go on the back either, and the raven is a separate
// object.
TEST(CharacterDrawingTest, ArrowsBoltsAndTheRavenAreNotInTheHand)
{
    for (const auto& [right, left] : {std::pair{std::uint16_t{0xa19}, std::uint16_t{0xa21}},
                                      std::pair{std::uint16_t{0x1c17}, std::uint16_t{0x1c17}}})
    {
        DrawnCharacter character;
        character.appearance = Client::Kernel::ClassAppearance(0, 0);
        character.appearance.rightHand = Client::Kernel::WornItem{.model = right};
        character.appearance.leftHand = Client::Kernel::WornItem{.model = left};
        OneModel rightModels(right);
        OneModel leftModels(left);

        EXPECT_TRUE(Drawn(rightModels, character).Vertices().empty()) << std::hex << right;
        EXPECT_TRUE(Drawn(leftModels, character).Vertices().empty()) << std::hex << left;
    }
}

// The pass colors of a +13 part (0x60b3d0): the base and the 0x1040 with the light times 0.9 (0x6093d0 multiplies by
// 1), and the 0x48 and the 0x44 with the item's color (0x608e00), without the light.
TEST(CharacterDrawingTest, AThirteenPartGlowsWithItsLightAndItsColor)
{
    DrawnCharacter character;
    character.appearance = Client::Kernel::ClassAppearance(0, 0);
    character.appearance.bodyParts[1].level = 13;
    const std::uint16_t part = character.appearance.bodyParts[1].model;
    const OneModel models(part);

    const ModelBatch batch = Drawn(models, character);

    const auto vertices = batch.Vertices();
    ASSERT_EQ(vertices.size(), 4u * 3u);
    const Client::Render::Color item = Client::Models::ShineColorOf(part);
    ExpectColor(vertices[0], 0.4f * 0.9f, 0.4f * 0.9f, 0.4f * 0.9f);
    ExpectColor(vertices[3], 0.4f * 0.9f, 0.4f * 0.9f, 0.4f * 0.9f);
    ExpectColor(vertices[6], item.red, item.green, item.blue);
    ExpectColor(vertices[9], item.red, item.green, item.blue);
}

// The 0x42 of an excellent item, with the pulsing color (ExcellentColorAt).
TEST(CharacterDrawingTest, TheExcellentPassPulses)
{
    DrawnCharacter character;
    character.appearance = Client::Kernel::ClassAppearance(0, 0);
    character.appearance.bodyParts[1].excellent = true;
    const OneModel models(character.appearance.bodyParts[1].model);

    ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});
    DrawCharacter(batch, models, character, std::chrono::milliseconds{250});

    const auto vertices = batch.Vertices();
    ASSERT_EQ(vertices.size(), 2u * 3u);
    const Client::Render::Color pulse = Client::Models::ExcellentColorAt(std::chrono::milliseconds{250});
    ExpectColor(vertices[3], pulse.red, pulse.green, pulse.blue);
}

// 0x509340 passes the set at 0 to 0x60b3d0: what hangs has no set pass.
TEST(CharacterDrawingTest, WhatHangsHasNoSetPass)
{
    DrawnCharacter character;
    character.appearance = Client::Kernel::ClassAppearance(0, 0);
    character.appearance.rightHand = Client::Kernel::WornItem{.model = 0x212, .ancient = true};
    const OneModel models(0x212);

    const ModelBatch batch = Drawn(models, character);

    EXPECT_EQ(batch.Commands().size(), 1u);
}

// 0x50ba30 draws the shadow (0x20b, Shadow01) before the parts, with the body's matrices: black, untextured, opaque and
// without writing the depth (0x60b3d0 with +0x98 at 1 and 0x4eff40), and flattened on the floor, at the character's z
// plus 5. Its mesh is the hidden one (hide_m, 0x12d), as in the Data folder: 0x4eff40 does not look at the texture.
TEST(CharacterDrawingTest, TheShadowGoesFirstBlackAndFlat)
{
    DrawnCharacter character;
    character.appearance = Client::Kernel::ClassAppearance(0, 0);
    character.object.position = {100.0f, 200.0f, 163.0f};
    const OneModel models(Client::Tables::PlayerShadowModel, "hide_m.jpg", 1, TextureSlot{0x12d});

    const ModelBatch batch = Drawn(models, character);

    ASSERT_EQ(batch.Commands().size(), 1u);
    EXPECT_EQ(batch.Commands()[0].state, (ModelDrawState{std::nullopt, ModelBlend::Shadow, true}));
    ASSERT_EQ(batch.Vertices().size(), 3u);
    for (const Client::Render::ModelVertex& vertex : batch.Vertices())
    {
        ExpectColor(vertex, 0.0f, 0.0f, 0.0f);
        EXPECT_FLOAT_EQ(vertex.position.z, 168.0f);
    }
}

// 0x4eff8d..0x4eff9f: player shadows use a spread of 2500 on map 30 and 2000 on every other map.
TEST(CharacterDrawingTest, ThePlayerShadowWidensOnlyInCastleSiege)
{
    DrawnCharacter character;
    character.appearance = Client::Kernel::ClassAppearance(0, 0);
    character.object.position = {100.0f, 200.0f, 163.0f};
    OneModel models(Client::Tables::PlayerShadowModel);
    models.FirstMesh().vertices[0].position = {100.0f, 0.0f, 50.0f};

    for (std::int32_t map = 0; map <= 255; ++map)
    {
        SCOPED_TRACE(map);
        character.map = static_cast<std::uint8_t>(map);
        const ModelBatch batch = Drawn(models, character);

        ASSERT_EQ(batch.Vertices().size(), 3u);
        const float x = map == 30 ? 0x1.4e2d5ep+7f : 0x1.5ad5ep+7f;
        EXPECT_EQ(batch.Vertices()[0].position, glm::vec3(x, 200.0f, 168.0f));
    }
}

// The shadow goes before the parts (0x50ba30 draws it before the parts' loop): with both, the first stretch is the
// shadow's.
TEST(CharacterDrawingTest, TheShadowGoesBeforeTheParts)
{
    DrawnCharacter character;
    character.appearance = Client::Kernel::ClassAppearance(0, 0);
    const std::uint16_t part = character.appearance.bodyParts[1].model;
    // A library with the shadow and a part, the same triangle.
    class ShadowAndPart final : public IModelLibrary
    {
    public:
        explicit ShadowAndPart(std::uint16_t drawn) : m_body(FlatSkeleton(48)), m_model(OneTriangle()), m_drawn(drawn)
        {
        }
        [[nodiscard]] const Model* Find(Client::Models::ModelId model) const override
        {
            return model.Value() == m_drawn || model.Value() == Client::Tables::PlayerShadowModel ? &m_model : nullptr;
        }
        [[nodiscard]] const Skeleton* SkeletonOf(Client::Models::ModelId model) const override
        {
            return model.Value() == 0x20a ? &m_body : nullptr;
        }
        [[nodiscard]] std::span<const TextureSlot> MeshTextures(Client::Models::ModelId /*model*/) const override
        {
            return {};
        }

    private:
        Skeleton m_body;
        Model m_model;
        std::uint16_t m_drawn;
    };
    const ShadowAndPart models(part);
    ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});

    DrawCharacter(batch, models, character, std::chrono::milliseconds{0});

    ASSERT_EQ(batch.Commands().size(), 2u);
    EXPECT_EQ(batch.Commands()[0].state.blend, ModelBlend::Shadow);
    EXPECT_EQ(batch.Commands()[1].state.blend, ModelBlend::Opaque);
}

// SkinObjectMeshesInto in buffers a bigger model left (three meshes, the first with more vertices): a model of two,
// the second broken, gets its two as SkinObjectMeshes skins them into fresh meshes, the broken one empty.
TEST(CharacterDrawingTest, ASmallModelSkinnedAfterABigOneMatchesFreshMeshes)
{
    constexpr std::uint16_t BigModel = 0x1c12;
    constexpr std::uint16_t SmallModel = 0x1c13;
    OneModel big(BigModel, "part.jpg", 3);
    big.FirstMesh().vertices.push_back({0, {0, 0, 1}});
    big.FirstMesh().normals.push_back({0, {1, 0, 0}, 0});
    OneModel small(SmallModel, "part.jpg", 2);
    small.MeshAt(1).triangles[0].vertices[0] = 9;
    DrawnObject bigObject;
    bigObject.model = BigModel;
    DrawnObject smallObject;
    smallObject.model = SmallModel;
    smallObject.transform.position = {5.0f, 6.0f, 7.0f};
    const std::vector<Client::Math::BoneTransform> bones(1, Client::Math::BoneTransform(1.0f));
    Client::Models::SkinnedMeshes buffers;
    static_cast<void>(Client::Actors::Drawing::SkinObjectMeshesInto(buffers, big, bigObject, bones));

    const auto reused = Client::Actors::Drawing::SkinObjectMeshesInto(buffers, small, smallObject, bones);

    const auto fresh = Client::Actors::Drawing::SkinObjectMeshes(small, smallObject, bones);
    ASSERT_EQ(reused.size(), fresh.size());
    for (std::size_t mesh = 0; mesh < fresh.size(); ++mesh)
    {
        EXPECT_EQ(reused[mesh].positions, fresh[mesh].positions) << mesh;
        EXPECT_EQ(reused[mesh].normals, fresh[mesh].normals) << mesh;
    }
    EXPECT_TRUE(reused[1].positions.empty());
}

// A character drawn in the buffers a bigger one left (three meshes in its part, its shadow and its sword, the first
// with one more vertex, somewhere else) draws as with buffers of its own.
TEST(CharacterDrawingTest, ACharacterDrawnInTheBuffersOfABiggerOneDrawsAsWithItsOwn)
{
    constexpr std::uint16_t Sword = 0x212;
    DrawnCharacter character;
    character.appearance = Client::Kernel::ClassAppearance(0, 0);
    character.appearance.rightHand = Client::Kernel::WornItem{.model = Sword};
    const std::uint16_t part = character.appearance.bodyParts[1].model;
    OneModel big(part, "part.jpg", 3);
    big.FirstMesh().vertices.push_back({0, {0, 0, 1}});
    big.FirstMesh().normals.push_back({0, {1, 0, 0}, 0});
    OneModel small(part, "part.jpg", 2);
    for (OneModel* models : {&big, &small})
    {
        models->AlsoAs(Client::Tables::PlayerShadowModel, TextureSlot{0x300});
        models->AlsoAs(Sword, TextureSlot{0x300});
    }
    Client::Models::SkinningBuffers buffers;
    DrawnCharacter lent = character;
    lent.drawBuffers = &buffers;
    static_cast<void>(Drawn(big, lent));
    character.object.position = {5.0f, 6.0f, 7.0f};
    lent.object.position = character.object.position;

    const ModelBatch reused = Drawn(small, lent);

    const ModelBatch own = Drawn(small, character);
    ASSERT_FALSE(own.Vertices().empty());
    ASSERT_EQ(reused.Vertices().size(), own.Vertices().size());
    EXPECT_EQ(reused.Commands().size(), own.Commands().size());
    for (std::size_t vertex = 0; vertex < own.Vertices().size(); ++vertex)
    {
        EXPECT_EQ(reused.Vertices()[vertex].position, own.Vertices()[vertex].position) << vertex;
        EXPECT_EQ(reused.Vertices()[vertex].uv, own.Vertices()[vertex].uv) << vertex;
        const Client::Render::Color& color = own.Vertices()[vertex].color;
        ExpectColor(reused.Vertices()[vertex], color.red, color.green, color.blue);
    }
}

// With one of the pets that are ridden (0x1c14 and 0x1c15), 0x50ba30 does not draw the shadow.
TEST(CharacterDrawingTest, ARiddenPetHasNoShadow)
{
    for (const std::uint16_t pet : {std::uint16_t{0x1c14}, std::uint16_t{0x1c15}})
    {
        DrawnCharacter character;
        character.appearance = Client::Kernel::ClassAppearance(0, 0);
        character.appearance.pet = pet;
        const OneModel models(Client::Tables::PlayerShadowModel);

        const ModelBatch batch = Drawn(models, character);

        EXPECT_TRUE(batch.Vertices().empty()) << std::hex << pet;
    }
}

// Without the mouse over it, the part alone.
TEST(CharacterDrawingTest, WithoutTheMouseThereIsNoOutline)
{
    DrawnCharacter character;
    character.appearance = Client::Kernel::ClassAppearance(0, 0);
    const OneModel models(character.appearance.bodyParts[1].model);

    const ModelBatch batch = Drawn(models, character);

    ASSERT_EQ(batch.Commands().size(), 1u);
    EXPECT_EQ(batch.Vertices().size(), 3u);
}

// A mesh whose triangle names a vertex, a normal or a UV it does not have (a broken .bmd: the original would read
// outside) is not drawn, not even in the outline; the model's others are.
TEST(CharacterDrawingTest, AMeshThatNamesWhatItLacksIsNotDrawn)
{
    DrawnCharacter character;
    character.appearance = Client::Kernel::ClassAppearance(0, 0);
    character.outlined = true;
    for (const int broken : {0, 1, 2})
    {
        OneModel models(character.appearance.bodyParts[1].model);
        Client::Data::Models::Triangle& triangle = models.FirstMesh().triangles[0];
        if (broken == 0)
        {
            triangle.vertices[1] = 3;
        }
        else if (broken == 1)
        {
            triangle.normals[2] = -1;
        }
        else
        {
            triangle.texCoords[0] = 1;
        }

        const ModelBatch batch = Drawn(models, character);

        EXPECT_TRUE(batch.Vertices().empty()) << broken;
    }
}

// Q-C98: of the pets, only 0x1c13 is drawn; the parts the library does not have are not either.
TEST(CharacterDrawingTest, OnlyHelper02IsDrawnAsThePet)
{
    const TwoPets models;
    for (const auto& [pet, vertices] : {std::pair{std::uint16_t{0x1c13}, 3u}, std::pair{std::uint16_t{0x1c12}, 0u}})
    {
        DrawnCharacter character;
        character.appearance = Client::Kernel::ClassAppearance(0, 0);
        character.appearance.pet = pet;
        ModelBatch batch;
        batch.Begin(Client::Render::Pass3D{});

        DrawCharacter(batch, models, character, std::chrono::milliseconds{0});

        EXPECT_EQ(batch.Vertices().size(), vertices) << pet;
    }
}

// 2 · (class & 7) + (class >> 3): class c uses 0x12e + 2c and its second class the one next to it.
TEST(CharacterDrawingTest, TheSkinGoesByClass)
{
    EXPECT_EQ(SkinIndexOf(0), 0);
    EXPECT_EQ(SkinIndexOf(1), 2);
    EXPECT_EQ(SkinIndexOf(8), 1);
    EXPECT_EQ(SkinIndexOf(4), 8);
    EXPECT_EQ(SkinIndexOf(12), 9);
}

// The skin goes to 0x12e plus the skin, the hair to 0x12f plus the skin (class 4, to hair_r's 0x137), the hidden one is
// not drawn, and the others stay in their slot.
TEST(CharacterDrawingTest, TheSkinAndHairSlotsFollowTheClass)
{
    EXPECT_EQ(MeshSlotOf(TextureSlot{0x12e}, 1), std::optional<TextureSlot>{TextureSlot{0x130}});
    EXPECT_EQ(MeshSlotOf(TextureSlot{0x12e}, 8), std::optional<TextureSlot>{TextureSlot{0x12f}});
    EXPECT_EQ(MeshSlotOf(TextureSlot{0x137}, 4), std::optional<TextureSlot>{TextureSlot{0x137}});
    EXPECT_EQ(MeshSlotOf(TextureSlot{0x12d}, 0), std::nullopt);
    EXPECT_EQ(MeshSlotOf(TextureSlot{0x6f8}, 3), std::optional<TextureSlot>{TextureSlot{0x6f8}});
}

// The draw of a +13 part, with the outline or without it, and the texture of its only mesh.
ModelBatch DrawnPart(const std::string& texture, bool outlined = false, std::uint8_t level = 13)
{
    DrawnCharacter character;
    character.appearance = Client::Kernel::ClassAppearance(0, 0);
    character.appearance.bodyParts[1].level = level;
    character.outlined = outlined;
    const OneModel models(character.appearance.bodyParts[1].model, texture);
    return Drawn(models, character);
}

// 0x4ef0c0: a mesh with H in the script is not drawn in any pass, not even in the outline's.
TEST(CharacterDrawingTest, AnHiddenScriptMeshIsNotDrawn)
{
    EXPECT_TRUE(DrawnPart("part_H.jpg").Commands().empty());
    EXPECT_TRUE(DrawnPart("part_H.jpg", true).Commands().empty());
}

// 0x4ef0c0 passes the mesh with R as the blendMesh, and 0x4ecc30 draws it with its texture, additive and with the color
// times the blendMesh's light (1 on a player, 0x511290); the glow passes go the same.
TEST(CharacterDrawingTest, AnRMeshIsTheAdditiveBlendMesh)
{
    const ModelBatch plain = DrawnPart("part.jpg");
    const ModelBatch blended = DrawnPart("part_R.jpg");

    ASSERT_EQ(blended.Commands().size(), 4u);
    EXPECT_EQ(blended.Commands()[0].state, (ModelDrawState{TextureSlot{0x300}, ModelBlend::Additive, true}));
    ExpectColor(blended.Vertices()[0], 0.4f * 0.9f, 0.4f * 0.9f, 0.4f * 0.9f);
    ASSERT_EQ(plain.Commands().size(), 4u);
    for (std::size_t pass = 1; pass < 4; ++pass)
    {
        EXPECT_EQ(blended.Commands()[pass].state, plain.Commands()[pass].state) << pass;
    }
    ASSERT_EQ(blended.Vertices().size(), plain.Vertices().size());
    for (std::size_t vertex = 3; vertex < plain.Vertices().size(); ++vertex)
    {
        EXPECT_EQ(blended.Vertices()[vertex].color, plain.Vertices()[vertex].color) << vertex;
    }
}

// 0x4ecc30 compares the blendMesh with the mesh's texture: with R but with another's texture, it goes as one without a
// script.
TEST(CharacterDrawingTest, AnRMeshWithTheTextureOfAnotherIsNotTheBlendMesh)
{
    DrawnCharacter character;
    character.appearance = Client::Kernel::ClassAppearance(0, 0);
    OneModel models(character.appearance.bodyParts[1].model, "part_R.jpg", 2);
    models.MeshAt(1).texture = 0;

    const ModelBatch batch = Drawn(models, character);

    ASSERT_EQ(batch.Commands().size(), 2u);
    EXPECT_EQ(batch.Commands()[0].state.blend, ModelBlend::Additive);
    EXPECT_EQ(batch.Commands()[1].state.blend, ModelBlend::Opaque);
}

// The blendMesh goes before the four-component blend: an OZT with R is additive.
TEST(CharacterDrawingTest, AnRFourComponentMeshIsAdditiveToo)
{
    const ModelBatch batch = DrawnPart("part_R.tga", false, 0);

    ASSERT_EQ(batch.Commands().size(), 1u);
    EXPECT_EQ(batch.Commands()[0].state, (ModelDrawState{TextureSlot{0x300}, ModelBlend::Additive, true}));
}

// The outline (0x60cd60 → 0x4ef0c0 with 0x40) also passes the blendMesh: the mesh with R goes with its texture and the
// outline's color times the light, even if it has four components.
TEST(CharacterDrawingTest, AnRMeshIsOutlinedWithItsTexture)
{
    for (const std::string texture : {"part_R.jpg", "part_R.tga"})
    {
        const ModelBatch batch = DrawnPart(texture, true, 0);

        ASSERT_EQ(batch.Commands().size(), 1u) << texture;
        EXPECT_EQ(batch.Commands()[0].state, (ModelDrawState{TextureSlot{0x300}, ModelBlend::Additive, true}))
            << texture;
        ASSERT_EQ(batch.Vertices().size(), 9u) << texture;
        ExpectColor(batch.Vertices()[0], 0.1f, 0.03f, 0.0f);
        ExpectColor(batch.Vertices()[3], 0.7f, 0.2f, 0.0f);
        EXPECT_FLOAT_EQ(batch.Vertices()[1].position.x, 1.2f);
    }
}

// 0x4ecc30 does not run the glow passes of a mesh with N; the excellent's 0x42, it does.
TEST(CharacterDrawingTest, AnNMeshDoesNotShine)
{
    EXPECT_EQ(DrawnPart("part_N.jpg").Commands().size(), 1u);

    DrawnCharacter character;
    character.appearance = Client::Kernel::ClassAppearance(0, 0);
    character.appearance.bodyParts[1].excellent = true;
    const OneModel models(character.appearance.bodyParts[1].model, "part_N.jpg");
    EXPECT_EQ(Drawn(models, character).Commands().size(), 2u);
}

// S scrolls the UVs with the object's U and V, which on a player are 0 (0x511290): nothing changes.
TEST(CharacterDrawingTest, AnSMeshDrawsAsOneWithoutScript)
{
    const ModelBatch plain = DrawnPart("part.jpg");
    const ModelBatch scrolling = DrawnPart("part_S.jpg");

    ASSERT_EQ(scrolling.Commands().size(), plain.Commands().size());
    for (std::size_t command = 0; command < plain.Commands().size(); ++command)
    {
        EXPECT_EQ(scrolling.Commands()[command].state, plain.Commands()[command].state) << command;
    }
    ASSERT_EQ(scrolling.Vertices().size(), plain.Vertices().size());
    for (std::size_t vertex = 0; vertex < plain.Vertices().size(); ++vertex)
    {
        EXPECT_EQ(scrolling.Vertices()[vertex].uv, plain.Vertices()[vertex].uv) << vertex;
    }
}

// A mesh the load marked as not shining (0x6117fd..0x611847): 0x4ecc30 does not run its glow passes, and it does on
// mesh 0.
TEST(CharacterDrawingTest, AMeshTheLoadMarkedDoesNotShine)
{
    DrawnCharacter character;
    character.appearance = Client::Kernel::ClassAppearance(0, 0);
    character.appearance.rightHand = Client::Kernel::WornItem{.model = 0x812, .level = 13};
    OneModel models(0x812, "spear.jpg", 2);
    models.WithoutShine(1);

    const ModelBatch batch = Drawn(models, character);

    EXPECT_EQ(batch.Vertices().size(), (4u + 1u) * 3u);
}

// How many vertices the helm `helm` leaves on a character of class `characterClass`, with the library that only has
// `drawn`.
std::size_t HelmVertices(std::uint8_t characterClass, std::uint16_t helm, std::uint16_t drawn)
{
    DrawnCharacter character;
    character.appearance = Client::Kernel::ClassAppearance(characterClass, 0);
    character.appearance.bodyParts[0].model = helm;
    const OneModel models(drawn);
    return Drawn(models, character).Vertices().size();
}

// 0x50ba30, in the helm part of class 4 (+0x1d4 & 7): the helms 0x1012, 0x1017, 0x1018, 0x101a and 0x101b go with the
// model + 0x328b, their mask (MaskHelmMale01, 06, 07, 09 and 10).
TEST(CharacterDrawingTest, ClassFourWearsTheMaskOfItsHelm)
{
    for (const std::uint16_t helm : {std::uint16_t{0x1012}, std::uint16_t{0x1017}, std::uint16_t{0x1018},
                                     std::uint16_t{0x101a}, std::uint16_t{0x101b}})
    {
        const auto mask = static_cast<std::uint16_t>(helm + 0x328b);
        EXPECT_EQ(HelmVertices(4, helm, mask), 3u) << std::hex << helm;
        EXPECT_EQ(HelmVertices(4, helm, helm), 0u) << std::hex << helm;
        EXPECT_EQ(HelmVertices(4 | 8, helm, mask), 3u) << std::hex << helm;
    }
}

// The other classes wear the helm, and class 4 wears the other helms without a mask.
TEST(CharacterDrawingTest, OnlyClassFourAndFiveHelmsWearTheMask)
{
    EXPECT_EQ(HelmVertices(0, 0x1012, 0x1012), 3u);
    EXPECT_EQ(HelmVertices(0, 0x1012, 0x1012 + 0x328b), 0u);
    EXPECT_EQ(HelmVertices(3, 0x1012, 0x1012), 3u);
    EXPECT_EQ(HelmVertices(4, 0x1013, 0x1013), 3u);
}

// 0x50ba30 changes the model only in the helm part (+0x218): in another, the same number goes as it is.
TEST(CharacterDrawingTest, TheMaskIsOnlyForTheHelmPart)
{
    DrawnCharacter character;
    character.appearance = Client::Kernel::ClassAppearance(4, 0);
    character.appearance.bodyParts[1].model = 0x1012;
    const OneModel models(0x1012);

    EXPECT_EQ(Drawn(models, character).Vertices().size(), 3u);
}

// The part `part` of class `characterClass` with an R mesh, at +0 or excellent, at 250 ms.
ModelBatch DrawnBlendMesh(std::uint8_t characterClass, bool excellent = false)
{
    DrawnCharacter character;
    character.appearance = Client::Kernel::ClassAppearance(characterClass, 0);
    character.appearance.bodyParts[1].excellent = excellent;
    const OneModel models(character.appearance.bodyParts[1].model, "part_R.jpg");
    ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});
    DrawCharacter(batch, models, character, std::chrono::milliseconds{250});
    return batch;
}

// The blendMesh's light is 1 (0x511290), and in class 4 the one 0x50ba30 sets in the helm part: sin(0.001 · T) · 0.1 +
// 0.7.
TEST(CharacterDrawingTest, TheBlendMeshLightOfClassFourPulses)
{
    const float pulse = RoundedSin(0.25f) * 0.1f + 0.7f;

    ExpectColor(DrawnBlendMesh(0).Vertices()[0], 0.4f, 0.4f, 0.4f);
    ExpectColor(DrawnBlendMesh(4).Vertices()[0], 0.4f * pulse, 0.4f * pulse, 0.4f * pulse);
}

// The blendMesh's 0x42 also goes by that light.
TEST(CharacterDrawingTest, TheExcellentPassOfTheBlendMeshTakesItsLight)
{
    const float pulse = RoundedSin(0.25f) * 0.1f + 0.7f;
    const Client::Render::Color excellent = Client::Models::ExcellentColorAt(std::chrono::milliseconds{250});

    const ModelBatch batch = DrawnBlendMesh(4, true);

    ASSERT_EQ(batch.Vertices().size(), 2u * 3u);
    ExpectColor(batch.Vertices()[3], excellent.red * pulse, excellent.green * pulse, excellent.blue * pulse);
}

// The blendMesh's outline also goes by the light: in class 4, the outline's color times the pulsing one.
TEST(CharacterDrawingTest, TheOutlineOfTheBlendMeshTakesItsLight)
{
    const float pulse = RoundedSin(0.25f) * 0.1f + 0.7f;
    DrawnCharacter character;
    character.appearance = Client::Kernel::ClassAppearance(4, 0);
    character.outlined = true;
    const OneModel models(character.appearance.bodyParts[1].model, "part_R.jpg");
    ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});

    DrawCharacter(batch, models, character, std::chrono::milliseconds{250});

    ASSERT_EQ(batch.Vertices().size(), 9u);
    ExpectColor(batch.Vertices()[0], 0.1f * pulse, 0.03f * pulse, 0.0f);
    ExpectColor(batch.Vertices()[3], 0.7f * pulse, 0.2f * pulse, 0.0f);
}

// The part of class `characterClass` with an R mesh of +13, at 250 ms.
ModelBatch DrawnThirteenBlendMesh(std::uint8_t characterClass)
{
    DrawnCharacter character;
    character.appearance = Client::Kernel::ClassAppearance(characterClass, 0);
    character.appearance.bodyParts[1].level = 13;
    const OneModel models(character.appearance.bodyParts[1].model, "part_R.jpg");
    ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});
    DrawCharacter(batch, models, character, std::chrono::milliseconds{250});
    return batch;
}

// The glow passes come out of 0x4ecc30 before the blendMesh's branch: the 0x1040 goes with the base color times the
// item's factor (0x6093d0), without the light, like the 0x48 and the 0x44.
TEST(CharacterDrawingTest, TheMetalPassesOfTheBlendMeshDoNotTakeItsLight)
{
    const ModelBatch plain = DrawnThirteenBlendMesh(0);
    const ModelBatch pulsing = DrawnThirteenBlendMesh(4);

    ASSERT_EQ(pulsing.Vertices().size(), 4u * 3u);
    ASSERT_EQ(plain.Vertices().size(), pulsing.Vertices().size());
    EXPECT_NE(pulsing.Vertices()[0].color, plain.Vertices()[0].color);
    for (std::size_t vertex = 3; vertex < plain.Vertices().size(); ++vertex)
    {
        EXPECT_EQ(pulsing.Vertices()[vertex].color, plain.Vertices()[vertex].color) << vertex;
    }
}

// A character of class `characterClass` with the parts of `models` in `parts` (the one of index 2, the pants, with the
// model `pants`), each with an R mesh in its slot, at 1000 ms.
ModelBatch DrawnLights(std::uint8_t characterClass, std::uint16_t pants, std::uint16_t helm = 0)
{
    DrawnCharacter character;
    character.appearance = Client::Kernel::ClassAppearance(characterClass, 0);
    character.appearance.bodyParts[2].model = pants;
    if (helm != 0)
    {
        character.appearance.bodyParts[0].model = helm;
    }
    OneModel models(pants, "part_R.jpg", 1, TextureSlot{0x302});
    for (const std::size_t part : {std::size_t{0}, std::size_t{1}, std::size_t{4}})
    {
        models.AlsoAs(character.appearance.bodyParts[part].model,
                      TextureSlot{static_cast<std::uint16_t>(0x300 + part)});
    }
    ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});
    DrawCharacter(batch, models, character, std::chrono::milliseconds{1000});
    return batch;
}

// The red of the R mesh of the part with slot 0x300 + `part`, which is 0.4 times the light.
float LightOfPart(const ModelBatch& batch, std::size_t part)
{
    const auto slot = TextureSlot{static_cast<std::uint16_t>(0x300 + part)};
    for (const Client::Render::ModelDrawCommand& command : batch.Commands())
    {
        if (command.state.texture == slot)
        {
            return batch.Vertices()[command.firstVertex].color.red / 0.4f;
        }
    }
    ADD_FAILURE() << "sin la parte " << part;
    return 0.0f;
}

// 0x60b3d0 writes the object's light with the model: PantMale25 (0x142a) sets it in that of its FShoots_R, sin(0.001 ·
// T) · 0.4 + 0.6, and it stays in the parts that follow (the armor and the helm) and in the next frame (the boots,
// which go first).
TEST(CharacterDrawingTest, PantMale25LightsTheCharacterWithItsShoots)
{
    const float shoots = RoundedSin(1.0f) * 0.4f + 0.6f;

    const ModelBatch batch = DrawnLights(0, 0x142a);

    for (const std::size_t part : {std::size_t{0}, std::size_t{1}, std::size_t{2}, std::size_t{4}})
    {
        EXPECT_NEAR(LightOfPart(batch, part), shoots, 1e-6f) << part;
    }
}

// In class 4, 0x50ba30 writes its own on the helm, which goes after the pants: the helm and what follows see it, and
// the armor, between the two, PantMale25's.
TEST(CharacterDrawingTest, ClassFourLightsFromTheHelmOn)
{
    const float shoots = RoundedSin(1.0f) * 0.4f + 0.6f;
    const float pulse = RoundedSin(1.0f) * 0.1f + 0.7f;

    const ModelBatch batch = DrawnLights(4, 0x142a);

    EXPECT_NEAR(LightOfPart(batch, 4), pulse, 1e-6f);
    EXPECT_NEAR(LightOfPart(batch, 2), shoots, 1e-6f);
    EXPECT_NEAR(LightOfPart(batch, 1), shoots, 1e-6f);
    EXPECT_NEAR(LightOfPart(batch, 0), pulse, 1e-6f);
}

// HelmMaleTest20 (0x1025) sets it back to 1 after class 4's, on the same helm.
TEST(CharacterDrawingTest, TheTest20PartsPutTheLightBackToOne)
{
    const ModelBatch batch = DrawnLights(4, 0x1425, 0x1025);

    for (const std::size_t part : {std::size_t{0}, std::size_t{1}, std::size_t{2}, std::size_t{4}})
    {
        EXPECT_FLOAT_EQ(LightOfPart(batch, part), 1.0f) << part;
    }
}

// 0x60cf70 draws the outline before calling 0x60b3d0: PantMale25's goes with the light there was (in class 4, the
// previous frame's helm's) and the part, with that of its FShoots_R.
TEST(CharacterDrawingTest, TheOutlineGoesWithTheLightFromBeforeThePart)
{
    const float shoots = RoundedSin(1.0f) * 0.4f + 0.6f;
    const float pulse = RoundedSin(1.0f) * 0.1f + 0.7f;
    DrawnCharacter character;
    character.appearance = Client::Kernel::ClassAppearance(4, 0);
    character.appearance.bodyParts[2].model = 0x142a;
    character.outlined = true;
    const OneModel models(0x142a, "part_R.jpg");
    ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});

    DrawCharacter(batch, models, character, std::chrono::milliseconds{1000});

    ASSERT_EQ(batch.Vertices().size(), 9u);
    EXPECT_NEAR(batch.Vertices()[3].color.red, 0.7f * pulse, 1e-6f);
    EXPECT_NEAR(batch.Vertices()[6].color.red, 0.4f * shoots, 1e-6f);
}

// The weapon `item` of level `level` in the right hand of class `characterClass`, with `meshes` meshes of texture
// `texture`, at `milliseconds`.
ModelBatch DrawnInTheHand(std::uint16_t item, std::size_t meshes, std::uint8_t level = 0, int milliseconds = 1000,
                          const std::string& texture = "item.jpg", std::uint8_t characterClass = 0)
{
    DrawnCharacter character;
    character.appearance = Client::Kernel::ClassAppearance(characterClass, 0);
    character.appearance.rightHand = Client::Kernel::WornItem{.model = item, .level = level};
    const OneModel models(item, texture, meshes);
    ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});
    DrawCharacter(batch, models, character, std::chrono::milliseconds{milliseconds});
    return batch;
}

// What hangs carries the object of 0x607b60: Bow07 (0xa18) has the blendMesh at −2, and 0x4ecc30 passes all its meshes
// through that branch, additive and by its light, sin(0.004 · T) · 0.3 + 0.7.
TEST(CharacterDrawingTest, Bow07BlendsEveryMesh)
{
    const ModelBatch batch = DrawnInTheHand(0xa18, 2);

    ASSERT_EQ(batch.Commands().size(), 1u);
    EXPECT_EQ(batch.Commands()[0].state.blend, ModelBlend::Additive);
    ASSERT_EQ(batch.Vertices().size(), 2u * 3u);
    for (std::size_t mesh = 0; mesh < 2; ++mesh)
    {
        EXPECT_NEAR(batch.Vertices()[mesh * 3].color.red, 0.4f * 0.47296f, 1e-5f) << mesh;
    }
}

// Sword06 (0x217) has the blendMesh at 1: the mesh with texture 1 goes through that branch, the other does not.
TEST(CharacterDrawingTest, AnItemBlendsTheMeshWithTheTextureOfItsObject)
{
    const ModelBatch batch = DrawnInTheHand(0x217, 2);

    ASSERT_EQ(batch.Commands().size(), 2u);
    EXPECT_EQ(batch.Commands()[0].state.blend, ModelBlend::Opaque);
    EXPECT_EQ(batch.Commands()[1].state.blend, ModelBlend::Additive);
}

// The light of what hangs is its object's, not the character's: an R mesh in the hand of class 4 goes by 1.
TEST(CharacterDrawingTest, WhatHangsTakesTheLightOfItsObject)
{
    const ModelBatch batch = DrawnInTheHand(0x212, 1, 0, 250, "item_R.jpg", 4);

    ASSERT_EQ(batch.Commands().size(), 1u);
    EXPECT_EQ(batch.Commands()[0].state.blend, ModelBlend::Additive);
    ExpectColor(batch.Vertices()[0], 0.4f, 0.4f, 0.4f);
}

// 0x609640 passes the object's +0x58 to the base pass as the hidden mesh: mesh 1 of Spear11 (0x81c) and mesh 2 of
// Sword32 (0x231) do not go.
TEST(CharacterDrawingTest, TheBaseSkipsTheHiddenMeshOfTheObject)
{
    EXPECT_EQ(DrawnInTheHand(0x81c, 2).Vertices().size(), 1u * 3u);
    EXPECT_EQ(DrawnInTheHand(0x231, 3).Vertices().size(), 2u * 3u);
    EXPECT_EQ(DrawnInTheHand(0x212, 3).Vertices().size(), 3u * 3u);
}

// 0x60b3d0 writes the light of Wing05 (0x1a16) as sin(0.001 · T) + 1.1: its R mesh goes by it.
TEST(CharacterDrawingTest, TheLightOfWing05Pulses)
{
    DrawnCharacter character;
    character.appearance = Client::Kernel::ClassAppearance(0, 0);
    character.appearance.wings = std::uint16_t{0x1a16};
    const OneModel models(0x1a16, "wing_R.jpg");
    ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});

    DrawCharacter(batch, models, character, std::chrono::milliseconds{1000});

    ASSERT_EQ(batch.Vertices().size(), 3u);
    EXPECT_NEAR(batch.Vertices()[0].color.red, 0.4f * (RoundedSin(1.0f) + 1.1f), 1e-6f);
}

// 0x509340 returns without drawing with model 0x1c30 (0x509363: CMP EDI,0x1c30 and JZ to the end): the Dark Lord's
// cape, DarkLordRobe.bmd, an inventory plane, does not hang from bone 47. What is seen is the cloth of 0x50ba30.
TEST(CharacterDrawingTest, TheLordsCapeDoesNotHangAsAModel)
{
    DrawnCharacter character;
    character.appearance = Client::Kernel::ClassAppearance(4, 0);
    character.appearance.wings = std::uint16_t{0x1c30};
    const OneModel models(0x1c30, "DarkLordRobe.jpg");
    ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});

    DrawCharacter(batch, models, character, std::chrono::milliseconds{1000});

    EXPECT_TRUE(batch.Vertices().empty());
}

// The vertices of the +13 weapon `item` in the right hand, with `meshes` meshes.
std::size_t HandVertices(std::uint16_t item, std::size_t meshes)
{
    DrawnCharacter character;
    character.appearance = Client::Kernel::ClassAppearance(0, 0);
    character.appearance.rightHand = Client::Kernel::WornItem{.model = item, .level = 13};
    const OneModel models(item, "item.jpg", meshes);
    return Drawn(models, character).Vertices().size();
}

// In the passes of 0x60b130 and 0x60b300, RenderBody skips the hidden mesh of some items: mesh 0 of 0x227 in the three
// passes of +13, and mesh 2 of 0x619 in the 0x48 and the 0x44, not in the 0x1040. 0x619 also hides it in the base pass,
// with its object's +0x58.
TEST(CharacterDrawingTest, SomeItemsHideAMeshInTheirPasses)
{
    EXPECT_EQ(HandVertices(0x227, 2), (1u + 4u) * 3u);
    EXPECT_EQ(HandVertices(0x619, 3), (4u + 4u + 1u) * 3u);
    EXPECT_EQ(HandVertices(0x212, 2), (4u + 4u) * 3u);
}

// 0x60b130 passes the object's +0x58 to 0x231, 0x81c and 0xe22 (and to 0x61a..0x61f, at −1): the mesh their object
// hides does not go in the 0x48 or the 0x44, but it does in the 0x1040.
TEST(CharacterDrawingTest, SomeItemsHideTheMeshOfTheirObjectInTheirPasses)
{
    EXPECT_EQ(HandVertices(0x81c, 2), (4u + 1u) * 3u);
    EXPECT_EQ(HandVertices(0x231, 3), (4u + 4u + 1u) * 3u);
    EXPECT_EQ(HandVertices(0xe22, 3), (4u + 4u + 1u) * 3u);
    EXPECT_EQ(HandVertices(0x61a, 3), (4u + 4u + 4u) * 3u);
}

// 0x60b3d0 with the object of Spear10 (0x81b, what hangs carries its item's type): first, the mesh with the 0x5c0
// forced and the color (0.5, 0.5, 1.5), opaque; then, the usual passes.
TEST(CharacterDrawingTest, Spear10DrawsItsChromeFirst)
{
    DrawnCharacter character;
    character.appearance = Client::Kernel::ClassAppearance(0, 0);
    character.appearance.rightHand = Client::Kernel::WornItem{.model = 0x81b};
    const OneModel models(0x81b);

    const ModelBatch batch = Drawn(models, character);

    ASSERT_EQ(batch.Commands().size(), 2u);
    EXPECT_EQ(batch.Commands()[0].state, (ModelDrawState{TextureSlot{0x5c0}, ModelBlend::Opaque, true}));
    EXPECT_EQ(batch.Commands()[1].state, (ModelDrawState{TextureSlot{0x300}, ModelBlend::Opaque, true}));
    ExpectColor(batch.Vertices()[0], 0.5f, 0.5f, 1.5f);
    ExpectColor(batch.Vertices()[3], 0.4f, 0.4f, 0.4f);
}

// 0x81b belongs to the object: a part, whose object is the character (0x20a), does not carry it.
TEST(CharacterDrawingTest, OnlyTheHanging0x81bDrawsTheChrome)
{
    DrawnCharacter character;
    character.appearance = Client::Kernel::ClassAppearance(0, 0);
    character.appearance.bodyParts[1].model = 0x81b;
    const OneModel models(0x81b);

    EXPECT_EQ(Drawn(models, character).Commands().size(), 1u);
}

// 0x607b60 gives Spear10 the V (T % 2000) · −0.0005, and 0x60b3d0 sets model+0x88 to 0 for the chrome pass: 0x4ecc30
// scrolls that mesh's UVs. Its mesh, an OZT, goes afterwards with alpha and without scrolling (its blendMesh is −1).
TEST(CharacterDrawingTest, Spear10ScrollsItsChrome)
{
    const ModelBatch batch = DrawnInTheHand(0x81b, 1, 0, 1000, "af.tga");

    ASSERT_EQ(batch.Commands().size(), 2u);
    EXPECT_EQ(batch.Commands()[0].state, (ModelDrawState{TextureSlot{0x5c0}, ModelBlend::Opaque, true}));
    EXPECT_EQ(batch.Commands()[1].state, (ModelDrawState{TextureSlot{0x300}, ModelBlend::Alpha, true}));
    EXPECT_FLOAT_EQ(batch.Vertices()[0].uv.x, 0.0f);
    EXPECT_FLOAT_EQ(batch.Vertices()[0].uv.y, -0.5f);
    EXPECT_EQ(batch.Vertices()[3].uv, (glm::vec2{0.0f, 0.0f}));
}

// 0x4ecc30 adds the object's U and V to the UVs of the 0x1040 pass (mode 0x1000), not to those of the 0x48 or the 0x44:
// those of a +13 Spear10, against those of an item that does not have them.
TEST(CharacterDrawingTest, TheChrome4PassTakesTheUVOfTheObject)
{
    const ModelBatch spear = DrawnInTheHand(0x81b, 1, 13);
    const ModelBatch plain = DrawnInTheHand(0x212, 1, 13);

    ASSERT_EQ(spear.Vertices().size(), 5u * 3u);
    ASSERT_EQ(plain.Vertices().size(), 4u * 3u);
    for (std::size_t vertex = 0; vertex < plain.Vertices().size(); ++vertex)
    {
        const glm::vec2 shift = plain.Commands()[1].firstVertex <= vertex && vertex < plain.Commands()[2].firstVertex
                                    ? glm::vec2{0.0f, -0.5f}
                                    : glm::vec2{0.0f, 0.0f};
        const glm::vec2 expected = plain.Vertices()[vertex].uv + shift;
        EXPECT_NEAR(spear.Vertices()[vertex + 3].uv.x, expected.x, 1e-6f) << vertex;
        EXPECT_NEAR(spear.Vertices()[vertex + 3].uv.y, expected.y, 1e-6f) << vertex;
    }
}

// Staff06 (0xc17) has the blendMesh at 2 and a U and a V of rand() % 10 · 0.1 in each frame: 0x4ecc30 scrolls the UVs
// of the mesh of index 2, and not the others', and they change from one frame to the next.
TEST(CharacterDrawingTest, TheBlendMeshOfStaff06ScrollsWithItsRandomUV)
{
    std::vector<glm::vec2> shifts;
    for (int milliseconds = 0; milliseconds < 20 * 40; milliseconds += 40)
    {
        const ModelBatch batch = DrawnInTheHand(0xc17, 3, 0, milliseconds);
        ASSERT_EQ(batch.Vertices().size(), 3u * 3u);
        EXPECT_EQ(batch.Vertices()[0].uv, (glm::vec2{0.0f, 0.0f}));
        EXPECT_EQ(batch.Vertices()[3].uv, (glm::vec2{0.0f, 0.0f}));
        const glm::vec2 shift = batch.Vertices()[6].uv;
        for (const float tenths : {shift.x * 10.0f, shift.y * 10.0f})
        {
            EXPECT_NEAR(tenths, std::round(tenths), 1e-4f);
            EXPECT_GE(tenths, 0.0f);
            EXPECT_LE(tenths, 9.0f + 1e-4f);
        }
        ASSERT_EQ(batch.Commands().size(), 2u);
        EXPECT_EQ(batch.Commands()[1].state.blend, ModelBlend::Additive);
        shifts.push_back(shift);
    }
    EXPECT_NE(std::ranges::find_if(shifts, [](glm::vec2 shift) { return shift != glm::vec2{0.0f, 0.0f}; }),
              shifts.end());
    EXPECT_NE(std::ranges::adjacent_find(shifts, std::not_equal_to{}), shifts.end());
}

// The set of a part of `meshes` meshes, with the texture `texture` on mesh 1, at 250 ms.
ModelBatch DrawnSet(std::size_t meshes, const std::string& second = "part.jpg")
{
    DrawnCharacter character;
    character.appearance = Client::Kernel::ClassAppearance(0, 0);
    character.appearance.bodyParts[1].ancient = true;
    OneModel models(character.appearance.bodyParts[1].model, "part.jpg", meshes);
    models.MeshAt(1).textureName = second;
    ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});
    DrawCharacter(batch, models, character, std::chrono::milliseconds{250});
    return batch;
}

// 0x60b3d0 calls the set pass with a pulsing alpha (SetAlphaAt), and 0x4ecc30 multiplies the model's color by it in
// place in each mesh that draws it: the first goes by a, the second by a² (Q-C95).
TEST(CharacterDrawingTest, TheSetPassFadesMeshByMesh)
{
    const ModelBatch batch = DrawnSet(2);
    const float alpha = RoundedSin(0.25f) * 0.5f + 0.4f;
    const Client::Render::Color set =
        Client::Models::SetColorOf(Client::Kernel::ClassAppearance(0, 0).bodyParts[1].model);

    const auto vertices = batch.Vertices();
    ASSERT_EQ(vertices.size(), 4u * 3u);
    ExpectColor(vertices[0], 0.4f, 0.4f, 0.4f);
    ExpectColor(vertices[3], 0.4f, 0.4f, 0.4f);
    ExpectColor(vertices[6], set.red * alpha, set.green * alpha, set.blue * alpha);
    ExpectColor(vertices[9], set.red * alpha * alpha, set.green * alpha * alpha, set.blue * alpha * alpha);
}

// A mesh that does not shine leaves 0x4ecc30 before multiplying: it does not count for the ones that follow.
TEST(CharacterDrawingTest, AMeshWithoutShineDoesNotFadeTheSetPass)
{
    const ModelBatch batch = DrawnSet(3, "part_N.jpg");
    const float alpha = RoundedSin(0.25f) * 0.5f + 0.4f;
    const Client::Render::Color set =
        Client::Models::SetColorOf(Client::Kernel::ClassAppearance(0, 0).bodyParts[1].model);

    const auto vertices = batch.Vertices();
    ASSERT_EQ(vertices.size(), 5u * 3u);
    ExpectColor(vertices[12], set.red * alpha * alpha, set.green * alpha * alpha, set.blue * alpha * alpha);
}

void ExpectAt(const Client::Render::ModelVertex& vertex, float x, float y, float z)
{
    constexpr float Tolerance = 1e-4f;
    EXPECT_NEAR(vertex.position.x, x, Tolerance);
    EXPECT_NEAR(vertex.position.y, y, Tolerance);
    EXPECT_NEAR(vertex.position.z, z, Tolerance);
}

// The quiver `quiver` in the left hand or in the right one, of level `level`.
ModelBatch DrawnQuiver(std::uint16_t quiver, bool left, std::uint8_t level = 0, bool excellent = false)
{
    DrawnCharacter character;
    character.appearance = Client::Kernel::ClassAppearance(0, 0);
    const Client::Kernel::WornItem item{.model = quiver, .level = level, .excellent = excellent, .ancient = true};
    (left ? character.appearance.leftHand : character.appearance.rightHand) = item;
    const OneModel models(quiver);
    return Drawn(models, character);
}

// 0x50ba30 with the weapons in the hand: the bolts of the left and the arrows of the right go on the back, from the
// wings' bone (47) by AngleMatrix(70, 0, 90) and (−10, 5, 10) (0x509340 with its flag). With the bone at the origin,
// the triangle (0, 0, 0), (1, 0, 0), (0, 1, 0) falls like this.
TEST(CharacterDrawingTest, TheBoltsAndTheArrowsGoOnTheBack)
{
    const float seventy = 70.0f * std::numbers::pi_v<float> / 180.0f;
    for (const auto& [quiver, left] : {std::pair{std::uint16_t{0xa19}, true}, std::pair{std::uint16_t{0xa21}, false}})
    {
        const ModelBatch batch = DrawnQuiver(quiver, left);

        ASSERT_EQ(batch.Vertices().size(), 3u) << std::hex << quiver;
        ExpectAt(batch.Vertices()[0], -10.0f, 5.0f, 10.0f);
        ExpectAt(batch.Vertices()[1], -10.0f, 6.0f, 10.0f);
        ExpectAt(batch.Vertices()[2], -10.0f - RoundedCos(seventy), 5.0f, 10.0f + RoundedSin(seventy));
    }
}

// With bolts on the left and arrows on the right, the bolts go (the arrows are the else of 0x50ba30): without the
// bolts' model, nothing.
TEST(CharacterDrawingTest, TheBoltsComeBeforeTheArrows)
{
    DrawnCharacter character;
    character.appearance = Client::Kernel::ClassAppearance(0, 0);
    character.appearance.leftHand = Client::Kernel::WornItem{.model = 0xa19};
    character.appearance.rightHand = Client::Kernel::WornItem{.model = 0xa21};
    const OneModel models(0xa21);

    EXPECT_TRUE(Drawn(models, character).Vertices().empty());
}

// 0x60b3d0 gives the quiver 2 · level + 1: +3 bolts go with the 0x44 of +7. The hand's excellent goes; the set does not
// (0x509340 passes 0).
TEST(CharacterDrawingTest, TheQuiverGlowsAtTwiceItsLevelPlusOne)
{
    EXPECT_EQ(DrawnQuiver(0xa19, true, 3).Vertices().size(), 2u * 3u);
    EXPECT_EQ(DrawnQuiver(0xa19, true, 0).Vertices().size(), 1u * 3u);
    EXPECT_EQ(DrawnQuiver(0xa21, false, 0, true).Vertices().size(), 2u * 3u);
}
// 0x50ba30 draws the parts from 5 to 0: the boots, the gloves, the pants, the armor, the helm and the head; then the
// back, the wings and the pet, and at the end the hands (0x50e6af).
TEST(CharacterDrawingTest, ThePartsGoFromTheBootsToTheHeadAndTheHandsLast)
{
    DrawnCharacter character;
    character.appearance = Client::Kernel::ClassAppearance(0, 0);
    character.appearance.rightHand = Client::Kernel::WornItem{.model = 0x212};
    character.appearance.leftHand = Client::Kernel::WornItem{.model = 0xa19};
    character.appearance.wings = std::uint16_t{0x1a12};
    character.appearance.pet = std::uint16_t{0x1c13};
    OneModel models(0x212, "part.jpg", 1, TextureSlot{0x310});
    std::vector<std::uint16_t> order;
    for (std::size_t part = 0; part < character.appearance.bodyParts.size(); ++part)
    {
        models.AlsoAs(character.appearance.bodyParts[part].model,
                      TextureSlot{static_cast<std::uint16_t>(0x300 + part)});
    }
    character.appearance.head = std::uint16_t{0x2001};
    models.AlsoAs(0x2001, TextureSlot{0x305});
    models.AlsoAs(0xa19, TextureSlot{0x306});
    models.AlsoAs(0x1a12, TextureSlot{0x307});
    models.AlsoAs(0x1c13, TextureSlot{0x308});

    const ModelBatch batch = Drawn(models, character);

    for (const Client::Render::ModelDrawCommand& command : batch.Commands())
    {
        ASSERT_TRUE(command.state.texture.has_value());
        order.push_back(command.state.texture->value);
    }
    EXPECT_EQ(order,
              (std::vector<std::uint16_t>{0x304, 0x303, 0x302, 0x301, 0x300, 0x305, 0x306, 0x307, 0x308, 0x310}));
}

// RenderBody makes one pass over every mesh before the next: the bases of the two meshes and then each glow pass on
// both.
TEST(CharacterDrawingTest, AModelGoesPassByPass)
{
    const ModelBatch batch = DrawnInTheHand(0x212, 2, 7);

    ASSERT_EQ(batch.Commands().size(), 2u);
    EXPECT_EQ(batch.Commands()[0].vertexCount, 2u * 3u);
    EXPECT_EQ(batch.Commands()[0].state.texture, TextureSlot{0x300});
    EXPECT_EQ(batch.Commands()[1].vertexCount, 2u * 3u);
    EXPECT_NE(batch.Commands()[1].state.texture, TextureSlot{0x300});
}
// The character's light goes by channel: the login's is the terrain's, which has color (0x50ba30 with 0x5f99e0 outside
// scene 4).
TEST(CharacterDrawingTest, TheLightOfACharacterGoesByChannel)
{
    DrawnCharacter character;
    character.appearance = Client::Kernel::ClassAppearance(0, 0);
    character.light = glm::vec3{0.2f, 0.4f, 0.6f};
    const OneModel models(character.appearance.bodyParts[1].model);

    const ModelBatch batch = Drawn(models, character);

    ASSERT_EQ(batch.Vertices().size(), 3u);
    ExpectColor(batch.Vertices()[0], 0.2f, 0.4f, 0.6f);
}

ModelBatch DrawnObjectOf(const OneModel& models, const DrawnObject& object)
{
    ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});
    DrawObject(batch, models, object, std::chrono::milliseconds{0});
    return batch;
}

DrawnObject LogoLike()
{
    DrawnObject object;
    object.model = 0xa2;
    object.transform.position = {10.0f, 20.0f, 30.0f};
    object.transform.scale = 2.0f;
    object.state.blendMesh = 1;
    object.state.light = 0.5f;
    object.color = {0.3f, 0.4f, 0.5f};
    return object;
}

// A world object without cases by type (0x6002e0): RenderBody(2) and nothing more. Each mesh with the model's color
// (0x5ffce0), and the blendMesh's additive and by the object's light; posed with its scale and its position (0x5ffdf0).
TEST(CharacterDrawingTest, AnObjectGoesWithItsBasePassOnly)
{
    const OneModel models(0xa2, "logo.jpg", 2);

    const ModelBatch batch = DrawnObjectOf(models, LogoLike());

    ASSERT_EQ(batch.Commands().size(), 2u);
    EXPECT_EQ(batch.Commands()[0].state, (ModelDrawState{TextureSlot{0x300}, ModelBlend::Opaque, true}));
    EXPECT_EQ(batch.Commands()[1].state, (ModelDrawState{TextureSlot{0x300}, ModelBlend::Additive, true}));
    const auto vertices = batch.Vertices();
    ASSERT_EQ(vertices.size(), 6u);
    ExpectColor(vertices[0], 0.3f, 0.4f, 0.5f);
    ExpectColor(vertices[3], 0.3f * 0.5f, 0.4f * 0.5f, 0.5f * 0.5f);
    EXPECT_FLOAT_EQ(vertices[1].position.x, 12.0f);
    EXPECT_FLOAT_EQ(vertices[1].position.y, 20.0f);
    EXPECT_FLOAT_EQ(vertices[1].position.z, 30.0f);
    EXPECT_FLOAT_EQ(vertices[2].position.y, 22.0f);
}

// The root turns with the object's angles (0x5ffdf0 → 0x4eb860 with +0x1c): with 90° in z, the vertex (1, 0, 0) goes to
// (0, 2, 0) of its place.
TEST(CharacterDrawingTest, AnObjectTurnsWithItsAngles)
{
    const OneModel models(0xa2, "logo.jpg", 1);
    DrawnObject object = LogoLike();
    object.transform.angles = {0.0f, 0.0f, 90.0f};

    const ModelBatch batch = DrawnObjectOf(models, object);

    ASSERT_EQ(batch.Vertices().size(), 3u);
    EXPECT_NEAR(batch.Vertices()[1].position.x, 10.0f, 1e-4f);
    EXPECT_NEAR(batch.Vertices()[1].position.y, 22.0f, 1e-4f);
}

// The mesh the object hides (+0x58) does not go, and the others do.
TEST(CharacterDrawingTest, AnObjectSkipsItsHiddenMesh)
{
    const OneModel models(0xa2, "logo.jpg", 2);
    DrawnObject object = LogoLike();
    object.state.hiddenMesh = 0;

    const ModelBatch batch = DrawnObjectOf(models, object);

    ASSERT_EQ(batch.Commands().size(), 1u);
    EXPECT_EQ(batch.Commands()[0].state.blend, ModelBlend::Additive);
}

// The texture's script also holds on an object (0x4ef0c0): with H, the mesh does not go.
TEST(CharacterDrawingTest, AnObjectFollowsTheScriptOfItsTextures)
{
    OneModel models(0xa2, "logo.jpg", 2);
    models.FirstMesh().textureName = "logo_H.jpg";

    const ModelBatch batch = DrawnObjectOf(models, LogoLike());

    ASSERT_EQ(batch.Commands().size(), 1u);
    EXPECT_EQ(batch.Commands()[0].state.blend, ModelBlend::Additive);
}

// An object with per-vertex light (+0xe8, which 0x5ffce0 copies to the model's +0x44): 0x4ecc30 gives each corner the
// model's color times the light of its normal (0x4ebc80), by the corner's normal index and not its vertex's. The one in
// x gives 1.44; the one in −z, the floor of 0.2; the one in y, 0.4.
DrawnObject LitObject()
{
    DrawnObject object = LogoLike();
    object.model = 0xbf;
    object.state.blendMesh = -1;
    object.color = {0.5f, 0.5f, 0.5f};
    object.vertexLight = true;
    return object;
}

TEST(CharacterDrawingTest, AnObjectWithVertexLightLightsEachCornerByItsNormal)
{
    OneModel models(0xbf, "dk.jpg", 1);
    models.FirstMesh().normals = {{0, {1, 0, 0}, 1}, {0, {0, 0, -1}, 2}, {0, {0, 1, 0}, 0}};
    models.FirstMesh().triangles[0].normals = {2, 0, 1, 0};
    DrawnObject object = LitObject();
    object.transform.scale = 1.0f;

    const ModelBatch batch = DrawnObjectOf(models, object);

    ASSERT_EQ(batch.Vertices().size(), 3u);
    ExpectColor(batch.Vertices()[0], 0.2f, 0.2f, 0.2f);
    ExpectColor(batch.Vertices()[1], 0.72f, 0.72f, 0.72f);
    ExpectColor(batch.Vertices()[2], 0.1f, 0.1f, 0.1f);
    // Without it, the model's color on the three.
    DrawnObject unlit = LitObject();
    unlit.vertexLight = false;
    const ModelBatch flat = DrawnObjectOf(models, unlit);
    ASSERT_EQ(flat.Vertices().size(), 3u);
    ExpectColor(flat.Vertices()[0], 0.5f, 0.5f, 0.5f);
}

TEST(CharacterDrawingTest, TheSelectedUnlitModelMeshScrollsWithoutChangingOtherMeshes)
{
    OneModel models(0x2a, "noria.jpg", 2);
    models.FirstMesh().normals = {{0, {0, 0, -1}, 0}};
    DrawnObject object = LitObject();
    object.model = 0x2a;
    object.transform.scale = 1.0f;
    object.state.uv = {0.25f, -0.5f};
    const ModelBatch plain = DrawnObjectOf(models, object);
    object.unlitMesh = std::int16_t{0};
    const ModelBatch selected = DrawnObjectOf(models, object);
    ASSERT_EQ(selected.Vertices().size(), 6u);
    ExpectColor(selected.Vertices()[0], 0.5f, 0.5f, 0.5f);
    EXPECT_EQ(selected.Vertices()[0].uv, plain.Vertices()[0].uv + object.state.uv);
    EXPECT_EQ(selected.Vertices()[3].uv, plain.Vertices()[3].uv);
    EXPECT_EQ(selected.Vertices()[3].color.red, plain.Vertices()[3].color.red);
}

// An object of 0x5ffdf0 carries the scale in the root (0x4eb860 with Translate), and 0x4ebc80 turns each normal with
// that matrix without normalizing it: with scale 2, the normal in z measures 2 and gives 0.8 · 2 · 2 + 0.4.
TEST(CharacterDrawingTest, TheNormalsOfAnObjectCarryItsScale)
{
    const OneModel models(0xbf, "dk.jpg", 1);
    DrawnObject object = LitObject();
    object.transform.scale = 2.0f;

    const ModelBatch batch = DrawnObjectOf(models, object);

    ASSERT_EQ(batch.Vertices().size(), 3u);
    ExpectColor(batch.Vertices()[0], 0.5f * 3.6f, 0.5f * 3.6f, 0.5f * 3.6f);
}

// The blendMesh's mesh goes through its branch, with glColor3f(color · the blendMesh's light), without the normal's.
TEST(CharacterDrawingTest, TheBlendMeshOfALitObjectGoesWithoutTheVertexLight)
{
    OneModel models(0xbf, "dk.jpg", 1);
    models.FirstMesh().normals = {{0, {0, -1, 0}, 0}};
    DrawnObject object = LitObject();
    object.state.blendMesh = 0;
    object.state.light = 0.5f;

    const ModelBatch batch = DrawnObjectOf(models, object);

    ASSERT_EQ(batch.Commands().size(), 1u);
    EXPECT_EQ(batch.Commands()[0].state.blend, ModelBlend::Additive);
    ExpectColor(batch.Vertices()[0], 0.25f, 0.25f, 0.25f);
}

// With the object's alpha (+0x174) below 0.99, 0x4ecc30 blends (0x620a00(1)) and sends it in the glColor4f; from 0.99
// on, it goes opaque and at 1.
TEST(CharacterDrawingTest, AnObjectFadingInBlendsWithItsAlpha)
{
    const OneModel models(0xbf, "dk.jpg", 1);
    DrawnObject object = LitObject();
    object.alpha = 0.5f;

    const ModelBatch fading = DrawnObjectOf(models, object);
    object.alpha = 0.99f;
    const ModelBatch opaque = DrawnObjectOf(models, object);

    ASSERT_EQ(fading.Commands().size(), 1u);
    EXPECT_EQ(fading.Commands()[0].state.blend, ModelBlend::Alpha);
    EXPECT_FLOAT_EQ(fading.Vertices()[0].color.alpha, 0.5f);
    ASSERT_EQ(opaque.Commands().size(), 1u);
    EXPECT_EQ(opaque.Commands()[0].state.blend, ModelBlend::Opaque);
    EXPECT_FLOAT_EQ(opaque.Vertices()[0].color.alpha, 1.0f);
}

// 0x5ffdf0 does not draw an object with the alpha below 0.01 (0x6b88d4).
TEST(CharacterDrawingTest, AnObjectBelowAHundredthOfAlphaIsNotDrawn)
{
    const OneModel models(0xbf, "dk.jpg", 1);
    DrawnObject object = LitObject();
    object.alpha = 0.0099f;
    EXPECT_TRUE(DrawnObjectOf(models, object).Commands().empty());
    object.alpha = 0.01f;
    EXPECT_FALSE(DrawnObjectOf(models, object).Commands().empty());
}

// The dark horse (0x6002e0 with 0xbf), with 13 meshes of one triangle and the normal in z (the per-vertex light gives
// 0.4): the base pass in white times that light, then the additive chrome of mesh 12 in (1, 0.8, 0.3) and the shadow of
// mesh 8, blended, untextured, in (0, 0, 0, 0.7) and flattened on the floor.
ModelBatch DrawnHorseOf(const OneModel& models, const DrawnObject& horse, std::int32_t map = 0)
{
    ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});
    DrawDarkHorse(batch, models, horse, map, std::chrono::milliseconds{0});
    return batch;
}

DrawnObject HorseLike()
{
    DrawnObject horse = LitObject();
    horse.color = {0.3f, 0.3f, 0.3f};
    horse.transform.position = {10.0f, 20.0f, 30.0f};
    horse.transform.scale = 1.0f;
    return horse;
}

TEST(CharacterDrawingTest, TheDarkHorseGoesWhiteThenItsChromeThenItsShadow)
{
    const OneModel models(0xbf, "dk.jpg", 13);

    const ModelBatch batch = DrawnHorseOf(models, HorseLike());

    ASSERT_EQ(batch.Commands().size(), 3u);
    EXPECT_EQ(batch.Commands()[0].state, (ModelDrawState{TextureSlot{0x300}, ModelBlend::Opaque, true}));
    EXPECT_EQ(batch.Commands()[0].vertexCount, 13u * 3u);
    EXPECT_EQ(batch.Commands()[1].state, (ModelDrawState{TextureSlot{0x5c0}, ModelBlend::Additive, true}));
    EXPECT_EQ(batch.Commands()[1].vertexCount, 3u);
    EXPECT_EQ(batch.Commands()[2].state, (ModelDrawState{std::nullopt, ModelBlend::BlendedShadow, true}));
    EXPECT_EQ(batch.Commands()[2].vertexCount, 3u);
    const auto vertices = batch.Vertices();
    ExpectColor(vertices[0], 2.0f, 2.0f, 2.0f);
    ExpectColor(vertices[13 * 3], 1.0f, 0.8f, 0.3f);
    const Client::Render::ModelVertex& shadow = vertices[13 * 3 + 3];
    ExpectColor(shadow, 0.0f, 0.0f, 0.0f);
    EXPECT_FLOAT_EQ(shadow.color.alpha, 0.7f);
    EXPECT_FLOAT_EQ(shadow.position.z, 30.0f + 5.0f);
}

// The horse of 0x4453b0 (0x44532e: 0x602e90(horse, 0, …)) comes out of the scratch of 0x5ffdf0 with the second argument
// at 0: 0x4eb860 with the flag at 1 puts the scale in the root and 0x4ebc80 with Translate at 0 turns each normal by
// that bone (0x5fcbd0), with the scale inside. The chrome of mesh 12 (0x4ecc30 with 0x44, 0x4ed309..0x4ed32c with the
// clock at 0) is u = 0.5 · nz and v = 0.5 · ny: with the login's scale 1.6f and the normal (0, 0, 1), nz = 1.6f and u =
// 0.8f.
TEST(CharacterDrawingTest, TheChromeOfAWorldScratchReadsTheScaledNormal)
{
    const OneModel models(0xbf, "dk.jpg", 13);
    DrawnObject horse = HorseLike();
    horse.transform.scale = 1.6f;

    const ModelBatch batch = DrawnHorseOf(models, horse);

    ASSERT_EQ(batch.Commands().size(), 3u);
    ASSERT_EQ(batch.Commands()[1].vertexCount, 3u);
    for (std::size_t corner = 13 * 3; corner < 13 * 3 + 3; ++corner)
        EXPECT_EQ(batch.Vertices()[corner].uv, glm::vec2(0.8f, 0.0f)) << corner;
}

// A scratch of 0x4ebc80 with Translate at 1 (a monster's body through 0x50baed → 0x5ffdf0(object, 1) → 0x600177, what
// hangs through 0x509a39, a floor item) keeps the turned normal without the scale, and 0x4ecc30 reads it that way: with
// 0x4 and the object's scale 1.5, the normal (0, 0, 1) gives u = 0.5, not 0.75.
TEST(CharacterDrawingTest, TheChromeOfATranslatedScratchIgnoresTheObjectScale)
{
    const OneModel models(0x197, "m.jpg");
    DrawnObject object = LitObject();
    object.model = 0x197;
    object.transform.scale = 1.5f;
    std::array<Client::Models::SkinnedMesh, 1> meshes{};
    meshes[0].positions = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    meshes[0].normals = {{0, 0, 1}};
    ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});

    const auto drawn = Client::Actors::Drawing::DrawItemMeshPass(batch, models, object, std::chrono::milliseconds{0},
                                                                 meshes, -1, 0x4, TextureSlot{0x5c0});

    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    ASSERT_EQ(batch.Vertices().size(), 3u);
    for (const Client::Render::ModelVertex& corner : batch.Vertices())
        EXPECT_EQ(corner.uv, glm::vec2(0.5f, 0.0f));
}

// While it appears, the base pass blends with its alpha and the chrome goes by it (0x4ecc30 multiplies the color in
// place); the shadow stays at 0.7.
TEST(CharacterDrawingTest, TheChromeOfAFadingHorseGoesByItsAlpha)
{
    const OneModel models(0xbf, "dk.jpg", 13);
    DrawnObject horse = HorseLike();
    horse.alpha = 0.5f;

    const ModelBatch batch = DrawnHorseOf(models, horse);

    ASSERT_EQ(batch.Commands().size(), 3u);
    EXPECT_EQ(batch.Commands()[0].state.blend, ModelBlend::Alpha);
    EXPECT_FLOAT_EQ(batch.Vertices()[0].color.alpha, 0.5f);
    ExpectColor(batch.Vertices()[13 * 3], 0.5f, 0.4f, 0.15f);
    EXPECT_FLOAT_EQ(batch.Vertices()[13 * 3 + 3].color.alpha, 0.7f);
}

// 0x60073c..0x600787: the shadow depends on the current world. In Lorencia (0) and Devias (2), and in the login (55),
// at 0.7; in 7, at 0.2; in 10, in Kalima (24 to 29) and in 36, there is none. The body and the chrome go in all of
// them.
TEST(CharacterDrawingTest, TheHorseShadowGoesByTheMap)
{
    const OneModel models(0xbf, "dk.jpg", 13);
    struct Case
    {
        std::int32_t map;
        std::optional<float> shadowAlpha;
    };
    for (const Case& probe :
         {Case{0, 0.7f}, Case{2, 0.7f}, Case{55, 0.7f}, Case{7, 0.2f}, Case{10, std::nullopt}, Case{24, std::nullopt},
          Case{29, std::nullopt}, Case{36, std::nullopt}, Case{23, 0.7f}, Case{30, 0.7f}})
    {
        SCOPED_TRACE(probe.map);

        const ModelBatch batch = DrawnHorseOf(models, HorseLike(), probe.map);

        ASSERT_EQ(batch.Commands().size(), probe.shadowAlpha ? 3u : 2u);
        EXPECT_EQ(batch.Commands()[1].state.blend, ModelBlend::Additive);
        if (probe.shadowAlpha)
        {
            EXPECT_EQ(batch.Commands()[2].state.blend, ModelBlend::BlendedShadow);
            EXPECT_FLOAT_EQ(batch.Vertices()[13 * 3 + 3].color.alpha, *probe.shadowAlpha);
        }
    }
}

// The Fenrirs (0x60277b..0x60292b), with three meshes of one triangle: 0 and 1 with the base pass in white and without
// the per-vertex light, 1 again with the additive chrome of eff_lighting (0x68f) with action 4, and the shadow of the
// three in (0, 0, 0, 0.7) flattened on the floor; in 7 at 0.2 and without it in 10, in Kalima and in 36.
TEST(CharacterDrawingTest, TheFenrirGoesWhiteChargesItsSecondMeshAndShadowsEveryMesh)
{
    const OneModel models(0x14a, "fenril.jpg", 3);
    DrawnObject fenrir = HorseLike();
    fenrir.model = 0x14a;
    struct Case
    {
        std::int32_t map;
        bool charging;
        std::optional<float> shadowAlpha;
    };
    for (const Case& probe :
         {Case{0, false, 0.7f}, Case{0, true, 0.7f}, Case{7, true, 0.2f}, Case{10, true, std::nullopt},
          Case{24, false, std::nullopt}, Case{36, false, std::nullopt}})
    {
        SCOPED_TRACE(testing::Message() << probe.map << " " << probe.charging);
        ModelBatch batch;
        batch.Begin(Client::Render::Pass3D{});

        const auto drawn = Client::Actors::Drawing::DrawFenrirBody(batch, models, fenrir, probe.map, probe.charging,
                                                                   std::chrono::milliseconds{0});

        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
        const std::size_t base = 1, chrome = probe.charging ? 1 : 0, shadow = probe.shadowAlpha ? 1 : 0;
        ASSERT_EQ(batch.Commands().size(), base + chrome + shadow);
        EXPECT_EQ(batch.Commands()[0].state, (ModelDrawState{TextureSlot{0x300}, ModelBlend::Opaque, true}));
        EXPECT_EQ(batch.Commands()[0].vertexCount, 2u * 3u);
        ExpectColor(batch.Vertices()[0], 1.0f, 1.0f, 1.0f);
        ExpectColor(batch.Vertices()[3], 1.0f, 1.0f, 1.0f);
        if (probe.charging)
        {
            EXPECT_EQ(batch.Commands()[1].state.texture, std::optional(TextureSlot{0x68f}));
            EXPECT_EQ(batch.Commands()[1].state.blend, ModelBlend::Additive);
            EXPECT_EQ(batch.Commands()[1].vertexCount, 3u);
        }
        if (probe.shadowAlpha)
        {
            const auto& last = batch.Commands().back();
            EXPECT_EQ(last.state, (ModelDrawState{std::nullopt, ModelBlend::BlendedShadow, true}));
            EXPECT_EQ(last.vertexCount, 3u * 3u);
            const Client::Render::ModelVertex& dark = batch.Vertices()[batch.Vertices().size() - 1];
            ExpectColor(dark, 0.0f, 0.0f, 0.0f);
            EXPECT_FLOAT_EQ(dark.color.alpha, *probe.shadowAlpha);
            EXPECT_FLOAT_EQ(dark.position.z, 30.0f + 5.0f);
        }
    }
}

// 0x602e90 with 10 in 0x6002e0 (0x60033c..0x600435): the model's color at 1 and, mesh by mesh, 0x4ecc30(mesh, 0x4040,
// 0.5, …, 0x620) without RenderBody: the 0x4000 pass with Chrome02 (0x620) forced, additive, with the normal's UVs
// (with the clock at 0, c = 1 and s = t = 0: u = nz + 3ny and v = 1 − nz − 2.5nz), and the color multiplied in place by
// 0.5 in each mesh that draws it (Q-C95). The object's hidden mesh (+0x58) goes the same; the one without triangles
// (0x4ecc5b) and the one with N (0x4ed07a) neither go nor multiply. They do not look at the object's alpha, color,
// blendMesh or per-vertex light, and there is no shadow.
TEST(CharacterDrawingTest, TheChromeShellGoesMeshByMeshWithChrome02AndHalvesTheColor)
{
    OneModel models(0xbf, "dk.jpg", 4);
    models.MeshAt(1).triangles.clear();
    models.MeshAt(2).textureName = "dkneck2_N.jpg";
    DrawnObject horse = HorseLike();
    horse.alpha = 0.3f;
    horse.state.blendMesh = -2;
    horse.state.hiddenMesh = 0;
    ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});

    const auto drawn = Client::Actors::Drawing::DrawChromeShell(batch, models, horse, std::chrono::milliseconds{0});

    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    ASSERT_EQ(batch.Commands().size(), 1u);
    EXPECT_EQ(batch.Commands()[0].state, (ModelDrawState{TextureSlot{0x620}, ModelBlend::Additive, true}));
    ASSERT_EQ(batch.Commands()[0].vertexCount, 2u * 3u);
    const auto vertices = batch.Vertices();
    for (std::size_t corner = 0; corner < 3; ++corner)
    {
        ExpectColor(vertices[corner], 0.5f, 0.5f, 0.5f);
        ExpectColor(vertices[3 + corner], 0.25f, 0.25f, 0.25f);
        EXPECT_EQ(vertices[corner].uv, glm::vec2(1.0f, -2.5f));
        EXPECT_EQ(vertices[3 + corner].uv, glm::vec2(1.0f, -2.5f));
    }
}

// The UVs of the 0x4000 carry the normal with the object's scale (0x4ebc80 does not normalize it): with 1.6, u = 1.6
// and v = 1 − 1.6 − 4. A model the library does not have draws nothing.
TEST(CharacterDrawingTest, TheChromeShellTakesTheScaledNormal)
{
    const OneModel models(0xbf, "dk.jpg", 1);
    DrawnObject horse = HorseLike();
    horse.transform.scale = 1.6f;
    ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});

    ASSERT_TRUE(
        Client::Actors::Drawing::DrawChromeShell(batch, models, horse, std::chrono::milliseconds{0}).HasValue());

    ASSERT_EQ(batch.Commands().size(), 1u);
    const float scaled = static_cast<float>(1.0 * static_cast<double>(1.6f));
    EXPECT_EQ(batch.Vertices()[0].uv,
              glm::vec2(scaled, static_cast<float>((1.0 - scaled) - static_cast<double>(scaled) * 2.5)));
    DrawnObject other = horse;
    other.model = 0xc0;
    ModelBatch none;
    none.Begin(Client::Render::Pass3D{});
    ASSERT_TRUE(Client::Actors::Drawing::DrawChromeShell(none, models, other, std::chrono::milliseconds{0}).HasValue());
    EXPECT_TRUE(none.Commands().empty());
}

// The chrome of 0x4ecc30 with 0x4 (Chrome): u = 0.5 · nz + t and v = 0.5 · ny + 2t, with the normal carrying the
// horse's scale: with 1.6 and the clock at 0, the normal in z gives u = 0.8.
TEST(CharacterDrawingTest, TheHorseChromeTakesTheScaledNormal)
{
    const OneModel models(0xbf, "dk.jpg", 13);
    DrawnObject horse = HorseLike();
    horse.transform.scale = 1.6f;

    const ModelBatch batch = DrawnHorseOf(models, horse);

    ASSERT_EQ(batch.Commands().size(), 3u);
    const Client::Render::ModelVertex& chrome = batch.Vertices()[13 * 3];
    EXPECT_FLOAT_EQ(chrome.uv.x, 0.8f);
    EXPECT_FLOAT_EQ(chrome.uv.y, 0.0f);
}

// 0x6002e0 asks for the chrome of meshes 12 to 15, and each one that exists multiplies the color in place by the alpha
// (Q-C95): with 16 meshes and 0.5, they go by 0.5, 0.25, 0.125 and 0.0625.
TEST(CharacterDrawingTest, TheHorseChromeGoesOnMeshesTwelveToFifteen)
{
    const OneModel models(0xbf, "dk.jpg", 16);
    DrawnObject horse = HorseLike();
    horse.alpha = 0.5f;

    const ModelBatch batch = DrawnHorseOf(models, horse);

    ASSERT_EQ(batch.Commands().size(), 3u);
    EXPECT_EQ(batch.Commands()[1].state.texture, TextureSlot{0x5c0});
    ASSERT_EQ(batch.Commands()[1].vertexCount, 4u * 3u);
    const std::size_t first = batch.Commands()[1].firstVertex;
    for (std::size_t mesh = 0; mesh < 4; ++mesh)
    {
        const float factor = std::ldexp(1.0f, -static_cast<int>(mesh + 1));
        ExpectColor(batch.Vertices()[first + (mesh * 3)], factor, 0.8f * factor, 0.3f * factor);
    }
}

// The chrome goes through 0x4ecc30, which does not run the glow passes of a mesh with N in the script.
TEST(CharacterDrawingTest, TheHorseChromeSkipsAMeshWithoutShine)
{
    OneModel models(0xbf, "dk.jpg", 13);
    models.MeshAt(12).textureName = "dkneck2_N.jpg";

    const ModelBatch batch = DrawnHorseOf(models, HorseLike());

    ASSERT_EQ(batch.Commands().size(), 2u);
    EXPECT_EQ(batch.Commands()[1].state.blend, ModelBlend::BlendedShadow);
}

// Without mesh 12 there is no chrome, and without mesh 8 there is no shadow; with the alpha below 0.01, nothing.
TEST(CharacterDrawingTest, TheHorseDrawsOnlyTheMeshesItHas)
{
    const OneModel nine(0xbf, "dk.jpg", 9);
    const ModelBatch withoutChrome = DrawnHorseOf(nine, HorseLike());
    ASSERT_EQ(withoutChrome.Commands().size(), 2u);
    EXPECT_EQ(withoutChrome.Commands()[1].state.blend, ModelBlend::BlendedShadow);

    const OneModel eight(0xbf, "dk.jpg", 8);
    EXPECT_EQ(DrawnHorseOf(eight, HorseLike()).Commands().size(), 1u);

    const OneModel models(0xbf, "dk.jpg", 13);
    DrawnObject hidden = HorseLike();
    hidden.alpha = 0.0f;
    EXPECT_TRUE(DrawnHorseOf(models, hidden).Commands().empty());
}

// An object whose model is not there is not drawn.
TEST(CharacterDrawingTest, AnObjectWithoutItsModelIsNotDrawn)
{
    const OneModel models(0xa3, "logo.jpg", 2);

    EXPECT_TRUE(DrawnObjectOf(models, LogoLike()).Commands().empty());
}

// 0x509340, case 0x620: with the HDK_Mace in the hand and the frame's table, the sceptre's five sprites on bone 0 of
// the mace's pose (with its own position and turn, hanging from the body turned by the character), with its scale and
// its position and the cloth's rand(): the same as CreateScepterGlow with that bone, which is not the root it hangs
// from.
TEST(CharacterDrawingTest, TheScepterAsksForItsFiveSprites)
{
    DrawnCharacter character;
    character.appearance = Client::Kernel::ClassAppearance(0, 0);
    character.appearance.rightHand = Client::Kernel::WornItem{.model = Client::World::HdkMaceModel};
    character.object.position = {1000.0f, 2000.0f, 300.0f};
    character.object.angles = {0.0f, 0.0f, 90.0f};
    character.object.scale = 1.5f;
    Client::Models::ClothWorld world{.random = Client::Crt::CrtRandom(7)};
    Client::World::FrameSprites sprites;
    character.clothWorld = &world;
    character.sprites = &sprites;
    OneModel models(Client::World::HdkMaceModel);
    BoneKey& key = models.ItemSkeleton().bones[0].actions[0][0];
    key.position = {5.0f, 10.0f, 20.0f};
    key.rotation = Client::Math::AngleQuaternion({0.3f, 0.0f, 0.0f});

    (void)Drawn(models, character);

    // The root is bone 33 of the flat body: the character's turn, without translation.
    const Client::Math::BoneTransform root = Client::Math::AngleMatrix({0.0f, 0.0f, 90.0f});
    std::vector<Client::Math::BoneTransform> mace(1);
    ASSERT_TRUE(Client::Models::PoseAttached(models.ItemSkeleton(), Client::Models::AnimationState{}, root, mace));
    ASSERT_NE(mace.front(), root);
    Client::World::FrameSprites expected;
    Client::Crt::CrtRandom random(7);
    Client::World::CreateScepterGlow(expected, Client::Math::OriginalMatrixOf(mace.front()), 1.5f,
                                     character.object.position, random);
    for (std::size_t k = 0; k <= Client::World::ScepterSpriteCount; ++k)
    {
        const Client::World::Sprite& got = sprites.Sprites()[k];
        const Client::World::Sprite& want = expected.Sprites()[k];
        EXPECT_EQ(got.alive, want.alive) << k;
        EXPECT_EQ(got.texture, want.texture) << k;
        EXPECT_EQ(got.scale, want.scale) << k;
        EXPECT_EQ(got.position, want.position) << k;
        EXPECT_EQ(got.rotation, want.rotation) << k;
    }
    EXPECT_EQ(world.random.State(), random.State()) << "seis rand() de la tela";
}

// With the sceptre's draw (World::ScepterGlowCarry), two MuRet frames in the same original frame ask for the same five
// sprites and draw the six rand() calls only once.
TEST(CharacterDrawingTest, TheScepterDrawsOncePerOriginalFrameWithItsCarry)
{
    DrawnCharacter character;
    character.appearance = Client::Kernel::ClassAppearance(0, 0);
    character.appearance.rightHand = Client::Kernel::WornItem{.model = Client::World::HdkMaceModel};
    Client::Models::ClothWorld world{.random = Client::Crt::CrtRandom(7)};
    Client::World::ScepterGlowCarry carry;
    character.clothWorld = &world;
    character.scepter = &carry;
    const OneModel models(Client::World::HdkMaceModel);
    Client::World::FrameSprites first;
    character.sprites = &first;
    (void)Drawn(models, character);
    const std::uint32_t afterTheFirst = world.random.State();

    Client::World::FrameSprites second;
    character.sprites = &second;
    (void)Drawn(models, character);

    EXPECT_EQ(world.random.State(), afterTheFirst);
    for (std::size_t k = 0; k < Client::World::ScepterSpriteCount; ++k)
    {
        EXPECT_TRUE(second.Sprites()[k].alive) << k;
        EXPECT_EQ(second.Sprites()[k].scale, first.Sprites()[k].scale) << k;
        EXPECT_EQ(second.Sprites()[k].position, first.Sprites()[k].position) << k;
    }
}

// On the left too (0x509340 does not look at the hand); without the table, or without the cloth's rand(), it asks for
// none and draws no rand(); and another item does not have that case.
TEST(CharacterDrawingTest, OnlyTheScepterWithTheTableAsksForSprites)
{
    const OneModel mace(Client::World::HdkMaceModel);
    {
        DrawnCharacter character;
        character.appearance = Client::Kernel::ClassAppearance(0, 0);
        character.appearance.leftHand = Client::Kernel::WornItem{.model = Client::World::HdkMaceModel};
        Client::Models::ClothWorld world;
        Client::World::FrameSprites sprites;
        character.clothWorld = &world;
        character.sprites = &sprites;
        (void)Drawn(mace, character);
        EXPECT_TRUE(sprites.Sprites()[Client::World::ScepterSpriteCount - 1].alive) << "la izquierda";
    }
    {
        DrawnCharacter character;
        character.appearance = Client::Kernel::ClassAppearance(0, 0);
        character.appearance.rightHand = Client::Kernel::WornItem{.model = Client::World::HdkMaceModel};
        Client::Models::ClothWorld world;
        character.clothWorld = &world;
        (void)Drawn(mace, character);
        EXPECT_EQ(world.random.State(), Client::Crt::CrtRandom().State()) << "sin la tabla";
        Client::World::FrameSprites sprites;
        character.clothWorld = nullptr;
        character.sprites = &sprites;
        (void)Drawn(mace, character);
        EXPECT_FALSE(sprites.Any()) << "sin el rand()";
    }
    {
        const OneModel sword(0x212);
        DrawnCharacter character;
        character.appearance = Client::Kernel::ClassAppearance(0, 0);
        character.appearance.rightHand = Client::Kernel::WornItem{.model = 0x212};
        Client::Models::ClothWorld world;
        Client::World::FrameSprites sprites;
        character.clothWorld = &world;
        character.sprites = &sprites;
        (void)Drawn(sword, character);
        EXPECT_FALSE(sprites.Any()) << "otro ítem";
    }
}

// The point of an object's bone, posed as in DrawObject and taken to the world like its vertices: with bone 0 flat, the
// offset times the scale plus the position; a bone the model does not have, a dummy one (0x4eb973 does not write it in
// the global buffer) or a model that is not there, nothing.
TEST(CharacterDrawingTest, ThePointOfABoneGoesToTheWorldLikeTheVertices)
{
    OneModel models(0xa3, "logo.jpg");
    DrawnObject object;
    object.model = 0xa3;
    object.transform.position = {100.0f, 200.0f, 300.0f};
    object.transform.scale = 2.0f;
    const auto point = Client::Actors::Drawing::PosedBonePoint(models, object, 0, {50.0f, -4.0f, 0.0f});
    ASSERT_TRUE(point.has_value());
    EXPECT_EQ(*point, glm::vec3(200.0f, 192.0f, 300.0f));
    EXPECT_FALSE(Client::Actors::Drawing::PosedBonePoint(models, object, 1, glm::vec3(0.0f)).has_value());
    object.model = 0xa4;
    EXPECT_FALSE(Client::Actors::Drawing::PosedBonePoint(models, object, 0, glm::vec3(0.0f)).has_value());
    object.model = 0xa3;
    models.ItemSkeleton().bones[0].dummy = true;
    EXPECT_FALSE(Client::Actors::Drawing::PosedBonePoint(models, object, 0, glm::vec3(0.0f)).has_value()) << "dummy";
}

// With a bone that translates and turns, and the object turned, scaled and in its place, the point of PosedBonePoint is
// that of a vertex at `offset` hanging from that bone, taken to the world by SkinMesh: the offset goes through the bone
// before the scale.
TEST(CharacterDrawingTest, ThePointOfABoneIsWhereItsVertexWouldBe)
{
    OneModel models(0xa3, "logo.jpg");
    BoneKey& key = models.ItemSkeleton().bones[0].actions[0][0];
    key.position = {12.0f, -7.0f, 30.0f};
    key.rotation = Client::Math::AngleQuaternion({0.2f, -0.4f, 0.9f});
    DrawnObject object;
    object.model = 0xa3;
    object.transform.position = {11000.0f, 14000.0f, 300.0f};
    object.transform.angles = {0.0f, 0.0f, 120.0f};
    object.transform.scale = 1.6f;
    const glm::vec3 offset{50.0f, -4.0f, 0.0f};

    std::vector<Client::Math::BoneTransform> bones(1);
    ASSERT_TRUE(Client::Models::PoseSkeleton(models.ItemSkeleton(), object.animation, object.transform, glm::vec2(0.0f),
                                             bones));
    Mesh mesh;
    mesh.vertices = {{0, {offset.x, offset.y, offset.z}}};
    Client::Models::SkinnedMesh skinned;
    ASSERT_TRUE(Client::Models::SkinMesh(mesh, bones, object.transform, skinned));

    const auto point = Client::Actors::Drawing::PosedBonePoint(models, object, 0, offset);
    ASSERT_TRUE(point.has_value());
    EXPECT_EQ(*point, skinned.positions.front());
}

// The global pose buffer is left by the last posed model that has the bone without it being a dummy: with two, the
// second wins; if the second's is a dummy, the first's stays; with none, nothing.
TEST(CharacterDrawingTest, TheLastPosedWithTheBoneWins)
{
    OneModel first(0xa3, "logo.jpg");
    OneModel second(0xa4, "logo.jpg");
    DrawnObject a;
    a.model = 0xa3;
    a.transform.position = {100.0f, 0.0f, 0.0f};
    DrawnObject b;
    b.model = 0xa4;
    b.transform.position = {200.0f, 0.0f, 0.0f};
    const std::array<Client::Actors::Drawing::PosedInTheBuffer, 2> posed{{{&first, a}, {&second, b}}};
    const auto point = Client::Actors::Drawing::LastPosedBonePoint(posed, 0, glm::vec3(0.0f));
    ASSERT_TRUE(point.has_value());
    EXPECT_EQ(point->x, 200.0f);
    second.ItemSkeleton().bones[0].dummy = true;
    const auto underneath = Client::Actors::Drawing::LastPosedBonePoint(posed, 0, glm::vec3(0.0f));
    ASSERT_TRUE(underneath.has_value());
    EXPECT_EQ(underneath->x, 100.0f);
    first.ItemSkeleton().bones[0].dummy = true;
    EXPECT_FALSE(Client::Actors::Drawing::LastPosedBonePoint(posed, 0, glm::vec3(0.0f)).has_value());
    EXPECT_FALSE(Client::Actors::Drawing::LastPosedBonePoint({}, 0, glm::vec3(0.0f)).has_value());
}
} // namespace

namespace
{
TEST(CharacterDrawingTest, PublishesTheCalculatedBodyAndAttachedMatricesInDrawOrder)
{
    const TwoPets models;
    DrawnCharacter character;
    character.appearance.pet = std::uint16_t{0x1c13};
    character.object.position = {100, 200, 300};
    character.object.scale = 2;
    std::vector<std::uint16_t> published;
    character.onPose = [&](std::uint16_t model, std::span<const Client::Math::BoneTransform> bones,
                           const Client::Models::ObjectTransform& transform, bool translated)
    {
        published.push_back(model);
        EXPECT_FALSE(bones.empty());
        EXPECT_EQ(transform.position, character.object.position);
        EXPECT_FLOAT_EQ(transform.scale, 2);
        EXPECT_FALSE(translated);
    };
    ModelBatch batch;
    DrawCharacter(batch, models, character, {});
    // What hangs is published once, with its root; the parts are not posed again.
    ASSERT_EQ(published.size(), 2U);
    EXPECT_EQ(published[0], 0x20a);
    EXPECT_EQ(published[1], 0x1c13);
}
} // namespace
