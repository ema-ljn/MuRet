#include "Hud/GameInventoryHud.h"
#include "Actors/ActorName.h"
#include "Math/X87.h"
#include "Math/RoundedMath.h"
#include "CastleSiege/CastleWindows.h"

#include "Core/Expected.h"
#include "Core/PixelSize.h"
#include "Math/FloatConstants.h"
#include "Models/LogicalAccess.h"
#include "Models/PassColors.h"
#include "Render/OriginalQuad.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Render/Text/TextLayout.h"
#include "Render/Text/TextStyle.h"
#include "Render/TextureSlot.h"
#include "Kernel/ActorState.h"
#include "Actors/Drawing/CharacterDrawing.h"
#include "Hud/GameHud.h"
#include "Chat/GameMessages.h"
#include "Scenes/GameProjection.h"
#include "Hud/GameWindowDrawing.h"
#include "Guild/GuildMarks.h"
#include "Guild/GuildState.h"
#include "Player/ItemSetOptionList.h"
#include "Player/ItemSetState.h"
#include "Npc/NpcWindowState.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Kernel/OwnInventory.h"
#include "Social/SocialState.h"
#include "Items/UiItemDrawing.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Ui/ZenText.h"
#include "Ui/GameText.h"
#include "Ui/HudPrimitives.h"
#include "Ui/PanelRegistry.h"
#include "Crt/SprintfFormat.h"
#include "Kernel/ItemState.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

namespace Client::Hud
{
namespace
{
using Render::BlendMode;
using Render::Color;

constexpr Color White{1.0f, 1.0f, 1.0f, 1.0f};
// glColor3f(1, 0.9, 0.7) of 0x5e3760's numbers.
constexpr Color NumberColor{1.0f, 0.9f, 0.7f, 1.0f};

// The Text.bmd texts (the address − 0x76bd1dc, over 300).
constexpr std::size_t InventoryTitleText = 223;
constexpr std::size_t ZenText = 224;
constexpr std::size_t CloseHelpText = 225;
constexpr std::size_t RepairHelpText = 233;
constexpr std::size_t OpenShopHelpText = 1125;
constexpr std::size_t ShopOpenHelpText = 1127;
constexpr std::size_t SetText = 989;

// Trade (0x5f3b40, 0x5f7610 and 0x5f7840): the 8 × 4 grids at the corner plus 15 (0x6b87bc) and 70 (0x6ba1b0) or
// 270 (0x6ba10c).
constexpr double TradeGridLeft = 15.0;
constexpr double OtherGridTop = 70.0;
constexpr double OwnGridTop = 270.0;
constexpr std::size_t TradeColumns = 8;
constexpr std::size_t TradeRows = 4;
// Personal shops (0x5f3030, 0x5f7610 and 0x5f7840): the 8 × 4 grid 0x78ab0b8 at the corner plus 15 and 110
// (0x6ba184).
constexpr double ShopGridLeft = 15.0;
constexpr double ShopGridTop = 110.0;
constexpr std::size_t ShopColumns = 8;
constexpr std::size_t ShopRows = 4;

// 0x449c00: [0x724580] at 3.
constexpr std::int32_t TrainerWithBox = 3;
// [0x7d536fc] + 0x18 at 3.
constexpr std::int32_t SiegeListWithInventory = 3;

using Math::F32;
using Math::Truncate;

Models::LogicalAccess OwnAccess(GameHudContext& context)
{
    return Models::LogicalAccess(context.ownRecordDepth, context.random, Models::OwnRecordCloseRandoms);
}

Core::Expected<std::string_view, std::string> TextOf(const GameHudContext& context, std::size_t index,
                                                     std::string_view where)
{
    const std::optional<std::string_view> text = Ui::TextAt(context.texts, index);
    if (!text)
        return Core::Unexpected(std::format("{}: sin el texto {} de Text.bmd", where, index));
    return *text;
}

// 0x621b60(slot, x, y, w, h, u, v, du, dv, 1, 1, 0): scaled in size and position, no alpha.
void Quad(Render::SpriteBatch& sprites, std::uint16_t slot, float x, float y, float width, float height, float du,
          float dv)
{
    sprites.Quad(Render::OriginalQuad{.slot = Render::TextureSlot{slot},
                                      .x = x,
                                      .y = y,
                                      .width = width,
                                      .height = height,
                                      .u = 0.0f,
                                      .v = 0.0f,
                                      .du = du,
                                      .dv = dv,
                                      .scaleSize = true,
                                      .scalePosition = true,
                                      .alpha = 0.0f});
}

// The mouse as float against the float box: left ≤ x < left + size, with the sum on the x87.
bool MouseInSquare(const GameHudContext& context, float left, float top, float size)
{
    const auto x = static_cast<double>(static_cast<float>(context.mouse.x));
    const auto y = static_cast<double>(static_cast<float>(context.mouse.y));
    return static_cast<double>(left) <= x && x < static_cast<double>(left) + static_cast<double>(size) &&
           static_cast<double>(top) <= y && y < static_cast<double>(top) + static_cast<double>(size);
}

// SelectObject on the text DC: the font the following texts measure and draw with.
void SelectFont(GameHudContext& context, Render::Text::FontKind font)
{
    context.hud.dcFont = font;
}

// 0x554180(x, y, text, 0, 0, 0): mode 1 with no limit, from x, with whatever font and colors are set.
void DrawText(GameHudContext& context, int x, int y, std::string_view text)
{
    context.text.Draw(x, y, text,
                      Render::Text::TextStyle{.font = context.hud.dcFont, .colors = context.hud.textColors});
}

// 0x554180(x, y, text, width, 1, p6) → 0x467060 with mode 2: a box of `boxPixels` pixels starting at _ftol(x · rx),
// with the text at column (width − cx) / 2 by C division and the background over the whole box, with whatever font and
// colors are set (Render::Text::CenterInBox).
void DrawTextCenteredInBox(GameHudContext& context, int x, int y, std::string_view text, int boxPixels)
{
    const Render::Text::CenteredInBox placed =
        Render::Text::CenterInBox(context.text, context.hud.dcFont, text, boxPixels);
    context.text.DrawColumns(x, y, text,
                             Render::Text::TextStyle{.font = context.hud.dcFont, .colors = context.hud.textColors},
                             -placed.textColumn, placed.width);
}

// 0x623690(x, y, text): centered at x, with whatever font and colors are set.
void DrawTextCenteredAt(GameHudContext& context, int x, int y, std::string_view text)
{
    const Render::Text::TextExtent extent = context.text.Measure(context.hud.dcFont, text);
    DrawText(context, Render::Text::CenteredAt(x, extent, context.sprites.Screen()), y, text);
}

// 0x554340 with the normal font and the white and black colors set before it: the help leaves the text white on a
// transparent background (0x6c8e5c at −1 and 0x6c8e64 at 0).
void DrawHelp(GameHudContext& context, int x, int y, std::string_view help)
{
    Ui::DrawHelp(context.sprites, context.text, x, y, help);
    context.hud.textColors = Render::Text::TextColors{.text = 0xffffffffu, .background = 0};
}

// 0x5e22d0(item): the item's warning color (+0x44, written by 0x5aac20). With another value, whatever is set.
void SetWarningColor(Render::SpriteBatch& sprites, const Kernel::ItemState& item)
{
    switch (item.warning)
    {
    case 0:
        sprites.SetColor(White);
        return;
    case 1:
        sprites.SetColor(Color{0.8f, 0.8f, 0.8f, 1.0f});
        return;
    case 2:
        sprites.SetColor(Color{0.6f, 0.7f, 1.0f, 1.0f});
        return;
    case 3:
    case 99:
        sprites.SetColor(Color{1.0f, 0.2f, 0.1f, 1.0f});
        return;
    case 4:
        sprites.SetColor(Color{0.5f, 1.0f, 0.6f, 1.0f});
        return;
    case 5:
        sprites.SetColor(Color{0.8f, 0.7f, 0.0f, 1.0f});
        return;
    case 6:
        sprites.SetColor(Color{0.8f, 0.5f, 0.0f, 1.0f});
        return;
    case 7:
        sprites.SetColor(Color{0.8f, 0.3f, 0.3f, 1.0f});
        return;
    case 8:
        sprites.SetColor(Color{1.0f, 0.0f, 0.0f, 1.0f});
        return;
    default:
        return;
    }
}

// ---------------------------------------------------------------------------------------------------------------
// The UI 3D item (0x5e1480 → 0x5e0720, Items/UiItemDrawing).

// Without the model library, the draw's rand() in their order: those of 0x607b60 (1 with 0x220 and 0xc19, 2 with
// 0xc17 and 0xe20), those of 0x609640 (2 with 0xe21 and 0xe22) and the set pass's (0x60cc29: with the set at 1 or 2,
// not excellent and the pulse off, rand() % 100 into the model's +0xc0; like DrawItemFromMeshes, only in a frame of
// the original). The 0x1e1e changes model without changing the count.
void SpendUiItemRandoms(GameHudContext& context, std::int32_t model, std::uint8_t excellent, std::uint8_t set)
{
    context.random.Discard(
        static_cast<std::uint32_t>(Actors::Drawing::HangingRandomCount(static_cast<std::uint16_t>(model))));
    constexpr std::int32_t ShieldWithUv = 0xe21;
    constexpr std::int32_t SecondShieldWithUv = 0xe22;
    if (model == ShieldWithUv || model == SecondShieldWithUv)
        context.random.Discard(2);
    const bool showsExcellent = (excellent & 0x3fu) != 0 && (model < 0x1a12 || model > 0x1a18) && model != 0x1c30;
    if (showsExcellent || context.projecting)
        return;
    const std::uint32_t setKind = set & 3u;
    // The set pass of 0x60b3d0 (0x60cbff..0x60cc29), with the x87 pulse not going through a float.
    if ((setKind == 1 || setKind == 2) &&
        Models::SetPulseIsOff(Models::SetPulseAt(static_cast<double>(context.worldClock))))
        context.world.Runtime().itemRender.selections[static_cast<std::uint16_t>(model)] = context.random.Next() % 100;
}

} // namespace

Core::Expected<Items::UiItemBox, std::string> ItemSizeOf(const GameHudContext& context, std::int32_t type,
                                                         std::string_view where)
{
    if (context.items == nullptr)
        return Core::Unexpected(std::format("{}: sin Item.bmd", where));
    const auto record = type < 0 ? std::nullopt : context.items->Record(static_cast<std::size_t>(type));
    if (!record)
        return Core::Unexpected(std::format("{}: Item.bmd no tiene el tipo {:#x}", where, type));
    constexpr std::size_t Width = 0x24;
    constexpr std::size_t Height = 0x25;
    constexpr float CellPixels = 20.0f;
    Items::UiItemBox box;
    box.width = F32(static_cast<double>(std::to_integer<std::uint8_t>((*record)[Width])) * CellPixels);
    box.height = F32(static_cast<double>(std::to_integer<std::uint8_t>((*record)[Height])) * CellPixels);
    return box;
}

Core::Expected<void, std::string> DrawUiItem(GameHudContext& context, const Items::UiItemBox& box, std::int32_t type,
                                             std::uint32_t level, std::uint8_t excellent, std::uint8_t set, bool always)
{
    // 0x5e1486..0x5e1d69: under the mouse, the box's point and 0x620480 with it (no effects outside 0x7d36834).
    const bool hovered = Items::UiItemHovered(box, context.mouse, context.world.Runtime().cursorItemCount, always);
    const auto anchor = Items::UiItemAnchor(type, level, box);
    if (!anchor.HasValue())
        return Core::Unexpected(anchor.Error());
    const std::optional<Items::UiItemCall> call = Items::UiItemCallOf(type, level);
    if (!call)
        return {};
    if (context.uiItems == nullptr)
    {
        // 0x60c3b6..0x60c598: with the models, the 0x1a18 hangs 20 joints from its pose's bones, which the next step
        // uses with their rand() (Q-C1821). Without the models MuRet does not have those bones.
        constexpr std::int32_t WingsWithJoints = 0x1a18;
        if (call->model == WingsWithJoints)
            return Core::Unexpected(std::string("0x60c3b6: las uniones 0x614 y 0x613 (0x530860) del 0x1a18 en la UI "
                                                "salen de los huesos de su pose, que sin los modelos no hay"));
        SpendUiItemRandoms(context, call->model, excellent, set);
        return {};
    }
    Items::UiItemResources& resources = *context.uiItems;
    const Scenes::UnprojectedPoint point =
        Scenes::UnprojectGamePoint(resources.pass.projection, anchor.Value().x, anchor.Value().y);
    auto placement =
        Items::PlaceUiItem(*call, point, hovered, always, context.worldClock, context.items, context.hud.uiItemAngles);
    if (!placement.HasValue())
        return Core::Unexpected(placement.Error());
    return Items::DrawUiItemModel(resources, context.random, placement.Value(), excellent, set);
}

namespace
{
// ---------------------------------------------------------------------------------------------------------------
// The window (0x5f2340).

// 0x5ee220(x, y, line): the 190 × 433 background in two textures and, with `line`, the trade line at y + 225.
void DrawWindowBack(Render::SpriteBatch& sprites, std::int32_t x, std::int32_t y, bool line)
{
    const auto left = static_cast<float>(x);
    const auto top = static_cast<float>(y);
    Quad(sprites, 0x104, left, top, 190.0f, 256.0f, 0.7421875f, 1.0f);
    Quad(sprites, 0x105, left, F32(static_cast<double>(top) + 256.0), 190.0f, 177.0f, 0.7421875f, 0.69140625f);
    if (line)
        Quad(sprites, 0x117, left, F32(static_cast<double>(top) + 225.0), 190.0f, 10.0f, 0.7421875f, 0.625f);
}

// 0x5e2460: the backgrounds of the twelve equipment slots, each in its item's warning color, with the own record open
// around them (3848 rand() when closing it without other accesses) and another access inside for the class: the
// helmet has no background with class 3.
void DrawTheEquipmentSlots(GameHudContext& context, std::int32_t x, std::int32_t y)
{
    Render::SpriteBatch& sprites = context.sprites;
    const Kernel::OwnCharacterRecord& own = context.world.OwnCharacter();
    const auto& equipment = own.equipment;
    sprites.SetBlend(BlendMode::Alpha);
    const auto outer = OwnAccess(context);
    const auto left = static_cast<float>(x);
    const auto top = static_cast<float>(y);
    const float column15 = F32(static_cast<double>(left) + 15.0);
    const float row46 = F32(static_cast<double>(top) + 46.0);
    SetWarningColor(sprites, equipment[8]);
    Quad(sprites, 0x113, column15, row46, 40.0f, 40.0f, 0.625f, 0.625f);
    SetWarningColor(sprites, equipment[7]);
    const float column115 = F32(static_cast<double>(left) + 115.0);
    Quad(sprites, 0x112, column115, row46, 60.0f, 40.0f, 0.9375f, 0.625f);
    {
        const auto inner = OwnAccess(context);
        if ((own.characterClass & 7u) != 3u)
        {
            SetWarningColor(sprites, equipment[2]);
            Quad(sprites, 0x107, F32(static_cast<double>(left) + 75.0), row46, 40.0f, 40.0f, 0.625f, 0.625f);
        }
    }
    SetWarningColor(sprites, equipment[3]);
    const float row89 = F32(static_cast<double>(top) + 89.0);
    const float column75 = F32(static_cast<double>(left) + 75.0);
    Quad(sprites, 0x108, column75, row89, 40.0f, 60.0f, 0.625f, 0.9375f);
    SetWarningColor(sprites, equipment[4]);
    const float row152 = F32(static_cast<double>(top) + 152.0);
    Quad(sprites, 0x109, column75, row152, 40.0f, 40.0f, 0.625f, 0.625f);
    SetWarningColor(sprites, equipment[0]);
    Quad(sprites, 0x10a, column15, row89, 40.0f, 60.0f, 0.625f, 0.9375f);
    SetWarningColor(sprites, equipment[1]);
    const float column134 = F32(static_cast<double>(left) + 134.0);
    Quad(sprites, 0x114, column134, row89, 40.0f, 60.0f, 0.625f, 0.9375f);
    SetWarningColor(sprites, equipment[5]);
    Quad(sprites, 0x10b, column15, row152, 40.0f, 40.0f, 0.625f, 0.625f);
    SetWarningColor(sprites, equipment[6]);
    Quad(sprites, 0x10c, column134, row152, 40.0f, 40.0f, 0.625f, 0.625f);
    SetWarningColor(sprites, equipment[9]);
    const float column55 = F32(static_cast<double>(left) + 55.0);
    Quad(sprites, 0x10d, column55, row89, 20.0f, 20.0f, 0.625f, 0.625f);
    SetWarningColor(sprites, equipment[10]);
    Quad(sprites, 0x10e, column55, row152, 20.0f, 20.0f, 0.625f, 0.625f);
    SetWarningColor(sprites, equipment[11]);
    Quad(sprites, 0x10e, column115, row152, 20.0f, 20.0f, 0.625f, 0.625f);
}
} // namespace

// 0x5e3660(x, y, grid, columns, rows): a 20 × 20 box per slot, in the warning color of whatever is there (empty ones
// too): Item_box (0x115) empty, InventoryBox2 (0x116) with a type.
void DrawTheGridBoxes(Render::SpriteBatch& sprites, float x, float y, std::span<const Kernel::ItemState> cells,
                      std::size_t columns, std::size_t rows)
{
    for (std::size_t row = 0; row < rows; ++row)
    {
        const float top = F32(static_cast<double>(static_cast<float>(row * 20)) + static_cast<double>(y));
        for (std::size_t column = 0; column < columns; ++column)
        {
            const float left = F32(static_cast<double>(static_cast<float>(column * 20)) + static_cast<double>(x));
            const Kernel::ItemState& item = cells[row * columns + column];
            SetWarningColor(sprites, item);
            if (item.id == Kernel::EmptyOwnItemId)
                Quad(sprites, 0x115, left, top, 20.0f, 20.0f, 1.0f, 1.0f);
            else
                Quad(sprites, 0x116, left, top, 20.0f, 20.0f, 0.625f, 0.6666667f);
        }
    }
}

namespace
{
// A 24 × 24 button at the window's foot and its help with the mouse over it, at (_ftol(x), _ftol(y) − above).
Core::Expected<void, std::string> DrawFootButton(GameHudContext& context, std::uint16_t slot, float x, float y,
                                                 std::size_t help, int above, std::string_view where)
{
    Quad(context.sprites, slot, x, y, 24.0f, 24.0f, 0.75f, 0.75f);
    if (!MouseInSquare(context, x, y, 24.0f))
        return {};
    SelectFont(context, Render::Text::FontKind::Normal);
    context.hud.textColors = Render::Text::TextColors{.text = 0xffffffffu, .background = 0xff000000u};
    auto text = TextOf(context, help, where);
    if (!text.HasValue())
        return Core::Unexpected(text.Error());
    DrawHelp(context, Truncate(x), Truncate(y) - above, text.Value());
    return {};
}

// 0x40e720(x, y), the method of the sets object ([0x6eed3c]): the setitem label and "[text 989]" twice, in black and
// shifted one point, and on top in gold with some set option (+0x9348 or +0x9349) or in grey.
Core::Expected<void, std::string> DrawTheSetLabel(GameHudContext& context, std::int32_t x, std::int32_t y)
{
    Render::SpriteBatch& sprites = context.sprites;
    const float top = F32(static_cast<double>(y) + 22.0);
    Quad(sprites, 0x652, F32(static_cast<double>(x) + 14.0), top, 162.0f, 20.0f, 0.6328125f, 0.625f);
    SelectFont(context, Render::Text::FontKind::Bold);
    context.hud.textColors = Render::Text::TextColors{.text = 0xff000000u, .background = 0x64u};
    auto set = TextOf(context, SetText, "0x40e786");
    if (!set.HasValue())
        return Core::Unexpected(set.Error());
    const std::string label = std::format("[{}]", set.Value());
    DrawTextCenteredAt(context, x + 0x60, Truncate(static_cast<double>(top) + 3.0), label);
    context.hud.textColors.text = 0xffffffffu;
    const Player::ItemSetOptionList& list = context.world.ItemSets().list;
    if (list.count9348 == 0 && list.count9349 == 0)
        sprites.SetColor(Color{0.5f, 0.5f, 0.5f, 1.0f});
    else
        sprites.SetColor(Color{1.0f, 0.8f, 0.1f, 1.0f});
    DrawTextCenteredAt(context, x + 0x5f, Truncate(static_cast<double>(top) + 2.0), label);
    sprites.SetColor(White);
    return {};
}
} // namespace

Core::Expected<void, std::string> DrawTheInventory(GameHudContext& context, Ui::PanelRegistry& panels)
{
    if (!panels.IsOpen(Ui::InventoryPanel))
        return {};
    Render::SpriteBatch& sprites = context.sprites;
    Kernel::WorldState& world = context.world;
    Kernel::WorldRuntime& runtime = world.Runtime();
    Kernel::HudState& hud = context.hud;
    std::int32_t x = 0x1c2;
    if (panels.IsOpen(Ui::CharacterPanel) || panels.IsOpen(Ui::PartyPanel))
        x = 0x104;
    constexpr std::int32_t y = 0;
    hud.inventoryX = x;
    hud.inventoryY = y;
    DrawWindowBack(sprites, x, y, false);
    DrawTheEquipmentSlots(context, x, y);
    DrawTheGridBoxes(sprites, F32(static_cast<double>(x) + 15.0), F32(static_cast<double>(y) + 200.0),
                     world.Inventory().cells, Kernel::InventoryColumns, Kernel::InventoryColumns);
    sprites.SetColor(White);
    // 0x5f23e1..0x5f2441: the title in the bold font, white on dark grey, centered in 120 of 640.
    SelectFont(context, Render::Text::FontKind::Bold);
    hud.textColors = Render::Text::TextColors{.text = 0xffffffffu, .background = 0xff141414u};
    auto title = TextOf(context, InventoryTitleText, "0x5f241c");
    if (!title.HasValue())
        return Core::Unexpected(title.Error());
    const auto screenWidth = static_cast<std::uint32_t>(sprites.Screen().width);
    DrawTextCenteredInBox(context, x + 0x23, y + 0xc, title.Value(), static_cast<int>(screenWidth * 120u / 640u));
    // 0x5f2441..0x5f2484: the zen background.
    const float zenX = F32(static_cast<double>(x) + 50.0);
    const float zenY = F32(static_cast<double>(y) + static_cast<double>(367.7f));
    Quad(sprites, 0x10f, zenX, zenY, 113.0f, 18.0f, 0.8828125f, 0.5625f);
    SelectFont(context, Render::Text::FontKind::Bold);
    std::uint32_t zen = 0;
    {
        // 0x5f249d..0x5f26a2: the zen of +0xecc, read with the record open.
        const auto own = OwnAccess(context);
        zen = world.OwnCharacter().zen;
    }
    // 0x5f26a7..0x5f2751 and 0x5f2771..0x5f27ef: the same computation as 0x5b1130 and 0x5b10f0, copied.
    const std::string amount = Ui::FormatZen(zen);
    hud.textColors = Render::Text::TextColors{.text = 0xff96dcffu, .background = 0xff282828u};
    const std::int32_t textY = Truncate(zenY) + 3;
    const std::int32_t textX = Truncate(zenX);
    auto zenLabel = TextOf(context, ZenText, "0x5f2795");
    if (!zenLabel.HasValue())
        return Core::Unexpected(zenLabel.Error());
    DrawText(context, textX - 30, textY, zenLabel.Value());
    hud.textColors.text = Ui::ZenColor(zen);
    DrawText(context, textX + 10, textY, amount);
    // 0x5f27ff..0x5f28f5: close, without the trade and without the trainer's box.
    const Ui::DelegatedPanels& delegated = runtime.panels.delegated;
    const float footY = F32(static_cast<double>(y) + 395.0);
    if (!panels.IsOpen(Ui::TradePanel) && delegated.trainer != TrainerWithBox)
    {
        if (auto close = DrawFootButton(context, 0x118, F32(static_cast<double>(x) + 20.0), footY, CloseHelpText, 13,
                                        "0x5f28d6");
            !close.HasValue())
            return close;
    }
    std::uint16_t level = 0;
    {
        // 0x5f28f8..0x5f2bdc: the level at [0x7681eb0] + 0xe, read with the record open.
        const auto own = OwnAccess(context);
        level = world.OwnCharacter().level;
    }
    // 0x5f2be1..0x5f2da9: repair, with no other window, without 0x78abb00 and from level 50.
    if (!panels.IsOpen(Ui::TradePanel) && !panels.IsOpen(Ui::NpcShopPanel) && !panels.IsOpen(Ui::WarehousePanel) &&
        !panels.IsOpen(Ui::ChaosBoxPanel) && !panels.IsOpen(Ui::CastleSiegeListPanel) &&
        !panels.IsOpen(Ui::EventNpcPanel) && !panels.IsOpen(Ui::SecondEventNpcPanel) &&
        delegated.trainer != TrainerWithBox && runtime.panels.field78abb00 == 0 && level > 0x31)
    {
        const std::uint16_t slot = runtime.panels.repairMode == 0 ? 0x11e : 0x11f;
        if (auto repair = DrawFootButton(context, slot, F32(static_cast<double>(x) + 48.0), footY, RepairHelpText, 12,
                                         "0x5f2d8a");
            !repair.HasValue())
            return repair;
    }
    // 0x5f2dac..0x5f3008: the personal shop, with the same windows in another order and from level 6.
    const float shopX = F32(static_cast<double>(x) + 76.0);
    if (!panels.IsOpen(Ui::TradePanel) && !panels.IsOpen(Ui::NpcShopPanel) && delegated.trainer != TrainerWithBox &&
        !panels.IsOpen(Ui::WarehousePanel) && !panels.IsOpen(Ui::ChaosBoxPanel) &&
        !panels.IsOpen(Ui::CastleSiegeListPanel) && !panels.IsOpen(Ui::EventNpcPanel) &&
        !panels.IsOpen(Ui::SecondEventNpcPanel) && runtime.panels.field78abb00 == 0 && level > 5)
    {
        const bool shopOpen = panels.IsOpen(Ui::PersonalShopPanel) || panels.IsOpen(Ui::OtherShopPanel);
        Quad(sprites, shopOpen ? 0x658 : 0x657, shopX, footY, 24.0f, 24.0f, 0.75f, 0.75f);
        if (MouseInSquare(context, shopX, footY, 24.0f))
        {
            SelectFont(context, Render::Text::FontKind::Normal);
            hud.textColors = Render::Text::TextColors{.text = 0xffffffffu, .background = 0xff000000u};
            const bool open = panels.IsOpen(Ui::PersonalShopPanel) || panels.IsOpen(Ui::OtherShopPanel);
            auto help = TextOf(context, open ? ShopOpenHelpText : OpenShopHelpText, "0x5f2fc4");
            if (!help.HasValue())
                return Core::Unexpected(help.Error());
            DrawHelp(context, Truncate(shopX), Truncate(footY) - 12, help.Value());
        }
    }
    return DrawTheSetLabel(context, x, y);
}

namespace
{
// ---------------------------------------------------------------------------------------------------------------
// The 3D items and the grids' numbers (0x5e3760).

// 0x5e2fc0: the equipment slots in their order and their boxes: the window's corner (0x78a5a14 and 0x78a5a10) plus
// the table's floats (fild, fadd and fstp; the 3 also an fsub of 10, 0x6b87a8). The 6, 9, 10 and 11 go through
// 0x5e2d00 with the corner stored as float before adding: with these integers, the same.
struct EquipmentBox
{
    std::size_t slot = 0;
    float x = 0.0f;
    float y = 0.0f;
    float lift = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

constexpr std::array<EquipmentBox, 12> EquipmentBoxes{{
    {.slot = 8, .x = 15.0f, .y = 46.0f, .width = 40.0f, .height = 40.0f},
    {.slot = 7, .x = 115.0f, .y = 46.0f, .width = 60.0f, .height = 40.0f},
    {.slot = 2, .x = 75.0f, .y = 46.0f, .width = 40.0f, .height = 40.0f},
    {.slot = 3, .x = 75.0f, .y = 89.0f, .lift = 10.0f, .width = 40.0f, .height = 60.0f},
    {.slot = 4, .x = 75.0f, .y = 152.0f, .width = 40.0f, .height = 40.0f},
    {.slot = 0, .x = 15.0f, .y = 89.0f, .width = 40.0f, .height = 60.0f},
    {.slot = 1, .x = 134.0f, .y = 89.0f, .width = 40.0f, .height = 60.0f},
    {.slot = 5, .x = 15.0f, .y = 152.0f, .width = 40.0f, .height = 40.0f},
    {.slot = 6, .x = 134.0f, .y = 152.0f, .width = 40.0f, .height = 40.0f},
    {.slot = 9, .x = 55.0f, .y = 89.0f, .width = 20.0f, .height = 20.0f},
    {.slot = 10, .x = 55.0f, .y = 152.0f, .width = 20.0f, .height = 20.0f},
    {.slot = 11, .x = 115.0f, .y = 152.0f, .width = 20.0f, .height = 20.0f},
}};

// The shop and warehouse grid (0x78a7070): 8 × 15 from the NPC window's corner plus 15 (0x6b87bc) and 50 (0x6b86b0),
// with the fild, fadd and fstp.
constexpr std::size_t NpcWindowGridColumns = 8;
constexpr std::size_t NpcWindowGridRows = 15;

float NpcGridLeft(const GameHudContext& context)
{
    return F32(static_cast<double>(context.world.Runtime().ui.npcWindowX) + 15.0);
}

float NpcGridTop(const GameHudContext& context)
{
    return F32(static_cast<double>(context.world.Runtime().ui.npcWindowY) + 50.0);
}

// 0x5e3760(x, y, grid, columns, rows, 0): each top-left cell's item (+0x3c > 0), in the cell
// ((float)(20 · column) + x, (float)(20 · row) + y) with the fstp, of Item.bmd's size.
Core::Expected<void, std::string> DrawTheGridModels(GameHudContext& context, float x, float y,
                                                    std::span<const Kernel::ItemState> cells, std::size_t columns,
                                                    std::size_t rows)
{
    for (std::size_t row = 0; row < rows; ++row)
    {
        const float top = F32(static_cast<double>(row * 20) + static_cast<double>(y));
        for (std::size_t column = 0; column < columns; ++column)
        {
            const Kernel::ItemState& item = cells[row * columns + column];
            if (item.id == Kernel::EmptyOwnItemId || static_cast<std::int32_t>(item.occupied) <= 0)
                continue;
            const auto type = static_cast<std::int32_t>(static_cast<std::int16_t>(item.id));
            auto box = ItemSizeOf(context, type, "0x5e37fd");
            if (!box.HasValue())
                return Core::Unexpected(box.Error());
            box.Value().x = F32(static_cast<double>(column * 20) + static_cast<double>(x));
            box.Value().y = top;
            if (auto drawn = DrawUiItem(context, box.Value(), type, item.scalar1Low, item.flags1b, item.flags1c, false);
                !drawn.HasValue())
                return drawn;
        }
    }
    return {};
}

// 0x5e3760(x, y, grid, columns, rows, 1): potions 0x1c00 to 0x1c08 with durability 2 or more show it (9 × 10, 13 to
// the right); the others, if 0x402b30 gives 2 or more (the bundles 0x181e and 0x181f), the count ((level + 1) · 5) · 2
// (7 × 8, 14 to the right). In (1, 0.9, 0.7).
void DrawTheGridCounts(Render::SpriteBatch& sprites, float x, float y, std::span<const Kernel::ItemState> cells,
                       std::size_t columns, std::size_t rows)
{
    for (std::size_t row = 0; row < rows; ++row)
    {
        const float top = F32(static_cast<double>(static_cast<float>(row * 20)) + static_cast<double>(y));
        for (std::size_t column = 0; column < columns; ++column)
        {
            const Kernel::ItemState& item = cells[row * columns + column];
            if (item.id == Kernel::EmptyOwnItemId || static_cast<std::int32_t>(item.occupied) <= 0)
                continue;
            const auto type = static_cast<std::int16_t>(item.id);
            const float left = F32(static_cast<double>(static_cast<float>(column * 20)) + static_cast<double>(x));
            if (type < 0x1c00 || type > 0x1c08 || item.durability < 2)
            {
                // 0x402b30: 2 with 0x181e and 3 with 0x181f; with the others, less than 2.
                if (type != 0x181e && type != 0x181f)
                    continue;
                sprites.SetColor(NumberColor);
                const auto steps = static_cast<std::int32_t>((item.scalar1Low >> 3u) & 0xfu);
                Ui::DrawNumber(sprites, F32(static_cast<double>(left) + 14.0), top, (steps * 5 + 5) * 2, 7.0f, 8.0f);
                continue;
            }
            sprites.SetColor(NumberColor);
            Ui::DrawNumber(sprites, F32(static_cast<double>(left) + 13.0), top, item.durability, 9.0f, 10.0f);
        }
    }
}

// The chaos box grid (0x7d2d380): 8 × 4 from the NPC window's corner plus 15 (0x6b87bc) and 110 (0x6ba184), with the
// fild, fadd and fstp.
constexpr std::size_t ChaosBoxGridColumns = 8;
constexpr std::size_t ChaosBoxGridRows = 4;

float ChaosBoxGridLeft(const GameHudContext& context)
{
    return F32(static_cast<double>(context.world.Runtime().ui.npcWindowX) + 15.0);
}

float ChaosBoxGridTop(const GameHudContext& context)
{
    return F32(static_cast<double>(context.world.Runtime().ui.npcWindowY) + 110.0);
}

// 0x44ad40(numbers): with 0x724580 at 3, 0x5e3760(275.0, 110.0, 0x723c80, 8, 4, numbers), the trainer's box at its
// fixed place (0x44ad59 and 0x44ad5e).
constexpr float TrainerBoxGridLeft = 275.0f;
constexpr float TrainerBoxGridTop = 110.0f;
} // namespace

Core::Expected<void, std::string> DrawTheGridItems(GameHudContext& context, Ui::PanelRegistry& panels)
{
    Kernel::WorldState& world = context.world;
    Kernel::WorldRuntime& runtime = world.Runtime();
    const Ui::DelegatedPanels& delegated = runtime.panels.delegated;
    if (panels.IsOpen(Ui::InventoryPanel))
    {
        // 0x5e2fc0: the equipment, with the own record open around it (each slot's accesses, 0x4e79a0 and 0x40f370 or
        // those of 0x5e2d00, stay inside and take nothing; the model's draw overwrites each slot's 0x5e22d0 color).
        // Each with its type and +0x3c > 0, in its box.
        {
            const auto own = OwnAccess(context);
            const auto& equipment = world.OwnCharacter().equipment;
            for (const EquipmentBox& place : EquipmentBoxes)
            {
                const Kernel::ItemState& item = equipment[place.slot];
                if (item.id == Kernel::EmptyOwnItemId || static_cast<std::int32_t>(item.occupied) <= 0)
                    continue;
                const auto type = static_cast<std::int32_t>(static_cast<std::int16_t>(item.id));
                const Items::UiItemBox box{
                    .x = F32(static_cast<double>(context.hud.inventoryX) + static_cast<double>(place.x)),
                    .y = F32(static_cast<double>(context.hud.inventoryY) + static_cast<double>(place.y) -
                             static_cast<double>(place.lift)),
                    .width = place.width,
                    .height = place.height};
                if (auto drawn = DrawUiItem(context, box, type, item.scalar1Low, item.flags1b, item.flags1c, false);
                    !drawn.HasValue())
                    return drawn;
            }
        }
        // 0x5f7624: the grid, at the corner plus 15 (0x6b87bc) and 200 (0x6b8928).
        if (auto grid = DrawTheGridModels(context, F32(static_cast<double>(context.hud.inventoryX) + 15.0),
                                          F32(static_cast<double>(context.hud.inventoryY) + 200.0),
                                          world.Inventory().cells, Kernel::InventoryColumns, Kernel::InventoryColumns);
            !grid.HasValue())
            return grid;
    }
    // 0x5f769b and 0x5f7756: the shop (with its rand()) and the warehouse, 0x5e3760(x + 15, y + 50, 0x78a7070, 8, 15,
    // 0) from the corner their draw left (0x78aba28 and 0x78aba2c).
    if (panels.IsOpen(Ui::NpcShopPanel))
    {
        if (auto shop = DrawTheGridModels(context, NpcGridLeft(context), NpcGridTop(context), runtime.npcWindows.grid,
                                          NpcWindowGridColumns, NpcWindowGridRows);
            !shop.HasValue())
            return shop;
    }
    if (panels.IsOpen(Ui::TradePanel))
    {
        // 0x5f76b4..0x5f7717: the other's grid (0x78a5a20) at the corner plus 15 and 70, and the own one (0x78a92a8) at
        // 270.
        const Social::TradeState& trade = runtime.social.trade;
        const float left = F32(static_cast<double>(context.hud.tradeX) + TradeGridLeft);
        if (auto other = DrawTheGridModels(context, left, F32(static_cast<double>(context.hud.tradeY) + OtherGridTop),
                                           trade.otherGrid, TradeColumns, TradeRows);
            !other.HasValue())
            return other;
        if (auto own = DrawTheGridModels(context, left, F32(static_cast<double>(context.hud.tradeY) + OwnGridTop),
                                         trade.ownGrid, TradeColumns, TradeRows);
            !own.HasValue())
            return own;
    }
    if (panels.IsOpen(Ui::WarehousePanel))
    {
        if (auto warehouse = DrawTheGridModels(context, NpcGridLeft(context), NpcGridTop(context),
                                               runtime.npcWindows.grid, NpcWindowGridColumns, NpcWindowGridRows);
            !warehouse.HasValue())
            return warehouse;
    }
    // 0x5f77c6: the chaos box, 0x5e3760(x + 15, y + 110, 0x7d2d380, 8, 4, 0) from the corner its draw left.
    if (panels.IsOpen(Ui::ChaosBoxPanel) ||
        (panels.IsOpen(Ui::CastleSiegeListPanel) && delegated.castleSiegeListMode == SiegeListWithInventory &&
         CastleSiege::HeroCanCraftAtTheCastle(context.world)))
    {
        if (auto chaos = DrawTheGridModels(context, ChaosBoxGridLeft(context), ChaosBoxGridTop(context),
                                           runtime.npcWindows.chaosBox, ChaosBoxGridColumns, ChaosBoxGridRows);
            !chaos.HasValue())
            return chaos;
    }
    // 0x5f7772..0x5f7799: without the box, with the siege list, the same draw if the hero's mark asks for it.
    if (panels.IsOpen(Ui::PersonalShopPanel) || panels.IsOpen(Ui::OtherShopPanel))
    {
        // 0x5f77f6..0x5f7821: the personal shops' grid (0x78ab0b8) at the corner (0x78aba58 and 0x78aba5c) plus 15 and
        // 110.
        const Social::PersonalShopState& shops = runtime.social.personalShop;
        if (auto shop = DrawTheGridModels(context, F32(static_cast<double>(shops.windowX) + ShopGridLeft),
                                          F32(static_cast<double>(shops.windowY) + ShopGridTop),
                                          world.Inventory().secondCells, ShopColumns, ShopRows);
            !shop.HasValue())
            return shop;
    }
    // 0x5f782b: 0x44ad40(0).
    if (delegated.trainer == TrainerWithBox)
        return DrawTheGridModels(context, TrainerBoxGridLeft, TrainerBoxGridTop, runtime.npcWindows.trainerBox,
                                 ChaosBoxGridColumns, ChaosBoxGridRows);
    return {};
}

Core::Expected<void, std::string> DrawTheGridNumbers(GameHudContext& context, Ui::PanelRegistry& panels)
{
    Kernel::WorldState& world = context.world;
    const Ui::DelegatedPanels& delegated = world.Runtime().panels.delegated;
    // 0x5f7849..0x5f78e4: returns with no grid open, in this order.
    bool any = panels.IsOpen(Ui::InventoryPanel) || panels.IsOpen(Ui::NpcShopPanel) ||
               panels.IsOpen(Ui::WarehousePanel) || panels.IsOpen(Ui::ChaosBoxPanel) || panels.IsOpen(Ui::TradePanel);
    if (!any && panels.IsOpen(Ui::CastleSiegeListPanel) && delegated.castleSiegeListMode == SiegeListWithInventory)
        any = CastleSiege::HeroCanCraftAtTheCastle(context.world);
    if (!any)
        any = panels.IsOpen(Ui::TrainerPanel);
    if (!any)
        return {};
    context.sprites.SetBlend(BlendMode::Alpha);
    if (panels.IsOpen(Ui::InventoryPanel))
        DrawTheGridCounts(context.sprites, F32(static_cast<double>(context.hud.inventoryX) + 15.0),
                          F32(static_cast<double>(context.hud.inventoryY) + 200.0), world.Inventory().cells,
                          Kernel::InventoryColumns, Kernel::InventoryColumns);
    // 0x5f7973 and 0x5f7a2e: the shop and the warehouse, 0x5e3760(x + 15, y + 50, 0x78a7070, 8, 15, 1).
    if (panels.IsOpen(Ui::NpcShopPanel))
        DrawTheGridCounts(context.sprites, NpcGridLeft(context), NpcGridTop(context), world.Runtime().npcWindows.grid,
                          NpcWindowGridColumns, NpcWindowGridRows);
    if (panels.IsOpen(Ui::TradePanel))
    {
        // 0x5f798c..0x5f79f0: the trade's two grids, like their items.
        const Social::TradeState& trade = world.Runtime().social.trade;
        const float left = F32(static_cast<double>(context.hud.tradeX) + TradeGridLeft);
        DrawTheGridCounts(context.sprites, left, F32(static_cast<double>(context.hud.tradeY) + OtherGridTop),
                          trade.otherGrid, TradeColumns, TradeRows);
        DrawTheGridCounts(context.sprites, left, F32(static_cast<double>(context.hud.tradeY) + OwnGridTop),
                          trade.ownGrid, TradeColumns, TradeRows);
    }
    if (panels.IsOpen(Ui::WarehousePanel))
        DrawTheGridCounts(context.sprites, NpcGridLeft(context), NpcGridTop(context), world.Runtime().npcWindows.grid,
                          NpcWindowGridColumns, NpcWindowGridRows);
    // 0x5f7a9e: the chaos box, 0x5e3760(x + 15, y + 110, 0x7d2d380, 8, 4, 1).
    if (panels.IsOpen(Ui::ChaosBoxPanel) ||
        (panels.IsOpen(Ui::CastleSiegeListPanel) && delegated.castleSiegeListMode == SiegeListWithInventory &&
         CastleSiege::HeroCanCraftAtTheCastle(context.world)))
        DrawTheGridCounts(context.sprites, ChaosBoxGridLeft(context), ChaosBoxGridTop(context),
                          world.Runtime().npcWindows.chaosBox, ChaosBoxGridColumns, ChaosBoxGridRows);
    if (panels.IsOpen(Ui::PersonalShopPanel) || panels.IsOpen(Ui::OtherShopPanel))
    {
        // 0x5f7ace..0x5f7af9: the personal shops' grid, like its items.
        const Social::PersonalShopState& shops = world.Runtime().social.personalShop;
        DrawTheGridCounts(context.sprites, F32(static_cast<double>(shops.windowX) + ShopGridLeft),
                          F32(static_cast<double>(shops.windowY) + ShopGridTop), world.Inventory().secondCells,
                          ShopColumns, ShopRows);
    }
    // 0x5f7b03: 0x44ad40(1).
    if (delegated.trainer == TrainerWithBox)
        DrawTheGridCounts(context.sprites, TrainerBoxGridLeft, TrainerBoxGridTop, world.Runtime().npcWindows.trainerBox,
                          ChaosBoxGridColumns, ChaosBoxGridRows);
    return {};
}

Core::Expected<void, std::string> DrawTheCursorItem(GameHudContext& context)
{
    const Kernel::WorldRuntime& runtime = context.world.Runtime();
    if (runtime.cursorItemCount <= 0)
        return {};
    const Kernel::ItemState& item = runtime.cursorItem;
    const auto type = static_cast<std::int32_t>(static_cast<std::int16_t>(item.id));
    // Item.bmd's size, centered on the mouse: fild of the mouse minus half (0x6b882c), with the fstp.
    auto box = ItemSizeOf(context, type, "0x5f7b34");
    if (!box.HasValue())
        return Core::Unexpected(box.Error());
    constexpr double Half = Math::HalfAsDouble;
    box.Value().x = F32(static_cast<double>(context.mouse.x) - static_cast<double>(box.Value().width) * Half);
    box.Value().y = F32(static_cast<double>(context.mouse.y) - static_cast<double>(box.Value().height) * Half);
    return DrawUiItem(context, box.Value(), type, item.scalar1Low, item.flags1b, item.flags1c, true);
}

DialogItemSource DialogItemOf(std::int32_t dialog, std::int32_t warningKind)
{
    constexpr std::int32_t PriceDialog = 0x94;
    constexpr std::int32_t ItemDialog = 0x96;
    constexpr std::int32_t SaleDialog = 0x98;
    constexpr std::int32_t PriceBelowValueDialog = 0x99;
    constexpr std::int32_t FruitDialog = 0xa8;
    constexpr std::int32_t SaleWarning = 1;
    constexpr std::int32_t OtherShopWarning = 5;
    // 0x633116..0x633144: CL at 1 with 0x94 or 0x98 and warning 1, or with 0x96 or 0xa8.
    const bool shown = ((dialog == PriceDialog || dialog == SaleDialog) && warningKind == SaleWarning) ||
                       dialog == ItemDialog || dialog == FruitDialog;
    // 0x633146..0x63315d: 0x94 with warning 5 draws; with another, 0x99 draws; otherwise, CL.
    const bool draws =
        dialog == PriceDialog ? warningKind == OtherShopWarning || shown : dialog == PriceBelowValueDialog || shown;
    if (!draws)
        return DialogItemSource::None;
    // 0x6331fc..0x633220: with warning 5, 0x96; 0x96, 0x99 and 0xa8 go to the dialogs' item.
    const std::int32_t chosen = warningKind == OtherShopWarning ? ItemDialog : dialog;
    if (chosen == ItemDialog || chosen == PriceBelowValueDialog || chosen == FruitDialog)
        return DialogItemSource::Dialog;
    return DialogItemSource::Cursor;
}

Core::Expected<void, std::string> DrawTheDialogItem(GameHudContext& context, float farPlane)
{
    const Kernel::WorldRuntime& runtime = context.world.Runtime();
    const DialogItemSource source = DialogItemOf(runtime.dialogs.current, runtime.social.trade.warningKind);
    if (source == DialogItemSource::None)
        return {};
    // 0x633222..0x633247 the cursor's and 0x633249..0x63326e the dialogs': the signed WORD, the DWORD +4, the byte
    // +0x1b and the DWORD +0x1c & 0xff.
    const Kernel::ItemState& item = source == DialogItemSource::Cursor ? runtime.cursorItem : runtime.dialogItem;
    const auto type = static_cast<std::int32_t>(static_cast<std::int16_t>(item.id));
    // 0x63326f..0x633283.
    constexpr Items::UiItemBox Box{.x = 245.0f, .y = 115.0f, .width = 40.0f, .height = 60.0f};
    if (context.uiItems == nullptr)
        return DrawUiItem(context, Box, type, item.scalar1Low, item.flags1b, item.flags1c, true);
    // 0x633163..0x6331f7: its own pass; 0x633288..0x633293, the two glPopMatrix. 0x5ac120's pass is put back for the
    // next frame.
    Items::UiItemResources& resources = *context.uiItems;
    auto pass = Items::MakeUiItemPass(resources.pass.pass.screen, farPlane);
    if (!pass.HasValue())
        return Core::Unexpected(pass.Error());
    const Items::UiItemPass itemsPass = resources.pass;
    resources.pass = pass.Value();
    auto drawn = DrawUiItem(context, Box, type, item.scalar1Low, item.flags1b, item.flags1c, true);
    Items::CloseUiItemPass(resources);
    resources.pass = itemsPass;
    return drawn;
}

namespace
{
// ---------------------------------------------------------------------------------------------------------------
// The trade window (0x5f3b40).

// The texts: 0x76cdab4 (the title), 0x76cdbe0, 0x76cdd0c and 0x76cde38 (the zen, OK and close helps), 0x76d811c and
// 0x76d8248 (the level and its format) and 0x76d8374, 0x76d7d98, 0x76d7ec4 and 0x76d7ff0 (the lines).
constexpr std::size_t TradeTitleText = 226;
constexpr std::size_t TradeMoneyHelpText = 227;
constexpr std::size_t TradeOkHelpText = 228;
constexpr std::size_t TradeCloseHelpText = 229;
constexpr std::size_t TradeLevelText = 368;
constexpr std::size_t TradeLevelFormatText = 369;
constexpr std::size_t TradeWarningText = 370;
constexpr std::size_t TradeFirstLineText = 365;
constexpr std::size_t TradeSecondLineText = 366;
constexpr std::size_t TradeThirdLineText = 367;
// 0x5f3b68: the corner, which the draw sets.
constexpr std::int32_t TradeWindowX = 0x104;
constexpr std::int32_t TradeWindowY = 0;
// 0x5ee220 with 1: Interface\Trading_line.jpg (0x117), 190 × 10 at y + 225 (0x6ba640).
constexpr double TradingLineTop = 225.0;
// The 24 × 24 buttons (0x6b8800) with du = dv = 0.75: the other's OK at (140, 185), zen at (26, 390), the own OK at
// (97, 390) and close at (137, 390). The help goes 13 above.
constexpr float ButtonSize = 24.0f;
constexpr float ButtonUv = 0.75f;
constexpr double OtherOkLeft = 140.0;
constexpr double OtherOkTop = 185.0;
constexpr double MoneyButtonLeft = 26.0;
constexpr double OwnOkLeft = 97.0;
constexpr double CloseLeft = 137.0;
constexpr double FootTop = 390.0;
constexpr int HelpAbove = 13;
// Interface\accept_box01.jpg and accept_box02.jpg (0x61ae60): the OK without and with the mark.
constexpr std::uint16_t OkOffSlot = 0x122;
constexpr std::uint16_t OkOnSlot = 0x123;
// Money_01 and Money_02, Item_Money and exit_01.
constexpr std::uint16_t MoneySlot = 0x110;
constexpr std::uint16_t MoneyPressedSlot = 0x111;
constexpr std::uint16_t MoneyBoxSlot = 0x10f;
constexpr std::uint16_t CloseSlot = 0x118;
constexpr std::uint16_t TradingLineSlot = 0x117;
// The zen boxes: 113 × 18 with du = 0.8828125 and dv = 0.5625 at (50, 160) and (50, 360); the text 30 to the left and
// the amount 10 to the right, 2 down.
constexpr double MoneyBoxLeft = 50.0;
constexpr double OtherMoneyTop = 160.0;
constexpr double OwnMoneyTop = 360.0;
// The arrow (CursorSitDown, 9), 15 × 24 from v = 0.4, at (x + 140, y + 28 + sin(T · 0.01)), in (1, 0.86, 0.58).
constexpr std::uint16_t ArrowSlot = 9;
constexpr std::int32_t ArrowTop = 0x1c;
constexpr float ArrowBob = 0.01f;
constexpr Color ArrowColor{1.0f, 0.86f, 0.58f, 1.0f};
// The pulsing warning: the alpha of min(sin(T · 0.005) · 200 + 275, 255) on 0xd2.
constexpr float WarningPulse = 0.005f;
constexpr double WarningSwing = 200.0;
constexpr double WarningBase = 275.0;
constexpr double WarningMost = 255.0;
constexpr std::uint32_t WarningRed = 0xd2;

using Render::Text::FontKind;
using Render::Text::TextColors;

// The pulsing warning's color (0x5f4623..0x5f465c and 0x5f34fe..0x5f3543): the clock's sine times 0.005 on the x87,
// times 200 plus 275, clamped to 255, with _ftol into the alpha and 0xd2 of red.
std::uint32_t WarningPulseColor(float worldClock)
{
    double alpha =
        Math::RoundedSin(static_cast<double>(worldClock) * static_cast<double>(WarningPulse)) * WarningSwing +
        WarningBase;
    if (WarningMost < alpha)
        alpha = WarningMost;
    return (static_cast<std::uint32_t>(Truncate(alpha)) << 24u) + WarningRed;
}

void TradeQuad(Render::SpriteBatch& sprites, std::uint16_t slot, float x, float y)
{
    Quad(sprites, slot, x, y, ButtonSize, ButtonSize, ButtonUv, ButtonUv);
}

float TradePlace(std::int32_t corner, double offset)
{
    return F32(static_cast<double>(corner) + offset);
}

// 0x5f3e0b..0x5f3ecf and its copies: with the mouse over the button, the normal font and the help at
// (_ftol(x), _ftol(y) − 13), with whatever colors are set.
Core::Expected<void, std::string> TradeHelp(GameHudContext& context, float x, float y, std::size_t help,
                                            std::string_view where)
{
    SelectFont(context, FontKind::Normal);
    auto text = TextOf(context, help, where);
    if (!text.HasValue())
        return Core::Unexpected(text.Error());
    DrawText(context, Truncate(static_cast<double>(x)), Truncate(static_cast<double>(y)) - HelpAbove, text.Value());
    return {};
}

// 0x5f40cd..0x5f428b: the zen box with "Zen" and the amount in thousands, colored by how much it is.
Core::Expected<void, std::string> DrawTradeMoney(GameHudContext& context, float x, float y, std::uint32_t zen)
{
    Quad(context.sprites, MoneyBoxSlot, x, y, 113.0f, 18.0f, 0.8828125f, 0.5625f);
    const std::int32_t textY = Truncate(static_cast<double>(y)) + 2;
    const std::int32_t textX = Truncate(static_cast<double>(x));
    auto label = TextOf(context, ZenText, "0x5f415d");
    if (!label.HasValue())
        return Core::Unexpected(label.Error());
    DrawText(context, textX - 30, textY, label.Value());
    context.hud.textColors.text = Ui::ZenColor(zen);
    DrawText(context, textX + 10, textY, Ui::FormatZen(zen));
    return {};
}

// 0x5f4507..0x5f4573: the floor of the other's level and its color.
std::pair<std::int32_t, std::uint32_t> LevelBracket(std::int32_t level)
{
    if (level >= 300)
        return {300, 0xffff99ffu};
    if (level >= 200)
        return {200, 0xffffe6d2u};
    if (level >= 100)
        return {100, 0xff18c900u};
    if (level >= 50)
        return {50, 0xff0096ffu};
    return {10, 0xff0000ffu};
}

// 0x5f3bf9..0x5f3c8a (CL9 milestone 8): the 2000 entries of the guild table (0x787aa48, 0x58 bytes each) whose key is
// not −1 and equals the other's guild's (0x6c9324, the one the 37 brings) upload their mark (0x5f16a0(i, 0)), draw it
// with 0x22 at (x − 32, y + 40), 32 × 32, and write the name (+0xd) at (x, y + 30) with 0x554240, which calls 0x465950
// like 0x554180 without the box (*static*), with the font and colors left by what came before.
Core::Expected<void, std::string> DrawTheOthersGuild(GameHudContext& context, std::int32_t x, std::int32_t y)
{
    constexpr float MarkLeft = 32.0f;
    constexpr float MarkTop = 40.0f;
    constexpr float MarkSize = 32.0f;
    constexpr std::int32_t NameTop = 0x1e;
    const Guild::GuildState& guild = context.world.Runtime().guild;
    const std::int32_t other = context.world.Runtime().social.trade.otherGuild;
    for (std::size_t index = 0; index < Guild::GuildTableSize; ++index)
    {
        const Guild::GuildTableEntry& entry = guild.table[index];
        if (entry.key == -1 || entry.key != other)
            continue;
        if (context.guildMarks == nullptr)
            return Core::Unexpected(std::string("0x5f3c10: la marca del gremio (0x5f16a0) sin la tabla de las marcas"));
        auto slot = context.guildMarks->Upload(guild, static_cast<std::int32_t>(index), false);
        if (!slot.HasValue())
            return Core::Unexpected(std::move(slot).Error());
        Quad(context.sprites, slot.Value().value, F32(static_cast<double>(x) - static_cast<double>(MarkLeft)),
             F32(static_cast<double>(y) + static_cast<double>(MarkTop)), MarkSize, MarkSize, 1.0f, 1.0f);
        const auto end = std::ranges::find(entry.guildName, '\0');
        DrawText(context, x, y + NameTop,
                 std::string_view(entry.guildName.data(), static_cast<std::size_t>(end - entry.guildName.begin())));
    }
    return {};
}

// The other's name (0x78aba1c, ten bytes and the 0 the 37 sets), up to the first 0.
std::string_view NameUntilZero(std::span<const std::byte> name)
{
    const auto end = std::ranges::find(name, std::byte{0});
    return {reinterpret_cast<const char*>(name.data()), static_cast<std::size_t>(end - name.begin())};
}
} // namespace

Core::Expected<void, std::string> DrawTheTradeWindow(GameHudContext& context, Ui::PanelRegistry& panels)
{
    if (!panels.IsOpen(Ui::TradePanel))
        return {};
    Render::SpriteBatch& sprites = context.sprites;
    Kernel::HudState& hud = context.hud;
    const Kernel::WorldRuntime& runtime = context.world.Runtime();
    const Social::TradeState& trade = runtime.social.trade;
    hud.tradeX = TradeWindowX;
    hud.tradeY = TradeWindowY;
    const std::int32_t x = hud.tradeX;
    const std::int32_t y = hud.tradeY;
    // 0x5f3b78: 0x5ee220(x, y, 1), the trade's background and line.
    WindowDrawing::DrawTheWindowBack(sprites, x, y);
    Quad(sprites, TradingLineSlot, static_cast<float>(x),
         F32(static_cast<double>(static_cast<float>(y)) + TradingLineTop), 190.0f, 10.0f, 0.7421875f, 0.625f);
    // 0x5f3b7d..0x5f3bda: the two grids' backgrounds (0x5e3660).
    const float gridLeft = TradePlace(x, TradeGridLeft);
    DrawTheGridBoxes(sprites, gridLeft, TradePlace(y, OtherGridTop), trade.otherGrid, TradeColumns, TradeRows);
    DrawTheGridBoxes(sprites, gridLeft, TradePlace(y, OwnGridTop), trade.ownGrid, TradeColumns, TradeRows);
    sprites.SetColor(White);
    if (auto guild = DrawTheOthersGuild(context, x, y); !guild.HasValue())
        return guild;
    sprites.SetColor(White);
    // 0x5f3ca1..0x5f3cff: the title in bold, centered at 120 · W / 640.
    SelectFont(context, FontKind::Bold);
    hud.textColors = TextColors{.text = 0xffdcdcdcu, .background = 0xff141414u};
    auto title = TextOf(context, TradeTitleText, "0x5f3cf8");
    if (!title.HasValue())
        return Core::Unexpected(title.Error());
    WindowDrawing::DrawTitle(context, x + 0x23, y + 0xc, title.Value());
    // 0x5f3d04..0x5f3d9e: the other's OK, red while 0x78abac4 is above 0.
    const Color okColor = trade.field78abac4 > 0 ? Color{1.0f, 0.0f, 0.0f, 1.0f} : White;
    sprites.SetColor(okColor);
    TradeQuad(sprites, trade.otherOk == 0 ? OkOffSlot : OkOnSlot, TradePlace(x, OtherOkLeft),
              TradePlace(y, OtherOkTop));
    sprites.SetColor(White);
    // 0x5f3da9..0x5f3ecf: zen, Money_02 with the left button down, and its help.
    hud.textColors = TextColors{.text = 0xffffffffu, .background = 0xff000000u};
    const float footY = TradePlace(y, FootTop);
    const float moneyX = TradePlace(x, MoneyButtonLeft);
    TradeQuad(sprites, MoneySlot, moneyX, footY);
    if (MouseInSquare(context, moneyX, footY, ButtonSize))
    {
        if (context.leftDown)
            TradeQuad(sprites, MoneyPressedSlot, moneyX, footY);
        if (auto help = TradeHelp(context, moneyX, footY, TradeMoneyHelpText, "0x5f3eb0"); !help.HasValue())
            return help;
    }
    // 0x5f3ed3..0x5f3ff4: the own OK, red while it waits (0x78abac8), and its help.
    const float okX = TradePlace(x, OwnOkLeft);
    sprites.SetColor(trade.okWait > 0 ? Color{1.0f, 0.0f, 0.0f, 1.0f} : White);
    TradeQuad(sprites, trade.ownOk == 0 ? OkOffSlot : OkOnSlot, okX, footY);
    sprites.SetColor(White);
    if (MouseInSquare(context, okX, footY, ButtonSize))
    {
        if (auto help = TradeHelp(context, okX, footY, TradeOkHelpText, "0x5f3fd5"); !help.HasValue())
            return help;
    }
    // 0x5f3ff8..0x5f40c9: close and its help.
    const float closeX = TradePlace(x, CloseLeft);
    TradeQuad(sprites, CloseSlot, closeX, footY);
    if (MouseInSquare(context, closeX, footY, ButtonSize))
    {
        if (auto help = TradeHelp(context, closeX, footY, TradeCloseHelpText, "0x5f40aa"); !help.HasValue())
            return help;
    }
    // 0x5f40cd..0x5f442b: the other's zen (0x78abab4) and the own (0x78abab8), in bold.
    hud.textColors = TextColors{.text = 0xff96dcffu, .background = 0x80282828u};
    const float moneyBoxX = TradePlace(x, MoneyBoxLeft);
    SelectFont(context, FontKind::Bold);
    if (auto other = DrawTradeMoney(context, moneyBoxX, TradePlace(y, OtherMoneyTop), trade.otherMoney);
        !other.HasValue())
        return other;
    hud.textColors.text = 0xff96dcffu;
    if (auto own = DrawTradeMoney(context, moneyBoxX, TradePlace(y, OwnMoneyTop), trade.ownMoney); !own.HasValue())
        return own;
    // 0x5f4430..0x5f447b: the other's name (0x78aba1c), in the big font.
    hud.textColors = TextColors{.text = 0xffffe6d2u, .background = 0x80282828u};
    SelectFont(context, FontKind::BigBold);
    DrawText(context, x + 0x14, y + 0x2d, NameUntilZero(trade.otherName));
    // 0x5f4489..0x5f4505: the arrow that bobs next to the other's OK.
    sprites.SetColor(ArrowColor);
    const double bob = Math::RoundedSin(static_cast<double>(context.worldClock) * static_cast<double>(ArrowBob));
    sprites.Quad(Render::OriginalQuad{.slot = Render::TextureSlot{ArrowSlot},
                                      .x = TradePlace(x, OtherOkLeft),
                                      .y = F32(bob + static_cast<double>(y + ArrowTop)),
                                      .width = 15.0f,
                                      .height = 24.0f,
                                      .u = 0.0f,
                                      .v = 0.4f,
                                      .du = 1.0f,
                                      .dv = 1.0f,
                                      .scaleSize = true,
                                      .scalePosition = true,
                                      .alpha = 0.0f});
    sprites.SetColor(White);
    // 0x5f4507..0x5f45d8: the other's level (0x78abab0, signed) by its floor, with text 369's format.
    const auto [floor, levelColor] = LevelBracket(static_cast<std::int32_t>(trade.otherLevel));
    hud.textColors.text = levelColor;
    auto levelFormat = TextOf(context, TradeLevelFormatText, "0x5f4577");
    if (!levelFormat.HasValue())
        return Core::Unexpected(levelFormat.Error());
    const std::string level = Crt::Sprintf(levelFormat.Value(), floor);
    SelectFont(context, FontKind::Bold);
    auto levelLabel = TextOf(context, TradeLevelText, "0x5f45a8");
    if (!levelLabel.HasValue())
        return Core::Unexpected(levelLabel.Error());
    DrawText(context, x + 0x8c, y + 0x2d, levelLabel.Value());
    DrawText(context, x + 0x8c, y + 0x37, level);
    // 0x5f45dd..0x5f461e: the hero's published name.
    hud.textColors.text = 0xffffe6d2u;
    SelectFont(context, FontKind::Bold);
    const Kernel::ActorState& hero = context.world.Hero();
    DrawText(context, x + 0x14, y + 0xfa, Actors::HeroNameOf(hero));
    // 0x5f4623..0x5f470b: the pulsing warning and the three lines.
    hud.textColors.text = WarningPulseColor(context.worldClock);
    for (const auto& [text, left, top, where] :
         {std::tuple{TradeWarningText, 0x14, 0xb9, "0x5f4682"}, std::tuple{TradeFirstLineText, 0x2d, 0xb9, "0x5f46a8"},
          std::tuple{TradeSecondLineText, 0x14, 200, "0x5f46d9"},
          std::tuple{TradeThirdLineText, 0x14, 0xd7, "0x5f4704"}})
    {
        auto line = TextOf(context, text, where);
        if (!line.HasValue())
            return Core::Unexpected(line.Error());
        DrawText(context, x + left, y + top, line.Value());
        hud.textColors.text = 0xff96dcffu;
    }
    return {};
}

namespace
{
// ---------------------------------------------------------------------------------------------------------------
// The personal shops window (0x5f3030).

// The texts: 0x770dd44 (the title), 0x770de70 and 0x76e621c (the own shop open or closed), 0x770e0c8 (no title),
// 0x770e1f4, 0x770e320 and 0x770e44c (the buttons), 0x770e578 and 0x770e6a4..0x770edac (the lines of the own shop and
// of another's) and 0x77102c4 and 0x77103f0 (the two that pulse). The pulsing warning is the trade's (370).
constexpr std::size_t ShopTitleText = 1102;
constexpr std::size_t ShopOpenText = 1103;
constexpr std::size_t ShopClosedText = 560;
constexpr std::size_t ShopNoTitleText = 1105;
constexpr std::size_t ShopApplyText = 1106;
constexpr std::size_t ShopOpenButtonText = 1107;
constexpr std::size_t ShopCloseButtonText = 1108;
constexpr std::size_t ShopSoldOutText = 1134;
constexpr std::size_t ShopZenOnlyText = 1135;
// 0x5f306e: the corner, which the draw always sets.
constexpr std::int32_t ShopWindowX = 0x104;
constexpr std::int32_t ShopWindowY = 0;
// 0x5f30c5..0x5f30cf: the title, greyish white on almost black; 0x5f32be, the shop title's.
constexpr TextColors ShopTitleColors{.text = 0xffdcdcdcu, .background = 0xff141414u};
constexpr std::uint32_t ShopNameColor = 0xff0de90du;
// 0x5f310f..0x5f318a: the own shop's strip, untextured (0x621aa0), 150 (0x43160000) wide at (x + 0x14, y + 0x30) and
// the font's step in 640 × 480 plus 2 tall: glColor4f(0.1, 0.8, 0.1, 0.6) with the shop open and (0.6, 0, 0, 0.8)
// otherwise. Its text goes 2 lower, in 0xbe000000 or in white.
constexpr std::int32_t StatusLeft = 0x14;
constexpr std::int32_t StatusTop = 0x30;
constexpr std::int32_t StatusTextTop = 0x32;
constexpr float StatusWidth = 150.0f;
constexpr std::int32_t StatusGap = 2;
constexpr std::uint32_t DesignWidth = 640;
constexpr Color OpenStatusColor{0.1f, 0.8f, 0.1f, 0.6f};
constexpr Color ClosedStatusColor{0.6f, 0.0f, 0.0f, 0.8f};
constexpr std::uint32_t OpenStatusText = 0xbe000000u;
constexpr std::uint32_t ClosedStatusText = 0xffffffffu;
// 0x554180's boxes: 150 · W / 640 the strip and the title, 52 · W / 640 the buttons.
constexpr std::uint32_t StatusBox = 150;
constexpr std::uint32_t ButtonBox = 52;
// 0x5f3238..0x5f3278: Item_Money (0x10f), 150 × 24 at (x + 20, y + 65) (0x6b86d0 and 0x6b8944), with du = 0.8828125
// and dv = 0.5625; the shop's title at (x + 0x14, y + 0x48).
constexpr double ShopNameBoxLeft = 20.0;
constexpr double ShopNameBoxTop = 65.0;
constexpr std::int32_t ShopNameLeft = 0x14;
constexpr std::int32_t ShopNameTop = 0x48;
// 0x5f33ac..0x5f34f6: the buttons, text_button (0x659), 56 × 24 with du = 0.875 and dv = 0.75 at (x + 0x1e, y + 0x18c)
// and (x + 0x69, y + 0x18c), with their text 2 to the right and 7 down; 0xff19ccff, or grey for close with the shop
// closed (the neg and sbb of 0x5f34be..0x5f34cd).
constexpr std::uint16_t TextButtonSlot = 0x659;
constexpr float TextButtonWidth = 56.0f;
constexpr float TextButtonHeight = 24.0f;
constexpr float TextButtonDu = 0.875f;
constexpr float TextButtonDv = 0.75f;
constexpr std::int32_t OpenButtonLeft = 0x1e;
constexpr std::int32_t CloseButtonLeft = 0x69;
constexpr std::int32_t ButtonTop = 0x18c;
constexpr std::int32_t ButtonTextRight = 2;
constexpr std::int32_t ButtonTextDown = 7;
constexpr std::uint32_t ButtonTextColor = 0xff19ccffu;
constexpr std::uint32_t DisabledButtonColor = 0xffaaaaaau;
// 0x5f34fe..0x5f37df: the pulsing warning at (x + 0x1e, y + 0xe6), on 0xff141414, and the lines at x + 0x1e, in
// 0xff96dcff: five of the own shop or another's and the last two pulsing.
constexpr std::int32_t LinesLeft = 0x1e;
constexpr std::int32_t WarningTop = 0xe6;
constexpr std::uint32_t LinesBackground = 0xff141414u;
constexpr std::uint32_t LinesColor = 0xff96dcffu;

struct ShopLine
{
    std::size_t text = 0;
    std::int32_t top = 0;
    std::string_view where;
};
constexpr std::array<ShopLine, 5> OwnShopLines{{{1109, 0xf5, "0x5f35b1"},
                                                {1111, 0x104, "0x5f35d6"},
                                                {1112, 0x113, "0x5f35fc"},
                                                {1113, 0x122, "0x5f3626"},
                                                {1115, 0x131, "0x5f364b"}}};
constexpr std::array<ShopLine, 5> OtherShopLines{{{1110, 0xf5, "0x5f36e4"},
                                                  {1111, 0x104, "0x5f370a"},
                                                  {1112, 0x113, "0x5f3731"},
                                                  {1114, 0x122, "0x5f3759"},
                                                  {1116, 0x131, "0x5f377f"}}};
constexpr std::array<ShopLine, 2> PulsingShopLines{
    {{ShopSoldOutText, 0x14a, "0x5f367d"}, {ShopZenOnlyText, 0x159, "0x5f36a7"}}};

// 0x554180(x, y, text, 0, 0, 0) with text `index`.
Core::Expected<void, std::string> DrawShopText(GameHudContext& context, std::int32_t x, std::int32_t y,
                                               std::size_t index, std::string_view where)
{
    auto text = TextOf(context, index, where);
    if (!text.HasValue())
        return Core::Unexpected(text.Error());
    WindowDrawing::DrawText(context, x, y, text.Value());
    return {};
}

// 0x554180(x, y, text, k · W / 640, 1, ·) with text `index`.
Core::Expected<void, std::string> DrawShopBoxText(GameHudContext& context, std::int32_t x, std::int32_t y,
                                                  std::size_t index, std::uint32_t box, std::string_view where)
{
    auto text = TextOf(context, index, where);
    if (!text.HasValue())
        return Core::Unexpected(text.Error());
    WindowDrawing::DrawInBox(context, x, y, text.Value(), box);
    return {};
}

// 0x5f310f..0x5f3233, with the own shop: the strip and, in the normal blend and on a background at 0, 1103 or 560
// centered in 150 · W / 640. Ends with no blend.
Core::Expected<void, std::string> DrawTheShopStatus(GameHudContext& context, std::int32_t x, std::int32_t y, bool open)
{
    Render::SpriteBatch& sprites = context.sprites;
    const auto screenWidth = static_cast<std::uint32_t>(sprites.Screen().width);
    if (screenWidth == 0)
        return Core::Unexpected(std::string("0x5f314e: el alto de la franja de la tienda sin pantalla (DIV por 0)"));
    sprites.SetColor(open ? OpenStatusColor : ClosedStatusColor);
    const auto step = static_cast<std::uint32_t>(Render::Text::FontStepFor(sprites.Screen().width));
    const auto height = static_cast<std::int32_t>(step * DesignWidth / screenWidth) + StatusGap;
    sprites.ColorQuad(static_cast<float>(x + StatusLeft), static_cast<float>(y + StatusTop), StatusWidth,
                      static_cast<float>(height));
    sprites.SetColor(White);
    sprites.SetBlend(BlendMode::Alpha);
    context.hud.textColors = TextColors{.text = open ? OpenStatusText : ClosedStatusText, .background = 0};
    if (auto status = DrawShopBoxText(context, x + StatusLeft, y + StatusTextTop, open ? ShopOpenText : ShopClosedText,
                                      StatusBox, "0x5f322b");
        !status.HasValue())
        return status;
    sprites.SetBlend(BlendMode::Opaque);
    return {};
}

// 0x5f3238..0x5f3351: Item_Money and, in the normal blend, on a background at 0 and in green, the title centered in
// 150 · W / 640. The text goes to a 48-byte stack buffer: 1105 and over it, with strcpy, 0x78aba64's if it has
// something or if another's shop is viewed (empty, then). MuRet keeps up to 36 bytes (07 Q-C962): it does not go past
// the 48. Ends with no blend.
Core::Expected<void, std::string> DrawTheShopName(GameHudContext& context, std::int32_t x, std::int32_t y,
                                                  const Social::PersonalShopState& shops)
{
    Render::SpriteBatch& sprites = context.sprites;
    Quad(sprites, MoneyBoxSlot, F32(static_cast<double>(x) + ShopNameBoxLeft),
         F32(static_cast<double>(y) + ShopNameBoxTop), 150.0f, 24.0f, 0.8828125f, 0.5625f);
    sprites.SetBlend(BlendMode::Alpha);
    context.hud.textColors = TextColors{.text = ShopNameColor, .background = 0};
    auto placeholder = TextOf(context, ShopNoTitleText, "0x5f329f");
    if (!placeholder.HasValue())
        return Core::Unexpected(placeholder.Error());
    std::string_view name = placeholder.Value();
    if (!shops.ownTitle.empty() || shops.mode == Social::OtherShopView)
        name = shops.ownTitle;
    WindowDrawing::DrawInBox(context, x + ShopNameLeft, y + ShopNameTop, name, StatusBox);
    sprites.SetBlend(BlendMode::Opaque);
    return {};
}

// 0x5f33ac..0x5f34f6, with the own shop: the two buttons, on a background at 0: open (1107) or apply with the shop
// open (1106), and close (1108), grey with the shop closed. Without a mouse they do not light up.
Core::Expected<void, std::string> DrawTheShopButtons(GameHudContext& context, std::int32_t x, std::int32_t y, bool open)
{
    Render::SpriteBatch& sprites = context.sprites;
    context.hud.textColors = TextColors{.text = ButtonTextColor, .background = 0};
    const auto top = static_cast<float>(y + ButtonTop);
    Quad(sprites, TextButtonSlot, static_cast<float>(x + OpenButtonLeft), top, TextButtonWidth, TextButtonHeight,
         TextButtonDu, TextButtonDv);
    const std::int32_t textTop = y + ButtonTop + ButtonTextDown;
    if (auto first = DrawShopBoxText(context, x + OpenButtonLeft + ButtonTextRight, textTop,
                                     open ? ShopApplyText : ShopOpenButtonText, ButtonBox, "0x5f3470");
        !first.HasValue())
        return first;
    Quad(sprites, TextButtonSlot, static_cast<float>(x + CloseButtonLeft), top, TextButtonWidth, TextButtonHeight,
         TextButtonDu, TextButtonDv);
    context.hud.textColors.text = open ? ButtonTextColor : DisabledButtonColor;
    return DrawShopBoxText(context, x + CloseButtonLeft + ButtonTextRight, textTop, ShopCloseButtonText, ButtonBox,
                           "0x5f34f6");
}

// 0x5f34fe..0x5f37e2: the pulsing warning, the five lines of the own shop or another's (with another 0x6c931c, none)
// and the last two pulsing. Ends with no blend.
Core::Expected<void, std::string> DrawTheShopLines(GameHudContext& context, std::int32_t x, std::int32_t y,
                                                   std::int32_t mode)
{
    Kernel::HudState& hud = context.hud;
    const std::uint32_t pulse = WarningPulseColor(context.worldClock);
    hud.textColors = TextColors{.text = pulse, .background = LinesBackground};
    if (auto warning = DrawShopText(context, x + LinesLeft, y + WarningTop, TradeWarningText, "0x5f356f");
        !warning.HasValue())
        return warning;
    hud.textColors.text = LinesColor;
    if (mode == Social::OwnShopView || mode == Social::OtherShopView)
    {
        for (const ShopLine& line : mode == Social::OwnShopView ? OwnShopLines : OtherShopLines)
        {
            if (auto drawn = DrawShopText(context, x + LinesLeft, y + line.top, line.text, line.where);
                !drawn.HasValue())
                return drawn;
        }
        hud.textColors = TextColors{.text = pulse, .background = LinesBackground};
        for (const ShopLine& line : PulsingShopLines)
        {
            if (auto drawn = DrawShopText(context, x + LinesLeft, y + line.top, line.text, line.where);
                !drawn.HasValue())
                return drawn;
        }
    }
    context.sprites.SetBlend(BlendMode::Opaque);
    return {};
}
} // namespace

Core::Expected<void, std::string> DrawThePersonalShopWindow(GameHudContext& context, Ui::PanelRegistry& panels)
{
    if (!panels.IsOpen(Ui::PersonalShopPanel) && !panels.IsOpen(Ui::OtherShopPanel))
        return {};
    Render::SpriteBatch& sprites = context.sprites;
    Kernel::WorldState& world = context.world;
    Social::PersonalShopState& shops = world.Runtime().social.personalShop;
    shops.windowX = ShopWindowX;
    shops.windowY = ShopWindowY;
    const std::int32_t x = shops.windowX;
    const std::int32_t y = shops.windowY;
    // 0x5f307e..0x5f30f9: 0x5ee220(x, y, 0), in white, the bold font and the title centered at 120 · W / 640.
    DrawWindowBack(sprites, x, y, false);
    sprites.SetColor(White);
    SelectFont(context, FontKind::Bold);
    context.hud.textColors = ShopTitleColors;
    auto title = TextOf(context, ShopTitleText, "0x5f30f9");
    if (!title.HasValue())
        return Core::Unexpected(title.Error());
    WindowDrawing::DrawTitle(context, x + 0x23, y + 0xc, title.Value());
    const bool own = shops.mode == Social::OwnShopView;
    const bool open = shops.ownOpen != 0;
    if (own)
    {
        if (auto status = DrawTheShopStatus(context, x, y, open); !status.HasValue())
            return status;
    }
    if (auto name = DrawTheShopName(context, x, y, shops); !name.HasValue())
        return name;
    // 0x5f3356..0x5f339d: the grid's cells (0x5e3660) and, in the normal blend, in white.
    DrawTheGridBoxes(sprites, F32(static_cast<double>(x) + ShopGridLeft), F32(static_cast<double>(y) + ShopGridTop),
                     world.Inventory().secondCells, ShopColumns, ShopRows);
    sprites.SetBlend(BlendMode::Alpha);
    sprites.SetColor(White);
    if (own)
    {
        if (auto buttons = DrawTheShopButtons(context, x, y, open); !buttons.HasValue())
            return buttons;
    }
    return DrawTheShopLines(context, x, y, shops.mode);
}
} // namespace Client::Hud
