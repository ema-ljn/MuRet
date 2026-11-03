#include "Render/Text/InlineColors.h"

namespace Client::Render::Text
{
TextColors InlineColor(std::uint8_t code, const InlineColorContext& context)
{
    // La tabla de saltos de 0x4665d0 (0x46667c, con los índices en 0x4666a4).
    switch (code)
    {
    case 0x01:
    case 0xf4:
        return {0xff0000ff, 0};
    case 0xf0:
        return context.scene;
    case 0xf1:
        return {context.scene.background, context.scene.text};
    case 0xf2:
        return {0xffffc8dc, 0};
    case 0xf3:
        return {0xff00ffc8, 0};
    case 0xf5:
        return {0xff503214, 0};
    case 0xf6:
        return {0xff00643c, 0};
    case 0xf7:
        return {0xff000064, 0};
    case 0xf8:
        return {0xffc8dce6, context.f8WithoutBackground ? 0u : 0x96000000u};
    default:
        return {0xffffffff, 0};
    }
}

InlineText ParseInlineColors(std::string_view text, const InlineColorContext& context)
{
    // strlen: el texto termina en el primer 0.
    text = text.substr(0, text.find('\0'));

    InlineText parsed;
    parsed.text.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] != InlineColorMarker)
        {
            parsed.text.push_back(text[i]);
            continue;
        }
        // El código es el byte que sigue, o el 0 del final (0x4667fe lee text[i + 1]). El par sale
        // del texto aunque ya haya nueve tramos (0x46687b..0x466894).
        const std::uint8_t code = i + 1 < text.size() ? static_cast<std::uint8_t>(text[i + 1]) : std::uint8_t{0};
        if (parsed.runs.size() < MaxInlineColors)
        {
            parsed.runs.push_back(ColorRun{parsed.text.size(), code, InlineColor(code, context)});
        }
        ++i;
    }
    return parsed;
}
} // namespace Client::Render::Text
