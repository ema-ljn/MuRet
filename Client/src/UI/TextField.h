#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

// Un campo de texto de la UI (CL5.5): cuenta/clave usan ASCII imprimible; Latin1 conserva el perfil
// previo del nombre de personaje, y Windows1252Xp usa el NLS XP para texto occidental del chat. El
// límite cuenta bytes, como EM_LIMITTEXT del original. La tabla está en test_text_field.cpp.
//
// Sin operator<<: lo que tiene no se imprime nunca, ni al log. El PrintTo de los tests
// (tests/support/UiPrinters.h) escribe el largo.
namespace Client::UI
{
class TextField
{
public:
    // Qué letras toma: ASCII imprimible; Latin-1 U+00A0 a U+00FF como su byte; o el perfil CP1252
    // Windows XP, incluidos best-fit y '?' para texto sin representación.
    enum class Charset : std::uint8_t
    {
        Ascii,
        Latin1,
        Windows1252Xp,
    };

    TextField(std::size_t maxBytes, bool masked, Charset charset = Charset::Ascii);

    // Suma texto UTF-8 hasta llenar el campo. En Windows1252Xp rechaza la secuencia completa si no
    // es UTF-8 válido; el primer byte insertado reemplaza lo elegido.
    void Type(std::string_view utf8);
    // Inserta bytes ya codificados, incluidos los de control, respetando selección y límite.
    void InsertBytes(std::string_view bytes);
    // Convierte todo el UTF-8 al CP1252 del XP, corta en CR o NUL y respeta el límite por byte.
    // Una conversión válida vacía también reemplaza la selección.
    void PasteUTF8(std::string_view utf8);
    // Borra la selección, o el byte anterior al cursor.
    void Backspace();
    // Lo mismo que Backspace, pero borra el byte siguiente al cursor.
    void Delete();
    void Clear();
    // WM_SETTEXT (la vtable + 0x4c de los EDIT, 0x467ee0): pone `raw` tal cual, en bytes, sin el tope de
    // EM_LIMITTEXT, que solo frena lo tecleado. Deja el cursor al final y nada elegido.
    void SetText(std::string_view raw);
    // Los bytes de `raw` tal cual, en el cursor o en lugar de lo elegido, solo si entran todos: el "\r\n" de Enter en
    // el EDIT de varios renglones (UI/OldEditLines.h).
    void Insert(std::string_view raw);
    // EM_LIMITTEXT (0x467f30, la vtable + 0x3c de los EDIT): el tope de lo tecleado. Lo que ya tiene queda.
    void SetLimit(std::size_t maxBytes);
    // EM_SETSEL(0, −1): todo elegido. EM_SETSEL(−2, −1) y EM_SETSEL(10000, −1): el cursor al final, nada
    // elegido.
    void SelectAll();
    void Deselect();
    [[nodiscard]] bool AllSelected() const;
    [[nodiscard]] std::size_t Cursor() const;
    [[nodiscard]] std::size_t SelectionBegin() const;
    [[nodiscard]] std::size_t SelectionEnd() const;
    [[nodiscard]] bool HasSelection() const;
    void SetCursor(std::size_t position, bool extend = false);
    void Select(std::size_t anchor, std::size_t caret);
    void MoveLeft(bool extend = false);
    void MoveRight(bool extend = false);
    void MoveHome(bool extend = false);
    void MoveEnd(bool extend = false);

    // Lo que tiene. Vale hasta el próximo cambio.
    [[nodiscard]] std::string_view Value() const&;
    // Sobre un campo temporal no compila: la vista quedaría colgada.
    std::string_view Value() const&& = delete;
    // Los bytes elegidos. Vale hasta el próximo cambio.
    [[nodiscard]] std::string_view Selected() const&;
    std::string_view Selected() const&& = delete;
    // Lo que se dibuja: lo que tiene, o un asterisco por byte si es la clave.
    [[nodiscard]] std::string Shown() const;
    [[nodiscard]] std::size_t Size() const;
    [[nodiscard]] bool Masked() const;

private:
    void EraseSelection();

    std::size_t m_maxBytes;
    bool m_masked;
    Charset m_charset;
    std::string m_value;
    std::size_t m_anchor = 0;
    std::size_t m_cursor = 0;
    bool m_allSelected = false;
};
} // namespace Client::UI
