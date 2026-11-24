#pragma once

#include "Core/Expected.h"
#include "Core/IClock.h"

#include <string>

namespace Client::Render
{
class ModelBatch;
}
namespace Client::World
{
struct ModelEffect;
}

namespace Client::Render::Models
{
class IModelLibrary;
}

// CL11, lote magos: lo que 0x6002e0 hace distinto del pase común (RenderBody(2)) con un efecto con modelo de 0x602e90,
// después de posarlo y de mirar la malla oculta −2 (0x600328). Contrato:
// Client/docs/research/06-render-escenas-y-ui.md, "Los efectos con modelo de los magos".
namespace Client::Scenes
{
struct DrawnObject;

// 0x6004aa..0x6004d7: con +0x74 en 0x80 (el 0xcc/1, el 0xd0 y las grietas de subtipo 1), RenderBody(0x82, +0x174,
// +0x64, +0x68, +0x6c, +0x70, +0x58, −1) y sale: el pase de la textura de cada malla con la mezcla que oscurece
// (ZERO / ONE_MINUS_SRC_COLOR), sobre las mallas posadas (`object.retainedMeshes`) o, sin ellas, las que posa acá como
// 0x5ffdf0 (nada con el alpha por debajo de 0,01 o sin el modelo o su esqueleto).
// 0x601da8..0x601df1: el 0xcc/3 y /4 con el color del modelo en (0,1f; 0,1f; 10) y el pase común.
// 0x601df6..0x601e39: el 0xcb/2 y /3 con el color en (0,5; 0,5; 1) y RenderBody(2, …, 0x6e9): Effect\Magic_b.jpg en
// cada malla. Después (0x6021fe) el color vuelve a (1, 1, 1), que no es del efecto.
// 0x60213e..0x60216b: el 0x109 con el subtipo (+0x4) en 1, antes del pase común, RenderBody(0x1040, …, 1, −1): el cromo
// aditivo sin la malla 1. El edx que compara es el 1 de 0x601095, así que no es del 0x109/0 (el escudo de 0x4cb3b0, el
// único 0x109 que crea el original), que va sólo por el común.
// Devuelve true si lo dibujó; false si sigue el pase común de `object` (con el color que le haya puesto).
[[nodiscard]] Core::Expected<bool, std::string> DrawTheWizardEffectBody(Render::ModelBatch& batch,
                                                                        const Render::Models::IModelLibrary& models,
                                                                        const World::ModelEffect& effect,
                                                                        DrawnObject& object, Core::Duration frameClock);
} // namespace Client::Scenes
