#include "Effects/CastleWeaponBodies.h"

#include "Core/Expected.h"
#include "Data/Models/Model.h"
#include "Math/HalfLifeMath.h"
#include "Models/IModelLibrary.h"
#include "Models/ModelId.h"
#include "Models/Skeleton.h"
#include "Models/Skinning.h"
#include "Render/ModelBatch.h"
#include "Render/Pass3D.h"
#include "Render/TextureSlot.h"
#include "Actors/Drawing/CharacterDrawing.h"
#include "Effects/ModelEffects.h"

#include <glm/ext/vector_float3.hpp>
#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <vector>

// The siege weapons' bodies in 0x6002e0 (0x601c90): the stones and their impact, the fragments and the extra pass.
namespace
{
using Client::Actors::Drawing::DrawnObject;
using Client::Data::Models::Mesh;
using Client::Data::Models::Model;
using Client::Effects::DrawCastleWeaponBody;
using Client::Models::IModelLibrary;
using Client::Models::Skeleton;
using Client::Render::ModelBatch;
using Client::Render::TextureSlot;

// A skeleton of one bone at the origin, with an action of one key.
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

constexpr std::uint16_t WeaponModel = 0x4c;

// `meshes` meshes of one triangle 10 above the floor, each with the texture of its index and slot 0x300 plus the
// index.
class WeaponModels final : public IModelLibrary
{
public:
    explicit WeaponModels(std::size_t meshes) : m_skeleton(OneBone())
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
        return model.Value() == WeaponModel ? &m_model : nullptr;
    }
    [[nodiscard]] const Skeleton* SkeletonOf(Client::Models::ModelId model) const override
    {
        return model.Value() == WeaponModel ? &m_skeleton : nullptr;
    }
    [[nodiscard]] std::span<const TextureSlot> MeshTextures(Client::Models::ModelId model) const override
    {
        return model.Value() == WeaponModel ? std::span<const TextureSlot>(m_slots) : std::span<const TextureSlot>{};
    }

private:
    Skeleton m_skeleton;
    Model m_model;
    std::vector<TextureSlot> m_slots;
};

DrawnObject WeaponObject()
{
    DrawnObject object;
    object.model = WeaponModel;
    object.transform.position = {9200.0f, 9900.0f, 180.0f};
    return object;
}

TEST(CastleWeaponBodiesTest, TheStoneHasItsWhiteBaseAndRedDefaultChrome)
{
    const WeaponModels models(1);
    Client::Effects::ModelEffect effect;
    effect.type = 0x12b;
    effect.subtype = 1;
    DrawnObject object = WeaponObject();
    object.color = {.2f, .4f, .6f};
    ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});
    ASSERT_TRUE(DrawCastleWeaponBody(batch, models, effect, object, {}).HasValue());
    ASSERT_EQ(batch.Vertices().size(), 6u);
    ASSERT_GE(batch.Commands().size(), 2u);
    EXPECT_EQ(batch.Commands().front().state.texture, TextureSlot{0x300});
    EXPECT_EQ(batch.Commands().back().state.texture, Client::Actors::Drawing::ChromeTextureSlot);
    EXPECT_FLOAT_EQ(batch.Vertices()[0].color.red, 1);
    EXPECT_FLOAT_EQ(batch.Vertices()[3].color.green, .2f);
}

TEST(CastleWeaponBodiesTest, TheImpactHidesBeforeEmittingItsNativeVertexFamilyAndDoesNotDrawTheBase)
{
    const WeaponModels models(1);
    for (const std::int16_t type : std::array<std::int16_t, 2>{0x12b, 0x12c})
    {
        Client::Effects::ModelEffect effect;
        effect.type = type;
        effect.seen = true;
        effect.objectState.hiddenMesh = 99;
        DrawnObject object = WeaponObject();
        object.state = effect.objectState;
        ModelBatch batch;
        batch.Begin(Client::Render::Pass3D{});
        int calls = 0;
        ASSERT_TRUE(
            DrawCastleWeaponBody(
                batch, models, effect, object, {},
                [&](std::int32_t family, const DrawnObject& owner,
                    std::span<const Client::Models::SkinnedMesh> meshes) -> Client::Core::Expected<void, std::string>
                {
                    ++calls;
                    EXPECT_EQ(family, type == 0x12b ? 0x12d : 0x12e);
                    EXPECT_EQ(effect.objectState.hiddenMesh, -2);
                    EXPECT_EQ(owner.state.hiddenMesh, -2);
                    EXPECT_EQ(meshes.size(), 1u);
                    return {};
                })
                .HasValue());
        EXPECT_EQ(calls, 1);
        EXPECT_TRUE(batch.Vertices().empty());
    }
}

TEST(CastleWeaponBodiesTest, InvisibleAndUnorderedAlphaDoNotConsumeTheImpact)
{
    const WeaponModels models(1);
    for (const float alpha : {0.0f, std::numeric_limits<float>::quiet_NaN()})
    {
        Client::Effects::ModelEffect effect;
        effect.type = 0x12b;
        effect.seen = true;
        effect.alpha = alpha;
        effect.objectState.hiddenMesh = 99;
        DrawnObject object = WeaponObject();
        object.state = effect.objectState;
        ModelBatch batch;
        batch.Begin(Client::Render::Pass3D{});
        ASSERT_TRUE(DrawCastleWeaponBody(batch, models, effect, object, {}).HasValue());
        EXPECT_EQ(effect.objectState.hiddenMesh, 99);
        EXPECT_TRUE(batch.Vertices().empty());
    }
}

TEST(CastleWeaponBodiesTest, FragmentColorsAndTheExtraBodyPassFollowTheNativeRestoration)
{
    const WeaponModels models(1);
    struct Case
    {
        std::int16_t type;
        std::int32_t subtype;
        std::size_t vertices;
        glm::vec3 last;
    };
    const std::array cases{
        Case{0x12d, 1, 9, {1, 1, 1}},       Case{0x12e, 2, 6, {1, 1, 1}},       Case{0x12f, 0, 6, {.2f, .4f, .6f}},
        Case{0x10a, 0, 12, {.3f, .3f, 1}},  Case{0x10b, 2, 6, {.1f, .3f, .6f}}, Case{0x114, 2, 6, {.2f, .4f, .6f}},
        Case{0x114, 6, 6, {.2f, .4f, .6f}}, Case{0x115, 1, 6, {.2f, .4f, .6f}}, Case{0xed, 0, 9, {1, 1, 1}},
        Case{0xee, 0, 9, {1, 1, 1}},        Case{0xe1, 1, 6, {.2f, .4f, .6f}}};
    for (const auto& item : cases)
    {
        Client::Effects::ModelEffect effect;
        effect.type = item.type;
        effect.subtype = item.subtype;
        effect.flags78 = 2;
        DrawnObject object = WeaponObject();
        object.color = {.2f, .4f, .6f};
        ModelBatch batch;
        batch.Begin(Client::Render::Pass3D{});
        ASSERT_TRUE(DrawCastleWeaponBody(batch, models, effect, object, {}).HasValue());
        ASSERT_EQ(batch.Vertices().size(), item.vertices);
        EXPECT_FLOAT_EQ(batch.Vertices().back().color.red, item.last.r);
        EXPECT_FLOAT_EQ(batch.Vertices().back().color.green, item.last.g);
        EXPECT_FLOAT_EQ(batch.Vertices().back().color.blue, item.last.b);
    }
}

// 0x601c90: the impact needs who emits its fragments (0x4ee770); without it, it stops after hiding the mesh.
TEST(CastleWeaponBodiesTest, TheImpactStopsWithoutTheVertexEmitter)
{
    const WeaponModels models(1);
    Client::Effects::ModelEffect effect;
    effect.type = 0x12b;
    effect.seen = true;
    effect.objectState.hiddenMesh = 99;
    DrawnObject object = WeaponObject();
    object.state = effect.objectState;
    ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});
    const auto stopped = DrawCastleWeaponBody(batch, models, effect, object, {});
    ASSERT_FALSE(stopped.HasValue());
    EXPECT_NE(stopped.Error().find("0x601c90"), std::string::npos) << stopped.Error();
    EXPECT_EQ(effect.objectState.hiddenMesh, -2);
    EXPECT_TRUE(batch.Vertices().empty());
}
} // namespace
