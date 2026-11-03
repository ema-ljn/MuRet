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

// La letra del OZT, después de tolower (0x60d6eb).
constexpr char OztLetter = 't';
constexpr char ScriptStart = '_';
constexpr char ScriptEnd = '.';
// 0x45999d: de lo que va del '_' al punto mira cinco caracteres, el '_' y cuatro letras.
constexpr std::size_t ScriptLetters = 4;

// El tolower de la "C" del original, para ASCII: no cambia los bytes de 0x80 en adelante.
constexpr char LowerAscii(char letter)
{
    return letter >= 'A' && letter <= 'Z' ? static_cast<char>(letter - 'A' + 'a') : letter;
}

struct SkinPrefix
{
    std::string_view prefix;
    std::uint16_t slot = 0;
};

// En el orden en que los mira 0x60d5f0.
constexpr std::array<SkinPrefix, 4> SkinPrefixes = {{
    {"ski", SkinTextureSlot},
    {"level", SkinTextureSlot},
    {"hid", HiddenTextureSlot},
    {"hair", HairTextureSlot},
}};

// Prende en `script` lo que pide `letter`. false si no es una de las cuatro.
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
    // Sin punto, el original lee el byte que sigue al 0 del nombre, que el string no tiene: el OZJ,
    // como con ese byte en 0 (Q-C107). Con el punto al final, la letra es el 0.
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
