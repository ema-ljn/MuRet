#pragma once

#include "UI/OldControls.h"
#include "UI/OldWindowBase.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// El gremio, la alianza y la guerra en el juego (CL9 hito 8; 06 § El gremio): los globales de la .bss y de la .data
// que escriben los paquetes (0x4d74b0..0x4d9e50) y lo que guardan las dos ventanas del gremio ([0x7d536f0] y
// [0x7d536f4]) de esos paquetes, y la ventana 2 con sus botones (Scenes/SecondGuildWindow, la fase 3). La ventana del
// gremio, con sus pestañas y sus listas (fases 4 y 5), está en Scenes/GuildWindow y Scenes/GuildWindowTabs.
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
    // +0: la clave del gremio (0x447b00 la busca, 0x447b50 la pone). 0x447ae0 las deja en −1: el GS manda la clave 0
    // para "sin gremio", y con 0 la búsqueda encontraría la entrada 0.
    std::int32_t key = -1;
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

// Un elemento de 0x14 bytes del deque de la lista de los miembros (0x46c930): +0 lo invierte el clic en su renglón
// (0x46d3a0) y +4..+0x11 es el lugar del miembro.
struct GuildMemberRow
{
    std::int32_t toggled = 0;
    GuildMemberSlot slot{};
};

// Un elemento de 0x40 bytes del deque de la lista del aviso (0x46bca0, desde 0x575d230): +0 lo invierte el clic en su
// renglón (0x46c3d0) y +4 es el renglón del aviso (strcpy, sin tope: los renglones de 0x459c70 son de 0x6e de ancho).
struct GuildNoticeRow
{
    std::int32_t toggled = 0;
    std::string text;
};

// Un elemento de 0x54 bytes del deque de la lista de la alianza (0x46d900, desde 0x5762e50): +0 lo invierte el clic
// (0x46e1e0), +4..+0x43 la marca en nibbles, +0x44..+0x4b el nombre (strncpy de 8) con +0x4c en 0 y +0x50 el número
// (pkt[0] de la fila del E9, en un DWORD).
struct GuildUnionRow
{
    std::int32_t toggled = 0;
    std::array<std::uint8_t, GuildMarkNibbles> mark{};
    std::array<char, 9> name{};
    std::uint8_t byte0 = 0;
};

// La ventana del gremio, [0x7d536f0] (0x64f3e0), con sus tres listas sobre UI::OldList (los elementos, en su dueño).
// El id de la ventana del gremio: el original lo saca del contador 0x5762eb0 como los de la ventana 2; MuRet le da −10,
// después de los de la ventana 2 (P-C280).
inline constexpr std::int32_t GuildWindowId = -10;

// 0x64f3e0: la ventana base (0x45a210) en (0x1c2, 0) de 0xbe × 0x17c (0x45a9d0 y 0x45a9f0), que nadie mueve.
[[nodiscard]] inline UI::OldWindowBase GuildWindowBase()
{
    UI::OldWindowBase base;
    base.id = GuildWindowId;
    base.x = 0x1c2;
    base.y = 0;
    base.width = 0xbe;
    base.height = 0x17c;
    return base;
}

// La lista de los miembros, +0x94 de [0x7d536f0] (0x46c470, vtable 0x6b971c): en (0x1d2, 0x159) de 0x9d × 0xeb,
// colgada de su borde de abajo (+0x78 en 2), 18 renglones (+0x104), hasta 0x96 elementos, con las flechas (+0xbc en 1)
// y la barra de las ventanas (+0x12c en 1). Sin padre (+0x44 en 0). Su id es del contador 0x5762eb0: MuRet le da −11
// (P-C280).
inline constexpr std::int32_t GuildMemberListId = -11;

[[nodiscard]] inline UI::OldList GuildMemberList()
{
    UI::OldList list;
    list.base.id = GuildMemberListId;
    list.base.x = 0x1d2;
    list.base.y = 0x159;
    list.base.width = 0x9d;
    list.base.height = 0xeb;
    list.base.rectMode = 2;
    list.rows = 0x12;
    return list;
}

// 0x64f3e0: los tres botones del maestro en la pestaña de los miembros (0x45ad80), de 0x32 × 0x12 en y + 0x15e, con el
// comando (0x45b150) y el texto de Text.bmd (0x45b170): +0x4b4 en x + 0x12 (2, el 1307, el cargo), +0x53c en x + 0x46
// (3, el 1308, quitarlo) y +0x5c4 en x + 0x7a (4, el 1309, echarlo). Cuelgan de la ventana (+0x44 con su id). Ids:
// −12..−14 (P-C280).
inline constexpr std::size_t GuildAssignButton = 0;
inline constexpr std::size_t GuildDismissButton = 1;
inline constexpr std::size_t GuildKickButton = 2;
inline constexpr std::size_t GuildMemberButtonCount = 3;

[[nodiscard]] inline std::array<UI::OldButton, GuildMemberButtonCount> GuildMemberButtons()
{
    constexpr std::array<std::int32_t, GuildMemberButtonCount> Lefts{0x12, 0x46, 0x7a};
    std::array<UI::OldButton, GuildMemberButtonCount> buttons{};
    for (std::size_t index = 0; index < buttons.size(); ++index)
    {
        UI::OldButton& button = buttons[index];
        button.base.id = GuildMemberListId - 1 - static_cast<std::int32_t>(index);
        button.base.parent = GuildWindowId;
        button.base.x = 0x1c2 + Lefts[index];
        button.base.y = 0x15e;
        button.base.width = 0x32;
        button.base.height = 0x12;
        button.command = 2 + static_cast<std::int32_t>(index);
    }
    return buttons;
}

// La lista del aviso, +0x1c8 (0x46b800, vtable 0x6b95e8): colgada de su borde de abajo, 6 renglones, hasta 0x96
// elementos, con las flechas (+0xbc en 1), la barra de las ventanas (+0x114 en 13) y sin padre. Queda en (0, 0) de
// 100 × 100 (0x45a210) hasta el dibujo de la pestaña 0 (0x64fdf0: 0x88 × 0x50), y la lista de la alianza cuelga de su
// alto (Q-C1293). La lista de la alianza, +0x2f8 (0x46d440, vtable 0x6b97bc), igual, y su tamaño lo pone el dibujo de
// su pestaña (0x651400: 0xa0 × 0x50). Ids del contador 0x5762eb0: MuRet les da −15 y −16 (P-C280).
inline constexpr std::int32_t GuildNoticeListId = -15;
inline constexpr std::int32_t GuildUnionListId = -16;

[[nodiscard]] inline UI::OldList GuildSmallList(std::int32_t id)
{
    UI::OldList list;
    list.base.id = id;
    list.base.rectMode = 2;
    list.rows = 6;
    return list;
}

// 0x64f3e0: los otros tres botones de 0x32 × 0x12 colgados de la ventana (0x45ad80, el comando de 0x45b150 y el texto
// de 0x45b170): +0x42c (1, sin texto, 0x6e57a4) en (x + 0x46, y + alto − 0x12), salir del gremio o disolverlo en la
// pestaña 0; +0x64c (7, sin texto) en (x + 0x46, y + 0x15e) y +0x6d4 (8, el 1422) en (x + 0x46, y + 0xdc), salir de la
// alianza o echar a un gremio de ella en la pestaña 2. Ids −17..−19 (P-C280).
inline constexpr std::size_t GuildLeaveButton = 0;
inline constexpr std::size_t GuildUnionLeaveButton = 1;
inline constexpr std::size_t GuildUnionKickButton = 2;
inline constexpr std::size_t GuildTabButtonCount = 3;

[[nodiscard]] inline std::array<UI::OldButton, GuildTabButtonCount> GuildTabButtons()
{
    constexpr std::array<std::int32_t, GuildTabButtonCount> Commands{1, 7, 8};
    constexpr std::array<std::int32_t, GuildTabButtonCount> Tops{0x17c - 0x12, 0x15e, 0xdc};
    std::array<UI::OldButton, GuildTabButtonCount> buttons{};
    for (std::size_t index = 0; index < buttons.size(); ++index)
    {
        UI::OldButton& button = buttons[index];
        button.base.id = GuildUnionListId - 1 - static_cast<std::int32_t>(index);
        button.base.parent = GuildWindowId;
        button.base.x = 0x1c2 + 0x46;
        button.base.y = Tops[index];
        button.base.width = 0x32;
        button.base.height = 0x12;
        button.command = Commands[index];
    }
    buttons[GuildLeaveButton].label = std::string();
    buttons[GuildUnionLeaveButton].label = std::string();
    return buttons;
}

struct GuildWindowState
{
    UI::OldWindowBase base = GuildWindowBase();
    // +0x80: la pestaña (0x64f3e0 la deja en 1) y +0x84, el pedido del E9 ya hecho.
    std::int32_t tab = 1;
    std::int32_t unionRequested = 0;
    // +0x88..+0x90: el nombre de pkt[0xd..0x14] del 52 (0x651650) y su cero.
    std::array<char, 9> name88{};
    // +0x94: la lista de los miembros (0x6517a0 agrega la entrada de 0x787a5e8 adelante, 0x6517d0 la vacía) y su
    // deque, en el orden del deque.
    UI::OldList memberList = GuildMemberList();
    std::vector<GuildMemberRow> members;
    // +0x130 de la lista: el primero que entró a la lista vacía es el héroe (0x46c992).
    std::int32_t heroFirst = 0;
    // +0x4b4, +0x53c y +0x5c4.
    std::array<UI::OldButton, GuildMemberButtonCount> memberButtons = GuildMemberButtons();
    // +0x1c8: la lista del aviso del gremio (0x651680 le agrega los renglones, 0x651790 la vacía) y su deque.
    UI::OldList noticeList = GuildSmallList(GuildNoticeListId);
    std::vector<GuildNoticeRow> notice;
    // +0x2f8: la lista de la alianza (0x6517e0 agrega, 0x651830 vacía) y su deque.
    UI::OldList unionList = GuildSmallList(GuildUnionListId);
    std::vector<GuildUnionRow> unions;
    // +0x42c, +0x64c y +0x6d4. El texto de +0x6d4 (el 1422) lo copia MuRet la primera vez que lo dibuja.
    std::array<UI::OldButton, GuildTabButtonCount> tabButtons = GuildTabButtons();
    // +0x428: el diálogo de los paneles que abrió (lo que dio 0x6600d0).
    std::uint32_t dialog = 0;
};

// 0x654900: los botones de la ventana 2 (0x45ad80), en el orden de sus offsets, con el comando y el texto de Text.bmd
// que les pone la vtable + 0x2c (0x45b150 y 0x45b170): +0x80 Crear (1, el 1303), +0x108 Cambiar la marca (2, el 1304),
// +0x190 Próximo (5, el 1305) y +0x218 Regreso (4, el 1306).
inline constexpr std::size_t SecondGuildCreateButton = 0;
inline constexpr std::size_t SecondGuildMarkButton = 1;
inline constexpr std::size_t SecondGuildNextButton = 2;
inline constexpr std::size_t SecondGuildBackButton = 3;
inline constexpr std::size_t SecondGuildButtonCount = 4;
// Los ids de la ventana y de sus botones. El original los saca del contador 0x5762eb0 (0x459ef0) cuando WinMain arma el
// registro; MuRet les da −5..−9, después de los del diálogo de los paneles (P-C280), que no chocan con los de las
// ventanas viejas.
inline constexpr std::int32_t SecondGuildWindowId = -5;

// 0x654900: la ventana base (0x45a210) en (0x1c2, 0) de 0xbe × 0x5a, que nadie mueve (*estático*: 0x45a9d0 sobre
// [0x7d536f4] solo lo llama el constructor).
[[nodiscard]] inline UI::OldWindowBase SecondGuildWindowBase()
{
    UI::OldWindowBase base;
    base.id = SecondGuildWindowId;
    base.x = 0x1c2;
    base.y = 0;
    base.width = 0xbe;
    base.height = 0x5a;
    return base;
}

// 0x654926..0x654a7a: los cuatro botones, colgados de la ventana (+0x44 con su id). Los dos primeros, de 100 × 20, los
// ubica el dibujo de la página 0 (0x6563e0); los otros dos, de 50 × 18, quedan en (x + 0x61, y + 0x168) y
// (x + 0x2d, y + 0x168).
[[nodiscard]] inline std::array<UI::OldButton, SecondGuildButtonCount> SecondGuildButtons()
{
    constexpr std::array<std::int32_t, SecondGuildButtonCount> Commands{1, 2, 5, 4};
    constexpr std::array<std::int32_t, SecondGuildButtonCount> Widths{100, 100, 0x32, 0x32};
    constexpr std::array<std::int32_t, SecondGuildButtonCount> Heights{20, 20, 0x12, 0x12};
    constexpr std::array<std::int32_t, SecondGuildButtonCount> Lefts{0, 0, 0x1c2 + 0x61, 0x1c2 + 0x2d};
    constexpr std::array<std::int32_t, SecondGuildButtonCount> Tops{0, 0, 0x168, 0x168};
    std::array<UI::OldButton, SecondGuildButtonCount> buttons{};
    for (std::size_t index = 0; index < buttons.size(); ++index)
    {
        UI::OldButton& button = buttons[index];
        button.base.id = SecondGuildWindowId - 1 - static_cast<std::int32_t>(index);
        button.base.parent = SecondGuildWindowId;
        button.base.x = Lefts[index];
        button.base.y = Tops[index];
        button.base.width = Widths[index];
        button.base.height = Heights[index];
        button.command = Commands[index];
    }
    return buttons;
}

// La ventana 2 del gremio, [0x7d536f4] (0x654900): su base, sus botones y los diálogos que abrió.
struct SecondGuildWindowState
{
    UI::OldWindowBase base = SecondGuildWindowBase();
    // Los textos (0x45b170) los copia MuRet la primera vez que la dibuja, como los del diálogo de los paneles.
    std::array<UI::OldButton, SecondGuildButtonCount> buttons = SecondGuildButtons();
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
    // 0x6c6498 (WORD, .data −1) y 0x6c649a (.data 0xff): el índice del gremio y el puesto del héroe, que el 65 copia de
    // +0x1f2 y +0x368 cuando la entrada es la del héroe (0x412120). *Estático*: los lee solo 0x412140, el panel del
    // mapa 30 (el asedio, CL9.5).
    std::int16_t heroGuildIndex = -1;
    std::uint8_t heroGuildStatus = 0xff;

    // El pedido de alianza del E5 (0x6565e0): 0x7d536e0 la relación, 0x7d536ea el pedido, 0x7d536e8 y 0x7d536e9 la
    // clave.
    std::uint8_t relation = 0;
    std::uint8_t relationRequest = 0;
    std::uint8_t relationKeyHigh = 0;
    std::uint8_t relationKeyLow = 0;

    // 0x7d536e4 y 0x7d536ec: qué arma la ventana 2 (1, el gremio nuevo; 2, la marca, que solo prende 0x6538e0, sin
    // referencias: Q-C1288) y en qué página va (0..3); 0x6570b0 los vuelve a 0.
    std::int32_t createKind = 0;
    std::int32_t createStep = 0;
    // 0x78aba44: el color elegido de la paleta del editor de la marca (0x6535c0 lo cambia, 0x653750 lo dibuja).
    std::int32_t markColor = 0;

    // 0x7d535a8..0x7d535b3 y 0x7d535b4: el miembro elegido en la pestaña de los miembros (su nombre, que manda el E1
    // de 0x64def0) y su puesto (para la ventana de los cargos, 0x64e600).
    std::array<char, 12> chosenName{};
    std::uint8_t chosenStatus = 0;
    // 0x7d536c8: el lugar en 0x787a5e8 del que se echa (0x64faa0, −1 si no está); 0x64e550 lo copia a 0x6d5e2c (.data
    // −1), que lee el diálogo 0x7d del juego.
    std::int32_t kickIndex = 0;
    std::int32_t kickMember = -1;

    GuildWindowState window{};
    SecondGuildWindowState secondWindow{};
};

// 0x447b00(clave): la primera entrada con la clave, mirando de 0 hasta la cuenta inclusive (una más: con la cuenta en
// 0, la entrada 0, con la clave en −1 desde 0x447ae0). −1 si no hay. Más allá de los 2.000 corta: el original leería
// fuera de la tabla.
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

// 0x447b50(clave): con la cuenta en 2.000 o más, −1; si no, la clave en la entrada de la cuenta, que sube. La entrada
// no se limpia: los nombres y la marca son los que había (ceros, en la .bss, o los de antes de 0x447ae0) hasta que
// llega su 66. Solo 0x447ae0 baja la cuenta.
[[nodiscard]] inline std::int32_t AddGuild(GuildState& guild, std::int32_t key)
{
    if (guild.tableCount >= GuildTableSize)
        return -1;
    const std::uint32_t index = guild.tableCount;
    guild.table[index].key = key;
    ++guild.tableCount;
    return static_cast<std::int32_t>(index);
}

// 0x447ae0 (sobre 0x723c58): la cuenta en 0 y las 2.000 claves en −1, sin tocar los nombres ni las marcas. La llaman
// el constructor de 0x723c58 (0x447a80), el F1 00 aceptado (0x4c1e54), la lista de personajes (0x4c1f14) y la
// entrada al juego (0x636935).
inline void ResetTheGuildTable(GuildState& guild)
{
    guild.tableCount = 0;
    for (GuildTableEntry& entry : guild.table)
        entry.key = -1;
}

// 0x787a5e8 (los miembros, de 0xe) y 0x787aa48 (la tabla, de 0x58) van seguidos (Q-C1281): la entrada −1 empieza en el
// byte 10 del nombre del miembro 73, su +4 es el byte 0 del nombre del miembro 74 y su +0xd, el byte 9.
inline constexpr std::size_t MemberBeforeTheTable = 74;

// Un nombre de la tabla hasta el primer 0, como lo leen strcmp y sprintf.
template <std::size_t Size> [[nodiscard]] std::string_view GuildTextOf(const std::array<char, Size>& text)
{
    return {text.data(), static_cast<std::size_t>(std::ranges::find(text, '\0') - text.begin())};
}

// El gremio (+0xd) de la entrada `index` como lo leen strcmp y sprintf: la −1 da el byte 9 del nombre del miembro 74 y
// el 0 de su byte 10 (+0xa, siempre en 0). Fuera de la tabla, nullopt (el original leería otra memoria).
[[nodiscard]] inline std::optional<std::string_view> GuildNameAt(const GuildState& guild, std::int16_t index)
{
    constexpr std::size_t NameByteBeforeTheTable = 9;
    if (index == NoGuildIndex)
    {
        const auto& name = guild.members[MemberBeforeTheTable].name;
        return std::string_view(name.data() + NameByteBeforeTheTable, name[NameByteBeforeTheTable] == '\0' ? 0 : 1);
    }
    if (index < 0 || static_cast<std::size_t>(index) >= GuildTableSize)
        return std::nullopt;
    return GuildTextOf(guild.table[static_cast<std::size_t>(index)].guildName);
}

// La alianza (+4) de la entrada `index` como la lee strcmp: la −1 da el nombre del miembro 74 (sus bytes 0 a 9 y el 0
// de +0xa). Fuera de la tabla, nullopt.
[[nodiscard]] inline std::optional<std::string_view> UnionNameAt(const GuildState& guild, std::int16_t index)
{
    if (index == NoGuildIndex)
        return GuildTextOf(guild.members[MemberBeforeTheTable].name);
    if (index < 0 || static_cast<std::size_t>(index) >= GuildTableSize)
        return std::nullopt;
    return GuildTextOf(guild.table[static_cast<std::size_t>(index)].unionName);
}

// El primer byte de la alianza (+4) de la entrada `index`: la −1 da el byte 0 del nombre del miembro 74. Fuera de la
// tabla, nullopt.
[[nodiscard]] inline std::optional<char> UnionByteAt(const GuildState& guild, std::int16_t index)
{
    if (index == NoGuildIndex)
        return guild.members[MemberBeforeTheTable].name[0];
    if (index < 0 || static_cast<std::size_t>(index) >= GuildTableSize)
        return std::nullopt;
    return guild.table[static_cast<std::size_t>(index)].unionName[0];
}
} // namespace Client::Scenes
