#include "Models/PassColors.h"

#include "Math/RoundedMath.h"
#include "Models/Shine.h"
#include "Render/SpriteVertex.h"

#include <gtest/gtest.h>

#include <glm/ext/vector_float3.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <optional>
#include <ratio>

// The color of each glow pass of 0x60b3d0, with the tables from emulating 0x608e00, 0x6093d0 and 0x609560 on main.exe
// (06 § Los pases de brillo).
namespace
{
using Client::Math::RoundedSin;
using Client::Models::ExcellentColorAt;
using Client::Models::MetalFactorOf;
using Client::Models::PassHiddenMesh;
using Client::Models::SetAlphaAt;
using Client::Models::SetColorOf;
using Client::Models::SetPulseAt;
using Client::Models::SetPulseIsOff;
using Client::Models::ShineColorOf;
using Client::Models::ShinePass;
using Client::Render::Color;

void ExpectColor(const Color& color, float red, float green, float blue)
{
    EXPECT_FLOAT_EQ(color.red, red);
    EXPECT_FLOAT_EQ(color.green, green);
    EXPECT_FLOAT_EQ(color.blue, blue);
}

// 0x608e00, the one of passes 0x44 and 0x48: one color per item, ignoring the light. That of the trace's Male17 (case
// 10) and that of Sword17, the usual one.
TEST(PassColorsTest, TheShinePassesHaveTheColorOfTheirItem)
{
    ExpectColor(ShineColorOf(0x1222), 0.75f, 0.65f, 0.5f);
    ExpectColor(ShineColorOf(0x222), 1.0f, 0.5f, 0.0f);
    ExpectColor(ShineColorOf(0x16d), 1.0f, 0.2f, 0.0f);
    ExpectColor(ShineColorOf(0x175), 1.0f, 1.0f, 1.0f);
}

// 0x6093d0, the one of passes 0x240 and 0x1040: multiplies the base color, which is the light times the level factor.
// Almost all by 1.
TEST(PassColorsTest, TheMetalPassesKeepTheBase)
{
    EXPECT_EQ(MetalFactorOf(0x1222), glm::vec3(1.0f, 1.0f, 1.0f));
    EXPECT_EQ(MetalFactorOf(0x1016), glm::vec3(1.0f, 0.5f, 0.0f));
    EXPECT_EQ(MetalFactorOf(0x220), glm::vec3(0.0f, 0.5f, 1.0f));
}

// 0x609560, the set pass one (0x840): blue, except 15 golden parts.
TEST(PassColorsTest, TheSetPassIsBlueOrGold)
{
    ExpectColor(SetColorOf(0x1222), 0.1f, 0.6f, 1.0f);
    ExpectColor(SetColorOf(0x1015), 1.0f, 0.7f, 0.2f);
}

// 0x60b3d0, the 0x42 of the excellent item: k = (sin(0.002 · T) + 1) · 0.5, and (k, 0.3k, 1 − k).
TEST(PassColorsTest, TheExcellentColorPulses)
{
    ExpectColor(ExcellentColorAt(std::chrono::milliseconds{0}), 0.5f, 0.15f, 0.5f);
    const auto top = std::chrono::duration<double, std::milli>(std::numbers::pi / 0.004);
    const Color brightest = ExcellentColorAt(std::chrono::duration_cast<std::chrono::nanoseconds>(top));
    EXPECT_NEAR(brightest.red, 1.0f, 1e-5f);
    EXPECT_NEAR(brightest.green, 0.3f, 1e-5f);
    EXPECT_NEAR(brightest.blue, 0.0f, 1e-5f);
}

// 0x60b3d0 calls the set pass with alpha sin(0.001 · T) · 0.5 + 0.4.
TEST(PassColorsTest, TheSetAlphaPulses)
{
    EXPECT_FLOAT_EQ(SetAlphaAt(std::chrono::milliseconds{0}), 0.4f);
    EXPECT_FLOAT_EQ(SetAlphaAt(std::chrono::milliseconds{250}), RoundedSin(0.25f) * 0.5f + 0.4f);
}

// 0x60cc19..0x60cc1c: the alpha is the pulse stored in a float, but the set's rand() is decided by the unrounded x87
// pulse (FCOMP against 0.01f). At 4036258298 ns the pulse exceeds 0.01f by 2.7e−10 and its float is already 0.01f;
// two nanoseconds later it is below.
TEST(PassColorsTest, TheSetPulseIsComparedBeforeItBecomesAFloat)
{
    const auto at = [](std::int64_t nanoseconds)
    { return std::chrono::duration<double, std::milli>(std::chrono::nanoseconds{nanoseconds}).count(); };
    const double above = SetPulseAt(at(4036258298));
    EXPECT_GT(above, static_cast<double>(0.01f));
    EXPECT_LE(static_cast<float>(above), 0.01f);
    EXPECT_FALSE(SetPulseIsOff(above));
    EXPECT_TRUE(SetPulseIsOff(SetPulseAt(at(4036258300))));
    EXPECT_EQ(SetAlphaAt(std::chrono::nanoseconds{4036258298}), static_cast<float>(above));
}

// The mesh 0x60b130 (passes 0x48 and 0x44) and 0x60b300 (0x240, 0x1040 and set) pass to RenderBody as hidden: 1 for
// 0xc17 and 0x226 and 0 for 0x227 in all, and 2 for 0x619 only in those of 0x60b130. The rest, none, even if their
// object hides one.
TEST(PassColorsTest, SomeItemsHideAMeshInTheirPasses)
{
    for (const ShinePass pass :
         {ShinePass::Chrome, ShinePass::Shiny, ShinePass::Chrome2, ShinePass::Chrome3, ShinePass::Chrome4})
    {
        EXPECT_EQ(PassHiddenMesh(0xc17, pass, -1), std::optional<std::size_t>{1});
        EXPECT_EQ(PassHiddenMesh(0x226, pass, -1), std::optional<std::size_t>{1});
        EXPECT_EQ(PassHiddenMesh(0x227, pass, -1), std::optional<std::size_t>{0});
        EXPECT_EQ(PassHiddenMesh(0x212, pass, 2), std::nullopt);
    }
    EXPECT_EQ(PassHiddenMesh(0x619, ShinePass::Chrome, -1), std::optional<std::size_t>{2});
    EXPECT_EQ(PassHiddenMesh(0x619, ShinePass::Shiny, -1), std::optional<std::size_t>{2});
    EXPECT_EQ(PassHiddenMesh(0x619, ShinePass::Chrome2, 2), std::nullopt);
    EXPECT_EQ(PassHiddenMesh(0x619, ShinePass::Chrome4, 2), std::nullopt);
}

// 0x60b130 passes the object's +0x58 to 0x231, 0x81c and 0xe22 and to 0x61a..0x61f; 0x60b300 does not.
TEST(PassColorsTest, SomeItemsHideTheMeshOfTheirObjectInTheShinePasses)
{
    for (const std::uint16_t model :
         {std::uint16_t{0x231}, std::uint16_t{0x81c}, std::uint16_t{0xe22}, std::uint16_t{0x61a}, std::uint16_t{0x61f}})
    {
        EXPECT_EQ(PassHiddenMesh(model, ShinePass::Chrome, 1), std::optional<std::size_t>{1}) << model;
        EXPECT_EQ(PassHiddenMesh(model, ShinePass::Shiny, 2), std::optional<std::size_t>{2}) << model;
        EXPECT_EQ(PassHiddenMesh(model, ShinePass::Shiny, -1), std::nullopt) << model;
        EXPECT_EQ(PassHiddenMesh(model, ShinePass::Chrome4, 1), std::nullopt) << model;
    }
    EXPECT_EQ(PassHiddenMesh(0x620, ShinePass::Chrome, 1), std::nullopt);
}
} // namespace
