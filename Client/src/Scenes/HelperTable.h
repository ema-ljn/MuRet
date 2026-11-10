#pragma once

#include "Core/Expected.h"
#include "Core/IClock.h"
#include "Render/ModelBatch.h"
#include "Render/Models/Skeleton.h"
#include "Scenes/DarkHorse.h"

#include <glm/ext/vector_float3.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace Client::Render::Models
{
class CrtRandom;
} // namespace Client::Render::Models

// La tabla de los ayudantes del juego: los 10 lugares de 0x1d4 bytes de 0x7d335d0, en la .bss, que crea
// 0x443fa0 → 0x443d70, apaga 0x443d40, mueve 0x445230 → 0x444010 y dibuja 0x4453b0 → 0x445260. Es una sola
// para las escenas 2, 4 y 5; MuRet la tiene en el mundo (WorldState::Helpers) y la usa en la 5: el caballo
// del login sigue en LoginScene, y el de la selección, en su escena (P-C47). Solo el caballo oscuro (0xbf):
// los otros tipos cortan con error. Contrato: Client/docs/research/06-render-escenas-y-ui.md, "El caballo
// oscuro".
namespace Client::Scenes
{
class IModelLibrary;
class WorldState;

inline constexpr std::size_t HelperCount = 10;

// Un lugar: una proyección con lo que MuRet lee, no el layout. Arranca en cero, como la .bss.
// 0x443d70 escribe además +4 (el quinto argumento, 0), el blendMesh y la malla oculta (+0x58 y +0x64) en
// −1, la luz del blendMesh (+0x68) en 1, la meta del alpha (+0x170) en 1, la luz por vértice (+0xe8) en
// 1, +0xf0, +0x16d y +0x11c en 0, +0x60 en 30 y la luz (+0xf4..+0xfc) en 3: el caballo de MuRet los
// lleva fijos (DarkHorse y DrawDarkHorse) o no los lee nadie de lo portado.
struct HelperState
{
    bool alive = false;     // +0
    std::uint16_t type = 0; // +2, el modelo
    float scale = 0.0f;     // +0xc, 1 para el 0xbf (0x443e82) y 1 en el dibujo de la escena 5 (0x445309)
    glm::vec3 position{};   // +0x10
    glm::vec3 angles{};     // +0x1c
    std::size_t rider = 0;  // +0x108, el jinete: un puntero al pool en el original, el lugar del pool en MuRet
    bool seen = false;      // +0x16c, del dibujo (0x5fba90)
    DarkHorse horse{};      // +0x111..+0x118 la animación, +0xd8 la velocidad y +0x174 el alpha
};

using HelperTable = std::array<HelperState, HelperCount>;

// 0x443fa0(tipo, posición, jinete, 0, 0), como la llaman 0x5bc740 y los otros armados del juego: en los
// mapas 18 a 23, o con un jinete que no es un jugador (0x20a) y un tipo que no es 0x1c12, nada. Si no,
// 0x443d70 en el primer lugar sin el byte vivo: vivo, el tipo, el jinete, la posición que recibe, los
// ángulos del jinete, el frame y la key anterior en 0, la velocidad en 0,5, el alpha en 0, sin verse y,
// con el 0xbf, la escala en 1. La acción y la anterior quedan las del uso anterior del lugar (Q-C221).
// Con los 10 vivos no crea nada. Devuelve el lugar, o nullopt si no creó. Otro tipo que el 0xbf corta con
// error antes de 0x443d70.
[[nodiscard]] Core::Expected<std::optional<std::size_t>, std::string>
CreateHelper(WorldState& world, std::uint16_t type, const glm::vec3& position, std::size_t rider);

// 0x443d40(jinete): apaga el byte vivo de los lugares vivos de ese jinete. Lo demás queda.
void TurnOffHelpersOf(HelperTable& helpers, std::size_t rider);

// Lo que el paso de los ayudantes lee fuera del mundo.
struct HelperStepContext
{
    WorldState& world;
    // El de Skill\DarkHorse.bmd (0x615700). nullptr sin los modelos: corta con error con un caballo vivo.
    const Render::Models::Skeleton* horseSkeleton = nullptr;
    Render::Models::CrtRandom& random;
};

// 0x445230 en la escena 5 (0x6370f4): 0x444010(lugar, 0) en los 10. Con el jinete muerto o con su +0x88
// distinto de 1, apaga el lugar (0x44402a..0x444047, Q-C225). Si no, con el 0xbf: el alpha sube
// (0x4e9980) y, en la celda de zona segura de la posición del jinete (0x4440b8..0x4440f8, Q-C222), va a 0
// y avanza; si no, copia la posición y los ángulos del jinete, toma su acción y su velocidad, saca el
// polvo y el aliento con sus rand() (0x44418e..0x44457a), y avanza. Corta con error, después de los
// rand() que el original saca antes, donde crea efectos o partículas que MuRet no tiene en el juego: el
// polvo (en el mapa 10 antes de su primer rand()), el aliento, la acción 3 (0x52e470) y el +8 del jinete
// (0x530860); también sin el mapa cargado o sin el esqueleto. Lo que hizo antes del corte queda.
[[nodiscard]] Core::Expected<void, std::string> StepHelpers(const HelperStepContext& context);

// Lo que el dibujo de los ayudantes lee y escribe fuera del mundo.
struct HelperDrawContext
{
    Render::ModelBatch& batch;
    const IModelLibrary& models;
    WorldState& world;
    Render::Models::CrtRandom& random;
    Core::Duration frameClock{};
    // Lo que va del último paso al próximo, para la animación (D-C4).
    float stepAlpha = 0.0f;
};

// 0x4453b0 en la escena 5 (0x637508): 0x445260(lugar, 0) en los 10. Con el lugar vivo, +0x16c con el
// trapecio global (0x5fba90, en su lugar por 0,01f y −20); visto y con el jinete un jugador: la escala en 1
// y 0x602e90 (DrawDarkHorse, con la sombra del mapa), que no lo dibuja con el alpha por debajo de 0,01 y
// con el +0x78 & 0x1000 del jinete le pone el frame en 0 (0x5fff0d, Q-C224); después, el rand() % 30 de
// 0x445336 aunque no lo haya dibujado (Q-C223). Corta con error en los mapas 18 a 23 (0x5ffe09 lee +0x1a1
// del héroe) y con el +0x78 & 0x4000 del jinete (0x602e90 con 10, 0x60033c). En MuRet no corre: el dibujo
// del juego corta antes, en el pasto (0x5fc353).
[[nodiscard]] Core::Expected<void, std::string> DrawHelpers(const HelperDrawContext& context);
} // namespace Client::Scenes
