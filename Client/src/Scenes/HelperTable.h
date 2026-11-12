#pragma once

#include "Core/Expected.h"
#include "Core/IClock.h"
#include "Render/ModelBatch.h"
#include "Render/Models/HalfLifeMath.h"
#include "Scenes/DarkHorse.h"
#include "Scenes/SceneId.h"

#include <glm/ext/vector_float3.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>

namespace Client::Render::Models
{
class CrtRandom;
struct SkinnedMesh;
} // namespace Client::Render::Models

namespace Client::World
{
class FrameSprites;
class ModelEffects;
class Particles;
struct TerrainLighting;
struct WorldTerrain;
} // namespace Client::World

// La tabla de los ayudantes del juego: los 10 lugares de 0x1d4 bytes de 0x7d335d0, en la .bss, que crea
// 0x443fa0 → 0x443d70, apaga 0x443d40, mueve 0x445230 → 0x444010 y dibuja 0x4453b0 → 0x445260. Es una sola
// para las escenas 2, 4 y 5; MuRet la tiene en el mundo (WorldState::Helpers) y la usa en la 4 y en la 5: el
// caballo del login sigue en LoginScene. Porta el caballo oscuro (0xbf), el Uniria (0xc6), el Dinorant (0x110), los
// Fenrir (0x14a..0x14c) y el Guardian Angel (0x1c12). Contrato: Client/docs/research/06-render-escenas-y-ui.md, "El
// caballo oscuro" y "Las mascotas".
namespace Client::Scenes
{
class IModelLibrary;
class WorldState;
struct DrawnObject;

inline constexpr std::size_t HelperCount = 10;

// Un lugar: una proyección con lo que MuRet lee, no el layout. Arranca en cero, como la .bss.
// 0x443d70 escribe además +4 (el quinto argumento, 0), la malla oculta (+0x58) en −1, la luz del blendMesh
// (+0x68) en 1, la meta del alpha (+0x170) en 1, la luz por vértice (+0xe8) en 1, +0xf0, +0x16d y +0x11c en
// 0, +0x60 en 30 y la luz (+0xf4..+0xfc) en 3: MuRet los lleva fijos (DarkHorse, DrawDarkHorse y el dibujo de
// los demás en Scenes/HelperTable.cpp) o no los lee nadie de lo portado. +0x74 no lo escribe nadie: en 0, el
// 0x6004aa de 0x6002e0 no toma su camino del 0x80.
struct HelperState
{
    bool alive = false;     // +0
    std::uint16_t type = 0; // +2, el modelo
    float scale = 0.0f;     // +0xc, 1 para el 0xbf (0x443e82) y 1 en el dibujo de la escena 5 (0x445309)
    glm::vec3 position{};   // +0x10
    glm::vec3 angles{};     // +0x1c
    // +0xcc..+0xd4: el vuelo del 0x1c12 (0x445052..0x445221). 0x443d70 no la escribe: un lugar que se vuelve a
    // usar se queda con la del ayudante de antes (Q-C420).
    glm::vec3 velocity{};
    // +0x64: −1 de 0x443d70, y 1 con el 0x1c12 (0x443f1a), que dibuja su malla 1 como blendMesh.
    std::int16_t blendMesh = -1;
    std::size_t rider = 0; // +0x108, el jinete: un puntero al pool en el original, el lugar del pool en MuRet
    bool seen = false;     // +0x16c, del dibujo (0x5fba90)
    // +0x111..+0x118 la animación, +0xd8 la velocidad y +0x174 el alpha: los del caballo, que son los mismos campos
    // en todos los ayudantes.
    DarkHorse horse{};
};

using HelperTable = std::array<HelperState, HelperCount>;

// 0x443fa0(tipo, posición, jinete, 0, 0), como la llaman 0x5bc740 y los otros armados del juego: en los
// mapas 18 a 23, o con un jinete que no es un jugador (0x20a) y un tipo que no es 0x1c12, nada. Si no,
// 0x443d70 en el primer lugar sin el byte vivo: vivo, el tipo, el jinete, la posición que recibe, los
// ángulos del jinete, el frame y la key anterior en 0, la velocidad en 0,5, el alpha en 0, sin verse y,
// con el 0xbf, la escala en 1. La acción y la anterior quedan las del uso anterior del lugar (Q-C221).
// Con los 10 vivos no crea nada. También construye 0xc6, 0x110 y 0x14a..0x14c con escala 0,9, y
// 0x1c12 con escala 0,7 y sus tres rand() de posición. Este último requiere `random`.
// Devuelve el lugar, o nullopt si no creó. Los otros tipos cortan antes de 0x443d70.
[[nodiscard]] Core::Expected<std::optional<std::size_t>, std::string>
CreateHelper(WorldState& world, std::uint16_t type, const glm::vec3& position, std::size_t rider,
             Render::Models::CrtRandom* random = nullptr);

// 0x443d40(jinete): apaga el byte vivo de los lugares vivos de ese jinete. Lo demás queda.
void TurnOffHelpersOf(HelperTable& helpers, std::size_t rider);

// Lo que el paso de los ayudantes lee fuera del mundo. Del mundo usa además las partículas (0x743a198), los
// efectos (0x7491fd8) y el buffer global de la pose del dibujo anterior (0x68b181c, WorldState::PoseCarry).
struct HelperStepContext
{
    WorldState& world;
    // Los modelos (0x5769ad8 + tipo · 0xe0), por las acciones que leen 0x4ea030 y 0x4ec200: los de Skill\ que carga
    // 0x615700 y Player\Helper01.bmd (el 0x1c12) de 0x60ff50. nullptr, o sin el del tipo: corta con uno vivo.
    const IModelLibrary* models = nullptr;
    Render::Models::CrtRandom& random;
    // El reloj del mundo (0x5767b88, en ms), con que 0x53fe70 gira las 5f2 al crearlas.
    float worldClock = 0.0f;
    // DAT_006d5dc8: solo la 5 apaga el ayudante sin jinete (0x44402a), y la 4 decide el final de la animación
    // con el frame + 2 (0x4ec200, Q-C21).
    SceneId scene = SceneId::Game;
    // Los atributos (0x7d0d348) de la carga de la escena 4; vacío, los del mapa del juego.
    const World::WorldTerrain* terrain = nullptr;
    // 0x7d3a0f0, la cuenta de los pasos lógicos que 0x4453e0 lee cruda para la onda 0x6eb/0 de la acción 3 del
    // caballo (P-C22 a: en el original es la cuenta cifrada, Q-C125).
    std::int32_t logicSteps = 0;
    // 0x7d325c8, el temblor de la cámara que escribe 0x4453e0 (0x445577). nullptr: corta si la rama lo pide.
    float* cameraShake = nullptr;
};

// 0x445230: 0x444010(lugar, 0) en los 10, en la escena 5 (0x6370f4) y en la 4 (0x6342f4). En la 5, con el jinete
// muerto o con su +0x88 distinto de 1, apaga el lugar (0x44402a..0x444047, Q-C225). Si no, el alpha sube
// (0x4e9980), el cuerpo del tipo y el avance de su acción (0x445006..0x445039):
// - el 0xbf: en la celda de zona segura de la posición del jinete (0x4440b8..0x4440f8, Q-C222), el alpha a 0;
//   si no, copia la posición y los ángulos del jinete, toma su acción y su velocidad, saca el polvo del galope
//   (0x44418e..0x4444ef: la 5f2 en el 2, la 5f3 en los demás y, en el 10, la onda 0x6eb/1 y las 5f2 desde un
//   hueso del buffer global) y el aliento (0x44453f..0x4445de, del hueso 27 del buffer global) con sus rand();
// - el Uniria y el Dinorant (0x4447c4..0x444ac4): la celda segura igual; el alpha con +0x80 del jinete, su lugar
//   (el Dinorant más abajo en los mapas que no son el −1), su acción, el polvo con el galope y la velocidad 0,34;
// - los Fenrir (0x444af0..0x444f25): igual, con su acción y su velocidad, y el polvo (en el 10, la onda y las
//   5f2 desde los huesos 22 y 44 del buffer global, en dos ventanas del frame);
// - el Guardian Angel (0x444f2a..0x444fff y 0x445052..0x445221): cuatro 5c5/1 y el vuelo hacia el jinete.
// Corta con error, después de los rand() que el original saca antes: con un hueso del buffer global que MuRet no
// conserva, con una partícula que no se puede crear, con la acción 3 del caballo (0x52e470) y con el +8 del
// jinete (0x530860); también sin el mapa cargado o sin el modelo del tipo. Lo que hizo antes del corte queda.
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
    // D-C4: dibujos intermedios proyectan sin publicar seen/frame ni consumir la CRT durable.
    bool originalFrame = true;
    std::function<Core::Expected<void, std::string>(const DrawnObject&)> onPose{};
    bool publishProjectedPose = false;
    std::function<std::span<const Render::Models::SkinnedMesh>()> posedMeshes{};
    // DAT_006d5dc8, por la escala de 0x4452db..0x445309: 1,6 en la 2 y 1 en la 4; en la 5, la del lugar con los
    // Fenrir y 1 con los demás.
    SceneId scene = SceneId::Game;
    // El buffer dinámico de la luz del terreno (0x5ffce0): el color de los que no son el caballo.
    const World::TerrainLighting* lighting = nullptr;
    // La tabla de los sprites del frame (0x5491e0): el brillo del 0x1c12 (0x445390) y los de los Fenrir.
    World::FrameSprites* sprites = nullptr;
    // Lo que el dibujo de los Fenrir (0x60277b) lee y crea además: el reloj del mundo (0x5767b88), los efectos con
    // modelo (0x518cf0), las partículas (0x53fe70), timeGetTime() (0x6b8544, del calco 0x14e) y el buffer global de la
    // pose (0x68b181c) después de posarlo, el del frame o su copia proyectada.
    float worldClock = 0.0f;
    World::ModelEffects* effects = nullptr;
    World::Particles* particles = nullptr;
    std::function<std::uint32_t()> systemTime{};
    std::function<std::span<const Render::Models::BoneTransform>()> globalPose{};
};

// 0x4453b0 en la escena 5 (0x637508) y en la 4 (0x63486a): 0x445260(lugar, 0) en los 10. Con el lugar vivo,
// +0x16c con el trapecio global (0x5fba90, en su lugar por 0,01f y −20); visto y con el jinete un jugador (o con
// el 0x1c12): la escala de `scene` (0x4452db..0x445309) y 0x602e90, que no lo dibuja con el alpha por debajo de
// 0,01 y con el +0x78 & 0x1000 del jinete le pone el frame en 0 (0x5fff0d, Q-C224). 0x6002e0 dibuja el 0xbf con
// DrawDarkHorse (con la sombra del mapa), y el Uniria, el Dinorant (0x600b35) y el Guardian Angel (0x602360) con
// RenderBody(2) y su blendMesh, con el color de la luz del terreno más 3 (0x5ffce0, con la luz por vértice).
// Después, el rand() % 30 + 70 de 0x445336 aunque no lo haya dibujado (Q-C223), y con el 0x1c12 el sprite 0x5ac
// en su lugar, de escala 1, con esa cuenta por 0,01f en (0,5; 0,8; 0,6). En los mapas 18 a 23, con +0x1a1 del
// héroe, 0x5ffdf0 le baja 0,15f al alpha guardado en cada frame del original. Los Fenrir (0x60277b) van en blanco y
// sin la luz del terreno (DrawFenrirBody) con el seno del reloj sin(T · 0,002f) · 0,2f: con la acción 4, la chispa
// 0x5c6/15 del hueso 14 con dos rand(); los seis sprites 0x5ac de los huesos 11 y 13; dos rayos 0x14d (0x518cf0, con
// sus rand()) con la luz y el subtipo del color; y, con la acción 1 y el frame entre 0 y 1,5, cuatro calcos 0x14e en
// los huesos 22, 28, 36 y 44, o con la 2, dos en el 22 y el 28 con el frame en (1; 1,4] o dos en el 36 y el 44 en
// (4,8; 5,2]. Corta con error con el +0x78 & 0x4000 del jinete (0x602e90 con 10, 0x60033c), sin la luz del terreno
// o sin la tabla de los sprites cuando los pide, y con un Fenrir sin los efectos, las partículas, timeGetTime() o el
// buffer global de la pose.
[[nodiscard]] Core::Expected<void, std::string> DrawHelpers(const HelperDrawContext& context);
} // namespace Client::Scenes
