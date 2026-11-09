#pragma once

#include "Core/Expected.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

// La lista de opciones de conjunto de [0x6eed3c] que dejan 0x40e1b0 y que leen 0x40d690 (igual a
// 0x40da20, byte a byte salvo las direcciones), 0x40d730 y 0x40d7f0, los tres thiscall sobre
// [0x6eed3c]. Contrato: Client/docs/research/06-render-escenas-y-ui.md, "Los ocho helpers de los stats
// propios". Es una proyección tipada de +0x9348..+0x940b, no el objeto. La llenan 0x40e1b0 y 0x40d010
// (Scenes/OwnItemSets.h), y vive en Scenes::ItemSetState con el resto de [0x6eed3c]; los helpers la
// reciben como puntero, nullptr mientras 0x550d10 no se los pase.
namespace Client::Scenes
{
// Las 16 entradas de 3 bytes de +0x934c terminan en +0x937c, donde empiezan sus 16 marcas.
inline constexpr std::size_t ItemSetOptionCapacity = 16;

struct ItemSetOptionList
{
    // +0x9348 y +0x9349: la lista tiene la suma de los dos.
    std::uint8_t count9348 = 0;
    std::uint8_t count9349 = 0;
    // +0x934a y +0x934b: el último conjunto de cada clase de pieza, de 0x40d010. No los lee ninguno de
    // los tres.
    std::array<std::uint8_t, 2> setIds{};
    // +0x934c, de a 3: el primer byte es el selector menos 0x8c; el segundo, la bandera de la clase del
    // héroe que pone 0x40d010, y 0x40d7f0 lo pide distinto de 0. El tercero no lo escribe 0x40d010 (solo
    // los 16 bytes que 0x40e1b0 llena al empezar cada pasada) ni lo lee ninguno de los tres.
    std::array<std::array<std::uint8_t, 3>, ItemSetOptionCapacity> entries{};
    // +0x937c: cada entrada se usa una sola vez hasta que 0x550d10 los pone en 0 (06, su paso 2).
    std::array<std::uint8_t, ItemSetOptionCapacity> used{};
    // +0x938c, de a 8: el DWORD con signo de cada entrada.
    std::array<std::int32_t, ItemSetOptionCapacity> values{};
    // +0x9390 + 8 · i, los otros 4 bytes de cada valor: 0x40d010 escribe ahí el mismo valor y no los lee
    // nadie de lo portado.
    std::array<std::int32_t, ItemSetOptionCapacity> secondValues{};
};

// 0x40d690 (y 0x40da20): suma los valores de las entradas sin usar cuyo primer byte es selector − 0x8c,
// las marca, y suma los 16 bits de abajo de esa suma de 32 a `word`. Con más de 16 entradas el
// original lee las marcas como entradas: error, sin tocar nada.
[[nodiscard]] Core::Expected<void, std::string> AddItemSetOptionValues(ItemSetOptionList& list, std::uint16_t& word,
                                                                       std::uint32_t selector);

// 0x40d730: la misma suma S, y `word` += (word · S) / 100: el producto de 32 bits con signo, que
// envuelve, y la división entera con signo, que trunca hacia 0.
[[nodiscard]] Core::Expected<void, std::string> ApplyItemSetOptionPercent(ItemSetOptionList& list, std::uint16_t& word,
                                                                          std::uint32_t selector);

// 0x40d7f0: con las entradas sin usar del selector que tienen el segundo byte distinto de 0, el valor
// V de la última y cuántas son, N; las marca. `word` += ((base · V) / 100) · N, con base sin signo, los
// productos de 32 bits y la división con signo.
[[nodiscard]] Core::Expected<void, std::string> AddItemSetOptionPercentOf(ItemSetOptionList& list, std::uint16_t& word,
                                                                          std::uint16_t base, std::uint32_t selector);
} // namespace Client::Scenes
