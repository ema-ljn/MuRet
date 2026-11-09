#pragma once

#include "Core/Expected.h"
#include "Render/Models/OriginalRotation.h"
#include "World/TerrainHeight.h"

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <system_error>
#include <vector>

namespace Client::Data::Terrain
{
struct TerrainAttributes;
struct TerrainHeights;
} // namespace Client::Data::Terrain

namespace Client::Render::Models
{
class CrtRandom;
} // namespace Client::Render::Models

// Las hojas (CL7.6 B3): la tabla de 0x530490 (0x75e34b8, 200 lugares de 0x78 bytes), que en cada mapa son
// otra cosa (lluvia, hojas, nieve, burbujas); en el login (el mapa 0x37) son 80 brasas de
// Effect\FireSnuff.jpg que nacen alrededor del héroe (0x42b1e0), caen con su velocidad al azar
// (0x5303a0) y en el suelo se apagan, y su dibujo, 0x5306a0. Contrato: 06 § Los efectos del login.
// Verificado bit a bit contra el código real emulado (tests/vectors/world55/leaves.txt).
namespace Client::World
{
struct LeafWeather;
class ILeafWaveEvaluator;
inline constexpr std::size_t LeafCount = 200;
// Cuántas usa el mapa 0x37 (0x5304e6).
inline constexpr std::size_t LoginEmberCount = 80;
inline constexpr std::uint16_t EmberTexture = 0x635;

// Predicate 0x43ff10: only raw map 31 selects this leaf creation branch.
[[nodiscard]] bool LeafMap31Predicate(std::int32_t map);
// Predicate 0x42ee20: AL is set exactly when the raw map DWORD is not 35.
[[nodiscard]] bool LeafMapIsNot35(std::int32_t map);
// Predicate 0x4303c0: only raw map 34 produces AL=1.
[[nodiscard]] bool LeafMap34Predicate(std::int32_t map);

struct LeafWorldBoundsView
{
    const float& lowerX;
    const float& upperX;
    const float& lowerY;
    const float& upperY;
};

// Callers must keep borrowed values finite and stable for this synchronous call. Non-finite values
// return invalid_argument; external writers cannot be excluded by this view. 0x425090's AL is
// represented only for that domain; EAX high bits and x87 status/flags are not.
[[nodiscard]] Core::Expected<bool, std::error_code>
LeafPointWithinFiniteStableBounds(const float& x, const float& y, const LeafWorldBoundsView& bounds);

struct Leaf
{
    std::uint8_t alive = 0; // +0x0
    std::int32_t type = 0;  // +0x4, la textura
    float scale = 0.0f;     // +0x10
    glm::vec3 position{};   // +0x14
    glm::vec3 angles{};     // +0x20: las brasas no los escriben (Q-C139)
    glm::vec3 light{};      // +0x2c
    glm::vec3 velocity{};   // +0x54
    glm::vec3 spin{};       // +0x60: el creador los sortea; las brasas no los usan
    glm::vec3 start{};      // +0x6c, donde nació
};

// C3/52fa60 borrows the raw map, rereadable canonical hero pointer, camera, and four live FP constants.
struct LeafC3World
{
    const std::int32_t& map;
    const glm::vec3* const& hero;
    const float& cameraY;
    const float& scale;
    const float& threshold;
    const float& mirror;
    const float& step;
};

// Each invocation supplies one signed return from the shared RNG target 69fd70. Production callers must
// bind the same canonical RNG stream; injected draws in tests specify this body's input, not RNG equivalence.
using LeafRandomDraw = std::function<Core::Expected<std::int32_t, std::error_code>()>;
using LeafHeroRead = std::function<Core::Expected<const glm::vec3*, std::error_code>()>;

// C3 admits only raw map0, then consumes nine draws in contract order. Missing reached draws or hero
// storage return an error after preserving the executed prefix. The caller must supply live borrowed
// values and the legacy FP profile it can support; no x87 status, flags, exceptions, or RNG algorithm is emulated.
[[nodiscard]] Core::Expected<std::uint8_t, std::error_code> CreateMapZeroLeafC3(Leaf& leaf, const LeafC3World& world,
                                                                                const LeafRandomDraw& draw);
// Resuelve el héroe canónico después deR1/R2/R3; presta G y las constantes cerradas de52fa60.
// cameraY nulo falla al alcanzar G trasR4, conservando el prefijo; no representa un valor0.
[[nodiscard]] Core::Expected<std::uint8_t, std::error_code> CreateMapZeroLeafC3(Leaf& leaf, const std::int32_t& map,
                                                                                const float* cameraY,
                                                                                const LeafHeroRead& readHero,
                                                                                const LeafRandomDraw& draw);

class Leaves
{
public:
    [[nodiscard]] std::span<Leaf> All();
    [[nodiscard]] std::span<const Leaf> All() const;
    [[nodiscard]] bool Any() const;
    // 0x6070a0 al entrar: el byte vivo de cada una.
    void Clear();

private:
    // En el heap: la escena que la lleva tiene que caber en la pila.
    std::vector<Leaf> m_leaves = std::vector<Leaf>(LeafCount);
};

// Fronteras síncronas de530490. Core enlaza los creadores/actualizadores acreditados con su mismo
// contexto vivo/RNG; no registrar callbacks no-op para completar dependencias pendientes.
using LeafCreator = std::function<Core::Expected<std::uint8_t, std::error_code>(Leaf&, std::int32_t)>;
using LeafUpdater = std::function<Core::Expected<std::uint8_t, std::error_code>(Leaf&)>;
using LeafFall = std::function<Core::Expected<void, std::error_code>(Leaf&)>;

struct LeafPoolHandlers
{
    std::array<LeafCreator, 9> creators; // C1..C9 en orden nativo, AL8 corta la cadena.
    std::array<LeafUpdater, 3> updaters; // U1..U3 en orden nativo.
    LeafCreator fallback;                // 435600, solo con selector tardío2; AL ignorado.
    LeafFall fall;                       // U4/5303a0, solo al agotar U1..U3.
};

// Visita un prefijo ya preparado. El handler ausente/fallido corta con error donde se alcanza,
// conservando stores/calls previos. prefix>200 devuelve invalid_argument antes de tocar el pool.
[[nodiscard]] Core::Expected<void, std::error_code>
VisitLeafPool(Leaves& leaves, std::size_t prefix, const std::int32_t& selector, const LeafPoolHandlers& handlers);

// Composición530490: captura mapa/prefijo, acerca clima, ondas/phase y después pool. No copia el
// reloj, RNG ni estado persistente. La disponibilidad del evaluador/handlers es explícita, sin defaults
// productivos falsos; los tests de composición no certifican los servicios numéricos/de gameplay.
[[nodiscard]] Core::Expected<void, std::error_code>
StepWorldLeaves(Leaves& leaves, LeafWeather& weather, const std::int32_t& map, const std::int32_t& selector,
                const float& time, ILeafWaveEvaluator* evaluator, const LeafPoolHandlers& handlers);

// Lo que leen las brasas: la posición del héroe (0x743a170 + 0x10), la y de la cámara (0x7d36888) y el
// terreno, para 0x5f93c0.
struct EmberWorld
{
    glm::vec3 hero{};
    float cameraY = 0.0f;
    const Data::Terrain::TerrainHeights* heights = nullptr;
    const Data::Terrain::TerrainAttributes* attributes = nullptr;
    float fixedHeight = FixedTerrainHeight;
};

// Vistas prestadas al contexto vivo: 0x52fa60 relee al héroe después de cada uno de los tres draws
// de posición y la cámara después del primer store de velocidad. No posee otro RNG ni otro héroe.
struct LeafCreationWorld
{
    const std::int32_t& map;
    const glm::vec3& hero;
    const float& cameraY;
};

struct RainLeafWorld
{
    const std::int32_t& map;
    const std::int32_t& selector;
    const glm::vec3& hero;
    const LeafWeather& weather;
};

// C1/C2/C4 de530490. index es signed, también en las ramas >=300 fuera del prefijo ordinario.
// Las ondas son entradas vivas acreditadas por el caller, no se fabrican en estos creadores.
[[nodiscard]] bool CreateExtendedRainLeaf(Leaf& leaf, const RainLeafWorld& world, std::int32_t index,
                                          Render::Models::CrtRandom& random);
[[nodiscard]] bool CreateEventRainLeaf(Leaf& leaf, const RainLeafWorld& world, std::int32_t index,
                                       Render::Models::CrtRandom& random);
[[nodiscard]] bool CreateIcarusRainLeaf(Leaf& leaf, const RainLeafWorld& world, std::int32_t index,
                                        Render::Models::CrtRandom& random);

// C3 de 0x530490: solo mapa0, nueve draws y escrituras parciales; AL indica admisión, no vida.
// No escribe alive, escala, ángulos ni luz: los defaults pertenecen al dispatcher.
[[nodiscard]] bool CreateLorenciaLeaf(Leaf& leaf, const LeafCreationWorld& world, Render::Models::CrtRandom& random);

// C5/52fd20: mapa2, cinco draws; tipo100/escala5 o tipo101/escala10.
[[nodiscard]] bool CreateSnowLeaf(Leaf& leaf, const LeafCreationWorld& world, Render::Models::CrtRandom& random);
// 52fd20 no lee cámara: préstamo mínimo del caller de juego, sin snapshots de mapa ni héroe.
[[nodiscard]] bool CreateSnowLeaf(Leaf& leaf, const std::int32_t& map, const glm::vec3& hero,
                                  Render::Models::CrtRandom& random);

// C6/52fe30: mapas3/7, nueve draws; conserva escala, ángulos, luz y byte vivo.
[[nodiscard]] bool CreateNoriaAtlansLeaf(Leaf& leaf, const LeafCreationWorld& world, Render::Models::CrtRandom& random);

// C7/42b1e0: mapa55 sin filtro; mapa30 con byte vivo4294b0 no-cero y rectángulo cerrado.
// castlePresentation es la entrada raw8 de0x7239ec; su significado/reglas de evento son del caller.
[[nodiscard]] bool CreateEmberLeaf(Leaf& leaf, const LeafCreationWorld& world, const std::uint8_t& castlePresentation,
                                   Render::Models::CrtRandom& random);

// Rama genérica de 0x5301a0 (0x530241..0x530394): seis draws, posición y ángulos, baja a distancia²
// >=200000. El dispatcher debe haber admitido mapa0/10 y excluido tipo0x66; no es el handler completo.
// No requiere alive: una baja anterior no interrumpe esta rama. No hace consulta ni emite partículas.
void StepDriftingLeaf(Leaf& leaf, Render::Models::CrtRandom& random);

// Dependencias prestadas de 0x5f93c0. scene es el ID raw compartido, no el mapa; 0/1/3 retornan0.
struct LeafTerrainWorld
{
    const std::int32_t& scene;
    const Data::Terrain::TerrainHeights& heights;
    const Data::Terrain::TerrainAttributes& attributes;
    const float& fixedHeight;
};

// U4 de 0x530490 (0x5303a0), sin guardas de mapa/tipo/alive: consulta, caída o fade y baja.
void StepFallingLeaf(Leaf& leaf, const LeafTerrainWorld& world, Render::Models::CrtRandom& random);

// Frontera53fe70 de5301a0: referencias a los tres vectores interiores vivos, sin copiar sus datos.
struct LeafSplashRequest
{
    glm::vec3& position;
    glm::vec3& angles;
    glm::vec3& light;
    std::int32_t type = 0x67;
    std::int32_t subtype = 0;
    float scale = 1.0f;
    std::uint32_t owner = 0;
};

using LeafSplash = std::function<std::error_code(const LeafSplashRequest&)>;

// Contexto vivo mínimo para U1/U2, sin snapshots de mapa/selector ni estado meteorológico inventado.
struct RainLeafMotionWorld
{
    const std::int32_t& map;
    const std::int32_t& selector;
    const LeafTerrainWorld& terrain;
};

// Frontera5f82c0: recibe XY releídos tras altura y entrega AL8; bit3 veta el contacto deU2.
// El binding productivo exacto está pendiente de su contrato, no sustituirlo por cero/no-op.
using LeafImpactAttributes = std::function<Core::Expected<std::uint8_t, std::error_code>(float, float)>;

// U1/52ffe0: mapa9 o34/selector1; H retenida, contacto orderedH>Z, un draw y servicio67/68.
[[nodiscard]] Core::Expected<bool, std::error_code> StepExtendedRainLeaf(Leaf& leaf, const RainLeafMotionWorld& world,
                                                                         Render::Models::CrtRandom& random,
                                                                         const LeafSplash& splash);

// U2/5300c0: mapas18..23; H se derramaf32; Z<H o unordered consulta5f82c0 antes de baja/RNG.
// Ambas rutas emiten67 siR%4==0,68 si no; el servicio debe usar el MISMO pool/RNG canónico.
[[nodiscard]] Core::Expected<bool, std::error_code> StepEventRainLeaf(Leaf& leaf, const RainLeafMotionWorld& world,
                                                                      Render::Models::CrtRandom& random,
                                                                      const LeafImpactAttributes& impactAttributes,
                                                                      const LeafSplash& splash);

// U3 completo funcional. Value es el byte de admisión: false permite U4, true corta aun tras baja.
// Un impacto fuera de10 exige splash: binding vacío/fallido devuelve error y conserva stores previos.
// El binding productivo a Particles67 pertenece a su owner; no hay implementación no-op por defecto.
[[nodiscard]] Core::Expected<bool, std::error_code> StepGameplayLeaf(Leaf& leaf, const std::int32_t& map,
                                                                     const LeafTerrainWorld& world,
                                                                     Render::Models::CrtRandom& random,
                                                                     const LeafSplash& splash);

// 0x530490 en el mapa 0x37, con el rand() de la CRT: las 80 primeras, en orden; la apagada nace (con la luz
// en 1 y el byte vivo, 0x42b1e0) y la viva cae (0x5303a0), las dos en el mismo paso para lugares
// distintos. Sin el terreno no corre.
void StepLoginEmbers(Leaves& leaves, const EmberWorld& world, Render::Models::CrtRandom& random);

// Lo que 0x5306a0 le pide a GL por una brasa viva: glTranslatef a su posición y el cuadrado de 0x621320
// con a = 2 · escala y b = 4 · escala, sus cuatro esquinas por AngleMatrix de los ángulos (0x5fccc0), con
// las UV (0, 1), (1, 1), (1, 0) y (0, 0).
struct LeafQuad
{
    glm::vec3 translation{};
    std::array<glm::vec3, 4> corners{};
    std::array<glm::vec2, 4> uv{};
};

enum class LeafBlend : std::uint8_t
{
    Alpha,
    Additive,
};

enum class LeafGeometryKind : std::uint8_t
{
    Quad,
    Suppressed,
};

struct LeafGeometry
{
    LeafGeometryKind kind;
    glm::vec2 dimensions;
};

// Decisiones propias de5306a0; no son el drawing ni sus servicios. El blend usa el mapa inicial.
// LeafGeometryOf es SOLO la ruta genérica, ya elegida con M1!=2: recibe M2 después de la matriz.
// Si M2 cambió a2, no reconsidera billboard. Su ruta se decide antes, con la lectura M1 tras binder.
[[nodiscard]] LeafBlend LeafBlendOf(std::int32_t initialMap);
[[nodiscard]] LeafGeometry LeafGeometryOf(const Leaf& leaf, std::int32_t map, std::int32_t selector);
// Geometría621320 con A/B independientes y matriz prestada; translation ya fue capturada alTranslate.
[[nodiscard]] LeafQuad LeafQuadOf(const glm::vec3& translation, const Render::Models::OriginalMatrix& matrix,
                                  glm::vec2 dimensions);
[[nodiscard]] LeafQuad LeafQuadOf(const Leaf& leaf);
} // namespace Client::World
