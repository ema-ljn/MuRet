#include "Tooltips/GameHelps.h"

#include "Core/Expected.h"
#include "Core/PixelSize.h"
#include "Tables/ItemSetTables.h"
#include "Tables/ItemTable.h"
#include "Tables/TextTable.h"
#include "Math/X87.h"
#include "Render/ScreenScale.h"
#include "Render/SpriteBatch.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Ui/GameHudContext.h"
#include "Tooltips/GameTooltips.h"
#include "Items/HudItems.h"
#include "Tooltips/ItemHelpLevels.h"
#include "Tooltips/ItemTooltipParts.h"
#include "Tooltips/TooltipLines.h"
#include "Tooltips/TooltipWriter.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Ui/PanelRegistry.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

// The item help (06 § HUD y ventanas, "La ayuda del ítem"): mode 3 of 0x5b0550, from 0x5b0802, with the per-level
// table (ItemHelpLevels.h), the top box (0x5af090, framed) and the columns (0x5afb00 and 0x5af9b0).
namespace Client::Tooltips
{
namespace
{
using Record = std::span<const std::byte, Tables::ItemRecordSize>;

// 0x5b089c..0x5b0944: the arrows and the bolts close the help; the type ranges.
constexpr std::int32_t BoltsItem = 0x807;
constexpr std::int32_t ArrowsItem = 0x80f;
constexpr std::int32_t FifthKind = 5;
// 0x5b0932..0x5b0944 and 0x5b0ae2..0x5b0aee: the fifth range, the scrolls'.
constexpr std::int32_t FirstScroll = 0x1e00;
constexpr std::int32_t ScrollsEnd = 0x2000;

// 0x5b0aa5: the top box is the one of the F1 helps, top left, centered and framed.
constexpr std::int32_t HelpCorner = 1;
constexpr std::int32_t CenteredMode = 2;
constexpr std::int32_t RightAlignedMode = 3;
// 0x5b09d3: the title, "\n" and the lines of 0x6c9758..0x6c9768.
constexpr std::size_t TitleText = 160;
constexpr std::string_view LineBreak = "\n";
constexpr std::string_view BlankLine = " ";
// 0x5b0bf2..0x5b0c0a: 46 spaces, 51 with a width above 800.
constexpr std::size_t NarrowSpaces = 46;
constexpr std::size_t WideSpaces = 51;
constexpr std::int32_t WideScreen = 800;
// 0x5b0983: the blank lines for the columns' 14 rows (none in the scrolls).
constexpr std::int32_t BlankRows = 13;
// 0x5b0cb3 and 0x5b0d29: each piece's set with its mix from 0 to 3, in green: "%s %s %s:(%s+%d)" with its name, 1089,
// 591 and 1090.
constexpr std::uint8_t LastMix = 3;
constexpr std::string_view SetFormat = "%s %s %s:(%s+%d)";
constexpr std::size_t SetText = 1089;
constexpr std::size_t MixText = 591;
constexpr std::size_t MixLevelText = 1090;
constexpr std::size_t LowMixByte = 2;
constexpr std::size_t HighMixByte = 3;
constexpr std::uint8_t FirstPiece = 1;
constexpr std::uint8_t SecondPiece = 2;

// 0x5b0ac7..0x5b0bf0: the box width, in columns of divisor · 17 / 90 (0x6b87d4, 0x6b8818, 0x6b8830 and 0x6b882c).
constexpr double OneColumn = 1.0;
constexpr double TwoColumns = 2.0;
constexpr float WideColumn = 1.1f;
constexpr float Rounding = 0.5f;
constexpr std::int32_t ColumnNumerator = 17;
constexpr std::int32_t ColumnDenominator = 90;

// 0x5af9b0: 14 rows, one in the scrolls.
constexpr std::int32_t LastRow = 13;
// 0x5b0e33 and 0x5b0e63: how far x moves around the required level.
constexpr std::int32_t BeforeRequiredLevel = 2;
constexpr std::int32_t AfterRequiredLevel = 10;

struct ItemKind
{
    std::int32_t kind = 0;
    std::int32_t dividend = 0;
};

// 0x5b08b4..0x5b0975: the item's range and what divides by the screen width; without a range, nothing.
std::optional<ItemKind> KindOf(std::int32_t item, std::int32_t screenWidth)
{
    if (item >= 0 && item < 0xa00)
        return ItemKind{.kind = 1, .dividend = 0x870};
    if (item >= 0xa00 && item < 0xc00)
        return ItemKind{.kind = 2, .dividend = 0x708};
    if (item >= 0xc00 && item < 0xe00)
        return ItemKind{.kind = 3, .dividend = 0x708};
    if (item >= 0xe00 && item < 0x1800)
        return ItemKind{.kind = 4, .dividend = 0x708};
    if (item >= FirstScroll && item < ScrollsEnd)
    {
        // 0x5b094f..0x5b0975: 0x1734 at 640 and 1280, 0x1450 at 800 and 1024 and, with another, no division.
        if (screenWidth == 640 || screenWidth == 1280)
            return ItemKind{.kind = FifthKind, .dividend = 0x1734};
        if (screenWidth == 800 || screenWidth == 1024)
            return ItemKind{.kind = FifthKind, .dividend = 0x1450};
        return ItemKind{.kind = FifthKind, .dividend = 0};
    }
    return std::nullopt;
}

// 0x5b0807..0x5b088c: per screen width, the divisor (0x6d5d60), the titles' y and the rows' y; with another width, all
// at 0.
struct HelpLayout
{
    std::int32_t divisor = 0;
    std::int32_t titleY = 0;
    std::int32_t rowY = 0;
};

HelpLayout LayoutOf(std::int32_t screenWidth)
{
    switch (screenWidth)
    {
    case 640:
        return HelpLayout{.divisor = 0x5a, .titleY = 0x26, .rowY = 0x34};
    case 800:
        return HelpLayout{.divisor = 0x5a, .titleY = 0x21, .rowY = 0x2f};
    case 1024:
        return HelpLayout{.divisor = 0x67, .titleY = 0x1c, .rowY = 0x28};
    case 1280:
        return HelpLayout{.divisor = 0x7b, .titleY = 0x16, .rowY = 0x20};
    default:
        return HelpLayout{};
    }
}

bool IsScroll(std::int32_t item)
{
    return item >= FirstScroll && item < ScrollsEnd;
}

using Math::Truncate;

// 0x5b0ac7..0x5b0bf0: the width passed to the box, from level 0's row, in double (the x87 with 53 bits): one column,
// or two with the min damage (except in the scrolls), plus one per drawn column (1.1 the wide ones). Never less than
// the divisor.
std::int32_t HelpWidth(const ItemHelpRow& first, std::int32_t item, std::int32_t divisor)
{
    const bool scroll = IsScroll(item);
    const auto wide = static_cast<double>(WideColumn);
    double columns = first[ItemHelpMinDamage] > 0 && !scroll ? TwoColumns : OneColumn;
    if (first[ItemHelpMaxDamage] > 0 && !scroll)
        columns += OneColumn;
    if (first[ItemHelpMagic] > 0)
        columns += OneColumn;
    if (first[ItemHelpScepterMagic] > 0)
        columns += OneColumn;
    if (first[ItemHelpDefense] > 0)
        columns += wide;
    if (first[ItemHelpDefenseRate] > 0)
        columns += wide;
    if (first[ItemHelpStrength] > 0)
        columns += OneColumn;
    if (first[ItemHelpAgility] > 0 || item < FirstScroll)
        columns += OneColumn;
    if (first[ItemHelpEnergy] > 0)
        columns += wide;
    if (first[ItemHelpRequiredLevel] > 0)
        columns += wide;
    if (first[ItemHelpVitality] > 0)
        columns += wide;
    if (first[ItemHelpLeadership] > 0)
        columns += wide;
    const std::int32_t step = divisor * ColumnNumerator / ColumnDenominator;
    const std::int32_t width = Truncate(static_cast<double>(step) * columns + static_cast<double>(Rounding));
    return std::max(divisor, width);
}

// 0x5b0c97..0x5b0d9f: 0x40cf00(item), bytes 2 and 3 of ItemSetType.bmd, and one line per piece (1 with 3 and 2 with 2)
// with its mix from 0 to 3 and a set (0x40cf30).
Core::Expected<void, std::string> WriteTheSetLines(Ui::GameHudContext& context, TooltipWriter& lines,
                                                   std::int32_t item)
{
    if (context.setTypes == nullptr)
        return Core::Unexpected(std::format("0x40cf13: la ayuda del ítem {:#x} sin ItemSetType.bmd", item));
    const auto type = static_cast<std::uint16_t>(item);
    const auto mixes = context.setTypes->Record(type);
    if (!mixes)
        return Core::Unexpected(std::format("0x40cf13: el tipo {:#x} lee fuera de ItemSetType.bmd", item));
    const std::array<std::pair<std::uint8_t, std::uint8_t>, 2> pieces{
        std::pair{FirstPiece, std::to_integer<std::uint8_t>((*mixes)[HighMixByte])},
        std::pair{SecondPiece, std::to_integer<std::uint8_t>((*mixes)[LowMixByte])}};
    for (const auto& [piece, mix] : pieces)
    {
        if (mix > LastMix)
            continue;
        const auto name = ItemSetNameOf(context, type, piece);
        if (!name.HasValue())
            return Core::Unexpected(name.Error());
        if (!name.Value())
            continue;
        lines.At(piece == FirstPiece ? "0x5b0d16" : "0x5b0d91");
        lines.Color(TooltipGreen);
        lines.Print(SetFormat, std::string_view(*name.Value()), lines.Text(SetText), lines.Text(MixText),
                    lines.Text(MixLevelText), static_cast<std::int32_t>(mix));
        lines.Next();
    }
    return {};
}

// 0x5b09b0..0x5b0dea: the top box's lines, from 0, and the box. The title is copied with strcpy and the item's name
// with "%s"; the "\n" and " " do not touch their color or their bold.
Core::Expected<void, std::string> DrawTheHelpBox(Ui::GameHudContext& context, std::int32_t item,
                                                 std::int32_t blankRows, Record record, std::int32_t width)
{
    TooltipLines& written = context.hud.tooltip;
    written.next = 0;
    TooltipWriter lines(written, context.texts);
    lines.At("0x5b09c4");
    lines.Print(LineBreak);
    lines.Next();
    if (std::string* line = lines.Current(); line != nullptr)
        *line = std::string(lines.Text(TitleText));
    lines.Color(TooltipBlue);
    lines.Bold(1);
    lines.Next();
    lines.At("0x5b0a34");
    const std::string name = Items::ItemRecordName(record);
    lines.Print("%s", std::string_view(name));
    lines.Color(TooltipWhite);
    lines.Bold(1);
    lines.Next();
    for (const std::string_view text : {LineBreak, BlankLine, LineBreak})
    {
        lines.Print(text);
        lines.Next();
    }
    // 0x5b0acc: 0x77f3b66 at 1.
    context.hud.durabilityWarning = 1;
    lines.At("0x5b0c25");
    const std::size_t spaces = context.sprites.Screen().width > WideScreen ? WideSpaces : NarrowSpaces;
    if (std::string* line = lines.Current(); line != nullptr)
        *line = std::string(spaces, ' ');
    lines.Color(TooltipWhite);
    lines.Bold(0);
    lines.Next();
    // 0x5b0c5a..0x5b0c8f: the colors go one below the lines: the first one's stays as it was and the one after the
    // last, at 0.
    for (std::int32_t row = 0; row < blankRows; ++row)
    {
        lines.Bold(0);
        lines.Print(BlankLine);
        lines.Next();
        lines.Color(TooltipWhite);
    }
    if (lines.Failed())
        return Core::Unexpected(lines.Error());
    if (auto set = WriteTheSetLines(context, lines, item); !set.HasValue())
        return set;
    WriteTheItemClasses(context, lines, record);
    lines.At("0x5b0dc8");
    lines.Print(LineBreak);
    lines.Next();
    if (lines.Failed())
        return Core::Unexpected(lines.Error());
    return DrawTheTooltipBox(context, HelpCorner, HelpCorner, written.next, width, CenteredMode, 1);
}

// 0x5afb00: the Text.bmd text of each column's title; 1, 11, 12 and those above 15, none (NULL, and sprintf cuts at
// 0x5afb7a), although 0x5b0550 does not ask for them.
std::optional<std::size_t> ColumnTitleText(ItemHelpColumn column)
{
    switch (column)
    {
    case ItemHelpLevel:
        return 161;
    case ItemHelpMinDamage:
    case ItemHelpMaxDamage:
        return 162;
    case ItemHelpMagic:
        return 163;
    case ItemHelpScepterMagic:
        return 1239;
    case ItemHelpDefense:
        return 164;
    case ItemHelpDefenseRate:
        return 165;
    case ItemHelpStrength:
        return 166;
    case ItemHelpAgility:
        return 167;
    case ItemHelpEnergy:
        return 168;
    case ItemHelpLeadership:
        return 1900;
    case ItemHelpVitality:
        return 169;
    case ItemHelpRequiredLevel:
        return 1931;
    default:
        return std::nullopt;
    }
}

// The columns of 0x5b0dfb..0x5b10b6, left to right from the x of 0x5b097d: each one is a title (0x5afb00) and its rows
// (0x5af9b0), with frameless boxes against the edge. The first error cuts what follows.
class HelpColumns
{
public:
    HelpColumns(Ui::GameHudContext& context, std::int32_t x, const HelpLayout& layout)
        : m_context(context), m_x(x), m_layout(layout)
    {
    }

    // 0x5afb00(column, x, y): the title as a sprintf format, with no arguments, on line 0 (0x78abb30), with color 1 and
    // whatever bold was left, and that line's box at (x, y). Then, 0x78abb30 at 0.
    void Title(ItemHelpColumn column)
    {
        if (!m_error.empty())
            return;
        const std::optional<std::size_t> text = ColumnTitleText(column);
        if (!text)
        {
            m_error = std::format("0x5afb7a: la columna {} de la ayuda del ítem no tiene título (sprintf con NULL)",
                                  static_cast<std::size_t>(column));
            return;
        }
        TooltipLines& written = m_context.hud.tooltip;
        TooltipWriter lines(written, m_context.texts);
        lines.At("0x5afb7a");
        lines.PrintText(*text);
        lines.Color(TooltipBlue);
        lines.Next();
        if (lines.Failed())
        {
            m_error = lines.Error();
            return;
        }
        Keep(DrawTheTooltipBox(m_context, m_x, m_layout.titleY, written.next, 0, RightAlignedMode, 0));
        written.next = 0;
    }

    // 0x5af9b0(column, format, &x, sample, y, range): the column's 14 rows (one with range 5) from line 0x78abb30, with
    // `format` and each level's value; the color (0 if the own record meets the level, 2 if not) and bold at 0 go per
    // row, not per line. The box at (x, y) and, with the font it leaves, x grows by what `sample` / rx measures.
    // 0x78abb30 goes back to where it was.
    void Column(ItemHelpColumn column, std::string_view format, std::string_view sample, std::int32_t kind = 0)
    {
        if (!m_error.empty())
            return;
        TooltipLines& written = m_context.hud.tooltip;
        const ItemHelpLevels& levels = m_context.hud.itemHelp;
        const std::int32_t last = kind == FifthKind ? 0 : LastRow;
        TooltipWriter lines(written, m_context.texts);
        lines.At("0x5afa01");
        for (std::int32_t row = 0; row <= last; ++row)
        {
            const ItemHelpRow& values = levels.rows[static_cast<std::size_t>(row)];
            lines.Print(format, values[column]);
            written.colors[static_cast<std::size_t>(row)] = values[ItemHelpUsable] == 1 ? TooltipWhite : TooltipRed;
            written.bold[static_cast<std::size_t>(row)] = 0;
            lines.Next();
        }
        if (lines.Failed())
        {
            m_error = lines.Error();
            return;
        }
        Keep(DrawTheTooltipBox(m_context, m_x, m_layout.rowY, written.next, 0, RightAlignedMode, 0));
        const Core::PixelSize screen = m_context.sprites.Screen();
        const Render::Text::TextExtent extent = m_context.text.Measure(m_context.hud.dcFont, sample);
        m_x += Truncate(static_cast<double>(extent.width) / static_cast<double>(Render::ScaleX(1.0f, screen)));
        written.next -= last + 1;
    }

    // x moves by itself between columns (0x5b0e37 and 0x5b0e63).
    void Advance(std::int32_t step)
    {
        m_x += step;
    }

    [[nodiscard]] Core::Expected<void, std::string> Result() const
    {
        if (!m_error.empty())
            return Core::Unexpected(m_error);
        return {};
    }

private:
    void Keep(const Core::Expected<void, std::string>& drawn)
    {
        if (!drawn.HasValue())
            m_error = drawn.Error();
    }

    Ui::GameHudContext& m_context;
    std::int32_t m_x = 0;
    HelpLayout m_layout;
    std::string m_error;
};

// 0x5b0dfb..0x5b10b6: the columns that level 0's row has, in the original's order.
Core::Expected<void, std::string> DrawTheColumns(Ui::GameHudContext& context, std::int32_t item, std::int32_t kind,
                                                 std::int32_t x, const HelpLayout& layout)
{
    const ItemHelpRow& first = context.hud.itemHelp.rows[0];
    const bool scroll = IsScroll(item);
    context.hud.tooltip.next = 0;
    HelpColumns columns(context, x, layout);
    columns.Title(ItemHelpLevel);
    columns.Column(ItemHelpLevel, "+%d", "000000", kind);
    if (first[ItemHelpRequiredLevel] > 0)
    {
        columns.Advance(BeforeRequiredLevel);
        columns.Title(ItemHelpRequiredLevel);
        columns.Column(ItemHelpRequiredLevel, "%3d", "00000");
        columns.Advance(AfterRequiredLevel);
    }
    if (first[ItemHelpMinDamage] > 0 && !scroll)
    {
        // 0x5b0e85..0x5b0ebc: the minimum, the title of both at the next x and the "~" column (the level's).
        columns.Column(ItemHelpMinDamage, "%3d", "00 ");
        columns.Title(ItemHelpMinDamage);
        columns.Column(ItemHelpLevel, "~", " 00");
    }
    if (first[ItemHelpMaxDamage] > 0 && !scroll)
        columns.Column(ItemHelpMaxDamage, "%3d", "00000");
    // 0x5b0efb..0x5b10b3: the others, each with its title.
    struct Titled
    {
        ItemHelpColumn column;
        std::string_view format;
        std::string_view sample;
    };
    const auto titled = [&](const Titled& next, std::int32_t withKind = 0)
    {
        columns.Title(next.column);
        columns.Column(next.column, next.format, next.sample, withKind);
    };
    if (first[ItemHelpMagic] > 0)
        titled({ItemHelpMagic, "%2d%%", "00000"});
    if (first[ItemHelpScepterMagic] > 0)
        titled({ItemHelpScepterMagic, "%2d%%", "00000"});
    if (first[ItemHelpDefense] > 0)
        titled({ItemHelpDefense, "%3d", "000000"});
    if (first[ItemHelpDefenseRate] > 0)
        titled({ItemHelpDefenseRate, "%3d%%", "000000"});
    if (first[ItemHelpStrength] > 0)
        titled({ItemHelpStrength, "%3d", "00000"});
    if (first[ItemHelpAgility] > 0 || item < FirstScroll)
        titled({ItemHelpAgility, "%3d", "00000"});
    if (first[ItemHelpVitality] > 0)
        titled({ItemHelpVitality, "%3d", "00000"});
    if (first[ItemHelpEnergy] > 0)
        titled({ItemHelpEnergy, "%3d", "00000"}, kind);
    if (first[ItemHelpLeadership] > 0)
        titled({ItemHelpLeadership, "%3d", "00000"});
    return columns.Result();
}
} // namespace

Core::Expected<void, std::string> DrawTheItemHelp(Ui::GameHudContext& context)
{
    // 0x5b0802.
    context.sprites.SetBlend(Render::BlendMode::Opaque);
    Ui::PanelState& panels = context.world.Runtime().panels;
    const std::int32_t item = panels.helpItem;
    const std::int32_t screenWidth = context.sprites.Screen().width;
    const std::optional<ItemKind> kind =
        item == BoltsItem || item == ArrowsItem ? std::nullopt : KindOf(item, screenWidth);
    if (!kind)
    {
        // 0x5b10c2: 0x77f3b08 at 0.
        if (!context.projecting)
            panels.overlayMode = 0;
        return {};
    }
    const HelpLayout layout = LayoutOf(screenWidth);
    if (kind->dividend != 0 && layout.divisor == 0)
        return Core::Unexpected(std::format(
            "0x5b097b: la ayuda del ítem {} divide por 0 con una pantalla de {} de ancho (Q-C615)", item, screenWidth));
    // 0x5b097a..0x5b098c: the first column's x and the blank lines.
    const std::int32_t x = kind->dividend != 0 ? kind->dividend / layout.divisor : 0;
    const std::int32_t blankRows = kind->kind == FifthKind ? 0 : BlankRows;
    if (context.items == nullptr)
        return Core::Unexpected(std::format("0x5b098f: la ayuda del ítem {} sin Item.bmd", item));
    const auto record = context.items->Record(static_cast<std::size_t>(item));
    if (!record)
        return Core::Unexpected(std::format("0x5b098f: el ítem {} lee fuera de Item.bmd", item));
    // 0x5b09f0..0x5b0a0b: the title's strcpy; one of 100 bytes or more continues on the line below (Q-C613).
    const std::optional<std::string_view> title =
        context.texts == nullptr ? std::nullopt : context.texts->Text(TitleText);
    if (!title)
        return Core::Unexpected(std::format("0x5b09f0: la ayuda del ítem sin el texto {} de Text.bmd", TitleText));
    if (title->size() >= TooltipLineBytes)
        return Core::Unexpected(
            std::format("0x5b0a03: el texto {} ({} bytes) no entra en un renglón de la ayuda del ítem (Q-C613)",
                        TitleText, title->size()));
    // 0x5b09a8: the per-level table, which only refills with another item.
    FillTheItemHelpLevels(context, item, *record);
    const std::int32_t width = HelpWidth(context.hud.itemHelp.rows[0], item, layout.divisor);
    if (auto box = DrawTheHelpBox(context, item, blankRows, *record, width); !box.HasValue())
        return box;
    // 0x5b0dea: 0x620a00(1).
    context.sprites.SetBlend(Render::BlendMode::Alpha);
    if (auto columns = DrawTheColumns(context, item, kind->kind, x, layout); !columns.HasValue())
        return columns;
    // 0x5b10b6.
    context.sprites.SetBlend(Render::BlendMode::Opaque);
    return {};
}
} // namespace Client::Tooltips
