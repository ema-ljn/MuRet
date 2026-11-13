#pragma once

#include "UI/ChatCommands.h"
#include "UI/Emotes.h"

#include <cstddef>
#include <map>
#include <string>

// Un Text.bmd inventado para la entrada del chat (CL8 hito 3): los comandos que compara 0x56c440, los emotes de
// 0x576eb0 y los avisos de la entrada, todos con textos que no aparecen en lo que dicen los tests. Un texto vacío lo
// encuentra 0x576960 en cualquier mensaje, como en el original.
namespace Client::Tests
{
inline constexpr std::size_t ChatFilteredText = 570;
inline constexpr std::size_t ChatWhisperOffCommandText = 264;
inline constexpr std::size_t ChatWhisperOnCommandText = 265;
inline constexpr std::size_t ChatWhisperOffNoticeText = 267;
inline constexpr std::size_t ChatWhisperOnNoticeText = 268;
inline constexpr std::size_t ChatWhisperDeniedText = 479;
// Los avisos de los pedidos a otro (0x56c440, CL9 hito 7): el 475 y el 476 son formatos de sprintf con el nombre.
inline constexpr std::size_t ChatTradeRequestedText = 475;
inline constexpr std::size_t ChatPartyRequestedText = 476;
inline constexpr std::size_t ChatPersonalShopText = 493;
inline constexpr std::size_t ChatNotTheLeaderText = 257;

inline std::map<std::size_t, std::string> ChatTexts()
{
    std::map<std::size_t, std::string> texts{
        {UI::ExchangeCommandText, "/cambiar"},
        {UI::TradeCommandText, "/comerciar"},
        {UI::MoveCommandText, "/mover"},
        {UI::GuildCommandText, "/clan"},
        {UI::PartyCommandText, "/grupo"},
        {UI::GuildWarCommandText, "/guerra"},
        {UI::SoccerCommandText, "/pelota"},
        {UI::FilterCommandText, "/filtro"},
        {UI::DuelStartCommandText, "/retar"},
        {UI::DuelEndCommandText, "/noretar"},
        {UI::ShopCommandText, "/puesto"},
        {UI::BuyCommandText, "/adquirir"},
        {UI::ShopViewOnCommandText, "/ver puestos"},
        {UI::ShopViewOffCommandText, "/no ver puestos"},
        {UI::AllianceCommandText, "/aliados"},
        {UI::RivalCommandText, "/rivales"},
        {UI::RivalOffCommandText, "/sin rivales"},
        {UI::ChaosCastleCommandText, "aviso castillo"},
        {ChatFilteredText, "..."},
        {ChatWhisperOffCommandText, "/callar"},
        {ChatWhisperOnCommandText, "/escuchar"},
        {ChatWhisperOffNoticeText, "aviso callar"},
        {ChatWhisperOnNoticeText, "aviso escuchar"},
        {ChatWhisperDeniedText, "aviso nivel"},
        {ChatTradeRequestedText, "pediste comerciar con %s"},
        {ChatPartyRequestedText, "invitaste a %s"},
        {ChatPersonalShopText, "aviso puesto"},
        {ChatNotTheLeaderText, "aviso lider"},
        {755, "aviso filtro"},
        {756, "aviso sin filtro"},
    };
    for (std::size_t index = UI::FirstEmoteText; index <= UI::LastEmoteText; ++index)
        texts.emplace(index, "#e" + std::to_string(index));
    texts.emplace(UI::LastEmoteExtraText, "#e" + std::to_string(UI::LastEmoteExtraText));
    return texts;
}
} // namespace Client::Tests
