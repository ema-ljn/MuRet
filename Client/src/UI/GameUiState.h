#pragma once

#include "UI/ChatLog.h"

#include <array>
#include <cstddef>
#include <cstdint>

// Los globales de la UI del juego que leen y escriben el paso de la UI (0x636ded..0x63705b), los diálogos
// (0x623e70), MoveHero y el picking del frame (06 § HUD y ventanas, "El clic es primero de la UI"). Todos en la
// .bss salvo los que dicen otra cosa; ninguna escena los vuelve a poner en 0 fuera de los lugares que se citan.
namespace Client::UI
{
// 0x77f36d4: el latch de 0x553520, un int por tecla virtual. Lo comparten Esc (0x623e70 y 0x490490), las teclas
// del chat, las de los paneles, Q/W/E, B, R y el Tab del panel del asedio: el primero que lee una tecla abajo
// pone su latch en 1 y los demás la ven como ya leída hasta que se suelta.
inline constexpr std::size_t VirtualKeyCount = 256;

// Adónde apunta 0x78abaa8: MuRet guarda la grilla o el equipo y el índice.
struct HoveredItem
{
    enum class Place : std::uint8_t
    {
        None,
        InventoryCell, // 0x78a9bb0 + índice · 0x48
        Equipment,     // [0x7681eb4] + 0xb6c + lugar · 0x48
    };
    Place place = Place::None;
    std::size_t index = 0;
};

struct GameUiState
{
    // 0x775857c: el mouse es de la UI. 0x636e04 lo pone en 0 en cada paso, la UI en 1 y lo leen MoveHero
    // (0x59b37a, 0x59b853), 0x57cfc0 (0x57cff2) y el picking (0x59ec80).
    std::uint8_t mouseOnUi = 0;
    // 0x6c9320 (.data −1): el tooltip de la skill del HUD. 0x636dfa lo vuelve a −1 en cada paso.
    std::int32_t skillTooltip = -1;
    // 0x78a9bac y 0x78a9ba8: dónde va ese tooltip (0x5a06a4, 0x5a06ba).
    std::int32_t skillTooltipX = 0;
    std::int32_t skillTooltipY = 0;
    // 0x78abaa8: el ítem bajo el mouse, un puntero del original. 0x636df2 lo pone en 0 en cada paso, la grilla del
    // inventario (0x5c429e) lo apunta a la casilla y el equipo (0x5c3aad) al lugar; levantar lo vuelve a 0. Lo leen
    // Ctrl+Q/W/E (0x5c4b09) y el cartel.
    HoveredItem hoveredItem{};
    // 0x78aba27: el último argumento de 0x5c4110 o 0 desde el equipo (0x5c4298, 0x5c3ab3).
    std::uint8_t field78aba27 = 0;
    // 0x78abb28 y 0x78abb2c: de qué ventana y qué lugar es el ítem del cartel (0x5aed40): 0 y el lugar del inventario
    // (la casilla + 12, o el del equipo) desde el inventario.
    std::int32_t field78abb28 = 0;
    std::int32_t field78abb2c = 0;
    // 0x5762eb8: la ventana bajo el mouse. 0x636e2f lo pone en 0 en cada paso, el registro del chat (0x45aa80) lo
    // pone en su id y lo leen 0x5ee140 y el clic sin dueño (0x636e66).
    std::int32_t hovered = 0;
    // 0x5762eb4: la ventana que agarró el mouse (el borde o la barra del registro del chat). Con otra que no sea 0,
    // no corren 0x623e70 (0x637c4a), la ventana /move (0x5ecdd1) ni los paneles (0x5ee167).
    std::int32_t capture = 0;
    // 0x5762ec0: la lista que toma las flechas del teclado. La pone el registro del chat y la vuelve a 0 el clic sin
    // dueño con el chat cerrado (0x636ead).
    std::int32_t focusedList = 0;
    // 0x7798bf4: la lista de skills abierta (el botón de la barra, 0x5a06e7).
    std::uint8_t skillListOpen = 0;
    // 0x6c7a88 (.data 1): el lugar del registro del chat, que su constructor (0x45b620) pone en 0 desde WinMain antes
    // de la primera escena; y 0x6c8dd1 (.data 1), F2, el chat normal a la vista.
    ChatModes chatModes{};
    // 0x6c8e68: el campo del chat abierto. Mientras está abierto, su EDIT tiene el foco (el clic sin dueño no se lo
    // saca, 0x636e80).
    bool chatOpen = false;
    // 0x576793c: la salida pedida desde el menú de Esc (0x62411c). Con 1, Open no abre (0x6599a0).
    bool logoutRequested = false;
    // 0x78abb39: la traba del C1 .. 24 que sale. Con 1, Esc no hace nada en 0x623e70 (0x623eff) y el equipo y la
    // grilla no corren (0x5bec50, 0x5c4110). La ponen en 1 los envíos (0x5ca557, 0x5bf294, 0x59bf56) y en 0 el 24
    // cifrado (0x4d1400).
    std::uint8_t field78abb39 = 0;
    // 0x5767954: con más de 0, el paso del equipo (0x5bec50) y de las grillas (0x5c4110) sale sin hacer nada. Lo
    // ponen en 10 el uso de un ítem (0x59ff10, CL9.3) y las respuestas de los ítems; MuRet todavía no lo escribe.
    std::int32_t field5767954 = 0;
    // 0x78abad9: con 1, 0x5ee140 no corre sus hijas (0x5ee181) y 0x5ebca0 no hace nada. Hito 4.
    std::uint8_t field78abad9 = 0;
    // 0x78abaac: con 1, 0x5ee140 no llama a 0x5d8d50 (0x5ee1de). Hito 4.
    std::uint8_t field78abaac = 0;
    // 0x78abb20: con 0, 0x5e7bf0 devuelve 0 y 0x5ee140 sigue. Hito 4.
    std::int32_t field78abb20 = 0;
    // 0x7d36880: el flanco del clic derecho. Lo prende WM_RBUTTONDOWN si el botón no estaba abajo, lo apaga
    // WM_RBUTTONUP (02, la tabla del WndProc) y lo apaga quien lo usa.
    bool rightClick = false;
    // 0x7d3685c: el derecho abajo. Lo prende WM_RBUTTONDOWN, lo apaga WM_RBUTTONUP y lo apaga quien lo usa (el
    // registro del chat, 0x45d760).
    bool rightDown = false;
    // 0x6c8e3c (.data 1): el ataque automático. Lo escriben el F3 30 (el bit 0x01, 0x4dd7d7), el menú de Esc
    // (0x625632) y la vuelta a la selección (0x4c2533, en 1). Sus lectores de MuRet (MoveHero en 0x59b22f y 0x59b89e y
    // el picking en 0x59ee4b) lo piden junto con un elegido (0x6c8e30, 0x6c8e38), que MuRet deja en −1.
    bool autoAttack = true;
    // 0x7d366d8: la rueda, (short) HIWORD(wParam) / 120 del último WM_MOUSEWHEEL (0x4b4c19). Nadie la vuelve a 0 por
    // paso: queda hasta que la usa alguien (el registro del chat, 0x479960; el volumen del menú, 0x625782).
    std::int32_t wheel = 0;
    // 0x77f36d4.
    std::array<std::int32_t, VirtualKeyCount> keyLatches{};
};

// El panel del asedio de la clase 0 (sin gremio), el objeto de 0x20 bytes que 0x412195 arma en el mapa 30
// (vtable 0x6b88a8). Su paso es 0x40f6d0 (vtable + 8) y su dibujo, del HUD.
struct SiegePanelState
{
    std::int32_t x = 0x1e9;   // +0xc
    std::int32_t y = 0x103;   // +0x10
    std::int32_t page = 1;    // +0x14, de 1 a 2
    std::uint8_t visible = 1; // +0x19, Tab lo invierte
    float alpha = 1.0f;       // +0x1c
};
} // namespace Client::UI
