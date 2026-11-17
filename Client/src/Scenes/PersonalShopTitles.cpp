#include "Scenes/PersonalShopTitles.h"

#include "Render/Text/FontSet.h"
#include "Scenes/ActorState.h"
#include "Scenes/ChatBubbles.h"
#include "Scenes/SocialState.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace Client::Scenes
{
namespace
{
// +0x88 de un jugador.
constexpr std::uint8_t PlayerKind = 1;
// 0x4524ef: el alta guarda la clave con & 0x7fff en +0x40.
constexpr std::int32_t StoredKeyMask = 0x7fff;
// 0x451348..0x451351 y 0x4512ef..0x4512fe: el lugar que el alta y la actualización le dan a 0x4527d0.
constexpr std::int32_t Unplaced = -1;

std::int32_t KeyOf(const ActorState& actor)
{
    return static_cast<std::int32_t>(static_cast<std::int16_t>(actor.key));
}
} // namespace

void AddShopTitle(WorldState& world, std::int32_t key, std::size_t index, std::string_view title)
{
    const ActorState& actor = world.Actors()[index];
    // 0x45122a: "AddShopTitle: there is NOT player" al log, nada más.
    if (actor.byte88 != PlayerKind)
        return;
    WorldRuntime& runtime = world.Runtime();
    PersonalShopState& shops = runtime.social.personalShop;
    const auto found = shops.titles.find(index);
    if (found == shops.titles.end())
    {
        // 0x451326..0x451382: el cartel nuevo (0x4524e0), con la clave & 0x7fff y el lugar en (−1, −1).
        shops.titles.emplace(index, ShopTitle{.key = key & StoredKeyMask,
                                              .name = ChatNameOf(actor),
                                              .title = std::string(title),
                                              .x = Unplaced,
                                              .y = Unplaced});
    }
    else if (found->second.key == key)
    {
        // 0x4512d1..0x451301: la clave guardada (0x452530, ya con & 0x7fff) contra la entera, que desde 0x8000 no se
        // iguala nunca (07 Q-C969); el nombre y el título nuevos (0x452540) y el lugar en (−1, −1) (0x4527d0).
        found->second.name = ChatNameOf(actor);
        found->second.title = std::string(title);
        found->second.x = Unplaced;
        found->second.y = Unplaced;
    }
    else
    {
        // 0x451308: "AddShopTitle: player key value is different" al log, nada más.
        return;
    }
    // 0x4531f7: después del nombre en negrita, las líneas con la normal, que queda.
    runtime.hud.dcFont = Render::Text::FontKind::Normal;
}

void RemoveShopTitle(PersonalShopState& shops, std::size_t index)
{
    shops.titles.erase(index);
}

bool HasShopTitle(const PersonalShopState& shops, std::size_t index)
{
    return shops.titles.contains(index);
}

void HideShopTitle(PersonalShopState& shops, std::size_t index)
{
    if (const auto found = shops.titles.find(index); found != shops.titles.end())
        found->second.visible = false;
}

void ShowShopTitle(PersonalShopState& shops, std::size_t index)
{
    if (const auto found = shops.titles.find(index); found != shops.titles.end())
        found->second.visible = true;
}

bool ShopTitleShown(const PersonalShopState& shops, std::size_t index)
{
    const auto found = shops.titles.find(index);
    return found != shops.titles.end() && found->second.visible;
}

std::optional<std::size_t> ShopTitleWithKey(const PersonalShopState& shops, std::int32_t key)
{
    for (const auto& [index, shop] : shops.titles)
    {
        if (shop.key == key)
            return index;
    }
    return std::nullopt;
}

void CheckTheShopKeys(WorldState& world)
{
    PersonalShopState& shops = world.Runtime().social.personalShop;
    const auto actors = world.Actors();
    for (auto shop = shops.titles.begin(); shop != shops.titles.end();)
    {
        const ActorState& actor = actors[shop->first];
        if (KeyOf(actor) == shop->second.key && actor.byte88 == PlayerKind)
            ++shop;
        else
            shop = shops.titles.erase(shop);
    }
}

void KeepTheHeroShopTitle(WorldState& world)
{
    const std::int32_t heroKey = KeyOf(world.Actors()[world.Runtime().hero]);
    PersonalShopState& shops = world.Runtime().social.personalShop;
    for (auto shop = shops.titles.begin(); shop != shops.titles.end();)
    {
        if (shop->second.key == heroKey)
            ++shop;
        else
            shop = shops.titles.erase(shop);
    }
}
} // namespace Client::Scenes
