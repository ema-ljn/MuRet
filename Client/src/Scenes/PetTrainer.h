#pragma once

#include "Core/Expected.h"

#include <string>

// El entrenador de las mascotas del Dark Lord (CL9 hito 6, fase 3b; 06 § HUD y ventanas, "El entrenador"): el paso de
// 0x44b4e0, que reparte por 0x724580 (UI::DelegatedPanels::trainer), y su modo 2, recuperar la vida del caballo
// oscuro (0x1a04, el lugar 8 del equipo) o del cuervo (0x1a05, el lugar 1) con el 34 (0x44b910). Los modos 1
// (0x44b540) y 3 (0x44d010) y el dibujo (0x44b510: 0x44b610, 0x44c850 y 0x44d680) cortan con su dirección (08
// P-C198).
namespace Client::Scenes
{
struct GameUiContext;

// 0x44b4e0: con 0x724580 en 0, en 4 o fuera de 1..4, nada; con 1 corta en 0x44b540 y con 3 en 0x44d010; con 2,
// StepThePetRecovery.
[[nodiscard]] Core::Expected<void, std::string> StepTheTrainer(const GameUiContext& context);

// 0x44b910, el modo 2. El mouse en [0xd5, 0x1aa) × [0x5a, 0x122) es de la UI. Con el clic, los dos botones
// ([0x103, 0x17b) × [0x96 + 0x3c · i, +0x16)) apagan el clic y el botón y piden el precio (0x44b190: el caballo con
// el 0 y el cuervo con el 1, en su lugar del equipo); con el registro propio abierto, si el zen alcanza (sin signo) y
// el precio no es −1, mandan C1 05 34 <8 o 1> <el byte bajo del precio> con el XOR32 y en C3, cierran el entrenador
// (0x4499b0: 0x724580 y 0x724584 en 0 y el C1 03 31 en claro) y vacían la caché del tooltip de las mascotas
// (0x421d90). Sin botón, el de cerrar ([0x189, 0x1a1) × [0x109, 0x121), con el clic) apaga los dos y cierra. Los
// rand(): los 3.848 de cada cierre del registro propio que lo deja en 0 (uno o dos en 0x44b190 y el del paso) y los
// dos del C3.
[[nodiscard]] Core::Expected<void, std::string> StepThePetRecovery(const GameUiContext& context);
} // namespace Client::Scenes
