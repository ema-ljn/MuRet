#include "Core/Expected.h"
#include "Render/Models/LogicalAccess.h"
#include "Scenes/GameHud.h"
#include "Scenes/GameInventoryHud.h"
#include "Scenes/ItemTooltipParts.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/SocialState.h"
#include "Scenes/TooltipLines.h"
#include "Scenes/TooltipWriter.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/GameUiState.h"
#include "World/ItemState.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>

// 0x5b2f7f..0x5b3716 del cuerpo del ítem (CL9 hito 7, fase 6a; 06 § Las tiendas personales, "El dibujo"): el precio de
// la tienda personal. Los textos son los de Text.bmd (la dirección − 0x76bd1dc, sobre 300).
namespace Client::Scenes
{
namespace
{
// 0x5b2f9f: el lugar de la casilla en los mapas de los precios, columna (D+0x42) + fila (D+0x43) · 8 + 0x4c.
constexpr std::int32_t ShopColumns = 8;
constexpr std::int32_t FirstShopPlace = 0x4c;
// 0x76c1bb0 (63, el precio con "%s"), 0x76dc190 (423, el zen que no alcanza) y 0x770dc18 (1101, sin precio).
constexpr std::size_t PriceText = 63;
constexpr std::size_t NotEnoughZenText = 423;
constexpr std::size_t NoPriceText = 1101;
// 0x5b30e4..0x5b3132: el color del precio, con comparaciones con signo.
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

// 0x5b2f99..0x5b2ff3: el precio del lugar en el mapa del modo (0x6c931c): 1 el de la propia (0x7245e4), 2 el de la de
// otro (0x7245e0), con map::find por lower_bound (0x5f8260). Sin mapa todavía, con otro modo o sin el lugar, nada.
std::optional<std::int32_t> PriceAt(const PersonalShopState& shops, std::int32_t place)
{
    if (!shops.pricesBuilt || (shops.mode != OwnShopView && shops.mode != OtherShopView))
        return std::nullopt;
    const std::map<std::int32_t, std::int32_t>& prices =
        shops.mode == OwnShopView ? shops.ownPrices : shops.otherPrices;
    const auto found = prices.find(place);
    if (found == prices.end())
        return std::nullopt;
    return found->second;
}
} // namespace

Core::Expected<void, std::string> WriteThePersonalShopPrice(GameHudContext& context, TooltipWriter& lines,
                                                            const World::ItemState& item)
{
    WorldRuntime& runtime = context.world.Runtime();
    // 0x5b2f7f: solo con el ítem de la grilla de la tienda personal bajo el mouse (0x78abaa4, 0x5c4110).
    if (runtime.ui.shopGridHovered == 0)
        return {};
    const PersonalShopState& shops = runtime.social.personalShop;
    const std::int32_t place = item.gridColumn + item.gridRow * ShopColumns + FirstShopPlace;
    const std::optional<std::int32_t> found = PriceAt(shops, place);
    if (!found)
    {
        // 0x5b3697..0x5b3714: con la propia, el 1101 en rojo y en negrita, y un "\n".
        if (shops.mode != OwnShopView)
            return {};
        lines.At("0x5b36a0");
        lines.PrintText(NoPriceText);
        lines.Color(TooltipRed);
        lines.Bold(1);
        lines.Next();
        lines.NewLine();
        return {};
    }
    // 0x5b2ff9..0x5b3165: el 63 con el precio escrito sin signo (0x6c9844..0x6c9870, la cuenta de 0x5b1130), en el
    // color del precio, en negrita, y un "\n".
    const std::int32_t price = *found;
    lines.At("0x5b30bd");
    lines.PrintText(PriceText, std::string_view(FormatZen(static_cast<std::uint32_t>(price))));
    lines.Color(PriceColor(price));
    lines.Bold(1);
    lines.Next();
    lines.NewLine();
    if (lines.Failed())
        return Core::Unexpected(lines.Error());
    // 0x5b3166..0x5b360b: el zen del registro propio (+0xecc) con su acceso abierto y cerrado (0x57630c8, los 3848
    // rand() si su cuenta vuelve a 0), en la propia también.
    std::uint32_t zen = 0;
    {
        const Render::Models::LogicalAccess access(context.ownRecordDepth, context.random,
                                                   Render::Models::OwnRecordCloseRandoms);
        zen = context.world.OwnCharacter().zen;
    }
    // 0x5b360e..0x5b367e: sin signo, el zen que no alcanza; solo mirando la de otro, el 423 en rojo y en negrita, y un
    // "\n".
    if (zen >= static_cast<std::uint32_t>(price) || shops.mode != OtherShopView)
        return {};
    lines.At("0x5b362e");
    lines.PrintText(NotEnoughZenText);
    lines.Color(TooltipRed);
    lines.Bold(1);
    lines.Next();
    lines.NewLine();
    return {};
}
} // namespace Client::Scenes
