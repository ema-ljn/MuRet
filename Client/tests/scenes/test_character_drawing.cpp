#include "Scenes/CharacterDrawing.h"

#include "Data/Models/Model.h"
#include "Data/Models/PlayerModels.h"
#include "GameLogic/CharacterAppearance.h"
#include "Render/ModelBatch.h"
#include "Render/ModelVertex.h"
#include "Render/Models/PassColors.h"
#include "Render/Models/HalfLifeMath.h"
#include "Render/Models/Skeleton.h"
#include "Render/Pass3D.h"
#include "Render/SpriteVertex.h"
#include "Render/TextureSlot.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <ios>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

// Client::Scenes::CharacterDrawing: el slot de la piel y del pelo de cada clase (0x50d41c) y la
// malla oculta (06 § El modelo en el binario). El dibujo entero va contra la traza en
// GoldenCharacterDrawing.
namespace
{
using Client::Data::Models::Mesh;
using Client::Data::Models::Model;
using Client::Render::ModelBatch;
using Client::Render::ModelBlend;
using Client::Render::ModelDrawState;
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

// Player.bmd y, para `drawn`, un modelo de `meshes` mallas de un triángulo colgadas del hueso 0, con la
// textura `texture` en `slot`; los demás no están.
class OneModel final : public IModelLibrary
{
public:
    explicit OneModel(std::uint16_t drawn, std::string texture = "part.jpg", std::size_t meshes = 1,
                      TextureSlot slot = TextureSlot{0x300})
        : m_body(FlatSkeleton(48)), m_item(FlatSkeleton(1)), m_model(OneTriangle()), m_drawn(drawn)
    {
        m_model.meshes[0].textureName = std::move(texture);
        m_model.meshes.resize(meshes, m_model.meshes[0]);
        m_slots.assign(meshes, slot);
    }

    [[nodiscard]] const Model* Find(std::uint16_t model) const override
    {
        return model == m_drawn ? &m_model : nullptr;
    }
    [[nodiscard]] const Skeleton* SkeletonOf(std::uint16_t model) const override
    {
        if (model == 0x20a)
        {
            return &m_body;
        }
        return model == m_drawn ? &m_item : nullptr;
    }
    [[nodiscard]] std::span<const TextureSlot> MeshTextures(std::uint16_t model) const override
    {
        return model == m_drawn ? std::span<const TextureSlot>(m_slots) : std::span<const TextureSlot>{};
    }

    // Para armar una malla rota.
    Mesh& FirstMesh()
    {
        return m_model.meshes[0];
    }

private:
    Skeleton m_body;
    Skeleton m_item;
    Model m_model;
    std::uint16_t m_drawn;
    std::vector<TextureSlot> m_slots;
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

// 0x60cf70 con su modo 1, el personaje bajo el mouse: antes de cada parte, dos pases sin textura y
// aditivos (0x60cd60 con 0x40), en (0,1; 0,03; 0) a 1,2 del hueso (+0x94) y en (0,7; 0,2; 0) a
// 1,2 − 0,02; después la parte, como siempre.
TEST(CharacterDrawingTest, AnOutlinedPartGoesTwiceAroundItsBoneFirst)
{
    DrawnCharacter character;
    character.appearance = Client::GameLogic::ClassAppearance(0, 0);
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

// Los dos pases recorren todas las mallas de la parte, y la parte va después (0x60cf70).
TEST(CharacterDrawingTest, TheOutlineGoesBeforeEveryMeshOfThePart)
{
    DrawnCharacter character;
    character.appearance = Client::GameLogic::ClassAppearance(0, 0);
    character.outlined = true;
    const OneModel models(character.appearance.bodyParts[1].model, "part.jpg", 2);

    const ModelBatch batch = Drawn(models, character);

    ASSERT_EQ(batch.Commands().size(), 2u);
    EXPECT_EQ(batch.Commands()[0].state.blend, ModelBlend::Additive);
    EXPECT_EQ(batch.Commands()[0].vertexCount, 2u * 2u * 3u);
    EXPECT_EQ(batch.Commands()[1].vertexCount, 2u * 3u);
}

// La malla oculta (0x12d, la que ModelTextureSlots manda ahí por su nombre) no se dibuja, y tampoco
// su contorno.
TEST(CharacterDrawingTest, TheHiddenMeshHasNoOutline)
{
    DrawnCharacter character;
    character.appearance = Client::GameLogic::ClassAppearance(0, 0);
    character.outlined = true;
    const OneModel models(character.appearance.bodyParts[1].model, "hide.jpg", 1, TextureSlot{0x12d});

    const ModelBatch batch = Drawn(models, character);

    EXPECT_TRUE(batch.Commands().empty());
}

// 0x4ecc30 con 0x40 sin 0x2: una malla de cuatro componentes (una OZT) no va en el contorno.
TEST(CharacterDrawingTest, TheOutlineSkipsAFourComponentMesh)
{
    DrawnCharacter character;
    character.appearance = Client::GameLogic::ClassAppearance(0, 0);
    character.outlined = true;
    const OneModel models(character.appearance.bodyParts[1].model, "part.tga");

    const ModelBatch batch = Drawn(models, character);

    ASSERT_EQ(batch.Commands().size(), 1u);
    EXPECT_EQ(batch.Commands()[0].state.blend, ModelBlend::Alpha);
}

// El contorno es de las seis partes (0x60cf70 en el lazo de 0x50ba30): lo que cuelga, por 0x509340,
// no lo lleva.
TEST(CharacterDrawingTest, WhatHangsIsNotOutlined)
{
    DrawnCharacter character;
    character.appearance = Client::GameLogic::ClassAppearance(0, 0);
    character.appearance.rightHand = Client::GameLogic::WornItem{.model = 0x212};
    character.outlined = true;
    const OneModel models(0x212);

    const ModelBatch batch = Drawn(models, character);

    ASSERT_EQ(batch.Commands().size(), 1u);
    EXPECT_EQ(batch.Commands()[0].state.blend, ModelBlend::Opaque);
}

// El lazo de las manos de 0x50ba30 saltea las saetas (0xa19), las flechas (0xa21) y el cuervo
// (0x1c17): no van en la mano. Las flechas y las saetas van a la espalda, y el cuervo es un
// objeto aparte (PR F).
TEST(CharacterDrawingTest, ArrowsBoltsAndTheRavenAreNotInTheHand)
{
    for (const std::uint16_t item : {std::uint16_t{0xa19}, std::uint16_t{0xa21}, std::uint16_t{0x1c17}})
    {
        DrawnCharacter character;
        character.appearance = Client::GameLogic::ClassAppearance(0, 0);
        character.appearance.rightHand = Client::GameLogic::WornItem{.model = item};
        character.appearance.leftHand = Client::GameLogic::WornItem{.model = item};
        const OneModel models(item);

        const ModelBatch batch = Drawn(models, character);

        EXPECT_TRUE(batch.Vertices().empty()) << std::hex << item;
    }
}

// Los colores de los pases de una parte de +13 (0x60b3d0): el base y el 0x1040 con la luz por 0,9
// (0x6093d0 multiplica por 1), y el 0x48 y el 0x44 con el color del ítem (0x608e00), sin la luz.
TEST(CharacterDrawingTest, AThirteenPartGlowsWithItsLightAndItsColor)
{
    DrawnCharacter character;
    character.appearance = Client::GameLogic::ClassAppearance(0, 0);
    character.appearance.bodyParts[1].level = 13;
    const std::uint16_t part = character.appearance.bodyParts[1].model;
    const OneModel models(part);

    const ModelBatch batch = Drawn(models, character);

    const auto vertices = batch.Vertices();
    ASSERT_EQ(vertices.size(), 4u * 3u);
    const Client::Render::Color item = Client::Render::Models::ShineColorOf(part);
    ExpectColor(vertices[0], 0.4f * 0.9f, 0.4f * 0.9f, 0.4f * 0.9f);
    ExpectColor(vertices[3], 0.4f * 0.9f, 0.4f * 0.9f, 0.4f * 0.9f);
    ExpectColor(vertices[6], item.red, item.green, item.blue);
    ExpectColor(vertices[9], item.red, item.green, item.blue);
}

// El 0x42 de un ítem excelente, con el color que late (ExcellentColorAt).
TEST(CharacterDrawingTest, TheExcellentPassPulses)
{
    DrawnCharacter character;
    character.appearance = Client::GameLogic::ClassAppearance(0, 0);
    character.appearance.bodyParts[1].excellent = true;
    const OneModel models(character.appearance.bodyParts[1].model);

    ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});
    DrawCharacter(batch, models, character, std::chrono::milliseconds{250});

    const auto vertices = batch.Vertices();
    ASSERT_EQ(vertices.size(), 2u * 3u);
    const Client::Render::Color pulse = Client::Render::Models::ExcellentColorAt(std::chrono::milliseconds{250});
    ExpectColor(vertices[3], pulse.red, pulse.green, pulse.blue);
}

// 0x509340 le pasa a 0x60b3d0 el set en 0: lo que cuelga no lleva el pase de set.
TEST(CharacterDrawingTest, WhatHangsHasNoSetPass)
{
    DrawnCharacter character;
    character.appearance = Client::GameLogic::ClassAppearance(0, 0);
    character.appearance.rightHand = Client::GameLogic::WornItem{.model = 0x212, .ancient = true};
    const OneModel models(0x212);

    const ModelBatch batch = Drawn(models, character);

    EXPECT_EQ(batch.Commands().size(), 1u);
}

// 0x50ba30 dibuja la sombra (0x20b, Shadow01) antes de las partes, con las matrices del cuerpo: negra,
// sin textura, opaca y sin escribir la profundidad (0x60b3d0 con +0x98 en 1 y 0x4eff40), y aplastada
// sobre el piso, en la z del personaje más 5. Su malla es la oculta (hide_m, 0x12d), como en el
// Data\: 0x4eff40 no mira la textura.
TEST(CharacterDrawingTest, TheShadowGoesFirstBlackAndFlat)
{
    DrawnCharacter character;
    character.appearance = Client::GameLogic::ClassAppearance(0, 0);
    character.object.position = {100.0f, 200.0f, 163.0f};
    const OneModel models(Client::Data::Models::PlayerShadowModel, "hide_m.jpg", 1, TextureSlot{0x12d});

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

// La sombra va antes de las partes (0x50ba30 la dibuja antes del lazo de las partes): con las dos,
// el primer tramo es el de la sombra.
TEST(CharacterDrawingTest, TheShadowGoesBeforeTheParts)
{
    DrawnCharacter character;
    character.appearance = Client::GameLogic::ClassAppearance(0, 0);
    const std::uint16_t part = character.appearance.bodyParts[1].model;
    // Una biblioteca con la sombra y una parte, el mismo triángulo.
    class ShadowAndPart final : public IModelLibrary
    {
    public:
        explicit ShadowAndPart(std::uint16_t drawn) : m_body(FlatSkeleton(48)), m_model(OneTriangle()), m_drawn(drawn)
        {
        }
        [[nodiscard]] const Model* Find(std::uint16_t model) const override
        {
            return model == m_drawn || model == Client::Data::Models::PlayerShadowModel ? &m_model : nullptr;
        }
        [[nodiscard]] const Skeleton* SkeletonOf(std::uint16_t model) const override
        {
            return model == 0x20a ? &m_body : nullptr;
        }
        [[nodiscard]] std::span<const TextureSlot> MeshTextures(std::uint16_t /*model*/) const override
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

// Con una de las mascotas que se montan (0x1c14 y 0x1c15), 0x50ba30 no dibuja la sombra.
TEST(CharacterDrawingTest, ARiddenPetHasNoShadow)
{
    for (const std::uint16_t pet : {std::uint16_t{0x1c14}, std::uint16_t{0x1c15}})
    {
        DrawnCharacter character;
        character.appearance = Client::GameLogic::ClassAppearance(0, 0);
        character.appearance.pet = pet;
        const OneModel models(Client::Data::Models::PlayerShadowModel);

        const ModelBatch batch = Drawn(models, character);

        EXPECT_TRUE(batch.Vertices().empty()) << std::hex << pet;
    }
}

// Sin el mouse encima, la parte sola.
TEST(CharacterDrawingTest, WithoutTheMouseThereIsNoOutline)
{
    DrawnCharacter character;
    character.appearance = Client::GameLogic::ClassAppearance(0, 0);
    const OneModel models(character.appearance.bodyParts[1].model);

    const ModelBatch batch = Drawn(models, character);

    ASSERT_EQ(batch.Commands().size(), 1u);
    EXPECT_EQ(batch.Vertices().size(), 3u);
}

// Una malla cuyo triángulo nombra un vértice, una normal o una UV que no tiene (un .bmd roto: el
// original leería fuera) no se dibuja, ni en el contorno; las demás del modelo, sí.
TEST(CharacterDrawingTest, AMeshThatNamesWhatItLacksIsNotDrawn)
{
    DrawnCharacter character;
    character.appearance = Client::GameLogic::ClassAppearance(0, 0);
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
