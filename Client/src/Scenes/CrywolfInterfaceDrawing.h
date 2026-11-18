#pragma once

#include "Core/Expected.h"

#include <string>

// La interfaz de Crywolf (CL9 hito 11, fase 3; 06 § La interfaz de Crywolf): 0x4365c0, el hijo 21 del HUD (0x5a74d4),
// *estático*. GS 0.97 no tiene Crywolf: lo que la llena son el BD 00 y los BD 02 a 09 (Scenes/EventPackets), con el
// ASM del cliente de oráculo; lo que la VM tiene que confirmar está en 08 (P-C298).
namespace Client::Scenes
{
struct GameHudContext;

// 0x4365c0: 0x620a00(1) y, en este orden,
//   - con 0x723bf9 en 1, los resultados: el marco 0x6e2, los títulos 680, 681, 1973 y 1977 y las cinco filas (el
//     lugar, el nombre, la clase y los puntos, sin las de puntos en −1), el texto 2000 y 2001 (o 2009 y 2010) y el
//     botón 0x6c9..0x6cb, que con el mouse encima y 0x723bdc en 5 cierra la ventana;
//   - con el aviso (0x6c73d1) en 0 o más, el cartel 0x6b6 + 0x6c73dc que entra, se queda y sale, el rango con su letra
//     y las nueve cifras de la experiencia;
//   - con la etapa en 3 o 4 en el mapa 34, el estado: el marco, los cinco altares, la barra de Balgass, el reloj
//     (0x6218f0), la luz y los consejos de 0x430180 (con la etapa 3); termina con 0x620980.
// Sin el estado no vuelve a 0x620980 y la mezcla queda en la normal. Escribe en `context.crywolfInterface` (o en la de
// WorldRuntime): las cuentas del cartel, la barra, el reloj y el consejo. Corta solo sin un texto de Text.bmd o con
// uno que no entra en el buffer de 300 bytes de la pila.
[[nodiscard]] Core::Expected<void, std::string> DrawTheCrywolfInterface(GameHudContext& context);

// 0x436240, el hijo 26 del HUD (0x5a74ed): con la etapa en 3 o 4 (0x435a00) en el mapa 34 (0x4303c0) deja el texto en
// 0xff1594ff sobre 0 para lo que sigue del HUD, y con 0x723a44 en 1 o 2 dibuja la ventana del contrato de los altares
// (0x436274..0x43656f, fase 4): el fondo 0x6cc, el No y el Sí (con 1) o el OK (con 2), que con el mouse encima y su
// número en 0x723bdc (1, 2 o 3) cierran la ventana, y los dos renglones de 0x723a48 con 0x623690. La abre 0x435b00
// (Scenes/CrywolfContract). Escribe en `context.crywolfInterface` (o en la de WorldRuntime). No saca rand().
[[nodiscard]] Core::Expected<void, std::string> DrawTheCrywolfContract(GameHudContext& context);
} // namespace Client::Scenes
