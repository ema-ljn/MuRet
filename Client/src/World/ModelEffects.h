#pragma once

#include "Core/Expected.h"
#include "Render/Models/ObjectState.h"
#include "Render/Models/Pose.h"
#include "Render/Models/Skeleton.h"

#include <glm/ext/vector_float3.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace Client::Data::Terrain
{
struct TerrainAttributes;
}

namespace Client::Render::Models
{
class CrtRandom;
}

// Los efectos con modelo (CL7.6 A): la tabla de 0x518cf0 (200 lugares de 0x1d4 bytes).
// El dragón conserva los vectores históricos; llegada, teleport y torre de Noria se contrastaron
// estáticamente con ASM. Para el paso del juego y su CRT, usar World/GameModelEffects.h.
namespace Client::World
{
class FrameSprites;

inline constexpr std::size_t ModelEffectCount = 200;
// El dragón que vuela en el login, con su modelo (Scenes::DragonModel) y la escala que le pasa 0x606660.
inline constexpr std::int16_t DragonEffect = 0xb4;
inline constexpr float DragonScale = 1.6f;
// El efecto de llegada: lo crea dos veces la respuesta del F3 03 sobre el héroe (06, "El héroe").
inline constexpr std::int16_t ArrivalEffect = 0x61f;
inline constexpr std::int16_t TeleportEffect = 0x5c6;
// La mariposa (0x51913a): la emite el 0x3c del mapa 54 y del 33 (0x425790), con el objeto de dueño.
inline constexpr std::int16_t ButterflyEffect = 0xae;
// Los del Dungeon y de Lost Tower: la piedra que cae del 0x34 del mapa 1 (DungeonStone), el chorro de fuego
// del 0x18 del 4 (sin modelo: sólo el pase del suelo) y las dos piedritas que salta el chorro (Stone01/02).
inline constexpr std::int16_t FallingStoneEffect = 0xda;
inline constexpr std::int16_t FlameJetEffect = 0x5de;
inline constexpr std::int16_t FirstStoneChipEffect = 0xc8;
inline constexpr std::int16_t SecondStoneChipEffect = 0xc9;
// Los de Icarus (0x605d70 y 0x6061c0): la nube 0xbb (Object11\cloud) sobre el héroe y el 0x5ac, sin modelo,
// que sube soltando partículas 5ac/1.
inline constexpr std::int16_t IcarusCloudEffect = 0xbb;
inline constexpr std::int16_t IcarusFlareEffect = 0x5ac;
// El 0x5ac/3 del conjunto completo (0x50fdc3): cada 100 dibujos del personaje con +0x358, con él de dueño. No se
// mueve solo: cada 15 pasos se corre alrededor del dueño y suelta un joint 619/9 (0x52a9ad).
inline constexpr std::int32_t FullSetFlareSubtype = 3;
// El de Aida (0x42593f): el 0x46 crea el 0xb3, un modelo que Aida no carga (en Atlans, Object8\Fish02), que
// vaga cerca de donde nació y suelta partículas 5ac/9.
inline constexpr std::int16_t AidaWispEffect = 0xb3;
// El de Castle Siege (0x42c6b0): el objeto 79 del mapa 30 crea el 0x135 (Skill\TowerGatePlane), un plano
// que sube y baja con el reloj y se dibuja dos veces, la segunda reflejada.
inline constexpr std::int16_t GatePlaneEffect = 0x135;
// El calco que se apaga (0x61d, sin modelo: solo el pase del suelo dibuja la /1). El 0x38 de Crywolf crea el /3, que
// suelta órbitas 60f/10 (0x52b933).
inline constexpr std::int16_t FadingDecalEffect = 0x61d;
inline constexpr std::int32_t CrywolfDecalSubtype = 3;
// La onda (0x6eb, Effect\ShockWave.jpg, sin modelo: solo el pase del suelo la dibuja). El galope del caballo oscuro en
// Icarus crea la /1 (0x44431b), que crece y tiembla.
inline constexpr std::int16_t ShockWaveEffect = 0x6eb;
inline constexpr std::int32_t GallopShockWaveSubtype = 1;
// El pisotón del caballo oscuro (0x4453e0, la acción 3 de 0x444010): cada 10 pasos lógicos la onda 0x6eb con el
// subtipo 0, que se achica, y seis piedras 0x118 o 0x119 (Skill\groundStone y groundStone2, 0x615700) alrededor. Cada
// piedra suelta en sus pasos 33 a 36 el polvo 5f2/11 y una piedrita 0xc8/0xc9 con el subtipo 10 (0x5275ca).
inline constexpr std::int32_t StompShockWaveSubtype = 0;
inline constexpr std::int16_t FirstGroundStoneEffect = 0x118;
inline constexpr std::int16_t SecondGroundStoneEffect = 0x119;
inline constexpr std::int32_t GroundStoneChipSubtype = 10;
// Los del dibujo de los Fenrir (0x60277b): el rayo 0x14d (Effect\lightning_type01.bmd), que nace en un hueso del
// Fenrir con su sprite 0x5ac y se prende y se apaga en pocos pasos, y el calco 0x14e (eff_lightinga01..05, sin
// modelo: lo dibuja 0x52d8f2 en el piso), que cambia de textura cada 200 ms de timeGetTime().
inline constexpr std::int16_t LightningEffect = 0x14d;
inline constexpr std::int16_t LightningDecalEffect = 0x14e;
// El calco de colores (0x61e, Effect\Magic_Ground2 en el pase del suelo). Al vestir una mascota fuera de la zona
// segura, 0x5bca46 crea el /1 sobre el héroe, con él de dueño: cada paso suelta tres joints 619/1 que bajan hacia el
// dueño (0x518900).
inline constexpr std::int16_t ColoredDecalEffect = 0x61e;
inline constexpr std::int32_t PetSummonSubtype = 1;
// El terremoto (la familia EarthQuake): el 0xfa, que la acción 3 del caballo oscuro crea con el caballo de dueño y el
// estado (+0x88) en 2 (0x4453e0) y que con un estado distinto de 0 no se dibuja (0x52d400). Su paso suelta en la vida
// 11 la explosión 5ed/0 y las grietas 0xfb..0x100, y en la 10 las 0x101 y 0x102 (Data\Skill\EarthQuake01..08, que
// dibuja 0x602e90). Los tres primeros (0xfd, 0xfb y 0xfc) llevan al 0xfa de dueño; los demás, al dueño del 0xfa.
inline constexpr std::int16_t EarthquakeEffect = 0xfa;
inline constexpr std::int16_t FirstEarthquakeCrack = 0xfb;
inline constexpr std::int16_t LastEarthquakeCrack = 0x102;
// +0x74 en 0x80: las grietas 0xfc, 0xff y 0x102 con el subtipo 1 (0x51a850); 0x6002e0 las dibuja por 0x4ef0c0 con
// 0x82 (0x6004aa).
inline constexpr std::uint32_t CrackRenderFlags = 0x80;

struct ModelEffect;
// +0x108, el dueño, con lo que leen de él en vivo los pasos del terremoto y sus grietas: el WORD +0x2 (su tipo) y +0x1c
// (sus ángulos). El original guarda un puntero a un objeto de cualquier tabla; MuRet, punteros a esos campos, que
// tienen que vivir más que el efecto (los lugares de las tablas no se mueven, y un lugar que se vuelve a usar se lee
// como el original lo lee). Un ayudante (Scenes::HelperState) da `type` y `angles`; otro efecto, `effect` (su
// ModelEffect::type) y sus ángulos. Sin ninguno de los tres, no hay dueño.
struct ModelEffectOwner
{
    const std::uint16_t* type = nullptr;
    const ModelEffect* effect = nullptr;
    const glm::vec3* angles = nullptr;
};

// Proyección de los campos compartidos por los efectos de 0x1d4 bytes.
struct ModelEffect
{
    bool alive = false;                        // +0x0
    bool seen = false;                         // +0x16c, del último dibujo de este lugar
    std::int16_t type = 0;                     // +0x2, el modelo
    std::int32_t subtype = 0;                  // +0x4, la rama del efecto
    Render::Models::ObjectTransform transform; // +0xc la escala, +0x10 la posición, +0x1c los ángulos
    std::int32_t life = 0;                     // +0x60, pasos que le quedan
    glm::vec3 velocity{};                      // +0xcc, por paso, girada por los ángulos
    float speed = 0.0f;                        // +0xd8, keys por paso
    float turnPending = 0.0f;                  // +0xdc: en el próximo paso lejos, da la vuelta
    float turnDirection = 0.0f;                // +0xe4: 1 o −1, el sentido del giro
    float flagEc = 0.0f;                       // +0xec
    std::uint8_t state = 0;                    // +0x88: 0 y 1 giran, 2 va derecho
    // +0x84: al empezar cada paso, la meta del alabeo del estado (30, −30 o 0); al dar la vuelta, el
    // yaw con el que se compara el giro de ±60°. Los dos usos son del original.
    float reference = 0.0f;
    Render::Models::AnimationState animation; // +0x111 la acción, +0x114 el frame, +0x118 el anterior
    glm::vec3 light{};                        // +0xf4
    float alpha = 1.0f;                       // +0x174
    Render::Models::ObjectState objectState;  // +0x58, +0x64 y +0x68
    std::uint16_t disturbanceState = 0;       // +0x8a, guard de437860 del efecto9
    bool vertexLight = true;                  // +0xe8
    glm::vec3 start{};                        // +0x17c, la partida
    // De MuRet: la posición antes del último paso, para dibujar entre dos pasos (D-C4).
    glm::vec3 previousPosition{};
    std::int32_t textureFrame = 0;           // +0x1c4: selector firmado del calco 0x14e en52d8f2.
    std::uint32_t textureTime = 0;           // +0x1c0: el timeGetTime() del último cambio del calco 0x14e.
    const std::int16_t* itemType = nullptr;  // +108 del efecto→+8c del dueño durable, paraf4/fa.
    const std::uint8_t* itemLevel = nullptr; // +108→+8e; no inferir desde ActorState.
    // +0x108 distinto de 0: el efecto tiene dueño. El plano 0x135 sólo se mueve con dueño (0x529745).
    bool hasOwner = false;
    // +0x108 como dueño opaco, para los pasos que lo leen fuera de World: el ActorState del personaje del 5ac/3.
    void* ownerContext = nullptr;
    // +0x108 con lo que leen en vivo el terremoto 0xfa y sus grietas (OwnerTypeOf). El prefijo lo pisa.
    ModelEffectOwner owner{};
    // +0x28..+0x30: el 0xfa copia ahí sus ángulos al nacer (0x51a66d) y su paso gira la velocidad con ellos. El prefijo
    // no los toca.
    glm::vec3 copiedAngles{};
    // +0x74: el prefijo lo deja en 0; CrackRenderFlags en las grietas de subtipo 1.
    std::uint32_t renderFlags = 0;
    // CL9 hito 3, fase 2e (las flechas). +0x89: el byte del noveno argumento de 0x518cf0, el lugar de la skill en el
    // registro propio que 0x520170 lee al chocar. +0x1bd: el del décimo, que nadie portado lee. +0x8c: la cuenta +0xf04
    // del registro propio que la salida de las flechas (0x51a1b6) copia entre la apertura y el cierre. +0x100: el
    // prefijo lo pone en 0; con la skill 0x34, los pasos que le quedan sin chocar (0x52045c, hito 4).
    std::uint8_t byte89 = 0;
    std::uint8_t byte1bd = 0;
    std::uint16_t word8c = 0;
    std::int32_t dword100 = 0;
};

// Los once argumentos de 0x518cf0, como los escribe su prefijo (0x518d28..0x518e36): el tipo en +0x2, la posición en
// +0x10, los ángulos en +0x1c, la luz en +0xf4, el subtipo en +0x4, el dueño en +0x108 (`owner`, opaco), +0x8a,
// +0x89, +0x88 y +0x1bd, y la escala (de 0 o menos, 0,9).
struct ModelEffectArguments
{
    std::int16_t type = 0;
    glm::vec3 position{};
    glm::vec3 angles{};
    glm::vec3 light{};
    std::int32_t subtype = 0;
    void* owner = nullptr;
    std::uint16_t key = 0xffff;
    std::uint8_t byte89 = 0;
    std::uint8_t byte88 = 0;
    std::uint8_t byte1bd = 0;
    float scale = 0.0f;
};

// El WORD +0x2 del dueño, leído en vivo, o nada sin dueño.
[[nodiscard]] std::optional<std::uint16_t> OwnerTypeOf(const ModelEffectOwner& owner);

class ModelEffects
{
public:
    // 438a1a→5191be: tipo9/0, ownerNULL, escalaargumento0; primer libre, lleno sinCRT.
    std::optional<std::size_t> CreateKalimaEffect(const glm::vec3& position, const glm::vec3& angles,
                                                  const glm::vec3& light, Render::Models::CrtRandom& random);
    // 0x518cf0(type, position, angles, light, 0, dueño, −1, 0, 0, 0, scale) en el primer lugar sin el
    // byte vivo, o nada si están todos. Una escala de 0 o menos pasa a 0,9 (0x518d3c). Del 0xb4: la
    // posición del dueño con la z 3.400 más arriba (0x518fc9), los ángulos con el alabeo en 0 (0x518fb3),
    // la luz en blanco (la salida 0x51a97b pisa `light`), vida 1.000, la velocidad (0, −35, 0), la de la
    // animación en 1, +0xdc, +0xe4 y +0xec en 1, luz por vértice y la partida en la posición. Del 0x61f
    // con el subtipo 0: la posición, los ángulos y la luz tal cual, 20 pasos de vida y la animación a
    // 0,3, sin rand(). El 0x5c6 conserva ese prefijo con vida 10 (0x51e4f6), también sin rand().
    // Noria tiene su creador separado con CRT; los demás tipos no se crean.
    std::optional<std::size_t> Create(std::int16_t type, const glm::vec3& position, const glm::vec3& angles,
                                      const glm::vec3& light, float scale);
    // 0x51c36e/0x51c38a: los efectos 322..324 de la torre de Noria; 322/323 ponen sourceLight en cero.
    std::optional<std::size_t> CreateNoriaEffect(std::int16_t type, const glm::vec3& position, const glm::vec3& angles,
                                                 glm::vec3& sourceLight, Render::Models::CrtRandom& random);
    // 0x518cf0(0xae, posición, ángulos, luz, subtipo, dueño, −1, 0, 0, 0, 0) → 0x51913a: el prefijo común
    // (+0x8a en −1), en la escena 4 oculto (+0x58 = −2), vida 250, animación a 0,3, sin luz por vértice, el
    // estado con rand() & 1, la velocidad (0, escala del dueño · −5, 0) y la escala 0,9 con el subtipo 3 o
    // 0,25 con los demás. Un rand(); lleno, ninguno.
    std::optional<std::size_t> CreateButterflyEffect(const glm::vec3& position, const glm::vec3& angles,
                                                     const glm::vec3& light, std::int32_t subtype, float ownerScale,
                                                     bool selectionScene, Render::Models::CrtRandom& random);
    // 0x518cf0(0xda, posición, ángulos, luz, 0, 0, −1, 0, 0, 0, 0) → 0x51b80a: el prefijo común (+0x8a en −1)
    // y seis rand(): la vida (& 15) + 24, la escala ((& 7) + 6) · 0,1f, la caída −(& 3) en +0xe4 y el
    // corrimiento ((& 63) − 32; −((& 31) + 50); (& 127) + 200), girado por los ángulos y sumado a la posición
    // en float. Lleno, ninguno y sin rand().
    std::optional<std::size_t> CreateFallingStone(const glm::vec3& position, const glm::vec3& angles,
                                                  const glm::vec3& light, Render::Models::CrtRandom& random);
    // 0x518cf0(0x5de, …, 0, 0, −1, 0, 0, 0, 0) → 0x51e7d8 con el subtipo 0: el prefijo común, vida 40 y el
    // registro propio abierto y cerrado (copia su +0xf04 a +0x8c, que sólo lee la rama del dueño héroe, y el
    // cierre que deja la cuenta en 0 gasta sus 3.848 rand()). Lleno, ninguno.
    std::optional<std::size_t> CreateFlameJet(const glm::vec3& position, const glm::vec3& angles,
                                              const glm::vec3& light, std::uint8_t& ownRecordDepth,
                                              Render::Models::CrtRandom& random);
    // 0x518cf0(0xc8 o 0xc9, …, 0, 0, −1, 0, 0, 0, 0) → 0x51b72b con el subtipo 0: el prefijo común y cinco
    // rand(): el empuje ((& 255) + 64) · 0,1f en y, la vida (& 15) + 32, la escala ((& 3) + 8) · 0,1f, el
    // yaw rand() % 360 y la caída (& 15) + 8 en +0xe4; la velocidad es el empuje girado por los ángulos con
    // ese yaw. Otro tipo o la tabla llena, ninguno.
    std::optional<std::size_t> CreateStoneChip(std::int16_t type, const glm::vec3& position, const glm::vec3& angles,
                                               const glm::vec3& light, Render::Models::CrtRandom& random);
    // 0x518cf0(0xbb o 0x5ac, …, 0, 0, −1, 0, 0, 0, 0) con el subtipo 0. La nube (0x51b968): vida 2, el
    // blendMesh en 0, escala 10, 200 más en y y 190 menos en z, sin luz por vértice; sin rand(). El 0x5ac
    // (0x51e3ec): la luz en 0,3f, s = rand() % 5 + 9 y la velocidad sin girar (sen 30° · cos 70°; cos 30° ·
    // cos 70°; sen 70°) · s · 0,5 con la x87 sobre los double 0x6ba1c8 y 0x6ba1d0, escala 3 y vida 400. Otro
    // tipo o la tabla llena, ninguno y sin rand().
    std::optional<std::size_t> CreateIcarusEffect(std::int16_t type, const glm::vec3& position, const glm::vec3& angles,
                                                  const glm::vec3& light, Render::Models::CrtRandom& random);
    // 0x518cf0(0xb3, posición, ángulos, luz, 0, objeto, −1, 0, ftol(escala del objeto), 0, 0) → 0x51900b: el
    // prefijo común (+0x8a en −1, escala 0,9) y seis rand(): la z, la y y la x ± 100 (r % 200 − 100), el yaw
    // más r % 360, la y de la velocidad (r & 7) · 0,1f − 5 (la x y la z en 0) y la escala más (r % 5) ·
    // 0,025f. El estado +0x88 en 0 (pisa el byte de la escala del objeto), +0x68 y la animación +0xd8 en 1,
    // vida 1.000, +0xe4 y +0xec en 1, la partida en la posición ya corrida y la luz (0,01f; 0,01f; 0,03f).
    // El dueño (+0x108) no lo lee nadie. Lleno, ninguno y sin rand().
    std::optional<std::size_t> CreateAidaWisp(const glm::vec3& position, const glm::vec3& angles,
                                              const glm::vec3& light, Render::Models::CrtRandom& random);
    // 0x518cf0(0x135, posición, ángulos, luz, 0, objeto, −1, 0, 0, 0, 0) → 0x51d73b: el prefijo común (+0x8a
    // en −1, escala 0,9), el dueño (+0x108), vida 100, la luz de las mallas (+0x68) en 0,3f y la partida en
    // la posición. Sin rand(); lleno, ninguno.
    std::optional<std::size_t> CreateGatePlane(const glm::vec3& position, const glm::vec3& angles,
                                               const glm::vec3& light, bool hasOwner);
    // 0x518cf0(0x61d, posición, ángulos, luz, subtipo, dueño, `disturbance`, 0, 0, 0, 0) → 0x51ee96: el prefijo
    // común (escala 0,9 y +0x8a con `disturbance`), vida 20 y escala 0,5; con el subtipo 0, la vida en 15. Sin
    // rand(). El dueño (+0x108) no lo lee el paso del 0x61d. Lleno, ninguno.
    std::optional<std::size_t> CreateFadingDecal(const glm::vec3& position, const glm::vec3& angles,
                                                 const glm::vec3& light, std::int32_t subtype,
                                                 std::uint16_t disturbance);
    // 0x518cf0(0x5ac, posición, ángulos, luz, 3, personaje, −1, 0, 0, 0, 0) → 0x51e489: el prefijo común (+0x8a en
    // −1, escala 0,9) con el personaje de dueño (+0x108), vida 30 y +0xd8 en 0. Sin rand(); lleno, ninguno.
    std::optional<std::size_t> CreateFullSetFlare(const glm::vec3& position, const glm::vec3& angles,
                                                  const glm::vec3& light, void* owner);
    // 0x518cf0(0x6eb, posición, ángulos, luz, 1, 0, −1, 0, 0, 0, 0) → 0x51f4ad: el prefijo común (+0x8a en −1, sin
    // dueño), la vida 30 y la escala 20 del tipo, y con el subtipo 1 la vida 20 y la escala ((rand() % 10) + 10) ·
    // 0,1f en la x87 (0x51f4c3..0x51f4eb). Un rand(); lleno, ninguno y sin rand().
    std::optional<std::size_t> CreateGallopShockWave(const glm::vec3& position, const glm::vec3& angles,
                                                     const glm::vec3& light, Render::Models::CrtRandom& random);
    // 0x518cf0(0x14d, posición, ángulos, luz, 0, dueño, −1, 0, 0, 0, 0) → 0x51db7a: el prefijo común (+0x8a en −1,
    // escala 0,9) con el dueño, vida 100, la escala (rand() % 100) · 0,002f + 0,3f, el subtipo en 0, el alpha en 0,7f,
    // los tres ángulos con rand() % 360 y, por rand() % 30, el lugar: el punto del hueso 10 (con 1 y 2), 14 (3), 2 (4
    // y 5), 50 o 51 con un rand() par o impar (6 y 7) o 53 (8) del buffer global de la pose (0x4ec110(…, 0), que no
    // mira el modelo del dueño), con 0,2f menos de escala en el 3, el 6, el 7 y el 8; con 9 y 10 lo apaga (y el
    // alpha en 0); con los demás, 0,1f más de escala y la posición corrida rand() % 240 − 120 en x, rand() % 10 − 5
    // en y y 110 en z. Siempre, también apagado, el sprite 0x5ac de escala 2 en su lugar con la luz menos 0,3f
    // (0x51ddab). `bonePoint` da el punto de un hueso del buffer global; sin él, corta antes de tocar nada. Lleno,
    // ninguno y sin rand().
    [[nodiscard]] Core::Expected<std::optional<std::size_t>, std::string>
    CreateLightning(const glm::vec3& position, const glm::vec3& angles, const glm::vec3& light, void* owner,
                    Render::Models::CrtRandom& random, const std::function<glm::vec3(std::size_t)>& bonePoint,
                    FrameSprites& sprites);
    // 0x518cf0(0x14e, posición, ángulos, luz, subtipo, dueño, −1, 0, 0, 0, 0) → 0x51ddfc: el prefijo común (+0x8a en
    // −1, escala 0,9) con el dueño, vida 200, el cuadro (+0x1c4) en 0, la luz, el alpha en 1, la hora (+0x1c0) de
    // timeGetTime() y la z en 0. Sin rand(); lleno, ninguno.
    std::optional<std::size_t> CreateLightningDecal(const glm::vec3& position, const glm::vec3& angles,
                                                    const glm::vec3& light, std::int32_t subtype, void* owner,
                                                    std::uint32_t systemTime);
    // 0x518cf0(0x61e, posición, ángulos, luz, 1, dueño, −1, 0, 0, 0, 0) → 0x51eebb: el prefijo común (+0x8a en −1,
    // escala 0,9) con el dueño (+0x108) y vida 20; el subtipo 1 no tiene caso y sale por 0x51db73. Sin rand(); lleno,
    // ninguno.
    std::optional<std::size_t> CreatePetSummon(const glm::vec3& position, const glm::vec3& angles,
                                               const glm::vec3& light, void* owner);
    // 0x518cf0(0x118 o 0x119, posición, ángulos, luz, 0, 0, −1, 0, 0, 0, 0) → 0x51cc6b o 0x51cd1b: el prefijo común
    // (+0x8a en −1, sin dueño, escala 0,9) y el atributo de la celda de la posición (UnmaskedCellAttribute, en
    // 0x51cc8b o 0x51cd3b): con el bit 0x04, el 0x08 o el 0x10 el lugar queda escrito con el byte vivo en 0, y
    // ninguno, sin rand(). Si no, vida 40, la escala rand() % 30 · 0,01f + 1,2f (0x118) o + 1 (0x119) en la x87, +0xd8
    // en 0,3f y el yaw rand() % 360; 0x4ea030(efecto, 0) no hace nada, porque el prefijo ya dejó la acción en 0. Dos
    // rand(). Sin la tabla de atributos, o con una celda fuera de ella, corta después del prefijo, con el lugar vivo
    // como lo dejó el original en ese punto. Otro tipo o la tabla llena, ninguno y sin rand().
    [[nodiscard]] Core::Expected<std::optional<std::size_t>, std::string>
    CreateGroundStone(std::int16_t type, const glm::vec3& position, const glm::vec3& angles, const glm::vec3& light,
                      const Data::Terrain::TerrainAttributes* attributes, Render::Models::CrtRandom& random);
    // 0x518cf0(0xc8 o 0xc9, …, 10, 0, −1, 0, 0, 0, 0) → 0x51b432, la piedrita que suelta la piedra 0x118/0x119: el
    // prefijo común, el subtipo 10 y cinco rand(): el empuje ((& 255) + 64) · 0,2 (el double 0x6ba1e8) en y, la vida
    // (& 15) + 32, la escala ((& 3) + 15) · 0,05f, el yaw rand() % 360 y la caída (& 15) + 28 en +0xe4; la velocidad es
    // el empuje girado por los ángulos con ese yaw. Otro tipo o la tabla llena, ninguno y sin rand().
    std::optional<std::size_t> CreateGroundStoneChip(std::int16_t type, const glm::vec3& position,
                                                     const glm::vec3& angles, const glm::vec3& light,
                                                     Render::Models::CrtRandom& random);
    // 0x518cf0(0x6eb, posición, ángulos, luz, 0, 0, −1, 0, 0, 0, 0) → 0x51f4ad con el subtipo 0, la onda del pisotón:
    // el prefijo común (+0x8a en −1, sin dueño), vida 30 y escala 20, sin rand(). Lleno, ninguna.
    std::optional<std::size_t> CreateStompShockWave(const glm::vec3& position, const glm::vec3& angles,
                                                    const glm::vec3& light);
    // 0x518cf0(0xfa, posición, ángulos, luz, 0, dueño, −1, 0, estado, 0, 0) → 0x51a66d, como la llama el caballo
    // (0x4453e0, con el estado 2): el prefijo común (+0x8a en −1, escala 0,9, el estado en +0x88) con el dueño,
    // +0x28..+0x30 con los ángulos, la partida con la posición, vida 20, el subtipo rand() % 100, la z de los ángulos
    // 330 más (fld/fadd/fstp), +0x28 80 más, +0x30 180 más y +0xe4 en 50. Después, 0x51a8d8: abre el registro propio
    // (0x4e79a0, no gasta), le sube 1 a su cuenta +0xf04 (`ownRecordCount`, que envuelve, Q-C426; la de antes va a
    // +0x8c, que no lee nadie portado) y lo cierra (0x40f370: con `ownRecordDepth` en 0, sus 3.848 rand()). Lleno,
    // ninguno, sin rand() y sin tocar el registro.
    std::optional<std::size_t> CreateEarthquake(const glm::vec3& position, const glm::vec3& angles,
                                                const glm::vec3& light, std::uint8_t state,
                                                const ModelEffectOwner& owner, std::uint8_t& ownRecordDepth,
                                                std::uint8_t& ownRecordCount, Render::Models::CrtRandom& random);
    // 0x518cf0(0xfb..0x102, posición, ángulos, luz, subtipo, dueño, tamaño, 0, 0, 0, 0) → 0x51a6e4..0x51a85c: el
    // prefijo común con el dueño y +0x8a en `size`, y la escala size · 0,01f (fild/fmul/fstp). Vida: 0xfb 35 con el
    // subtipo 0 y 60 con el 1; 0xfc 20 con el 0 y el 1 y 50 con el 2; 0xfd, 0xfe y 0x100 35; 0xff, 0x101 y 0x102 40.
    // +0x64 en 0 en todos salvo 0xfd y 0x100, que dejan el −1 del prefijo. Con el subtipo 1, 0xfc, 0xff y 0x102 ponen
    // +0x74 en CrackRenderFlags. Un subtipo sin caso (0xfb con otro que 0 o 1, 0xfc con otro que 0..2) sale por
    // 0x51db73 sin escala ni vida: la escala 0,9 del prefijo y la vida del uso anterior del lugar (Q-C423). Otro tipo
    // o la tabla llena, ninguno. Sin rand().
    std::optional<std::size_t> CreateEarthquakeCrack(std::int16_t type, const glm::vec3& position,
                                                     const glm::vec3& angles, const glm::vec3& light,
                                                     std::int32_t subtype, const ModelEffectOwner& owner,
                                                     std::int16_t size);
    // 0x518cf0 hasta su switch (0x518e3c): el primer lugar sin el byte vivo con lo que escribe el prefijo (el byte
    // vivo, los argumentos, +0x58 y +0x64 en −1, +0x68 en 1, el alpha en 1, +0xd8 en 0,3, la acción y el frame en 0,
    // +0x74 y +0x100 en 0, la luz por vértice y la velocidad en 0). Los demás campos del lugar quedan los del uso
    // anterior. Lleno, nada (0x51db71), sin rand(). El caso de cada tipo lo sigue quien lo llama (Scenes/ArrowEffects).
    std::optional<std::size_t> Begin(const ModelEffectArguments& arguments);
    // 0x6070a0: sólo apaga el byte vivo de cada lugar (Q-C138).
    void Clear();

    [[nodiscard]] std::span<ModelEffect> Effects();
    [[nodiscard]] std::span<const ModelEffect> Effects() const;

private:
    std::array<ModelEffect, ModelEffectCount> m_effects{};
};

// 0x5f8290(ftol(x · 0,01f), ftol(y · 0,01f)) y el WORD de 0x7d0d348 en ese índice, como lo leen las altas de las
// piedras 0x118/0x119: y · 256 + x en 32 bits, sin la máscara de 0x5f82a0. Sin la tabla, con una coordenada que el
// ftol de 0x69fd3c no representa o con un índice fuera de las 65.536 celdas (el original leería otra memoria), corta
// con `address`.
[[nodiscard]] Core::Expected<std::uint16_t, std::string>
UnmaskedCellAttribute(const Data::Terrain::TerrainAttributes* attributes, const glm::vec3& position,
                      std::string_view address);

// 0x5208a0 sobre un efecto vivo, con el esqueleto de su modelo (para el avance, o nullptr): el caso del
// tipo (el 0xb4: 0x52096c..0x520b72) y la cola común (0x52cb98): la animación a +0xd8 (0x4ec200; si la
// acción termina, el frame en sus keys y la acción en 0xff), la posición más la velocidad girada por los
// ángulos (0x51fc80 con 0x5fc8f0 y 0x5fcbd0) y una vida menos; con la vida en 0, muere.
void StepModelEffect(ModelEffect& effect, const Render::Models::Skeleton* skeleton);
} // namespace Client::World
