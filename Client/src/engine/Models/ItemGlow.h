#pragma once

#include "EngineApi.h"
#include "Models/Shine.h"

#include <cstdint>
#include <optional>
#include <vector>

// How much a part glows by its level, like 0x60b3d0 (Client/docs/research/06-render-escenas-y-ui.md, "El modelo en el
// binario", Q-C97): the base pass color factor and the passes that follow, in order. *Static*; level 13 (the base,
// 141, 115 and 120) is **verified** in the trace.
namespace Client::Models
{
// A glow pass: one with the normal's UVs (ShinePass), or, without it, the 0x42 of an excellent item, which repeats the
// mesh with its texture, additive.
struct GlowPass
{
    std::optional<ShinePass> shine;

    friend bool operator==(const GlowPass&, const GlowPass&) = default;
};

struct ItemGlow
{
    // Times the character's light, in the base pass color.
    float baseFactor = 1.0f;
    std::vector<GlowPass> passes;
};

// The options byte of 0x60b3d0 (DAT_006c9dcc): 4 in .data.
inline constexpr std::uint8_t DefaultGlowDetail = 4;

// 0x60b3d0 with the level already clamped to 2 · detail + 5 if detail is below 4: 7 and 8 give 0x44 (Chrome) and
// color · 0.8; 9 and 10 also 0x48 (Shiny), with · 0.9; 11 and 12, 0x240 (Chrome2), 0x48 and 0x44; 13, 0x1040
// (Chrome4), 0x48 and 0x44; 14 and 15, none (Q-C97). An excellent item adds 0x42, and a non-excellent set item 0x840
// (Chrome3). Levels 3 to 6 pulse with DAT_07d36ba8 in the original: here, the color without pulsing (P-C18).
[[nodiscard]] ENGINE_API ItemGlow ItemGlowOf(std::uint8_t level, bool excellent, bool ancient,
                                             std::uint8_t detail = DefaultGlowDetail);

// The level 0x60b3d0 draws bolts (0xa19) and arrows (0xa21) with: 0 stays 0 and the rest go to 2 · level + 1. From 14
// up, ItemGlowOf gives no passes (LAB_0060cb0f).
[[nodiscard]] constexpr std::uint8_t QuiverGlowLevel(std::uint8_t level)
{
    return level == 0 ? std::uint8_t{0} : static_cast<std::uint8_t>(2 * level + 1);
}
} // namespace Client::Models
