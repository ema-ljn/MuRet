#pragma once

#include "Core/Expected.h"

#include <glm/ext/vector_float3.hpp>

#include <cstddef>
#include <string>

namespace Client::World
{
struct WorldObject;
}
namespace Client::Scenes
{
struct GameObjectPass;

// 0x4ec110(0x68b181c + hueso · 0x30, desplazamiento, salida, 0): el punto del hueso en el buffer global de la
// pose, la del último modelo posado (Translate en 0: el hueso ya está escalado y trasladado al mundo). Un
// hueso que todavía no se escribió conserva la matriz en cero de la .bss. Sin la pose prestada, corta.
[[nodiscard]] Core::Expected<glm::vec3, std::string> ObjectBonePoint(const GameObjectPass& pass, std::size_t bone,
                                                                     const glm::vec3& offset);

// 0x602ed0 con el 0x38 (MerchantAnimal01) del mapa 0 (0x603149): dos sprites 0x5ac en los huesos 48 y 57,
// de escala L · 5 y luz (L · 0,6; L · 0,3; L · 0,1), con L = (rand() común % 30 + 70) · 0,01f. El
// desplazamiento que les pasa a 0x4ec110 es la pila sin inicializar ([ebp − 0x28], Q-C256): con el objeto
// posado, ahí quedó el marco de 0x6002e0 (el nivel del try en −1 en la y), y los dos salen en NaN. Sin
// posar, lo que dejó 0x5ffdf0 no está leído y corta.
[[nodiscard]] Core::Expected<void, std::string>
StepMerchantAnimalFrame(const World::WorldObject& object, int commonRandom, const GameObjectPass& pass, bool posed);

// 0x602ed0 con el mapa 4 (0x603799): el 0x13 y el 0x14 dejan dos sprites (giro r y −r, con r =
// ftol(reloj · 0,1f) % 360) en los huesos 15 y 19 (0,3) y 21 (1,5): 0x61e con (L; L · 0,2; 0) el 0x13 y
// 0x5d5 con (L · 0,4; L · 0,8; L) el 0x14; el 0x28, dos 0x5d5 de 2,5 sobre el objeto, 260 más arriba, con
// (L; L; L). Después todos caen al caso del mapa 6 (StepArenaObjectFrame).
[[nodiscard]] Core::Expected<void, std::string>
StepLostTowerObjectFrame(const World::WorldObject& object, int commonRandom, const GameObjectPass& pass, bool posed);

// 0x602ed0 con el mapa 6 (0x6039cb), al que también cae el 4: el 9 deja un sprite 0x5ac de escala L · 5 y
// luz (L · 0,6; L · 0,3; L · 0,1) en el hueso 1 con el desplazamiento de la pila sin inicializar, como el
// 0x38 del 0: NaN con el objeto posado, corte sin posar.
[[nodiscard]] Core::Expected<void, std::string> StepArenaObjectFrame(const World::WorldObject& object, int commonRandom,
                                                                     const GameObjectPass& pass, bool posed);
} // namespace Client::Scenes
