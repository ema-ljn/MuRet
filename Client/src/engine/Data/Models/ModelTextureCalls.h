#pragma once

#include "Data/Textures/Sampling.h"
#include "Data/Textures/TextureSet.h"
#include "EngineApi.h"

#include <cstdint>
#include <span>
#include <string_view>

// As 0x60f7d0 (load step 2 of 0x61ccb0) and 0x611850 (step 4) ask for them
// (Client/docs/research/06-render-escenas-y-ui.md, "El modelo en el binario"). Each loader picks which models get
// textures, in which order and from which folder, not always the .bmd's (Player\'s Helpers take them from Item\;
// 0x1c16, from Skill\). An emulation of both loaders on main.exe c32bbd5f gives the same 505 calls in the same order.
namespace Client::Data::Models
{
// A call to 0x60d5f0; the folder keeps its trailing slash.
struct ModelTextureCall
{
    std::uint16_t model = 0;
    std::string_view folder;
    Textures::Wrap wrap = Textures::Wrap::Repeat;
};

// The default: 0x2901 and 0x2600, repeating and unfiltered. OZTs are always unfiltered (0x60d5f0 passes 0x2600 to
// 0x63bd00).
inline constexpr Textures::Sampling ModelTextureSampling{Textures::Filter::Nearest, Textures::Wrap::Repeat};

// The 214 of 0x60f7d0, after its 0x60d530(0x138), all with Player\.
[[nodiscard]] ENGINE_API std::span<const ModelTextureCall> PlayerTextureCalls();

// The 291 of 0x611850, after its 0x60d530(0x25a): with Item\, except 0x1c16, 0x1c17 and 0x1c15 (Skill\), 0x224c
// (Monster\), 0x1c2f (Npc\) and 0xdb (Item\Bow\, before loading the BMD of 0x616095). 26 models are requested twice,
// and the second call finds the first one's names.
[[nodiscard]] ENGINE_API std::span<const ModelTextureCall> ItemTextureCalls();

// The 45 explicit lookups of 0x615700 after the reset to 0x3b8, before the be..150 sweep. 0x113, 0x11a and 0x11b use
// GL_CLAMP; the rest, GL_REPEAT.
[[nodiscard]] ENGINE_API std::span<const ModelTextureCall> SkillTextureCalls();

// The Fenrir (0x14a to 0x14c), their bolt (0x14d) and the electric arrow (0xf9) in the block after 0x611850 (0x371),
// before 0x615700's slot changes.
[[nodiscard]] ENGINE_API std::span<const ModelTextureCall> FenrirTextureCalls();

// What 0x60f7d0 uploads by hand with 0x63b870 and 0x63bd00, unfiltered and clamped, in fixed slots that neither keep
// the name nor advance the next slot: each class's skin and second level and the hair (0x12e to 0x137; 0x12d and 0x135
// stay empty) and the four layers (0x250, 0x251, 0x252 and 0x257).
[[nodiscard]] ENGINE_API std::span<const Textures::TextureSetEntry> PlayerFixedTextures();
} // namespace Client::Data::Models
