#pragma once

#include "Core/Expected.h"
#include "Audio/SoundId.h"

#include <glm/ext/vector_float3.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <functional>
#include <string>

namespace Client::Crt
{
class CrtRandom;
} // namespace Client::Crt

namespace Client::Models
{
struct Skeleton;
} // namespace Client::Models
namespace Client::Data::Terrain
{
struct TerrainAttributes;
}
namespace Client::Terrain
{
struct TerrainLighting;
} // namespace Client::Terrain

namespace Client::World
{
struct FishEffectRequest;
struct ModelEffect;
struct ModelEffectArguments;
class ModelEffects;
class Particles;
class KalimaWorld;
class FrameSprites;
// 0x5192a3 y 0x5212e7: alta y cuerpo de la estela compartida por Crywolf y el terremoto.
inline constexpr std::int16_t SpiritTrailEffect = 0x104;
[[nodiscard]] std::optional<std::size_t> CreateSpiritTrail(const ModelEffectArguments&, ModelEffects&);

// Lo que los casos piden afuera de su efecto: la altura del terreno (0x5f93c0) de la mariposa, de las
// piedras y del terremoto, la tabla de sprites del frame (0x5491e0) de la mariposa, la de los efectos (0x518cf0) y
// la luz dinámica (0x7b4ccc4) del chorro 0x5de, de las piedras 0x118/0x119 y de las grietas del terremoto,
// timeGetTime() (0x6b8544) del calco 0x14e de los Fenrir, y del terremoto 0xfa y sus grietas, los atributos de las
// celdas (0x7d0d348), el mapa (0x6c9dd0), el temblor de la cámara (0x7d325c8) y el sonido (0x41ed10(id, 0, 0)). Sin
// ellos, el caso corta en su dirección.
struct ModelEffectStepServices
{
    std::function<double(float, float)> terrainHeight{};
    FrameSprites* sprites = nullptr;
    ModelEffects* effects = nullptr;
    Terrain::TerrainLighting* lighting = nullptr;
    std::function<std::uint32_t()> systemTime{};
    const Data::Terrain::TerrainAttributes* terrainAttributes = nullptr;
    const std::int32_t* map = nullptr;
    float* cameraShake = nullptr;
    std::function<void(Audio::SoundId)> sound{};
    std::function<Core::Expected<void, std::string>(const FishEffectRequest&)> joint{};
    // 0x52cc21: el paso del pulso 0x103 publica su acción en la tabla compartida antes de animar.
    std::function<void(std::uint16_t, std::uint8_t)> publishAction{};
    // 0x51ed6a y 0x52b349 piden sonido para el destello 0x60d en su posición.
    std::function<void(Audio::SoundId, const glm::vec3&)> soundAt{};
    // 0x52ae2a: el héroe publicado ([0x743a170]), que el chorro 0x5de/0 de la Flame compara con su dueño (+0x108), y
    // 0x517a20(+0x89, la posición, `radius`, el byte +0x8c, +0x8a, 0) sobre ese chorro (Scenes/SkillHits, que lee el
    // pool de personajes y el registro propio y manda el C1 10; CL9 hito 4). Sin `skillHits`, el chorro del héroe corta
    // ahí.
    const void* hero = nullptr;
    std::function<Core::Expected<void, std::string>(const ModelEffect&, float)> skillHits{};
    // CL11 (World/BlastModelEffects): el registro propio (0x7681eb4) que abre y cierra el alta del 0xf4 que suelta el
    // paso del 0xf3: su profundidad (el cierre en 0 gasta 3.848 rand()) y su cuenta +0xf04.
    std::uint8_t* ownRecordDepth = nullptr;
    std::uint8_t* ownRecordCount = nullptr;
};
// 0x52bbf6..0x52be6d: las tres emisiones del brillo 0x621 de cualquier subtipo (el /0 desde aquí, los demás desde
// Scenes/HighModelEffects): dos rand() % 360 de los ángulos y, fuera del /1 y el /2, el sprite 0x5fd con dos rand() más
// (escala · 0,3f) y la chispa 0x5c6/2 con otro (0x52be64). El /1 y el /2, con la vida par, el joint 0x614/3 del punto a
// la posición (0x52bcc4, 0x52bd80) y, el /1 con la vida impar, la chispa (0x52bd02); después los dos, el sprite con
// escala · 0,2f. Sin la tabla de sprites o de efectos, o sin el servicio de joints cuando lo pide, corta.
[[nodiscard]] Core::Expected<void, std::string> EmitPiercingShotGlow(ModelEffect& effect, Particles& particles,
                                                                     Crt::CrtRandom& random,
                                                                     const ModelEffectStepServices& services);
// 0x5208c5..0x52091a: la luz local del paso, del primer rand(): ((r & 3) + 7) · 0,1f (0x6b87b8); con menos de 5 de
// vida, menos (5 − vida) · 0,2f (0x6b8854), guardada en float y comparada sin redondear con 0.
[[nodiscard]] float StepLuminosity(int draw, std::int32_t life);

// 0x5208a0: CRT común, llegada/teleport, torre de Noria, 9/0 de Kalima con el owner de ondas, la mariposa
// 0xae (sus rand() del vuelo, la luz del subtipo, las chispas 5c5/7 del /3 y el sprite 0x5ac) y los del
// Dungeon y Lost Tower: la piedra 0xda que rebota (0x52444d), el chorro 0x5de/0 (0x52ad21: seis llamas
// 5de/0, a veces una piedrita y su luz, con la luz local del primer rand(); el de la Flame, con el héroe de dueño y la
// vida múltiplo de 20, manda los blancos a 150 por `skillHits`, 0x52ae2a) y las piedritas 0xc8/0xc9 con el
// subtipo 0 (0x5238be: rebotan, se inclinan y a veces sueltan un fuego 5d9 de subtipo 1 a 3), 1 (0x523985: suben
// sin leer el terreno) y 10 (como el 0, sin el fuego ni su rand()). Dejan en previousPosition la posición de antes del
// paso entero. Las piedras
// 0x118/0x119 del pisotón del caballo (0x5275ca), en sus pasos 33 a 36, sueltan el polvo 5f2/11 y una piedrita
// 0xc8/0xc9 con el subtipo 10 (de la tabla de efectos), con menos de 8 de vida se apagan, y siempre prenden la luz
// dinámica de radio 2. El 0xb3 de Aida (0x520b77) vaga con sus
// dos giros sorteados y el seno del reloj, y suelta una 5ac/9 por paso. El 0x135 del 30 (0x52973e) no
// muere y, con dueño, sube y baja con el seno del reloj. El calco 0x61d/3 del 0x38 de Crywolf (0x52b933), con la
// vida entre 6 y 9, suelta una órbita 60f/10; el /0 deja el /1 en cada paso y, con el /2 y el /7, suelta la 60f/0
// (0x52b8ff, 0x52b977): sin la tabla de efectos corta sólo si la órbita tiene lugar.
// La onda 0x6eb/1 del galope del caballo en Icarus (0x52c5ca) crece y tiembla con
// tres rand() (la /2, igual de a 0,025f); la /0 del pisotón se achica 1 por paso, sin rand(); la /3 y la /4 crecen 2 y
// 0,3f; los subtipos5..8 del asedio conservan sus escalas y RGB: el6 emite hijos5 cada8vidas, que el pool ascendente
// puede avanzar en el mismo paso; otro subtipo no toca la escala. El rayo 0x14d de los Fenrir (0x5295a4) se prende de a
// 0,3f con el subtipo 0 y se apaga con el 1, y gira 0,15f por eje; el calco 0x14e (0x52965a) gira 0,1f en x, se apaga
// de a 0,05f (la luz según el subtipo) y cambia de cuadro cada 200 ms de timeGetTime(), hasta el quinto. El terremoto
// 0xfa (0x521c5c): en la vida 11 corre la partida detrás de su dueño, la pone sobre el terreno (en Kalima, con la onda
// 0x437860), suelta la explosión 5ed/0 y, con un dueño que no es del tipo 0x1b1, las grietas 0xfd, 0xfb y 0xfc, y cinco
// pares 0xfe/0xff donde la celda lo deja; en la 10, cuatro anillos de cinco pares 0x101/0x102, el sonido 0x97 y la vida
// en 0; en las demás, se hamaca con +0x28. Con el estado 0 (la habilidad del Dark Lord), en la vida 13 emite dos grupos
// de cuatro estelas 0x104 y el sonido 0x96 antes de la hamaca; en la 11 emite ocho joints 0x617/0, una chispa 5c5/0 y
// el pulso 0x103. Salta la animación y 0x51fc80 de la cola (0x52cbb0). Las grietas 0xfb..0x102 (0x52276a..0x522bd2) se
// apagan y se hunden; la 0xfb tiembla la cámara, las 0xfc y 0xff sueltan a veces piedritas 0xc8/0xc9 y, con la 0x102,
// prenden la luz dinámica según el tipo de su dueño. Las piedras 0x10a y 0x10b de las estatuas de Blood Castle
// (0x52531d) saltan, caen sobre el terreno y se inclinan; uno de cada diez pasos corta en 0x5252e6, donde el polvo
// 5f3/0 va en un local sin escribir (Q-C1176).
[[nodiscard]] Core::Expected<void, std::string>
StepGameModelEffect(ModelEffect& effect, const Models::Skeleton* skeleton, Particles& particles, float worldClock,
                    Crt::CrtRandom& random, KalimaWorld* kalima = nullptr,
                    const ModelEffectStepServices& services = {});
} // namespace Client::World
