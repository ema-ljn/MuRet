#pragma once

#include "Core/IClock.h"
#include "Core/Expected.h"
#include "World/WorldObjectDrawingError.h"
#include "Render/ModelBatch.h"
#include "Scenes/CharacterDrawing.h"
#include "World/TerrainCulling.h"
#include "World/ModelEffects.h"
#include "World/Leaves.h"
#include "World/Particles.h"
#include "World/Sprites.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/OriginalRotation.h"
#include "World/TerrainLighting.h"
#include "World/WorldObjects.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <span>

// Los objetos de un mundo en una escena (CL7.2): su paso (0x606560) y su dibujo (0x6046a0), con los
// modelos de la escena. La lógica de cada uno es de World::WorldObjects. Contrato: 06 § El mundo del
// login.
namespace Client::Data::Terrain
{
struct TerrainHeights;
}

namespace Client::World
{
struct WorldObjectDrawingContext;
}

namespace Client::Scenes
{
// 602e90: el servicio5ffdf0 es obligatorio y puede mutar el slot objeto. Los otros tres slots
// se capturan por valor. No sustituye la preparación/pose por un bool sin efectos.
using WorldObjectBodyGate = std::function<Core::Expected<std::uint32_t, World::WorldObjectDrawingError>(
    World::WorldObject*&, std::uint32_t, std::uint32_t, std::uint32_t)>;
[[nodiscard]] Core::Expected<void, World::WorldObjectDrawingError>
DrawBoundWorldObject(World::WorldObject*& object, World::WorldObjectDrawingContext& context,
                     const WorldObjectBodyGate& gate, std::uint32_t argument2, std::uint32_t argument3,
                     std::uint32_t mode);

// El paso de un objeto que tiene el suyo (el logo del login): devuelve true si lo avanzó, y entonces
// StepWorldObject no lo toca.
using SpecialObjectStep = std::function<bool(World::WorldObject&)>;

// 0x606560: World::StepWorldObject sobre los que se vieron en el último dibujo (+0x16c), con el
// esqueleto de su tipo, sector por sector como el dibujo (World::DrawOrder). Los que no se vieron
// quedan como están (Q-C135). `special`, si está, va primero con cada uno.
void StepWorldObjects(std::span<World::WorldObject> objects, std::uint8_t map, const IModelLibrary& models,
                      const SpecialObjectStep& special = {});

// Lo que 0x602ed0 hace en el frame con cada objeto que llega a él, después de su dibujo (0x604a4d).
struct ObjectFrameEffects
{
    // El rand() de la CRT (el de la tela): 0x602ef4 saca uno por objeto y frame original, antes de mirar
    // la escena o el tipo. Sin él, 0x602ed0 no corre.
    Render::Models::CrtRandom* random = nullptr;
    // Donde van los sprites del farol (World::StreetLight); sin ellos no se piden.
    World::FrameSprites* sprites = nullptr;
    // Las nubes de 0x622/10 del marcador 0x54, solo en el mapa 55 (0x6044bc). Sin pool se consume
    // igualmente el rand de su guard, pero no los de creación.
    World::Particles* particles = nullptr;
    // Lo que el frame posó en el buffer global de la pose antes de los objetos, en su orden: el caballo
    // (0x4453b0).
    std::span<const PosedInTheBuffer> posedBefore{};
    // El caso del farol es de la escena 2 (0x602f09).
    World::ObjectScene scene = World::ObjectScene::Other;
    // Los frames originales que pasaron: el dibujo puro corre incluso con cero.
    int frames = 1;
    // El rand común del último frame original por índice; los no vistos en él pierden su muestra.
    // Sin una muestra previa no hay brillo que repetir.
    std::span<std::optional<int>> lastRandoms{};
};

// 0x6046a0 con `objects`: por sector, en World::DrawOrder; el sector que pasa el trapecio en su centro
// (x · 16 + 8, y · 16 + 8) con −180, y en él cada objeto que lo pasa en su lugar por 0,01f (en float) con
// su margen individual (+0xdc; −30 por defecto), queda visto (+0x16c guarda el resultado; los de un sector que no pasa
// quedan sin ver, como los apaga 0x606560) y se dibuja como dice World::DrawOf, con la luz del terreno en su lugar
// (`lighting`, o 0 sin ella) y su animación `alpha` de un paso más (Render::Models::Interpolated).
// Con las poses capturadas, `effects` avanza en orden frame original → objetos vistos: el rand común y
// el guard de nube del marcador 0x54 del mapa 55. Al final recrea los dos sprites del farol en la escena
// 2, con el último sorteo y los huesos 8 y 9 que tenía el buffer al visitarlo (LastPosedBonePoint): los
// objetos anteriores posados o `posedBefore`; sin ninguno, (0, 0, 0). Con cero frames se dibuja y se
// repite la luz de los faroles que tengan muestra del último frame original, sin consumir RNG.
void DrawWorldObjects(Render::ModelBatch& batch, const IModelLibrary& models, std::span<World::WorldObject> objects,
                      const World::TerrainPolygon& polygon, const World::TerrainLighting* lighting, std::uint8_t map,
                      Core::Duration frameClock, float alpha, const ObjectFrameEffects& effects = {});

// 0x52d220: World::StepModelEffect sobre cada efecto vivo, con el esqueleto de su modelo.
void StepModelEffects(std::span<World::ModelEffect> effects, const IModelLibrary& models);
// 52d220: los vivos avanzan aunque visible sea0. 61f/0 recibe el RNG del caller y no consulta BMD;
// el overload sin RNG conserva el paso histórico del dragón y omite llegadas.
void StepModelEffects(std::span<World::ModelEffect> effects, const IModelLibrary& models,
                      Render::Models::CrtRandom& random);

// Variante all-maps con las fronteras vivas de metadata/reloj de 5208a0.
Core::Expected<void, World::ModelEffectStepError> StepModelEffects(std::span<World::ModelEffect> effects,
                                                                   const IModelLibrary& models,
                                                                   Render::Models::CrtRandom& random,
                                                                   const World::ModelEffectStepBindings& bindings);

// El destino 602e90 se ejecuta sobre el registro vivo: 135 llama dos veces y persiste Z entre llamadas.
using ModelEffectBodyDraw = std::function<void(World::ModelEffect&)>;
void DrawModelEffectBody(World::ModelEffect& effect, const ModelEffectBodyDraw& drawBody);

// 0x52d620(0) con los efectos vivos: un RenderBody(2, 1, −1, 1, 0, 0, −1, −1) por cada uno, opaco, con
// el color de la luz del terreno en su lugar (`lighting`, o 0 sin ella) más la suya, la luz de cada
// normal y el frame entero (06 § El dragón de fondo); la posición, `alpha` de un paso más con lo que se
// movió en el último (D-C4: el original la dibuja tal cual, a 25 por segundo). La esfera de 400
// contra el frustum no va: en el login el dragón está siempre en pantalla.
void DrawModelEffects(Render::ModelBatch& batch, const IModelLibrary& models,
                      std::span<const World::ModelEffect> effects, const World::TerrainLighting* lighting,
                      Core::Duration frameClock, float alpha);

// La frontera de despacho de 52d620: false retorna del pase entero, conservando los bytes
// de los slots posteriores (p.ej. la salida de 5e8..5ea). Se llama sólo tras guardar AL no cero.
using ModelEffectVisitor = std::function<bool(World::ModelEffect&)>;
void VisitVisibleModelEffects(std::span<World::ModelEffect> effects, const World::ModelEffectPlanes& planes,
                              const ModelEffectVisitor& visitor);

// Pase con culling real: escribe +16c por slot activo alcanzado y dibuja sólo los que pasan.
// Debe correr DESPUÉS del ground, que lee la historia anterior. 61f no consulta BMD ni subtipo.
void DrawModelEffects(Render::ModelBatch& batch, const IModelLibrary& models, std::span<World::ModelEffect> effects,
                      const World::ModelEffectPlanes& planes, const World::TerrainLighting* lighting,
                      Core::Duration frameClock, float alpha);

enum class ModelEffectDrawErrorKind : std::uint8_t
{
    MissingModel,
    MissingSkeleton,
    MissingTextures,
};

struct ModelEffectDrawError
{
    ModelEffectDrawErrorKind kind;
    std::uint16_t model;
};

// Entrada all-maps: un binding ausente es un error observable, nunca un modelo ficticio o silencio.
Core::Expected<void, ModelEffectDrawError> DrawBoundModelEffects(Render::ModelBatch& batch, const IModelLibrary& models,
                                                                 std::span<World::ModelEffect> effects,
                                                                 const World::ModelEffectPlanes& planes,
                                                                 const World::TerrainLighting* lighting,
                                                                 Core::Duration frameClock, float alpha);

// 52dc20, antes de los personajes: un rand por registro alive+visible, y para 61f/0 dos
// superficies 53d2c0 en ±theta, seguidas del calco 5fb2f0 sobre las alturas directas del terreno.
// No consulta modelos BMD ni interpola posición; worldClock es el reloj float32 del original.
void DrawGroundModelEffects(Render::ModelBatch& batch, std::span<const World::ModelEffect> effects,
                            const Data::Terrain::TerrainHeights& heights, float worldClock,
                            Render::Models::CrtRandom& random);

// 620a90 es una frontera obligatoria: después de Next y antes de muestrear tipo, sin releer guardas.
using GroundModelEffectService = std::function<void()>;
Core::Expected<void, World::ModelEffectStepError>
DrawGroundModelEffects(Render::ModelBatch& batch, std::span<World::ModelEffect> effects,
                       const Data::Terrain::TerrainHeights& heights, float worldClock,
                       Render::Models::CrtRandom& random, const GroundModelEffectService& service);

// 0x549440(0), en el pase de ojo (después de 0x6213e0, con la modelview en la identidad): cada sprite
// vivo, con la mezcla de su +4 (0 aditiva, 1 la de 0x620b10, 2 la normal; la 3, 0x620b90, todavía no se
// leyó y va aditiva; la textura 0x637, la normal), el alpha de 0x549290 y el cuadrado de 0x621400 con el
// tamaño de su textura (`textures`) por la escala y el alpha, con la vista `view` (0x7d366f0). Después de
// dibujarlo, se apaga.
void DrawFrameSprites(Render::ModelBatch& batch, World::FrameSprites& sprites, const IModelLibrary& textures,
                      const Render::Models::OriginalMatrix& view);

// 0x548550(0), en el mismo pase de ojo, después de los sprites: cada partícula viva que MuRet arma, con la
// mezcla de World::ParticleDrawOf y el cuadrado de 0x621400 con el tamaño de su textura por la escala,
// con la vista `view`. Los JPG son aditivos, salvo las nubes 0x622/10, sustractivas. Su rotación cambia
// con `worldClock` en ms y el índice real del pool, incluidos los lugares apagados entre los vivos.
// Sin el tamaño de la textura no se dibuja.
void DrawParticles(Render::ModelBatch& batch, World::Particles& particles, const IModelLibrary& textures,
                   const Render::Models::OriginalMatrix& view, float worldClock = 0.0f);

// 0x5306a0, en el pase del mundo, después de los efectos con modelo: las hojas vivas, aditivas (en el mapa
// 0x37, 0x620a90) y con el blanco de un solo glColor3f(1, 1, 1), cada una el cuadrado de LeafQuadOf
// trasladado a su posición, con la textura de su tipo.
void DrawLeaves(Render::ModelBatch& batch, const World::Leaves& leaves);
} // namespace Client::Scenes
