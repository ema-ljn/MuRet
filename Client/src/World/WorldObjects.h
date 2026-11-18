#pragma once

#include "Core/Expected.h"
#include "Data/Terrain/TerrainObjects.h"
#include "Render/Models/ObjectState.h"
#include "Render/Models/Pose.h"
#include "Render/Models/Skeleton.h"
#include "World/WorldObjectRegistrations.h"

#include <glm/ext/vector_float3.hpp>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace Client::Render::Models
{
class CrtRandom;
} // namespace Client::Render::Models

// Los objetos de un mundo (CL7.2): los del .obj como los arma 0x606660, su paso de 0x604ab0 y lo que
// 0x6002e0 hace con cada uno. Contrato: 06 § El mundo del login; vectores del código real en
// Client/tests/vectors/world55/objects.txt (scripts/login/make_world55_vectors.py).
namespace Client::World
{
// La escena en que se crea: los casos del farol y del dueño del dragón de 0x606660 solo corren en el
// login (2) y la selección (4). Los casos por mapa no dependen de la escena.
enum class ObjectScene : std::uint8_t
{
    Login,
    CharacterSelect,
    Other,
};

// Proyección tipada de los campos usados de 0x1d4 bytes; no reproduce el layout i386.
struct WorldObject
{
    WorldObjectIdentity identity = 0;          // ID del caller; 0 en la proyección sin registro
    std::int16_t type = 0;                     // +0x2, el modelo
    Render::Models::ObjectTransform transform; // +0xc la escala, +0x10 la posición, +0x1c los ángulos
    Render::Models::ObjectState state;         // +0x64, +0x68, +0x6c, +0x70 y +0x58
    std::int32_t field04 = 0;                  // +0x4, en 0 de 0x606660; lo miran las puertas del 30
    std::uint8_t field08 = 0;                  // +0x8, byte escrito por creación del mapa 30
    float field84 = 0.0f;                      // +0x84, store FP del mapa 30/tipo 41
    std::uint8_t field91 = 0;                  // +0x91, byte escrito por creación del mapa 30
    // +0x8a: 4 cuando 0x5ff960 abre el 9 o el 10 de 11..17; 1 en el 3 de 18..23 que 0x4055b0 elige.
    std::int16_t field8a = 0;
    std::int32_t field60 = 0;     // +0x60, la cuenta del 3 de 18..23 (0x4055b0 la pone en 10, 0x4059d0 la baja)
    glm::vec3 light{};            // +0xf4..+0xfc, la que se suma a la del terreno
    float speed = 0.0f;           // +0xd8, keys por paso
    float cullingMargin = -30.0f; // +0xdc, margen inicial de 0x6067ae
    float fieldE0 = 50.0f;        // +0xe0, significado/consumidor sin modelar
    glm::vec3 velocity{};         // +0xcc..d4, cero nativo606830..60683c
    glm::vec3 copiedAngles{};     // +0x28..+0x30, copia del caso del mapa 2
    glm::vec3 copiedPosition{};   // +0x34..+0x3c, copia del caso del mapa 2
    glm::vec3 basePosition{};     // +0x17c, segunda copia inicial de la posición
    glm::vec3 boundsMinimum{-40.0f, -40.0f, 0.0f}; // +0x124..+0x12c
    glm::vec3 boundsMaximum{40.0f, 40.0f, 80.0f};  // +0x130..+0x138
    bool alive = true;                             // +0x0
    float alpha = 1.0f;                            // +0x174
    float targetAlpha = 1.0f;                      // +0x170
    bool vertexLight = true;                       // +0xe8
    Render::Models::AnimationState animation;      // +0x111 la acción, +0x114 el frame
    // +0x16c: se vio en el último dibujo; 0x606560 solo avanza los que tienen esto.
    bool seen = false;
    // +8 de su sector (0x7d325d4 + 0x10 · (x · 16 + y), .bss 0): el sector pasó el trapecio del último dibujo
    // (0x60470b). 0x606560 hace el derrumbe (0x5ff960) con los vivos de un sector que no pasó y apaga su +0x16c.
    bool sectorPassed = false;
    // +0x1c8: el sector de 16 × 16 celdas donde lo guardó 0x606660, de la posición del archivo.
    int sectorX = 0;
    int sectorY = 0;
};

class IWorldObjectCreationServices;

// Bits de imagen de 0x6b88d4: default del port con almacenamiento estático, no una garantía de
// inmutabilidad del dato original durante ejecución.
inline constexpr float InitialWorldObjectRandomFactor = std::bit_cast<float>(0x3C23D70Au);

struct WorldObjectCreationContext
{
    // Prestado y leído después del servicio de escena 180, como el global nativo en 0x606985.
    std::int32_t map;
    ObjectScene scene;
    WorldObjectRegistrations& registrations;
    // El binding y el servicio permanecen vivos y estables durante todo el batch; sus datos
    // prestados sí pueden cambiar. Se configura entre creaciones, no entre emisiones.
    IWorldObjectCreationServices* services = nullptr;
    Render::Models::CrtRandom* random = nullptr; // El mismo estado CRT que usan los efectos y el mundo.
    // Byte vivo de 0x7239ec, con almacenamiento/valor/sincronización del caller. nullptr no fabrica
    // un valor inicial: conserva el gate hasta que el productor real preste este estado durable.
    const std::uint8_t* state7239Ec = nullptr;
    // Factor vivo de 0x6b88d4, leído después del RNG. El caller conserva este almacenamiento hasta
    // que termine la creación y sincroniza sus cambios; un alias mutable puede cambiarlo entre
    // llamadas o desde un emisor anterior. El resultado no retiene la referencia.
    // Dominio portable: factor finito y producto representable en float; binary64/PC53-RN y un
    // store binary32. No se acreditan control word, traps o máscaras x87 arbitrarios.
    // Préstamo no rvalue: no extiende ni administra la vida del almacenamiento del caller.
    const std::reference_wrapper<const float> randomPhaseFactor = std::cref(InitialWorldObjectRandomFactor);
};

enum class WorldObjectCreationError : std::uint8_t
{
    UnsupportedEffect,
    UnsupportedMapHandler,
    InvalidAngle, // ftol con ±infinito o fuera de [-2^63,2^63), sin observar; −2^63 y NaN (Q-C186) sí valen.
};

struct WorldObjectCreationFailure
{
    WorldObjectCreationError error;
    std::size_t recordIndex;
};

struct WorldObjectCreationResult
{
    std::vector<WorldObject> objects;
    // Al retornar por valor, failure conserva objetos/registro/efectos previos e incluye el objeto
    // actual parcialmente creado. Sus campos pendientes no son un resultado válido del caso fallido.
    // Es estado provisional de integración, no un resultado del handler original.
    std::optional<WorldObjectCreationFailure> failure;
};

// Árboles0/1: reciprocal binary64, producto y store binary32. El modelo IEEE del port conserva
// ±0/±infinito; no acredita flags, payloads NaN, traps ni un CW x87 universal. No es un gate de escala.
// Constructor común con estado durable y servicios prestados. Se detiene en la primera dependencia
// sin implementar, sin rollback ni fallback silencioso; los records posteriores no se crean.
// Records estables y disjuntos del estado/resultados. El caller conserva identidad única y contador
// con 1<=nextIdentity y records.size()<=UINT64_MAX-nextIdentity, también al restaurar el mundo.
// Garantía de conservación sólo al retornar por valor: reserve puede lanzar antes de consumir IDs;
// el provider debe retornar normalmente, sin lanzar. No se capturan ni traducen excepciones.
[[nodiscard]] WorldObjectCreationResult CreateWorldObjects(std::span<const Data::Terrain::TerrainObject> records,
                                                           WorldObjectCreationContext& context);

// +0x58 = −2: 0x6002e0 no lo dibuja (DAT_077f3b18, el modo editor, nunca está prendido).
inline constexpr std::int16_t HiddenObject = -2;

// El farol del castillo (Object61.bmd, de un hueso) y lo que pide 0x602ed0 en la escena 2 (0x602f21): dos
// sprites de flare01 (0x5ac), de escala 1, aditivos y sin giro, en las traslaciones de los huesos 8 y 9
// del buffer global de la pose (0x68b199c y 0x68b19cc), las del último modelo posado que los tiene
// (Q-C140).
inline constexpr std::int16_t StreetLight = 0x3c;
inline constexpr std::uint16_t StreetLightGlowTexture = 0x5ac;
inline constexpr std::array<std::size_t, 2> StreetLightGlowBones{8, 9};

// La luz de los dos sprites del farol con `random`, el rand() que 0x602ef4 saca por cada objeto: L =
// (random % 30 + 70) · 0,01f (0x6b88d4) guardado en float, y (L; L · 0,5; L · 0,2f), cada producto
// guardado en float (0x6b882c y 0x6b8854).
[[nodiscard]] glm::vec3 StreetLightGlowLight(int random);

// Proyección visual sin registro auxiliar: aplica los casos directos 0/1/2/3/7/8/55 y escenas 2/4.
// Omite 0x606630, las emisiones 180/322 y la cadena default. No equivale al constructor completo.
// Cada record válido crea otro objeto, sin pool ni reutilización; fuera de la grilla no se crea.
// Una conversión no acreditada devuelve failure con el índice y el prefijo parcial: hay que
// comprobarlo antes de consumir objects, también en esta proyección.
[[nodiscard]] WorldObjectCreationResult CreateWorldObjects(std::span<const Data::Terrain::TerrainObject> records,
                                                           std::uint8_t map, ObjectScene scene);

// Proyección visual con el registro PREEXISTENTE del caller. Aplica registros y casos directos,
// pero omite emisiones 180/322 y handlers default: para éstos se usa el contexto con servicios.
// Los records deben ser estables y disjuntos del estado/resultados durante la creación. El dominio
// de tipo es el int16 del Data reader extendido con signo (el callee nativo admite int32).
// El caller conserva registro/contador durante la vida del mundo y aporta nextIdentity libre.
// Precondición portable: 1<=nextIdentity y records.size()<=UINT64_MAX-nextIdentity; no reproduce un
// contador nativo. La tabla no mantiene con vida ni resuelve por sí misma los IDs.
[[nodiscard]] WorldObjectCreationResult CreateWorldObjects(std::span<const Data::Terrain::TerrainObject> records,
                                                           std::uint8_t map, ObjectScene scene,
                                                           WorldObjectRegistrations& registrations);

// 0x604ab0 sobre un objeto que se vio: el alpha hacia su meta (0x4e9980) y, si no quedó por debajo de
// 0,01, la animación a su velocidad
// (0x4ec200, con la acción de `skeleton`, si tiene) y los casos del mapa: en el 0x37, los marcadores
// 0x54, 0x55, 0x57 y 0x59 se ocultan, el remolino 0x56 gira −0,6° (alpha 0,2, luz (1, 0, 0) y todas
// las mallas por el blendMesh) y el 0x5a −0,23° (alpha 1 y luz blanca, también por el blendMesh).
void StepWorldObject(WorldObject& object, std::uint8_t map, const Render::Models::Skeleton* skeleton);

struct TerrainLighting;
struct ObjectModelStepState;
class Particles;
class ModelEffects;

// Lo que 0x604ab0 lee afuera del objeto en el juego (CL7, hito 3).
struct ObjectStepGlobals
{
    // 0x77f36d0: la capa 1 de la celda del héroe, la que escribe MoveHero (0x59e2ba). En la .bss, 0.
    std::int32_t heroCellLayer = 0;
    // 0x5767b88: el reloj del mundo, en ms.
    float worldClock = 0.0f;
    // 0x7b4ccc4 y 0x743a198: los destinos durables del paso; no se fabrican buffers locales.
    TerrainLighting* dynamicLighting = nullptr;
    Particles* particles = nullptr;
    // Las puertas consultan al héroe publicado y el modo editor, antes de mover o sonar.
    const glm::vec3* heroPosition = nullptr;
    bool editorMode = false;
    // 0x41ed10(id, 0, 0): el caller conserva o emite la petición, aun en pasos consecutivos.
    std::function<void(std::int32_t)> sound{};
    // 0x41ed10(id, 0, 1): la misma petición, en bucle (el ambiente del 9, 0x604b9b).
    std::function<void(std::int32_t)> loopedSound{};
    ObjectModelStepState* modelState = nullptr;
    // 0x518cf0: la tabla de los efectos con modelo, donde el 0x34 del 1 suelta la piedra 0xda y el 0x18 del 4
    // el chorro 0x5de; el chorro abre y cierra el registro propio (0x7681eb4) con su cuenta de accesos.
    ModelEffects* effects = nullptr;
    std::uint8_t* ownRecordDepth = nullptr;
    // +0x111 del héroe (0x743a170): la acción que mira la patada de 0x52f550 en el 0x26 y el 0x27 del 4.
    const std::uint8_t* heroAction = nullptr;
    // 0x6b8544 (timeGetTime): la ventana de las mariposas del 31 y su sonido ambiente (0x43ffd8, Q-C304).
    std::function<std::uint32_t()> systemTime{};
    // 0x723c4c: cuándo sonó el ambiente 0x1ef del 31 por última vez, en ms del sistema. En la .bss, 0.
    std::uint32_t* trialsAmbientTime = nullptr;
    // 0x7239ec: el byte del asedio que lee 0x4294b0 en los casos del 30 (0x42c850). Sin él, el 30 corta.
    const bool* siegeActive = nullptr;
};

// 0x604ab0 con un objeto visto en el juego, en los mapas 0 a 4, 6 a 17, 30, 31, 33 y 34 (*estático*, 06 § Los
// objetos del juego; los del 1, del 4 y del 6, World/DungeonObjectStep.h; los del 7, World/AtlansObjectStep.h; los del
// 9 y de 11 a 17, World/EventMapObjectStep.h; los del 8, con
// el tipo 8 animado al cuádruple, World/TarkanObjectStep.h; los del 30, 0x42c850, World/CastleSiegeObjectStep.h; los
// del 31, 0x43ffc0, StepLandOfTrialsObject, y los del 34, 0x4303e0, StepCrywolfObject, en
// World/WorldObjectStepServices.h). Antes del alpha, los techos: el 0x7d y el 0x7e del 0 llevan la meta del alpha
// (+0x170) a 0 con la capa del héroe en 4 (0x604bc4), y el 0x51, el 0x52, el 0x60, el 0x62 y el 0x63 del 2 con la capa
// en 3 o desde 10 (0x604c06); a 1 si no. Después del alpha/animación, los casos del mapa: UV y flicker, lámparas
// 0x5a/0x96 al destino dinámico, fuegos de 0x52f360 al pool durable y puertas del mapa 2 con posición del
// héroe/editor/sonido prestados. Un binding ausente corta donde lo pide el ASM, conservando RNG, partículas y stores
// anteriores. En el 0x37 corre lo de StepWorldObject; los demás mapas cortan antes de tocar el objeto.
[[nodiscard]] Core::Expected<void, std::string> StepWorldObjectChecked(WorldObject& object, std::uint8_t map,
                                                                       const Render::Models::Skeleton* skeleton,
                                                                       Render::Models::CrtRandom& random,
                                                                       const ObjectStepGlobals& globals);

// Lo que 0x5ffdf0 y 0x6002e0 hacen con un objeto en el dibujo.
enum class ObjectDrawKind : std::uint8_t
{
    // Nada: el alpha por debajo de 0,01 o +0x58 en −2.
    Hidden,
    // Un RenderBody(2, alpha, blendMesh, luz del blendMesh, U, V, malla oculta, −1) con el color del
    // modelo.
    Body,
    // El remolino 0x5a del mapa 0x37: 0x4ecc30(0, 0x80, 1, blendMesh, 1, U, V, −1), la malla 0 sola,
    // con GL_ZERO, GL_ONE_MINUS_SRC_COLOR, en blanco y sin luz.
    SubtractiveMesh,
    // El 0x69 del mapa 0 (Waterspout, 0x600a4a): cuatro 0x4ecc30 sueltos en vez de RenderBody, las mallas
    // 0 a 2 con el blendMesh y la luz del objeto y U = V = 0, y la 3 con el blendMesh 3, la luz y la U y
    // la V del objeto.
    Waterspout,
    // El 0x51 del mapa 8 (0x600c81): la malla 0 con 0x4ecc30(0, 2, …, −1) y otra vez con el cromo aditivo
    // de Chrome01, 0x4ecc30(0, 0x44, …, 0x5c0).
    TarkanChrome,
};

struct ObjectDraw
{
    ObjectDrawKind kind = ObjectDrawKind::Hidden;
    // 0x5ffdf0 lo posó en el buffer global de la pose (0x5fff7a): todos menos los de alpha por debajo de
    // 0,01, que sale antes (0x5ffe3d); también los de +0x58 en −2, que 0x6002e0 no dibuja.
    bool posed = false;
    float alpha = 1.0f;
    Render::Models::ObjectState state;
    // +0x48..+0x50 del modelo, el que le pone 0x5ffce0.
    glm::vec3 color{1.0f};
    bool vertexLight = false;
};

// El dibujo de `object` con `terrainLight`, la del terreno en su lugar (0x5f99e0).
[[nodiscard]] ObjectDraw DrawOf(const WorldObject& object, const glm::vec3& terrainLight, std::uint8_t map);

// La fuente de Lorencia (Waterspout01): el único caso del mapa 0 en 0x6002e0 (0x600a4a) y uno de los dos
// de 0x602ed0 (0x603011). Su alta le pone el blendMesh en 3 (06 § Lo propio de Lorencia).
inline constexpr std::int16_t Waterspout = 0x69;

// 0x603011, el caso del 0x69 en 0x602ed0, después del rand() común de 0x602ef4 (*estático*): la luz del
// blendMesh (+0x68) en 1, la V (+0x70) con el reloj del mundo `worldClock` (0x5767b88), −(ftol(T) % 1000) ·
// 0,001f como el 0x76 del paso, y un rand(): impar, nada más. Par, dos gotas 0x5f2 (0x53fe70) en los
// huesos 1 y 4 (0x68b184c y 0x68b18dc), cada una a (rand() % 32 − 16, −20 u −80, rand() % 32 − 16) del
// hueso. MuRet no tiene las partículas del juego (0x743a198): corta en la primera (0x6030c7) después de
// sus dos rand(). Sin un ftol acreditado del reloj, corta en 0x60301e con la luz ya en 1, sin tocar la V
// ni sacar rand().
[[nodiscard]] Core::Expected<void, std::string>
StepTheWaterspoutFrame(WorldObject& object, Render::Models::CrtRandom& random, float worldClock);

// El sector de 16 × 16 celdas de un objeto, de 0 a 15: el que le dio 0x606660 con la posición del
// archivo por 0,000625f, el producto sin redondear (la x87) y el dword bajo de ftol. Fuera de la grilla,
// 0x606660 no lo crea; con un NaN, el sector es 0 (Q-C186).
[[nodiscard]] int SectorX(const WorldObject& object);
[[nodiscard]] int SectorY(const WorldObject& object);
// El sector de una coordenada, como lo saca 0x606660 (de 0 a 15, o −1): para un objeto que no viene de
// un .obj, como el logo.
[[nodiscard]] int SectorOf(float coordinate);

// El orden del dibujo de 0x6046a0: los sectores por x y después por y (0x7d325d0 + (x · 16 + y) ·
// 0x10) y, en cada uno, el orden en que se crearon. Devuelve los índices de `objects`.
[[nodiscard]] std::vector<std::size_t> DrawOrder(std::span<const WorldObject> objects);
} // namespace Client::World
