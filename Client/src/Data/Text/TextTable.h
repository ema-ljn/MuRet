#pragma once

#include "Core/Expected.h"
#include "Data/DataError.h"

#include <cstddef>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

// Text.bmd, los textos de la interfaz (Client/docs/research/05-formatos-de-datos.md, "Tablas
// .bmd"; en el original, el cargador de 0x5495e0).
//
// 0x96000 bytes: 2048 registros de 300, sin cabecera, contador ni checksum, cifrados con XOR3 de
// punta a punta. El texto i está en el byte 300 * i y termina en el primer '\0' o al final del
// registro: en el archivo real, el registro 7 ocupa sus 300 bytes sin '\0' (Q-C07). El original
// lo lee como cadena de C y sigue en el registro 8 (519 bytes con el archivo real); MuRet corta en
// los 300 del registro.
//
// Los textos quedan como bytes, en la codificación del archivo; pasarlos a UTF-8 para dibujarlos
// es de la CL5.3. El original usa algunos como formato de sprintf (FUN_00633300). MuRet nunca
// pasa un texto de datos como formato.
namespace Client::Data::Text
{
// Cuántos textos trae la tabla, uno por registro: el texto i es el que el original busca con el
// índice i.
inline constexpr std::size_t TextCount = 2048;
// Lo que ocupa cada texto, con su '\0' si lo tiene (el 7 no lo tiene, Q-C07).
inline constexpr std::size_t TextRecordSize = 300;
// La tabla entera, que es el archivo entero: no hay cabecera, contador ni checksum.
inline constexpr std::size_t TextTableSize = TextCount * TextRecordSize;
static_assert(TextTableSize == 0x96000, "el tamaño que mide el research (05, Tablas .bmd)");

class TextTable
{
public:
    // El texto `index`, o std::nullopt si index >= TextCount. Un índice que viene del servidor
    // puede pasarse (FUN_004da070 le suma 2020 a un byte del paquete), y el original leería
    // después de la tabla. El texto apunta adentro de la tabla: vale hasta que la tabla se
    // destruya, se mueva o se le asigne otra.
    std::optional<std::string_view> Text(std::size_t index) const&;

    // Llamado sobre una tabla temporal no compila, como en `LoadTextTable(file).Value().Text(0)`:
    // el texto colgaría de una tabla que muere al final de la línea. Si la temporal llega por un
    // `const TextTable&`, el compilador ya no la ve, y el texto cuelga igual.
    std::optional<std::string_view> Text(std::size_t index) const&& = delete;

private:
    explicit TextTable(std::vector<char> decoded);

    // Solo LoadTextTable arma una tabla, con los TextTableSize bytes. Una tabla movida queda en un
    // estado válido pero sin especificar (en la práctica, vacía), y su Text() no lee fuera de lo
    // que le quedó.
    friend Core::Expected<TextTable, DataError> LoadTextTable(std::span<const std::byte> file);

    std::vector<char> m_records;
};

// Descifra los primeros TextTableSize bytes de `file`.
//   - Si `file` es más corto, devuelve DataError::FileTooShort. El original leería igual y
//     dejaría en la tabla lo que hubiera en la memoria.
//   - Si es más largo, el resto se ignora, como con el fread de tamaño fijo del original.
Core::Expected<TextTable, DataError> LoadTextTable(std::span<const std::byte> file);
} // namespace Client::Data::Text
