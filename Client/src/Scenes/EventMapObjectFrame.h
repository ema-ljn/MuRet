#pragma once

#include "Core/Expected.h"

#include <string>

namespace Client::World
{
struct WorldObject;
}
// 0x602ed0 con Devil Square (el mapa 9, 0x603dd7) y con los Blood Castle (11 a 17, 0x604152), y la cadena de
// 0x6044ed con los Chaos Castle (18 a 23, 0x4059d0), después del rand() común de 0x602ef4. Contrato: 06 § Los
// objetos de Devil Square y de Blood Castle, y § Los objetos de Chaos Castle.
namespace Client::Scenes
{
struct GameObjectPass;

// El 2: con rand() & 3 en 0, una 68/0 en el hueso 23 de la pose global (0x4ec110 con (−15; 0; 0)); con otro
// rand() & 3 en 0, otra en el hueso 31; y siempre una más en el 23. Las tres con los ángulos del objeto, luz 1,
// escala 1 y sin dueño. Los demás tipos, nada.
[[nodiscard]] Core::Expected<void, std::string> StepDevilSquareObjectFrame(const World::WorldObject& object,
                                                                           const GameObjectPass& pass);

// Con el reloj del mundo T y el seno en la x87:
// - el 0xb: siete sprites 0x5ac de 0,5 en (0; 0; 2) por los huesos 1, 2, 4, 6, 9, 10 y 11, sin giro, con la luz
//   (a; a · 0,5; 0), a = (sin((ángulo z · 20 + T) · 0,001f) + 1) · 0,5 (la y desde a sin redondear);
// - el 0xd: un sprite 0x60f en el hueso 3, con v = sin(T · 0,001f) · 0,3f + 0,7f guardada en float, la luz
//   (v; v; v) y la escala v + 0,5;
// - el 0x25: con rand() & 1 en 0, ftol(fase + 2) % 4 y la fase (+0x84) más 1; con 0, una 6ac/0 y una 6ab/0 en el
//   objeto. Después, con otro rand() & 1 en 0, ftol(fase) % 4 y la fase más 1; con 0, una 622/6 y una 6ab/1 con
//   luz 1 y una 60f/4 con luz (1; 0,8f; 0,8f) y escala 0,19f. Todas con los ángulos del objeto y sin dueño.
// Los demás tipos, nada. Un seno o un ftol fuera del perfil acreditado, la pose, los sprites o el pool ausentes
// y un alta que MuRet no tiene cortan con su dirección, después de lo ya hecho.
[[nodiscard]] Core::Expected<void, std::string> StepBloodCastleObjectFrame(World::WorldObject& object,
                                                                           const GameObjectPass& pass);

// 0x4059d0, con el área de 0x6c634c (`pass.chaosCastleArea`, un byte sin signo):
// - del 0 al 3: con +0x8a distinto de 0 (lo prende 0x4055b0) y +0x58 distinto de −2, el punto del hueso 1 de la
//   pose global; con +0x60 en 10, ahí el joint 0x615/2 (escala rand() % 10 + 60, los ángulos del objeto) y el
//   sonido 0x9c + (rand() & 1); +0x60 por debajo de 5 apaga +0x8a y, si no, baja uno. Después, como el 4;
// - el 4, el 5 y del 0xd al 0x11: con el área menor que 2 o en 0xff, nada; si no, +0x58 = −2;
// - del 6 al 0xc: la primera vez (+0x58 distinto de −2), nubes 622 con la posición, los ángulos y la escala del
//   objeto y el objeto de dueño: diez de los subtipos 0, 1 y 2 (el 6, el 7 y el 8), cinco del 3, el 4 y el 5 (el
//   9, el 0xa y el 0xb), todas con la luz (0,05f; 0,05f; 0,1f), y siete del 7 (el 0xc) con la luz 0,3f. Después,
//   +0x58 = −2;
// - del 0x12 al 0x15: con el área en 7 o en 8, +0x58 = −1; si no, −2;
// - del 0x18 al 0x1d: con el área en 4 o en 5, −1; con otra menor que 8, nada; si no, −2;
// - del 0x1e al 0x23: con el área en 1 o en 2, −1; con otra menor que 5, nada; si no, −2;
// - el 0x16, el 0x17 y los demás, nada.
// Sin pool, sin pose, sin quien cree el joint o sin sonido, o con un alta que MuRet no tiene, corta con su
// dirección, después de lo que ya hizo.
[[nodiscard]] Core::Expected<void, std::string> StepChaosCastleObjectFrame(World::WorldObject& object,
                                                                           const GameObjectPass& pass);
} // namespace Client::Scenes
