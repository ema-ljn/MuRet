#include "Actors/Drawing/CharacterDrawing.h"

#include "Data/Models/Model.h"
#include "Render/ModelBatch.h"
#include "Render/ModelVertex.h"
#include "Math/HalfLifeMath.h"
#include "Models/IModelLibrary.h"
#include "Models/Skeleton.h"
#include "Render/Pass3D.h"
#include "Render/TextureSlot.h"

#include <gtest/gtest.h>

#include <glm/ext/vector_float3.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace
{
class OneTriangle final : public Client::Models::IModelLibrary
{
public:
    OneTriangle()
    {
        Client::Data::Models::Mesh mesh;
        mesh.vertices = {{0, {0, 0, 0}}, {0, {1, 0, 0}}, {0, {0, 1, 0}}};
        mesh.normals = {{0, {0.3f, -0.5f, 0.8f}, 0}};
        mesh.texCoords = {{0, 0}};
        mesh.triangles = {{3, {0, 1, 2, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}}};
        mesh.textureName = "item.jpg";
        m_model.meshes = {mesh};
    }
    [[nodiscard]] const Client::Data::Models::Model* Find(std::uint16_t) const override
    {
        return &m_model;
    }
    [[nodiscard]] const Client::Models::Skeleton* SkeletonOf(std::uint16_t) const override
    {
        return nullptr;
    }
    [[nodiscard]] std::span<const Client::Render::TextureSlot> MeshTextures(std::uint16_t) const override
    {
        return m_textures;
    }

private:
    Client::Data::Models::Model m_model;
    std::array<Client::Render::TextureSlot, 1> m_textures{Client::Render::TextureSlot{0x400}};
};

struct Drawn
{
    std::vector<Client::Render::ModelVertex> vertices;
    std::vector<Client::Render::ModelDrawState> states;

    friend bool operator==(const Drawn& left, const Drawn& right)
    {
        if (left.states != right.states || left.vertices.size() != right.vertices.size())
            return false;
        for (std::size_t index = 0; index < left.vertices.size(); ++index)
        {
            const auto& a = left.vertices[index];
            const auto& b = right.vertices[index];
            if (a.position != b.position || a.uv != b.uv || a.color != b.color)
                return false;
        }
        return true;
    }
};

Drawn DrawSword(std::uint8_t level, std::uint8_t excellent, std::uint8_t glowDetail)
{
    const OneTriangle models;
    Client::Actors::Drawing::DrawnObject object;
    object.model = 0x212;
    object.color = glm::vec3(1.0f);
    const std::array<Client::Math::BoneTransform, 1> bones{Client::Math::BoneTransform(1.0f)};
    const auto meshes = Client::Actors::Drawing::SkinObjectMeshes(models, object, bones);
    Client::Actors::Drawing::ItemDrawContext context;
    context.glowDetail = glowDetail;
    Client::Render::ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});

    const auto drawn = Client::Actors::Drawing::DrawItemFromMeshes(
        batch, models, object, std::chrono::milliseconds(1571), meshes, level, excellent, 0, context);

    EXPECT_TRUE(drawn.HasValue()) << drawn.Error();
    Drawn result{.vertices = {batch.Vertices().begin(), batch.Vertices().end()}, .states = {}};
    for (const auto& command : batch.Commands())
        result.states.push_back(command.state);
    return result;
}
} // namespace

// 0x60b573: under 4, the options' glow detail (0x6c9dcc) trims the level to 2 · detail + 5, and at 0 the excellent
// pass is off (0x60cb3b).
TEST(ItemGlowDetailTest, TheDetailTrimsTheLevelAndZeroTurnsTheExcellentPassOff)
{
    for (const std::uint8_t detail : {std::uint8_t{0}, std::uint8_t{1}, std::uint8_t{2}, std::uint8_t{3}})
    {
        SCOPED_TRACE(static_cast<int>(detail));
        const auto trimmed = static_cast<std::uint8_t>(2 * detail + 5);
        const auto excellent = static_cast<std::uint8_t>(detail == 0 ? 0 : 1);

        EXPECT_TRUE(DrawSword(13, 1, detail) == DrawSword(trimmed, excellent, 4));
        EXPECT_FALSE(DrawSword(13, 1, detail) == DrawSword(13, 1, 4));
    }
}
