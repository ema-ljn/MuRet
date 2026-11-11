#pragma once

#include "Render/Models/ObjectState.h"
#include "Render/Models/Pose.h"
#include "Render/Models/Skeleton.h"

#include <glm/ext/vector_float3.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace Client::Render::Models
{
class CrtRandom;
}

// Los efectos con modelo (CL7.6 A): la tabla de 0x518cf0 (200 lugares de 0x1d4 bytes).
// El dragón conserva los vectores históricos; llegada, teleport y torre de Noria se contrastaron
// estáticamente con ASM. Para el paso del juego y su CRT, usar World/GameModelEffects.h.
namespace Client::World
{
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
    const std::int16_t* itemType = nullptr;  // +108 del efecto→+8c del dueño durable, paraf4/fa.
    const std::uint8_t* itemLevel = nullptr; // +108→+8e; no inferir desde ActorState.
    // +0x108 distinto de 0: el efecto tiene dueño. El plano 0x135 sólo se mueve con dueño (0x529745).
    bool hasOwner = false;
    // +0x108 como dueño opaco, para los pasos que lo leen fuera de World: el ActorState del personaje del 5ac/3.
    void* ownerContext = nullptr;
};

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
    // 0x6070a0: sólo apaga el byte vivo de cada lugar (Q-C138).
    void Clear();

    [[nodiscard]] std::span<ModelEffect> Effects();
    [[nodiscard]] std::span<const ModelEffect> Effects() const;

private:
    std::array<ModelEffect, ModelEffectCount> m_effects{};
};

// 0x5208a0 sobre un efecto vivo, con el esqueleto de su modelo (para el avance, o nullptr): el caso del
// tipo (el 0xb4: 0x52096c..0x520b72) y la cola común (0x52cb98): la animación a +0xd8 (0x4ec200; si la
// acción termina, el frame en sus keys y la acción en 0xff), la posición más la velocidad girada por los
// ángulos (0x51fc80 con 0x5fc8f0 y 0x5fcbd0) y una vida menos; con la vida en 0, muere.
void StepModelEffect(ModelEffect& effect, const Render::Models::Skeleton* skeleton);
} // namespace Client::World
