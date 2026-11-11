#pragma once

#include "Core/Expected.h"
#include "Scenes/SceneId.h"
#include "Scenes/WorldState.h"

#include <cstdint>
#include <filesystem>
#include <string>

namespace Client::Render::Models
{
class CrtRandom;
}

// La carga del mapa en la CPU: 0x61a560 (con 0x510f70, que mata a los personajes de otra clave) y
// 0x447d70, los objetos de los mapas de evento. Contrato: Client/docs/research/06-render-escenas-y-ui.md,
// "La carga del mapa" y la tabla de 0x4c3940.
namespace Client::Scenes
{
struct WorldMap;

// Lo que la carga lee y escribe fuera del mapa. Quien llama lo presta durante la llamada.
struct MapLoadContext
{
    const std::filesystem::path& dataRoot;
    // El mundo en curso (DAT_006c9dd0), el pool de personajes y el mapa cargado (WorldState::Map).
    WorldState& world;
    // DAT_006d5dc8: en la selección (4) no corre 0x510f70.
    SceneId scene;
    // DAT_057678bc, la clave del jugador que dio el F1 00.
    std::uint16_t playerKey;
    // El rand() de la CRT: el pasto y los objetos que lo piden al crearse.
    Render::Models::CrtRandom& random;
};

// 0x61a560(argumento), con el mundo en curso ya escrito por quien llama: la guarda de 32 a 9 (Q-C58);
// suelta lo del mapa anterior (0x6070a0); fuera de la selección, 0x510f70; los especiales de 0x616ec0
// (los del 2); el corrimiento del pasto con 65.536 rand() (0x5f8300); el MAP/ATT, los objetos del .obj con 0x606660 y
// después las alturas/luz. Corta con error, con lo que el original hizo antes, en lo que MuRet no tiene: los especiales
// del 55, el mapa 30 (0x42a760 y el .att de 0x4294b0), el 34 (el .att según Crywolf, 0x42fe50) y un objeto que pide un
// servicio de efectos o del asedio. Cada error conserva el diagnóstico del archivo; sólo los casos que
// piden aviso/cierre en ASM publican WorldRuntime::fatalDataMessage (Q-C27/Q-C180), sin UI aquí.
// El ATT de tamaño inválido, los archivos cortos y unsupported-map no piden cierre nativo. 4377a0 reemplaza el dueño
// de Kalima recién después de leer los archivos: un error anterior conserva el del mapa previo.
[[nodiscard]] Core::Expected<void, std::string> LoadMap(std::uint8_t argumentMap, const MapLoadContext& context);

// 0x510f70(clave): mata al vivo del pool del mundo cuya clave, con signo (movsx en 0x510f8a), no es
// `key`: +0 en 0, 0x401040 y apaga los ayudantes que lo llevan (+0x108, 0x510fa1..0x510fc0, como
// 0x443d40). Recorre 400 registros, uno menos que el pool. El muerto y el de la misma clave saltan los dos
// a 0x510fc2: con los 400, también con el que sobrevive, suelta el cuervo (0x421d60, +0x430), las telas
// (0x4fb1a0) y +0x364 (0x414340) (Q-C194). MuRet no tiene eso en el mundo: ni cuervos ni telas de los
// personajes del juego. La carga pasa la clave del jugador; la vuelta a la selección, −1 (0x61ef04): solo
// sobrevive una clave 0xffff (Q-C212).
void KillOtherActors(WorldState& world, std::int32_t key);

// 0x6070a0: el byte vivo de los efectos y los objetos del mapa (06, "La carga del mapa", paso 2). No toca
// el terreno ni el pasto, que la carga pisa después. El registro de los objetos (registrations) queda: su
// limpieza no está leída.
void ReleaseTheMapObjects(WorldMap& loaded);
// Los otros pools de 0x6070a0: sólo apaga los vivos, sin reconstruir los registros.
void ClearWorldTransientPools(WorldState& world);

// 0x447d70(mundo en curso): arma el objeto de evento de Devil Square (9), Blood Castle (11 a 17) y
// Chaos Castle (18 a 23); en los demás no hace nada. MuRet no tiene esos objetos: en esos mapas corta
// con error.
[[nodiscard]] Core::Expected<void, std::string> CheckEventMap(std::int32_t map);
} // namespace Client::Scenes
