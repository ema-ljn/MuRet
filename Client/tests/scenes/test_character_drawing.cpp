#include "Scenes/CharacterDrawing.h"

#include "Data/Models/Model.h"
#include "Data/Models/PlayerModels.h"
#include "GameLogic/CharacterAppearance.h"
#include "Render/ModelBatch.h"
#include "Render/ModelVertex.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/HalfLifeMath.h"
#include "Render/Models/OriginalRotation.h"
#include "Render/Models/PassColors.h"
#include "Render/Models/Pose.h"
#include "Render/Models/Skeleton.h"
#include "Render/Models/Skinning.h"
#include "Render/Pass3D.h"
#include "Render/SpriteVertex.h"
#include "Render/TextureSlot.h"
#include "World/ScepterGlow.h"
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
#include <numbers>
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
using Client::Scenes::DrawDarkHorse;
using Client::Scenes::DrawnCharacter;
using Client::Scenes::DrawnObject;
using Client::Scenes::DrawObject;
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
        // Cada malla con la textura de su índice, como todas las del laboratorio.
        for (std::size_t index = 0; index < meshes; ++index)
        {
            m_model.meshes[index].texture = static_cast<std::int16_t>(index);
        }
        m_slots.assign(meshes, slot);
    }

    [[nodiscard]] const Model* Find(std::uint16_t model) const override
    {
        return Has(model) ? &m_model : nullptr;
    }
    [[nodiscard]] const Skeleton* SkeletonOf(std::uint16_t model) const override
    {
        if (model == 0x20a)
        {
            return &m_body;
        }
        return Has(model) ? &m_item : nullptr;
    }
    [[nodiscard]] std::span<const TextureSlot> MeshTextures(std::uint16_t model) const override
    {
        if (model == m_drawn)
        {
            return m_slots;
        }
        const auto other = std::ranges::find(m_others, model, &Other::model);
        return other != m_others.end() ? std::span<const TextureSlot>(other->slots) : std::span<const TextureSlot>{};
    }

    // El mismo modelo también como `model`, con todas sus mallas en `slot`: para ver en qué orden van.
    void AlsoAs(std::uint16_t model, TextureSlot slot)
    {
        m_others.push_back(Other{model, std::vector<TextureSlot>(m_slots.size(), slot)});
    }

    // El esqueleto de los ítems, para darle a su hueso 0 una pose propia.
    Skeleton& ItemSkeleton()
    {
        return m_item;
    }

    // Para armar una malla rota.
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
// (0x1c17): no van en la mano. Las saetas de la derecha y las flechas de la izquierda tampoco van a
// la espalda, y el cuervo es un objeto aparte (CL8).
TEST(CharacterDrawingTest, ArrowsBoltsAndTheRavenAreNotInTheHand)
{
    for (const auto& [right, left] : {std::pair{std::uint16_t{0xa19}, std::uint16_t{0xa21}},
                                      std::pair{std::uint16_t{0x1c17}, std::uint16_t{0x1c17}}})
    {
        DrawnCharacter character;
        character.appearance = Client::GameLogic::ClassAppearance(0, 0);
        character.appearance.rightHand = Client::GameLogic::WornItem{.model = right};
        character.appearance.leftHand = Client::GameLogic::WornItem{.model = left};
        OneModel rightModels(right);
        OneModel leftModels(left);

        EXPECT_TRUE(Drawn(rightModels, character).Vertices().empty()) << std::hex << right;
        EXPECT_TRUE(Drawn(leftModels, character).Vertices().empty()) << std::hex << left;
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

// El dibujo de una parte de +13, con el contorno o sin él, y la textura de su única malla.
ModelBatch DrawnPart(const std::string& texture, bool outlined = false, std::uint8_t level = 13)
{
    DrawnCharacter character;
    character.appearance = Client::GameLogic::ClassAppearance(0, 0);
    character.appearance.bodyParts[1].level = level;
    character.outlined = outlined;
    const OneModel models(character.appearance.bodyParts[1].model, texture);
    return Drawn(models, character);
}

// 0x4ef0c0: una malla con H en el script no se dibuja en ningún pase, ni en el del contorno.
TEST(CharacterDrawingTest, AnHiddenScriptMeshIsNotDrawn)
{
    EXPECT_TRUE(DrawnPart("part_H.jpg").Commands().empty());
    EXPECT_TRUE(DrawnPart("part_H.jpg", true).Commands().empty());
}

// 0x4ef0c0 pasa la malla con R como el blendMesh, y 0x4ecc30 la dibuja con su textura, aditiva y con
// el color por la luz del blendMesh (1 en un jugador, 0x511290); los pases de brillo van igual.
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

// 0x4ecc30 compara el blendMesh con la textura de la malla: con R pero con la textura de otra, va
// como una sin script.
TEST(CharacterDrawingTest, AnRMeshWithTheTextureOfAnotherIsNotTheBlendMesh)
{
    DrawnCharacter character;
    character.appearance = Client::GameLogic::ClassAppearance(0, 0);
    OneModel models(character.appearance.bodyParts[1].model, "part_R.jpg", 2);
    models.MeshAt(1).texture = 0;

    const ModelBatch batch = Drawn(models, character);

    ASSERT_EQ(batch.Commands().size(), 2u);
    EXPECT_EQ(batch.Commands()[0].state.blend, ModelBlend::Additive);
    EXPECT_EQ(batch.Commands()[1].state.blend, ModelBlend::Opaque);
}

// El blendMesh va antes que la mezcla de cuatro componentes: una OZT con R es aditiva.
TEST(CharacterDrawingTest, AnRFourComponentMeshIsAdditiveToo)
{
    const ModelBatch batch = DrawnPart("part_R.tga", false, 0);

    ASSERT_EQ(batch.Commands().size(), 1u);
    EXPECT_EQ(batch.Commands()[0].state, (ModelDrawState{TextureSlot{0x300}, ModelBlend::Additive, true}));
}

// El contorno (0x60cd60 → 0x4ef0c0 con 0x40) también pasa el blendMesh: la malla con R va con su
// textura y el color del contorno por la luz, aunque sea de cuatro componentes.
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

// 0x4ecc30 no corre los pases de brillo de una malla con N; el 0x42 del excelente, sí.
TEST(CharacterDrawingTest, AnNMeshDoesNotShine)
{
    EXPECT_EQ(DrawnPart("part_N.jpg").Commands().size(), 1u);

    DrawnCharacter character;
    character.appearance = Client::GameLogic::ClassAppearance(0, 0);
    character.appearance.bodyParts[1].excellent = true;
    const OneModel models(character.appearance.bodyParts[1].model, "part_N.jpg");
    EXPECT_EQ(Drawn(models, character).Commands().size(), 2u);
}

// La S corre las UV con la U y la V del objeto, que en un jugador son 0 (0x511290): no cambia nada.
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

// 0x60ff50 prende el +0 de la malla 1 de Spear01 (0x812): 0x4ecc30 no le corre los pases de brillo,
// y a la malla 0 sí.
TEST(CharacterDrawingTest, TheMeshesThat0x60ff50MarksDoNotShine)
{
    DrawnCharacter character;
    character.appearance = Client::GameLogic::ClassAppearance(0, 0);
    character.appearance.rightHand = Client::GameLogic::WornItem{.model = 0x812, .level = 13};
    const OneModel models(0x812, "spear.jpg", 2);

    const ModelBatch batch = Drawn(models, character);

    EXPECT_EQ(batch.Vertices().size(), (4u + 1u) * 3u);
}

// Cuántos vértices deja el casco `helm` en un personaje de la clase `characterClass`, con la
// biblioteca que tiene solo `drawn`.
std::size_t HelmVertices(std::uint8_t characterClass, std::uint16_t helm, std::uint16_t drawn)
{
    DrawnCharacter character;
    character.appearance = Client::GameLogic::ClassAppearance(characterClass, 0);
    character.appearance.bodyParts[0].model = helm;
    const OneModel models(drawn);
    return Drawn(models, character).Vertices().size();
}

// 0x50ba30, en la parte del casco de la clase 4 (+0x1d4 & 7): los cascos 0x1012, 0x1017, 0x1018,
// 0x101a y 0x101b van con el modelo + 0x328b, su máscara (MaskHelmMale01, 06, 07, 09 y 10).
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

// Las otras clases llevan el casco, y la clase 4 lleva los demás cascos sin máscara.
TEST(CharacterDrawingTest, OnlyClassFourAndFiveHelmsWearTheMask)
{
    EXPECT_EQ(HelmVertices(0, 0x1012, 0x1012), 3u);
    EXPECT_EQ(HelmVertices(0, 0x1012, 0x1012 + 0x328b), 0u);
    EXPECT_EQ(HelmVertices(3, 0x1012, 0x1012), 3u);
    EXPECT_EQ(HelmVertices(4, 0x1013, 0x1013), 3u);
}

// 0x50ba30 cambia el modelo solo en la parte del casco (+0x218): en otra, el mismo número va como es.
TEST(CharacterDrawingTest, TheMaskIsOnlyForTheHelmPart)
{
    DrawnCharacter character;
    character.appearance = Client::GameLogic::ClassAppearance(4, 0);
    character.appearance.bodyParts[1].model = 0x1012;
    const OneModel models(0x1012);

    EXPECT_EQ(Drawn(models, character).Vertices().size(), 3u);
}

// La parte `part` de la clase `characterClass` con una malla R, de +0 o excelente, a los 250 ms.
ModelBatch DrawnBlendMesh(std::uint8_t characterClass, bool excellent = false)
{
    DrawnCharacter character;
    character.appearance = Client::GameLogic::ClassAppearance(characterClass, 0);
    character.appearance.bodyParts[1].excellent = excellent;
    const OneModel models(character.appearance.bodyParts[1].model, "part_R.jpg");
    ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});
    DrawCharacter(batch, models, character, std::chrono::milliseconds{250});
    return batch;
}

// La luz del blendMesh es 1 (0x511290), y en la clase 4 la que 0x50ba30 pone en la parte del casco:
// sin(0,001 · T) · 0,1 + 0,7.
TEST(CharacterDrawingTest, TheBlendMeshLightOfClassFourPulses)
{
    const float pulse = std::sin(0.25f) * 0.1f + 0.7f;

    ExpectColor(DrawnBlendMesh(0).Vertices()[0], 0.4f, 0.4f, 0.4f);
    ExpectColor(DrawnBlendMesh(4).Vertices()[0], 0.4f * pulse, 0.4f * pulse, 0.4f * pulse);
}

// El 0x42 del blendMesh también va por esa luz.
TEST(CharacterDrawingTest, TheExcellentPassOfTheBlendMeshTakesItsLight)
{
    const float pulse = std::sin(0.25f) * 0.1f + 0.7f;
    const Client::Render::Color excellent = Client::Render::Models::ExcellentColorAt(std::chrono::milliseconds{250});

    const ModelBatch batch = DrawnBlendMesh(4, true);

    ASSERT_EQ(batch.Vertices().size(), 2u * 3u);
    ExpectColor(batch.Vertices()[3], excellent.red * pulse, excellent.green * pulse, excellent.blue * pulse);
}

// El contorno del blendMesh también va por la luz: en la clase 4, el color del contorno por la que late.
TEST(CharacterDrawingTest, TheOutlineOfTheBlendMeshTakesItsLight)
{
    const float pulse = std::sin(0.25f) * 0.1f + 0.7f;
    DrawnCharacter character;
    character.appearance = Client::GameLogic::ClassAppearance(4, 0);
    character.outlined = true;
    const OneModel models(character.appearance.bodyParts[1].model, "part_R.jpg");
    ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});

    DrawCharacter(batch, models, character, std::chrono::milliseconds{250});

    ASSERT_EQ(batch.Vertices().size(), 9u);
    ExpectColor(batch.Vertices()[0], 0.1f * pulse, 0.03f * pulse, 0.0f);
    ExpectColor(batch.Vertices()[3], 0.7f * pulse, 0.2f * pulse, 0.0f);
}

// La parte de la clase `characterClass` con una malla R de +13, a los 250 ms.
ModelBatch DrawnThirteenBlendMesh(std::uint8_t characterClass)
{
    DrawnCharacter character;
    character.appearance = Client::GameLogic::ClassAppearance(characterClass, 0);
    character.appearance.bodyParts[1].level = 13;
    const OneModel models(character.appearance.bodyParts[1].model, "part_R.jpg");
    ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});
    DrawCharacter(batch, models, character, std::chrono::milliseconds{250});
    return batch;
}

// Los pases de brillo salen de 0x4ecc30 antes de la rama del blendMesh: el 0x1040 va con el color
// base por el factor del ítem (0x6093d0), sin la luz, como el 0x48 y el 0x44.
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

// Un personaje de la clase `characterClass` con las partes de `models` en `parts` (la del índice 2,
// el pantalón, con el modelo `pants`), cada una con una malla R en su slot, a los 1000 ms.
ModelBatch DrawnLights(std::uint8_t characterClass, std::uint16_t pants, std::uint16_t helm = 0)
{
    DrawnCharacter character;
    character.appearance = Client::GameLogic::ClassAppearance(characterClass, 0);
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

// El rojo de la malla R de la parte con el slot 0x300 + `part`, que es 0,4 por la luz.
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

// 0x60b3d0 escribe la luz del objeto con el modelo: PantMale25 (0x142a) la pone en la de sus
// FShoots_R, sin(0,001 · T) · 0,4 + 0,6, y queda en las partes que siguen (la armadura y el casco) y
// en el frame que sigue (las botas, que van antes).
TEST(CharacterDrawingTest, PantMale25LightsTheCharacterWithItsShoots)
{
    const float shoots = std::sin(1.0f) * 0.4f + 0.6f;

    const ModelBatch batch = DrawnLights(0, 0x142a);

    for (const std::size_t part : {std::size_t{0}, std::size_t{1}, std::size_t{2}, std::size_t{4}})
    {
        EXPECT_NEAR(LightOfPart(batch, part), shoots, 1e-6f) << part;
    }
}

// En la clase 4, 0x50ba30 escribe la suya en el casco, que va después del pantalón: el casco y lo que
// sigue la ven, y la armadura, entre los dos, la de PantMale25.
TEST(CharacterDrawingTest, ClassFourLightsFromTheHelmOn)
{
    const float shoots = std::sin(1.0f) * 0.4f + 0.6f;
    const float pulse = std::sin(1.0f) * 0.1f + 0.7f;

    const ModelBatch batch = DrawnLights(4, 0x142a);

    EXPECT_NEAR(LightOfPart(batch, 4), pulse, 1e-6f);
    EXPECT_NEAR(LightOfPart(batch, 2), shoots, 1e-6f);
    EXPECT_NEAR(LightOfPart(batch, 1), shoots, 1e-6f);
    EXPECT_NEAR(LightOfPart(batch, 0), pulse, 1e-6f);
}

// HelmMaleTest20 (0x1025) la vuelve a 1 después de la de la clase 4, en el mismo casco.
TEST(CharacterDrawingTest, TheTest20PartsPutTheLightBackToOne)
{
    const ModelBatch batch = DrawnLights(4, 0x1425, 0x1025);

    for (const std::size_t part : {std::size_t{0}, std::size_t{1}, std::size_t{2}, std::size_t{4}})
    {
        EXPECT_FLOAT_EQ(LightOfPart(batch, part), 1.0f) << part;
    }
}

// 0x60cf70 dibuja el contorno antes de llamar a 0x60b3d0: el de PantMale25 va con la luz que había
// (en la clase 4, la del casco del frame anterior) y la parte, con la de sus FShoots_R.
TEST(CharacterDrawingTest, TheOutlineGoesWithTheLightFromBeforeThePart)
{
    const float shoots = std::sin(1.0f) * 0.4f + 0.6f;
    const float pulse = std::sin(1.0f) * 0.1f + 0.7f;
    DrawnCharacter character;
    character.appearance = Client::GameLogic::ClassAppearance(4, 0);
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

// El arma `item` de nivel `level` en la mano derecha de la clase `characterClass`, con `meshes` mallas
// de textura `texture`, a los `milliseconds`.
ModelBatch DrawnInTheHand(std::uint16_t item, std::size_t meshes, std::uint8_t level = 0, int milliseconds = 1000,
                          const std::string& texture = "item.jpg", std::uint8_t characterClass = 0)
{
    DrawnCharacter character;
    character.appearance = Client::GameLogic::ClassAppearance(characterClass, 0);
    character.appearance.rightHand = Client::GameLogic::WornItem{.model = item, .level = level};
    const OneModel models(item, texture, meshes);
    ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});
    DrawCharacter(batch, models, character, std::chrono::milliseconds{milliseconds});
    return batch;
}

// Lo que cuelga lleva el objeto de 0x607b60: Bow07 (0xa18) tiene el blendMesh en −2, y 0x4ecc30
// pasa todas sus mallas por esa rama, aditivas y por su luz, sin(0,004 · T) · 0,3 + 0,7.
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

// Sword06 (0x217) tiene el blendMesh en 1: la malla con la textura 1 va por esa rama, la otra no.
TEST(CharacterDrawingTest, AnItemBlendsTheMeshWithTheTextureOfItsObject)
{
    const ModelBatch batch = DrawnInTheHand(0x217, 2);

    ASSERT_EQ(batch.Commands().size(), 2u);
    EXPECT_EQ(batch.Commands()[0].state.blend, ModelBlend::Opaque);
    EXPECT_EQ(batch.Commands()[1].state.blend, ModelBlend::Additive);
}

// La luz de lo que cuelga es la de su objeto, no la del personaje: una malla R en la mano de la clase
// 4 va por 1.
TEST(CharacterDrawingTest, WhatHangsTakesTheLightOfItsObject)
{
    const ModelBatch batch = DrawnInTheHand(0x212, 1, 0, 250, "item_R.jpg", 4);

    ASSERT_EQ(batch.Commands().size(), 1u);
    EXPECT_EQ(batch.Commands()[0].state.blend, ModelBlend::Additive);
    ExpectColor(batch.Vertices()[0], 0.4f, 0.4f, 0.4f);
}

// 0x609640 le pasa al pase base el +0x58 del objeto como la malla oculta: la 1 de Spear11 (0x81c) y la
// 2 de Sword32 (0x231) no van.
TEST(CharacterDrawingTest, TheBaseSkipsTheHiddenMeshOfTheObject)
{
    EXPECT_EQ(DrawnInTheHand(0x81c, 2).Vertices().size(), 1u * 3u);
    EXPECT_EQ(DrawnInTheHand(0x231, 3).Vertices().size(), 2u * 3u);
    EXPECT_EQ(DrawnInTheHand(0x212, 3).Vertices().size(), 3u * 3u);
}

// 0x60b3d0 escribe la luz de Wing05 (0x1a16) en sin(0,001 · T) + 1,1: su malla R va por ella.
TEST(CharacterDrawingTest, TheLightOfWing05Pulses)
{
    DrawnCharacter character;
    character.appearance = Client::GameLogic::ClassAppearance(0, 0);
    character.appearance.wings = std::uint16_t{0x1a16};
    const OneModel models(0x1a16, "wing_R.jpg");
    ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});

    DrawCharacter(batch, models, character, std::chrono::milliseconds{1000});

    ASSERT_EQ(batch.Vertices().size(), 3u);
    EXPECT_NEAR(batch.Vertices()[0].color.red, 0.4f * (std::sin(1.0f) + 1.1f), 1e-6f);
}

// 0x509340 sale sin dibujar con el modelo 0x1c30 (0x509363: CMP EDI,0x1c30 y JZ al final): la capa
// del Dark Lord, DarkLordRobe.bmd, un plano del inventario, no cuelga del hueso 47. Lo que se ve es la
// tela de 0x50ba30.
TEST(CharacterDrawingTest, TheLordsCapeDoesNotHangAsAModel)
{
    DrawnCharacter character;
    character.appearance = Client::GameLogic::ClassAppearance(4, 0);
    character.appearance.wings = std::uint16_t{0x1c30};
    const OneModel models(0x1c30, "DarkLordRobe.jpg");
    ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});

    DrawCharacter(batch, models, character, std::chrono::milliseconds{1000});

    EXPECT_TRUE(batch.Vertices().empty());
}

// Los vértices del arma `item` de +13 en la mano derecha, con `meshes` mallas.
std::size_t HandVertices(std::uint16_t item, std::size_t meshes)
{
    DrawnCharacter character;
    character.appearance = Client::GameLogic::ClassAppearance(0, 0);
    character.appearance.rightHand = Client::GameLogic::WornItem{.model = item, .level = 13};
    const OneModel models(item, "item.jpg", meshes);
    return Drawn(models, character).Vertices().size();
}

// En los pases de 0x60b130 y 0x60b300, RenderBody saltea la malla oculta de algunos ítems: la 0 de
// 0x227 en los tres pases del +13, y la 2 de 0x619 en el 0x48 y el 0x44, no en el 0x1040. 0x619 la
// oculta también en el pase base, con el +0x58 de su objeto.
TEST(CharacterDrawingTest, SomeItemsHideAMeshInTheirPasses)
{
    EXPECT_EQ(HandVertices(0x227, 2), (1u + 4u) * 3u);
    EXPECT_EQ(HandVertices(0x619, 3), (4u + 4u + 1u) * 3u);
    EXPECT_EQ(HandVertices(0x212, 2), (4u + 4u) * 3u);
}

// 0x60b130 les pasa el +0x58 del objeto a 0x231, 0x81c y 0xe22 (y a 0x61a..0x61f, en −1): la malla
// que su objeto oculta no va en el 0x48 ni en el 0x44, sí en el 0x1040.
TEST(CharacterDrawingTest, SomeItemsHideTheMeshOfTheirObjectInTheirPasses)
{
    EXPECT_EQ(HandVertices(0x81c, 2), (4u + 1u) * 3u);
    EXPECT_EQ(HandVertices(0x231, 3), (4u + 4u + 1u) * 3u);
    EXPECT_EQ(HandVertices(0xe22, 3), (4u + 4u + 1u) * 3u);
    EXPECT_EQ(HandVertices(0x61a, 3), (4u + 4u + 4u) * 3u);
}

// 0x60b3d0 con el objeto de Spear10 (0x81b, lo que cuelga lleva el tipo de su ítem): antes, la malla
// con el 0x5c0 forzado y el color (0,5; 0,5; 1,5), opaca; después, los pases de siempre.
TEST(CharacterDrawingTest, Spear10DrawsItsChromeFirst)
{
    DrawnCharacter character;
    character.appearance = Client::GameLogic::ClassAppearance(0, 0);
    character.appearance.rightHand = Client::GameLogic::WornItem{.model = 0x81b};
    const OneModel models(0x81b);

    const ModelBatch batch = Drawn(models, character);

    ASSERT_EQ(batch.Commands().size(), 2u);
    EXPECT_EQ(batch.Commands()[0].state, (ModelDrawState{TextureSlot{0x5c0}, ModelBlend::Opaque, true}));
    EXPECT_EQ(batch.Commands()[1].state, (ModelDrawState{TextureSlot{0x300}, ModelBlend::Opaque, true}));
    ExpectColor(batch.Vertices()[0], 0.5f, 0.5f, 1.5f);
    ExpectColor(batch.Vertices()[3], 0.4f, 0.4f, 0.4f);
}

// El 0x81b es del objeto: una parte, cuyo objeto es el personaje (0x20a), no lo lleva.
TEST(CharacterDrawingTest, OnlyTheHanging0x81bDrawsTheChrome)
{
    DrawnCharacter character;
    character.appearance = Client::GameLogic::ClassAppearance(0, 0);
    character.appearance.bodyParts[1].model = 0x81b;
    const OneModel models(0x81b);

    EXPECT_EQ(Drawn(models, character).Commands().size(), 1u);
}

// 0x607b60 le da a Spear10 la V (T % 2000) · −0,0005, y 0x60b3d0 pone el model+0x88 en 0 para el
// pase del chrome: 0x4ecc30 le corre las UV a esa malla. Su malla, una OZT, va después con alpha y sin
// correr (su blendMesh es −1).
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

// 0x4ecc30 suma la U y la V del objeto a las UV del pase 0x1040 (el modo 0x1000), no a las del 0x48
// ni el 0x44: las de Spear10 de +13, contra las de un ítem que no las tiene.
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

// Staff06 (0xc17) tiene el blendMesh en 2 y una U y una V de rand() % 10 · 0,1 en cada frame: 0x4ecc30
// le corre las UV a la malla de índice 2, y no a las otras, y de un frame al otro cambian.
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

// El set de una parte de `meshes` mallas, con la textura `texture` en la malla 1, a los 250 ms.
ModelBatch DrawnSet(std::size_t meshes, const std::string& second = "part.jpg")
{
    DrawnCharacter character;
    character.appearance = Client::GameLogic::ClassAppearance(0, 0);
    character.appearance.bodyParts[1].ancient = true;
    OneModel models(character.appearance.bodyParts[1].model, "part.jpg", meshes);
    models.MeshAt(1).textureName = second;
    ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});
    DrawCharacter(batch, models, character, std::chrono::milliseconds{250});
    return batch;
}

// 0x60b3d0 llama al pase de set con un alpha que late (SetAlphaAt), y 0x4ecc30 multiplica por él en
// el lugar el color del modelo en cada malla que lo dibuja: la primera va por a, la segunda por a²
// (Q-C95).
TEST(CharacterDrawingTest, TheSetPassFadesMeshByMesh)
{
    const ModelBatch batch = DrawnSet(2);
    const float alpha = std::sin(0.25f) * 0.5f + 0.4f;
    const Client::Render::Color set =
        Client::Render::Models::SetColorOf(Client::GameLogic::ClassAppearance(0, 0).bodyParts[1].model);

    const auto vertices = batch.Vertices();
    ASSERT_EQ(vertices.size(), 4u * 3u);
    ExpectColor(vertices[0], 0.4f, 0.4f, 0.4f);
    ExpectColor(vertices[3], 0.4f, 0.4f, 0.4f);
    ExpectColor(vertices[6], set.red * alpha, set.green * alpha, set.blue * alpha);
    ExpectColor(vertices[9], set.red * alpha * alpha, set.green * alpha * alpha, set.blue * alpha * alpha);
}

// Una malla que no brilla sale de 0x4ecc30 antes de multiplicar: no cuenta para las que siguen.
TEST(CharacterDrawingTest, AMeshWithoutShineDoesNotFadeTheSetPass)
{
    const ModelBatch batch = DrawnSet(3, "part_N.jpg");
    const float alpha = std::sin(0.25f) * 0.5f + 0.4f;
    const Client::Render::Color set =
        Client::Render::Models::SetColorOf(Client::GameLogic::ClassAppearance(0, 0).bodyParts[1].model);

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

// El carcaj `quiver` en la mano izquierda o en la derecha, de nivel `level`.
ModelBatch DrawnQuiver(std::uint16_t quiver, bool left, std::uint8_t level = 0, bool excellent = false)
{
    DrawnCharacter character;
    character.appearance = Client::GameLogic::ClassAppearance(0, 0);
    const Client::GameLogic::WornItem item{.model = quiver, .level = level, .excellent = excellent, .ancient = true};
    (left ? character.appearance.leftHand : character.appearance.rightHand) = item;
    const OneModel models(quiver);
    return Drawn(models, character);
}

// 0x50ba30 con las armas en la mano: las saetas de la izquierda y las flechas de la derecha van a la
// espalda, del hueso de las alas (47) por AngleMatrix(70, 0, 90) y (−10, 5, 10) (0x509340 con su
// bandera). Con el hueso en el origen, el triángulo (0, 0, 0), (1, 0, 0), (0, 1, 0) cae así.
TEST(CharacterDrawingTest, TheBoltsAndTheArrowsGoOnTheBack)
{
    const float seventy = 70.0f * std::numbers::pi_v<float> / 180.0f;
    for (const auto& [quiver, left] : {std::pair{std::uint16_t{0xa19}, true}, std::pair{std::uint16_t{0xa21}, false}})
    {
        const ModelBatch batch = DrawnQuiver(quiver, left);

        ASSERT_EQ(batch.Vertices().size(), 3u) << std::hex << quiver;
        ExpectAt(batch.Vertices()[0], -10.0f, 5.0f, 10.0f);
        ExpectAt(batch.Vertices()[1], -10.0f, 6.0f, 10.0f);
        ExpectAt(batch.Vertices()[2], -10.0f - std::cos(seventy), 5.0f, 10.0f + std::sin(seventy));
    }
}

// Con saetas en la izquierda y flechas en la derecha, van las saetas (las flechas son el else de
// 0x50ba30): sin el modelo de las saetas, nada.
TEST(CharacterDrawingTest, TheBoltsComeBeforeTheArrows)
{
    DrawnCharacter character;
    character.appearance = Client::GameLogic::ClassAppearance(0, 0);
    character.appearance.leftHand = Client::GameLogic::WornItem{.model = 0xa19};
    character.appearance.rightHand = Client::GameLogic::WornItem{.model = 0xa21};
    const OneModel models(0xa21);

    EXPECT_TRUE(Drawn(models, character).Vertices().empty());
}

// 0x60b3d0 le da al carcaj 2 · nivel + 1: unas saetas +3 van con el 0x44 del +7. El excelente de la
// mano va; el set, no (0x509340 le pasa 0).
TEST(CharacterDrawingTest, TheQuiverGlowsAtTwiceItsLevelPlusOne)
{
    EXPECT_EQ(DrawnQuiver(0xa19, true, 3).Vertices().size(), 2u * 3u);
    EXPECT_EQ(DrawnQuiver(0xa19, true, 0).Vertices().size(), 1u * 3u);
    EXPECT_EQ(DrawnQuiver(0xa21, false, 0, true).Vertices().size(), 2u * 3u);
}
// 0x50ba30 dibuja las partes de la 5 a la 0: las botas, los guantes, el pantalón, la armadura, el casco
// y la cabeza; después la espalda, las alas y la mascota, y al final las manos (0x50e6af).
TEST(CharacterDrawingTest, ThePartsGoFromTheBootsToTheHeadAndTheHandsLast)
{
    DrawnCharacter character;
    character.appearance = Client::GameLogic::ClassAppearance(0, 0);
    character.appearance.rightHand = Client::GameLogic::WornItem{.model = 0x212};
    character.appearance.leftHand = Client::GameLogic::WornItem{.model = 0xa19};
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

// RenderBody hace un pase sobre todas las mallas antes del siguiente: las bases de las dos mallas y
// después cada pase de brillo en las dos.
TEST(CharacterDrawingTest, AModelGoesPassByPass)
{
    const ModelBatch batch = DrawnInTheHand(0x212, 2, 7);

    ASSERT_EQ(batch.Commands().size(), 2u);
    EXPECT_EQ(batch.Commands()[0].vertexCount, 2u * 3u);
    EXPECT_EQ(batch.Commands()[0].state.texture, TextureSlot{0x300});
    EXPECT_EQ(batch.Commands()[1].vertexCount, 2u * 3u);
    EXPECT_NE(batch.Commands()[1].state.texture, TextureSlot{0x300});
}
// La luz del personaje va por canal: la del login es la del terreno, que tiene color (0x50ba30 con
// 0x5f99e0 fuera de la escena 4).
TEST(CharacterDrawingTest, TheLightOfACharacterGoesByChannel)
{
    DrawnCharacter character;
    character.appearance = Client::GameLogic::ClassAppearance(0, 0);
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

// Un objeto del mundo sin casos por tipo (0x6002e0): RenderBody(2) y nada más. Cada malla con el color
// del modelo (0x5ffce0), y la del blendMesh aditiva y por la luz del objeto; posado con su escala y su
// posición (0x5ffdf0).
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

// La raíz gira con los ángulos del objeto (0x5ffdf0 → 0x4eb860 con +0x1c): con 90° en z, el vértice
// (1, 0, 0) va a (0, 2, 0) de su lugar.
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

// La malla que el objeto oculta (+0x58) no va, y las demás sí.
TEST(CharacterDrawingTest, AnObjectSkipsItsHiddenMesh)
{
    const OneModel models(0xa2, "logo.jpg", 2);
    DrawnObject object = LogoLike();
    object.state.hiddenMesh = 0;

    const ModelBatch batch = DrawnObjectOf(models, object);

    ASSERT_EQ(batch.Commands().size(), 1u);
    EXPECT_EQ(batch.Commands()[0].state.blend, ModelBlend::Additive);
}

// El script de la textura vale también en un objeto (0x4ef0c0): con H, la malla no va.
TEST(CharacterDrawingTest, AnObjectFollowsTheScriptOfItsTextures)
{
    OneModel models(0xa2, "logo.jpg", 2);
    models.FirstMesh().textureName = "logo_H.jpg";

    const ModelBatch batch = DrawnObjectOf(models, LogoLike());

    ASSERT_EQ(batch.Commands().size(), 1u);
    EXPECT_EQ(batch.Commands()[0].state.blend, ModelBlend::Additive);
}

// Un objeto con luz por vértice (+0xe8, que 0x5ffce0 copia a +0x44 del modelo): 0x4ecc30 le da a cada
// esquina el color del modelo por la luz de su normal (0x4ebc80), por el índice de normal de la esquina
// y no el de su vértice. La de x da 1,44; la de −z, el piso de 0,2; la de y, 0,4.
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
    // Sin ella, el color del modelo en las tres.
    DrawnObject unlit = LitObject();
    unlit.vertexLight = false;
    const ModelBatch flat = DrawnObjectOf(models, unlit);
    ASSERT_EQ(flat.Vertices().size(), 3u);
    ExpectColor(flat.Vertices()[0], 0.5f, 0.5f, 0.5f);
}

// Un objeto de 0x5ffdf0 lleva la escala en la raíz (0x4eb860 con Translate), y 0x4ebc80 gira cada
// normal con esa matriz sin normalizarla: con la escala 2, la normal en z mide 2 y da 0,8 · 2 · 2 + 0,4.
TEST(CharacterDrawingTest, TheNormalsOfAnObjectCarryItsScale)
{
    const OneModel models(0xbf, "dk.jpg", 1);
    DrawnObject object = LitObject();
    object.transform.scale = 2.0f;

    const ModelBatch batch = DrawnObjectOf(models, object);

    ASSERT_EQ(batch.Vertices().size(), 3u);
    ExpectColor(batch.Vertices()[0], 0.5f * 3.6f, 0.5f * 3.6f, 0.5f * 3.6f);
}

// La malla del blendMesh va por su rama, con glColor3f(color · luz del blendMesh), sin la de la normal.
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

// Con el alpha del objeto (+0x174) por debajo de 0,99, 0x4ecc30 mezcla (0x620a00(1)) y lo manda en el
// glColor4f; desde 0,99, va opaca y en 1.
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

// 0x5ffdf0 no dibuja un objeto con el alpha por debajo de 0,01 (0x6b88d4).
TEST(CharacterDrawingTest, AnObjectBelowAHundredthOfAlphaIsNotDrawn)
{
    const OneModel models(0xbf, "dk.jpg", 1);
    DrawnObject object = LitObject();
    object.alpha = 0.0099f;
    EXPECT_TRUE(DrawnObjectOf(models, object).Commands().empty());
    object.alpha = 0.01f;
    EXPECT_FALSE(DrawnObjectOf(models, object).Commands().empty());
}

// El caballo oscuro (0x6002e0 con el 0xbf), con 13 mallas de un triángulo y la normal en z (la luz
// por vértice da 0,4): el pase base en blanco por esa luz, después el cromo aditivo de la malla 12 en
// (1; 0,8; 0,3) y la sombra de la malla 8, mezclada, sin textura, en (0, 0, 0, 0,7) y aplastada sobre
// el piso.
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

// Mientras aparece, el pase base mezcla con su alpha y el cromo va por él (0x4ecc30 multiplica el
// color en el lugar); la sombra sigue en 0,7.
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

// 0x60073c..0x600787: la sombra depende del mundo en curso. En Lorencia (0) y Devias (2), y en el login
// (55), en 0,7; en el 7, en 0,2; en el 10, en Kalima (24 a 29) y en el 36, no hay. El cuerpo y el cromo
// van en todos.
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

// El cromo de 0x4ecc30 con 0x4 (Chrome): u = 0,5 · nz + t y v = 0,5 · ny + 2t, con la normal con la
// escala del caballo: con 1,6 y el reloj en 0, la normal en z da u = 0,8.
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

// 0x6002e0 pide el cromo de las mallas 12 a 15, y cada una que existe multiplica el color en el lugar
// por el alpha (Q-C95): con 16 mallas y 0,5, van por 0,5, 0,25, 0,125 y 0,0625.
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
        const float factor = std::pow(0.5f, static_cast<float>(mesh + 1));
        ExpectColor(batch.Vertices()[first + (mesh * 3)], factor, 0.8f * factor, 0.3f * factor);
    }
}

// El cromo va por 0x4ecc30, que no corre los pases de brillo de una malla con N en el script.
TEST(CharacterDrawingTest, TheHorseChromeSkipsAMeshWithoutShine)
{
    OneModel models(0xbf, "dk.jpg", 13);
    models.MeshAt(12).textureName = "dkneck2_N.jpg";

    const ModelBatch batch = DrawnHorseOf(models, HorseLike());

    ASSERT_EQ(batch.Commands().size(), 2u);
    EXPECT_EQ(batch.Commands()[1].state.blend, ModelBlend::BlendedShadow);
}

// Sin la malla 12 no hay cromo, y sin la 8 no hay sombra; con el alpha por debajo de 0,01, nada.
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

// Un objeto cuyo modelo no está no se dibuja.
TEST(CharacterDrawingTest, AnObjectWithoutItsModelIsNotDrawn)
{
    const OneModel models(0xa3, "logo.jpg", 2);

    EXPECT_TRUE(DrawnObjectOf(models, LogoLike()).Commands().empty());
}

// 0x509340, caso 0x620: con el HDK_Mace en la mano y la tabla del frame, los cinco sprites del cetro en
// el hueso 0 de la pose de la maza (con su propia posición y su giro, colgado del cuerpo girado por el
// personaje), con su escala y su posición y el rand() de la tela: los mismos que CreateScepterGlow con
// ese hueso, que no es la raíz de la que cuelga.
TEST(CharacterDrawingTest, TheScepterAsksForItsFiveSprites)
{
    DrawnCharacter character;
    character.appearance = Client::GameLogic::ClassAppearance(0, 0);
    character.appearance.rightHand = Client::GameLogic::WornItem{.model = Client::World::HdkMaceModel};
    character.object.position = {1000.0f, 2000.0f, 300.0f};
    character.object.angles = {0.0f, 0.0f, 90.0f};
    character.object.scale = 1.5f;
    Client::Render::Models::ClothWorld world{.random = Client::Render::Models::CrtRandom(7)};
    Client::World::FrameSprites sprites;
    character.clothWorld = &world;
    character.sprites = &sprites;
    OneModel models(Client::World::HdkMaceModel);
    BoneKey& key = models.ItemSkeleton().bones[0].actions[0][0];
    key.position = {5.0f, 10.0f, 20.0f};
    key.rotation = Client::Render::Models::AngleQuaternion({0.3f, 0.0f, 0.0f});

    (void)Drawn(models, character);

    // La raíz es el hueso 33 del cuerpo plano: el giro del personaje, sin traslación.
    const Client::Render::Models::BoneTransform root = Client::Render::Models::AngleMatrix({0.0f, 0.0f, 90.0f});
    std::vector<Client::Render::Models::BoneTransform> mace(1);
    ASSERT_TRUE(Client::Render::Models::PoseAttached(models.ItemSkeleton(), Client::Render::Models::AnimationState{},
                                                     root, mace));
    ASSERT_NE(mace.front(), root);
    Client::World::FrameSprites expected;
    Client::Render::Models::CrtRandom random(7);
    Client::World::CreateScepterGlow(expected, Client::Render::Models::OriginalMatrixOf(mace.front()), 1.5f,
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

// Con el sorteo del cetro (World::ScepterGlowCarry), dos frames de MuRet en el mismo frame del original
// piden los mismos cinco sprites y sacan los seis rand() una sola vez.
TEST(CharacterDrawingTest, TheScepterDrawsOncePerOriginalFrameWithItsCarry)
{
    DrawnCharacter character;
    character.appearance = Client::GameLogic::ClassAppearance(0, 0);
    character.appearance.rightHand = Client::GameLogic::WornItem{.model = Client::World::HdkMaceModel};
    Client::Render::Models::ClothWorld world{.random = Client::Render::Models::CrtRandom(7)};
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

// En la izquierda también (0x509340 no mira la mano); sin la tabla, o sin el rand() de la tela, no pide
// ninguno ni saca rand(); y otro ítem no tiene ese caso.
TEST(CharacterDrawingTest, OnlyTheScepterWithTheTableAsksForSprites)
{
    const OneModel mace(Client::World::HdkMaceModel);
    {
        DrawnCharacter character;
        character.appearance = Client::GameLogic::ClassAppearance(0, 0);
        character.appearance.leftHand = Client::GameLogic::WornItem{.model = Client::World::HdkMaceModel};
        Client::Render::Models::ClothWorld world;
        Client::World::FrameSprites sprites;
        character.clothWorld = &world;
        character.sprites = &sprites;
        (void)Drawn(mace, character);
        EXPECT_TRUE(sprites.Sprites()[Client::World::ScepterSpriteCount - 1].alive) << "la izquierda";
    }
    {
        DrawnCharacter character;
        character.appearance = Client::GameLogic::ClassAppearance(0, 0);
        character.appearance.rightHand = Client::GameLogic::WornItem{.model = Client::World::HdkMaceModel};
        Client::Render::Models::ClothWorld world;
        character.clothWorld = &world;
        (void)Drawn(mace, character);
        EXPECT_EQ(world.random.State(), Client::Render::Models::CrtRandom().State()) << "sin la tabla";
        Client::World::FrameSprites sprites;
        character.clothWorld = nullptr;
        character.sprites = &sprites;
        (void)Drawn(mace, character);
        EXPECT_FALSE(sprites.Any()) << "sin el rand()";
    }
    {
        const OneModel sword(0x212);
        DrawnCharacter character;
        character.appearance = Client::GameLogic::ClassAppearance(0, 0);
        character.appearance.rightHand = Client::GameLogic::WornItem{.model = 0x212};
        Client::Render::Models::ClothWorld world;
        Client::World::FrameSprites sprites;
        character.clothWorld = &world;
        character.sprites = &sprites;
        (void)Drawn(sword, character);
        EXPECT_FALSE(sprites.Any()) << "otro ítem";
    }
}

// El punto de un hueso de un objeto, posado como en DrawObject y pasado al mundo como sus vértices: con
// el hueso 0 plano, el desplazamiento por la escala más la posición; un hueso que el modelo no tiene, uno
// dummy (0x4eb973 no lo escribe en el buffer global) o un modelo que no está, nada.
TEST(CharacterDrawingTest, ThePointOfABoneGoesToTheWorldLikeTheVertices)
{
    OneModel models(0xa3, "logo.jpg");
    DrawnObject object;
    object.model = 0xa3;
    object.transform.position = {100.0f, 200.0f, 300.0f};
    object.transform.scale = 2.0f;
    const auto point = Client::Scenes::PosedBonePoint(models, object, 0, {50.0f, -4.0f, 0.0f});
    ASSERT_TRUE(point.has_value());
    EXPECT_EQ(*point, glm::vec3(200.0f, 192.0f, 300.0f));
    EXPECT_FALSE(Client::Scenes::PosedBonePoint(models, object, 1, glm::vec3(0.0f)).has_value());
    object.model = 0xa4;
    EXPECT_FALSE(Client::Scenes::PosedBonePoint(models, object, 0, glm::vec3(0.0f)).has_value());
    object.model = 0xa3;
    models.ItemSkeleton().bones[0].dummy = true;
    EXPECT_FALSE(Client::Scenes::PosedBonePoint(models, object, 0, glm::vec3(0.0f)).has_value()) << "dummy";
}

// Con un hueso que se traslada y gira, y el objeto girado, a escala y en su lugar, el punto de
// PosedBonePoint es el de un vértice en `offset` colgado de ese hueso, pasado al mundo por SkinMesh: el
// desplazamiento va por el hueso antes que por la escala.
TEST(CharacterDrawingTest, ThePointOfABoneIsWhereItsVertexWouldBe)
{
    OneModel models(0xa3, "logo.jpg");
    BoneKey& key = models.ItemSkeleton().bones[0].actions[0][0];
    key.position = {12.0f, -7.0f, 30.0f};
    key.rotation = Client::Render::Models::AngleQuaternion({0.2f, -0.4f, 0.9f});
    DrawnObject object;
    object.model = 0xa3;
    object.transform.position = {11000.0f, 14000.0f, 300.0f};
    object.transform.angles = {0.0f, 0.0f, 120.0f};
    object.transform.scale = 1.6f;
    const glm::vec3 offset{50.0f, -4.0f, 0.0f};

    std::vector<Client::Render::Models::BoneTransform> bones(1);
    ASSERT_TRUE(Client::Render::Models::PoseSkeleton(models.ItemSkeleton(), object.animation, object.transform,
                                                     glm::vec2(0.0f), bones));
    Mesh mesh;
    mesh.vertices = {{0, {offset.x, offset.y, offset.z}}};
    Client::Render::Models::SkinnedMesh skinned;
    ASSERT_TRUE(Client::Render::Models::SkinMesh(mesh, bones, object.transform, skinned));

    const auto point = Client::Scenes::PosedBonePoint(models, object, 0, offset);
    ASSERT_TRUE(point.has_value());
    EXPECT_EQ(*point, skinned.positions.front());
}

// El buffer global de la pose lo deja el último modelo posado que tiene el hueso sin ser dummy: con dos,
// gana el segundo; si el del segundo es dummy, queda el del primero; sin ninguno, nada.
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
    const std::array<Client::Scenes::PosedInTheBuffer, 2> posed{{{&first, a}, {&second, b}}};
    const auto point = Client::Scenes::LastPosedBonePoint(posed, 0, glm::vec3(0.0f));
    ASSERT_TRUE(point.has_value());
    EXPECT_EQ(point->x, 200.0f);
    second.ItemSkeleton().bones[0].dummy = true;
    const auto underneath = Client::Scenes::LastPosedBonePoint(posed, 0, glm::vec3(0.0f));
    ASSERT_TRUE(underneath.has_value());
    EXPECT_EQ(underneath->x, 100.0f);
    first.ItemSkeleton().bones[0].dummy = true;
    EXPECT_FALSE(Client::Scenes::LastPosedBonePoint(posed, 0, glm::vec3(0.0f)).has_value());
    EXPECT_FALSE(Client::Scenes::LastPosedBonePoint({}, 0, glm::vec3(0.0f)).has_value());
}
} // namespace
