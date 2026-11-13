#include "Scenes/JewelMixAnswers.h"

#include "App/WindowState.h"
#include "Core/Expected.h"
#include "Protocol/JewelMix.h"
#include "Scenes/GameMessages.h"
#include "Scenes/JewelMix.h"
#include "Scenes/WorldEntry.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/Dialogs.h"
#include "UI/GameText.h"
#include "UI/MessageArea.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace Client::Scenes
{
namespace
{
// Lo que hace cada resultado.
enum class AnswerKind : std::uint8_t
{
    Sentence,     // el sprintf con el texto, 1816 y 868
    ResultDialog, // el 0xa6
    Message,      // el texto solo
};

struct AnswerCase
{
    AnswerKind kind = AnswerKind::Message;
    std::size_t text = 0;
};

// Los textos de Text.bmd (0x76bd1dc + 0x12c · índice).
constexpr std::size_t MixFailedText = 1801;      // 0x7741068
constexpr std::size_t UnmixFailedText = 1800;    // 0x7740f3c
constexpr std::size_t SentenceMiddleText = 1816; // 0x77421fc
constexpr std::size_t SentenceEndText = 868;     // 0x76fcb0c
// 0x6c8490 y 0x6c8498: "%s%s %s".
constexpr std::string_view SentenceSeparator = " ";
// [EBP − 0x17c] de 0x4e2b10 y [EBP − 0x100] de 0x4da540: 0x40 dwords en 0.
constexpr std::size_t SentenceBufferSize = 0x100;
// 0x4e45e0 y 0x4da5b6: el diálogo del resultado.
constexpr std::int32_t ResultDialog = 0xa6;
constexpr std::uint8_t MixSubcode = Protocol::JewelMix::MixSubcode;

// 0x4e60a8: 0x4e4592 (0, 2 y 3), 0x4e45d9 (1), 0x4e45f9 (4, el 1817 en 0x7742328) y 0x4e461e (5, el 1811 en
// 0x7741c20).
constexpr std::array<AnswerCase, 6> MixAnswers{{{AnswerKind::Sentence, MixFailedText},
                                                {AnswerKind::ResultDialog, 0},
                                                {AnswerKind::Sentence, MixFailedText},
                                                {AnswerKind::Sentence, MixFailedText},
                                                {AnswerKind::Message, 1817},
                                                {AnswerKind::Message, 1811}}};
// 0x4da620: 0x4da570 (0 y 5), 0x4da5af (1), 0x4da5c7 (2, 3, 4 y 6, el 1812 en 0x7741d4c), 0x4da5e4 (7, el 1815 en
// 0x77420d0) y 0x4da601 (8, el 1811).
constexpr std::array<AnswerCase, 9> UnmixAnswers{{{AnswerKind::Sentence, UnmixFailedText},
                                                  {AnswerKind::ResultDialog, 0},
                                                  {AnswerKind::Message, 1812},
                                                  {AnswerKind::Message, 1812},
                                                  {AnswerKind::Message, 1812},
                                                  {AnswerKind::Sentence, UnmixFailedText},
                                                  {AnswerKind::Message, 1812},
                                                  {AnswerKind::Message, 1815},
                                                  {AnswerKind::Message, 1811}}};

Core::Expected<std::string_view, std::string> TextOf(const WorldEntryResources& resources, std::size_t index,
                                                     std::string_view where)
{
    const std::optional<std::string_view> text = UI::TextAt(resources.texts, index);
    if (!text)
        return Core::Unexpected(std::format("{}: sin el texto {} de Text.bmd", where, index));
    return *text;
}

// 0x555190(cadena vacía, texto, 1).
Core::Expected<void, std::string> Show(const WorldEntryResources& resources, std::string_view text,
                                       std::string_view where)
{
    if (resources.textMeasure == nullptr)
        return Core::Unexpected(std::format("{}: el mensaje sin el medidor de los textos", where));
    const App::PixelSize screen = resources.screen ? resources.screen() : App::PixelSize{640, 480};
    return AddGameMessage(resources.world.Runtime(), *resources.textMeasure, screen, {}, text, UI::SystemMessage);
}

// 0x4e4592..0x4e45c0 y 0x4da570..0x4da59e.
Core::Expected<void, std::string> ShowTheSentence(const WorldEntryResources& resources, std::size_t first,
                                                  std::string_view where)
{
    std::string sentence;
    for (const std::size_t index : {first, SentenceMiddleText, SentenceEndText})
    {
        auto text = TextOf(resources, index, where);
        if (!text.HasValue())
            return Core::Unexpected(std::move(text).Error());
        if (index == SentenceEndText)
            sentence += SentenceSeparator;
        sentence += text.Value();
    }
    if (sentence.size() >= SentenceBufferSize)
        return Core::Unexpected(std::format("{}: \"%s%s %s\" escribe {} bytes en el buffer de {:#x}", where,
                                            sentence.size() + 1, SentenceBufferSize));
    return Show(resources, sentence, where);
}
} // namespace

Core::Expected<void, std::string> ApplyJewelMixAnswer(const Protocol::JewelMix::JewelMixAnswer& answer,
                                                      const WorldEntryResources& resources)
{
    if (answer.subcode != MixSubcode && answer.subcode != Protocol::JewelMix::UnmixSubcode)
        return {};
    const bool mixing = answer.subcode == MixSubcode;
    const std::span<const AnswerCase> cases =
        mixing ? std::span<const AnswerCase>(MixAnswers) : std::span<const AnswerCase>(UnmixAnswers);
    if (answer.result >= cases.size())
        return {};
    const AnswerCase& chosen = cases[answer.result];
    const std::string where = std::format("{}({})", mixing ? "0x4e4570" : "0x4da540", answer.result);
    WorldRuntime& runtime = resources.world.Runtime();
    switch (chosen.kind)
    {
    case AnswerKind::ResultDialog:
        UI::PushDialog(runtime.dialogs, 0);
        UI::PushDialog(runtime.dialogs, ResultDialog);
        return {};
    case AnswerKind::Sentence:
        if (auto shown = ShowTheSentence(resources, chosen.text, where); !shown.HasValue())
            return shown;
        break;
    case AnswerKind::Message:
    {
        auto text = TextOf(resources, chosen.text, where);
        if (!text.HasValue())
            return Core::Unexpected(std::move(text).Error());
        if (auto shown = Show(resources, text.Value(), where); !shown.HasValue())
            return shown;
        break;
    }
    }
    // 0x4e45c8, 0x4e460d, 0x4e4632, 0x4da5a6, 0x4da5db, 0x4da5f8 y 0x4da615.
    return ReturnToTheJewelChoice(resources.world, resources.sendNpcWindowPacket);
}
} // namespace Client::Scenes
