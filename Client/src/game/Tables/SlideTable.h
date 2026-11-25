#pragma once

#include "Core/Expected.h"
#include "Data/DataError.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

// Slide.bmd, los textos del cartel que cruza la pantalla en el juego (Client/docs/research/05-formatos-de-datos.md,
// "Slide.bmd"; en el original, 0x46b3b0, que la carga de los datos del juego, 0x61ccb0, llama en 0x61ed18 con
// "Data\Local\Slide.bmd").
//
// 0xa030 bytes cifrados con XOR3 desde el primero (0x4c1690, la clave de 0x6c82e4): el período del timer en segundos,
// la velocidad del cartel (un float) y cinco tramos de nivel de 0x2008 bytes, cada uno con el nivel más alto del
// tramo, la cuenta de textos y 32 textos de 0x100 bytes. *Verificado* con el Slide.bmd del repo: 60 segundos, 2,5 y
// los tramos (15, 28), (30, 20), (50, 19), (0, −1) y (0, −1).
namespace Client::Data::Text
{
inline constexpr std::size_t SlideTableSize = 0xa030;
inline constexpr std::size_t SlideGroupCount = 5;
inline constexpr std::size_t SlideGroupSize = 0x2008;
inline constexpr std::size_t SlideTextSize = 0x100;

// Un tramo de nivel (+0x11b0, +0x11c4 y la lista de +0x11d8 del objeto [0x5763528]).
struct SlideGroup
{
    // El nivel más alto del tramo: 0x46b680 elige el primero con el nivel propio menor o igual.
    std::int32_t maxLevel = 0;
    // La cuenta del archivo, que divide el rand() de 0x46b680 aunque sea negativa.
    std::int32_t count = 0;
    // Los que 0x46b3b0 lee con esa cuenta, en orden.
    std::vector<std::string> texts;
};

struct SlideTable
{
    // +0x11ac: el período del timer 0x3eb, en segundos.
    std::int32_t periodSeconds = 0;
    // +0x1218: la velocidad de los carteles del timer.
    float speed = 0.0f;
    std::array<SlideGroup, SlideGroupCount> groups{};
};

// Descifra los primeros SlideTableSize bytes de `file` y lee los tramos como 0x46b3b0: con una cuenta positiva, los
// textos j = 0..cuenta − 1 desde 8 + 0x2008 i + 8 + 0x100 j, cada uno hasta su primer cero (strlen y strncpy). El
// original no mira cuánto leyó fread ni dónde termina un texto: un archivo corto deja lo que había en la pila
// (FileTooShort, Q-C516), y una cuenta de más de 32 o un texto sin cero siguen en el tramo de al lado o fuera del
// buffer (InvalidData si pasan del final). Lo que sobra se ignora.
[[nodiscard]] Core::Expected<SlideTable, DataError> LoadSlideTable(std::span<const std::byte> file);
} // namespace Client::Data::Text
