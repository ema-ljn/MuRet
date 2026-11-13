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
#include <functional>
#include <optional>
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
// 0x56c666: el nivel no alcanza para comerciar (0x76e0204).
inline constexpr std::size_t TradeLevelText = 478;
// 0x56e9f3: ya hay un duelo (0x7700220).
inline constexpr std::size_t DuelBusyText = 915;
// 0x576718: /personalshop con el entrenador abierto (0x770f388), con el tipo 1.
inline constexpr std::size_t ShopTrainerText = 1121;
// 0x576860: /personalshop sin el nivel (0x770f5e0), un formato con el nivel, con el tipo 1.
inline constexpr std::size_t ShopLevelText = 1123;
// 0x576810: el nivel para abrir la tienda propia.
inline constexpr std::uint16_t ShopLevel = 6;

// Un aviso de 0x555190 con el nombre vacío: el texto `text` de Text.bmd y su tipo.
// 0x6c8dc8: su valor de la .data.
inline constexpr std::uint8_t DefaultTradeLevel = 6;

struct ChatCommandNotice
{
    std::size_t text = 0;
    std::int32_t type = 0;
    // El texto ya armado por sprintf con el texto `text` de formato (los pedidos de 0x56cde9 y 0x56fd75); vacío, el
    // texto tal cual.
    std::optional<std::string> formatted{};

    friend bool operator==(const ChatCommandNotice&, const ChatCommandNotice&) = default;
};

struct ChatCommandOutcome
{
    // Lo que devuelve: con true el WndProc no manda nada.
    bool handled = false;
    std::vector<ChatCommandNotice> notices;
    // 0x57693f: el 0x19 (una macro guardada o la ayuda de un ítem).
    bool sound = false;
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
    // 0x77f3b08: /move lo pone en 4, la ayuda de un ítem en 3 y el nombre de un conjunto en 0.
    std::int32_t& overlayMode;
    // 0x77f3b0c: el ítem de la ayuda.
    std::int32_t& helpItem;
    // [0x6eed3c] + 0x9305, el cartel del set (0x40ebd0, y 0x5ae360 lo apaga); sin él, no lo escribe.
    std::uint8_t* setLabel = nullptr;
    // 0x56c5d0..0x56c652 (/trade): el nivel del registro propio (+0xe), abriéndolo y cerrándolo (sus rand()). Sin él,
    // /trade corta ahí.
    std::function<std::uint16_t()> ownLevel{};
    // 0x6c8dc8 (.data 6): el nivel mínimo para comerciar.
    std::uint8_t tradeLevel = DefaultTradeLevel;
    // 0x78abad8: el duelo en curso.
    bool duelActive = false;
    // 0x56ea6c..0x56ee2c (/duelend con el duelo en curso): el C1 03 AB en C3, con sus dos rand(). Sin él, ese caso
    // corta.
    std::function<Core::Expected<void, std::string>()> endTheDuel{};
    // 0x56c68b..0x56d623 (/trade con el nivel) y 0x56f13f..0x570d47 (/party fuera de Chaos Castle): a quién pedirle
    // (el elegido 0x6c8e30 o el que está enfrente), su paquete y su aviso (Scenes::RequestATrade y RequestAParty); lo
    // que devuelven es la salida del comando. Sin ellos, esos casos cortan.
    std::function<Core::Expected<ChatCommandOutcome, std::string>()> requestATrade{};
    std::function<Core::Expected<ChatCommandOutcome, std::string>()> requestAParty{};
    // 0x56d823..0x56e9ec (/duelstart sin duelo): el AA al elegido o al de enfrente, y la cadena sigue
    // (Scenes::RequestADuel). Sin él, ese caso corta.
    std::function<Core::Expected<void, std::string>()> requestADuel{};
    // IsOpen(0x8000), el entrenador (sin rand()): /personalshop no abre la tienda.
    bool trainerOpen = false;
    // 0x576815..0x576822 (/personalshop con el nivel): 0x6599a0(0x800, 0), con lo que abrir la tienda hace
    // (Scenes::OpenThePersonalShop). Sin él, ese caso corta.
    std::function<Core::Expected<void, std::string>()> openThePersonalShop{};
    // 0x574c0c..0x57694d (/purchase fuera de Chaos Castle): los paneles que lo traban, el elegido, el nombre o el que
    // está enfrente y el C1 10 3F 05 (Scenes::RequestTheShopItems). Recibe lo que strlen y strcmp leen en el segundo
    // buffer de sscanf (EBP − 0xc0): la segunda palabra, o sin valor si sscanf no la llenó; lo que devuelve es la
    // salida del comando. Sin él, ese caso corta.
    std::function<Core::Expected<ChatCommandOutcome, std::string>(std::optional<std::string_view>)> requestShopItems{};
};

// 0x56c440(texto, desde una macro).
[[nodiscard]] Core::Expected<ChatCommandOutcome, std::string> RunTheChatCommand(const ChatCommandContext& context,
                                                                                std::string_view text, bool fromMacro);

// 0x56c1f0: lo que sigue a "/N " es uno de los comandos que una macro no puede guardar.
[[nodiscard]] bool IsAForbiddenMacro(const Data::Text::TextTable& texts, std::string_view text);
} // namespace Client::UI
