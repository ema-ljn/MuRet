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

    // Suma lo que se puede de `utf8`, en orden, hasta llenar el campo. Con todo elegido, lo primero que entra
    // reemplaza lo que había, como el EDIT con una selección.
    void Type(std::string_view utf8);
    // Borra el último byte; vacío, nada. Con todo elegido, lo borra todo.
    void Backspace();
    void Clear();
    // WM_SETTEXT (la vtable + 0x4c de los EDIT, 0x467ee0): pone `raw` tal cual, en bytes, sin el tope de
    // EM_LIMITTEXT, que solo frena lo tecleado. Deja el cursor al final y nada elegido.
    void SetText(std::string_view raw);
    // EM_LIMITTEXT (0x467f30, la vtable + 0x3c de los EDIT): el tope de lo tecleado. Lo que ya tiene queda.
    void SetLimit(std::size_t maxBytes);
    // EM_SETSEL(0, −1): todo elegido. EM_SETSEL(−2, −1) y EM_SETSEL(10000, −1): el cursor al final, nada
    // elegido.
    void SelectAll();
    void Deselect();
    [[nodiscard]] bool AllSelected() const;

    // Lo que tiene. Vale hasta el próximo cambio.
    [[nodiscard]] std::string_view Value() const&;
    // Sobre un campo temporal no compila: la vista quedaría colgada.
    std::string_view Value() const&& = delete;
    // Lo que se dibuja: lo que tiene, o un asterisco por byte si es la clave.
    [[nodiscard]] std::string Shown() const;
    [[nodiscard]] std::size_t Size() const;
    [[nodiscard]] bool Masked() const;

private:
    // Lo elegido se va antes de la primera letra que entra.
    void ReplaceTheSelection();

    std::size_t m_maxBytes;
    bool m_masked;
    Charset m_charset;
    std::string m_value;
    bool m_allSelected = false;
};
} // namespace Client::UI
