#pragma once

#include "Core/Expected.h"

#include <string>

// CL9 hito 11, fase 2: los dibujos de los eventos en la pasada 2D del juego (06 § Los eventos), *estático*: no hay
// traza de GL de ninguno; lo que la VM tiene que confirmar está en 08 (P-C294).
namespace Client::Scenes
{
struct EventMapObject;
struct GameHudContext;

// 0x5a7eb0, después del HUD (0x637668): con la palabra del F3 22 (0x5767816) distinta de 0 y a lo sumo 10 minutos, el
// tiempo con la fuente 0x576355c, centrado en la mitad del ancho del mundo (0x5bb8d0, con sus rand()): en color en
// (x, 350) y en blanco en (x + 1, 351). La otra mitad (0x5a800e, la guerra de gremios con 0x57678ed distinto de
// 0x77f3cc0) es del hito 8: MuRet no escribe 0x57678ed y no la alcanza.
[[nodiscard]] Core::Expected<void, std::string> DrawTheEventTime(GameHudContext& context);

// El método +4 del objeto del evento: 0x40c2d0 (Blood Castle, mapas 11 a 17, +0x10 en 1, 2 o 5) y 0x405090 (Chaos
// Castle, mapas 18 a 23, +0x10 de 6 a 8), con +0x18 (el tiempo) en 1 o más: los puntos de +0x20 y +0x1c, el 865 y
// el tiempo (0x40bbf0). Chaos Castle, después, elige el área de la trampa (0x6c634c) por +0x20, solo si `write` (un
// dibujo intermedio de MuRet no escribe). Devil Square (0x40bdb0) es un ret.
[[nodiscard]] Core::Expected<void, std::string> DrawTheCastleTimeAndPoints(GameHudContext& context,
                                                                           EventMapObject& event, bool write);

// 0x632a2b: el método +0x10 del objeto del evento con la caja 0x8b, 0x97 o 0x9b, por su tipo y no por la caja: los
// resultados de Devil Square (0x40bdd0, el ranking de +0x24 registros de +0x2c), de Blood Castle (0x40c440) y de
// Chaos Castle (0x405220), con el registro de +0x2c (+0x38, +0x3c y +0x40) y +0x24.
[[nodiscard]] Core::Expected<void, std::string> DrawTheEventBoxText(GameHudContext& context,
                                                                    const EventMapObject& event);
} // namespace Client::Scenes
