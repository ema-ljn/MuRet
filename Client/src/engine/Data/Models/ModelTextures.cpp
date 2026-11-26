#include "Data/Models/ModelTextures.h"

#include "Data/Textures/TextureSet.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace Client::Data::Models
{
namespace
{
using Textures::TextureLoader;

// The OZT letter, after tolower (0x60d6eb).
constexpr char OztLetter = 't';
constexpr char ScriptStart = '_';
constexpr char ScriptEnd = '.';
// 0x45999d: from the '_' to the dot it looks at five characters, the '_' and four letters.
constexpr std::size_t ScriptLetters = 4;

// The original's tolower in the "C" locale: bytes 0x80 and up stay as they are.
constexpr char LowerAscii(char letter)
{
    return letter >= 'A' && letter <= 'Z' ? static_cast<char>(letter - 'A' + 'a') : letter;
}

struct SkinPrefix
{
    std::string_view prefix;
    std::uint16_t slot = 0;
};

// In the order 0x60d5f0 checks them.
constexpr std::array<SkinPrefix, 4> SkinPrefixes = {{
    {"ski", SkinTextureSlot},
    {"level", SkinTextureSlot},
    {"hid", HiddenTextureSlot},
    {"hair", HairTextureSlot},
}};

// false if it is not one of the four.
bool ApplyScriptLetter(char letter, TextureScript& script)
{
    switch (letter)
    {
    case 'R':
        script.blendMesh = true;
        return true;
    case 'H':
        script.hidden = true;
        return true;
    case 'S':
        script.scrollUv = true;
        return true;
    case 'N':
        script.withoutShine = true;
        return true;
    default:
        return false;
    }
}
} // namespace

TextureLoader ModelTextureLoaderOf(std::string_view textureName)
{
    // Without a dot, the original reads the byte after the name's 0, which the string lacks: the OZJ, as with that byte
    // at 0 (Q-C107). With the dot at the end, the letter is the 0.
    const std::size_t dot = textureName.find(ScriptEnd);
    if (dot == std::string_view::npos || dot + 1 >= textureName.size())
    {
        return TextureLoader::Ozj;
    }
    return LowerAscii(textureName[dot + 1]) == OztLetter ? TextureLoader::Ozt : TextureLoader::Ozj;
}

std::optional<std::uint16_t> SkinSlotOf(std::string_view textureName)
{
    for (const SkinPrefix& skin : SkinPrefixes)
    {
        if (textureName.starts_with(skin.prefix))
        {
            return skin.slot;
        }
    }
    return std::nullopt;
}

std::optional<TextureScript> TextureScriptOf(std::string_view textureName)
{
    const std::size_t start = textureName.find(ScriptStart);
    if (start == std::string_view::npos)
    {
        return std::nullopt;
    }
    std::string_view letters = textureName.substr(start + 1);
    letters = letters.substr(0, letters.find(ScriptEnd)).substr(0, ScriptLetters);
    if (letters.empty())
    {
        return std::nullopt;
    }
    TextureScript script;
    for (const char letter : letters)
    {
        if (!ApplyScriptLetter(letter, script))
        {
            return std::nullopt;
        }
    }
    return script;
}
} // namespace Client::Data::Models
