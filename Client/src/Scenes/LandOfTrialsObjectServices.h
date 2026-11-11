#pragma once

#include "Core/Expected.h"

#include <glm/ext/vector_float3.hpp>

#include <optional>
#include <string>

namespace Client::World
{
struct WorldObject;
}
namespace Client::Scenes
{
struct GameObjectPass;

// 0x4401e0 (0x602ed0), con el mapa 31 (0x43ff10), después del rand() común de 0x602ef4 (que no usa): el 3 y
// el 0x35, con rand() % 3 en 0, sueltan la salpicadura 6f2/3 y el humo 5f2/22 sobre el objeto, con luz
// (1, 1, 1), su escala y sin dueño; el 0x31 deja en el hueso 3 de la pose global, 10 más abajo en la y, dos
// sprites 0x5ac en rojo (el primero de escala ((sin(reloj · 0,002f) · 0,35f + 0,65f) · 1,2f + 0,3f), el
// segundo de 0,5) y un 0x5fd de 1,2 en gris 0,3, girado rand() % 360. Un pool, un rand() o una pose
// ausentes cortan donde lo pide el ASM, conservando lo ya creado.
[[nodiscard]] Core::Expected<void, std::string> StepLandOfTrialsObjectFrame(const World::WorldObject& object,
                                                                            const GameObjectPass& pass);

// Lo que 0x4403b0 (la cadena de 0x6002e0 en el 31) hace con el cuerpo de un objeto.
struct TrialsBodyDraw
{
    // El color del modelo (+0x48..+0x50) con que se dibuja el cuerpo.
    glm::vec3 color{1.0f};
    // El 0x34 dibuja un RenderBody propio y devuelve 0: 0x6002e0 sigue y lo vuelve a dibujar con el color
    // que le quedó (Q-C317).
    bool twice = false;
};

// 0x4403b0: el 0x1b y el 0x36 multiplican el color del modelo (`color`, el de 0x5ffce0) por
// sin(reloj · 0,0012f + fase) · 0,5f + 0,9f, con la fase de +0x84; el 0x34, por el mismo pulso con 0,0009f;
// el 0xa lo pone en (0,56; 0,8; 0,81). Los demás tipos siguen sin caso (nullopt). Un pulso fuera del
// dominio portado corta.
[[nodiscard]] Core::Expected<std::optional<TrialsBodyDraw>, std::string>
LandOfTrialsBody(const World::WorldObject& object, const glm::vec3& color, float worldClock);
} // namespace Client::Scenes
