#include "Actors/Drawing/ItemDrawing.h"

#include "Core/Expected.h"
#include "Data/Models/Model.h"
#include "Data/Models/ModelTextures.h"
#include "Render/ModelBatch.h"
#include "Math/HalfLifeMath.h"
#include "Models/IModelLibrary.h"
#include "Math/RoundedMath.h"
#include "Models/Skeleton.h"
#include "Render/Pass3D.h"
#include "Render/SpriteVertex.h"
#include "Render/TextureSlot.h"
#include "Actors/Drawing/CharacterDrawing.h"
#include "Actors/Drawing/GameItemSpecialDrawing.h"

#include <gtest/gtest.h>

#include <glm/ext/vector_float3.hpp>

#include <array>
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace
{
using Client::Actors::Drawing::ItemPart;
using Client::Actors::Drawing::ItemPartOwner;
using Client::Actors::Drawing::ObjectOutlinePass;
using Client::Actors::Drawing::SkinIndexOf;
using Client::Actors::Drawing::WornParts;
using Client::Math::BoneTransform;
using Client::Render::Color;
using Client::Render::TextureSlot;

constexpr std::uint16_t Head = 0x1ec;
constexpr std::uint16_t Helm = 0x1013;
constexpr std::uint16_t MaskedHelm = 0x1017;
constexpr std::uint16_t Mask = 0x42a2;
constexpr std::uint16_t Armor = 0x1213;
constexpr std::uint16_t Pants = 0x1413;
constexpr std::uint16_t Gloves = 0x1613;
constexpr std::uint16_t Boots = 0x1813;
constexpr std::array<std::uint16_t, 8> Models{Boots, Gloves, Pants, Armor, Helm, Head, MaskedHelm, Mask};

constexpr auto Clock = std::chrono::milliseconds(1571);

// The x of the first vertex of `model`'s triangle.
float X(std::uint16_t model)
{
    for (std::size_t index = 0; index < Models.size(); ++index)
        if (Models[index] == model)
            return 10.0f * static_cast<float>(index + 1);
    return 0.0f;
}

// One triangle per model of `Models`, at x = X(model), on the texture slot given to it.
class PartLibrary final : public Client::Models::IModelLibrary
{
public:
    void Add(std::uint16_t id, TextureSlot slot = TextureSlot{0x400})
    {
        Client::Data::Models::Mesh mesh;
        const float x = X(id);
        mesh.vertices = {{0, {x, 0, 0}}, {0, {x + 1, 0, 0}}, {0, {x, 1, 0}}};
        mesh.normals = {{0, {0.3f, -0.5f, 0.8f}, 0}};
        mesh.texCoords = {{0, 0}};
        mesh.triangles = {{3, {0, 1, 2, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}}};
        mesh.textureName = "part.jpg";
        Entry& entry = m_models[id];
        entry.model.meshes = {mesh};
        entry.slots = {slot};
    }

    [[nodiscard]] const Client::Data::Models::Model* Find(std::uint16_t id) const override
    {
        const auto found = m_models.find(id);
        return found == m_models.end() ? nullptr : &found->second.model;
    }
    [[nodiscard]] const Client::Models::Skeleton* SkeletonOf(std::uint16_t) const override
    {
        return nullptr;
    }
    [[nodiscard]] std::span<const TextureSlot> MeshTextures(std::uint16_t id) const override
    {
        const auto found = m_models.find(id);
        if (found == m_models.end())
            return {};
        return found->second.slots;
    }

private:
    struct Entry
    {
        Client::Data::Models::Model model;
        std::array<TextureSlot, 1> slots{};
    };
    std::map<std::uint16_t, Entry> m_models;
};

class ItemDrawingTest : public testing::Test
{
protected:
    void SetUp() override
    {
        for (const std::uint16_t model : Models)
            m_models.Add(model);
        m_batch.Begin(Client::Render::Pass3D{});
        m_owner.bones = m_bones;
        m_owner.blendMeshLight = &m_light;
        m_special.worldClock = 0.0f;
        m_context.special = &m_special;
    }

    Client::Core::Expected<void, std::string> DrawPart(const ItemPart& part)
    {
        return Client::Actors::Drawing::DrawItemPart(m_batch, m_models, m_owner, part, Clock, m_context,
                                                     [this](const glm::vec3& position)
                                                     { m_boxes.push_back(position); });
    }

    Client::Core::Expected<void, std::string> DrawWorn(const WornParts& parts, std::uint8_t characterClass)
    {
        return Client::Actors::Drawing::DrawWornParts(m_batch, m_models, m_owner, parts, characterClass, Clock,
                                                      m_context, [this](const glm::vec3& position)
                                                      { m_boxes.push_back(position); });
    }

    // The x of the first vertex of each triangle drawn, in order.
    [[nodiscard]] std::vector<float> DrawnX() const
    {
        std::vector<float> drawn;
        const auto vertices = m_batch.Vertices();
        for (std::size_t index = 0; index < vertices.size(); index += 3)
            drawn.push_back(vertices[index].position.x);
        return drawn;
    }

    // The color of the first vertex of each triangle drawn, in order.
    [[nodiscard]] std::vector<Color> DrawnColors() const
    {
        std::vector<Color> drawn;
        const auto vertices = m_batch.Vertices();
        for (std::size_t index = 0; index < vertices.size(); index += 3)
            drawn.push_back(vertices[index].color);
        return drawn;
    }

    PartLibrary m_models;
    Client::Render::ModelBatch m_batch;
    std::array<BoneTransform, 1> m_bones{BoneTransform(1.0f)};
    float m_light = 1.0f;
    Client::Actors::Drawing::ItemSpecialDrawContext m_special;
    Client::Actors::Drawing::ItemDrawContext m_context;
    ItemPartOwner m_owner;
    std::vector<glm::vec3> m_boxes;
};

// 0x60cf99: at an alpha of 0.01f or less, nothing, not even the picking box.
TEST_F(ItemDrawingTest, APartAtAnAlphaOfOneHundredthDrawsNothing)
{
    m_owner.alpha = std::bit_cast<float>(0x3c23d70au);

    ASSERT_TRUE(DrawPart({.model = Armor}).HasValue());

    EXPECT_TRUE(m_batch.Vertices().empty());
    EXPECT_TRUE(m_boxes.empty());
}

// The record 0x60dbe0 zeroes has no mesh to draw; 0x4ebc80 publishes the picking box anyway.
TEST_F(ItemDrawingTest, AMissingModelDrawsNothingAndStillPublishesTheBox)
{
    m_owner.transform.position = {150.0f, 200.0f, 300.0f};

    ASSERT_TRUE(DrawPart({.model = 0x1214}).HasValue());

    EXPECT_TRUE(m_batch.Vertices().empty());
    ASSERT_EQ(m_boxes.size(), 1u);
    EXPECT_EQ(m_boxes.front(), m_owner.transform.position);
}

// 0x60d04d..0x60d14c: with the mode 1, the two passes of 0x60cd60 before the part; 0x60cd74: none with +0x78 & 0x4000.
TEST_F(ItemDrawingTest, TheOutlineGoesBeforeThePartAndNotOnARider)
{
    const std::array<ObjectOutlinePass, 2> passes{
        {{1.2f, Color{0.1f, 0.02f, 0.0f, 1.0f}}, {1.18f, Color{0.7f, 0.2f, 0.0f, 1.0f}}}};
    m_owner.outline = passes;

    ASSERT_TRUE(DrawPart({.model = Armor}).HasValue());

    const std::vector<Color> colors = DrawnColors();
    ASSERT_EQ(colors.size(), 3u);
    EXPECT_EQ(colors[0], passes[0].color);
    EXPECT_EQ(colors[1], passes[1].color);
    EXPECT_FLOAT_EQ(DrawnX()[2], X(Armor));

    m_batch.Begin(Client::Render::Pass3D{});
    m_owner.stateFlags = 0x4000;
    ASSERT_TRUE(DrawPart({.model = Armor}).HasValue());
    EXPECT_EQ(DrawnX(), std::vector<float>{X(Armor)});
}

// 0x50d3dc..0x50d4f5: k from 5 to 0, skipping the parts without a model.
TEST_F(ItemDrawingTest, TheWornPartsGoFromTheBootsToTheHead)
{
    const WornParts parts{ItemPart{.model = Head},   ItemPart{.model = Helm}, ItemPart{.model = Armor}, std::nullopt,
                          ItemPart{.model = Gloves}, ItemPart{.model = Boots}};

    ASSERT_TRUE(DrawWorn(parts, 0).HasValue());

    EXPECT_EQ(DrawnX(), (std::vector<float>{X(Boots), X(Gloves), X(Armor), X(Helm), X(Head)}));
    EXPECT_EQ(m_boxes.size(), 5u);
}

// 0x50d431: each part's model takes the skin 2 · (class & 7) + (class >> 3); its skin mesh (0x12e) goes on
// 0x12e + skin and its hair mesh (0x137) on 0x12f + skin.
TEST_F(ItemDrawingTest, TheClassSkinGoesOnTheSkinAndHairMeshesOfEachPart)
{
    m_models.Add(Armor, TextureSlot{Client::Data::Models::SkinTextureSlot});
    m_models.Add(Pants, TextureSlot{Client::Data::Models::HairTextureSlot});
    WornParts parts{};
    parts[2] = ItemPart{.model = Armor};
    parts[3] = ItemPart{.model = Pants};
    for (const int characterClass : {0, 1, 2, 3, 4, 9, 12})
    {
        SCOPED_TRACE(characterClass);
        m_batch.Begin(Client::Render::Pass3D{});

        ASSERT_TRUE(DrawWorn(parts, static_cast<std::uint8_t>(characterClass)).HasValue());

        const auto skin = SkinIndexOf(static_cast<std::uint8_t>(characterClass));
        const auto commands = m_batch.Commands();
        ASSERT_EQ(commands.size(), 2u);
        EXPECT_EQ(commands[0].state.texture, TextureSlot{static_cast<std::uint16_t>(0x12f + skin)});
        EXPECT_EQ(commands[1].state.texture, TextureSlot{static_cast<std::uint16_t>(0x12e + skin)});
    }
}

// 0x50d43d..0x50d46d: the class 4 helm stores sin(T · 0.001f) · 0.1f + 0.7f in +0x68, the light of the blend mesh: the
// head drawn after it takes it, and so do the boots of the next frame, drawn before the helm writes again.
TEST_F(ItemDrawingTest, TheClassFourHelmLightsTheHeadNowAndTheBootsNextFrame)
{
    m_owner.blendMesh = 0;
    const WornParts parts{ItemPart{.model = Head}, ItemPart{.model = Helm}, std::nullopt, std::nullopt, std::nullopt,
                          ItemPart{.model = Boots}};

    ASSERT_TRUE(DrawWorn(parts, 4).HasValue());
    EXPECT_EQ(m_light, 0.7f);
    const std::vector<Color> first = DrawnColors();

    m_batch.Begin(Client::Render::Pass3D{});
    m_special.worldClock = 1571.0f;
    ASSERT_TRUE(DrawWorn(parts, 4).HasValue());
    EXPECT_NEAR(m_light, 0.7 + 0.1 * Client::Math::RoundedSin(1.571), 1e-6);
    const std::vector<Color> second = DrawnColors();

    ASSERT_EQ(first.size(), 3u);
    ASSERT_EQ(second.size(), 3u);
    EXPECT_NE(first[0], first[2]);
    EXPECT_EQ(first[1], first[2]);
    EXPECT_EQ(second[0], first[2]);
    EXPECT_NE(second[2], first[2]);
}

// A class other than 4 leaves +0x68 alone, and so does a class 4 without a helm.
TEST_F(ItemDrawingTest, OnlyTheHelmOfTheClassFourWritesTheLight)
{
    WornParts parts{};
    parts[1] = ItemPart{.model = Helm};
    ASSERT_TRUE(DrawWorn(parts, 3).HasValue());
    EXPECT_EQ(m_light, 1.0f);

    parts[1].reset();
    parts[2] = ItemPart{.model = Armor};
    ASSERT_TRUE(DrawWorn(parts, 4).HasValue());
    EXPECT_EQ(m_light, 1.0f);
}

// 0x50d470..0x50d4a4: with the class 4 (or its second class), a helm 0x1012 + {0, 5, 6, 8, 9} draws its mask
// 0x429d + {0, 5, 6, 8, 9} instead, with the same skin.
TEST_F(ItemDrawingTest, TheClassFourWearsTheMaskOfAMaskedHelm)
{
    m_models.Add(Mask, TextureSlot{Client::Data::Models::SkinTextureSlot});
    WornParts parts{};
    parts[1] = ItemPart{.model = MaskedHelm};
    for (const int characterClass : {4, 12})
    {
        SCOPED_TRACE(characterClass);
        m_batch.Begin(Client::Render::Pass3D{});
        const auto skin = SkinIndexOf(static_cast<std::uint8_t>(characterClass));

        ASSERT_TRUE(DrawWorn(parts, static_cast<std::uint8_t>(characterClass)).HasValue());

        EXPECT_EQ(DrawnX(), std::vector<float>{X(Mask)});
        ASSERT_EQ(m_batch.Commands().size(), 1u);
        EXPECT_EQ(m_batch.Commands()[0].state.texture, TextureSlot{static_cast<std::uint16_t>(0x12e + skin)});
    }

    m_batch.Begin(Client::Render::Pass3D{});
    ASSERT_TRUE(DrawWorn(parts, 3).HasValue());
    EXPECT_EQ(DrawnX(), std::vector<float>{X(MaskedHelm)});
}

// 0x50d44b reads the world clock (0x5767b88).
TEST_F(ItemDrawingTest, TheClassFourHelmNeedsTheWorldClock)
{
    m_special.worldClock.reset();
    WornParts parts{};
    parts[1] = ItemPart{.model = Helm};

    const auto drawn = DrawWorn(parts, 4);

    ASSERT_FALSE(drawn.HasValue());
    EXPECT_NE(drawn.Error().find("0x50d44b"), std::string::npos) << drawn.Error();
}

// A model outside the records 0x60dbe0 zeroes would make the original write outside them: it cuts after the parts
// already drawn.
TEST_F(ItemDrawingTest, AModelOutsideTheZeroedRecordsCuts)
{
    WornParts parts{};
    parts[5] = ItemPart{.model = Boots};
    parts[0] = ItemPart{.model = 0x6e61};

    const auto drawn = DrawWorn(parts, 0);

    ASSERT_FALSE(drawn.HasValue());
    EXPECT_NE(drawn.Error().find("0x50d431"), std::string::npos) << drawn.Error();
    EXPECT_EQ(DrawnX(), std::vector<float>{X(Boots)});
}

// 0x60d198: the pants that put a cloth on the part (0x455e30) are not ported.
TEST_F(ItemDrawingTest, ThePantsWithAClothCut)
{
    for (const std::uint16_t model : {std::uint16_t{0x1424}, std::uint16_t{0x1425}, std::uint16_t{0x1428}})
    {
        SCOPED_TRACE(model);
        m_models.Add(model);

        const auto drawn = DrawPart({.model = model});

        ASSERT_FALSE(drawn.HasValue());
        EXPECT_NE(drawn.Error().find("0x60d198"), std::string::npos) << drawn.Error();
    }
}
} // namespace
