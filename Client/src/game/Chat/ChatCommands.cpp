#include "Chat/ChatCommands.h"

#include "Core/Expected.h"
#include "Tables/ItemSetTables.h"
#include "Tables/ItemTable.h"
#include "Tables/TextTable.h"
#include "Maps/MapId.h"
#include "Chat/ChatInput.h"
#include "Chat/ChatLog.h"
#include "Ui/MessageArea.h"
#include "FrontEnd/NameChecks.h"
#include "Ui/PanelRegistry.h"
#include "Crt/SprintfFormat.h"

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

namespace Client::Chat
{
namespace
{
// 0x56c4a1..0x56c4b7: the first word, up to the space, in 0x100 bytes.
constexpr std::size_t FirstWordLimit = 0x100;
// 0x56c54d and the others: Chaos Castle, from 18 to 23.
using Maps::IsChaosCastle;
// 0x56f0c1: "/N " before the macro.
constexpr std::size_t MacroCommandPrefix = 3;
// 0x5768a2: the +0x24 byte of a named item; 0x40ec0c: the +0x3c of a set, 0xff without a set.
constexpr std::size_t ItemNamedByte = 0x24;
constexpr std::size_t SetOptionUsedByte = 0x3c;
constexpr std::byte UnusedSetOption{0xff};

using Tables::TextOr;

// 0x6ada60, _stricmp with the "C" locale: only A..Z fold.
bool EqualsIgnoringCase(std::string_view left, std::string_view right)
{
    const auto lower = [](char letter)
    { return letter >= 'A' && letter <= 'Z' ? static_cast<char>(letter - 'A' + 'a') : letter; };
    return std::ranges::equal(left, right, [&](char a, char b) { return lower(a) == lower(b); });
}

// A record's name as sprintf("%s") reads it: up to its '\0', within the record.
template <std::size_t Size> std::string NameOf(std::span<const std::byte, Size> record)
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

// 0x573f49 (/guild) and 0x570d48 (/union, /rival and /rivaloff): in Chaos Castle, the notice 1150 and it returns 0
// (the text goes out as chat); if not, the scene's request.
Core::Expected<ChatCommandOutcome, std::string> GuildCommand(const ChatCommandContext& context, std::string_view text)
{
    if (IsChaosCastle(context.map))
        return ChatCommandOutcome{.handled = false, .notices = {{ChaosCastleCommandText, Ui::SystemMessage}}};
    if (text == TextOr(context.texts, GuildCommandText) || EqualsIgnoringCase(text, "/guild"))
    {
        if (!context.requestAGuildJoin)
            return Core::Unexpected(std::string("0x573f85: /guild sin el pedido"));
        return context.requestAGuildJoin();
    }
    if (!context.requestAGuildRelation)
        return Core::Unexpected(std::string("0x570d7e: /union, /rival o /rivaloff sin el pedido"));
    GuildRelationCommand command = GuildRelationCommand::RivalOff;
    if (text == TextOr(context.texts, AllianceCommandText) || EqualsIgnoringCase(text, "/union"))
        command = GuildRelationCommand::Union;
    else if (text == TextOr(context.texts, RivalCommandText) || EqualsIgnoringCase(text, "/rival"))
        command = GuildRelationCommand::Rival;
    return context.requestAGuildRelation(command);
}

// 0x5766ce..0x576890: /personalshop. In Chaos Castle, the notice 1150 and 0; with the trainer open, the 1121 and 0; if
// not, the own record's level (+0xe, opening and closing it): from 6, Open(0x800) and 1; if not, sprintf of the text
// 1123 with the 6 into the 0x30 stack bytes (EBP − 0xf0, zeroed) and 1. The notices go with type 1.
Core::Expected<ChatCommandOutcome, std::string> PersonalShopCommand(const ChatCommandContext& context)
{
    constexpr std::size_t LevelTextBufferSize = 0x30;
    if (IsChaosCastle(context.map))
        return ChatCommandOutcome{.handled = false, .notices = {{ChaosCastleCommandText, Ui::SystemMessage}}};
    if (context.trainerOpen)
        return ChatCommandOutcome{.handled = false, .notices = {{ShopTrainerText, Ui::SystemMessage}}};
    if (!context.ownLevel)
        return Core::Unexpected(std::string("0x57673f: /personalshop sin el registro propio"));
    if (context.ownLevel() >= ShopLevel)
    {
        if (!context.openThePersonalShop)
            return Core::Unexpected(std::string("0x576822: /personalshop sin abrir la tienda"));
        if (auto opened = context.openThePersonalShop(); !opened.HasValue())
            return Core::Unexpected(std::move(opened).Error());
        return ChatCommandOutcome{.handled = true, .notices = {}};
    }
    std::string text = Crt::Sprintf(TextOr(context.texts, ShopLevelText), static_cast<std::int32_t>(ShopLevel));
    if (text.size() >= LevelTextBufferSize)
        return Core::Unexpected(std::format("0x576866: el texto {} con el nivel pasa los {} bytes de la pila",
                                            ShopLevelText, LevelTextBufferSize));
    return ChatCommandOutcome{
        .handled = true,
        .notices = {ChatCommandNotice{.text = ShopLevelText, .type = Ui::SystemMessage, .formatted = std::move(text)}}};
}

// 0x574c8c: sscanf(text, "%s %s", EBP − 0x108, EBP − 0xc0). Each %s skips the blanks (isspace of the "C" locale) and
// reads up to the next one, and the format's space skips any there are: what matters is the second word, the name.
// Without it, the second buffer keeps what was on the stack (no value). A first word of 0x48 bytes or more overwrites
// the second buffer and a second one of 0x90 or more reaches EBP − 0x30, which 0x575503 overwrites: MuRet does not
// model it (the chat field has 0x32 bytes) and returns the error.
Core::Expected<std::optional<std::string_view>, std::string> ScanTheName(std::string_view text)
{
    constexpr std::size_t FirstWordBuffer = 0x48;
    constexpr std::size_t NameBuffer = 0x90;
    const auto blank = [](char letter) { return letter == ' ' || (letter >= '\t' && letter <= '\r'); };
    text = text.substr(0, text.find('\0'));
    std::size_t at = 0;
    const auto word = [&]()
    {
        while (at < text.size() && blank(text[at]))
            ++at;
        const std::size_t start = at;
        while (at < text.size() && !blank(text[at]))
            ++at;
        return text.substr(start, at - start);
    };
    const std::string_view first = word();
    const std::string_view second = word();
    if (first.size() >= FirstWordBuffer || second.size() >= NameBuffer)
        return Core::Unexpected(
            std::format("0x574c92: /purchase con una palabra que pasa su buffer de la pila ({} y {} "
                        "bytes)",
                        first.size(), second.size()));
    if (second.empty())
        return std::optional<std::string_view>{};
    return std::optional<std::string_view>{second};
}

// 0x574bd6..0x57694d: /purchase. In Chaos Castle, the notice 1150 and 0; if not, the rest (the panels, the selected
// one, the name sscanf read or the one in front) belongs to the scene's request.
Core::Expected<ChatCommandOutcome, std::string> PurchaseCommand(const ChatCommandContext& context,
                                                                std::string_view text)
{
    if (IsChaosCastle(context.map))
        return ChatCommandOutcome{.handled = false, .notices = {{ChaosCastleCommandText, Ui::SystemMessage}}};
    auto name = ScanTheName(text);
    if (!name.HasValue())
        return Core::Unexpected(std::move(name).Error());
    if (!context.requestShopItems)
        return Core::Unexpected(std::string("0x574c0c: /purchase sin el pedido"));
    return context.requestShopItems(name.Value());
}

// 0x56c54d..0x56c690: /trade. Outside Chaos Castle, the own record's level against 0x6c8dc8: without reaching it, the
// notice 478 and 1. With it, the request to the selected one (0x6c8e30) or to the one in front (0x56ce1e), with the
// C1 05 36.
Core::Expected<ChatCommandOutcome, std::string> TradeCommand(const ChatCommandContext& context)
{
    if (IsChaosCastle(context.map))
        return ChatCommandOutcome{.handled = false, .notices = {{ChaosCastleCommandText, Ui::SystemMessage}}};
    if (!context.ownLevel)
        return Core::Unexpected(std::string("0x56c5d0: /trade sin el registro propio"));
    if (context.ownLevel() < context.tradeLevel)
        return ChatCommandOutcome{.handled = true, .notices = {{TradeLevelText, Ui::SystemMessage}}};
    if (!context.requestATrade)
        return Core::Unexpected(std::string("0x56c68b: /trade sin el pedido"));
    return context.requestATrade();
}

// 0x576894..0x576922 and 0x40ebd0: "/" and an item's name opens its help; if not, 0x40ebd0 compares with "/" and the
// name of each used set (strncmp both ways: equal), and before each comparison sets the label (+0x9305) to 0. With an
// equal one, 0x77f3b08 at 0 and the label at its index + 1. It returns 0 (0x576912).
Core::Expected<ChatCommandOutcome, std::string> TheNamesTail(const ChatCommandContext& context, std::string_view text,
                                                             ChatCommandOutcome outcome)
{
    if (context.items != nullptr)
    {
        for (std::size_t index = 0; index < context.items->Count(); ++index)
        {
            const auto record = context.items->Record(index);
            if (!record || (*record)[ItemNamedByte] == std::byte{0})
                continue;
            if (text != "/" + NameOf(*record))
                continue;
            // 0x576923..0x576952: without the set's label (0x5ae360), the item's help (mode 3), the 0x19 and 1.
            if (context.setLabel != nullptr)
                *context.setLabel = 0;
            context.overlayMode = Ui::ItemHelpMode;
            context.helpItem = static_cast<std::int32_t>(index);
            return ChatCommandOutcome{.handled = true, .notices = {}, .sound = true};
        }
    }
    // 0x576900..0x576912: 0x40ebd0 and 0, which sends the text as chat.
    if (text.empty() || text.front() != '/' || context.setOptions == nullptr)
        return outcome;
    for (std::size_t index = 0; index < context.setOptions->Count(); ++index)
    {
        const auto record = context.setOptions->Record(index);
        if (!record || (*record)[SetOptionUsedByte] == UnusedSetOption)
            continue;
        if (context.setLabel != nullptr)
            *context.setLabel = 0;
        if (text == "/" + NameOf(*record))
        {
            context.overlayMode = 0;
            if (context.setLabel != nullptr)
                *context.setLabel = static_cast<std::uint8_t>(index + 1);
            return outcome;
        }
    }
    return outcome;
}

// 0x56efdc..0x56f104: "/1" to "/9" and "/0" store what follows in their macro, except a command a macro cannot store
// (0x56c1f0): that text goes out as chat, without the tail.
ChatCommandOutcome SetTheMacro(const ChatCommandContext& context, std::string_view text, std::size_t macro)
{
    if (IsAForbiddenMacro(context.texts, text))
        return {};
    const std::string_view kept = text.size() > MacroCommandPrefix ? text.substr(MacroCommandPrefix) : "";
    context.macros[macro] = std::string(kept.substr(0, Ui::ChatBufferSize - 1));
    return ChatCommandOutcome{.handled = true, .notices = {}, .sound = true};
}

// The macro number of "/N", or MacroCount: only the first two bytes are looked at.
std::size_t MacroOf(std::string_view text)
{
    if (text.size() < 2 || text[0] != '/')
        return Tables::MacroCount;
    for (std::size_t macro = 0; macro < Tables::MacroCount; ++macro)
    {
        const char digit = static_cast<char>('0' + (macro + 1) % Tables::MacroCount);
        if (text[1] == digit)
            return macro;
    }
    return Tables::MacroCount;
}
} // namespace

Core::Expected<ChatCommandOutcome, std::string> RunTheChatCommand(const ChatCommandContext& context,
                                                                  std::string_view text, bool fromMacro)
{
    const Tables::TextTable& texts = context.texts;
    if (fromMacro || context.logoutRequested)
        return TheNamesTail(context, text, {});
    if (!context.warehouseOpen)
    {
        const std::string_view word = text.substr(0, std::min({text.find(' '), text.size(), FirstWordLimit}));
        if (word == TextOr(texts, ExchangeCommandText) || word == TextOr(texts, TradeCommandText) ||
            EqualsIgnoringCase(word, "/trade"))
            return TradeCommand(context);
    }
    // 0x56d624..0x56d66a: /move opens the window and goes on.
    if (text == TextOr(texts, MoveCommandText) || EqualsIgnoringCase(text, "/move"))
        context.overlayMode = Ui::MoveOverlayMode;
    if (text == TextOr(texts, ShopCommandText) || EqualsIgnoringCase(text, "/personalshop"))
        return PersonalShopCommand(context);
    if (Ui::ContainsLikeTheOriginal(text, TextOr(texts, BuyCommandText)) ||
        Ui::ContainsLikeTheOriginal(text, "/purchase"))
        return PurchaseCommand(context, text);
    // 0x56d6ee..0x56d796: "/ver puestos" (strcmp with the text 1136) turns the signs on (0x4516a0 over 0x451d10) with
    // the notice 1138 of type 1, and its pair (the 1137) turns them off (0x4516b0) with the 1139 of type 2. Both
    // comparisons always run and the chain goes on.
    ChatCommandOutcome outcome;
    if (text == TextOr(texts, ShopViewOnCommandText))
    {
        if (context.signsShown == nullptr)
            return Core::Unexpected(std::string("0x56d722: \"/ver puestos\" sin el mapa de los carteles"));
        *context.signsShown = 1;
        outcome.notices.push_back(ChatCommandNotice{.text = ShopViewOnNoticeText, .type = Ui::SystemMessage});
    }
    if (text == TextOr(texts, ShopViewOffCommandText))
    {
        if (context.signsShown == nullptr)
            return Core::Unexpected(std::string("0x56d776: apagar los carteles sin su mapa"));
        *context.signsShown = 0;
        outcome.notices.push_back(ChatCommandNotice{.text = ShopViewOffNoticeText, .type = Ui::ErrorMessage});
    }
    // 0x56d7e0..0x56d82b: /duelstart. With the duel in progress, the notice 915; without it, the AA to the selected one
    // or to the one in front. With or without it, the chain goes on (0x56ea05 and 0x56e07c).
    if (text == TextOr(texts, DuelStartCommandText) || EqualsIgnoringCase(text, "/duelstart"))
    {
        if (IsChaosCastle(context.map))
        {
            outcome.notices.push_back(ChatCommandNotice{ChaosCastleCommandText, Ui::SystemMessage});
            return outcome;
        }
        if (context.duelActive)
            outcome.notices.push_back(ChatCommandNotice{.text = DuelBusyText, .type = Ui::ErrorMessage});
        else if (!context.requestADuel)
            return Core::Unexpected(std::string("0x56d823: /duelstart sin el pedido"));
        else if (auto asked = context.requestADuel(); !asked.HasValue())
            return Core::Unexpected(std::move(asked).Error());
    }
    // 0x56ea29..0x56ee40: /duelend. With the duel in progress, the C1 03 AB in C3; either way, the chain goes on.
    if (text == TextOr(texts, DuelEndCommandText) || EqualsIgnoringCase(text, "/duelend"))
    {
        if (IsChaosCastle(context.map))
        {
            outcome.notices.push_back(ChatCommandNotice{ChaosCastleCommandText, Ui::SystemMessage});
            return outcome;
        }
        if (context.duelActive)
        {
            if (!context.endTheDuel)
                return Core::Unexpected(std::string("0x56ea6c: /duelend sin el cable"));
            if (auto ended = context.endTheDuel(); !ended.HasValue())
                return Core::Unexpected(std::move(ended).Error());
        }
    }
    if (text == TextOr(texts, GuildCommandText) || EqualsIgnoringCase(text, "/guild") ||
        text == TextOr(texts, AllianceCommandText) || EqualsIgnoringCase(text, "/union") ||
        text == TextOr(texts, RivalCommandText) || EqualsIgnoringCase(text, "/rival") ||
        text == TextOr(texts, RivalOffCommandText) || EqualsIgnoringCase(text, "/rivaloff"))
        return GuildCommand(context, text);
    if (text == TextOr(texts, PartyCommandText) || EqualsIgnoringCase(text, "/party") ||
        EqualsIgnoringCase(text, "/pt"))
    {
        // 0x56f109..0x56f13e: in Chaos Castle, the notice 1150 and 0; if not, the request of 0x56f13f.
        if (IsChaosCastle(context.map))
            return ChatCommandOutcome{.handled = false, .notices = {{ChaosCastleCommandText, Ui::SystemMessage}}};
        if (!context.requestAParty)
            return Core::Unexpected(std::string("0x56f13f: /party sin el pedido"));
        return context.requestAParty();
    }
    if (const std::size_t macro = MacroOf(text); macro < Tables::MacroCount)
        return SetTheMacro(context, text, macro);
    // 0x56f02e..0x56f0ab: "/filter" (the text 753, if not empty, or the literal) and the tail.
    const std::string_view filter = TextOr(texts, FilterCommandText);
    if ((!filter.empty() && text.starts_with(filter)) || text.starts_with("/filter"))
    {
        if (const auto notice = Ui::SetTheChatFilter(context.log, text))
            outcome.notices.push_back(ChatCommandNotice{*notice, Ui::SystemMessage});
    }
    return TheNamesTail(context, text, std::move(outcome));
}

bool IsAForbiddenMacro(const Tables::TextTable& texts, std::string_view text)
{
    const std::string_view macro = text.size() > MacroCommandPrefix ? text.substr(MacroCommandPrefix) : "";
    return macro == TextOr(texts, ExchangeCommandText) || macro == TextOr(texts, TradeCommandText) ||
           EqualsIgnoringCase(macro, "/trade") || macro == TextOr(texts, PartyCommandText) ||
           EqualsIgnoringCase(macro, "/party") || EqualsIgnoringCase(macro, "/pt") ||
           macro == TextOr(texts, GuildCommandText) || EqualsIgnoringCase(macro, "/guild") ||
           macro == TextOr(texts, GuildWarCommandText) || EqualsIgnoringCase(macro, "/GuildWar") ||
           macro == TextOr(texts, SoccerCommandText) || EqualsIgnoringCase(macro, "/BattleSoccer");
}
} // namespace Client::Chat
