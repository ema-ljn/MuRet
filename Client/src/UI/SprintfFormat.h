#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

// El sprintf del CRT del original (0x69fd9b) con un texto de Text.bmd de formato, como lo usan los tooltips, el
// cartel del set y la ventana del personaje (Q-C590). MuRet no le pasa el texto al CRT: lo recorre con la máquina de
// estados de _output (0x6a5904, con la tabla de clases y estados de 0x6bdb14, leída del binario; lo que hace cada
// estado es el output.c de Visual C++ 6, *inferido*) y pone sus propios argumentos, de a uno, en cada marca.
namespace Client::UI
{
// Un argumento de 32 bits de la pila de sprintf: un entero (lo que empuja el original, con o sin signo según quién
// lo lee) o un texto (el puntero de un %s).
class SprintfArgument
{
public:
    SprintfArgument(std::int32_t value) : m_bits(static_cast<std::uint32_t>(value)) {}
    SprintfArgument(std::uint32_t value) : m_bits(value) {}
    SprintfArgument(std::string_view text) : m_text(text), m_isText(true) {}

    [[nodiscard]] bool IsText() const
    {
        return m_isText;
    }
    [[nodiscard]] std::uint32_t Bits() const
    {
        return m_bits;
    }
    [[nodiscard]] std::string_view Text() const
    {
        return m_text;
    }

private:
    std::uint32_t m_bits = 0;
    std::string_view m_text{};
    bool m_isText = false;
};

// `format` como lo escribe sprintf con `arguments`:
//   - las banderas '-', '+', ' ', '#' y '0', el ancho (o '*'), la precisión y los tamaños 'h', 'l', 'I' y 'L' (que en
//     32 bits no cambian nada);
//   - los tipos d, i, u, o, x, X, c y s, y "%%";
//   - un carácter que corta una marca (el ')' de "25%)") sale solo, sin lo que la marca juntó, y una marca que
//     termina el texto no escribe nada, como el CRT.
// Donde el original leería de la pila lo que no le pasaron (una marca sin argumento, un %s con un entero, un %d con
// un texto o un tipo de punto flotante), MuRet deja la marca escrita como está y no gasta el argumento.
[[nodiscard]] std::string FormatLikeSprintf(std::string_view format, std::span<const SprintfArgument> arguments);

// Lo mismo con los argumentos en la llamada.
template <typename... Arguments> [[nodiscard]] std::string Sprintf(std::string_view format, Arguments... arguments)
{
    const std::array<SprintfArgument, sizeof...(Arguments) + 1> list{SprintfArgument(arguments)..., SprintfArgument(0)};
    return FormatLikeSprintf(format, std::span<const SprintfArgument>(list.data(), sizeof...(Arguments)));
}
} // namespace Client::UI
