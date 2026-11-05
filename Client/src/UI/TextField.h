#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

// Un campo de texto de la UI del login (CL5.5): la cuenta o la clave. El original usa dos EDIT de
// Windows con EM_LIMITTEXT 10, la clave con ES_PASSWORD (06 § El login; CL5.R), y su subclase
// 0x467b10 toma Tab y Enter. MuRet guarda bytes de ASCII imprimible (0x20 a 0x7e), hasta
// `maxBytes`: es lo que entra en el paquete del login (Q-C43). Lo demás se descarta byte por byte:
// una letra con tilde o un emoji en UTF-8 son todos bytes de 0x80 o más, y no entra ninguno. La
// tabla, en test_text_field.cpp (el paso 2 del plan).
//
// Sin operator<<: lo que tiene no se imprime nunca, ni al log. El PrintTo de los tests
// (tests/support/UiPrinters.h) escribe el largo.
namespace Client::UI
{
class TextField
{
public:
    // Qué letras toma: las de ASCII (la cuenta, la clave y el código), o además las de Latin-1 de
    // U+00A0 a U+00FF, como su byte (el nombre de un personaje nuevo: el EDIT de 0x63d670 no tiene
    // filtro, y en esa franja Latin-1 y CP1252 coinciden).
    enum class Charset : std::uint8_t
    {
        Ascii,
        Latin1,
    };

    TextField(std::size_t maxBytes, bool masked, Charset charset = Charset::Ascii);

    // Suma lo que se puede de `utf8`, en orden, hasta llenar el campo.
    void Type(std::string_view utf8);
    // Borra el último byte; vacío, nada.
    void Backspace();
    void Clear();

    // Lo que tiene. Vale hasta el próximo cambio.
    [[nodiscard]] std::string_view Value() const&;
    // Sobre un campo temporal no compila: la vista quedaría colgada.
    std::string_view Value() const&& = delete;
    // Lo que se dibuja: lo que tiene, o un asterisco por byte si es la clave.
    [[nodiscard]] std::string Shown() const;
    [[nodiscard]] std::size_t Size() const;
    [[nodiscard]] bool Masked() const;

private:
    std::size_t m_maxBytes;
    bool m_masked;
    Charset m_charset;
    std::string m_value;
};
} // namespace Client::UI
