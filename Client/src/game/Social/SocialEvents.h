#pragma once

#include "Core/Expected.h"
#include "Protocol/Social/Duel.h"
#include "Protocol/Outgoing.h"
#include "Protocol/Party/Party.h"
#include "Protocol/PersonalShop/PersonalShop.h"
#include "Protocol/Social/Trade.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

// What the incoming trade, party and duel do (CL9 milestone 7; 04 § El comercio, § La party and § El duelo): the cases
// of dispatcher 0x4e2b10 and their functions, over SocialState's globals. What fails says so with the address and the
// wave.
namespace Client::Core
{
class Log;
}

namespace Client::Crt
{
class CrtRandom;
}

namespace Client::Kernel
{
struct ItemState;
}

namespace Client::Kernel
{
class WorldState;
} // namespace Client::Kernel

namespace Client::Scenes
{
struct WorldEntryResources;
} // namespace Client::Scenes

namespace Client::Social
{
struct TradeState;

using GamePacketSender = std::function<void(Protocol::Outgoing)>;

// 0x4d4af0 with the encrypted 36. With neither the personal shop (0x800) nor another's (0x20000) open, which take no
// rand(): 0x78aba26 at 0, the name in 0x78aba1c, dialog 0x78 (0x6236f0) and, with an item on the cursor, 0x5be150.
// With either open, the C1 04 37 00 it returns to send in the clear, touching nothing.
[[nodiscard]] Core::Expected<std::optional<Protocol::Outgoing>, std::string>
ApplyTradeRequest(const Protocol::Social::TradeRequest& request, const Scenes::WorldEntryResources& resources);

// 0x4d4b22..0x4d5238, the 36 in the clear: the warning C1 06 F1 03 00 36 in C3, with its two rand() (the padding and
// the serial).
[[nodiscard]] Protocol::Outgoing ApplyClearTradeRequest(Crt::CrtRandom& random);

// 0x4d5570: 0 text 492 and 2 text 493 (type 2); with 1, Open(0x20), the SetCursorPos request, the zen and the OKs at
// 0, 0x5bbdf0 if 0x78abaa5 is at 0 and the name changed (strcmp), and the other's name, guild and level.
[[nodiscard]] Core::Expected<void, std::string> ApplyTradeResponse(const Protocol::Social::TradeResponse& response,
                                                                   const Scenes::WorldEntryResources& resources);

// 0x4e3d6a: 0x5bd750(slot, 0x78a5a20, 8) (with 0x5bc5e0 before and 0x5bc470 after) and sound 0x1d. A slot from 32
// (other than 0xff), a footprint that runs past the grid or an eleventh item equal to its copy (Q-C961) take the
// original outside the grid or the stack: the whole packet is discarded, without sound, with a warning in the log
// (Q-C200).
[[nodiscard]] Core::Expected<void, std::string> ApplyTradeItemRemoved(const Protocol::Social::TradeItemRemoved& removed,
                                                                      const Scenes::WorldEntryResources& resources);

// 0x4e3d99: 0x5bc740(0x78a5a20, 8, 4, slot, item, 1), with 0x5bc470 at the end, and sound 0x1d. A footprint that runs
// past cell 31 (Q-C200) is discarded before writing and before 0x54bf40's rand(); a twelfth item equal to its copy
// (0x5bc545, Q-C961), after putting it in a copy of the trade, which is thrown away with the rand() it took. Both
// without sound and with a warning in the log. The eleventh drops out of the list and the count's 1 is compared in its
// place.
[[nodiscard]] Core::Expected<void, std::string> ApplyTradeItemAdded(const Protocol::Social::TradeItemAdded& added,
                                                                    const Scenes::WorldEntryResources& resources);

// 0x4e3dd0: 0x78abab8 with the sent zen (0x57678ac) if it accepted it, or 0.
void ApplyTradeMoneyResult(const Protocol::Social::TradeMoneyResult& result, Kernel::WorldState& world);

// 0x4e3df3: the other's zen into 0x78abab4.
void ApplyTradeOtherMoney(const Protocol::Social::TradeOtherMoney& money, Kernel::WorldState& world);

// 0x4e3e0a: 0 turns the other's OK off, 1 turns it on and 2 turns the own off; always sound 0x19.
void ApplyTradeOkState(const Protocol::Social::TradeOkState& state, Kernel::WorldState& world);

// 0x4d57f0: with the lock 0x78abb39, stores it for 0x4d1400 (once); otherwise texts 494 (with 0x5bc5e0 on each of the
// other's items), 495 and 496 (with dialog 0), the cursor's count at 0, the closing of dialog 0x73 and Close(0x20, 1),
// without the 3D.
[[nodiscard]] Core::Expected<void, std::string> ApplyTradeResult(const Protocol::Social::TradeResult& result,
                                                                 const Scenes::WorldEntryResources& resources);

// 0x5bc107..0x5bc13f, the tail of 0x5bbe10 (from 0x633f20, on entering the selection, before any trade): 0x78abaa5 at
// 0 and the 32 entries of the two grids (0x78a5a20 and 0x78a92a8) and of the copy (0x77f3dc8) with the type at −1 and
// +0x3c at 0. No rand().
void ClearTheTradeForSelection(TradeState& trade);

// 0x657fb5..0x6584a4, the trade's close inside 0x657ab0: with `aux` at 0, the C1 03 3D in C3 (two rand() before
// sending it through `send`; without it, fails); then the 32 cells of 0x78a92a8 and 0x78a5a20 emptied (type −1 and
// +0x3c at 0), with 0x5bbdf0 on each pass if 0x78abaa5 is at 1.
[[nodiscard]] Core::Expected<void, std::string> CloseTheTradeGrids(Kernel::WorldState& world, Crt::CrtRandom& random,
                                                                   bool aux, const GamePacketSender& send);

// 0x5bc300(a, b): 2 with another type; 0 with another +9; otherwise the level ((+4 >> 3) & 0xf), bit 7 of +4, the
// excellents (+0x1b & 0x3f), the option count (+0x2a), the option from 0x50 to 0x53 (+0x2b, the value at +0x33) and
// the durability (+0x1a), in that order: −1 if `a` has less, 1 if more, 0 if all equal.
[[nodiscard]] std::int32_t CompareTradeItems(const Kernel::ItemState& a, const Kernel::ItemState& b);

// 0x4e3f41: the inviter's key into 0x78aba4c and dialog 0x77.
void ApplyPartyRequest(const Protocol::Party::PartyRequest& request, Kernel::WorldState& world);

// 0x4e3f6f: texts 497 to 501 and 535 (type 2) for 0 to 5, and dialog 0 always.
[[nodiscard]] Core::Expected<void, std::string>
ApplyPartyRequestResult(const Protocol::Party::PartyRequestResult& result,
                        const Scenes::WorldEntryResources& resources);

// 0x4d67e0: the count into 0x78aba48 and each member in its slot of 0x77f3cd0, with 0 at +0xa. With more than five
// the original writes past the array (Q-C960): the whole packet is discarded, with a warning in `log`.
void ApplyPartyList(const Protocol::Party::PartyList& list, Kernel::WorldState& world, Core::Log& log);

// 0x4e409b: the count at 0, text 502 (type 2) and, with 0x6c8e50 at 0 or more, 0x6c8e50 at −1.
[[nodiscard]] Core::Expected<void, std::string> ApplyPartyLeft(const Protocol::Party::PartyLeft&,
                                                               const Scenes::WorldEntryResources& resources);

// 0x4e4174: for each byte, +0x18 of the high nibble's slot with the low one (up to 10). With a slot of 5 or more the
// original writes past the array (Q-C960): the whole packet is discarded, with a warning in `log`.
void ApplyPartyLife(const Protocol::Party::PartyLife& life, Kernel::WorldState& world, Core::Log& log);

// 0x4dac90: with the result, the duel on, the key, the name, text 912 with the name (type 2) and sound 0x8d; without
// it, the name and text 913.
[[nodiscard]] Core::Expected<void, std::string> ApplyDuelStart(const Protocol::Social::DuelStart& start,
                                                               const Scenes::WorldEntryResources& resources);

// 0x4e4d10: the duel ended, the key, the name, text 914 (type 2) and, with the F3 22's time of type 2 (0x5767814),
// the time at 0 (0x5767816): 0x5a7eb0 stops drawing it.
[[nodiscard]] Core::Expected<void, std::string> ApplyDuelEnd(const Protocol::Social::DuelEnd& end,
                                                             const Scenes::WorldEntryResources& resources);

// 0x4e4d75: with dialog 0x9a open, nothing; otherwise the key, the name, the request (0x78abad9) and sound 0x8c.
void ApplyDuelRequest(const Protocol::Social::DuelRequest& request, Kernel::WorldState& world);

// 0x4e4dd1: the scores by which of the two keys is the rival's; with neither, nothing.
void ApplyDuelScore(const Protocol::Social::DuelScore& score, Kernel::WorldState& world);

// 0x4dae80 (3F 00): for each entry, the key's character (0x5110d0); from 0 to 399, AddShopTitle with the key, the
// character and the title.
void ApplyShopTitles(const Protocol::PersonalShop::ShopTitles& titles, Kernel::WorldState& world);

// 0x4db700 (3F 02): with 1, 0x78aba50 at 1 and AddShopTitle of the published hero (0x743a170) with its key (MOVSX of
// +0x1f4) and the own title (0x78aba64); otherwise only the log "Fault ReceiveCreatePersonalShop" (0x6c84d0).
void ApplyShopCreated(const Protocol::PersonalShop::ShopCreated& created, Kernel::WorldState& world);

// 0x4e4ea5 (3F 03): with 1, the key's character; from 0 to 399, if it is the published hero, 0x78aba50 at 0, and its
// sign leaves the map (0x451430). With another result, only the log (0x6c8504).
void ApplyShopClosed(const Protocol::PersonalShop::ShopClosed& closed, Kernel::WorldState& world);

// 0x4e4f5d (3F 08): the name in 10 bytes terminated at the 11th and sprintf(Text[1122], name) with type 1 and
// 0x5767a8c's empty name. MuRet does not use the text as a format beyond one %s: with 0x70 bytes or more, it fails.
[[nodiscard]] Core::Expected<void, std::string> ApplyShopNotice(const Protocol::PersonalShop::ShopNotice& notice,
                                                                const Scenes::WorldEntryResources& resources);

// 0x4dafe0 (3F 10): the key's character; from 0 to 399 and with the same name (strncmp of 10 with +0x1d9),
// AddShopTitle with the title.
void ApplyOtherShopTitle(const Protocol::PersonalShop::ShopTitle& title, Kernel::WorldState& world);

// 0x4e4fcc (3F 12): with 0x6c931c at 2, Close(0x800, 0), Close(0x20000, 0) and text 1126 with type 2.
[[nodiscard]] Core::Expected<void, std::string> ApplyShopOwnerLeft(const Protocol::PersonalShop::ShopOwnerLeft&,
                                                                   const Scenes::WorldEntryResources& resources,
                                                                   const GamePacketSender& send);

// 0x555190("", Text.bmd text `text`, `type`): the social packets' notices, with the empty .bss buffer as the name
// (08 P-C72).
[[nodiscard]] Core::Expected<void, std::string> AddSocialNotice(const Scenes::WorldEntryResources& resources,
                                                                std::size_t text, std::int32_t type,
                                                                std::string_view where);
} // namespace Client::Social
