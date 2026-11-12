#pragma once

#include <array>
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
    // 0x78abaa8: 0x636df2 lo pone en 0 en cada paso.
    std::int32_t field78abaa8 = 0;
    // 0x5762eb8: 0x636e2f lo pone en 0 en cada paso; lo lee 0x5ee140.
    std::int32_t field5762eb8 = 0;
    // 0x7798bf4: la lista de skills abierta (el botón de la barra, 0x5a06e7).
    std::uint8_t skillListOpen = 0;
    // 0x6c7a88 (.data 1): el lugar del registro del chat. Lo pone en 0 su constructor (0x45b620), que corre
    // desde WinMain antes de la primera escena, y nadie más lo escribe hasta el hito 3.
    std::int32_t chatPlacement = 0;
    // 0x576793c: la salida pedida desde el menú de Esc (0x62411c). Con 1, Open no abre (0x6599a0).
    bool logoutRequested = false;
    // 0x78abb39: con 1, Esc no hace nada en 0x623e70 (0x623eff). La escribe la traba de C3 24 (hito 4).
    std::uint8_t field78abb39 = 0;
    // 0x78abad9: con 1, 0x5ee140 no corre sus hijas (0x5ee181) y 0x5ebca0 no hace nada. Hito 4.
    std::uint8_t field78abad9 = 0;
    // 0x78abaac: con 1, 0x5ee140 no llama a 0x5d8d50 (0x5ee1de). Hito 4.
    std::uint8_t field78abaac = 0;
    // 0x78abb20: con 0, 0x5e7bf0 devuelve 0 y 0x5ee140 sigue. Hito 4.
    std::int32_t field78abb20 = 0;
    // 0x7d36880: el flanco del clic derecho. Lo prende WM_RBUTTONDOWN si el botón no estaba abajo, lo apaga
    // WM_RBUTTONUP (02, la tabla del WndProc) y lo apaga quien lo usa.
    bool rightClick = false;
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
