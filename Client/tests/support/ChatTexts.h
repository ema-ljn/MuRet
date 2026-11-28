#pragma once

#include "Chat/ChatCommands.h"
#include "Chat/Emotes.h"

#include <cstddef>
#include <map>
#include <string>

// A made-up Text.bmd for the chat input (CL8 milestone 3): the commands 0x56c440 compares, the emotes of 0x576eb0 and
// the input's notices, all with texts that do not appear in what the tests say. 0x576960 finds an empty text in any
// message, as in the original.
namespace Client::Tests
{
inline constexpr std::size_t ChatFilteredText = 570;
inline constexpr std::size_t ChatWhisperOffCommandText = 264;
inline constexpr std::size_t ChatWhisperOnCommandText = 265;
inline constexpr std::size_t ChatWhisperOffNoticeText = 267;
inline constexpr std::size_t ChatWhisperOnNoticeText = 268;
inline constexpr std::size_t ChatWhisperDeniedText = 479;
// The notices of the requests to another (0x56c440, CL9 milestone 7): 475 and 476 are sprintf formats with the name.
inline constexpr std::size_t ChatTradeRequestedText = 475;
inline constexpr std::size_t ChatPartyRequestedText = 476;
inline constexpr std::size_t ChatPersonalShopText = 493;
inline constexpr std::size_t ChatNotTheLeaderText = 257;
// The personal shop title that 0x5ebbd0 does not allow (0x4b55fd and 0x626241).
inline constexpr std::size_t ChatBadShopTitleText = 1130;

inline std::map<std::size_t, std::string> ChatTexts()
{
    std::map<std::size_t, std::string> texts{
        {Chat::ExchangeCommandText, "/cambiar"},
        {Chat::TradeCommandText, "/comerciar"},
        {Chat::MoveCommandText, "/mover"},
        {Chat::GuildCommandText, "/clan"},
        {Chat::PartyCommandText, "/grupo"},
        {Chat::GuildWarCommandText, "/guerra"},
        {Chat::SoccerCommandText, "/pelota"},
        {Chat::FilterCommandText, "/filtro"},
        {Chat::DuelStartCommandText, "/retar"},
        {Chat::DuelEndCommandText, "/noretar"},
        {Chat::ShopCommandText, "/puesto"},
        {Chat::BuyCommandText, "/adquirir"},
        {Chat::ShopViewOnCommandText, "/ver puestos"},
        {Chat::ShopViewOffCommandText, "/no ver puestos"},
        {Chat::ShopViewOnNoticeText, "aviso carteles"},
        {Chat::ShopViewOffNoticeText, "aviso sin carteles"},
        {Chat::AllianceCommandText, "/aliados"},
        {Chat::RivalCommandText, "/rivales"},
        {Chat::RivalOffCommandText, "/sin rivales"},
        {Chat::ChaosCastleCommandText, "aviso castillo"},
        {Chat::ShopTrainerText, "aviso entrenador"},
        {Chat::ShopLevelText, "desde el nivel %d"},
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
        {ChatBadShopTitleText, "aviso titulo"},
        {755, "aviso filtro"},
        {756, "aviso sin filtro"},
    };
    for (std::size_t index = Chat::FirstEmoteText; index <= Chat::LastEmoteText; ++index)
        texts.emplace(index, "#e" + std::to_string(index));
    texts.emplace(Chat::LastEmoteExtraText, "#e" + std::to_string(Chat::LastEmoteExtraText));
    return texts;
}
} // namespace Client::Tests
