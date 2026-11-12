#pragma once

#include "Scenes/ItemSetOptionList.h"

#include <array>
#include <cstddef>
#include <cstdint>

// Lo que escriben 0x40e1b0, 0x40d010 y 0x40da20 en el objeto de los conjuntos, [0x6eed3c] (el estático
// de 0x6e5930), de +0x9307 a +0x940b. Es una proyección tipada, no el layout; cada campo dice su
// offset. Las tablas de +4 y +0x8004 son Data::Tables::ItemSetTables; +0x9304 y +0x9305 son de los tooltips
// (CL8 hito 5) y +0x9306 no lo lee nadie de lo portado. Arranca en cero, como el objeto estático, que además su
// constructor (0x40f2a0) pone en cero de +0x9304 a +0x9347. Contrato: Client/docs/research/06-render-escenas-y-ui.md,
// "Los stats del registro propio".
namespace Client::Scenes
{
// Las dos clases de pieza de 0x40d010, el byte +0x1c & 3 del ítem menos 1: los nombres de +0x9308 y
// +0x9328, y en la lista las cuentas de +0x9348 y +0x9349 y los conjuntos de +0x934a y +0x934b. Con
// & 3 en 3 cae fuera (Q-C187).
inline constexpr std::size_t ItemSetKindCount = 2;
// Lo que ocupa cada nombre: el de ItemSetOption.bmd con su 0.
inline constexpr std::size_t ItemSetNameSize = 0x20;

struct ItemSetState
{
    // +0x9304: la ventana del set del inventario. La prende el mouse sobre el cartel (0x40e6c0, en el paso) y la apaga
    // su dibujo (0x40e850, desde los tooltips), que la muestra con 1.
    std::uint8_t byte9304 = 0;
    // +0x9305: el cartel de un conjunto de ItemSetOption.bmd, su índice + 1. Lo escribe el comando "/" y el nombre
    // (0x40ebd0) y lo dibuja 0x40eca0 hasta que lo apagan F1 (0x5aca34), 0x5ae360, abrir el personaje (0x65c9c6) o un
    // conjunto sin usar.
    std::uint8_t byte9305 = 0;
    // +0x9306: lo pone en 0 abrir el personaje (0x65c9c6).
    std::uint8_t byte9306 = 0;
    // +0x9307: 0x40d010 lo pone en 0, en piezas − 1 cuando un conjunto no entra en el nombre de su
    // clase, y con un conjunto completo le suma 1 por opción si no es 0.
    std::uint8_t byte9307 = 0;
    // +0x9308 y +0x9328: el nombre del conjunto de cada clase, copiado con su 0 (strcpy).
    std::array<std::array<char, ItemSetNameSize>, ItemSetKindCount> names{};
    // +0x9348..+0x940b: las cuentas, los conjuntos y la lista de opciones que leen 0x40da20 y los
    // helpers de 0x550d10. 0x550d10 pone sus marcas en 0 después de 0x40e1b0 (0x550d29).
    ItemSetOptionList list;
};
} // namespace Client::Scenes
