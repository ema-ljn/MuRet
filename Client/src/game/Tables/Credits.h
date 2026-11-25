#pragma once

#include "Core/Expected.h"
#include "Data/DataError.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

// Data\Local\credit.bmd, los créditos (Client/docs/research/05-formatos-de-datos.md, "credit.bmd"; en
// el original, 0x63ff30 desde 0x63f020, al armar la UI del login).
//
// 0x63ff30 lee 13.200 bytes (0x3390) en +0x610 de la ventana de los créditos y les pasa 0x4c1690, el
// XOR3 del login, de una vez desde el primer byte. Quedan 400 registros de 33: la clase en el primero
// (1, el título de una parte; 2, un rol; 3, un nombre; 0, el final) y el texto en los 32 que siguen,
// hasta su primer cero. No tiene suma. *Verificado* con el del laboratorio: el primer 0 está en el
// registro 244, y ningún texto llena los 32 bytes.
namespace Client::Data::Text
{
inline constexpr std::size_t CreditRecordSize = 33;
inline constexpr std::size_t CreditRecordCount = 400;
inline constexpr std::size_t CreditFileSize = CreditRecordSize * CreditRecordCount;

// Las clases del primer byte, como las mira 0x63ffe0.
enum class CreditKind : std::uint8_t
{
    End = 0,
    Title = 1,
    Role = 2,
    Name = 3,
};

struct CreditEntry
{
    // El byte tal cual: 0x63ffe0 compara con 0, 1, 2 y 3, y cualquier otro valor no es ninguno.
    std::uint8_t kind = 0;
    std::string text;

    friend bool operator==(const CreditEntry&, const CreditEntry&) = default;
};

// Los 400 registros de `file`, descifrados. FileTooShort con menos de 13.200 bytes: fread lee lo que
// haya y el resto queda con lo que tenía la ventana, que MuRet no tiene (el del juego tiene 13.200). Lo
// que sobra se ignora, como fread.
[[nodiscard]] Core::Expected<std::vector<CreditEntry>, DataError> LoadCredits(std::span<const std::byte> file);

// La clase del registro `index` como la lee 0x63ffe0: después del último, End. En el original son
// los bytes de la ventana que siguen a la tabla (+0x39a0, el índice mismo), que el archivo del juego no
// alcanza.
[[nodiscard]] std::uint8_t CreditKindAt(std::span<const CreditEntry> credits, std::size_t index);
} // namespace Client::Data::Text
