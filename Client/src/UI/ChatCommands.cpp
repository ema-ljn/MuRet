#include "UI/ChatCommands.h"

#include "Core/Expected.h"
#include "Data/Tables/ItemSetTables.h"
#include "Data/Tables/ItemTable.h"
#include "Data/Text/TextTable.h"
#include "UI/ChatInput.h"
#include "UI/ChatLog.h"
#include "UI/MessageArea.h"
#include "UI/NameChecks.h"
#include "UI/PanelRegistry.h"
#include "UI/SprintfFormat.h"

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

namespace Client::UI
{
namespace
{
// 0x56c4a1..0x56c4b7: la primera palabra, hasta el espacio, en 0x100 bytes.
constexpr std::size_t FirstWordLimit = 0x100;
// 0x56c54d y los demás: Chaos Castle, del 18 al 23.
constexpr std::int32_t FirstChaosCastleMap = 18;
constexpr std::int32_t LastChaosCastleMap = 23;
// 0x56f0c1: "/N " antes de la macro.
constexpr std::size_t MacroCommandPrefix = 3;
// 0x5768a2: el byte +0x24 de un ítem con nombre; 0x40ec0c: el +0x3c de un conjunto, 0xff sin conjunto.
constexpr std::size_t ItemNamedByte = 0x24;
constexpr std::size_t SetOptionUsedByte = 0x3c;
constexpr std::byte UnusedSetOption{0xff};

std::string_view TextAt(const Data::Text::TextTable& texts, std::size_t index)
{
    return texts.Text(index).value_or(std::string_view{});
}

// 0x6ada60, _stricmp con el locale "C": solo A..Z se pliegan.
bool EqualsIgnoringCase(std::string_view left, std::string_view right)
{
    const auto lower = [](char letter)
    { return letter >= 'A' && letter <= 'Z' ? static_cast<char>(letter - 'A' + 'a') : letter; };
    return std::ranges::equal(left, right, [&](char a, char b) { return lower(a) == lower(b); });
}

// El nombre de un registro como lo lee sprintf("%s"): hasta su '\0', dentro del registro.
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

bool InChaosCastle(const ChatCommandContext& context)
{
    return context.map >= FirstChaosCastleMap && context.map <= LastChaosCastleMap;
}

// El comienzo de cada comando social: en Chaos Castle, el aviso 1150 y devuelve 0 (el texto sale como chat); si no,
// lo que sigue es de `wave`.
Core::Expected<ChatCommandOutcome, std::string>
SocialCommand(const ChatCommandContext& context, std::string_view address, std::string_view what, std::string_view wave)
{
    if (InChaosCastle(context))
        return ChatCommandOutcome{.handled = false, .notices = {{ChaosCastleCommandText, SystemMessage}}};
    return Core::Unexpected(std::format("{}: {} es de {}", address, what, wave));
}

// 0x5766ce..0x576890: /personalshop. En Chaos Castle, el aviso 1150 y 0; con el entrenador abierto, el 1121 y 0; si no,
// el nivel del registro propio (+0xe, abriéndolo y cerrándolo): desde el 6, Open(0x800) y 1; si no, sprintf del texto
// 1123 con el 6 en los 0x30 bytes de la pila (EBP − 0xf0, en ceros) y 1. Los avisos van con el tipo 1.
Core::Expected<ChatCommandOutcome, std::string> PersonalShopCommand(const ChatCommandContext& context)
{
    constexpr std::size_t LevelTextBufferSize = 0x30;
    if (InChaosCastle(context))
        return ChatCommandOutcome{.handled = false, .notices = {{ChaosCastleCommandText, SystemMessage}}};
    if (context.trainerOpen)
        return ChatCommandOutcome{.handled = false, .notices = {{ShopTrainerText, SystemMessage}}};
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
    std::string text = Sprintf(TextAt(context.texts, ShopLevelText), static_cast<std::int32_t>(ShopLevel));
    if (text.size() >= LevelTextBufferSize)
        return Core::Unexpected(std::format("0x576866: el texto {} con el nivel pasa los {} bytes de la pila",
                                            ShopLevelText, LevelTextBufferSize));
    return ChatCommandOutcome{
        .handled = true,
        .notices = {ChatCommandNotice{.text = ShopLevelText, .type = SystemMessage, .formatted = std::move(text)}}};
}

// 0x574c8c: sscanf(texto, "%s %s", EBP − 0x108, EBP − 0xc0). Cada %s salta los blancos (isspace del locale "C") y lee
// hasta el siguiente, y el espacio del formato salta los que haya: lo que importa es la segunda palabra, el nombre.
// Sin ella, el segundo buffer queda con lo que había en la pila (sin valor). Una primera palabra de 0x48 bytes o más
// pisa el segundo buffer y una segunda de 0x90 o más llega a EBP − 0x30, que 0x575503 pisa: MuRet no lo modela (el
// campo del chat tiene 0x32 bytes) y devuelve el error.
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

// 0x574bd6..0x57694d: /purchase. En Chaos Castle, el aviso 1150 y 0; si no, lo demás (los paneles, el elegido, el
// nombre que sscanf leyó o el que está enfrente) es del pedido de la escena.
Core::Expected<ChatCommandOutcome, std::string> PurchaseCommand(const ChatCommandContext& context,
                                                                std::string_view text)
{
    if (InChaosCastle(context))
        return ChatCommandOutcome{.handled = false, .notices = {{ChaosCastleCommandText, SystemMessage}}};
    auto name = ScanTheName(text);
    if (!name.HasValue())
        return Core::Unexpected(std::move(name).Error());
    if (!context.requestShopItems)
        return Core::Unexpected(std::string("0x574c0c: /purchase sin el pedido"));
    return context.requestShopItems(name.Value());
}

// 0x56c54d..0x56c690: /trade. Fuera de Chaos Castle, el nivel del registro propio contra 0x6c8dc8: sin alcanzarlo, el
// aviso 478 y 1. Con él, el pedido al elegido (0x6c8e30) o al que está enfrente (0x56ce1e), con el C1 05 36.
Core::Expected<ChatCommandOutcome, std::string> TradeCommand(const ChatCommandContext& context)
{
    if (InChaosCastle(context))
        return ChatCommandOutcome{.handled = false, .notices = {{ChaosCastleCommandText, SystemMessage}}};
    if (!context.ownLevel)
        return Core::Unexpected(std::string("0x56c5d0: /trade sin el registro propio"));
    if (context.ownLevel() < context.tradeLevel)
        return ChatCommandOutcome{.handled = true, .notices = {{TradeLevelText, SystemMessage}}};
    if (!context.requestATrade)
        return Core::Unexpected(std::string("0x56c68b: /trade sin el pedido"));
    return context.requestATrade();
}

// 0x576894..0x576922 y 0x40ebd0: "/" y el nombre de un ítem abre su ayuda; si no, 0x40ebd0 compara con "/" y el
// nombre de cada conjunto usado (strncmp en los dos sentidos: iguales), y antes de cada comparación pone el cartel
// (+0x9305) en 0. Con uno igual, 0x77f3b08 en 0 y el cartel en su índice + 1. Devuelve 0 (0x576912).
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
            // 0x576923..0x576952: sin el cartel del conjunto (0x5ae360), la ayuda del ítem (el modo 3), el 0x19 y 1.
            if (context.setLabel != nullptr)
                *context.setLabel = 0;
            context.overlayMode = ItemHelpMode;
            context.helpItem = static_cast<std::int32_t>(index);
            return ChatCommandOutcome{.handled = true, .notices = {}, .sound = true};
        }
    }
    // 0x576900..0x576912: 0x40ebd0 y 0, que manda el texto como chat.
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

// 0x56efdc..0x56f104: "/1" a "/9" y "/0" guardan lo que sigue en su macro, salvo un comando que una macro no puede
// guardar (0x56c1f0): ese texto sale como chat, sin la cola.
ChatCommandOutcome SetTheMacro(const ChatCommandContext& context, std::string_view text, std::size_t macro)
{
    if (IsAForbiddenMacro(context.texts, text))
        return {};
    const std::string_view kept = text.size() > MacroCommandPrefix ? text.substr(MacroCommandPrefix) : "";
    context.macros[macro] = std::string(kept.substr(0, ChatBufferSize - 1));
    return ChatCommandOutcome{.handled = true, .notices = {}, .sound = true};
}

// El número de la macro de "/N", o MacroCount: solo miran los dos primeros bytes.
std::size_t MacroOf(std::string_view text)
{
    if (text.size() < 2 || text[0] != '/')
        return MacroCount;
    for (std::size_t macro = 0; macro < MacroCount; ++macro)
    {
        const char digit = static_cast<char>('0' + (macro + 1) % MacroCount);
        if (text[1] == digit)
            return macro;
    }
    return MacroCount;
}
} // namespace

Core::Expected<ChatCommandOutcome, std::string> RunTheChatCommand(const ChatCommandContext& context,
                                                                  std::string_view text, bool fromMacro)
{
    const Data::Text::TextTable& texts = context.texts;
    if (fromMacro || context.logoutRequested)
        return TheNamesTail(context, text, {});
    if (!context.warehouseOpen)
    {
        const std::string_view word = text.substr(0, std::min({text.find(' '), text.size(), FirstWordLimit}));
        if (word == TextAt(texts, ExchangeCommandText) || word == TextAt(texts, TradeCommandText) ||
            EqualsIgnoringCase(word, "/trade"))
            return TradeCommand(context);
    }
    // 0x56d624..0x56d66a: /move abre la ventana y sigue.
    if (text == TextAt(texts, MoveCommandText) || EqualsIgnoringCase(text, "/move"))
        context.overlayMode = MoveOverlayMode;
    if (text == TextAt(texts, ShopCommandText) || EqualsIgnoringCase(text, "/personalshop"))
        return PersonalShopCommand(context);
    if (ContainsLikeTheOriginal(text, TextAt(texts, BuyCommandText)) || ContainsLikeTheOriginal(text, "/purchase"))
        return PurchaseCommand(context, text);
    // 0x56d6ee..0x56d796: "/ver puestos" (strcmp con el texto 1136) prende los carteles (0x4516a0 sobre 0x451d10) con
    // el aviso 1138 del tipo 1, y su par (el 1137) los apaga (0x4516b0) con el 1139 del tipo 2. Las dos comparaciones
    // corren siempre y la cadena sigue.
    ChatCommandOutcome outcome;
    if (text == TextAt(texts, ShopViewOnCommandText))
    {
        if (context.signsShown == nullptr)
            return Core::Unexpected(std::string("0x56d722: \"/ver puestos\" sin el mapa de los carteles"));
        *context.signsShown = 1;
        outcome.notices.push_back(ChatCommandNotice{.text = ShopViewOnNoticeText, .type = SystemMessage});
    }
    if (text == TextAt(texts, ShopViewOffCommandText))
    {
        if (context.signsShown == nullptr)
            return Core::Unexpected(std::string("0x56d776: apagar los carteles sin su mapa"));
        *context.signsShown = 0;
        outcome.notices.push_back(ChatCommandNotice{.text = ShopViewOffNoticeText, .type = ErrorMessage});
    }
    // 0x56d7e0..0x56d82b: /duelstart. Con el duelo en curso, el aviso 915; sin él, el AA al elegido o al que está
    // enfrente. Con o sin él, sigue la cadena (0x56ea05 y 0x56e07c).
    if (text == TextAt(texts, DuelStartCommandText) || EqualsIgnoringCase(text, "/duelstart"))
    {
        if (InChaosCastle(context))
        {
            outcome.notices.push_back(ChatCommandNotice{ChaosCastleCommandText, SystemMessage});
            return outcome;
        }
        if (context.duelActive)
            outcome.notices.push_back(ChatCommandNotice{.text = DuelBusyText, .type = ErrorMessage});
        else if (!context.requestADuel)
            return Core::Unexpected(std::string("0x56d823: /duelstart sin el pedido"));
        else if (auto asked = context.requestADuel(); !asked.HasValue())
            return Core::Unexpected(std::move(asked).Error());
    }
    // 0x56ea29..0x56ee40: /duelend. Con el duelo en curso, el C1 03 AB en C3; con o sin él, sigue la cadena.
    if (text == TextAt(texts, DuelEndCommandText) || EqualsIgnoringCase(text, "/duelend"))
    {
        if (InChaosCastle(context))
        {
            outcome.notices.push_back(ChatCommandNotice{ChaosCastleCommandText, SystemMessage});
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
    if (text == TextAt(texts, GuildCommandText) || EqualsIgnoringCase(text, "/guild"))
        return SocialCommand(context, "0x573f49", "el gremio", "la CL9 hito 8");
    if (text == TextAt(texts, AllianceCommandText) || EqualsIgnoringCase(text, "/union") ||
        text == TextAt(texts, RivalCommandText) || EqualsIgnoringCase(text, "/rival") ||
        text == TextAt(texts, RivalOffCommandText) || EqualsIgnoringCase(text, "/rivaloff"))
        return SocialCommand(context, "0x570d48", "la alianza y la rivalidad de los gremios", "la CL9 hito 8");
    if (text == TextAt(texts, PartyCommandText) || EqualsIgnoringCase(text, "/party") ||
        EqualsIgnoringCase(text, "/pt"))
    {
        // 0x56f109..0x56f13e: en Chaos Castle, el aviso 1150 y 0; si no, el pedido de 0x56f13f.
        if (InChaosCastle(context))
            return ChatCommandOutcome{.handled = false, .notices = {{ChaosCastleCommandText, SystemMessage}}};
        if (!context.requestAParty)
            return Core::Unexpected(std::string("0x56f13f: /party sin el pedido"));
        return context.requestAParty();
    }
    if (const std::size_t macro = MacroOf(text); macro < MacroCount)
        return SetTheMacro(context, text, macro);
    // 0x56f02e..0x56f0ab: "/filter" (el texto 753, si no está vacío, o el literal) y la cola.
    const std::string_view filter = TextAt(texts, FilterCommandText);
    if ((!filter.empty() && text.starts_with(filter)) || text.starts_with("/filter"))
    {
        if (const auto notice = SetTheChatFilter(context.log, text))
            outcome.notices.push_back(ChatCommandNotice{*notice, SystemMessage});
    }
    return TheNamesTail(context, text, std::move(outcome));
}

bool IsAForbiddenMacro(const Data::Text::TextTable& texts, std::string_view text)
{
    const std::string_view macro = text.size() > MacroCommandPrefix ? text.substr(MacroCommandPrefix) : "";
    return macro == TextAt(texts, ExchangeCommandText) || macro == TextAt(texts, TradeCommandText) ||
           EqualsIgnoringCase(macro, "/trade") || macro == TextAt(texts, PartyCommandText) ||
           EqualsIgnoringCase(macro, "/party") || EqualsIgnoringCase(macro, "/pt") ||
           macro == TextAt(texts, GuildCommandText) || EqualsIgnoringCase(macro, "/guild") ||
           macro == TextAt(texts, GuildWarCommandText) || EqualsIgnoringCase(macro, "/GuildWar") ||
           macro == TextAt(texts, SoccerCommandText) || EqualsIgnoringCase(macro, "/BattleSoccer");
}
} // namespace Client::UI
