#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

// A UI text field (CL5.5): account/password use printable ASCII; Latin1 keeps the previous character name profile,
// and Windows1252Xp uses the XP NLS for Western chat text. The limit counts bytes, like the original's EM_LIMITTEXT.
// The table is in test_text_field.cpp.
//
// No operator<<: its contents are never printed, not even to the log. The tests' PrintTo
// (tests/support/UiPrinters.h) writes the length.
namespace Client::Ui
{
class TextField
{
public:
    // Printable ASCII; Latin-1 U+00A0 to U+00FF as its byte; or the Windows XP CP1252 profile, best-fit and '?' for
    // unrepresentable text included.
    enum class Charset : std::uint8_t
    {
        Ascii,
        Latin1,
        Windows1252Xp,
    };

    TextField(std::size_t maxBytes, bool masked, Charset charset = Charset::Ascii);

    // Adds UTF-8 text until the field is full. In Windows1252Xp it rejects the whole sequence if it is not valid UTF-8;
    // the first inserted byte replaces the selection.
    void Type(std::string_view utf8);
    // Already encoded bytes, control ones included, honoring the selection and the limit.
    void InsertBytes(std::string_view bytes);
    // Converts all the UTF-8 to XP's CP1252, cuts at CR or NUL and honors the byte limit. A valid empty conversion also
    // replaces the selection.
    void PasteUTF8(std::string_view utf8);
    // The selection, or the byte before the cursor.
    void Backspace();
    // Like Backspace, but the byte after the cursor.
    void Delete();
    void Clear();
    // WM_SETTEXT (the EDITs' vtable + 0x4c, 0x467ee0): sets `raw` as is, in bytes, without the EM_LIMITTEXT cap, which
    // only stops what is typed. Leaves the cursor at the end and nothing selected.
    void SetText(std::string_view raw);
    // The bytes of `raw` as is, at the cursor or in place of the selection, only if they all fit: Enter's "\r\n" in the
    // multi-line EDIT (Ui/OldEditLines.h).
    void Insert(std::string_view raw);
    // EM_LIMITTEXT (0x467f30, the EDITs' vtable + 0x3c): the cap on what is typed. What it already has stays.
    void SetLimit(std::size_t maxBytes);
    // EM_SETSEL(0, −1): all selected. EM_SETSEL(−2, −1) and EM_SETSEL(10000, −1): the cursor at the end, nothing
    // selected.
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

    // Valid until the next change.
    [[nodiscard]] std::string_view Value() const&;
    // Does not compile on a temporary field: the view would dangle.
    std::string_view Value() const&& = delete;
    // Valid until the next change.
    [[nodiscard]] std::string_view Selected() const&;
    std::string_view Selected() const&& = delete;
    // What is drawn: its contents, or an asterisk per byte for the password.
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
} // namespace Client::Ui
