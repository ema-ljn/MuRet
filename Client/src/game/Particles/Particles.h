#pragma once

#include "Core/Expected.h"
#include "Particles/ParticleOwner.h"
#include "Audio/SoundId.h"

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace Client::Crt
{
class CrtRandom;
} // namespace Client::Crt

namespace Client::Terrain
{
struct TerrainLighting;
} // namespace Client::Terrain

namespace Client::WorldObjects
{
struct WorldObject;
} // namespace Client::WorldObjects

// Las partículas (CL7.6 B2): la tabla de 0x53fe70 (0x743a198, 3.000 lugares de 0x78 bytes) y su paso,
// 0x5446e0, con el viento de 0x7609bb8. Su dibujo, 0x548550, tiene un solo contrato: el de
// World/ParticleDrawing.h, que sigue la tabla de saltos del ASM y no este subconjunto de creación. De sus
// tipos, MuRet arma el aliento del caballo del login (0x5f2, Effect\smoke01.jpg, subtipo 0) y la chispa
// del cuervo (0x5c6, Effect\Spark03.jpg, subtipo 5, CL7.6 C), y la neblina del login (0x622,
// Effect\clouds.jpg, subtipo 10). Contrato: 06 § Los efectos del login. Aliento y chispa se comparan
// bit a bit con vectores del original; la neblina tiene tests derivados de su ASM. El mundo agrega
// 5d9/0..3, 5f2/2, 5c6/9, 607/0, 67/0 y 622/8 según sus contratos específicos. Los cálculos
// conservan stores binary32 e intermedios double (53 bits); no emulan status/excepciones x87 ni
// prometen equivalencia para cualquier control word o plataforma XP.
namespace Client::Particles
{
class FrameSprites;
class IParticleTrigonometry;

inline constexpr std::size_t ParticleCount = 3000;
// 0x52a866/0x52a89a: las dos luces que siguen los huesos del blanco de Swell Life.
inline constexpr std::int32_t SwellLifeParticle = 0x5ac;
inline constexpr std::int32_t SwellLifeParticleSubtype = 4;
// 540260 y542599: las chispas5ad/2 y la niebla5f2/27 de la mano de Crywolf. La 5ad/2, la /3 y la /4 (con la textura
// 0x5fd) son también las del pie y del blanco del 0x1b6 (0x42769f..0x427a4d).
inline constexpr std::int32_t MagicSparkParticle = 0x5ad;
inline constexpr std::int32_t CrywolfTideSmokeSubtype = 25;
inline constexpr std::int32_t CrywolfSwingSmokeSubtype = 27;
inline constexpr std::int32_t MissileFireParticle = 0x5da;
inline constexpr std::int32_t DeathSpiritFireParticle = 0x5db;
inline constexpr std::int32_t MissileSmokeSubtype = 38;
inline constexpr std::int32_t ExplosionSmokeSubtype = 35;
// La 5f2/34 del joint 0x610/0xe de la flecha 0xf9 (0x537f39): nace como la /35 pero quieta (0x541a69 → 0x5435bb) y
// comparte con ella el paso (0x5446e0, el caso 0x22..0x23) y el dibujo.
inline constexpr std::int32_t ViperSmokeSubtype = 34;
inline constexpr std::int32_t FireworkParticle = 0x60d;
inline constexpr std::int32_t PoundingBallParticle = 0x640;
inline constexpr std::int32_t ShinyParticle = 0x5fc;
inline constexpr std::int32_t ThunderParticle = 0x5ca;
// 0x5410d9: la /1 (la de 0x53fd42) gris que se apaga con la vida; la /2 (la de 0x51df52) con la textura 0x61e.
inline constexpr std::int32_t ThunderGraySubtype = 1;
inline constexpr std::int32_t ThunderBoltSubtype = 2;
inline constexpr std::int32_t FairySparkParticle = 0x5c5;
inline constexpr std::int32_t FireParticle = 0x5d9;
// 0x4fd580/0x43ebb7: la llama azul de la estela de Kalima, con su textura Effect\Fire05.jpg (0x61ddac).
inline constexpr std::int32_t BlueTrailFireParticle = 0x5dc;
inline constexpr std::int32_t BlueTrailFireSubtype = 12;
// Los casos del fuego (5d9, 5db y 5dc, 0x541172 y 0x5457a5) que tienen nombre: la /7 y la /11 se mecen de costado, la
// /9 sigue a su dueño (+0x40) desde una partida en anillo, la /12 es la alta de la estela y la /13 sube recta.
inline constexpr std::int32_t FireSwaySubtype = 7;
inline constexpr std::int32_t FireRingSubtype = 9;
inline constexpr std::int32_t FireLongSwaySubtype = 11;
inline constexpr std::int32_t FireTallSubtype = BlueTrailFireSubtype;
inline constexpr std::int32_t FireFountainSubtype = 13;
inline constexpr std::int32_t DropParticle = 0x607;
inline constexpr std::int32_t WorldSmokeSubtype = 2;
inline constexpr std::uint16_t BreathParticle = 0x5f2;
// La chispa del cuervo (Effect\Spark03.jpg), con el subtipo 5 (06 § El cuervo).
inline constexpr std::uint16_t SparkParticle = 0x5c6;
inline constexpr std::int32_t PiercingSparkSubtype = 2;
// 0x42ddb0: bendición sobre un personaje cerca de la corona/marca del asedio.
inline constexpr std::int32_t SiegeBlessingParticle = 0x638;
inline constexpr std::int32_t TeleportSparkSubtype = 1;
inline constexpr std::int32_t RavenSparkSubtype = 5;
inline constexpr std::int32_t MapSparkSubtype = 9;
// Las veinte chispas del 0x815 en la izquierda del registro propio, que pide el paso del ataque (0x4fb0d1): nacen en
// 0x540a04 con la textura 0x5fc, seis rand() y +0x38 en 1,0f, y su paso (0x545397) saca un rand() para la luz.
inline constexpr std::int32_t BowSparkSubtype = 12;
// El rayo 0x5d5 (Effect\lightning2.jpg) que deja el joint 0x618/0 o /1 del que mató (0x53867e, la /0 en cada paso, y
// 0x538577, la /1 al llegar). Nacen sin rand() (0x540bbe y 0x540bfe) y su paso (0x545419) gira con el reloj y pide la
// luz dinámica; 548550 los dibuja con el billboard común.
inline constexpr std::int32_t LightningParticle = 0x5d5;
inline constexpr std::int32_t LightningTrailSubtype = 0;
inline constexpr std::int32_t LightningHitSubtype = 1;
// Los demás casos de 0x540bb2: la /2 (vida 20, la del 0x4c965c) sigue en su paso a su dueño (+0x40, un personaje),
// la /3 vive 1 paso con la escala del pedido, y la /4 y la /5 conservan la luz del pedido.
inline constexpr std::int32_t LightningFollowSubtype = 2;
inline constexpr std::int32_t LightningFlashSubtype = 3;
inline constexpr std::int32_t LightningPlainSubtype = 4;
inline constexpr std::int32_t LightningPlainScaledSubtype = 5;
inline constexpr std::uint16_t CloudParticle = 0x622;
inline constexpr std::int32_t LoginCloudSubtype = 10;
// Los dos tipos que comparten creación y paso; la 68 además corre la posición al nacer.
inline constexpr std::int32_t Particle67 = 0x67;
inline constexpr std::int32_t Particle68 = 0x68;
// La órbita (0x60f/3): nace con 60 de vida, y 548550 no la dibuja mientras la conserve, salvo la /4 (la de la
// antorcha de Blood Castle, que nace con 40). La /0 (la del conjunto completo, 0x510339) nace y gira igual que la
// /3; con su dueño (el personaje del conjunto, o el calco 0x61d de 0x52b8ff y 0x52b977) en ciertas acciones pasa a
// ser la /1, que sólo se apaga (0x54735c).
inline constexpr std::int32_t OrbitParticle = 0x60f;
inline constexpr std::int32_t OrbitLife = 60;
// Las flores 608, 609 y 60a (Skill\flower1..3.tga) de la flecha 0xe1 (0x526a8b): nacen en 0x542c4b sin leer el
// subtipo y su paso (0x546da6) las deja caer, la /0 y la /1 con su deriva, hasta el terreno (0x5f93c0), girando.
inline constexpr std::int32_t FirstFlowerParticle = 0x608;
inline constexpr std::int32_t LastFlowerParticle = 0x60a;
// La columna 62b/0 (Effect\flareBlue.jpg) del círculo 0xcc/3 y /4 (0x523677 y 0x52384c; la tabla 0x52cef4 lleva el
// 0xcc a 0x523374): nace en 0x542d01 y sube cada vez más rápido (0x546f0d). Los demás subtipos salen de 0x53fe70 en
// 0x54304c con sólo el prefijo y no tienen paso propio (0x546f12).
inline constexpr std::int32_t FlareColumnParticle = 0x62b;
// El agujero 6ae (Effect\Hole.jpg) de la puerta 0xc5 de Kalima (0x43c20d): nace en 0x543bf1 y gira, se achica y se
// apaga con la vida (0x547d42), sin leer el subtipo.
inline constexpr std::int32_t HoleParticle = 0x6ae;
// Las de los objetos y el agua de Crywolf (el mapa 34): el humo 5f2/21 (0x542370, que pasa por el paso de la 5f2/2,
// 0x5466c3), la nube 622/9 (0x54330a, con el paso de la 622/8, 0x5476a8), la órbita 60f/10 del efecto 0x61d/3
// (0x542ed0, que se queda en su partida, 0x547297) y la burbuja 698/0 (Effect\coll.jpg) del agua (0x543152 y
// 0x54719f).
inline constexpr std::int32_t CrywolfSmokeSubtype = 21;
// El humo 5f2/33 del 0x48 de Crywolf (0x430932): nace en 0x541a8d con la vida y los ángulos de cuatro rand() y su paso
// (0x545ffc) frena la velocidad y lo sube con la escala, que crece.
inline constexpr std::int32_t CrywolfRisingSmokeSubtype = 33;
// El polvo 5f2/11 de las piedras 0x118/0x119 del pisotón del caballo oscuro (0x527631): nace en 0x541d81 corrido con
// tres rand(), inclinado y girado, y con la luz de la creación guardada en +0x60..+0x68; su paso (0x546111) la apaga
// con la vida, lo frena, lo agranda y lo baja.
inline constexpr std::int32_t StoneDustSubtype = 11;
inline constexpr std::int32_t CrywolfCloudSubtype = 9;
inline constexpr std::int32_t AnchoredOrbitSubtype = 10;
inline constexpr std::int32_t BubbleParticle = 0x698;
// El polvo 5f3/0 (Effect\smoke02.tga) del galope del caballo oscuro fuera de los mapas 2 y 10 (0x4444dd): nace en
// 0x54271f y su paso (0x546b66) lo deja sobre el terreno con el alto de su textura.
inline constexpr std::int32_t DustParticle = 0x5f3;
// El polvo 5f6/1 del recorrido de Crywolf (0x403c0c): comparte el paso de 5f3, pero nace con vida40, velocidad0
// y seis rand() en 0x54271f..0x542897. El alta no separa los dos tipos: la 5f3/1 del freno de +0x3e5 (0x506a73) nace
// como la 5f6/1, y la 5f6/0 de las piedras de Chaos Castle (0x405630 y 0x405e00), como la 5f3/0.
inline constexpr std::int32_t ArrivalDustParticle = 0x5f6;
// La explosión 5ed/0 (Effect\Explotion01.jpg, cuatro por cuatro cuadros) que suelta el terremoto 0xfa en su vida 11
// (0x521dfd): nace en 0x542a60 con vida 20 y su paso (0x5459a4) avanza el cuadro (+0x48) con la vida y prende la luz
// dinámica. El alta termina con el sonido 0x41ed10(0x40, 0, 0), que Particles no hace: lo hace quien la crea.
inline constexpr std::int32_t ExplosionParticle = 0x5ed;
// La llamarada 5ee (Effect\DinoE.jpg, cuatro cuadros en una fila) del 0x6ac (0x51ffd5) y del joint 0x632/6
// (0x53bf6e): nace en 0x542a94 con vida 12 y la escala del pedido, de cualquier subtipo y sin rand(), su paso
// (0x545ce3) pone el cuadro (+0x48) en (12 − vida) / 3 y 0x5486b6 la dibuja como el fuego (un cuarto del ancho, el
// cuadro % 4 y el giro de +0x20). El alta termina con el sonido 0x41ed10(0x40, 0, 0): lo hace quien la crea.
inline constexpr std::int32_t BlazeParticle = 0x5ee;
// La brasa 6ad (Effect\PoundingBall.jpg) del fuego que rebota 0xc2/5 (la /0, 0x524f05) y de la jabalina 0x139 (la
// /1, 0x529c69): nace en 0x543939 con vida 24 o 15, tres rand() y la velocidad hacia −y girada por sus ángulos; su paso
// (0x5478d7) la sube cada vez más rápido, la achica y, la /1, la apaga con la vida. 0x5488f6 la dibuja con el billboard
// común.
inline constexpr std::int32_t CinderParticle = 0x6ad;
// La onda 0x6eb (Effect\ShockWave.jpg) como partícula: la 0x6eb/3 del brazo izquierdo del 0x1b7 (0x427fad). Nace en
// 0x5443ae con vida 7, sin rand(); su paso (0x5481ab) apaga la luz por 0,5555556f y crece 0,3f, y 0x5488f6 la dibuja
// con el billboard común. No lee el subtipo.
inline constexpr std::int32_t ShockWaveParticle = 0x6eb;
// La sangre 5e4 (Effect\blood.tga, 0x61d7c9) que suelta el golpe con daño del paso del ataque (0x50419c, diez sobre el
// blanco): nace en 0x542b3b con cuatro rand() y la velocidad girada por los ángulos del que pega, su paso (0x546b04)
// la frena y avanza el cuadro (+0x48), y 548550 la dibuja con los cuadros de dos por dos (0x5487b9). No lee el subtipo.
// La 5e3 comparte su creación (sin la luz) y su paso, pero nadie la pide en MuRet.
inline constexpr std::int32_t BloodParticle = 0x5e4;
// El brillo 5fe (Effect\Shiny03.jpg, 0x61d937) de las skills de las armas (0x55be23, sobre el héroe): nace en 0x542bff
// con vida 18 y sin rand(), y 0x53fdb0 la pone en el hueso de su dueño (+0x40); su paso (0x546d54) le da la escala
// sin(vida · π/18) · 3 y la vuelve a poner en el hueso. 548550 la dibuja con el billboard común (0x5488f6 → 0x54889a).
// No lee el subtipo; 0x53fdb0 sí.
inline constexpr std::int32_t SkillShineParticle = 0x5fe;
// El brillo de la mano 5fd (Effect\Shiny02.jpg) de la skill que pega a varios (0x53d810, del 1A): la /0 y la /2 nacen
// en 0x542b0e con vida 36 y el ángulo x en 45, y 0x53fdb0 las pone en la mano de su dueño, un personaje (lo hace quien
// las crea: Effects::RaiseTheHandShine). Su paso (0x546cd4) late la escala con el seno de la vida, gira la /2 y las
// vuelve a poner en la mano; 548550 las dibuja con el billboard común (0x5488f6 → 0x54889a).
inline constexpr std::int32_t HandShineParticle = 0x5fd;
inline constexpr std::int32_t HandShineSecondSubtype = 2;

// Offsets nativos de procedencia; este DTO no promete layout binario de 0x78 bytes.
struct Particle
{
    bool alive = false;          // +0x0
    std::int32_t type = 0;       // +0x4
    std::int32_t texture = 0;    // +0x8, la textura con que se dibuja (0x53fe70 le copia el tipo)
    std::int32_t subtype = 0;    // +0xc
    float scale = 0.0f;          // +0x10
    glm::vec3 position{};        // +0x14
    glm::vec3 angles{};          // +0x20, en grados: los de la velocidad
    glm::vec3 light{};           // +0x2c
    std::uint32_t field38 = 0;   // +0x38: retenido por el prefijo; 622/8 lo inicializa
    std::int32_t life = 0;       // +0x3c, pasos que le quedan
    std::uint32_t owner = 0;     // +0x40, el dueño (un puntero en el original)
    float rotation = 0.0f;       // +0x44, la del billboard, en grados
    float field48 = 0.0f;        // +0x48: frame raw32 por bit_cast para 5d9 y 607; sin garantía ABI x87 universal
    bool moves = false;          // +0x4c: el paso le suma la velocidad girada por los ángulos
    float verticalSpeed = 0.0f;  // +0x50
    glm::vec3 velocity{};        // +0x54
    float rotationFactor = 0.0f; // +0x60: 622/10 retiene al crear y escribe 1,5 al avanzar; 622/8 inicializa; 5f2/11
                                 // guarda el rojo de su luz de creación
    glm::vec2 field64{};         // +0x64..68: retenidos al crear; 5f2/11 guarda el verde y el azul de su luz
    glm::vec3 start{};           // +0x6c, donde nació
    // En las familias Kalima esta identidad es el owner; el DWORD owner anterior sólo conserva la API legacy.
    ParticleOwner typedOwner{};
};

// El viento de las partículas: 0x7609bb8 (x, y), entre −1,7 y 1,7, y lo que cambia en cada paso,
// 0x7609bc4, entre −0,6 y 0,6.
struct ParticleWind
{
    glm::vec2 wind{};
    glm::vec2 change{};
};

enum class ParticleError : std::uint8_t
{
    UnsupportedType,
    UnsupportedSubtype,
    MissingTextureSize,
    MissingMaterialFlag,
    MissingMap,
    MissingColorFlag,
    MissingDrawingService,
    UnrepresentableTrigonometry,
    MissingTerrainHeight,
    MissingClock,
    MissingScene,
    // 0x5476f0: el dueño (+0x40) de una 622/1 no está entre los objetos del mapa; 0x54735c: el de una 60f/0 no
    // responde (ni un personaje del pool por ParticleActorActions ni un lugar de la tabla de efectos, el calco 0x61d,
    // por ParticleOwnerStates).
    MissingOwner,
    // 0x545cd6: la 5ed pide la luz dinámica (0x7b4ccc4) y no está, o su centro no cabe en el int32 de 0x5f9780.
    MissingDynamicLight,
    // 0x546c96: la 5fc/2 deja en cada paso un sprite en la tabla del frame (0x5491e0) y no está.
    MissingSprites,
};

// 5f93c0: retorno retenido, consultado después del prefijo del slot5c5.
using ParticleTerrainHeight = std::function<double(float, float)>;
// El alto de la textura de un lugar de 0x7d3a198 (+0x24, el lado del buffer), o nullopt si no se cargó: lo lee el
// paso de la 5f3 (0x546be9).
using ParticleTextureHeight = std::function<std::optional<float>(std::int32_t)>;
// El objeto del mundo de una identidad (ParticleOwner::identity): lo que el original lee por el puntero
// +0x40. nullptr si no está.
using ParticleObjectOwners = std::function<WorldObjects::WorldObject*(std::uint64_t)>;
// La acción (+0x111) del personaje de una identidad (ParticleOwnerKind::Actor, el lugar del pool): lo que el paso de
// la órbita 60f/0 lee por el puntero +0x40 (0x54735f). nullopt si no está. Con un efecto con modelo de dueño, el paso
// la lee de ParticleOwnerStates.
using ParticleActorActions = std::function<std::optional<std::uint8_t>(std::uint64_t)>;
// 0x53fdb0(partícula): el punto en el mundo del hueso que sigue la 5fe, el de su dueño (+0x40, ParticleOwner del pool
// de personajes) que elige el héroe publicado con el subtipo. También resuelve la 5ac/4 por rotation y pose propia,
// sin escala del modelo. nullopt si el dueño no está o no se resuelve; la 5ac/4 se apaga y termina sus dos rand().
// Para 5c6/2 resuelve el centro del lugar físico ModelEffect cuando está vivo.
using ParticleOwnerBones = std::function<std::optional<glm::vec3>(const Particle&)>;
// 0x53fdb0 en el paso de la 5fd (0x546d37 y 0x546d47): el punto de la mano del personaje dueño
// (ParticleOwnerKind::Actor, el lugar del pool) para el subtipo. nullopt si no está.
using ParticleActorHands = std::function<std::optional<glm::vec3>(std::uint64_t, std::int32_t)>;

// Lo que el paso lee por el puntero +0x40 de las partículas que siguen a su dueño (5c6/4, 5d5/2, el fuego /9 y
// 5de/4): el byte vivo (+0x0), la posición (+0x10) y, sólo de un efecto con modelo, su partida (+0x17c). La acción
// (+0x111): de un personaje la lee la fanta 6ec/1 y /2 (0x547a54) y la órbita 60f/0 (0x54735f), que también la lee del
// efecto con modelo 0x61d que la suelta (0x52b8ff, 0x52b977). De un personaje, además, sus huesos con nombre:
// 0x4010b0(dueño, nombre, punto), sin corrimiento, que lee la 6ec/3 y /4 (0x547c1e); nullopt si 0x4016c0 no lo
// encuentra (o el dueño no es un personaje).
struct ParticleOwnerState
{
    bool alive = false;
    glm::vec3 position{};
    std::optional<glm::vec3> start;
    std::uint8_t action = 0;
    std::function<std::optional<glm::vec3>(std::string_view)> namedBone{};
};
// El estado del dueño de una identidad (Particle::typedOwner); nullopt si no está.
using ParticleOwnerStates = std::function<std::optional<ParticleOwnerState>(const ParticleOwner&)>;

class Particles
{
public:
    // 0x53fe70(tipo, posición, ángulos, luz, subtipo, escala, dueño) en el primer lugar libre, con el reloj
    // del mundo (DAT_05767b88, en ms) y el rand() de la CRT. Con los 3.000 llenos no se crea ni saca
    // rand(). Se aceptan, entre otras (la lista entera es la de BindingError más las familias de Kalima, de la
    // selección, de las pruebas y de Blood Castle), la 5ca, la 5e4, las flores 608..60a, la 62b y el 6ae (cualquier
    // subtipo; la 62b fuera de la /0 sólo con el prefijo), 5f2/0,2,11,21,25,27,33,34,35,38, 5f3/0,1, 5f6/0,1, 5ed/0,
    // 5c6/1,5,9,11, 622/6,8,9,10, el fuego 5d9, 5db y 5dc (cualquier subtipo), 607/0..4, 67/0, 68/0, 698/0, 6ab/0,1,
    // 6ac/0,1 y la 6f4/2 del golpe de Kundun (0x52a37d, de Kalima); la órbita 60f/0,3,4,10 pide el overload con la
    // trigonometría. Create es la compatibilidad login; Core usa CreateChecked para distinguir errores
    // de pool lleno. La neblina consume cinco rand() y conserva +0x60 del uso anterior del lugar. 622/8 consume siete
    // rand().
    std::optional<std::size_t> Create(std::int32_t type, const glm::vec3& position, const glm::vec3& angles,
                                      const glm::vec3& light, std::int32_t subtype, float scale, float worldClock,
                                      Crt::CrtRandom& random);
    // Pool lleno es éxito sin índice; un binding no implementado es error explícito.
    [[nodiscard]] Core::Expected<std::optional<std::size_t>, ParticleError>
    CreateChecked(std::int32_t type, const glm::vec3& position, const glm::vec3& angles, const glm::vec3& light,
                  std::int32_t subtype, float scale, float worldClock, Crt::CrtRandom& random, std::uint32_t owner = 0);
    // Reloj prestado: 5f2/4 lo carga después de R2; 6/7/8 no lo leen al crear. `typedOwner` es el dueño (+0x40) de
    // la órbita 60f/0, que su paso lee (0x54735c); las demás lo ignoran.
    [[nodiscard]] Core::Expected<std::optional<std::size_t>, ParticleError>
    CreateChecked(std::int32_t type, const glm::vec3& position, const glm::vec3& angles, const glm::vec3& light,
                  std::int32_t subtype, float scale, std::reference_wrapper<const float> worldClock,
                  Crt::CrtRandom& random, std::uint32_t owner, IParticleTrigonometry& trigonometry,
                  ParticleOwner typedOwner = {});
    // Sólo las familias Kalima acreditadas. Conserva identidad por valor, nunca una dirección host.
    [[nodiscard]] Core::Expected<std::optional<std::size_t>, ParticleError>
    CreateKalimaChecked(std::int32_t type, const glm::vec3& position, const glm::vec3& angles, const glm::vec3& light,
                        std::int32_t subtype, float scale, float worldClock, Crt::CrtRandom& random, std::int32_t scene,
                        ParticleOwner owner = {});
    // CreateKalimaChecked agrega5ac/6,7,6ed/0,6f0..4/0, 6f1/3 y6f4/1; escena prestada para6f1.
    // Las familias con dueño tipado: las de Kalima y las de la selección (SelectionParticleKernels.h:
    // 622/1, 5de/7 y 729/0,1), más 6f0/2, 6f1/2 y 6f2,6f3/4 de los objetos del 54 y del 33, y la 5fd/0 y /2 de la
    // mano (que no lee el reloj ni la escena, ni saca rand()), y la 5ac/4 de Swell Life (0x540112), sin rand(). También
    // las que siguen a su dueño sin leerlo al nacer (5d5/2, el fuego /9 y 5f2/36: FollowsItsOwner).
    [[nodiscard]] Core::Expected<std::optional<std::size_t>, ParticleError>
    CreateOwnedChecked(std::int32_t type, const glm::vec3& position, const glm::vec3& angles, const glm::vec3& light,
                       std::int32_t subtype, float scale, float worldClock, Crt::CrtRandom& random, std::int32_t scene,
                       ParticleOwner owner = {});
    // 0x53fe70 con la 5fe (SkillShineParticle) de las skills: el prefijo común, la vida 18 (0x542c00) y 0x53fdb0 con
    // `bones` y `owner` (el dueño, +0x40), que pisa la posición. Sin rand(). Con los 3.000 llenos, nada; sin el
    // dueño, MissingOwner con el lugar apagado. Otro tipo, UnsupportedType.
    [[nodiscard]] Core::Expected<std::optional<std::size_t>, ParticleError>
    CreateFollowingChecked(std::int32_t type, const glm::vec3& position, const glm::vec3& angles,
                           const glm::vec3& light, std::int32_t subtype, float scale, ParticleOwner owner,
                           const ParticleOwnerBones& bones);
    // 0x53fe70 con las que leen la posición de su dueño al nacer (+0x40 → +0x10): la 5c6/4 (0x540686, la velocidad
    // hacia él), la 5de/4 (0x5417ba, la partida) y la 5c5/5 (0x5403ad, la partida). Las demás la ignoran; el dueño
    // queda en typedOwner para su paso. Sin ella, CreateChecked y CreateOwnedChecked rechazan esas tres con
    // MissingOwner o UnsupportedSubtype antes de tocar el lugar.
    [[nodiscard]] Core::Expected<std::optional<std::size_t>, ParticleError>
    CreateTowardChecked(std::int32_t type, const glm::vec3& position, const glm::vec3& angles, const glm::vec3& light,
                        std::int32_t subtype, float scale, std::reference_wrapper<const float> worldClock,
                        Crt::CrtRandom& random, IParticleTrigonometry& trigonometry, ParticleOwner owner,
                        const glm::vec3& ownerPosition);
    // 0x53fe70 con las que piden la altura del terreno (0x5f93c0) en su punto al nacer: la niebla 5f2/28, /29 y /30 de
    // la tormenta 0xcf/3..7 (0x5425f9, 0x54264d y 0x54269a). La /30 además corre la x y la y con el seno del reloj
    // (0x5426ae, `trigonometry`). Las demás la ignoran. Sin ella, CreateChecked rechaza esas tres con
    // MissingTerrainHeight antes de tocar el lugar.
    [[nodiscard]] Core::Expected<std::optional<std::size_t>, ParticleError>
    CreateOnGroundChecked(std::int32_t type, const glm::vec3& position, const glm::vec3& angles, const glm::vec3& light,
                          std::int32_t subtype, float scale, std::reference_wrapper<const float> worldClock,
                          Crt::CrtRandom& random, IParticleTrigonometry& trigonometry,
                          const ParticleTerrainHeight& terrainHeight);
    // 0x54061d: chispa5c6/2 hacia el centro de su lugar físico621, sinCRT.
    std::optional<std::size_t> CreatePiercingSpark(const glm::vec3& position, float scale, ParticleOwner owner,
                                                   const glm::vec3& center);
    // 0x6070a0, al entrar a la escena: apaga el byte vivo de cada lugar. El viento sigue como estaba.
    void Clear();

    [[nodiscard]] std::span<Particle> All();
    [[nodiscard]] std::span<const Particle> All() const;
    [[nodiscard]] bool Any() const;
    [[nodiscard]] ParticleWind& Wind();
    [[nodiscard]] const ParticleWind& Wind() const;

private:
    [[nodiscard]] Core::Expected<std::optional<std::size_t>, ParticleError>
    CreateImpl(std::int32_t type, const glm::vec3& position, const glm::vec3& angles, const glm::vec3& light,
               std::int32_t subtype, float scale, float legacyClock, const float* worldClock, Crt::CrtRandom& random,
               std::uint32_t owner, IParticleTrigonometry* trigonometry,
               std::optional<std::int32_t> scene = std::nullopt, ParticleOwner typedOwner = {},
               const glm::vec3* ownerPosition = nullptr, const ParticleTerrainHeight* terrainHeight = nullptr);
    // En el heap: la escena que la lleva tiene que caber en la pila.
    std::vector<Particle> m_particles = std::vector<Particle>(ParticleCount);
    ParticleWind m_wind;
};

// 0x5446e0: el viento (dos rand(), uno por eje) y cada partícula viva: la vida baja uno y en 0 se apaga
// (el movimiento común corre igual), con +0x4c se mueve (0x4e98d0), y el caso de su tipo: el del
// aliento, la chispa del cuervo y la neblina de login (0x5475b6: suma otra vez la velocidad sin girar y decide
// crecimiento, fade o muerte por la distancia 3D al origen); 622/8 sólo actualiza +38/Z con vida positiva.
// Compatibilidad login: devuelve un error si aparece un binding nuevo que requiere contexto.
// El caller de mundo usa StepParticlesChecked y examina su resultado.
std::optional<ParticleError> StepParticles(Particles& particles, Crt::CrtRandom& random);
// Para 67 y 68, 4303c0 consulta el mapa raw32 al tratar cada slot. Step conserva sus continuaciones
// subtype1 (campo +0x50 a escala) y !=1 aunque Create sólo admita /0; no agregar una guarda común /0.
// Un binding no admitido falla después de viento y vida/movimiento del slot actual. Sin mapa, 67/68
// fallan además después de cambiar escala y antes de RGB. El error conserva todos los efectos del
// prefijo y slots anteriores, y deja los posteriores sin visitar: no es transaccional. Repetir el tick
// consume viento y avanza otra vez los slots anteriores. No copia ni posee WorldState.
[[nodiscard]] Core::Expected<void, ParticleError> StepParticlesChecked(Particles& particles, Crt::CrtRandom& random,
                                                                       const std::int32_t* map = nullptr,
                                                                       const ParticleTerrainHeight& terrainHeight = {});
// Carga el reloj por slot6 después del movimiento común, sin snapshot de la pasada. `owners` resuelve el
// dueño de 622/1 (0x5476f0), que se lee y se escribe en el paso, y `actorActions` la acción del personaje dueño de
// la órbita 60f/0 (0x54735c); sin él, una 60f/0 viva falla con MissingOwner. La 5f3 pide la altura del terreno y el
// alto de su textura
// (`textureHeight`): sin ellos corta después de mover, frenar y agrandar. La 5ed pide la luz dinámica (`lighting`,
// 0x7b4ccc4): sin ella corta con MissingDynamicLight después de su cuadro. La 5fe pide el hueso de su dueño
// (`ownerBones`, 0x53fdb0): sin él corta con MissingOwner después de su escala.
// 0x7b4ccc4): sin ella corta con MissingDynamicLight después de su cuadro. La 5fd pide la mano de su dueño
// (`actorHands`, 0x53fdb0): sin ella corta con MissingOwner después de la escala y la rotación.
[[nodiscard]] Core::Expected<void, ParticleError>
StepParticlesChecked(Particles& particles, Crt::CrtRandom& random, std::reference_wrapper<const float> worldClock,
                     const std::int32_t* map, IParticleTrigonometry& trigonometry,
                     const ParticleTerrainHeight& terrainHeight = {}, const ParticleObjectOwners& owners = {},
                     const ParticleActorActions& actorActions = {}, const ParticleTextureHeight& textureHeight = {},
                     Terrain::TerrainLighting* lighting = nullptr, const ParticleActorHands& actorHands = {},
                     const ParticleOwnerBones& ownerBones = {});

// Igual contrato con los sonidos0x40 de las explosiones1 creadas por5ed/2; no retiene el callback. `ownerStates` da
// el dueño (+0x40) de las que lo siguen (5c6/4, 5d5/2, el fuego /9 y 5de/4): sin él, o sin ese dueño, cortan con
// MissingOwner en su caso. `sprites` es la tabla del frame (0x5491e0) donde la 5fc/2 deja su destello en cada paso:
// sin ella corta con MissingSprites después de girar, sacar su rand(), moverse y apagarse.
[[nodiscard]] Core::Expected<void, ParticleError>
StepParticlesChecked(Particles& particles, Crt::CrtRandom& random, std::reference_wrapper<const float> worldClock,
                     const std::int32_t* map, IParticleTrigonometry& trigonometry,
                     const ParticleTerrainHeight& terrainHeight, const ParticleObjectOwners& owners,
                     const ParticleActorActions& actorActions, const ParticleTextureHeight& textureHeight,
                     Terrain::TerrainLighting* lighting, const ParticleActorHands& actorHands,
                     const ParticleOwnerBones& ownerBones, const std::function<void(Audio::SoundId)>& sound,
                     const ParticleOwnerStates& ownerStates = {}, FrameSprites* sprites = nullptr);

} // namespace Client::Particles
