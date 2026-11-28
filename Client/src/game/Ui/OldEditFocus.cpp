#include "Ui/OldEditFocus.h"

#include "Chat/ChatInput.h"
#include "Ui/OldControls.h"
#include "Ui/OldEditInput.h"
#include "Ui/OldEditLines.h"
#include "Ui/OldWindowBase.h"
#include "Ui/OldWindows.h"

#include <cstdint>
#include <string_view>

namespace Client::Ui
{
namespace
{
OldEdit* EditWithId(OldWindowManager& manager, std::int32_t id)
{
    for (auto& [windowId, window] : manager.windows)
    {
        if (window.kind == OldWindowKind::AddFriendBox && window.add.edit.base.id == id)
            return &window.add.edit;
        // CL9 milestone 10: the EDIT of the chat with a friend and the three of the letter-writing window.
        if (window.kind == OldWindowKind::ChatWindow && window.chat.edit.base.id == id)
            return &window.chat.edit;
        if (window.kind != OldWindowKind::LetterWriteWindow)
            continue;
        for (OldEdit* edit : {&window.letter.to, &window.letter.subject, &window.letter.text})
        {
            if (edit->base.id == id)
                return edit;
        }
    }
    return nullptr;
}

OldEdit* FocusedEdit(OldWindowManager& manager)
{
    if (manager.focusedEditIsOrphan)
        return manager.orphanedEdit ? &*manager.orphanedEdit : nullptr;
    return EditWithId(manager, manager.focusedEdit);
}
} // namespace

bool HasAnOldEditFocused(const OldWindowManager& manager, ChatFocus focus)
{
    if (focus != ChatFocus::OldWindow)
        return false;
    if (manager.focusedEditIsOrphan)
        return manager.orphanedEdit.has_value();
    for (const auto& [id, window] : manager.windows)
    {
        if (window.kind == OldWindowKind::AddFriendBox && window.add.edit.base.id == manager.focusedEdit)
            return true;
        // CL9 milestone 10: the EDIT of the chat with a friend and the three of the letter-writing window.
        if (window.kind == OldWindowKind::ChatWindow && window.chat.edit.base.id == manager.focusedEdit)
            return true;
        if (window.kind == OldWindowKind::LetterWriteWindow &&
            (window.letter.to.base.id == manager.focusedEdit || window.letter.subject.base.id == manager.focusedEdit ||
             window.letter.text.base.id == manager.focusedEdit))
            return true;
    }
    return false;
}

void TypeInTheOldEdit(OldWindowManager& manager, std::string_view typed)
{
    if (OldEdit* edit = FocusedEdit(manager); edit != nullptr)
    {
        edit->compositionText.clear();
        edit->compositionStart = 0;
        edit->compositionLength = 0;
        TypeInTheEdit(*edit, typed);
    }
}

void BackspaceInTheOldEdit(OldWindowManager& manager)
{
    if (OldEdit* edit = FocusedEdit(manager); edit != nullptr)
    {
        edit->draws = 0;
        if (!edit->compositionText.empty())
            return;
        edit->compositionText.clear();
        edit->compositionStart = 0;
        edit->compositionLength = 0;
        BackspaceInTheEdit(*edit);
    }
}

void ApplyKeyToTheOldEdit(OldWindowManager& manager, OldEditKey key, bool shift, bool ctrl,
                          Platform::IClipboard* clipboard, const Render::Text::ITextMeasure* measure,
                          std::int32_t* focusedList)
{
    OldEdit* edit = FocusedEdit(manager);
    if (edit == nullptr)
        return;
    ApplyKeyToTheEdit(*edit, key, shift, ctrl, clipboard, measure,
                      [&manager, focusedList](std::int32_t id)
                      {
                          if (OldEdit* next = EditWithId(manager, id); next != nullptr)
                          {
                              manager.orphanedEdit.reset();
                              manager.focusedEditIsOrphan = false;
                              manager.focusedEdit = next->base.id;
                              if (focusedList != nullptr)
                                  *focusedList = next->base.id;
                              next->text.SetCursor(next->text.Size());
                              next->draws = 0;
                          }
                      });
}

void SetTheOldEditComposition(OldWindowManager& manager, std::string_view text, std::int32_t start, std::int32_t length)
{
    OldEdit* edit = FocusedEdit(manager);
    if (edit == nullptr)
        return;
    SetTheEditComposition(*edit, text, start, length);
}

void PressEnterInTheOldEdit(OldWindowManager& manager)
{
    OldEdit* edit = FocusedEdit(manager);
    if (edit == nullptr)
        return;
    edit->draws = 0;
    if (!edit->compositionText.empty())
        return;
    // 0x467b10: with +0x150 of the window list at 1 (F5, which also reads it), with the vtable + 0x5c at 1 (+0xb4, the
    // chat with a friend closed, 0x467c83) or with +0xc4 at 1 (0x467c8f), Enter goes to the Windows EDIT: the multiline
    // one breaks the line and the single-line one does nothing. Otherwise, the 7 to the parent.
    if (manager.windowList.list.armed == 1 || edit->closed == 1 || edit->multiline == 1)
    {
        BreakTheEditLine(*edit);
        return;
    }
    if (manager.focusedEditIsOrphan)
        return;
    PostToTheWindow(manager, edit->base.parent, EnterMessage, 0, 0);
}
} // namespace Client::Ui
