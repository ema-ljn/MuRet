#pragma once

#include "Core/Expected.h"

#include <cstdint>
#include <string>

// La caja de la contraseña del almacén (CL9 hito 6, fase 3): el teclado de 0x78abb20, que abren 0x5e7aa0
// (Scenes::OpenTheKeypad), soltar en el inventario con la traba y el botón de la traba del almacén. Su paso (0x5e7bf0,
// con el 83 y el 99), su dibujo (0x5e9b10, el paso 30 del HUD) y lo que hay bajo el mouse (0x5e7b50). Contrato: 06 §
// "Los dibujos de las ventanas de los NPC" y 04 § "Las ventanas de los NPC".
namespace Client::UI
{
class PanelRegistry;
}

namespace Client::Scenes
{
struct GameHudContext;
struct GameUiContext;

// 0x7d3a0a4: las cifras del número personal que piden los modos 2, 3 y 6. Lo escribe solo el inicializador estático
// 0x622ac0 con el 7 de .data (0x6c76c0); los demás modos piden 4.
inline constexpr std::int32_t PersonalNumberDigits = 7;
inline constexpr std::int32_t PasswordDigits = 4;

// Lo que devuelve 0x5e7b50: las teclas 0 a 9 son las del teclado barajado, la 10 borra, 11 es el OK y 12 cancelar.
inline constexpr std::int32_t NoKeypadKey = -1;
inline constexpr std::int32_t KeypadDigitKeys = 10;
inline constexpr std::int32_t KeypadEraseKey = 10;
inline constexpr std::int32_t KeypadOkKey = 11;
inline constexpr std::int32_t KeypadCancelKey = 12;

// 0x5e7b50: la tecla bajo el mouse (0x7d3682c y 0x7d36828, enteros): las once de 32 × 32 en ((i % 5) · 40 + 0xdf,
// (i / 5) · 40 + 0x96), y el OK y cancelar de 0x46 × 0x15 en (0x109 + 0x4e · j, 0xeb), con el borde de arriba y el
// de la izquierda adentro. Sin ninguna, −1. Las filas de las teclas van de a 40 desde 0x96 y el dibujo las pone de a
// 38 desde 0x9a (07, Q-C923).
[[nodiscard]] std::int32_t KeypadKeyAt(std::int32_t mouseX, std::int32_t mouseY);

// 0x5e7bf0, primero en 0x5ee140: con el modo en 0, false y nada más. Si no, true: 0x5ee140 no sigue. Con el clic y una
// tecla bajo el mouse, el sonido 0x19 y la tecla: una cifra de 0x7879fc8 se agrega a lo tecleado si no llegó a las
// cifras del modo, borrar saca la última, cancelar pone el modo en 0 y el OK, con todas las cifras, cuatro iguales
// limpian lo tecleado y abren el 0x87; si no, el modo en 0 y por el modo que tenía (la tabla de 0x5e9af0): el 1 manda
// el 83 con 0, el atoi de lo tecleado y los diez primeros bytes del buffer del chat (Q-C924); el 2, el 83 con 1, la
// contraseña de 0x78abb24 y lo tecleado; el 3, el 83 con 2, 0 y lo tecleado (los tres con el XOR32 y en C3); el 4
// guarda lo tecleado y vuelve a abrir el teclado en 5 (sus 40 rand()); el 5, si es lo mismo, guarda su atoi en
// 0x78abb24 y sigue en 2 sin barajar, y si no abre el 0x88; el 6 manda el C1 03 31 en claro, cierra todo (0x657a90) y
// manda el 99 con lo tecleado (Q-C925). Después, con el clic la tecla queda apretada (0x78abb3a), el botón suelto la
// suelta, y el clic y el botón abajo quedan en 0.
[[nodiscard]] Core::Expected<bool, std::string> StepTheKeypad(const GameUiContext& context, UI::PanelRegistry& panels);

// 0x5e9b10, el paso 30 del HUD: con el modo en 0, nada. Si no, el marco de 0xfc y 0xfb en (213, 100), el título (texto
// 690 más el modo − 1 entre 0 y 4; el 6 es el 691) y el pedido (697 con los modos 2, 3 y 6, 696 con el 5 y si no 695)
// centrados en 0x140, la barra de lo tecleado (0xfd, de 52 de ancho, 13 más por cada cifra después de la cuarta) con un
// "*" por cada byte de 0x78ab9fc, las once teclas (con su número de 0x7879fc8, o borrar) en amarillo la del mouse,
// apretada con 0x78abb3a, y el OK y cancelar. Sin rand(). Deja el blanco y la fuente gruesa grande.
[[nodiscard]] Core::Expected<void, std::string> DrawTheKeypad(GameHudContext& context);
} // namespace Client::Scenes
