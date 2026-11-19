#pragma once

#include "Core/Expected.h"
#include "Core/IClock.h"
#include "Render/Models/HalfLifeMath.h"

#include <functional>
#include <optional>
#include <span>
#include <string>

namespace Client::Render
{
class ModelBatch;
}
namespace Client::Render::Models
{
class CrtRandom;
}
namespace Client::World
{
class FrameSprites;
struct ModelEffect;
} // namespace Client::World

// CL11, tipos altos: los casos propios de 0x6002e0 (y de 0x438fe0, que 0x6002e0 llama en 0x600ef6) para los efectos
// con modelo de los tipos altos que 0x52d620 manda por 0x52da92 → 0x602e90, después de posarlos (0x5ffdf0) y de mirar
// la malla oculta −2 (0x600328). Contrato: Client/docs/research/06-render-escenas-y-ui.md, "Los efectos con modelo
// de los tipos altos".
namespace Client::Scenes
{
class IModelLibrary;
struct DrawnObject;

// Lo que esos casos leen además del objeto: los sprites del frame (0x5491e0), el CRT (0x69fd70), los huesos del buffer
// global de la pose (0x68b181c, que 0x5ffdf0 deja en el mundo) y el reloj del mundo (0x5767b88).
struct HighEffectDrawServices
{
    World::FrameSprites* sprites = nullptr;
    Render::Models::CrtRandom* random = nullptr;
    std::function<std::span<const Render::Models::BoneTransform>()> posedBones{};
    std::optional<float> worldClock{};
};

// Si el efecto tiene caso propio acá: el 0x11b, el 0x114/3, /4 y /5, el 0x140 y el 0x125, de cualquier otro subtipo.
[[nodiscard]] bool HasHighEffectBody(const World::ModelEffect& effect);

// El cuerpo de esos casos, con `object` posado (sus mallas en `object.retainedMeshes` o posadas acá como 0x5ffdf0).
// Sólo llega acá un efecto con el alpha en 0,01 o más: con menos, o sin orden, 0x5ffdf0 devuelve 0 (0x5ffe2a..0x5ffe3d)
// y 0x602e90 no llama a 0x6002e0 (DrawPlainModelEffect sale antes de la pose):
// - 0x601e68..0x601faf, el 0x11b: el color del modelo en (1; 0,6f; 0,3f) y RenderBody(2); +0x40 en el hueso 2
//   (0x68b187c) y +0x4c en el 3 (0x68b18ac), con 0x4ec110 sin trasladar; dos sprites 0x5ac (escala 1, luz (1, 0, 0),
//   giro 0) en +0x40 y en +0x4c, y dos 0x5fd (escala 0,5f, luz 0,5) en los mismos puntos, cada uno con el giro
//   rand() % 360 (0x601f49 y 0x601f7d). En los dos pases si el blendMesh es −2 (0x52d673): cuatro rand() por frame.
// - 0x601fb4..0x602000, el 0x114/3, /4 y /5: RenderBody(2, …, malla oculta −1, textura 0x6ef, Skill\pinkWave.jpg).
// - 0x602005..0x602044, el 0x140: el color en (0,4f; 0,6f; 1) y dos RenderBody(0x44) con la textura en −1 (el
//   cromado, Chrome01).
// - 0x43909e..0x439214 (0x600ef6 → 0x438fe0), el 0x125: el color en 0,3 y RenderBody(2); con l = FSIN(T · 0,003f) ·
//   0,2f + 0,8f, los huesos 3 (0x68b18ac) y 4 (0x68b18dc), cada uno con un sprite 0x5ca (escala 0,2f) y un 0x5fd
//   (escala 0,5f) de luz (l; l · 0,2f; l · 0,1f). Sin rand(). El 0x125 nace con el alpha en 0 (0x51cdf5): sólo su
//   joint 0x6ee/0x12 lo enciende (0x53949a, el alpha en 1 con la vida del efecto por encima de 5) y le copia su
//   posición (0x5394a4..0x5394bb); hasta entonces no se posa ni se dibuja.
// Después todos van a 0x602365, que con +0x78 & 2 repite RenderBody(2) con el blendMesh −2 y la luz 1: un efecto con
// modelo no escribe su +0x78 (nadie lo hace entre 0x518cf0 y 0x52ce13), así que acá no corre. Devuelve true si lo
// dibujó; false si el efecto no tiene caso acá. Sin los sprites, el CRT, los huesos o el reloj que pide, corta.
[[nodiscard]] Core::Expected<bool, std::string>
DrawTheHighEffectBody(Render::ModelBatch& batch, const IModelLibrary& models, World::ModelEffect& effect,
                      DrawnObject& object, Core::Duration frameClock, const HighEffectDrawServices& services);
} // namespace Client::Scenes
