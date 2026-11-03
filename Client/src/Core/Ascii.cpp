#include "Core/Ascii.h"

#include <algorithm>
#include <functional>
#include <string_view>

namespace Client::Core
{
char ToLowerAscii(char c)
{
    // El byte sin signo: con un char con signo, los >= 0x80 son negativos, y así quedan afuera de
    // la A a la Z con cualquiera de los dos.
    const auto byte = static_cast<unsigned char>(c);
    if (byte < 'A' || byte > 'Z')
    {
        return c;
    }
    return static_cast<char>(byte - 'A' + 'a');
}

// operator== de string_view compara bytes exactos, y la biblioteca estándar no trae una comparación
// sin mayúsculas. ranges::equal sí deja proyectar: pasa cada char de los dos lados por ToLowerAscii
// antes del ==, y si los tamaños difieren devuelve false sin comparar nada.
bool EqualsIgnoringAsciiCase(std::string_view a, std::string_view b)
{
    return std::ranges::equal(a, b, std::ranges::equal_to{}, ToLowerAscii, ToLowerAscii);
}
} // namespace Client::Core
