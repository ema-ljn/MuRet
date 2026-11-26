#pragma once

#include "Core/IClock.h"
#include "EngineApi.h"
#include "Models/Shine.h"
#include "Render/SpriteVertex.h"

#include <glm/ext/vector_float3.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>

// The color of each glow pass of a part or of what hangs, as 0x60b3d0 sets it before each RenderBody
// (Client/docs/research/06-render-escenas-y-ui.md, "Los pases de brillo"). The three tables come from emulating
// 0x608e00, 0x6093d0 and 0x609560 on the c32bbd5f main.exe for each table model, with their arguments at 1.
namespace Client::Models
{
// The one of passes 0x44 and 0x48 (0x60b130 → 0x608e00, with the object's alpha at 1): one per item, ignoring the
// light. Almost all (1; 0.5; 0); the Male17 ones, (0.75; 0.65; 0.5).
[[nodiscard]] ENGINE_API Render::Color ShineColorOf(std::uint16_t model);

// What 0x6093d0 (from 0x60b300) multiplies the base color by in passes 0x240 and 0x1040: almost all by 1, and the
// pass keeps the light times the level factor.
[[nodiscard]] ENGINE_API glm::vec3 MetalFactorOf(std::uint16_t model);

// The set pass one, 0x840 (0x60b300 → 0x609560): (0.1; 0.6; 1), except 15 parts (1; 0.7; 0.2).
[[nodiscard]] ENGINE_API Render::Color SetColorOf(std::uint16_t model);

// The one of the excellent's 0x42 pass: k = (sin(0.002 · T) + 1) · 0.5 and (k, 0.3k, 1 − k), with T the world clock
// in ms (DAT_05767b88).
[[nodiscard]] ENGINE_API Render::Color ExcellentColorAt(Core::Duration clock);

// The set pass pulse on the x87 (0x60cbff..0x60cc13), before it is stored: sin(0.001 · T) · 0.5 + 0.4, with T the
// world clock in ms (the float at 0x5767b88) and the math in double (PC53).
[[nodiscard]] ENGINE_API double SetPulseAt(double milliseconds);

// 0x60cc1c: FCOMP of the x87 pulse, not rounded to float, against the 0.01 of 0x6b88d4. Off (less or equal), the set
// pass draws its rand() % 100 (0x60cc29).
[[nodiscard]] ENGINE_API bool SetPulseIsOff(double pulse);

// The alpha 0x60b3d0 calls the set pass with: the pulse stored in a float (the FST at 0x60cc19). Since it is below
// 0.99, 0x4ecc30 multiplies the model's color by it, in place, on each mesh that pass draws: the first goes by a, the
// second by a² (Q-C95).
[[nodiscard]] ENGINE_API float SetAlphaAt(Core::Duration clock);

// The mesh 0x60b130 (passes 0x48 and 0x44) and 0x60b300 (0x240, 0x1040 and set) pass to RenderBody as hidden: 1 for
// 0xc17 and 0x226 and 0 for 0x227 in all; and in those of 0x60b130, 2 for 0x619 and the object's (`objectHidden`, its
// +0x58) for 0x231, 0x81c, 0xe22 and 0x61a..0x61f. The rest, none (−1).
[[nodiscard]] ENGINE_API std::optional<std::size_t> PassHiddenMesh(std::uint16_t model, ShinePass pass,
                                                                   std::int16_t objectHidden);
} // namespace Client::Models
