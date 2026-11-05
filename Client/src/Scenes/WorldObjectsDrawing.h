#pragma once

#include "Core/IClock.h"
#include "Render/ModelBatch.h"
#include "Scenes/CharacterDrawing.h"
#include "World/TerrainCulling.h"
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
} // namespace Client::Scenes
