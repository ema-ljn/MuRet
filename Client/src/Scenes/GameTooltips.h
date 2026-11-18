#pragma once

#include "Core/Expected.h"
#include "Data/Text/TextTable.h"

#include <cstdint>
#include <string>

// Los tooltips del juego (06 § Los tooltips, CL8 hito 5): 0x5f7bc0, lo que el dibujo del juego llama en 0x63769b
// (el ítem bajo el mouse, la skill de la barra y la ventana del set del inventario), el cartel de un conjunto
// (0x40eca0, en 0x6376c6) y la caja que dibujan todos (0x5af090). Los renglones van en HudState::tooltip, que dura de
// un frame al otro como los arreglos de la .bss.
namespace Client::Scenes
{
struct GameHudContext;
struct TooltipLines;

// 0x5af090(x, y, renglones, ancho, modo, marco): la caja de los primeros `count` renglones de HudState::tooltip, hasta
// el primero vacío. Mide cada uno con su fuente (la negrita con bold distinto de 0) y arma, en 640 × 480:
//   - el ancho, el mayor cx / rx (o 2 · ancho / rx con un ancho mayor que 0) + 4, centrado en x, sin pasar del borde
//     izquierdo ni del derecho (W / rx − ancho − 1);
//   - el alto, (cy · enteros + cy · 0,5 · medios) / (ry · 0,90909088), donde un renglón que empieza con '\n' es un
//     medio y cy es el del último medido;
//   - con el marco en 1, los cuatro bordes negros de 1 y el fondo negro con 0,8 de alfa (0x621aa0);
//   - cada renglón con su color (el glColor3f y el fondo de 0x6c8e5c y 0x6c8e64), con el modo de 0x467060 en una caja
//     de (ancho − 2) · rx píxeles desde x + 1, con el fondo en toda ella (ITextDrawer::DrawColumns): el 1 pone el
//     texto contra el borde izquierdo (la ayuda del retrato de las cartas, 0x497f3f), el 2 lo centra
//     (Render::Text::CenterInBox) y el 3 lo pone contra el borde derecho; uno con '\n' o " " solo baja medio o un
//     renglón, sin dibujar.
// Cada renglón baja (cy / ry) · 1,1. Termina con el color blanco y sin mezcla. Los modos que no son 1, 2 ni 3 cortan.
[[nodiscard]] Core::Expected<void, std::string> DrawTheTooltipBox(GameHudContext& context, std::int32_t x,
                                                                  std::int32_t y, std::int32_t count,
                                                                  std::int32_t widthOverride, std::int32_t textMode,
                                                                  std::int32_t frame);

// 0x40d530(renglón, opción, valor): la opción de un conjunto con su valor (el byte de abajo) en el texto de Text.bmd
// que le toca, de formato: 950 + opción hasta la 6, el 632 para la 7, 949 + opción de la 8 a la 0x14, el 970, el 984
// y el 983 para la 0x15 a la 0x17 y 947 + opción de la 0x18 a la 0x23. Con otra, el renglón queda como estaba.
[[nodiscard]] Core::Expected<void, std::string> WriteTheSetOption(const Data::Text::TextTable* texts, std::string& line,
                                                                  std::uint8_t option, std::uint8_t value);

// 0x5f7bc0: sin mezcla y con 0x77f3b66 en 0; el modo de reparar (0x78abb0c) distinto de 0 pasa a 1. Con un ítem
// bajo el mouse (0x78abaa8), con el registro propio abierto alrededor, 0x5b8150 (reparar, con el mouse sobre el
// inventario) o 0x5b2aa0; con la skill de la barra (0x6c9320), 0x5b90b0; con el inventario abierto, la ventana del
// set (0x40e850). Los cuerpos que MuRet no tiene cortan con su dirección.
[[nodiscard]] Core::Expected<void, std::string> DrawTheTooltips(GameHudContext& context);

// 0x40eca0: con +0x9305, el cartel del conjunto +0x9305 − 1 de ItemSetOption.bmd arriba a la izquierda: su nombre
// (con el 1089 y el 159) en dorado y cada opción de sus pares, sus dos extras y sus cinco del conjunto completo en
// azul. Un conjunto sin usar (+0x3c en 0xff) lo apaga.
[[nodiscard]] Core::Expected<void, std::string> DrawTheSetLabel(GameHudContext& context);
} // namespace Client::Scenes
