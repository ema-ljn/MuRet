#pragma once

#include "Data/Textures/TextureSet.h"
#include "EngineApi.h"

#include <cstdint>
#include <optional>
#include <string_view>

// What a mesh's texture name says, as the model reader and the slot allocator read it
// (Client/docs/research/05-formatos-de-datos.md, "Modelos .bmd"; 06, "El modelo en el binario"). Scene textures
// follow another rule: Textures::TextureLoaderOf.
namespace Client::Data::Models
{
// As 0x60d5f0: the letter after the first dot, with tolower (0x6a0cce); 't' is the OZT and any other the OZJ.
// "Monster\\bossmap1_R.jpg.jpg" is an OZJ and "x.Tga.jpg" an OZT. Without a dot the original looks at the file byte
// after the name's 0; MuRet gives the OZJ, what it gives with that byte at 0 (Q-C107).
[[nodiscard]] ENGINE_API Textures::TextureLoader ModelTextureLoaderOf(std::string_view textureName);

// The slots 0x60d5f0 sends a mesh to by the start of its texture's name, after loading it: "ski" and "level" the skin
// (0x12e), "hid" the mesh that is not drawn (0x12d) and "hair" the hair (0x137). Byte by byte and lowercase: "Hair"
// does not count. Any other, std::nullopt.
inline constexpr std::uint16_t SkinTextureSlot = 0x12e;
inline constexpr std::uint16_t HiddenTextureSlot = 0x12d;
inline constexpr std::uint16_t HairTextureSlot = 0x137;
[[nodiscard]] ENGINE_API std::optional<std::uint16_t> SkinSlotOf(std::string_view textureName);

// A mesh's script (0x459940): what follows the first '_' of its texture's name, up to the first dot. Each letter turns
// on a byte at mesh + 0x24 that the drawing reads (06).
struct TextureScript
{
    bool blendMesh = false;    // 'R', byte 0: the mesh is the blendMesh (0x4ef0c0)
    bool hidden = false;       // 'H', byte 1: not drawn (0x4ef0c0)
    bool scrollUv = false;     // 'S', byte 2: scrolls its UVs (0x4ecc30)
    bool withoutShine = false; // 'N', byte 3: no shine passes (0x4ecc30)
};

// std::nullopt without a script: no '_', no letters after it, or one of the four it looks at that is not an
// uppercase R, H, S or N (0x459a2f; the caller, 0x4f17f3, keeps it only if it returns 1). "Monster_R.jpg" is the
// blendMesh; "skin_barbarian_01.jpg" has no script.
[[nodiscard]] ENGINE_API std::optional<TextureScript> TextureScriptOf(std::string_view textureName);
} // namespace Client::Data::Models
