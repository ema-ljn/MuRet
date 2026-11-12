#pragma once

#include "Core/Expected.h"

#include <glm/ext/vector_float3.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

// Lo que comparten los cuerpos de 0x444010 (Scenes/HelperTable.cpp y Scenes/PetHelperSteps.cpp): la celda segura
// del jinete, el buffer global de la pose, el alta de las partículas y el polvo. Contrato:
// Client/docs/research/06-render-escenas-y-ui.md, "El caballo oscuro" y "Las mascotas".
namespace Client::Scenes
{
struct ActorState;
struct HelperState;
struct HelperStepContext;
class WorldState;

// 0x444174..0x444182, 0x444a04..0x444a12 y 0x444c5a..0x444c6b: la luz del polvo.
inline constexpr glm::vec3 HelperDustLight{1.0f, 1.0f, 1.0f};
// 0x4443f6..0x444408 y 0x444e30..0x444e46: los mapas de Kalima, del 24 al 29 y el 36.
[[nodiscard]] bool InKalima(std::int32_t map);

// 0x4440b8..0x4440e9 (también 0x4447c4..0x4447f4 y 0x444af0..0x444b20): la celda de la posición del jinete, por
// 0,01f y truncada, con el byte bajo de cada una (0x5f82a0), y el bit 0 de su atributo (0x7d0d348), en la carga
// de la escena 4 o en el mapa del juego. `address` es el de la lectura del atributo.
[[nodiscard]] Core::Expected<bool, std::string> InTheRidersSafeCell(const HelperStepContext& context,
                                                                    const ActorState& rider, std::string_view address);

// 0x4ec110(hueso, desplazamiento, salida, 0) sobre el buffer global de la pose (0x68b181c + hueso · 0x30): sin la
// escala ni el origen del modelo, el punto que deja el último modelo posado del dibujo anterior, que MuRet lleva en
// WorldState::PoseCarry (Q-C401). Un hueso que ningún dibujo escribió todavía es la matriz en cero (el buffer es de
// 200 huesos en la .bss) y da el origen; más allá de los 200 corta.
[[nodiscard]] Core::Expected<glm::vec3, std::string> GlobalPosePoint(const WorldState& world, std::size_t bone,
                                                                     const glm::vec3& offset, std::string_view address);

// 0x53fe70(tipo, posición, ángulos, luz, subtipo, 1, 0) en las partículas del mundo (0x743a198), con su reloj.
// Lleno, ninguna y sin rand(); una que MuRet no sabe crear corta en `address`.
[[nodiscard]] Core::Expected<void, std::string> EmitHelperParticle(const HelperStepContext& context, std::int32_t type,
                                                                   std::int32_t subtype, const glm::vec3& position,
                                                                   const glm::vec3& angles, const glm::vec3& light,
                                                                   std::string_view address);

// El polvo alrededor del ayudante (0x444425..0x4444dd, 0x444a19..0x444aa8 y 0x444e6f..0x444f1d): (rand() & 63) − 32
// en x y en y y (rand() & 31) − 16 en z, cada eje en la x87 hasta su fstp, y la 5f2 en el mapa 2, ninguna en el 10
// o la 5f3 en los demás, con la luz del polvo y los ángulos del ayudante.
[[nodiscard]] Core::Expected<void, std::string>
ScatterTheDust(const HelperState& helper, const HelperStepContext& context, std::string_view address);

// Las 5f2 del galope en Icarus (0x444325..0x4443d1 y 0x444d68..0x444e16) desde `base`: la guarda saca un
// rand() % 5 + 5, siempre positivo, y después van de a una mientras su cuenta no llega a un rand() % 5 + 5 nuevo en
// cada vuelta (Q-C400), cada una corrida rand() % 50 − 25 en x y en y y (rand() & 15) − 8 − 10 en z.
[[nodiscard]] Core::Expected<void, std::string> PuffTheIcarusDust(const glm::vec3& base, const glm::vec3& angles,
                                                                  const HelperStepContext& context,
                                                                  std::string_view address);

// 0x44480b..0x44484a y 0x444b3b..0x444b7a: con +0x80 del jinete en 1 o en 2 el alpha baja 0,1f, con la resta en
// la x87 guardada en float y comparada sin redondear: por debajo de 0, 0. Con otro, 1.
void FadeWithTheRider(HelperState& helper, const ActorState& rider);
} // namespace Client::Scenes
