#pragma once

#include "Core/Expected.h"

#include <string>

// El dibujo del entrenador de las mascotas (0x44b510, CL9 hito 6, fase 4b; 06 § HUD y ventanas, "El entrenador"), que
// 0x5f7440 llama siempre después de la caja del caos (0x5f75b7). Reparte por el modo de 0x724580 como el paso
// (Scenes/PetTrainer.h): 0x44b610 el menú, 0x44c850 recuperar la mascota y 0x44d680 la caja. Solo el modo 2 saca
// rand(): por cada botón, los cierres del registro propio de 0x44b190 (uno, o dos con un precio distinto de 0) y el de
// su propio acceso para comparar el zen.
namespace Client::Scenes
{
struct GameHudContext;

// 0x44b510: con 0x724580 en 0, en 4 o fuera de 1..3, nada. Un texto de Text.bmd que falta, uno que desborda su buffer
// de la pila o la mezcla de 0x724584 fuera de 0..2 (que lee 0x6b8c68 de más), corta.
//   - El menú (0x44b610): el fondo de 0xfb y 0xfc sin mezcla, el título 1227 en una caja de 203 · W / 640 en
//     (0xda, 0x64), los textos 1202 y 1203 (con el nombre del héroe) centrados en 0x13f, los dos botones (1204 y
//     1205, en gruesa con el mouse encima) y el de cerrar con su ayuda (1002) en [0x18a, 0x1a2) × [0x109, 0x121), uno
//     más a la derecha que el que cierra en el paso (Q-C958).
//   - Recuperar (0x44c850): lo mismo con el 1228, sin dibujar el 1203 que arma (Q-C959), y por botón (1187 el caballo,
//     1214 el cuervo) el precio de 0x44b190 (1229 a 1232) 0x1e más abajo, en blanco sobre rojo si el zen no alcanza.
//   - La caja (0x44d680): la ventana en (0x104, 0) con la grilla de 8 × 4 de 0x723c80, el botón de mezclar y su ayuda
//     (1212), el título 1205 con el impuesto (1623), la mezcla de 0x724584 (1213 sin mezcla, "%s %s" con 1187 o 1214),
//     la chance (584, 60 con una mezcla), el precio con impuesto (1622) y los avisos 586 y 587.
[[nodiscard]] Core::Expected<void, std::string> DrawTheTrainer(GameHudContext& context);
} // namespace Client::Scenes
