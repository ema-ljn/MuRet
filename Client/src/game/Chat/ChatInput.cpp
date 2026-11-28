#include "Chat/ChatInput.h"

#include "Ui/OldControls.h"
#include "Ui/OldEditInput.h"
#include "Ui/OldEditLines.h"
#include "Ui/TextField.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>

namespace Client::Chat
{
namespace
{
// 0x468260: SW_HIDE with 3.
constexpr std::int32_t HiddenState = 3;
// 0x469450(0): the focus to the message one.
constexpr std::int32_t FocusedState = 0;
// 0x4693e0 and the vtable + 0x50: GetText with 0xff.
constexpr std::size_t GetTextLimit = 0xff;
// 0x467ee0: SetText only with fewer than 0x100 bytes.
constexpr std::size_t SetTextLimit = 0x100;

Ui::TextField& FieldOf(ChatInputState& input, ChatFocus focus)
{
    return focus == ChatFocus::Whisper ? input.whisper.text : input.message.text;
}

std::int32_t LengthOf(const ChatBuffer& buffer)
{
    return static_cast<std::int32_t>(CStringOf(buffer).size());
}

// 0x467ec0, the vtable + 0x50: WM_GETTEXT, up to `limit` bytes and the '\0'.
void GetTextInto(ChatBuffer& buffer, const Ui::TextField& field)
{
    const std::string_view value = field.Value();
    CopyIntoTheBuffer(buffer, value.substr(0, std::min(value.size(), GetTextLimit)));
}

// 0x467ee0, the vtable + 0x4c.
void SetTextOf(Ui::TextField& field, std::string_view text)
{
    const std::string_view value = text.substr(0, text.find('\0'));
    if (value.size() < SetTextLimit)
        field.SetText(value);
}

// 0x4682a0(with selection): the focus to the EDIT; with 1, EM_SETSEL(−2, −1) leaves the cursor at the end.
void SetFocusOn(ChatInputState& input, ChatFocus focus, std::int32_t* focusedList = nullptr)
{
    input.focus = focus;
    // 0x4682f1..0x4682f7: taking the focus also moves the arrows' list, even if the control is an EDIT.
    if (focusedList != nullptr)
        if (const Ui::OldEdit* edit = ChatEditWithFocus(input); edit != nullptr)
            *focusedList = edit->base.id;
}

// strncpy(target, source, 0xb): the first eleven bytes, with zeros after the '\0'.
void CopyTheName(ChatBuffer& target, const ChatBuffer& source)
{
    const std::size_t length = std::min(CStringOf(source).size(), WhisperNameCopy);
    std::copy_n(source.begin(), length, target.begin());
    std::fill_n(target.begin() + static_cast<std::ptrdiff_t>(length), WhisperNameCopy - length, '\0');
}
} // namespace

Ui::OldEdit* ChatEditWithFocus(ChatInputState& input)
{
    switch (input.focus)
    {
    case ChatFocus::Message:
        return &input.message;
    case ChatFocus::Whisper:
        return &input.whisper;
    case ChatFocus::Dialog:
        return &input.dialogEdit;
    case ChatFocus::NumberField:
        return &input.numberField;
    case ChatFocus::Game:
    case ChatFocus::OldWindow:
        return nullptr;
    }
    return nullptr;
}

std::string_view CStringOf(const ChatBuffer& buffer)
{
    const std::string_view all(buffer.data(), buffer.size());
    return all.substr(0, all.find('\0'));
}

void CopyIntoTheBuffer(ChatBuffer& buffer, std::string_view text)
{
    const std::string_view value = text.substr(0, std::min(text.find('\0'), buffer.size() - 1));
    std::copy(value.begin(), value.end(), buffer.begin());
    buffer[value.size()] = '\0';
}

void ResetTheTextBuffers(ChatInputState& input, bool second)
{
    input.field = MessageField;
    input.messageBuffer.fill('\0');
    input.messageLength = 0;
    if (second)
    {
        input.whisperBuffer.fill('\0');
        input.whisperLength = 0;
    }
    // 0x5535bd..0x5535db: the vtable + 0x4c (SetText) with 0 on both.
    input.dialogEdit.text.SetText({});
    input.numberField.text.SetText({});
}

void ClearTheChatFields(ChatInputState& input)
{
    input.message.text.Clear();
    input.whisper.text.Clear();
}

void FocusTheChatField(ChatInputState& input, std::int32_t field, std::int32_t* focusedList)
{
    if (!input.shown)
        return;
    const ChatFocus focus = field == WhisperField ? ChatFocus::Whisper : ChatFocus::Message;
    SetFocusOn(input, focus, focusedList);
    FieldOf(input, focus).SelectAll();
}

void ClickTheChatField(ChatInputState& input, std::int32_t field, std::int32_t* focusedList)
{
    const ChatFocus focus = field == WhisperField ? ChatFocus::Whisper : ChatFocus::Message;
    SetFocusOn(input, focus, focusedList);
    FieldOf(input, focus).Deselect();
}

void ShowTheChatFields(ChatInputState& input, std::int32_t state, std::int32_t* focusedList)
{
    input.shown = state != HiddenState;
    input.message.base.state = state;
    input.whisper.base.state = state;
    if (!input.shown)
    {
        // 0x468260(3) is ShowWindow(SW_HIDE) of the chat's two EDITs: the focus of another EDIT (the dialog's, the
        // number's or an old window's) does not change.
        if (input.focus == ChatFocus::Message || input.focus == ChatFocus::Whisper)
            input.focus = ChatFocus::Game;
        return;
    }
    if (state == FocusedState)
        SetFocusOn(input, ChatFocus::Message, focusedList);
}

void ShowTheDialogField(ChatInputState& input, std::int32_t state)
{
    input.dialogEdit.base.state = state;
    if (state == HiddenState && input.focus == ChatFocus::Dialog)
        input.focus = ChatFocus::Game;
}

void FocusTheDialogField(ChatInputState& input)
{
    if (input.dialogEdit.base.state != HiddenState)
        SetFocusOn(input, ChatFocus::Dialog);
}

void ReadTheDialogField(const ChatInputState& input, ChatBuffer& buffer)
{
    GetTextInto(buffer, input.dialogEdit.text);
}

void ShowTheNumberField(ChatInputState& input, std::int32_t state)
{
    input.numberField.base.state = state;
    if (state == HiddenState && input.focus == ChatFocus::NumberField)
        input.focus = ChatFocus::Game;
}

void FocusTheNumberField(ChatInputState& input)
{
    if (input.numberField.base.state != HiddenState)
        SetFocusOn(input, ChatFocus::NumberField);
}

void ReadTheNumberField(const ChatInputState& input, ChatBuffer& buffer)
{
    GetTextInto(buffer, input.numberField.text);
}

void ClearTheNumberField(ChatInputState& input)
{
    SetTextOf(input.numberField.text, {});
}

void ResetTheChatInput(ChatInputState& input, std::int32_t* focusedList)
{
    input.field = MessageField;
    FocusTheChatField(input, MessageField, focusedList);
    ClearTheChatFields(input);
    TrimTheHistory(input.history, true);
}

void ReadTheChatFields(ChatInputState& input)
{
    GetTextInto(input.messageBuffer, input.message.text);
    GetTextInto(input.whisperBuffer, input.whisper.text);
}

void SetTheChatFields(ChatInputState& input, std::optional<std::string_view> message,
                      std::optional<std::string_view> whisper)
{
    if (message)
        SetTextOf(input.message.text, *message);
    if (whisper)
        SetTextOf(input.whisper.text, *whisper);
}

void RememberTheMessage(ChatInputState& input, std::string_view said)
{
    const std::string_view value = said.substr(0, said.find('\0'));
    if (value.empty())
        return;
    ChatHistory& history = input.history;
    history.said.emplace_front(value);
    history.at = 0;
    history.saved = false;
    TrimTheHistory(history, false);
}

void TrimTheHistory(ChatHistory& history, bool all)
{
    if (all)
    {
        history.said.clear();
        history.at = 0;
        return;
    }
    while (history.said.size() > ChatHistoryLimit)
        history.said.pop_back();
    history.at = std::min(history.at, history.said.size());
}

bool RecallFromTheHistory(ChatInputState& input, std::int32_t steps)
{
    ChatHistory& history = input.history;
    if (history.said.empty())
        return false;
    bool sound = false;
    if (steps == NewestInTheHistory)
    {
        history.at = 0;
        history.saved = false;
    }
    else if (steps < 0)
    {
        sound = true;
        for (std::int32_t step = steps; step < 0; ++step)
        {
            if (history.at == history.said.size())
                continue;
            if (!history.saved)
            {
                history.typing = std::string(input.message.text.Value().substr(0, GetTextLimit));
                history.saved = true;
            }
            else
                ++history.at;
        }
        if (history.at == history.said.size())
            --history.at;
    }
    else
    {
        sound = true;
        for (std::int32_t step = 0; step < steps; ++step)
        {
            if (history.at == 0)
            {
                if (!history.saved)
                    return sound;
                SetTextOf(input.message.text, history.typing);
                input.message.text.Deselect();
                history.saved = false;
                return sound;
            }
            --history.at;
        }
    }
    SetTextOf(input.message.text, history.said[history.at]);
    input.message.text.Deselect();
    return sound;
}

bool PressTabInTheChat(ChatInputState& input, bool chatOpen, std::int32_t* focusedList)
{
    if (!chatOpen)
        return false;
    if (input.fieldCount > 1)
    {
        input.field = (input.field + 1) % input.fieldCount;
        FocusTheChatField(input, input.field, focusedList);
        return true;
    }
    if ((input.field == MessageField ? input.messageLength : input.whisperLength) == 0)
        input.otherInput = !input.otherInput;
    return false;
}

void TypeInTheChat(ChatInputState& input, std::string_view typed)
{
    Ui::OldEdit* edit = ChatEditWithFocus(input);
    if (edit == nullptr || typed.empty())
        return;
    Ui::SetTheEditComposition(*edit, {}, 0, 0);
    Ui::TypeInTheEdit(*edit, typed);
}

void BackspaceInTheChat(ChatInputState& input)
{
    Ui::OldEdit* edit = ChatEditWithFocus(input);
    if (edit == nullptr)
        return;
    edit->draws = 0;
    if (!edit->compositionText.empty())
        return;
    Ui::BackspaceInTheEdit(*edit);
}

bool StepTheWhisperName(ChatInputState& input, std::int32_t direction, std::int32_t* focusedList)
{
    const std::int32_t next = input.whisperNameAt + direction;
    if (next < 0 || next > LastWhisperNameSlot)
        return false;
    CopyTheName(input.whisperNames[static_cast<std::size_t>(input.whisperNameAt)], input.whisperBuffer);
    input.whisperBuffer.fill('\0');
    input.whisperNameAt = next;
    const ChatBuffer& slot = input.whisperNames[static_cast<std::size_t>(next)];
    SetTheChatFields(input, std::nullopt, CStringOf(slot));
    if (input.field % 2 == WhisperField)
        FocusTheChatField(input, WhisperField, focusedList);
    CopyTheName(input.whisperBuffer, slot);
    input.whisperLength = LengthOf(input.whisperBuffer);
    return true;
}

void PutTheWhisperName(ChatInputState& input, std::string_view name)
{
    CopyIntoTheBuffer(input.whisperBuffer, name);
    input.whisperLength = LengthOf(input.whisperBuffer);
}
} // namespace Client::Chat
