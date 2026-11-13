#include "Scenes/GameTooltips.h"

#include "App/WindowState.h"
#include "Core/Expected.h"
#include "Data/Tables/ItemSetTables.h"
#include "Data/Text/TextTable.h"
#include "Render/Models/LogicalAccess.h"
#include "Render/ScreenScale.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Render/Text/TextLayout.h"
#include "Render/Text/TextStyle.h"
#include "Scenes/GameHud.h"
#include "Scenes/GamePanels.h"
#include "Scenes/ItemSetOptionList.h"
#include "Scenes/ItemSetState.h"
#include "Scenes/ItemTooltips.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/OwnInventory.h"
#include "Scenes/SkillTooltip.h"
#include "Scenes/SocialState.h"
#include "Scenes/TooltipLines.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/GameText.h"
#include "UI/GameUiState.h"
#include "UI/PanelRegistry.h"
#include "UI/SprintfFormat.h"
#include "World/ItemState.h"
#include "World/OriginalFtol.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace Client::Scenes
{
namespace
{
using Render::BlendMode;
using Render::Color;

// 0x6ba26c: 0,90909088, el renglón de la caja es 1,1 veces su alto.
constexpr float LineHeightInverse = 0.90909088f;
// 0x6b8830: lo que baja cada renglón, en renglones.
constexpr float LineAdvance = 1.1f;
// 0x6b8a44 y 0x6b8818: el margen del ancho y lo que se le resta a la caja del texto.
constexpr double WidthMargin = 4.0;
constexpr double TextBoxMargin = 2.0;
// 0x3f4ccccd: el alfa del fondo de la caja.
constexpr float BackgroundAlpha = 0.8f;
// 0x5af36f: el modo de 0x467060 que usan todos los cuerpos, centrado en la caja, y el de 0x5af9b0, contra el borde.
constexpr std::int32_t CenteredMode = 2;
constexpr std::int32_t RightAlignedMode = 3;
// 0x40e850: la ventana del set, con 0x78 de ancho, en (x + 0x5f, y + 0x28) del inventario, con el fondo 0x64.
constexpr std::int32_t SetWindowWidth = 0x78;
constexpr std::int32_t SetWindowDx = 0x5f;
constexpr std::int32_t SetWindowDy = 0x28;
constexpr std::uint32_t SetWindowBackground = 0x64;
// 0x40eb2b: una opción de 0xa5 o más sin la marca de la clase va en rojo.
constexpr std::uint8_t RedSetOption = 0xa5;
// Los textos de Text.bmd (la dirección − 0x76bd1dc, sobre 300): "%s %s" y "%s %s %s" del conjunto los llevan detrás
// del nombre (0x770ce08 y 0x76c8c30).
constexpr std::size_t SetTitleText = 1089;
constexpr std::size_t SetLabelTailText = 159;
// 0x40d530: los textos de las opciones de un conjunto.
constexpr std::size_t FirstStatOptionText = 950;
constexpr std::size_t MagicDamageOptionText = 632;
constexpr std::size_t SecondOptionBase = 949;
constexpr std::size_t IgnoreDefenseOptionText = 970;
constexpr std::size_t ShieldDefenseOptionText = 984;
constexpr std::size_t TwoHandedOptionText = 983;
constexpr std::size_t ThirdOptionBase = 947;
constexpr std::uint8_t LastStatOption = 6;
constexpr std::uint8_t MagicDamageOption = 7;
constexpr std::uint8_t LastSecondOption = 0x14;
constexpr std::uint8_t IgnoreDefenseOption = 0x15;
constexpr std::uint8_t ShieldDefenseOption = 0x16;
constexpr std::uint8_t TwoHandedOption = 0x17;
constexpr std::uint8_t LastSetOption = 0x23;
// ItemSetOption.bmd (05, Las tablas de los conjuntos): +0x20 los seis pares de opciones, +0x2c sus valores, +0x38 y
// +0x3a los dos extras con sus valores, +0x3c las piezas + 1 (0xff sin usar), +0x3d las cinco del conjunto completo y
// +0x42 sus valores.
constexpr std::size_t SetPairOptions = 0x20;
constexpr std::size_t SetPairValues = 0x2c;
constexpr std::size_t SetPairCount = 6;
constexpr std::size_t SetExtraEnd = 8;
constexpr std::size_t SetExtraOptions = 0x32;
constexpr std::size_t SetExtraValues = 0x34;
constexpr std::size_t SetFullOptions = 0x35;
constexpr std::size_t SetFullValues = 0x3a;
constexpr std::size_t SetLabelSlots = 13;
constexpr std::size_t SetUsedByte = 0x3c;
constexpr std::uint8_t NoSetOption = 0xff;
// 0x40e850 escribe tres "\n" y 0x40eca0 cuatro renglones antes de las opciones.
constexpr std::size_t SetWindowFirstLine = 3;
constexpr std::size_t SetLabelFirstLine = 4;

std::int32_t Truncate(double value)
{
    return World::OriginalFtolLow(value).value_or(0);
}

Render::Text::FontKind FontOf(std::int32_t bold)
{
    return bold != 0 ? Render::Text::FontKind::Bold : Render::Text::FontKind::Normal;
}

std::string_view TextOrEmpty(std::span<const char> bytes)
{
    std::size_t size = 0;
    while (size < bytes.size() && bytes[size] != '\0')
        ++size;
    return {bytes.data(), size};
}

// El nombre de un registro como lo lee sprintf("%s"): hasta su '\0', dentro del registro.
std::string NameOf(std::span<const std::byte, Data::Tables::ItemSetOptionRecordSize> record)
{
    std::string name;
    for (const std::byte byte : record)
    {
        if (byte == std::byte{0})
            break;
        name.push_back(static_cast<char>(byte));
    }
    return name;
}

Core::Expected<std::string_view, std::string> TextOf(const GameHudContext& context, std::size_t index,
                                                     std::string_view where)
{
    const std::optional<std::string_view> text = UI::TextAt(context.texts, index);
    if (!text)
        return Core::Unexpected(std::format("{}: sin el texto {} de Text.bmd", where, index));
    return *text;
}

// glColor3f del switch de 0x5af368 (la tabla de 0x5af4f4); con un color de más de 10, el que haya.
void SetLineColor(Render::SpriteBatch& sprites, std::int32_t color)
{
    const auto rgb = [&sprites](float red, float green, float blue)
    { sprites.SetColor(Color{red, green, blue, 1.0f}); };
    switch (color)
    {
    case TooltipWhite:
    case TooltipWhiteOnBlue:
    case TooltipWhiteOnRed:
    case TooltipWhiteOnTeal:
    case TooltipGreenOnRed:
        rgb(1.0f, 1.0f, 1.0f);
        return;
    case TooltipBlue:
        rgb(0.5f, 0.7f, 1.0f);
        return;
    case TooltipRed:
        rgb(1.0f, 0.2f, 0.1f);
        return;
    case TooltipGold:
        rgb(1.0f, 0.8f, 0.1f);
        return;
    case TooltipGreen:
        rgb(0.1f, 1.0f, 0.5f);
        return;
    case TooltipPurple:
        rgb(1.0f, 0.1f, 1.0f);
        return;
    case TooltipGray:
        rgb(0.4f, 0.4f, 0.4f);
        return;
    default:
        return;
    }
}

// 0x5af3d7..0x5af42c: el texto en −1 (0x5af359) y el fondo de los colores 5, 7, 8 y 9.
Render::Text::TextColors LineColors(std::int32_t color)
{
    switch (color)
    {
    case TooltipWhiteOnBlue:
        return {.text = 0xffffffffu, .background = 0xff0000a0u};
    case TooltipWhiteOnRed:
        return {.text = 0xffffffffu, .background = 0xffa00000u};
    case TooltipWhiteOnTeal:
        return {.text = 0xffffffffu, .background = 0xff0066a0u};
    case TooltipGreenOnRed:
        return {.text = 0xff00ff00u, .background = 0xffc83c3cu};
    default:
        return {.text = 0xffffffffu, .background = 0};
    }
}

// 0x465950(x, y, texto, caja, 0, modo, 0, 0x280) → 0x467060: una caja de `boxPixels` columnas que empieza en
// _ftol(x · rx), con el fondo (0x6c8e64) en toda ella (ITextDrawer::DrawColumns, como el título del inventario,
// Q-C710). El modo 2 (0x46711b..0x467132) pone el texto en la columna (caja − cx) / 2, con la caja de
// Render::Text::CenterInBox; el 3 (0x467134..0x467144), en la columna caja − cx de una caja de `boxPixels` justas:
// contra su borde derecho. Devuelve lo que baja: cy / ry, la mitad si empieza con '\n'.
double DrawLine(GameHudContext& context, std::int32_t x, std::int32_t y, std::string_view text, std::int32_t boxPixels,
                std::int32_t mode)
{
    const App::PixelSize screen = context.sprites.Screen();
    const Render::Text::TextExtent extent = context.text.Measure(context.hud.dcFont, text);
    const Render::Text::TextStyle style{.font = context.hud.dcFont, .colors = context.hud.textColors};
    if (mode == CenteredMode)
    {
        const Render::Text::CenteredInBox placed = Render::Text::CenterInBox(boxPixels, extent.width);
        context.text.DrawColumns(x, y, text, style, -placed.textColumn, placed.width);
    }
    else
        context.text.DrawColumns(x, y, text, style, extent.width - boxPixels, boxPixels);
    const double rows = static_cast<double>(extent.height) / static_cast<double>(Render::ScaleY(1.0f, screen));
    return !text.empty() && text.front() == '\n' ? rows / 2.0 : rows;
}

// Los renglones "\n" y el título "%s %s" de la ventana del set (0x40e94b.. y 0x40ea9e..).
void WriteNewLine(TooltipLines& lines, std::size_t& next)
{
    lines.text[next] = "\n";
    lines.colors[next] = TooltipWhite;
    lines.bold[next] = 0;
    ++next;
}

void WriteSetTitle(TooltipLines& lines, std::size_t& next, std::string_view name, std::string_view title)
{
    lines.text[next] = UI::Sprintf("%s %s", name, title);
    lines.colors[next] = TooltipGold;
    lines.bold[next] = 1;
    ++next;
}

// 0x40e850(x, y), un thiscall sobre [0x6eed3c]: con +0x9304 y alguna opción en la lista, los conjuntos puestos con sus
// opciones, y +0x9304 de vuelta en 0.
Core::Expected<void, std::string> DrawTheSetWindow(GameHudContext& context, std::int32_t x, std::int32_t y)
{
    ItemSetState& sets = context.world.ItemSets();
    if (sets.byte9304 == 0)
        return {};
    const ItemSetOptionList& list = sets.list;
    if (list.count9348 == 0 && list.count9349 == 0)
        return {};
    const auto title = TextOf(context, SetTitleText, "0x40e995");
    if (!title.HasValue())
        return Core::Unexpected(title.Error());
    TooltipLines& lines = context.hud.tooltip;
    context.hud.textColors = Render::Text::TextColors{.text = 0xffffffffu, .background = SetWindowBackground};
    std::size_t next = 0;
    for (std::size_t i = 0; i < SetWindowFirstLine; ++i)
        WriteNewLine(lines, next);
    const std::int32_t first = list.count9348;
    const std::int32_t second = list.count9349;
    const std::int32_t pieces = sets.byte9307;
    const std::string_view firstName = TextOrEmpty(sets.names[0]);
    const std::string_view secondName = TextOrEmpty(sets.names[1]);
    for (std::int32_t i = 0; i < first + second; ++i)
    {
        const auto entry = static_cast<std::size_t>(i);
        if (entry >= ItemSetOptionCapacity)
            return Core::Unexpected(std::format("0x40eb01: la ventana del set lee la entrada {} de 16 (las marcas de "
                                                "+0x937c como opciones)",
                                                entry));
        if (next + 3 > TooltipLineCount)
            return Core::Unexpected(std::string("0x40eb23: la ventana del set pasa de 30 renglones (el original "
                                                "escribe sobre el ítem del cursor, 0x7879f80)"));
        if (i == 0 && first > 0)
            WriteSetTitle(lines, next, firstName, title.Value());
        else if (i == first && second > 0)
        {
            WriteNewLine(lines, next);
            WriteSetTitle(lines, next, secondName, title.Value());
        }
        else if (pieces != 0)
        {
            // 0x40e9c5..0x40ea64: con +0x9307, el título de la otra clase a +0x9307 del final de cada una.
            if (first > 0 && first - pieces == i)
            {
                WriteNewLine(lines, next);
                WriteSetTitle(lines, next, secondName, title.Value());
            }
            else if (second > 0 && second - pieces == i)
            {
                WriteNewLine(lines, next);
                WriteSetTitle(lines, next, firstName, title.Value());
            }
        }
        const std::array<std::uint8_t, 3>& option = list.entries[entry];
        const auto value = static_cast<std::uint8_t>(static_cast<std::uint32_t>(list.values[entry]) & 0xffu);
        if (auto written = WriteTheSetOption(context.texts, lines.text[next], option[0], value); !written.HasValue())
            return written;
        lines.colors[next] = option[0] >= RedSetOption && option[1] == 0 ? TooltipRed : TooltipBlue;
        lines.bold[next] = 0;
        ++next;
    }
    if (auto drawn = DrawTheTooltipBox(context, x + SetWindowDx, y + SetWindowDy, static_cast<std::int32_t>(next),
                                       SetWindowWidth, CenteredMode, 1);
        !drawn.HasValue())
        return drawn;
    if (!context.projecting)
        sets.byte9304 = 0;
    return {};
}

// El ítem de 0x78abaa8: la casilla de la grilla (la del inventario, las del comercio o la de las tiendas personales)
// o el lugar del equipo.
const World::ItemState* HoveredItemOf(WorldState& world)
{
    const UI::HoveredItem& hovered = world.Runtime().ui.hoveredItem;
    switch (hovered.place)
    {
    case UI::HoveredItem::Place::InventoryCell:
        return hovered.index < world.Inventory().cells.size() ? &world.Inventory().cells[hovered.index] : nullptr;
    case UI::HoveredItem::Place::Equipment:
        return hovered.index < world.OwnCharacter().equipment.size() ? &world.OwnCharacter().equipment[hovered.index]
                                                                     : nullptr;
    case UI::HoveredItem::Place::OwnTradeCell:
    case UI::HoveredItem::Place::OtherTradeCell:
    {
        TradeState& trade = world.Runtime().social.trade;
        auto& cells = hovered.place == UI::HoveredItem::Place::OwnTradeCell ? trade.ownGrid : trade.otherGrid;
        return hovered.index < cells.size() ? &cells[hovered.index] : nullptr;
    }
    case UI::HoveredItem::Place::ShopCell:
        return hovered.index < world.Inventory().secondCells.size() ? &world.Inventory().secondCells[hovered.index]
                                                                    : nullptr;
    case UI::HoveredItem::Place::None:
        return nullptr;
    }
    return nullptr;
}
} // namespace

Core::Expected<void, std::string> DrawTheTooltipBox(GameHudContext& context, std::int32_t x, std::int32_t y,
                                                    std::int32_t count, std::int32_t widthOverride,
                                                    std::int32_t textMode, std::int32_t frame)
{
    if (textMode != CenteredMode && textMode != RightAlignedMode)
        return Core::Unexpected(std::format("0x5af471: la caja de los tooltips con el modo {} de 0x467060", textMode));
    if (count > static_cast<std::int32_t>(TooltipLineCount))
        return Core::Unexpected(
            std::format("0x5af0c5: la caja de {} renglones lee más allá de los 30 (0x7879f80)", count));
    TooltipLines& lines = context.hud.tooltip;
    Render::SpriteBatch& sprites = context.sprites;
    const App::PixelSize screen = sprites.Screen();
    const double rx = Render::ScaleX(1.0f, screen);
    const double ry = Render::ScaleY(1.0f, screen);
    // 0x5af0c5..0x5af139: la medida, hasta el primer renglón vacío.
    float widest = 0.0f;
    std::int32_t full = 0;
    std::int32_t half = 0;
    std::int32_t cy = 0;
    for (std::int32_t i = 0; i < count; ++i)
    {
        const auto line = static_cast<std::size_t>(i);
        if (lines.text[line].empty())
        {
            count = i;
            break;
        }
        context.hud.dcFont = FontOf(lines.bold[line]);
        const Render::Text::TextExtent extent = context.text.Measure(context.hud.dcFont, lines.text[line]);
        if (widest < static_cast<float>(extent.width))
            widest = static_cast<float>(extent.width);
        cy = extent.height;
        if (lines.text[line].front() == '\n')
            ++half;
        else
            ++full;
    }
    const auto height = static_cast<float>(
        (static_cast<double>(half) * static_cast<double>(cy) * 0.5 + static_cast<double>(full * cy)) /
        (ry * static_cast<double>(LineHeightInverse)));
    sprites.SetBlend(BlendMode::Alpha);
    double measured = static_cast<double>(widest) / rx;
    if (widthOverride > 0)
        measured = static_cast<double>(widthOverride) / rx * 2.0;
    const auto width = static_cast<float>(measured + WidthMargin);
    std::int32_t left = Truncate(static_cast<double>(x) - static_cast<double>(width) * 0.5);
    if (left < 0)
        left = 0;
    const double right = screen.width > 0 ? static_cast<double>(screen.width) / rx : 0.0;
    if (static_cast<double>(left) + static_cast<double>(width) > right)
        left = Truncate(right - static_cast<double>(width) - 1.0);
    if (frame == 1)
    {
        // 0x5af1f5..0x5af2da: los cuatro bordes y el fondo, sin textura.
        sprites.SetColor(Color{0.0f, 0.0f, 0.0f, 1.0f});
        const auto top = static_cast<float>(static_cast<double>(y) - 1.0);
        const auto edge = static_cast<float>(static_cast<double>(left) - 1.0);
        const auto tall = static_cast<float>(static_cast<double>(height) + 1.0);
        sprites.ColorQuad(edge, top, static_cast<float>(static_cast<double>(width) + 1.0), 1.0f);
        sprites.ColorQuad(edge, top, 1.0f, tall);
        sprites.ColorQuad(static_cast<float>(static_cast<double>(edge) + static_cast<double>(width) + 1.0), top, 1.0f,
                          tall);
        sprites.ColorQuad(edge, static_cast<float>(static_cast<double>(top) + static_cast<double>(height) + 1.0),
                          static_cast<float>(static_cast<double>(width) + 2.0), 1.0f);
        sprites.SetColor(Color{0.0f, 0.0f, 0.0f, BackgroundAlpha});
        sprites.ColorQuad(static_cast<float>(left), static_cast<float>(y), width, height);
        sprites.EnableTexture();
    }
    // 0x5af2e0..0x5af4c5: los renglones.
    const auto textX = static_cast<float>(left + 1);
    auto textY = static_cast<float>(y);
    const std::int32_t boxPixels = Truncate((static_cast<double>(width) - TextBoxMargin) * rx);
    for (std::int32_t i = 0; i < count; ++i)
    {
        const auto line = static_cast<std::size_t>(i);
        const std::string& text = lines.text[line];
        context.hud.dcFont = FontOf(lines.bold[line]);
        double down = 0.0;
        if (text.front() == '\n' || text == " ")
        {
            const Render::Text::TextExtent extent = context.text.Measure(context.hud.dcFont, text);
            down = static_cast<double>(extent.height) / ry / (text.front() == '\n' ? 2.0 : 1.0);
        }
        else
        {
            const std::int32_t color = lines.colors[line];
            SetLineColor(sprites, color);
            context.hud.textColors = LineColors(color);
            down = DrawLine(context, Truncate(static_cast<double>(textX)), Truncate(static_cast<double>(textY)), text,
                            boxPixels, textMode);
        }
        textY = static_cast<float>(static_cast<double>(textY) + down * static_cast<double>(LineAdvance));
    }
    sprites.SetColor(Color{1.0f, 1.0f, 1.0f, 1.0f});
    sprites.SetBlend(BlendMode::Opaque);
    return {};
}

Core::Expected<void, std::string> WriteTheSetOption(const Data::Text::TextTable* texts, std::string& line,
                                                    std::uint8_t option, std::uint8_t value)
{
    std::size_t index = 0;
    if (option <= LastStatOption)
        index = FirstStatOptionText + option;
    else if (option == MagicDamageOption)
        index = MagicDamageOptionText;
    else if (option <= LastSecondOption)
        index = SecondOptionBase + option;
    else if (option == IgnoreDefenseOption)
        index = IgnoreDefenseOptionText;
    else if (option == ShieldDefenseOption)
        index = ShieldDefenseOptionText;
    else if (option == TwoHandedOption)
        index = TwoHandedOptionText;
    else if (option <= LastSetOption)
        index = ThirdOptionBase + option;
    else
        return {};
    const std::optional<std::string_view> format = UI::TextAt(texts, index);
    if (!format)
        return Core::Unexpected(std::format("0x40d530: sin el texto {} de Text.bmd", index));
    line = UI::Sprintf(*format, static_cast<std::int32_t>(value));
    return {};
}

Core::Expected<void, std::string> DrawTheTooltips(GameHudContext& context)
{
    WorldState& world = context.world;
    WorldRuntime& runtime = world.Runtime();
    const UI::GameUiState& ui = runtime.ui;
    context.sprites.SetBlend(BlendMode::Opaque);
    context.hud.durabilityWarning = 0;
    std::int32_t repairMode = runtime.panels.repairMode;
    if (repairMode != 0)
    {
        repairMode = 1;
        if (!context.projecting)
            runtime.panels.repairMode = 1;
    }
    if (const World::ItemState* item = HoveredItemOf(world); item != nullptr)
    {
        const bool overTheInventory = context.mouse.x >= context.hud.inventoryX && context.mouse.x < 0x280 &&
                                      context.mouse.y >= context.hud.inventoryY && context.mouse.y < 0x1e0;
        const Render::Models::LogicalAccess own(context.ownRecordDepth, context.random,
                                                Render::Models::OwnRecordCloseRandoms);
        const ItemTooltipContext body{
            .hud = context, .x = ui.skillTooltipX, .y = ui.skillTooltipY, .item = *item, .grid = ui.field78aba27};
        auto drawn = overTheInventory && repairMode != 0 ? DrawTheRepairTooltip(body) : DrawTheItemTooltip(body);
        if (!drawn.HasValue())
            return drawn;
    }
    // 0x5f7dfe: con 0x78abb14 en 1 (lo escriben las ventanas de la CL9.3 y la CL9.4: 0x5d8d50, 0x5e74f0, 0x5ecde0,
    // 0x5fe610 y 0x5c5181), el ítem del cursor en (0x109, 0x78). MuRet no lo escribe: queda en 0.
    if (ui.skillTooltip != -1)
    {
        if (auto drawn = DrawTheSkillTooltip(context, ui.skillTooltipX, ui.skillTooltipY, ui.skillTooltip, 0);
            !drawn.HasValue())
            return drawn;
    }
    GamePanels panels(world, context.random);
    if (!panels.Registry().IsOpen(UI::InventoryPanel))
        return {};
    return DrawTheSetWindow(context, context.hud.inventoryX, context.hud.inventoryY);
}

Core::Expected<void, std::string> DrawTheSetLabel(GameHudContext& context)
{
    ItemSetState& sets = context.world.ItemSets();
    if (sets.byte9305 == 0)
        return {};
    TooltipLines& lines = context.hud.tooltip;
    lines.ClearColors();
    lines.ClearTexts();
    if (context.setOptions == nullptr)
        return Core::Unexpected(std::string("0x40ece9: el cartel del set sin ItemSetOption.bmd"));
    const std::size_t index = sets.byte9305 - 1u;
    const auto record = context.setOptions->Record(index);
    if (!record)
        return Core::Unexpected(std::format("0x40ece9: el cartel del conjunto {} lee fuera de los 64", index));
    const std::span<const std::byte, Data::Tables::ItemSetOptionRecordSize> set = *record;
    const auto byteAt = [&set](std::size_t offset) { return std::to_integer<std::uint8_t>(set[offset]); };
    if (byteAt(SetUsedByte) == NoSetOption)
    {
        if (!context.projecting)
            sets.byte9305 = 0;
        return {};
    }
    const auto title = TextOf(context, SetTitleText, "0x40ed26");
    if (!title.HasValue())
        return Core::Unexpected(title.Error());
    const auto tail = TextOf(context, SetLabelTailText, "0x40ed21");
    if (!tail.HasValue())
        return Core::Unexpected(tail.Error());
    // 0x40ed06..0x40ed64: las negritas de estos cuatro no las escribe (quedan las del anterior).
    lines.text[0] = "\n";
    lines.text[1] = UI::Sprintf("%s %s %s", std::string_view(NameOf(set)), title.Value(), tail.Value());
    lines.colors[1] = TooltipGold;
    lines.text[2] = "\n";
    lines.text[3] = "\n";
    std::size_t next = SetLabelFirstLine;
    // 0x40ed11 y 0x40ed15: la segunda opción y su valor empiezan en 0xff, y después del sexto par quedan las de ese par
    // (Q-C592).
    std::uint8_t secondOption = NoSetOption;
    std::uint8_t secondValue = NoSetOption;
    const auto writeOption = [&](std::uint8_t option, std::uint8_t value) -> Core::Expected<void, std::string>
    {
        if (next >= TooltipLineCount)
            return Core::Unexpected(std::string("0x40edee: el cartel del set pasa de 30 renglones (el original "
                                                "escribe sobre el ítem del cursor, 0x7879f80)"));
        if (auto written = WriteTheSetOption(context.texts, lines.text[next], option, value); !written.HasValue())
            return written;
        lines.colors[next] = TooltipBlue;
        lines.bold[next] = 0;
        ++next;
        return {};
    };
    for (std::size_t slot = 0; slot < SetLabelSlots; ++slot)
    {
        std::uint8_t firstOption = 0;
        std::uint8_t firstValue = 0;
        if (slot < SetPairCount)
        {
            firstOption = byteAt(SetPairOptions + 2 * slot);
            secondOption = byteAt(SetPairOptions + 2 * slot + 1);
            firstValue = byteAt(SetPairValues + 2 * slot);
            secondValue = byteAt(SetPairValues + 2 * slot + 1);
        }
        else if (slot < SetExtraEnd)
        {
            firstOption = byteAt(SetExtraOptions + slot);
            firstValue = byteAt(SetExtraValues + slot);
        }
        else
        {
            firstOption = byteAt(SetFullOptions + slot);
            firstValue = byteAt(SetFullValues + slot);
        }
        if (firstOption != NoSetOption)
        {
            if (auto written = writeOption(firstOption, firstValue); !written.HasValue())
                return written;
        }
        if (secondOption != NoSetOption)
        {
            if (auto written = writeOption(secondOption, secondValue); !written.HasValue())
                return written;
        }
    }
    if (next + 2 > TooltipLineCount)
        return Core::Unexpected(std::string("0x40ee79: el cartel del set pasa de 30 renglones (el original escribe "
                                            "sobre el ítem del cursor, 0x7879f80)"));
    lines.text[next++] = "\n";
    lines.text[next++] = "\n";
    return DrawTheTooltipBox(context, 0, 0, static_cast<std::int32_t>(next), 0, CenteredMode, 1);
}
} // namespace Client::Scenes
