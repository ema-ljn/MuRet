#pragma once

#include "Core/Expected.h"

#include <string>

namespace Client::World
{
struct WorldObject;
}
// 0x602ed0 con Devil Square (el mapa 9, 0x603dd7) y con los Blood Castle (11 a 17, 0x604152), después del rand()
// común de 0x602ef4. Contrato: 06 § Los objetos de Devil Square y de Blood Castle.
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
} // namespace Client::Scenes
