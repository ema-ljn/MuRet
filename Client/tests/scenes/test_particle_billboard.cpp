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

// Casos 0/1 de world55/billboard.txt, ya congelados: atlas y giro finito pasan por este adaptador.
TEST(ParticleBillboardTest, FrozenHelperCasesAlsoObserveAdapterRotationAtlasAndRgbForwarding)
{
    struct Case
    {
        std::int32_t texture;
        std::uint8_t flag;
        std::array<std::uint32_t, 25> input;
        std::array<std::array<std::uint32_t, 5>, 4> corners;
    };
    constexpr std::array cases{
        Case{1522,
             4,
             {0x3ee51759u, 0x3f64f1a3u, 0,           0x45bcab08u, 0x3c72b126u, 0xbbf2d920u, 0x3f7ff702u,
              0xc6859d19u, 0x3f64e999u, 0xbee50f4du, 0xbc87afb8u, 0x44b368f7u, 0x4656b748u, 0x46b5342au,
              0x4456b2bcu, 0x43278f17u, 0x41b38034u, 0x3e8403d4u, 0x3fa093ffu, 0x3f38d445u, 0,
              0x3f28f5c3u, 0,           0x3ea8f5c3u, 0x3ea8f5c3u},
             {{{0x47004d0au, 0xc67d7dbcu, 0x45504391u, 0x3f28f5c3u, 0x3ea8f5c3u},
               {0x4700f49au, 0xc67d7dbcu, 0x45504391u, 0x3f7d70a4u, 0x3ea8f5c3u},
               {0x4700f49au, 0xc67d23fcu, 0x45504391u, 0x3f7d70a4u, 0},
               {0x47004d0au, 0xc67d23fcu, 0x45504391u, 0x3f28f5c3u, 0}}}},
        Case{1452,
             3,
             {0xbf722bf0u, 0xbea5ffb1u, 0,           0x454f7e05u, 0x3e1b081eu, 0xbee22c08u, 0x3f625fdau,
              0xc688eb09u, 0xbe92c9e2u, 0x3f562577u, 0x3eef1636u, 0x4555da83u, 0x469977a7u, 0x45b95d3bu,
              0x45569dfbu, 0x42c00118u, 0x4375c1ddu, 0x3f5e6b81u, 0x3edd9368u, 0x3eb4edcbu, 0xc365e083u,
              0,           0,           0x3f800000u, 0x3f800000u},
             {{{0xc6854a30u, 0xc65c3313u, 0x458819d1u, 0, 0x3f800000u},
               {0xc685c5ecu, 0xc65b0d70u, 0x458819d1u, 0x3f800000u, 0x3f800000u},
               {0xc6873dc4u, 0xc65d86efu, 0x458819d1u, 0x3f800000u, 0},
               {0xc686c208u, 0xc65eac92u, 0x458819d1u, 0, 0}}}}};
    for (const Case& value : cases)
    {
        SCOPED_TRACE(value.texture);
        const auto f = [&value](std::size_t i) { return std::bit_cast<float>(value.input[i]); };
        Client::Render::Models::OriginalMatrix view{};
        for (std::size_t i = 0; i < view.size(); ++i)
        {
            view[i] = f(i);
        }
        const Client::World::ParticleDraw draw{.texture = value.texture,
                                               .position = {f(12), f(13), f(14)},
                                               .width = f(15),
                                               .height = f(16),
                                               .light = {f(17), f(18), f(19)},
                                               .rotation = f(20),
                                               .uv = {f(21), f(22)},
                                               .uvSize = {f(23), f(24)}};
        const auto prepared = Client::Scenes::MakeParticleBillboard(view, draw, value.flag);
        ASSERT_TRUE(prepared.HasValue());
        for (std::size_t i = 0; i < value.corners.size(); ++i)
        {
            const auto& corner = prepared.Value().corners[i];
            for (int axis = 0; axis < 3; ++axis)
            {
                EXPECT_EQ(std::bit_cast<std::uint32_t>(corner.position[axis]),
                          value.corners[i][static_cast<std::size_t>(axis)]);
            }
            EXPECT_EQ(std::bit_cast<std::uint32_t>(corner.uv.x), value.corners[i][3]);
            EXPECT_EQ(std::bit_cast<std::uint32_t>(corner.uv.y), value.corners[i][4]);
        }
        EXPECT_EQ(std::bit_cast<std::uint32_t>(prepared.Value().color.red), value.input[17]);
        EXPECT_EQ(std::bit_cast<std::uint32_t>(prepared.Value().color.green), value.input[18]);
        EXPECT_EQ(std::bit_cast<std::uint32_t>(prepared.Value().color.blue), value.input[19]);
        EXPECT_EQ(prepared.Value().color.alpha, value.flag == 3 ? 1.0f : f(17));
    }
}

} // namespace
