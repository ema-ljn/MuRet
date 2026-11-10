#pragma once

#include "Core/Expected.h"
#include "Core/IClock.h"
#include "Render/ModelBatch.h"
#include "Scenes/CharacterDrawing.h"
#include "World/TerrainCulling.h"
#include "World/ModelEffects.h"
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
namespace Client::Scenes
{
// El paso de un objeto que tiene el suyo (el logo del login): devuelve true si lo avanzó, y entonces
// StepWorldObject no lo toca.
using SpecialObjectStep = std::function<bool(World::WorldObject&)>;

// 0x606560: World::StepWorldObject sobre los que se vieron en el último dibujo (+0x16c), con el
// esqueleto de su tipo, sector por sector como el dibujo (World::DrawOrder). Los que no se vieron
// quedan como están (Q-C135). `special`, si está, va primero con cada uno.
void StepWorldObjects(std::span<World::WorldObject> objects, std::uint8_t map, const IModelLibrary& models,
                      const SpecialObjectStep& special = {});

// 0x606560 en el juego (0x63706a): World::StepWorldObjectChecked sobre los vivos que se vieron en el último
// dibujo, en World::DrawOrder, con el esqueleto de su tipo y el rand() de la CRT. 0x6061c0 al empezar,
// 0x6063d0 y 0x5ff960 con cada uno y el 0x6c9ddc-- del final no hacen nada en los mapas 0 y 2 (*estático*,
// 06 § Los objetos del juego). Corta en el primer objeto cuyo caso MuRet no tiene, con los anteriores ya
// avanzados.
[[nodiscard]] Core::Expected<void, std::string> StepGameWorldObjects(std::span<World::WorldObject> objects,
                                                                     std::uint8_t map, const IModelLibrary& models,
                                                                     Render::Models::CrtRandom& random,
                                                                     const World::ObjectStepGlobals& globals);

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
};

// 0x6046a0 en el juego (0x6374ce), en los mapas 0 y 2 (06 § Los objetos del juego): sector por sector, en
// World::DrawOrder, cada objeto vivo que pasa el trapecio como en DrawWorldObjects queda visto. El 0x64 de
// Devias sigue solo con el nivel del registro propio desde 0x50 (0x35 con la clase 3 o 4), con su acceso.
// Después, 0x602e90: el dibujo de World::DrawOf, sin interpolar la animación; y, con un frame del original,
// 0x602ed0: un rand() y el caso de su tipo. Corta con la dirección en lo primero que MuRet no tiene: el
// dibujo del 0x69 de Lorencia (0x600a4a), antes de su rand(), y los casos de 0x602ed0 (el 0x38 y el 0x69 del
// 0; el 0x64 y el 0x67 del 2), después. Los objetos de antes quedan dibujados. Los demás mapas cortan
// antes de mirar un objeto.
[[nodiscard]] Core::Expected<void, std::string>
DrawGameWorldObjects(Render::ModelBatch& batch, const IModelLibrary& models, std::span<World::WorldObject> objects,
                     const World::TerrainPolygon& polygon, const World::TerrainLighting* lighting,
                     Core::Duration frameClock, const GameObjectPass& pass);

// 0x52d220: World::StepModelEffect sobre cada efecto vivo, con el esqueleto de su modelo.
void StepModelEffects(std::span<World::ModelEffect> effects, const IModelLibrary& models);

// 0x52d620(0) con los efectos vivos: un RenderBody(2, 1, −1, 1, 0, 0, −1, −1) por cada uno, opaco, con
// el color de la luz del terreno en su lugar (`lighting`, o 0 sin ella) más la suya, la luz de cada
// normal y el frame entero (06 § El dragón de fondo); la posición, `alpha` de un paso más con lo que se
// movió en el último (D-C4: el original la dibuja tal cual, a 25 por segundo). La esfera de 400
// contra el frustum no va: en el login el dragón está siempre en pantalla.
void DrawModelEffects(Render::ModelBatch& batch, const IModelLibrary& models,
                      std::span<const World::ModelEffect> effects, const World::TerrainLighting* lighting,
                      Core::Duration frameClock, float alpha);

// 0x549440(0), en el pase de ojo (después de 0x6213e0, con la modelview en la identidad): cada sprite
// vivo, con la mezcla de su +4 (0 aditiva, 1 la de 0x620b10, 2 la normal; la 3, 0x620b90, todavía no se
// leyó y va aditiva; la textura 0x637, la normal), el alpha de 0x549290 y el cuadrado de 0x621400 con el
// tamaño de su textura (`textures`) por la escala y el alpha, con la vista `view` (0x7d366f0). Después de
// dibujarlo, se apaga.
void DrawFrameSprites(Render::ModelBatch& batch, World::FrameSprites& sprites, const IModelLibrary& textures,
                      const Render::Models::OriginalMatrix& view);

// 0x548550(0), en el mismo pase de ojo, después de los sprites: el contrato único de World::
// DrawParticlesChecked, con los servicios de la escena (ParticleDrawServices) sobre el adaptador del lote
// (Render::ModelBatchParticleBackend). Las dimensiones, el material y el color salen de la metadata de
// `textures` (Recurso+20/+24/+28) y el binder, de su TextureSlotOf; el cuadrado de 0x621400, de la vista
// `view`. La rotación de la neblina cambia con `worldClock` en ms y el índice real del pool. Un lugar
// que falla corta el pase después de su prefijo, sin rollback: los anteriores quedan en el lote y el error
// dice cuál fue.
[[nodiscard]] Core::Expected<void, World::ParticleDrawingFailure>
DrawParticles(Render::ModelBatch& batch, World::Particles& particles, const IModelLibrary& textures,
              const Render::Models::OriginalMatrix& view, float worldClock = 0.0f);

// 0x5306a0, en el pase del mundo, después de los efectos con modelo: las hojas vivas, aditivas (en el mapa
// 0x37, 0x620a90) y con el blanco de un solo glColor3f(1, 1, 1), cada una el cuadrado de LeafQuadOf
// trasladado a su posición, con la textura de su tipo.
void DrawLeaves(Render::ModelBatch& batch, const World::Leaves& leaves);
} // namespace Client::Scenes
