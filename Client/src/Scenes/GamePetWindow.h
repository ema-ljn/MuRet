#pragma once

#include "Core/Expected.h"

#include <string>

// El dibujo de la ventana de la mascota del Dark Lord y del cartel del cuervo (CL9 hito 4; 06 § El A9 de las mascotas,
// "La ventana de la mascota"). El paso de la ventana (0x424759) está en Scenes/GameUi; lo que dibuja sale de la tabla
// de 0x723960 (WorldRuntime::pets) y del cuervo del héroe (ActorState::darkSpirit), que escribe el A9.
namespace Client::UI
{
class PanelRegistry;
}

namespace Client::Scenes
{
struct GameHudContext;

// 0x424160(héroe), que 0x5f7440 llama siempre (0x5f75e7), entre la ventana de las misiones y la 0x2000:
//   - 0x4249a0: con el personaje cerrado, 0x723911 en 0 y nada más; abierto, el botón de la mascota (pet1.jpg o, con
//     0x723911 en 2, pet2.jpg) con su ayuda (1217) y, con la ventana abierta (0x723911 en 2), el fondo de 0x5ee220, las
//     dos pestañas (el caballo, la 0, y el cuervo, la 1), las líneas del marco, el botón de cerrar con su ayuda (1002),
//     el título (1217) y los nombres de las pestañas (1187 y 1217);
//   - con la ventana abierta, la pestaña 0 (0x424e40: los datos del caballo con 0x4149b0 y el liderazgo, o "no tiene"
//     con 1233) y la 1 (0x416670 sobre el cuervo, con sus cuatro órdenes, o 1169 sin cuervo);
//   - con cuervo, su cartel arriba a la derecha (la vtable + 0xc, 0x4182e0 → 0x418460): el nombre (1214), su barra de
//     durabilidad (el lugar 1 del registro propio) y el ícono de su orden, con su ayuda.
// Cada acceso al registro propio saca 3.848 rand() al cerrarse en 0, y el daño del caballo abre la skill 0x3e de
// Skill.bmd (60). La rama del tipo 0 sin las órdenes de 0x4149b0 (0x415869), que ninguno de los dos llama, corta.
[[nodiscard]] Core::Expected<void, std::string> DrawThePetWindow(GameHudContext& context, UI::PanelRegistry& panels);
} // namespace Client::Scenes
