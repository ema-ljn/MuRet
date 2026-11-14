#pragma once

// La vuelta del juego a la selección: el F1 02 con 1 que llega cifrado, 0x4c2710 (0x4c3626..0x4c390f) y
// 0x4c2480, en lo que MuRet tiene del mundo. Los paquetes que manda (el F3 00 y los dos C1 03 31) son
// de la sesión. Contrato: Client/docs/research/06-render-escenas-y-ui.md, "La vuelta a la selección", y
// Q-C211 a Q-C215 en 07-quirks-y-bugs.md.
namespace Client::Scenes
{
struct WorldEntryResources;

// En el orden del original, todo estático: suelta los objetos y los efectos del mapa sin el terreno
// (0x6070a0), mata a los personajes de los 400 (0x510f70(−1)), deja la orden de la escena 4 (0x4c363f),
// apaga el mundo cargado (0x4c38f9), pone el nivel de la cámara en 0 (0x4c250e) y el mundo en curso en −1
// (0x4c2540), y abre el registro propio para poner en 0 +0x3c, +0x3e, +0x40 y +0x42 (0x4c2575..0x4c264b):
// el cierre que lo deja sin accesos gasta 3.848 rand(). Lo demás que escriben 0x4c2710 y 0x4c2480 son
// globales que MuRet no tiene (06, la tabla de "La vuelta a la selección"). No corta: nada de lo que lee
// le falta a MuRet. El terreno, el pasto, el héroe publicado (0x743a170) y su lugar quedan.
void ApplyReturnToSelection(const WorldEntryResources& resources);

// 0x4c2480 en lo que MuRet tiene del mundo: el nivel de la cámara en 0, el ataque automático, el mundo en curso en −1,
// el registro propio (3.848 rand() al cerrarse sin accesos), el sonido de los avisos y la lista de los que
// susurraron. Los dos C1 03 31 que manda (0x4c2546 y 0x657670) son de la sesión. Lo llaman la vuelta a la selección
// (0x4c390a) y el cambio de servidor del mapa (0x412c2a, hito 13 de la CL9).
void ResetForTheSelection(const WorldEntryResources& resources);
} // namespace Client::Scenes
