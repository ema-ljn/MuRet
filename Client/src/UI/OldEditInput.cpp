#include "UI/OldEditInput.h"

#include "Core/Windows1252.h"
#include "Platform/IClipboard.h"
#include "UI/OldControls.h"
#include "UI/OldEditLines.h"
#include "UI/OldEditWordMovement.h"
#include "UI/TextField.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace Client::UI
{
void ApplyKeyToTheEdit(OldEdit& edit, OldEditKey key, bool shift, bool ctrl, IClipboard* clipboard,
                       const Render::Text::ITextMeasure* measure, const std::function<void(std::int32_t)>& focusNext)
{
    switch (key)
    {
    case OldEditKey::Backspace:
    case OldEditKey::Left:
    case OldEditKey::Right:
    case OldEditKey::Up:
    case OldEditKey::Down:
    case OldEditKey::Tab:
    case OldEditKey::Copy:
    case OldEditKey::Cut:
    case OldEditKey::Paste:
    case OldEditKey::CtrlA:
        edit.draws = 0;
        break;
    case OldEditKey::Delete:
    case OldEditKey::Home:
    case OldEditKey::End:
    case OldEditKey::PageUp:
    case OldEditKey::PageDown:
        break;
    }
    if (!edit.compositionText.empty())
        return;
    // CL9 hito 10: el EDIT de varios renglones (el cuerpo de la carta) mueve el cursor por renglones.
    const bool lines = edit.multiline == 1 && measure != nullptr;
    const auto moveInTheLines = [&](EditCaretKey move)
    {
        if (lines)
            MoveTheEditCaret(edit, *measure, move, shift);
        return lines;
    };
    switch (key)
    {
    case OldEditKey::Backspace:
        if (ctrl)
            edit.text.InsertBytes(std::string(1, '\x7f'));
        else
            BackspaceInTheEdit(edit);
        break;
    case OldEditKey::Delete:
        // Ctrl+Delete borra hasta el final solo sin selección; con una, EDIT borra únicamente el rango elegido.
        if (ctrl && !edit.text.HasSelection())
            edit.text.Select(edit.text.Cursor(), edit.text.Size());
        DeleteInTheEdit(edit);
        break;
    case OldEditKey::PageUp:
        static_cast<void>(moveInTheLines(EditCaretKey::PageUp));
        break;
    case OldEditKey::PageDown:
        static_cast<void>(moveInTheLines(EditCaretKey::PageDown));
        break;
    case OldEditKey::Left:
        if (ctrl)
            edit.text.SetCursor(OldEditWordMovement::CursorAfterCtrlLeft(edit.text.Value(), edit.text.Cursor()), shift);
        else if (!moveInTheLines(EditCaretKey::Left))
            edit.text.MoveLeft(shift);
        break;
    case OldEditKey::Right:
        if (ctrl)
            edit.text.SetCursor(OldEditWordMovement::CursorAfterCtrlRight(edit.text.Value(), edit.text.Cursor()),
                                shift);
        else if (!moveInTheLines(EditCaretKey::Right))
            edit.text.MoveRight(shift);
        break;
    case OldEditKey::Home:
        // Ctrl+Inicio y Ctrl+Fin van al principio y al final de todo el texto.
        if (ctrl || !moveInTheLines(EditCaretKey::Home))
            edit.text.MoveHome(shift);
        break;
    case OldEditKey::End:
        if (ctrl || !moveInTheLines(EditCaretKey::End))
            edit.text.MoveEnd(shift);
        break;
    case OldEditKey::Up:
        static_cast<void>(moveInTheLines(EditCaretKey::Up));
        break;
    case OldEditKey::Down:
        static_cast<void>(moveInTheLines(EditCaretKey::Down));
        break;
    case OldEditKey::Tab:
        if (edit.base.parent != 0 && edit.next != 0 && focusNext)
            focusNext(edit.next);
        break;
    case OldEditKey::Copy:
    case OldEditKey::Cut:
        if (!ctrl || clipboard == nullptr || !edit.text.HasSelection())
            break;
        if (const std::string copied = Core::DecodeWindows1252(edit.text.Selected());
            clipboard->WriteText(copied) && key == OldEditKey::Cut)
        {
            edit.draws = 0;
            edit.text.Backspace();
        }
        break;
    case OldEditKey::Paste:
        if (!ctrl || clipboard == nullptr)
            break;
        if (std::optional<std::string> pasted = clipboard->ReadText(); pasted.has_value())
            PasteInTheEdit(edit, *pasted);
        break;
    case OldEditKey::CtrlA:
        // El Ctrl+A del EDIT XP es no-op: no reemplaza ni mueve una selección existente.
        break;
    }
}

void SetTheEditComposition(OldEdit& edit, std::string_view text, std::int32_t start, std::int32_t length)
{
    edit.compositionText.assign(text);
    edit.compositionStart = text.empty() ? 0 : start;
    edit.compositionLength = text.empty() ? 0 : length;
}
} // namespace Client::UI
