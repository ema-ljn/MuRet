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

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
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

// El comienzo de cada comando social: en Chaos Castle, el aviso 1150 y devuelve 0 (el texto sale como chat); si no,
// lo que sigue es de su ola.
Core::Expected<ChatCommandOutcome, std::string> SocialCommand(const ChatCommandContext& context,
                                                              std::string_view address, std::string_view what)
{
    if (context.map >= FirstChaosCastleMap && context.map <= LastChaosCastleMap)
        return ChatCommandOutcome{.handled = false, .notices = {{ChaosCastleCommandText, SystemMessage}}};
    return Core::Unexpected(std::format("{}: {} es de la CL9.4", address, what));
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
            return SocialCommand(context, "0x56c54d", "el comercio");
    }
    // 0x56d624..0x56d66a: /move abre la ventana y sigue.
    if (text == TextAt(texts, MoveCommandText) || EqualsIgnoringCase(text, "/move"))
        context.overlayMode = MoveOverlayMode;
    if (text == TextAt(texts, ShopCommandText) || EqualsIgnoringCase(text, "/personalshop"))
        return SocialCommand(context, "0x5766ce", "la tienda personal");
    if (ContainsLikeTheOriginal(text, TextAt(texts, BuyCommandText)) || ContainsLikeTheOriginal(text, "/purchase"))
        return SocialCommand(context, "0x574bd6", "comprar en una tienda personal");
    if (text == TextAt(texts, ShopViewOnCommandText) || text == TextAt(texts, ShopViewOffCommandText))
        return Core::Unexpected(
            std::string("0x56d722: ver las tiendas personales (0x4516a0 y 0x4516b0) es de la CL9.4"));
    if (text == TextAt(texts, DuelStartCommandText) || EqualsIgnoringCase(text, "/duelstart"))
        return SocialCommand(context, "0x56d7e0", "el duelo");
    if (text == TextAt(texts, DuelEndCommandText) || EqualsIgnoringCase(text, "/duelend"))
        return SocialCommand(context, "0x56ea29", "terminar el duelo");
    if (text == TextAt(texts, GuildCommandText) || EqualsIgnoringCase(text, "/guild"))
        return SocialCommand(context, "0x573f49", "el gremio");
    if (text == TextAt(texts, AllianceCommandText) || EqualsIgnoringCase(text, "/union") ||
        text == TextAt(texts, RivalCommandText) || EqualsIgnoringCase(text, "/rival") ||
        text == TextAt(texts, RivalOffCommandText) || EqualsIgnoringCase(text, "/rivaloff"))
        return SocialCommand(context, "0x570d48", "la alianza y la rivalidad de los gremios");
    if (text == TextAt(texts, PartyCommandText) || EqualsIgnoringCase(text, "/party") ||
        EqualsIgnoringCase(text, "/pt"))
        return SocialCommand(context, "0x56f109", "la party");
    if (const std::size_t macro = MacroOf(text); macro < MacroCount)
        return SetTheMacro(context, text, macro);
    // 0x56f02e..0x56f0ab: "/filter" (el texto 753, si no está vacío, o el literal) y la cola.
    ChatCommandOutcome outcome;
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

Core::Expected<void, std::string> CheckTheEmotes(const Data::Text::TextTable& texts, std::string_view text)
{
    const auto found = [&](std::size_t index) { return ContainsLikeTheOriginal(text, TextAt(texts, index)); };
    for (std::size_t index = FirstEmoteText; index <= LastEmoteText; ++index)
    {
        if (found(index))
            return Core::Unexpected(std::format("0x576a60: el emote del texto {} (y su C1 18) es de la CL9.1", index));
    }
    if (found(LastEmoteExtraText))
        return Core::Unexpected(
            std::format("0x576a60: el emote del texto {} (y su C1 18) es de la CL9.1", LastEmoteExtraText));
    return {};
}
} // namespace Client::UI
