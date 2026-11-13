#include "UI/ChatInput.h"

#include "UI/TextField.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>

namespace Client::UI
{
namespace
{
// 0x468260: SW_HIDE con 3.
constexpr std::int32_t HiddenState = 3;
// 0x469450(0): el foco al del mensaje.
constexpr std::int32_t FocusedState = 0;
// 0x4693e0 y la vtable + 0x50: GetText con 0xff.
constexpr std::size_t GetTextLimit = 0xff;
// 0x467ee0: SetText solo con menos de 0x100 bytes.
constexpr std::size_t SetTextLimit = 0x100;

TextField& FieldOf(ChatInputState& input, ChatFocus focus)
{
    if (focus == ChatFocus::DialogField)
        return input.dialogField;
    return focus == ChatFocus::Whisper ? input.whisper : input.message;
}

std::int32_t LengthOf(const ChatBuffer& buffer)
{
    return static_cast<std::int32_t>(CStringOf(buffer).size());
}

// 0x467ec0, la vtable + 0x50: WM_GETTEXT, hasta `limit` bytes y el '\0'.
void GetTextInto(ChatBuffer& buffer, const TextField& field)
{
    const std::string_view value = field.Value();
    CopyIntoTheBuffer(buffer, value.substr(0, std::min(value.size(), GetTextLimit)));
}

// 0x467ee0, la vtable + 0x4c.
void SetTextOf(TextField& field, std::string_view text)
{
    const std::string_view value = text.substr(0, text.find('\0'));
    if (value.size() < SetTextLimit)
        field.SetText(value);
}

// 0x4682a0(con selección): el foco al EDIT; con 1, EM_SETSEL(−2, −1) deja el cursor al final.
void SetFocusOn(ChatInputState& input, ChatFocus focus)
{
    input.focus = focus;
}

// strncpy(destino, origen, 0xb): los once primeros bytes, con ceros detrás del '\0'.
void CopyTheName(ChatBuffer& target, const ChatBuffer& source)
{
    const std::size_t length = std::min(CStringOf(source).size(), WhisperNameCopy);
    std::copy_n(source.begin(), length, target.begin());
    std::fill_n(target.begin() + static_cast<std::ptrdiff_t>(length), WhisperNameCopy - length, '\0');
}
} // namespace

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
    SetTextOf(input.dialogField, {});
    if (!second)
        return;
    input.whisperBuffer.fill('\0');
    input.whisperLength = 0;
}

void ClearTheChatFields(ChatInputState& input)
{
    input.message.Clear();
    input.whisper.Clear();
}

void FocusTheChatField(ChatInputState& input, std::int32_t field)
{
    if (!input.shown)
        return;
    const ChatFocus focus = field == WhisperField ? ChatFocus::Whisper : ChatFocus::Message;
    SetFocusOn(input, focus);
    FieldOf(input, focus).SelectAll();
}

void ClickTheChatField(ChatInputState& input, std::int32_t field)
{
    const ChatFocus focus = field == WhisperField ? ChatFocus::Whisper : ChatFocus::Message;
    SetFocusOn(input, focus);
    FieldOf(input, focus).Deselect();
}

void ShowTheChatFields(ChatInputState& input, std::int32_t state)
{
    input.shown = state != HiddenState;
    if (!input.shown)
    {
        input.focus = ChatFocus::Game;
        return;
    }
    if (state == FocusedState)
        SetFocusOn(input, ChatFocus::Message);
}

void ShowTheDialogField(ChatInputState& input, std::int32_t state)
{
    input.dialogFieldShown = state != HiddenState;
    if (!input.dialogFieldShown && input.focus == ChatFocus::DialogField)
        input.focus = ChatFocus::Game;
}

void FocusTheDialogField(ChatInputState& input)
{
    if (input.dialogFieldShown)
        SetFocusOn(input, ChatFocus::DialogField);
}

void ReadTheDialogField(const ChatInputState& input, ChatBuffer& buffer)
{
    GetTextInto(buffer, input.dialogField);
}

void ResetTheChatInput(ChatInputState& input)
{
    input.field = MessageField;
    FocusTheChatField(input, MessageField);
    ClearTheChatFields(input);
    TrimTheHistory(input.history, true);
}

void ReadTheChatFields(ChatInputState& input)
{
    GetTextInto(input.messageBuffer, input.message);
    GetTextInto(input.whisperBuffer, input.whisper);
}

void SetTheChatFields(ChatInputState& input, std::optional<std::string_view> message,
                      std::optional<std::string_view> whisper)
{
    if (message)
        SetTextOf(input.message, *message);
    if (whisper)
        SetTextOf(input.whisper, *whisper);
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
                history.typing = std::string(input.message.Value().substr(0, GetTextLimit));
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
                SetTextOf(input.message, history.typing);
                input.message.Deselect();
                history.saved = false;
                return sound;
            }
            --history.at;
        }
    }
    SetTextOf(input.message, history.said[history.at]);
    input.message.Deselect();
    return sound;
}

bool PressTabInTheChat(ChatInputState& input, bool chatOpen)
{
    if (!chatOpen)
        return false;
    if (input.fieldCount > 1)
    {
        input.field = (input.field + 1) % input.fieldCount;
        FocusTheChatField(input, input.field);
        return true;
    }
    if ((input.field == MessageField ? input.messageLength : input.whisperLength) == 0)
        input.otherInput = !input.otherInput;
    return false;
}

void TypeInTheChat(ChatInputState& input, std::string_view typed)
{
    if (input.focus == ChatFocus::Game || input.focus == ChatFocus::OldWindow || typed.empty())
        return;
    FieldOf(input, input.focus).Type(typed);
}

void BackspaceInTheChat(ChatInputState& input)
{
    if (input.focus == ChatFocus::Game || input.focus == ChatFocus::OldWindow)
        return;
    FieldOf(input, input.focus).Backspace();
}

bool StepTheWhisperName(ChatInputState& input, std::int32_t direction)
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
        FocusTheChatField(input, WhisperField);
    CopyTheName(input.whisperBuffer, slot);
    input.whisperLength = LengthOf(input.whisperBuffer);
    return true;
}

void PutTheWhisperName(ChatInputState& input, std::string_view name)
{
    CopyIntoTheBuffer(input.whisperBuffer, name);
    input.whisperLength = LengthOf(input.whisperBuffer);
}
} // namespace Client::UI
