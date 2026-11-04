#include "Render/Models/Shine.h"

#include "Core/IClock.h"

#include <gtest/gtest.h>

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <chrono>
#include <cmath>
#include <cstdint>

// Client::Render::Models::ShineTexCoords: las UV de los tres pases de brillo de 0x4ecc30, contra las
// fórmulas del 06.
namespace
{
using Client::Core::Duration;
using Client::Render::Models::ShinePass;
using Client::Render::Models::ShineTexCoords;

constexpr float Tolerance = 1e-5f;
constexpr glm::vec3 Normal{0.2f, -0.4f, 0.6f};

Duration Milliseconds(std::int64_t ms)
{
    return std::chrono::milliseconds{ms};
}

void ExpectNear(const glm::vec2& actual, const glm::vec2& expected)
{
    EXPECT_NEAR(actual.x, expected.x, Tolerance);
    EXPECT_NEAR(actual.y, expected.y, Tolerance);
}

// 0x8, la textura 115: la mitad de la normal, sin el reloj.
TEST(ShineTest, TheShinyPassIsHalfTheNormal)
{
    for (const std::int64_t ms : {std::int64_t{0}, std::int64_t{12345}, std::int64_t{26437420}})
    {
        ExpectNear(ShineTexCoords(ShinePass::Shiny, Normal, Milliseconds(ms)),
                   {0.5f * 0.6f + 0.2f, 0.5f * (-0.4f + 1)});
    }
}

// 0x4, la 120: corre con t = (T mod 10000) · 0,0001, y vuelve cada 10 s.
TEST(ShineTest, TheChromePassRunsWithTheClock)
{
    const float t = 2345 * 0.0001f;

    ExpectNear(ShineTexCoords(ShinePass::Chrome, Normal, Milliseconds(12345)), {0.5f * 0.6f + t, 0.5f * -0.4f + 2 * t});
    ExpectNear(ShineTexCoords(ShinePass::Chrome, Normal, Milliseconds(22345)),
               ShineTexCoords(ShinePass::Chrome, Normal, Milliseconds(12345)));
}

// 0x1000, la 141: con c = cos(0,001·T) y s = sin(0,002·T), y t.
TEST(ShineTest, TheChrome4PassUsesSinAndCos)
{
    const auto c = static_cast<float>(std::cos(1000 * 0.001));
    const auto s = static_cast<float>(std::sin(1000 * 0.002));
    const float t = 1000 * 0.0001f;

    ExpectNear(ShineTexCoords(ShinePass::Chrome4, Normal, Milliseconds(1000)),
               {c * 0.2f + (s + 0.5f) * -0.4f + 0.6f + 3 * s, 1 - c * 0.2f - s * -0.4f - 1.5f * 0.6f - 3 * t});
}

// 0x200 (los niveles 11 y 12, slot 0x620): u = (nz + nx) · 0,8 + 2f y v = nx + ny + 3f, con
// f = (T mod 5000) · 0,00024 − 0,4. A los 6.234 ms, T mod 5000 es 1.234.
TEST(ShineTest, TheChrome2PassRunsWithAFiveSecondClock)
{
    const float f = 1234 * 0.00024f - 0.4f;

    ExpectNear(ShineTexCoords(ShinePass::Chrome2, Normal, Milliseconds(6234)),
               {(0.6f + 0.2f) * 0.8f + 2 * f, 0.2f - 0.4f + 3 * f});
}

// 0x800 (un ítem de set, slot 0x620): la luz L = (0; −0,1; −0,8) contra la normal, u = L·n y
// v = 1 − L·n, sin el reloj.
TEST(ShineTest, TheChrome3PassIsTheLightAgainstTheNormal)
{
    const float dot = -0.1f * -0.4f - 0.8f * 0.6f;

    ExpectNear(ShineTexCoords(ShinePass::Chrome3, Normal, Milliseconds(1000)), {dot, 1 - dot});
    ExpectNear(ShineTexCoords(ShinePass::Chrome3, Normal, Milliseconds(7777)), {dot, 1 - dot});
}

// Q-C22: el original guarda T en un float, y en 2²⁴ + 1 ms ya no cuenta de a uno. Acá T es el reloj
// entero desde que arranca el cliente: un ms más corre la u del pase 120 lo de un ms.
TEST(ShineTest, TheClockCountsFromTheStart)
{
    constexpr std::int64_t Late = std::int64_t{1} << 24;

    const glm::vec2 before = ShineTexCoords(ShinePass::Chrome, Normal, Milliseconds(Late));
    const glm::vec2 after = ShineTexCoords(ShinePass::Chrome, Normal, Milliseconds(Late + 1));

    EXPECT_NEAR(after.x - before.x, 0.0001f, 1e-6f);
}

// Un reloj negativo cuenta 0.
TEST(ShineTest, ANegativeClockCountsZero)
{
    ExpectNear(ShineTexCoords(ShinePass::Chrome, Normal, Milliseconds(-5)),
               ShineTexCoords(ShinePass::Chrome, Normal, Milliseconds(0)));
}
} // namespace
