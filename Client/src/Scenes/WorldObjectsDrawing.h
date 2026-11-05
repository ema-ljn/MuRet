#pragma once

#include "Core/IClock.h"
#include "Render/ModelBatch.h"
#include "Scenes/CharacterDrawing.h"
#include "World/TerrainCulling.h"
#include "World/ModelEffects.h"
#include "World/Leaves.h"
#include "World/Particles.h"
#include "World/Sprites.h"
#include "Render/Models/OriginalRotation.h"
#include "World/TerrainLighting.h"
#include "World/WorldObjects.h"

#include <cstdint>
#include <functional>
#include <span>

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

// 0x6046a0 con `objects`: por sector, en World::DrawOrder; el sector que pasa el trapecio en su centro
// (x · 16 + 8, y · 16 + 8) con −180, y en él cada objeto que lo pasa en su lugar por 0,01f (en float) con
// −30 (+0xdc), queda visto (+0x16c guarda el resultado; los de un sector que no pasa quedan sin ver,
// como los apaga 0x606560) y se dibuja como dice World::DrawOf, con la luz del terreno en su lugar
// (`lighting`, o 0 sin ella) y su animación `alpha` de un paso más (Render::Models::Interpolated).
void DrawWorldObjects(Render::ModelBatch& batch, const IModelLibrary& models, std::span<World::WorldObject> objects,
                      const World::TerrainPolygon& polygon, const World::TerrainLighting* lighting, std::uint8_t map,
                      Core::Duration frameClock, float alpha);

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

// 0x548550(0), en el mismo pase de ojo, después de los sprites: cada partícula viva que MuRet arma, con la
// mezcla de los componentes de su textura y el cuadrado de 0x621400 con el tamaño de su textura por la
// escala (World::ParticleDrawOf), con la vista `view`. Las del login son JPG, de 3 componentes (aditivas).
// Sin el tamaño de la textura no se dibuja.
void DrawParticles(Render::ModelBatch& batch, const World::Particles& particles, const IModelLibrary& textures,
                   const Render::Models::OriginalMatrix& view);

// 0x5306a0, en el pase del mundo, después de los efectos con modelo: las hojas vivas, aditivas (en el mapa
// 0x37, 0x620a90) y con el blanco de un solo glColor3f(1, 1, 1), cada una el cuadrado de LeafQuadOf
// trasladado a su posición, con la textura de su tipo.
void DrawLeaves(Render::ModelBatch& batch, const World::Leaves& leaves);
} // namespace Client::Scenes
