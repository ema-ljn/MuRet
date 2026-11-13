#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

// Los globales que escriben los paquetes de los eventos (CL9 hito 11, 04 § Los eventos) y que no son del objeto de
// [0x723c60] (EventMapObject) ni de Crywolf (CrywolfGlobals). Arrancan como la .data y la .bss del original.
namespace Client::Scenes
{
// 0x6c9dd8, 0x6c9dd4, 0x6c9ddc y 0x6c9de0 (.data −1, −1, −1 y −1,0f; 0x5ff910 los vuelve a poner así): el derrumbe de
// objetos que pide 0x5ff930(mapa, tipo, cuenta, demora) y que hace 0x5ff960 con cada objeto vivo del paso (0x606560),
// solo con los tres enteros en 0 o más y el mapa en curso igual. 0x606560 le resta uno a la cuenta al final de cada
// paso, también en −1.
struct ObjectCollapse
{
    std::int32_t map = -1;
    std::int32_t type = -1;
    std::int32_t counter = -1;
    float delay = -1.0f;
};

// 0x57678d0..0x57678e2 y 0x57678b4, 0x57678b8, 0x576796f (.bss 0): lo que deja el F3 23 (0x4d9f00), el tanteador de
// Battle Soccer que leen 0x5a7640 (la guerra de gremios, hito 8) y 0x50ba30.
struct SoccerScoreGlobals
{
    // 0x57678d0: los 8 bytes del paquete y un '\0' en el noveno (0x4d9f34).
    std::array<char, 9> firstName{};
    // 0x57678d9: ídem (0x4d9f7e).
    std::array<char, 9> secondName{};
    // 0x57678b4 y 0x57678b8, de pkt[0xc] y pkt[0x15].
    std::int32_t firstScore = 0;
    std::int32_t secondScore = 0;
    // 0x576796f: pkt[0xc] distinto de 0xff.
    bool active = false;
};

// El recorrido de la cámara (CCameraMove, el objeto de 0x6e57a8 en [0x6e5904]) que arma 0x403250 entre los
// inicializadores estáticos, con los valores de 0x403300 (que también le dan la vuelta a la selección, 0x4c26f2, y la
// etapa 3). Solo lo que mira su arranque: el guion (la cámara, +4..+0x150, y los personajes del 0x4039f0) no está.
struct CameraTour
{
    // +0x154: la etapa de Crywolf que deja el BD 00 con 0x4033b0 (pkt[5]) después de 0x42fe80.
    std::int32_t stage = 0;
    // +0x6a (1): la espera antes de empezar, que 0x4033f0 apaga a los 5.000 ms de timeGetTime con la etapa 2.
    bool waiting = true;
    // +0x158: el recorrido cortado (Esc, 0x623edc) o terminado (0x404bc6).
    bool skipped = false;
    // +0x37 (1), +0x148 y +0x140: el reloj de 0x404c90 (vuelve a empezar en la llamada que sigue a la que vence) y las
    // veces que venció.
    bool timerRestart = true;
    std::uint32_t timerStart = 0;
    std::int32_t timerCount = 0;
    // +0x14c (10000): la clave del próximo personaje del guion; 0x403d60 borra de 10000 a esta − 1 (0x510ff0).
    std::int32_t nextActorKey = 10000;
};

struct EventGlobals
{
    ObjectCollapse objectCollapse{};
    SoccerScoreGlobals soccer{};
    // 0x5767814 y 0x5767816 (.bss 0): el F3 22 (0x4e31f8), que lee 0x5a7eb0.
    std::uint8_t timeKind = 0;
    std::uint16_t timeValue = 0;
    // 0x6c9328 (dword) y 0x6c932c (word), .data todo en 0xff: los tres números del Muto del 94 con el tipo 0 y del
    // 96 (0x4dd8f0, 0x4e4a22), que lee la ventana de 0x78abb00.
    std::array<std::uint16_t, 3> mutoNumbers{0xffff, 0xffff, 0xffff};
    // 0x78ab078 (.bss 0): los 0x40 bytes del 9D (0x4e4ab9), que 0x94 con el tipo 2 vacía (0x4dd8f5).
    std::array<std::byte, 0x40> serial{};
    // 0x77f3b6b (.bss 0): la entrada del número de serie, que prende el 94 con el tipo 2 (0x4dd919) y apagan
    // SendMove (0x56b72a) y el botón de cerrar de su ventana (0x5e74b6); el 94 con otro tipo la deja como está. La lee
    // el Enter del WndProc (0x4b5616 y 0x4b8888) y, con 0x6c7b8c en 0, sus teclas (Scenes/EventChipWindow.h).
    bool serialEntry = false;
    // El recorrido de la cámara ([0x6e5904], Scenes/CameraTour.h).
    CameraTour cameraTour{};
    // 0x78ab9cc (.bss 0): los niveles de los seis cuadrados de Devil Square, {mínimo, máximo}, que escribe el dibujo de
    // su ventana (0x5f5a3f, con la tabla del Magic Gladiator y del Dark Lord por la clase del registro propio) y lee
    // su paso al elegir uno (0x5e5a8b, Q-C1336).
    std::array<std::array<std::int32_t, 2>, 6> devilSquareLevels{};
};
} // namespace Client::Scenes
