#include "Scenes/ParticleBillboard.h"

#include "Core/Expected.h"
#include "Render/Billboard.h"
#include "Render/Models/OriginalRotation.h"
#include "World/Particles.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>

// Contrato 621400_fauna_state: test C3 para rotación y selector de 32 bits completo para excepción 5e4.
namespace
{
constexpr Client::Render::Models::OriginalMatrix Identity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};

TEST(ParticleBillboardTest, AnUnorderedRotationUsesTheUnrotatedGeometryAndKeepsTheRawArgument)
{
    Client::World::ParticleDraw draw{.texture = 17,
                                     .position = {10, 20, 30},
                                     .width = 8,
                                     .height = 4,
                                     .light = {0.25f, 0.5f, 0.75f},
                                     .rotation = std::bit_cast<float>(0x7fc12345u)};
    const auto prepared = Client::Scenes::MakeParticleBillboard(Identity, draw, std::uint8_t{4});
    ASSERT_TRUE(prepared.HasValue());
    constexpr std::array expected{glm::vec3(6, 18, 30), glm::vec3(14, 18, 30), glm::vec3(14, 22, 30),
                                  glm::vec3(6, 22, 30)};
    for (std::size_t k = 0; k < expected.size(); ++k)
    {
        EXPECT_EQ(prepared.Value().corners[k].position, expected[k]);
    }
    EXPECT_EQ(std::bit_cast<std::uint32_t>(draw.rotation), 0x7fc12345u);
}

TEST(ParticleBillboardTest, TheOpaqueAlphaExceptionComparesTheFullSelectorAndAnyNonThreeFlagUsesRgba)
{
    struct Case
    {
        std::int32_t selector;
        std::uint8_t flag;
        float alpha;
    };
    constexpr std::array cases{Case{0x5e4, 4, 1}, Case{0x105e4, 4, 0.25f}, Case{-1, 0, 0.25f}, Case{17, 2, 0.25f},
                               Case{0x105e4, 3, 1}};
    for (const Case& value : cases)
    {
        const Client::World::ParticleDraw draw{
            .texture = value.selector, .width = 8, .height = 4, .light = {0.25f, 0.5f, 0.75f}};
        const auto prepared = Client::Scenes::MakeParticleBillboard(Identity, draw, value.flag);
        ASSERT_TRUE(prepared.HasValue());
        EXPECT_EQ(prepared.Value().color.alpha, value.alpha) << value.selector << ": " << static_cast<int>(value.flag);
        EXPECT_EQ(draw.texture, value.selector);
    }
}

TEST(ParticleBillboardTest, AColorMetadataBindingIsRequiredInsteadOfAssumingJpegComponents)
{
    const Client::World::ParticleDraw draw{.texture = 17};
    const auto prepared = Client::Scenes::MakeParticleBillboard(Identity, draw, std::nullopt);
    ASSERT_FALSE(prepared.HasValue());
    EXPECT_EQ(prepared.Error(), Client::World::ParticleError::MissingColorFlag);
}
TEST(ParticleBillboardTest, ZeroDimensionsAndAlphaStillPrepareAQuadAndNegativeWidthIsNotClamped)
{
    Client::World::ParticleDraw draw{.texture = 17, .position = {10, 20, 30}, .light = {0, 0.5f, 0.75f}};
    const auto zero = Client::Scenes::MakeParticleBillboard(Identity, draw, std::uint8_t{4});
    ASSERT_TRUE(zero.HasValue());
    for (const Client::Render::BillboardCorner& corner : zero.Value().corners)
    {
        EXPECT_EQ(corner.position, glm::vec3(10, 20, 30));
    }
    EXPECT_EQ(zero.Value().color.alpha, 0);
    draw.width = -8;
    draw.height = 4;
    const auto negative = Client::Scenes::MakeParticleBillboard(Identity, draw, std::uint8_t{4});
    ASSERT_TRUE(negative.HasValue());
    EXPECT_EQ(negative.Value().corners[0].position, glm::vec3(14, 18, 30));
    EXPECT_EQ(negative.Value().corners[1].position, glm::vec3(6, 18, 30));
}
} // namespace
