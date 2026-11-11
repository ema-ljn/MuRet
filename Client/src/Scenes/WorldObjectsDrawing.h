#pragma once

#include "Core/Expected.h"
#include "Core/IClock.h"
#include "Render/ModelBatch.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/ModelPoseCallbacks.h"
#include "World/TerrainCulling.h"
#include "World/ModelEffects.h"
#include "World/GroundEffectDrawing.h"
#include "World/SphereFrustum.h"
#include "World/Leaves.h"
#include "World/ParticleDrawing.h"
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
#include <string>

// Los objetos de un mundo en una escena (CL7.2): su paso (0x606560) y su dibujo (0x6046a0), con los
// modelos de la escena. La lógica de cada uno es de World::WorldObjects. Contrato: 06 § El mundo del
// login.
namespace Client::World
{
class KalimaWorld;
struct FishEffectRequest;
struct ObjectModelStepState;
} // namespace Client::World

namespace Client::Scenes
{
struct ActorState;
struct FaunaDrawCarry;
// El paso de un objeto que tiene el suyo (el logo del login): devuelve true si lo avanzó, y entonces
// StepWorldObject no lo toca.
using SpecialObjectStep = std::function<bool(World::WorldObject&)>;

// 0x606560: World::StepWorldObject sobre los que se vieron en el último dibujo (+0x16c), con el
// esqueleto de su tipo, sector por sector como el dibujo (World::DrawOrder). Los que no se vieron
// quedan como están (Q-C135). `special`, si está, va primero con cada uno.
void StepWorldObjects(std::span<World::WorldObject> objects, std::uint8_t map, const IModelLibrary& models,
                      const SpecialObjectStep& special = {});

// 0x6063d0 con cada objeto, después de su 0x604ab0: lo que el mapa hace con él (Icarus).
using AfterObjectStep = std::function<Core::Expected<void, std::string>(World::WorldObject&)>;

// 0x606560 en el juego (0x63706a): World::StepWorldObjectChecked sobre los vivos que se vieron en el último
// dibujo, en World::DrawOrder, con el esqueleto de su tipo y el rand() de la CRT, y `after` (0x6063d0) con
// cada uno, si está. 0x6061c0 al empezar va antes, en la escena; 0x5ff960 con cada uno y el 0x6c9ddc-- del
// final no hacen nada fuera de los eventos (06 § Los objetos del juego). Corta en el primer objeto cuyo caso
// MuRet no tiene, con los anteriores ya avanzados.
[[nodiscard]] Core::Expected<void, std::string> StepGameWorldObjects(std::span<World::WorldObject> objects,
                                                                     std::uint8_t map, const IModelLibrary& models,
                                                                     Render::Models::CrtRandom& random,
                                                                     const World::ObjectStepGlobals& globals,
                                                                     const AfterObjectStep& after = {});

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
    std::function<Core::Expected<void, std::string>(const DrawnObject&)> onPose{};
    const FaunaDrawCarry* retainedPose = nullptr;
    bool publishProjectedPose = false;
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

// Lo que 0x6046a0 lee afuera de los objetos en el juego.
struct GameObjectPass
{
    // DAT_006c9dd0.
    std::uint8_t map = 0;
    // Si el dibujo es de un frame del original: 0x602ed0 saca su rand() (0x602ef4) por cada objeto que
    // llega y el 0x64 de Devias abre el registro propio. Un dibujo intermedio de MuRet solo dibuja.
    bool originalFrame = true;
    Render::Models::CrtRandom* random = nullptr;
    // Lo que mira el 0x64 de Devias: la clase del héroe (+0x1d4), el nivel del registro propio (+0xe) y la
    // cuenta de los accesos abiertos de ese registro (WorldRuntime::ownRecordDepth).
    std::uint8_t heroClass = 0;
    std::uint16_t ownLevel = 0;
    std::uint8_t* ownRecordDepth = nullptr;
    // 0x5767b88: el reloj del mundo en ms, del que el 0x69 de Lorencia saca su V (0x603018).
    float worldClock = 0.0f;
    // La tabla global compartida de 0x743a198. Sin ella queda el corte histórico de la primera gota.
    World::Particles* particles = nullptr;
    // 5ffdf0 publica el scratch de la pose antes de 602ed0 y antes de las aves.
    std::function<Core::Expected<void, std::string>(const DrawnObject&)> onPose{};
    World::FrameSprites* sprites = nullptr;
    const FaunaDrawCarry* retainedPose = nullptr;
    std::function<Core::Expected<void, std::string>(const World::FishEffectRequest&)> createJoint{};
    const World::ObjectModelStepState* modelState = nullptr;
    // Publicación sobre scratch de presentación del caller, sin ejecutar602ed0 ni consumir RNG.
    bool publishProjectedPose = false;
    // DAT_07d2d360, prestado por el pulso global637700: sin productor no se inventa un frame0.
    std::optional<std::uint32_t> waterTextureFrame{};
    const ActorState* hero = nullptr;
    const World::KalimaWorld* kalima = nullptr;
    std::function<void(std::int32_t)> requestSound{};
    // 0x518cf0: los efectos de modelo, para la mariposa 0xae del 0x3c del 54 (0x4257d5).
    World::ModelEffects* effects = nullptr;
    // 0x6b8544 (timeGetTime), que el 0x3c del 54 y el 0x46 del 33 leen en el dibujo (Q-C304).
    std::function<std::uint32_t()> systemTime{};
    // DAT_006d5dc8: con 4 (la selección), la mariposa sale oculta y el 6f1/2 con luz 0,25.
    std::int32_t scene = 0;
};

// 0x6046a0 en el juego (0x6374ce), en los mapas 0 a 4, 6 a 8, 10, 31, 33 y Kalima (06 § Los objetos del juego):
// sector por sector, en World::DrawOrder, cada objeto vivo que pasa el trapecio como en DrawWorldObjects queda visto.
// El 0x64 de Devias sigue solo con el nivel del registro propio desde 0x50 (0x35 con la clase 3 o 4), con su acceso.
// Después, 0x602e90: el dibujo de World::DrawOf, sin interpolar la animación (el 0x69 de Lorencia, con
// DrawWaterspout); y, con un frame del original, 0x602ed0: un rand() y el caso de su tipo. El del 0x69 es
// StepGameWaterspoutFrame, con el pool compartido de `pass.particles`: los dos 5f2/0 sobre los huesos
// recién posados, con sus rand() intercalados. Un hueso no escrito necesita la pose global anterior y
// corta después de sus sorteos; sin pool conserva el corte histórico en 0x6030c7. El 0x38 del 0 deja sus dos
// sprites en NaN (Scenes/ObjectFrameSprites.h). En el 4, el pase con Chrome01 antes del cuerpo y los sprites
// de los huesos; en el 8, el cromo del 0x51 y los sprites, humos y piedritas de Tarkan; en el 10, el margen −10 del
// trapecio y las nubes y el 5ac/0 de Icarus; en el 33 y el 54, los casos de 0x425260 y, en cada dibujo, la
// malla 0 del 0x29 y del 0x41/0x42 (DrawAidaMeshPass). Los otros casos de 0x602ed0 (el
// 0x64 y el 0x67 del 2) cortan con su dirección. Los objetos de antes quedan dibujados. Kalima24..29/36 reutiliza la
// pose y el cuerpo públicos43948d, sus cáusticas de DAT_07d2d360, el calco0x22 y el sprite del tipo12; los productores
// aún ausentes de438c10 conservan su dirección y prefijo. Los demás mapas cortan antes de mirar un objeto.
[[nodiscard]] Core::Expected<void, std::string>
DrawGameWorldObjects(Render::ModelBatch& batch, const IModelLibrary& models, std::span<World::WorldObject> objects,
                     const World::TerrainPolygon& polygon, const World::TerrainLighting* lighting,
                     Core::Duration frameClock, const GameObjectPass& pass);

// 0x52dc20 (0x6374d3): por cada lugar vivo y con el +0x16c retenido, un rand() común, la mezcla
// aditiva y el caso del tipo. Conserva los efectos de los lugares anteriores si uno corta. Un pase del
// original; GroundEffectFrame conserva la geometría entre frames de MuRet (D-C4).
[[nodiscard]] Core::Expected<void, std::string> DrawGroundEffects(Render::ModelBatch& batch,
                                                                  std::span<World::ModelEffect> effects,
                                                                  const World::GroundEffectResources& resources);

// 0x52d220: World::StepModelEffect sobre cada efecto vivo, con el esqueleto de su modelo.
void StepModelEffects(std::span<World::ModelEffect> effects, const IModelLibrary& models);

// 0x52d620(0) con los efectos vivos: un RenderBody(2, 1, −1, 1, 0, 0, −1, −1) por cada uno, opaco, con
// el color de la luz del terreno en su lugar (`lighting`, o 0 sin ella) más la suya, la luz de cada
// normal y el frame entero (06 § El dragón de fondo); la posición, `alpha` de un paso más con lo que se
// movió en el último (D-C4: el original la dibuja tal cual, a 25 por segundo). Primero publica +0x16c
// con la esfera de 400, sobre la posición lógica, aun si el modelo no se encuentra. Un lugar muerto
// conserva su marca; uno vivo fuera de la esfera no consulta el modelo.
void DrawModelEffects(Render::ModelBatch& batch, const IModelLibrary& models, std::span<World::ModelEffect> effects,
                      const World::SphereFrustum& frustum, const World::TerrainLighting* lighting,
                      Core::Duration frameClock, float alpha);
// Mismo recorrido legacy, publicando la pose y consumiendo ese scratch sin volver a posar.
[[nodiscard]] Core::Expected<void, std::string>
DrawModelEffects(Render::ModelBatch& batch, const IModelLibrary& models, std::span<World::ModelEffect> effects,
                 const World::SphereFrustum& frustum, const World::TerrainLighting* lighting, Core::Duration frameClock,
                 float alpha, const TailPoseObserver& onPose, const TailMeshProvider& posedMeshes);

// 0x549440(0), en el pase de ojo (después de 0x6213e0, con la modelview en la identidad): cada sprite
// vivo, con la mezcla de su +4 (0 aditiva, 1 la de 0x620b10, 2 la normal, 3 la de 0x620b90:
// ONE_MINUS_SRC_COLOR/ONE; la textura 0x637, la normal), el alpha de 0x549290 y el cuadrado de 0x621400 con el
// tamaño de su textura (`textures`) por la escala y el alpha, con la vista `view` (0x7d366f0). Después de
// dibujarlo, se apaga en modos0/2. Modo1 retiene y omite z>350; modo2 apaga z<=300/NaN sin dibujar.
void DrawFrameSprites(Render::ModelBatch& batch, World::FrameSprites& sprites, const IModelLibrary& textures,
                      const Render::Models::OriginalMatrix& view, std::uint8_t mode = 0);

// 0x548550(mode), en el mismo pase de ojo, después de los sprites: el contrato único de World::
// DrawParticlesChecked, con los servicios de la escena (ParticleDrawServices) sobre el adaptador del lote
// (Render::ModelBatchParticleBackend). Las dimensiones, el material y el color salen de la metadata de
// `textures` (Recurso+20/+24/+28) y el binder, de su TextureSlotOf; el cuadrado de 0x621400, de la vista
// `view`. La rotación de la neblina cambia con `worldClock` en ms y el índice real del pool. Un lugar
// que falla corta el pase después de su prefijo, sin rollback: los anteriores quedan en el lote y el error
// dice cuál fue.
[[nodiscard]] Core::Expected<void, World::ParticleDrawingFailure>
DrawParticles(Render::ModelBatch& batch, World::Particles& particles, const IModelLibrary& textures,
              const Render::Models::OriginalMatrix& view, float worldClock = 0.0f, std::uint8_t mode = 0);

// 0x5306a0, en el pase del mundo, después de los efectos con modelo: las hojas vivas, aditivas (en el mapa
// 0x37, 0x620a90) y con el blanco de un solo glColor3f(1, 1, 1), cada una el cuadrado de LeafQuadOf
// trasladado a su posición, con la textura de su tipo.
void DrawLeaves(Render::ModelBatch& batch, const World::Leaves& leaves);
} // namespace Client::Scenes
