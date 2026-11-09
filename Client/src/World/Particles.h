#pragma once

#include "Core/Expected.h"

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

// Las partículas (CL7.6 B2): la tabla de 0x53fe70 (0x743a198, 3.000 lugares de 0x78 bytes), su paso,
// 0x5446e0, con el viento de 0x7609bb8, y lo que su dibujo, 0x548550(0), le pide a 0x621400. De sus
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
inline constexpr std::int32_t FireParticle = 0x5d9;
inline constexpr std::int32_t DropParticle = 0x607;
inline constexpr std::int32_t WorldSmokeSubtype = 2;
inline constexpr std::uint16_t BreathParticle = 0x5f2;
// La chispa del cuervo (Effect\Spark03.jpg), con el subtipo 5 (06 § El cuervo).
inline constexpr std::uint16_t SparkParticle = 0x5c6;
inline constexpr std::int32_t RavenSparkSubtype = 5;
inline constexpr std::int32_t MapSparkSubtype = 9;
inline constexpr std::uint16_t CloudParticle = 0x622;
inline constexpr std::int32_t LoginCloudSubtype = 10;

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
};

class Particles
{
public:
    // 0x53fe70(tipo, posición, ángulos, luz, subtipo, escala, dueño) en el primer lugar libre, con el reloj
    // del mundo (DAT_05767b88, en ms) y el rand() de la CRT. Con los 3.000 llenos no se crea ni saca
    // rand(). Se aceptan 5f2/0,2, 5c6/5,9, 622/8,10, 5d9/0..3, 607/0 y 67/0; Create/Draw admiten 68/0.
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
               Render::Models::CrtRandom& random, std::uint32_t owner, IParticleTrigonometry* trigonometry);
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
// subtype1 (campo +0x50 a escala) y !=1 aunque Create/Draw sólo admitan /0; no agregar una guarda común /0.
// Un binding no admitido falla después de viento y vida/movimiento del slot actual. Sin mapa, 67/68
// fallan además después de cambiar escala y antes de RGB. El error conserva todos los efectos del
// prefijo y slots anteriores, y deja los posteriores sin visitar: no es transaccional. Repetir el tick
// consume viento y avanza otra vez los slots anteriores. No copia ni posee WorldState.
[[nodiscard]] Core::Expected<void, ParticleError>
StepParticlesChecked(Particles& particles, Render::Models::CrtRandom& random, const std::int32_t* map = nullptr);
// Carga el reloj por slot6 después del movimiento común, sin snapshot de la pasada.
[[nodiscard]] Core::Expected<void, ParticleError> StepParticlesChecked(Particles& particles,
                                                                       Render::Models::CrtRandom& random,
                                                                       std::reference_wrapper<const float> worldClock,
                                                                       const std::int32_t* map,
                                                                       IParticleTrigonometry& trigonometry);

// DTO geométrico/material de 548550(0), sin lookup ni servicios GL. Slot+08 contiene el índice de
// imagen I0; Slot+04, el tipo T0. El registro de recurso (stride nativo56) aporta dimensiones f32 en
// Recurso+20/+24 por I0 y byte selector de material en Recurso+28 por T0, antes del primer servicio.
// materialFlag==3 pide mezcla aditiva (620a90); !=3 pide 620a00(0), sin deducir formato del loader.
// La neblina cambia después a mezcla sustractiva (620b10). Contrato 548550, prefijo dimensiones/material.
struct ParticleDraw
{
    enum class Blend : std::uint8_t
    {
        Alpha,
        Additive,
        Subtractive,
    };

    std::int32_t texture = 0;
    Blend blend = Blend::Additive;
    glm::vec3 position{};
    float width = 0.0f;
    float height = 0.0f;
    glm::vec3 light{};
    float rotation = 0.0f;
    glm::vec2 uv{};
    glm::vec2 uvSize{1.0f, 1.0f};
};

// La textura por la escala, girada +0x44, con toda la textura. La neblina sobreescribe +0x44 con el
// reloj, +0x60, la paridad del índice real en el pool y start.y (+0x70), sin normalizar el ángulo.
// 5d9 usa frame int32 de +48, ancho/4 y rotación angles.x; 5f2/2 usa mezcla sustractiva.
// 607 usa atlas3x3 de +48, inset/extensión nativos y rotación0; 622/8 escala RGB temporal por +38.
// La API legacy recibe valores del caller, incluidos sus literales; no acredita un binding de recursos.
// Los lugares inactivos y los tipos que MuRet no arma devuelven nullopt (compatibilidad login).
[[nodiscard]] std::optional<ParticleDraw> ParticleDrawOf(Particle& particle, const glm::vec2& textureSize,
                                                         int materialFlag, float worldClock = 0.0f,
                                                         std::size_t slotIndex = 0);
// Binding obligatorio por valor: textureSize viene de Recurso+20/+24 indexado por Slot+08;
// materialFlag viene de Recurso+28 indexado por Slot+04. No es el flag de color tardío de 621400,
// que lee ese mismo Recurso+28 por imagen después de glBegin. El provider debe respetar índices y
// momentos; este DTO no implementa los lookups ni acredita tamaño/contenido runtime de la tabla.
// mode consume sólo su byte bajo: 1 permite Z<=350/unordered; 2 permite Z>300 ordered.
// Recursos ausentes son errores; !=3 no implica un formato RGBA concreto.
[[nodiscard]] Core::Expected<std::optional<ParticleDraw>, ParticleError>
ParticleDrawChecked(Particle& particle, std::optional<glm::vec2> textureSize, std::optional<std::uint8_t> materialFlag,
                    float worldClock = 0.0f, std::size_t slotIndex = 0, std::uint8_t mode = 0);
} // namespace Client::World
