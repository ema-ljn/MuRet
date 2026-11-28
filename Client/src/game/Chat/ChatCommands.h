#pragma once

#include "Core/Expected.h"
#include "Tables/ItemSetTables.h"
#include "Tables/ItemTable.h"
#include "Tables/TextTable.h"
#include "Chat/ChatInput.h"
#include "Chat/ChatLog.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// The chat commands (CL8 milestone 3; 06 § El chat): 0x56c440(text, from a macro), 42 KB that the WndProc calls
// before sending what was typed, and the macros with 1. It is ported as a dispatch (a CL8 plan decision): the gates,
// the order of the comparisons, the Chaos Castle notice each command starts with, /move, /filter, /1../0 and the
// names' tail; each social command's part stops with its block's address (CL9.4), where no rand() has been drawn yet.
// The comparisons draw no rand(): IsOpen(0x40) does not read the protection table. *Static*.
namespace Client::Chat
{
// The Text.bmd texts it compares and gives as notices.
inline constexpr std::size_t ExchangeCommandText = 258;     // "/exchange"
inline constexpr std::size_t TradeCommandText = 259;        // "/trade"
inline constexpr std::size_t MoveCommandText = 260;         // "/move"
inline constexpr std::size_t GuildCommandText = 254;        // "/guild"
inline constexpr std::size_t PartyCommandText = 256;        // "/party"
inline constexpr std::size_t GuildWarCommandText = 248;     // the guild war one
inline constexpr std::size_t SoccerCommandText = 249;       // the Battle Soccer one
inline constexpr std::size_t FilterCommandText = 753;       // "/filter"
inline constexpr std::size_t DuelStartCommandText = 908;    // "/Duelchallenge"
inline constexpr std::size_t DuelEndCommandText = 909;      // "/DuelCancel"
inline constexpr std::size_t ShopCommandText = 1117;        // "/Personal store"
inline constexpr std::size_t BuyCommandText = 1118;         // "/Buy"
inline constexpr std::size_t ShopViewOnCommandText = 1136;  // 0x771051c, "/ver puestos"
inline constexpr std::size_t ShopViewOffCommandText = 1137; // 0x7710648, its pair
// 0x56d730 and 0x56d784: the notices of the signs turned on (0x7710774, type 1) and off (0x77108a0, type 2).
inline constexpr std::size_t ShopViewOnNoticeText = 1138;
inline constexpr std::size_t ShopViewOffNoticeText = 1139;
inline constexpr std::size_t AllianceCommandText = 1354;
inline constexpr std::size_t RivalCommandText = 1356;
inline constexpr std::size_t RivalOffCommandText = 1357;
// "Cannot be done in Chaos Castle", with type 1.
inline constexpr std::size_t ChaosCastleCommandText = 1150;
// 0x56c666: the level is not enough to trade (0x76e0204).
inline constexpr std::size_t TradeLevelText = 478;
// 0x56e9f3: there is already a duel (0x7700220).
inline constexpr std::size_t DuelBusyText = 915;
// 0x576718: /personalshop with the trainer open (0x770f388), with type 1.
inline constexpr std::size_t ShopTrainerText = 1121;
// 0x576860: /personalshop without the level (0x770f5e0), a format with the level, with type 1.
inline constexpr std::size_t ShopLevelText = 1123;
// 0x576810: the level to open one's own shop.
inline constexpr std::uint16_t ShopLevel = 6;

// A notice of 0x555190 with the name empty: the Text.bmd text `text` and its type.
// 0x6c8dc8: its .data value.
inline constexpr std::uint8_t DefaultTradeLevel = 6;

struct ChatCommandNotice
{
    std::size_t text = 0;
    std::int32_t type = 0;
    // The text already built by sprintf with the format text `text` (the requests of 0x56cde9 and 0x56fd75); empty, the
    // text as is.
    std::optional<std::string> formatted{};

    friend bool operator==(const ChatCommandNotice&, const ChatCommandNotice&) = default;
};

// 0x570fca..0x571068: which of 0x570d48's three ran: strcmp with the text 1354 or _stricmp with "/union", the
// alliance; with the 1356 or "/rival", the rivalry; if not, the end of the rivalry (/rivaloff or the 1357).
enum class GuildRelationCommand
{
    Union,
    Rival,
    RivalOff,
};

struct ChatCommandOutcome
{
    // With true, the WndProc sends nothing.
    bool handled = false;
    std::vector<ChatCommandNotice> notices;
    // 0x57693f: the 0x19 (a stored macro or an item's help).
    bool sound = false;
};

struct ChatCommandContext
{
    const Tables::TextTable& texts;
    // *[0x7758550] and the 64 of [0x6eed3c] + 0x8004: the names' tail. Without one, it has no names to compare.
    const Tables::ItemTable* items = nullptr;
    const Tables::ItemSetOptionTable* setOptions = nullptr;
    // 0x6c9dd0.
    std::int32_t map = -1;
    // 0x576793c
    bool logoutRequested = false;
    // IsOpen(0x40), the warehouse, which skips the trade.
    bool warehouseOpen = false;
    // [0x576350c]: 0x45dc40, the filter.
    Ui::ChatLogState& log;
    // 0x77f1db0.
    std::array<std::string, Tables::MacroCount>& macros;
    // 0x77f3b08: /move sets it to 4, an item's help to 3 and a set's name to 0.
    std::int32_t& overlayMode;
    // 0x77f3b0c
    std::int32_t& helpItem;
    // [0x6eed3c] + 0x9305, the set's label (0x40ebd0, and 0x5ae360 turns it off); without it, it does not write it.
    std::uint8_t* setLabel = nullptr;
    // +0x14 of the signs' map (0x451d10): "/ver puestos" sets it to 1 (0x4516a0) and its pair to 0 (0x4516b0). Without
    // it, those two cases stop.
    std::uint8_t* signsShown = nullptr;
    // 0x56c5d0..0x56c652 (/trade): the own record's level (+0xe), opening and closing it (its rand()). Without it,
    // /trade stops there.
    std::function<std::uint16_t()> ownLevel{};
    // 0x6c8dc8 (.data 6)
    std::uint8_t tradeLevel = DefaultTradeLevel;
    // 0x78abad8
    bool duelActive = false;
    // 0x56ea6c..0x56ee2c (/duelend with the duel in progress): the C1 03 AB in C3, with its two rand(). Without it,
    // that case stops.
    std::function<Core::Expected<void, std::string>()> endTheDuel{};
    // 0x56c68b..0x56d623 (/trade with the level) and 0x56f13f..0x570d47 (/party outside Chaos Castle): whom to ask (the
    // selected one 0x6c8e30 or the one in front), its packet and its notice (Social::RequestATrade and RequestAParty);
    // what they return is the command's outcome. Without them, those cases stop.
    std::function<Core::Expected<ChatCommandOutcome, std::string>()> requestATrade{};
    std::function<Core::Expected<ChatCommandOutcome, std::string>()> requestAParty{};
    // 0x56d823..0x56e9ec (/duelstart without a duel): the AA to the selected one or to the one in front, and the chain
    // goes on (Social::RequestADuel). Without it, that case stops.
    std::function<Core::Expected<void, std::string>()> requestADuel{};
    // IsOpen(0x8000), the trainer (without rand()): /personalshop does not open the shop.
    bool trainerOpen = false;
    // 0x576815..0x576822 (/personalshop with the level): 0x6599a0(0x800, 0), with what opening the shop does
    // (PersonalShop::OpenThePersonalShop). Without it, that case stops.
    std::function<Core::Expected<void, std::string>()> openThePersonalShop{};
    // 0x574c0c..0x57694d (/purchase outside Chaos Castle): the panels that block it, the selected one, the name or the
    // one in front and the C1 10 3F 05 (Social::RequestTheShopItems). It receives what strlen and strcmp read in
    // sscanf's second buffer (EBP − 0xc0): the second word, or no value if sscanf did not fill it; what it returns is
    // the command's outcome. Without it, that case stops.
    std::function<Core::Expected<ChatCommandOutcome, std::string>(std::optional<std::string_view>)> requestShopItems{};
    // 0x573f85..0x574bd5 (/guild outside Chaos Castle) and 0x570d7e..0x573f48 (/union, /rival and /rivaloff): the
    // hero's guild, whom to ask (the selected one or the one in front), the C1 05 50 or the E5 and its notice
    // (Hud::RequestAGuildJoin and RequestAGuildRelation); what they return is the command's outcome. Without them,
    // those cases stop.
    std::function<Core::Expected<ChatCommandOutcome, std::string>()> requestAGuildJoin{};
    std::function<Core::Expected<ChatCommandOutcome, std::string>(GuildRelationCommand)> requestAGuildRelation{};
};

// 0x56c440(text, from a macro).
[[nodiscard]] Core::Expected<ChatCommandOutcome, std::string> RunTheChatCommand(const ChatCommandContext& context,
                                                                                std::string_view text, bool fromMacro);

// 0x56c1f0: what follows "/N " is one of the commands a macro cannot store.
[[nodiscard]] bool IsAForbiddenMacro(const Tables::TextTable& texts, std::string_view text);
} // namespace Client::Chat
