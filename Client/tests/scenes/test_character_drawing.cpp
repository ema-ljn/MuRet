#include "Scenes/CharacterDrawing.h"

#include "Data/Models/Model.h"
#include "GameLogic/CharacterAppearance.h"
#include "Render/ModelBatch.h"
#include "Render/Models/HalfLifeMath.h"
#include "Render/Models/Skeleton.h"
#include "Render/Pass3D.h"
#include "Render/TextureSlot.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>

// Client::Scenes::CharacterDrawing: el slot de la piel y del pelo de cada clase (0x50d41c) y la
// malla oculta (06 § El modelo en el binario). El dibujo entero va contra la traza en
// GoldenCharacterDrawing.
namespace
{
using Client::Data::Models::Mesh;
using Client::Data::Models::Model;
using Client::Render::ModelBatch;
using Client::Render::TextureSlot;
using Client::Render::Models::ActionInfo;
using Client::Render::Models::BoneKey;
using Client::Render::Models::Skeleton;
using Client::Render::Models::SkeletonBone;
using Client::Scenes::DrawCharacter;
using Client::Scenes::DrawnCharacter;
using Client::Scenes::IModelLibrary;
using Client::Scenes::MeshSlotOf;
using Client::Scenes::SkinIndexOf;

// Un esqueleto de `bones` huesos sin padre, todos en el origen, con una acción de una key.
Skeleton FlatSkeleton(std::size_t bones)
{
    Skeleton skeleton;
    SkeletonBone bone;
    bone.parent = -1;
    bone.actions = {{BoneKey{}}};
    bone.actions[0][0].rotation = Client::Render::Models::AngleQuaternion({0.0f, 0.0f, 0.0f});
    skeleton.bones.assign(bones, bone);
    skeleton.actions = {ActionInfo{.repeats = true, .speed = 0.0f, .keyCount = 1, .lockPositions = false}};
    return skeleton;
}

// Un modelo de una malla con un triángulo, colgado del hueso 0.
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

// Player.bmd con los 48 huesos que cuelgan cosas, y dos mascotas de un triángulo.
class TwoPets final : public IModelLibrary
{
public:
    TwoPets() : m_body(FlatSkeleton(48)), m_pet(FlatSkeleton(1)), m_model(OneTriangle()) {}

    [[nodiscard]] const Model* Find(std::uint16_t model) const override
    {
        return model == 0x1c12 || model == 0x1c13 ? &m_model : nullptr;
    }
    [[nodiscard]] const Skeleton* SkeletonOf(std::uint16_t model) const override
    {
        if (model == 0x20a)
        {
            return &m_body;
        }
        return model == 0x1c12 || model == 0x1c13 ? &m_pet : nullptr;
    }
    [[nodiscard]] std::span<const TextureSlot> MeshTextures(std::uint16_t /*model*/) const override
    {
        return {};
    }

private:
    Skeleton m_body;
    Skeleton m_pet;
    Model m_model;
};

// Q-C98: de las mascotas, solo 0x1c13 se dibuja; las partes que la biblioteca no tiene, tampoco.
TEST(CharacterDrawingTest, OnlyHelper02IsDrawnAsThePet)
{
    const TwoPets models;
    for (const auto& [pet, vertices] : {std::pair{std::uint16_t{0x1c13}, 3u}, std::pair{std::uint16_t{0x1c12}, 0u}})
    {
        DrawnCharacter character;
        character.appearance = Client::GameLogic::ClassAppearance(0, 0);
        character.appearance.pet = pet;
        ModelBatch batch;
        batch.Begin(Client::Render::Pass3D{});

        DrawCharacter(batch, models, character, std::chrono::milliseconds{0});

        EXPECT_EQ(batch.Vertices().size(), vertices) << pet;
    }
}

// 2 · (clase & 7) + (clase >> 3): la clase c usa 0x12e + 2c y su segunda clase el de al lado.
TEST(CharacterDrawingTest, TheSkinGoesByClass)
{
    EXPECT_EQ(SkinIndexOf(0), 0);
    EXPECT_EQ(SkinIndexOf(1), 2);
    EXPECT_EQ(SkinIndexOf(8), 1);
    EXPECT_EQ(SkinIndexOf(4), 8);
    EXPECT_EQ(SkinIndexOf(12), 9);
}

// La piel va a 0x12e más la piel, el pelo a 0x12f más la piel (la clase 4, al 0x137 de hair_r), la
// oculta no se dibuja, y las demás quedan en su slot.
TEST(CharacterDrawingTest, TheSkinAndHairSlotsFollowTheClass)
{
    EXPECT_EQ(MeshSlotOf(TextureSlot{0x12e}, 1), std::optional<TextureSlot>{TextureSlot{0x130}});
    EXPECT_EQ(MeshSlotOf(TextureSlot{0x12e}, 8), std::optional<TextureSlot>{TextureSlot{0x12f}});
    EXPECT_EQ(MeshSlotOf(TextureSlot{0x137}, 4), std::optional<TextureSlot>{TextureSlot{0x137}});
    EXPECT_EQ(MeshSlotOf(TextureSlot{0x12d}, 0), std::nullopt);
    EXPECT_EQ(MeshSlotOf(TextureSlot{0x6f8}, 3), std::optional<TextureSlot>{TextureSlot{0x6f8}});
}
} // namespace
