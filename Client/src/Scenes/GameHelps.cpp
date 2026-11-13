#include "Scenes/GameHelps.h"

#include "Core/Expected.h"
#include "Data/Text/TextTable.h"
#include "Render/SpriteBatch.h"
#include "Scenes/GameHud.h"
#include "Scenes/GameTooltips.h"
#include "Scenes/TooltipLines.h"
#include "Scenes/WorldRuntime.h"
#include "UI/PanelRegistry.h"

#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>

namespace Client::Scenes
{
namespace
{
// 0x6c9740..0x6c9754: el renglón "\n" de las ayudas.
constexpr std::string_view LineBreak = "\n";

// Los textos de cada ayuda de F1: el título y los renglones (0x76c5e7c, 0x76c5fa8..0x76c75ec; 0x76c75ec,
// 0x76c7718..0x76c89d8).
struct FunctionHelpTexts
{
    std::size_t title = 0;
    std::size_t first = 0;
    std::size_t last = 0;
};
constexpr FunctionHelpTexts FirstHelpTexts{.title = 120, .first = 121, .last = 139};
constexpr FunctionHelpTexts SecondHelpTexts{.title = 140, .first = 141, .last = 156};
// 0x5af090(1, 1, cuántos, 0, 2, 1).
constexpr std::int32_t HelpCorner = 1;
constexpr std::int32_t CenteredMode = 2;
// 0x5b05bf..0x5b05ce: el título, con el color 1 y la negrita.
constexpr std::int32_t TitleColor = 1;
} // namespace

Core::Expected<void, std::string> DrawTheFunctionHelp(GameHudContext& context, std::int32_t mode)
{
    if (context.texts == nullptr)
        return Core::Unexpected(std::format("0x5b0590: la ayuda {} de F1 sin Text.bmd", mode));
    const FunctionHelpTexts& help = mode == UI::FirstHelpMode ? FirstHelpTexts : SecondHelpTexts;
    // 0x5b056c y 0x5b0571..0x5b0581: sin mezcla y el renglón que sigue (0x78abb30) en 0.
    context.sprites.SetBlend(Render::BlendMode::Opaque);
    TooltipLines& lines = context.hud.tooltip;
    lines.next = 0;
    const auto next = [&lines]() { return static_cast<std::size_t>(lines.next++); };
    const auto add = [&](std::size_t index, std::int32_t color, std::int32_t bold) -> Core::Expected<void, std::string>
    {
        const std::optional<std::string_view> text = context.texts->Text(index);
        if (!text)
            return Core::Unexpected(std::format("0x5b0590: la ayuda {} de F1 sin el texto {}", mode, index));
        if (text->size() >= TooltipLineBytes)
            return Core::Unexpected(
                std::format("0x5b05bb: el texto {} ({} bytes) no entra en un renglón de la caja de ayuda (Q-C613)",
                            index, text->size()));
        const std::size_t line = next();
        lines.text[line] = std::string(*text);
        lines.colors[line] = color;
        lines.bold[line] = bold;
        return {};
    };
    lines.text[next()] = std::string(LineBreak);
    if (auto added = add(help.title, TitleColor, 1); !added.HasValue())
        return added;
    lines.text[next()] = std::string(LineBreak);
    for (std::size_t index = help.first; index <= help.last; ++index)
    {
        if (auto added = add(index, 0, 0); !added.HasValue())
            return added;
    }
    lines.text[next()] = std::string(LineBreak);
    // 0x5b0696 y 0x5b069d: 0x77f3b66 y la caja de los tooltips, arriba a la izquierda y con marco.
    context.hud.durabilityWarning = 1;
    return DrawTheTooltipBox(context, HelpCorner, HelpCorner, lines.next, 0, CenteredMode, 1);
}
} // namespace Client::Scenes
