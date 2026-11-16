#pragma once

#include "Core/Expected.h"

#include <string>

// El tooltip de las mascotas, 0x422990 (CL9 hito 4; 06 § El A9 de las mascotas, "El tooltip de las mascotas"): lo que
// 0x5b2aa0 (Scenes/ItemTooltips.h) muestra del Dark Horse (0x1a04) y del Dark Raven (0x1a05), con lo que el A9 escribe
// en 0x723960 (WorldRuntime::pets).
namespace Client::Scenes
{
struct ItemTooltipContext;

// 0x422990(x, y, ítem, grilla), desde 0x5b2b0d con los renglones ya limpios. Con otro tipo devuelve false y no hace
// nada. Con una mascota devuelve true y 0x5b2aa0 sale (0x5b2b17):
//   - si el ítem no es el del último pedido (WorldRuntime::petTooltipQuery: el lugar de la caja, el tipo y D+4), lo
//     anota y pide su A9 (C1 06 A9, la mascota, 0x78abb28 y 0x78abb2c) por GameHudContext::send, sin dibujar nada;
//     un dibujo intermedio no anota ni pide;
//   - si es el mismo, 0x4221d0 con la mascota, el precio (con la tienda de NPC abierta, 1 rand(), corta: el formato de
//     0x5b1130 es de la CL9.3), el nombre, la experiencia, el nivel, el daño y la velocidad del cuervo, la durabilidad,
//     el nivel pedido del caballo y el liderazgo pedido del cuervo (cada uno con el registro propio abierto y
//     cerrado), las clases, las opciones (0x4225d0 y 0x5b2320 por cada una) y, en el caballo, la defensa y la skill;
//     después, la caja (0x5af090) arriba de (x, y) con el alto que cuentan sus renglones enteros y sus medios
//     renglones, que no son los de 0x78abb30 y 0x78abb34.
// Lo que escribe en el mundo (la tabla de las mascotas, la opción del caballo en el ítem, el pedido anotado) va a
// copias en un dibujo intermedio.
[[nodiscard]] Core::Expected<bool, std::string> DrawThePetTooltip(const ItemTooltipContext& context);
} // namespace Client::Scenes
