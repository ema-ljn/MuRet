#pragma once

#include "Protocol/Connection/ServerList.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// La lista de servidores como la arma 0x4c1860 con el F4 06 y la muestra 0x645860 (CL5.5; 06 § La
// lista de servidores; Q-C62 y Q-C63). Cada servidor va al grupo `código / 20`, con el número
// `código % 20 + 1`, y el grupo va a un lugar de la pantalla por una tabla de 25: del lugar 4 al 24
// hay un botón de grupo, en el orden 20, 16, 15, 14, 13, 11, 19, 18, 17, 10, 9, ..., 0 y 12. Los
// nombres y las cargas son textos de Text.bmd, por índice. *Estático*, de 0x4c1860, 0x645860 y
// 0x447c70.
//
// Lo que MuRet hace distinto: los botones salen de todos los lugares con servidores, no hasta el
// primero vacío (Q-C62); los grupos sin botón (21 a 24) y los códigos de 500 o más, que el
// original escribe fuera de su arreglo, se ignoran y se cuentan; y un grupo guarda hasta 20
// servidores, donde el original sigue escribiendo sobre el grupo que sigue (Q-C63).
namespace Client::UI
{
// Los grupos que caben en la tabla de 0x4c1860: los códigos de 0 a 499.
inline constexpr std::uint16_t ServerGroupCount = 25;
// Los números de un grupo (código % 20), y los botones de servidor de la ventana de MuRet (el
// original tiene 16: Q-C63).
inline constexpr std::size_t ServersPerGroup = 20;
// Los lugares con botón: del 4 al 24, 21 botones de grupo.
inline constexpr int FirstButtonSlot = 4;
inline constexpr int LastButtonSlot = 24;
inline constexpr std::size_t GroupButtonCount = static_cast<std::size_t>(LastButtonSlot - FirstButtonSlot + 1);

// Un grupo con servidores, en su lugar de la pantalla.
struct ServerGroup
{
    // código / 20.
    std::uint16_t group = 0;
    // El lugar de 0x4c1860, del 4 al 24.
    int slot = 0;
    // En el orden de la lista, hasta ServersPerGroup.
    std::vector<Protocol::Connection::ServerEntry> servers;
};

struct ServerGroups
{
    // En el orden de los lugares: el del botón 0 primero.
    std::vector<ServerGroup> groups;
    // Los servidores que no se muestran: de un grupo sin botón, o del 21 en adelante de un grupo.
    std::size_t ignored = 0;
};

// 0x4c1860: el lugar del grupo, 23 menos la tabla de 25, salvo el 12, que va al 24. Los grupos 21
// a 23 dan 2 a 0 y el 24 da -1, sin botón; del 25 en adelante la tabla se lee fuera de la pila
// (Q-C63) y da nada.
[[nodiscard]] std::optional<int> GroupSlot(std::uint16_t group);

// Reparte la lista en sus grupos, en el orden de la pantalla.
[[nodiscard]] ServerGroups GroupServers(std::span<const Protocol::Connection::ServerEntry> list);

// El texto del nombre del grupo: 540 + g, salvo el 12 (559), el 19 (552) y el 20 (1829).
[[nodiscard]] std::size_t GroupNameText(std::uint16_t group);

// código % 20 + 1.
[[nodiscard]] int ServerNumber(std::uint16_t code);

// 0x447c70: el 1 y el 15 son sin PvP, salvo en el lugar 4.
[[nodiscard]] bool IsNonPvp(int slot, int number);

// El texto de la carga (0x645860): 562 debajo de 100, 561 de 100 a 127 y 560 de 128 en adelante.
[[nodiscard]] std::size_t LoadText(std::uint8_t load);

// El título de la cuenta (0x641990): el 460 sin PvP y el 461 si no, con el nombre del grupo y el
// número.
[[nodiscard]] std::size_t LoginTitleText(int slot, int number);

// La marca de sin PvP de 0x645860, del binario (0x6d6738), no de Text.bmd.
inline constexpr std::string_view NonPvpMark = "(Non-PVP)";

// Los rótulos de los botones de servidor, con los formatos del binario: "%s%s %s" para el único de
// un grupo (0x6d6724) y "%s-%d%s %s" con más (0x6d672c), con la marca o nada.
[[nodiscard]] std::string SingleServerLabel(std::string_view group, bool nonPvp, std::string_view load);
[[nodiscard]] std::string ServerLabel(std::string_view group, int number, bool nonPvp, std::string_view load);
} // namespace Client::UI
