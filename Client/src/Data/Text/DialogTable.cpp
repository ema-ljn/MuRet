#include "Data/Text/DialogTable.h"

#include "Core/Expected.h"
#include "Core/Xor3.h"
#include "Data/DataError.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace Client::Data::Text
{
namespace
{
constexpr std::size_t AnswerCountOffset = 0x12c;
constexpr std::size_t NextOffset = 0x130;
constexpr std::size_t ActionsOffset = 0x158;
constexpr std::size_t AnswersOffset = 0x180;

std::int32_t IntAt(std::span<const std::byte> bytes, std::size_t offset)
{
    std::uint32_t value = 0;
    for (std::size_t i = 0; i < 4; ++i)
        value |= std::to_integer<std::uint32_t>(bytes[offset + i]) << (8 * i);
    return static_cast<std::int32_t>(value);
}

std::string_view UpToZero(std::span<const char> text)
{
    const std::string_view view(text.data(), text.size());
    return view.substr(0, view.find('\0'));
}
} // namespace

std::string_view DialogTextOf(const DialogRecord& dialog)
{
    return UpToZero(dialog.text);
}

std::string_view DialogAnswerOf(const DialogRecord& dialog, std::size_t answer)
{
    if (answer >= DialogAnswerCount)
        return {};
    return UpToZero(dialog.answers[answer]);
}

Core::Expected<DialogTable, DataError> LoadDialogTable(std::span<const std::byte> file)
{
    // Con menos de 0x400 bytes, el resto del primer bloque es el de operator new sin iniciar.
    if (file.size() < DialogRecordSize)
        return Core::Unexpected(DataError::FileTooShort);
    DialogTable table(DialogCount);
    // 0x54b32b..0x54b338: un solo bloque de operator new para los 200.
    std::array<std::byte, DialogRecordSize> block{};
    for (std::size_t i = 0; i < DialogCount; ++i)
    {
        // 0x54b343..0x54b37e: fread de 0x400 en el bloque, 0x5496c0 sobre él y la copia a 0x7688ac0 + 0x400 i. El
        // fread de la CRT (0x69fecb) copia lo que queda del archivo al principio del bloque y deja el resto, que es lo
        // del registro anterior ya descifrado.
        const std::size_t start = std::min(i * DialogRecordSize, file.size());
        std::ranges::copy(file.subspan(start, std::min(DialogRecordSize, file.size() - start)), block.begin());
        Core::Xor3(block);
        const std::span<const std::byte> bytes(block);
        DialogRecord& dialog = table[i];
        const auto toChar = [](std::byte letter) { return static_cast<char>(letter); };
        std::ranges::transform(bytes.first(DialogTextSize), dialog.text.begin(), toChar);
        dialog.answerCount = IntAt(bytes, AnswerCountOffset);
        if (dialog.answerCount > static_cast<std::int32_t>(DialogAnswerCount))
            return Core::Unexpected(DataError::InvalidData);
        for (std::size_t a = 0; a < DialogAnswerCount; ++a)
        {
            dialog.next[a] = IntAt(bytes, NextOffset + 4 * a);
            dialog.actions[a] = IntAt(bytes, ActionsOffset + 4 * a);
            std::ranges::transform(bytes.subspan(AnswersOffset + a * DialogAnswerSize, DialogAnswerSize),
                                   dialog.answers[a].begin(), toChar);
        }
    }
    return table;
}
} // namespace Client::Data::Text
