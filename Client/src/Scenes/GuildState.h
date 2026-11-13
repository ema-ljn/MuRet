#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// El gremio, la alianza y la guerra en el juego (CL9 hito 8; 06 § El gremio): los globales de la .bss y de la .data
// que escriben los paquetes (0x4d74b0..0x4d9e50) y lo que guardan las dos ventanas del gremio ([0x7d536f0] y
// [0x7d536f4]) de esos paquetes. Las ventanas mismas (su paso y su dibujo) no están: cortan con su dirección.
namespace Client::Scenes
{
// 0x447b50: la tabla de los gremios ([0x723c58], entradas en 0x787aa48) tiene lugar para 2.000.
inline constexpr std::size_t GuildTableSize = 2000;
// 0x447b90: la marca de 32 bytes, abierta en 64 nibbles (+0x16..+0x55).
inline constexpr std::size_t GuildMarkNibbles = 64;
// 0x787a5e8..0x787aa47: 80 lugares de 0xe bytes, los miembros del gremio del héroe (0x4d7b60).
inline constexpr std::size_t GuildMemberSlots = 80;
// 0x4d9940 y 0x6570b0: el índice de +0x1f2 sin gremio (−1) y el que 0x6570b0 vuelve a −1 (*inferido*: el gremio que
// se está creando).
inline constexpr std::int16_t NoGuildIndex = -1;
inline constexpr std::int16_t CreatingGuildIndex = 1999;

// Una entrada de 0x58 bytes de 0x787aa48.
struct GuildTableEntry
{
    // +0: la clave del gremio (0x447b00 la busca, 0x447b50 la pone).
    std::int32_t key = 0;
    // +4..+0xc: el nombre de la alianza (8 bytes y su cero, 0x447b90 desde pkt[9] del 66). Lo dibuja el globo como
    // "<%s>" (0x556dd0) y lo borra el E6 que rompe la alianza (0x4d9b2b).
    std::array<char, 9> unionName{};
    // +0xd..+0x15: el nombre del gremio (0x447b90 desde pkt[0x11] del 66). El globo lo dibuja como "[%s]" y el 62 busca
    // en él al rival (0x4d832c).
    std::array<char, 9> guildName{};
    // +0x16..+0x55: la marca en nibbles.
    std::array<std::uint8_t, GuildMarkNibbles> mark{};
};

// Un lugar de 0xe bytes de 0x787a5e8 (0x4d7b60).
struct GuildMemberSlot
{
    // +0..+9 desde la entrada del 52, +0xa en 0.
    std::array<char, 11> name{};
    std::uint8_t number = 0; // +0xb
    // +0xc: con el bit 7 del 52, sus 7 de abajo; sin él, 0xff.
    std::uint8_t server = 0;
    std::uint8_t status = 0; // +0xd
};

// Una fila de la lista de la alianza (+0x2f8 de [0x7d536f0], la vtable + 0x94 que llama 0x6517e0).
struct GuildUnionRow
{
    std::array<std::uint8_t, GuildMarkNibbles> mark{};
    std::array<char, 9> name{};
    std::uint8_t byte0 = 0;
};

// Lo que los paquetes escriben de la ventana del gremio, [0x7d536f0] (0x64f3e0). Sus listas son objetos de las clases
// de 0x46b800..0x46d630, que MuRet no tiene: acá, lo que se les agregó, en orden (*inferido*: ni el dibujo ni el paso
// están portados para ver si recortan).
struct GuildWindowState
{
    // +0x80: la pestaña (0x64f3e0 la deja en 1) y +0x84, el pedido del E9 ya hecho.
    std::int32_t tab = 1;
    std::int32_t unionRequested = 0;
    // +0x88..+0x90: el nombre de pkt[0xd..0x14] del 52 (0x651650) y su cero.
    std::array<char, 9> name88{};
    // +0x94: la lista de los miembros (0x6517a0 agrega la entrada de 0x787a5e8, 0x6517d0 la vacía).
    std::vector<GuildMemberSlot> members;
    // +0x1c8: los renglones del aviso del gremio (0x651680; 0x651790 la vacía).
    std::vector<std::string> notice;
    // +0x2f8: la lista de la alianza (0x6517e0; 0x651830 la vacía).
    std::vector<GuildUnionRow> unions;
    // +0x428: el diálogo de los paneles que abrió (lo que dio 0x6600d0).
    std::uint32_t dialog = 0;
};

// Lo que guarda la ventana 2 del gremio, [0x7d536f4] (0x654900), de los paquetes.
struct SecondGuildWindowState
{
    // +0x2a0 y +0x2a4: los diálogos de los paneles que abrió (0x654f50 los cancela al cerrarse).
    std::uint32_t dialog2a0 = 0;
    std::uint32_t dialog2a4 = 0;
};

struct GuildState
{
    // 0x787a5e8 y 0x787aa48: los dos arreglos van seguidos, y el original escribe a uno desde el otro (Q-C1281).
    std::array<GuildMemberSlot, GuildMemberSlots> members{};
    std::array<GuildTableEntry, GuildTableSize> table{};
    // [0x723c58] + 4 (0x723c5c): cuántas entradas tiene la tabla.
    std::uint32_t tableCount = 0;
    // 0x78aba38: la cuenta de los miembros del 52; −1 al salir (0x4d7d1a, 0x4e4327).
    std::int32_t memberCount = 0;
    // 0x78aba3c: el puntaje del 52, sin negativos.
    std::int32_t score = 0;
    // 0x78aba40: la clave de quien pidió entrar (el 50).
    std::int32_t requester = 0;

    // La guerra. 0x576796c: en guerra (el 62 y el 64 lo prenden, el 63 y el 53 lo apagan).
    std::uint8_t warOn = 0;
    // 0x576796d: el 61 y el 62 con el tipo 1 lo prenden; el 63 lo apaga.
    std::uint8_t warType = 0;
    // 0x576796e: el equipo del 62 (pkt[0xc]).
    std::uint8_t warTeam = 0;
    // 0x5767804..0x576780c: el nombre del rival (8 bytes y su cero).
    std::array<char, 9> rival{};
    // 0x5767816: el 63 lo pone en 0 (0x4d8956); *estático*: nadie de este hito lo lee.
    std::uint8_t byte5767816 = 0;
    // 0x57678b4 y 0x57678b8: los puntajes del 64.
    std::int32_t ourScore = 0;
    std::int32_t theirScore = 0;
    // 0x6c82f0 (.data −1): el índice del rival en la tabla.
    std::int32_t rivalIndex = -1;

    // El pedido de alianza del E5 (0x6565e0): 0x7d536e0 la relación, 0x7d536ea el pedido, 0x7d536e8 y 0x7d536e9 la
    // clave.
    std::uint8_t relation = 0;
    std::uint8_t relationRequest = 0;
    std::uint8_t relationKeyHigh = 0;
    std::uint8_t relationKeyLow = 0;

    // 0x7d536e4 y 0x7d536ec: qué arma la ventana 2 y en qué paso va (crear el gremio, 0x6552b0 y 0x655d70, no está
    // portado); 0x6570b0 los vuelve a 0.
    std::int32_t createKind = 0;
    std::int32_t createStep = 0;

    GuildWindowState window{};
    SecondGuildWindowState secondWindow{};
};

// 0x447b00(clave): la primera entrada con la clave, mirando de 0 hasta la cuenta inclusive (una más: con la cuenta en
// 0, la entrada 0 de la .bss tiene la clave 0). −1 si no hay. Más allá de los 2.000 corta: el original leería fuera de
// la tabla.
[[nodiscard]] inline std::optional<std::int32_t> FindGuild(const GuildState& guild, std::int32_t key)
{
    for (std::size_t index = 0; index <= guild.tableCount; ++index)
    {
        if (index >= GuildTableSize)
            return std::nullopt;
        if (guild.table[index].key == key)
            return static_cast<std::int32_t>(index);
    }
    return -1;
}
} // namespace Client::Scenes
