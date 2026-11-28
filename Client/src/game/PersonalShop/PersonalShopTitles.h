#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace Client::Kernel
{
class WorldState;
} // namespace Client::Kernel

namespace Client::Social
{
struct PersonalShopState;
} // namespace Client::Social

// The personal shops' signs: the map of 0x7245c8's singleton (0x451d10) per character (CL9 milestone 7, phase 3; 06 §
// Lo social en pantalla, "Las tiendas personales"). The methods here change the map; the draw (0x451c20) and what it
// measures for it are in PersonalShop/ShopSigns (phase 6b).
namespace Client::PersonalShop
{

// 0x4511f0 (AddShopTitle): with a character that is not a player (+0x88 other than 1), only the log (0x6c7938).
// Otherwise, without a sign a new one (0x4521d0 and 0x4524e0) with the key & 0x7fff, the name (+0x1d9), the title and
// the position at (−1, −1); with a sign of the same key (the stored one against the whole one, 07 Q-C969), the new
// name and title (0x452540) and the position at (−1, −1); with another key, only the log (0x6c7900). The two branches
// that write measure: text 1104 and the name in bold (SelectObject of the DC with 0x5763558, 0x452760 and 0x4531c6)
// and the title's lines with the normal font (0x5763554, 0x4531f7), which stays in the DC.
void AddShopTitle(Kernel::WorldState& world, std::int32_t key, std::size_t index, std::string_view title);

// 0x451430
void RemoveShopTitle(Social::PersonalShopState& shops, std::size_t index);

// 0x451810
[[nodiscard]] bool HasShopTitle(const Social::PersonalShopState& shops, std::size_t index);

// 0x451720 and 0x4516d0 (+0x44)
void HideShopTitle(Social::PersonalShopState& shops, std::size_t index);
void ShowShopTitle(Social::PersonalShopState& shops, std::size_t index);

// 0x451770 (+0x44)
[[nodiscard]] bool ShopTitleShown(const Social::PersonalShopState& shops, std::size_t index);

// 0x451630: the character of the first sign with the key, in the map's order; without any, nothing (the original's 0).
[[nodiscard]] std::optional<std::size_t> ShopTitleWithKey(const Social::PersonalShopState& shops, std::int32_t key);

// 0x451c10 and 0x451fe0 (CheckKeyIntegrity), every frame with the map not empty: removes the sign whose character
// changed key (MOVSX of +0x1f4 against the sign's) or stopped being a player, with its log (0x6c79d8 and 0x6c7974).
void CheckTheShopKeys(Kernel::WorldState& world);

// 0x451590, after each 0x510f70 of the player's key (the map load at 0x61a5b5, also in the selection; the F3 04 at
// 0x4c4e86 and the 1C at 0x4c77eb): removes the signs whose key is not the published hero's (MOVSX of +0x1f4 of
// 0x743a170, 0x4515ae). Only the own one stays.
void KeepTheHeroShopTitle(Kernel::WorldState& world);
} // namespace Client::PersonalShop
