#pragma once

#include "Core/Expected.h"
#include "Data/Tables/ItemSetTables.h"
#include "Data/Tables/ItemTable.h"
#include "Data/Text/TextTable.h"
#include "UI/ChatInput.h"
#include "UI/ChatLog.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// Los comandos del chat (CL8 hito 3; 06 § El chat): 0x56c440(texto, desde una macro), 42 KB que el WndProc llama
// antes de mandar lo escrito y las macros con 1. Se porta como despacho (decisión del plan de la CL8): las
// compuertas, el orden de las comparaciones, el aviso de Chaos Castle con que empieza cada comando, /move, /filter,
// /1../0 y la cola de los nombres; lo de cada comando social corta con la dirección de su bloque (CL9.4), donde
// todavía no se sacó ningún rand(). Las comparaciones no sacan rand(): IsOpen(0x40) no lee la tabla de protección.
// *Estático*.
namespace Client::UI
{
// Los textos de Text.bmd que compara y avisa.
inline constexpr std::size_t ExchangeCommandText = 258;  // "/exchange"
inline constexpr std::size_t TradeCommandText = 259;     // "/trade"
inline constexpr std::size_t MoveCommandText = 260;      // "/move"
inline constexpr std::size_t GuildCommandText = 254;     // "/guild"
inline constexpr std::size_t PartyCommandText = 256;     // "/party"
inline constexpr std::size_t GuildWarCommandText = 248;  // el de la guerra de gremios
inline constexpr std::size_t SoccerCommandText = 249;    // el de Battle Soccer
inline constexpr std::size_t FilterCommandText = 753;    // "/filter"
inline constexpr std::size_t DuelStartCommandText = 908; // "/Duelchallenge"
inline constexpr std::size_t DuelEndCommandText = 909;   // "/DuelCancel"
inline constexpr std::size_t ShopCommandText = 1117;     // "/Personal store"
inline constexpr std::size_t BuyCommandText = 1118;      // "/Buy"
inline constexpr std::size_t ShopViewOnCommandText = 1136;
inline constexpr std::size_t ShopViewOffCommandText = 1137;
inline constexpr std::size_t AllianceCommandText = 1354;
inline constexpr std::size_t RivalCommandText = 1356;
inline constexpr std::size_t RivalOffCommandText = 1357;
// "No se puede en Chaos Castle", con el tipo 1.
inline constexpr std::size_t ChaosCastleCommandText = 1150;
// 0x576eb0: los textos de los emotes, del 270 al 348 y el 783.
inline constexpr std::size_t FirstEmoteText = 270;
inline constexpr std::size_t LastEmoteText = 348;
inline constexpr std::size_t LastEmoteExtraText = 783;

// Un aviso de 0x555190 con el nombre vacío: el texto `text` de Text.bmd y su tipo.
struct ChatCommandNotice
{
    std::size_t text = 0;
    std::int32_t type = 0;

    friend bool operator==(const ChatCommandNotice&, const ChatCommandNotice&) = default;
};

// Lo que el despacho lee y escribe.
struct ChatCommandContext
{
    const Data::Text::TextTable& texts;
    // *[0x7758550] y las 64 de [0x6eed3c] + 0x8004: la cola de los nombres. Sin una, no tiene nombres que comparar.
    const Data::Tables::ItemTable* items = nullptr;
    const Data::Tables::ItemSetOptionTable* setOptions = nullptr;
    // 0x6c9dd0.
    std::int32_t map = -1;
    // 0x576793c: la salida pedida.
    bool logoutRequested = false;
    // IsOpen(0x40), el almacén, que se saltea el comercio.
    bool warehouseOpen = false;
    // [0x576350c]: 0x45dc40, el filtro.
    ChatLogState& log;
    // 0x77f1db0.
    std::array<std::string, MacroCount>& macros;
    // 0x77f3b08: /move lo pone en 4 y el nombre de un conjunto en 0.
    std::int32_t& overlayMode;
    // [0x6eed3c] + 0x9305, el cartel del set (0x40ebd0); sin él, no lo escribe.
    std::uint8_t* setLabel = nullptr;
};

struct ChatCommandOutcome
{
    // Lo que devuelve: con true el WndProc no manda nada.
    bool handled = false;
    std::vector<ChatCommandNotice> notices;
    // 0x57693f: el 0x19.
    bool sound = false;
};

// 0x56c440(texto, desde una macro).
[[nodiscard]] Core::Expected<ChatCommandOutcome, std::string> RunTheChatCommand(const ChatCommandContext& context,
                                                                                std::string_view text, bool fromMacro);

// 0x56c1f0: lo que sigue a "/N " es uno de los comandos que una macro no puede guardar.
[[nodiscard]] bool IsAForbiddenMacro(const Data::Text::TextTable& texts, std::string_view text);

// 0x576eb0: con alguno de los textos de los emotes en `text` (0x576960), el emote (0x576a60 y el C1 18) es de la
// CL9.1 y el error lo dice. Sin ninguno, nada.
[[nodiscard]] Core::Expected<void, std::string> CheckTheEmotes(const Data::Text::TextTable& texts,
                                                               std::string_view text);
} // namespace Client::UI
