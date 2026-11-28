#include "Core/Expected.h"
#include "Models/LogicalAccess.h"
#include "Hud/GameHud.h"
#include "Hud/GameInventoryHud.h"
#include "Tooltips/ItemTooltipParts.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Social/SocialState.h"
#include "Tooltips/TooltipLines.h"
#include "Tooltips/TooltipWriter.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Ui/ZenText.h"
#include "Ui/GameUiState.h"
#include "Kernel/ItemState.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>

// 0x5b2f7f..0x5b3716 of the item body (CL9 milestone 7, phase 6a; 06 § Las tiendas personales, "El dibujo"): the
// personal shop's price. The texts are the Text.bmd ones (the address − 0x76bd1dc, over 300).
namespace Client::Tooltips
{
namespace
{
// 0x5b2f9f: the cell's place in the price maps, column (D+0x42) + row (D+0x43) · 8 + 0x4c.
constexpr std::int32_t ShopColumns = 8;
constexpr std::int32_t FirstShopPlace = 0x4c;
// 0x76c1bb0 (63, the price with "%s"), 0x76dc190 (423, not enough zen) and 0x770dc18 (1101, no price).
constexpr std::size_t PriceText = 63;
constexpr std::size_t NotEnoughZenText = 423;
constexpr std::size_t NoPriceText = 1101;
// 0x5b30e4..0x5b3132: the price's color, with signed comparisons.
constexpr std::int32_t RedPrice = 10000000;
constexpr std::int32_t GoldPrice = 1000000;
constexpr std::int32_t GreenPrice = 100000;

std::int32_t PriceColor(std::int32_t price)
{
    if (price >= RedPrice)
        return TooltipRed;
    if (price >= GoldPrice)
        return TooltipGold;
    if (price >= GreenPrice)
        return TooltipGreen;
    return TooltipWhite;
}

// 0x5b2f99..0x5b2ff3: the place's price in the mode's map (0x6c931c): 1 the own one's (0x7245e4), 2 another's
// (0x7245e0), with map::find via lower_bound (0x5f8260). With no map yet, with another mode or without the place,
// nothing.
std::optional<std::int32_t> PriceAt(const Social::PersonalShopState& shops, std::int32_t place)
{
    if (!shops.pricesBuilt || (shops.mode != Social::OwnShopView && shops.mode != Social::OtherShopView))
        return std::nullopt;
    const std::map<std::int32_t, std::int32_t>& prices =
        shops.mode == Social::OwnShopView ? shops.ownPrices : shops.otherPrices;
    const auto found = prices.find(place);
    if (found == prices.end())
        return std::nullopt;
    return found->second;
}
} // namespace

Core::Expected<void, std::string> WriteThePersonalShopPrice(Hud::GameHudContext& context, TooltipWriter& lines,
                                                            const Kernel::ItemState& item)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    // 0x5b2f7f: only with the personal shop grid's item under the mouse (0x78abaa4, 0x5c4110).
    if (runtime.ui.shopGridHovered == 0)
        return {};
    const Social::PersonalShopState& shops = runtime.social.personalShop;
    const std::int32_t place = item.gridColumn + item.gridRow * ShopColumns + FirstShopPlace;
    const std::optional<std::int32_t> found = PriceAt(shops, place);
    if (!found)
    {
        // 0x5b3697..0x5b3714: with the own one, 1101 in red and bold, and a "\n".
        if (shops.mode != Social::OwnShopView)
            return {};
        lines.At("0x5b36a0");
        lines.PrintText(NoPriceText);
        lines.Color(TooltipRed);
        lines.Bold(1);
        lines.Next();
        lines.NewLine();
        return {};
    }
    // 0x5b2ff9..0x5b3165: 63 with the price written unsigned (0x6c9844..0x6c9870, the arithmetic of 0x5b1130), in the
    // price's color, in bold, and a "\n".
    const std::int32_t price = *found;
    lines.At("0x5b30bd");
    lines.PrintText(PriceText, std::string_view(Ui::FormatZen(static_cast<std::uint32_t>(price))));
    lines.Color(PriceColor(price));
    lines.Bold(1);
    lines.Next();
    lines.NewLine();
    if (lines.Failed())
        return Core::Unexpected(lines.Error());
    // 0x5b3166..0x5b360b: the own record's zen (+0xecc) with its access opened and closed (0x57630c8, the 3848 rand()
    // if its count returns to 0), in the own one too.
    std::uint32_t zen = 0;
    {
        const Models::LogicalAccess access(context.ownRecordDepth, context.random, Models::OwnRecordCloseRandoms);
        zen = context.world.OwnCharacter().zen;
    }
    // 0x5b360e..0x5b367e: unsigned, the zen that is not enough; only viewing another's, 423 in red and bold, and a
    // "\n".
    if (zen >= static_cast<std::uint32_t>(price) || shops.mode != Social::OtherShopView)
        return {};
    lines.At("0x5b362e");
    lines.PrintText(NotEnoughZenText);
    lines.Color(TooltipRed);
    lines.Bold(1);
    lines.Next();
    lines.NewLine();
    return {};
}
} // namespace Client::Tooltips
