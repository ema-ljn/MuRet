#include "Actors/Drawing/GameItemGlowEffects.h"

#include "Models/Shine.h"

#include <gtest/gtest.h>

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <array>
#include <chrono>
#include <cstddef>

namespace
{
using Client::Actors::Drawing::ItemShineClockOf;
using Client::Actors::Drawing::ItemShineTexCoords;
using Client::Models::ShinePass;

struct ShineCase
{
    ShinePass pass;
    glm::vec3 normal;
    std::chrono::nanoseconds clock;
    glm::vec2 uv;
};

constexpr glm::vec3 Up{0x1.333334p-2f, -0x1p-1f, 0x1.99999ap-1f};
constexpr glm::vec3 Side{-0x1.666666p-1f, 0x1.99999ap-4f, 0x1.99999ap-3f};
constexpr std::chrono::nanoseconds Start{0};
constexpr std::chrono::nanoseconds Seconds{1'234'567'891};
constexpr std::chrono::nanoseconds Days{987'654'321'000};

// The UVs of 0x4ecc30 that ItemShineTexCoords gave when it computed the clock, and the PC53 sine and cosine of Chrome4,
// at each corner, in the Debug build and in the Release one.
constexpr std::array<ShineCase, 30> Cases{{
    {ShinePass::Shiny, Up, Start, {0x1.333334p-1f, 0x1p-2f}},
    {ShinePass::Shiny, Up, Seconds, {0x1.333334p-1f, 0x1p-2f}},
    {ShinePass::Shiny, Up, Days, {0x1.333334p-1f, 0x1p-2f}},
    {ShinePass::Shiny, Side, Start, {0x1.333334p-2f, 0x1.19999ap-1f}},
    {ShinePass::Shiny, Side, Seconds, {0x1.333334p-2f, 0x1.19999ap-1f}},
    {ShinePass::Shiny, Side, Days, {0x1.333334p-2f, 0x1.19999ap-1f}},
    {ShinePass::Chrome, Up, Start, {0x1.99999ap-2f, -0x1p-2f}},
    {ShinePass::Chrome, Up, Seconds, {0x1.0bfb16p-1f, -0x1.a36e8p-9f}},
    {ShinePass::Chrome, Up, Days, {0x1.2a57a8p+0f, 0x1.47e282p+0f}},
    {ShinePass::Chrome, Side, Start, {0x1.99999ap-4f, 0x1.99999ap-5f}},
    {ShinePass::Chrome, Side, Seconds, {0x1.c985fp-3f, 0x1.2fec56p-2f}},
    {ShinePass::Chrome, Side, Days, {0x1.bb15b6p-1f, 0x1.94af4ep+0f}},
    {ShinePass::Chrome2, Up, Start, {0x1.47ae1ap-4f, -0x1.666666p+0f}},
    {ShinePass::Chrome2, Up, Seconds, {0x1.583a54p-1f, -0x1.05e5f4p-1f}},
    {ShinePass::Chrome2, Up, Days, {0x1.5a9a8p+0f, 0x1.05920ep-1f}},
    {ShinePass::Chrome2, Side, Start, {-0x1.333334p+0f, -0x1.ccccccp+0f}},
    {ShinePass::Chrome2, Side, Seconds, {-0x1.3721d6p-1f, -0x1.d2b2cp-1f}},
    {ShinePass::Chrome2, Side, Days, {0x1.2ec6b4p-4f, 0x1.c62a0ep-4f}},
    {ShinePass::Chrome3, Up, Start, {-0x1.2e147cp-1f, 0x1.970a3ep+0f}},
    {ShinePass::Chrome3, Up, Seconds, {-0x1.2e147cp-1f, 0x1.970a3ep+0f}},
    {ShinePass::Chrome3, Up, Days, {-0x1.2e147cp-1f, 0x1.970a3ep+0f}},
    {ShinePass::Chrome3, Side, Start, {-0x1.5c28f6p-3f, 0x1.2b851ep+0f}},
    {ShinePass::Chrome3, Side, Seconds, {-0x1.5c28f6p-3f, 0x1.2b851ep+0f}},
    {ShinePass::Chrome3, Side, Days, {-0x1.5c28f6p-3f, 0x1.2b851ep+0f}},
    {ShinePass::Chrome4, Up, Start, {0x1.b33334p-1f, -0x1p-1f}},
    {ShinePass::Chrome4, Up, Seconds, {0x1.1a668p+1f, -0x1.6e4f28p-2f}},
    {ShinePass::Chrome4, Up, Days, {0x1.2f5c04p+1f, -0x1.21dd7ap+1f}},
    {ShinePass::Chrome4, Side, Start, {-0x1.ccccccp-2f, 0x1.666666p+0f}},
    {ShinePass::Chrome4, Side, Seconds, {0x1.f337cap+0f, 0x1.fe6c2p-2f}},
    {ShinePass::Chrome4, Side, Days, {0x1.0e6a7cp+1f, -0x1.683e48p+0f}},
}};

// The clock is computed once per pass and each corner uses it: the UVs give the same bits as when each corner computed
// it again.
TEST(GameItemGlowEffectsTest, TheShineClockOfAPassGivesTheSameUvAsEachCorner)
{
    for (std::size_t index = 0; index < Cases.size(); ++index)
    {
        const ShineCase& shine = Cases[index];
        const glm::vec2 uv = ItemShineTexCoords(shine.pass, shine.normal, ItemShineClockOf(shine.pass, shine.clock));
        EXPECT_EQ(uv.x, shine.uv.x) << index;
        EXPECT_EQ(uv.y, shine.uv.y) << index;
    }
}

// 0x4ed25e..0x4ed2b2, the 0x4000: the sum c·nx + s·ny + nz is stored in u; v is 1 minus the sum, minus nz · 2.5f and t;
// and u then adds s · 5f and ny · 3f to the stored float. The bits come from those computations in double (the x87 in
// PC53) with c, s and t in float, as 0x4ed109..0x4ed11a stores them.
TEST(GameItemGlowEffectsTest, TheChrome5PassWeighsTheSineByFiveAndTheNormalByTwoAndAHalf)
{
    struct Case
    {
        glm::vec3 normal;
        std::chrono::nanoseconds clock;
        glm::vec2 uv;
    };
    for (const Case& shine :
         {Case{Up, Start, {-0x1.999998p-2f, -0x1.0ccccep+1f}}, Case{Side, Start, {-0x1.999998p-3f, 0x1p+0f}},
          Case{Up, Seconds, {0x1.19dd84p+1f, -0x1.b5ff1cp+0f}}, Case{Side, Seconds, {0x1.b912eap+1f, 0x1.618baap-2f}}})
    {
        const glm::vec2 uv =
            ItemShineTexCoords(ShinePass::Chrome5, shine.normal, ItemShineClockOf(ShinePass::Chrome5, shine.clock));
        EXPECT_EQ(uv.x, shine.uv.x) << shine.clock.count();
        EXPECT_EQ(uv.y, shine.uv.y) << shine.clock.count();
    }
}

// One clock serves every corner of the pass: with another normal and the same clock, the UV of Chrome4 is the one that
// corner gave alone.
TEST(GameItemGlowEffectsTest, OneChrome4ClockServesEveryCornerOfThePass)
{
    const auto clock = ItemShineClockOf(ShinePass::Chrome4, Seconds);

    EXPECT_EQ(ItemShineTexCoords(ShinePass::Chrome4, Up, clock), (glm::vec2{0x1.1a668p+1f, -0x1.6e4f28p-2f}));
    EXPECT_EQ(ItemShineTexCoords(ShinePass::Chrome4, Side, clock), (glm::vec2{0x1.f337cap+0f, 0x1.fe6c2p-2f}));
}
} // namespace
