#pragma once

#include "Core/Expected.h"
#include "World/ParticleOwner.h"

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <vector>

namespace Client::Render::Models
{
class CrtRandom;
} // namespace Client::Render::Models

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
namespace Client::World
{
class IParticleTrigonometry;

inline constexpr std::size_t ParticleCount = 3000;
inline constexpr std::int32_t ShinyParticle = 0x5fc;
inline constexpr std::int32_t ThunderParticle = 0x5ca;
inline constexpr std::int32_t FairySparkParticle = 0x5c5;
inline constexpr std::int32_t FireParticle = 0x5d9;
inline constexpr std::int32_t DropParticle = 0x607;
inline constexpr std::int32_t WorldSmokeSubtype = 2;
inline constexpr std::uint16_t BreathParticle = 0x5f2;
// La chispa del cuervo (Effect\Spark03.jpg), con el subtipo 5 (06 § El cuervo).
inline constexpr std::uint16_t SparkParticle = 0x5c6;
inline constexpr std::int32_t TeleportSparkSubtype = 1;
inline constexpr std::int32_t RavenSparkSubtype = 5;
inline constexpr std::int32_t MapSparkSubtype = 9;
inline constexpr std::uint16_t CloudParticle = 0x622;
inline constexpr std::int32_t LoginCloudSubtype = 10;
// Los dos tipos que comparten creación y paso; la 68 además corre la posición al nacer.
inline constexpr std::int32_t Particle67 = 0x67;
inline constexpr std::int32_t Particle68 = 0x68;
// La órbita (0x60f/3): nace con 60 de vida, y 548550 no la dibuja mientras la conserve, salvo la /4.
inline constexpr std::int32_t OrbitParticle = 0x60f;
inline constexpr std::int32_t OrbitLife = 60;

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
    float rotationFactor = 0.0f; // +0x60: 622/10 retiene al crear y escribe 1,5 al avanzar; 622/8 inicializa
    glm::vec2 field64{};         // +0x64..68: retenidos al crear
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
};

// 5f93c0: retorno retenido, consultado después del prefijo del slot5c5.
using ParticleTerrainHeight = std::function<double(float, float)>;

class Particles
{
public:
    // 0x53fe70(tipo, posición, ángulos, luz, subtipo, escala, dueño) en el primer lugar libre, con el reloj
    // del mundo (DAT_05767b88, en ms) y el rand() de la CRT. Con los 3.000 llenos no se crea ni saca
    // rand(). Se aceptan5ca/0, 5f2/0,2, 5c6/1,5,9, 622/8,10, 5d9/0..3, 607/0, 67/0 y 68/0.
    // Create es la compatibilidad login; Core usa CreateChecked para distinguir errores de pool lleno. La neblina
    // consume cinco rand() y conserva +0x60 del uso anterior del lugar. 622/8 consume siete rand().
    std::optional<std::size_t> Create(std::int32_t type, const glm::vec3& position, const glm::vec3& angles,
                                      const glm::vec3& light, std::int32_t subtype, float scale, float worldClock,
                                      Render::Models::CrtRandom& random);
    // Pool lleno es éxito sin índice; un binding no implementado es error explícito.
    [[nodiscard]] Core::Expected<std::optional<std::size_t>, ParticleError>
    CreateChecked(std::int32_t type, const glm::vec3& position, const glm::vec3& angles, const glm::vec3& light,
                  std::int32_t subtype, float scale, float worldClock, Render::Models::CrtRandom& random,
                  std::uint32_t owner = 0);
    // Reloj prestado: 5f2/4 lo carga después de R2; 6/7/8 no lo leen al crear.
    [[nodiscard]] Core::Expected<std::optional<std::size_t>, ParticleError>
    CreateChecked(std::int32_t type, const glm::vec3& position, const glm::vec3& angles, const glm::vec3& light,
                  std::int32_t subtype, float scale, std::reference_wrapper<const float> worldClock,
                  Render::Models::CrtRandom& random, std::uint32_t owner, IParticleTrigonometry& trigonometry);
    // Sólo las familias Kalima acreditadas. Conserva identidad por valor, nunca una dirección host.
    [[nodiscard]] Core::Expected<std::optional<std::size_t>, ParticleError>
    CreateKalimaChecked(std::int32_t type, const glm::vec3& position, const glm::vec3& angles, const glm::vec3& light,
                        std::int32_t subtype, float scale, float worldClock, Render::Models::CrtRandom& random,
                        std::int32_t scene, ParticleOwner owner = {});
    // CreateKalimaChecked agrega5ac/6,7,6ed/0,6f0..4/0 y6f4/1; escena prestada para6f1.
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
               std::int32_t subtype, float scale, float legacyClock, const float* worldClock,
               Render::Models::CrtRandom& random, std::uint32_t owner, IParticleTrigonometry* trigonometry,
               std::optional<std::int32_t> scene = std::nullopt, ParticleOwner typedOwner = {});
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
std::optional<ParticleError> StepParticles(Particles& particles, Render::Models::CrtRandom& random);
// Para 67 y 68, 4303c0 consulta el mapa raw32 al tratar cada slot. Step conserva sus continuaciones
// subtype1 (campo +0x50 a escala) y !=1 aunque Create sólo admita /0; no agregar una guarda común /0.
// Un binding no admitido falla después de viento y vida/movimiento del slot actual. Sin mapa, 67/68
// fallan además después de cambiar escala y antes de RGB. El error conserva todos los efectos del
// prefijo y slots anteriores, y deja los posteriores sin visitar: no es transaccional. Repetir el tick
// consume viento y avanza otra vez los slots anteriores. No copia ni posee WorldState.
[[nodiscard]] Core::Expected<void, ParticleError> StepParticlesChecked(Particles& particles,
                                                                       Render::Models::CrtRandom& random,
                                                                       const std::int32_t* map = nullptr,
                                                                       const ParticleTerrainHeight& terrainHeight = {});
// Carga el reloj por slot6 después del movimiento común, sin snapshot de la pasada.
[[nodiscard]] Core::Expected<void, ParticleError>
StepParticlesChecked(Particles& particles, Render::Models::CrtRandom& random,
                     std::reference_wrapper<const float> worldClock, const std::int32_t* map,
                     IParticleTrigonometry& trigonometry, const ParticleTerrainHeight& terrainHeight = {});
} // namespace Client::World
