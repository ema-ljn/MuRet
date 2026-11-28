#include "PersonalShop/PersonalShopTitles.h"

#include "Actors/ActorName.h"
#include "Render/Text/FontSet.h"
#include "Kernel/ActorState.h"
#include "Chat/ChatBubbles.h"
#include "Social/SocialState.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace Client::PersonalShop
{
namespace
{
// +0x88 of a player.
constexpr std::uint8_t PlayerKind = 1;
// 0x4524ef: the add stores the key with & 0x7fff at +0x40.
constexpr std::int32_t StoredKeyMask = 0x7fff;
// 0x451348..0x451351 and 0x4512ef..0x4512fe: the position that the add and the update give 0x4527d0.
constexpr std::int32_t Unplaced = -1;

std::int32_t KeyOf(const Kernel::ActorState& actor)
{
    return static_cast<std::int32_t>(static_cast<std::int16_t>(actor.key));
}
} // namespace

void AddShopTitle(Kernel::WorldState& world, std::int32_t key, std::size_t index, std::string_view title)
{
    const Kernel::ActorState& actor = world.Actors()[index];
    // 0x45122a: "AddShopTitle: there is NOT player" to the log, nothing else.
    if (actor.byte88 != PlayerKind)
        return;
    Kernel::WorldRuntime& runtime = world.Runtime();
    Social::PersonalShopState& shops = runtime.social.personalShop;
    const auto found = shops.titles.find(index);
    if (found == shops.titles.end())
    {
        // 0x451326..0x451382: the new sign (0x4524e0), with the key & 0x7fff and the position at (−1, −1).
        shops.titles.emplace(index, Social::ShopTitle{.key = key & StoredKeyMask,
                                                      .name = Actors::ChatNameOf(actor),
                                                      .title = std::string(title),
                                                      .x = Unplaced,
                                                      .y = Unplaced});
    }
    else if (found->second.key == key)
    {
        // 0x4512d1..0x451301: the stored key (0x452530, already with & 0x7fff) against the whole one, which from 0x8000
        // never matches (07 Q-C969); the new name and title (0x452540) and the position at (−1, −1) (0x4527d0).
        found->second.name = Actors::ChatNameOf(actor);
        found->second.title = std::string(title);
        found->second.x = Unplaced;
        found->second.y = Unplaced;
    }
    else
    {
        // 0x451308: "AddShopTitle: player key value is different" to the log, nothing else.
        return;
    }
    // 0x4531f7: after the name in bold, the lines with the normal font, which stays.
    runtime.hud.dcFont = Render::Text::FontKind::Normal;
}

void RemoveShopTitle(Social::PersonalShopState& shops, std::size_t index)
{
    shops.titles.erase(index);
}

bool HasShopTitle(const Social::PersonalShopState& shops, std::size_t index)
{
    return shops.titles.contains(index);
}

void HideShopTitle(Social::PersonalShopState& shops, std::size_t index)
{
    if (const auto found = shops.titles.find(index); found != shops.titles.end())
        found->second.visible = false;
}

void ShowShopTitle(Social::PersonalShopState& shops, std::size_t index)
{
    if (const auto found = shops.titles.find(index); found != shops.titles.end())
        found->second.visible = true;
}

bool ShopTitleShown(const Social::PersonalShopState& shops, std::size_t index)
{
    const auto found = shops.titles.find(index);
    return found != shops.titles.end() && found->second.visible;
}

std::optional<std::size_t> ShopTitleWithKey(const Social::PersonalShopState& shops, std::int32_t key)
{
    for (const auto& [index, shop] : shops.titles)
    {
        if (shop.key == key)
            return index;
    }
    return std::nullopt;
}

void CheckTheShopKeys(Kernel::WorldState& world)
{
    Social::PersonalShopState& shops = world.Runtime().social.personalShop;
    const auto actors = world.Actors();
    for (auto shop = shops.titles.begin(); shop != shops.titles.end();)
    {
        const Kernel::ActorState& actor = actors[shop->first];
        if (KeyOf(actor) == shop->second.key && actor.byte88 == PlayerKind)
            ++shop;
        else
            shop = shops.titles.erase(shop);
    }
}

void KeepTheHeroShopTitle(Kernel::WorldState& world)
{
    const std::int32_t heroKey = KeyOf(world.Hero());
    Social::PersonalShopState& shops = world.Runtime().social.personalShop;
    for (auto shop = shops.titles.begin(); shop != shops.titles.end();)
    {
        if (shop->second.key == heroKey)
            ++shop;
        else
            shop = shops.titles.erase(shop);
    }
}
} // namespace Client::PersonalShop
