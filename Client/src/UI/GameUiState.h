#pragma once

#include "UI/ChatLog.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

// Los globales de la UI del juego que leen y escriben el paso de la UI (0x636ded..0x63705b), los diálogos
// (0x623e70), MoveHero y el picking del frame (06 § HUD y ventanas, "El clic es primero de la UI"). Todos en la
// .bss salvo los que dicen otra cosa; ninguna escena los vuelve a poner en 0 fuera de los lugares que se citan.
namespace Client::UI
{
// 0x77f36d4: el latch de 0x553520, un int por tecla virtual. Lo comparten Esc (0x623e70 y 0x490490), las teclas
// del chat, las de los paneles, Q/W/E, B, R y el Tab del panel del asedio: el primero que lee una tecla abajo
// pone su latch en 1 y los demás la ven como ya leída hasta que se suelta.
inline constexpr std::size_t VirtualKeyCount = 256;
// VK_MENU, el Alt del hover (0x5bbd6a).
inline constexpr std::size_t AltVirtualKey = 0x12;

// 0x553520(vk), con el latch de esa tecla: con la tecla abajo y su latch en 0, lo pone en 1 y da verdadero; con la
// tecla arriba, lo vuelve a 0 y da falso. GetAsyncKeyState mira el teclado en el momento: MuRet mira el estado del
// frame (Input::InputState).
inline bool LatchTheKey(std::int32_t& latch, bool down)
{
    if (!down)
    {
        latch = 0;
        return false;
    }
    if (latch != 0)
        return false;
    latch = 1;
    return true;
}

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
    // 0x7d3685c: el derecho abajo. Lo prende WM_RBUTTONDOWN, lo apaga WM_RBUTTONUP y lo apaga quien lo usa (el
    // registro del chat, 0x45d760).
    bool rightDown = false;
    // 0x6c8e3c (.data 1): el ataque automático. Lo escriben el F3 30 (el bit 0x01, 0x4dd7d7), el menú de Esc
    // (0x625632) y la vuelta a la selección (0x4c2533, en 1). Sus lectores de MuRet (MoveHero en 0x59b22f y 0x59b89e y
    // el picking en 0x59ee4b) lo piden junto con un elegido (0x6c8e30, 0x6c8e38), que MuRet deja en −1.
    bool autoAttack = true;
    // 0x7d3a118: el volumen. WinMain lo lee del registro (VolumeLevel, 0x4bad33..0x4bad55) y, fuera de 0 a 9,
    // pone 5; después 0x632d80 (el DirectSound de 0x41eed0, de la CL10). MuRet no tiene registro: 5. Lo
    // cambian las opciones (0x6447d9) y el menú de Esc del juego (0x6257b8).
    std::int32_t volume = 5;
    // 0x6c9dcc (.data 4): el detalle del brillo de los ítems (Render/Models/ItemGlow.h). Lo cambian las
    // opciones (0x64480a) y el menú de Esc del juego (0x62589b).
    std::uint8_t glowDetail = 4;
    // 0x7d366d8: la rueda, (short) HIWORD(wParam) / 120 del último WM_MOUSEWHEEL (0x4b4c19). Nadie la vuelve a 0 por
    // paso: queda hasta que la usa alguien (el registro del chat, 0x479960; el volumen del menú, 0x625782).
    std::int32_t wheel = 0;
    // 0x77f36d4.
    std::array<std::int32_t, VirtualKeyCount> keyLatches{};
    // 0x78abb38: los nombres de los ítems del piso prendidos. El flanco de Alt (0x553520(0x12)) del hover (0x5bbd6c)
    // lo invierte (0x5bbd7f) y solo lo lee ese dibujo (0x5bbd86).
    std::uint8_t altNames = 0;
    // 0x5763588: Enter. Lo prende el WndProc (0x4b50ff, salvo en Chaos Castle fuera de la zona segura), lo leen y
    // apagan los diálogos (0x6263df y 0x62da58) y lo apaga la cola del frame (0x63b0b7).
    bool enterPressed = false;
    // SendMessageA(hWnd, WM_DESTROY) de los diálogos 0x70, 0x6f, 0x1a y 0x1c (0x62828e): el juego se cierra.
    bool destroyRequested = false;
    // 0x7d3a120 y 0x7d3a124: la esquina de la barra del volumen, que escribe el dibujo de las opciones (0x62db60) y
    // lee su paso (0x62570a).
    std::int32_t volumeBarX = 0;
    std::int32_t volumeBarY = 0;
    // 0x7d3a128: la barra del volumen agarrada.
    std::uint8_t volumeDragging = 0;
    // 0x41eed0(v): lo que el menú le pide a DirectSound al cambiar el volumen (0x6257e5 y 0x6257fa), −10000 en 0 o
    // −2000 · log10(10 / v). El gancho de la CL10, como el sonido de WorldRuntime::lastSoundId.
    std::optional<std::int32_t> volumeRequest{};
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
