#include "Data/Text/DialogTable.h"

#include "Core/Xor3.h"
#include "Data/DataError.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

// Client::Data::Text::LoadDialogTable: Dialog.bmd (0x54b2b0), 200 registros de 0x400 con el XOR3 de 0x5496c0 desde el
// primer byte de cada uno. Los textos son inventados.
namespace
{
using Client::Data::DataError;
using Client::Data::Text::DialogAnswerOf;
using Client::Data::Text::DialogCount;
using Client::Data::Text::DialogRecordSize;
using Client::Data::Text::DialogTableSize;
using Client::Data::Text::DialogTextOf;
using Client::Data::Text::LoadDialogTable;

void PutInt(std::span<std::byte> bytes, std::size_t offset, std::int32_t value)
{
    for (std::size_t i = 0; i < 4; ++i)
        bytes[offset + i] = static_cast<std::byte>((static_cast<std::uint32_t>(value) >> (8 * i)) & 0xffu);
}

void PutText(std::span<std::byte> bytes, std::size_t offset, std::string_view text)
{
    for (std::size_t i = 0; i < text.size(); ++i)
        bytes[offset + i] = static_cast<std::byte>(text[i]);
}

std::vector<std::byte> EncryptedFile(std::vector<std::byte> plain)
{
    for (std::size_t i = 0; i < DialogCount; ++i)
        Client::Core::Xor3(std::span(plain).subspan(i * DialogRecordSize, DialogRecordSize));
    return plain;
}

TEST(DialogTable, ReadsTheTextTheAnswersTheirNextAndTheirAction)
{
    std::vector<std::byte> plain(DialogTableSize);
    const std::span<std::byte> record = std::span(plain).subspan(199 * DialogRecordSize, DialogRecordSize);
    PutText(record, 0, "Un texto inventado");
    PutInt(record, 0x12c, 2);
    PutInt(record, 0x130, 41);
    PutInt(record, 0x134, -1);
    PutInt(record, 0x158, 1);
    PutInt(record, 0x15c, 2);
    PutText(record, 0x180, "Sí");
    PutText(record, 0x1c0, "No");

    const auto loaded = LoadDialogTable(EncryptedFile(plain));
    ASSERT_TRUE(loaded.HasValue());
    ASSERT_EQ(loaded.Value().size(), DialogCount);
    const auto& dialog = loaded.Value()[199];
    EXPECT_EQ(DialogTextOf(dialog), "Un texto inventado");
    EXPECT_EQ(dialog.answerCount, 2);
    EXPECT_EQ(dialog.next[0], 41);
    EXPECT_EQ(dialog.next[1], -1);
    EXPECT_EQ(dialog.actions[0], 1);
    EXPECT_EQ(dialog.actions[1], 2);
    EXPECT_EQ(DialogAnswerOf(dialog, 0), "Sí");
    EXPECT_EQ(DialogAnswerOf(dialog, 1), "No");
    EXPECT_EQ(DialogAnswerOf(dialog, 10), "");
}

// 0x419710 y 0x632f70 escribirían los renglones de la undécima respuesta en adelante fuera de los 10 de 0x7d3691c,
// sobre el texto de 0x7d36a98 y lo que sigue: corta al cargar (Q-C1127).
TEST(DialogTable, MoreThanTenAnswersWouldWriteOutsideTheRows)
{
    std::vector<std::byte> plain(DialogTableSize);
    PutInt(std::span(plain).subspan(3 * DialogRecordSize), 0x12c, 11);
    const auto loaded = LoadDialogTable(EncryptedFile(plain));
    ASSERT_FALSE(loaded.HasValue());
    EXPECT_EQ(loaded.Error(), DataError::InvalidData);
}

// 0x54b343..0x54b37e con el fread de 0x69fecb: un registro y medio. El segundo es su mitad leída con la otra mitad del
// primero, ya descifrada, que el XOR3 vuelve a cifrar; desde el tercero no se lee nada y el bloque se descifra una vez
// más cada vez. Con menos de un registro, el resto del bloque sin iniciar es un error (Q-C1127).
TEST(DialogTable, AShortFileKeepsWhatItsFreadLeaves)
{
    const auto tooShort = LoadDialogTable(std::vector<std::byte>(DialogRecordSize - 1));
    ASSERT_FALSE(tooShort.HasValue());
    EXPECT_EQ(tooShort.Error(), DataError::FileTooShort);

    constexpr std::size_t Half = DialogRecordSize / 2;
    std::vector<std::byte> plain(2 * DialogRecordSize);
    const std::span<std::byte> first = std::span(plain).first(DialogRecordSize);
    const std::span<std::byte> second = std::span(plain).subspan(DialogRecordSize, DialogRecordSize);
    PutText(first, 0, "Primero inventado");
    PutInt(first, 0x12c, 2);
    PutText(first, 0x180, "Uno");
    PutText(first, 0x280, "Cinco");
    PutText(second, 0, "Segundo inventado");
    PutInt(second, 0x12c, 1);
    PutText(second, 0x180, "Otro");
    std::vector<std::byte> file = plain;
    Client::Core::Xor3(std::span(file).first(DialogRecordSize));
    Client::Core::Xor3(std::span(file).subspan(DialogRecordSize, DialogRecordSize));
    file.resize(DialogRecordSize + Half);

    const auto loaded = LoadDialogTable(file);
    ASSERT_TRUE(loaded.HasValue());
    const auto& dialogs = loaded.Value();
    ASSERT_EQ(dialogs.size(), DialogCount);
    EXPECT_EQ(DialogTextOf(dialogs[0]), "Primero inventado");
    EXPECT_EQ(DialogAnswerOf(dialogs[0], 4), "Cinco");
    EXPECT_EQ(DialogTextOf(dialogs[1]), "Segundo inventado");
    EXPECT_EQ(dialogs[1].answerCount, 1);
    EXPECT_EQ(DialogAnswerOf(dialogs[1], 0), "Otro");
    EXPECT_NE(DialogAnswerOf(dialogs[1], 4), "Cinco");
    // El tercero, el bloque del segundo descifrado otra vez: la mitad del archivo cifrada y la del primero en claro.
    EXPECT_NE(DialogTextOf(dialogs[2]), "Segundo inventado");
    EXPECT_EQ(DialogAnswerOf(dialogs[2], 4), "Cinco");
    EXPECT_EQ(dialogs[3], dialogs[1]);
    EXPECT_EQ(dialogs[DialogCount - 1], dialogs[3]);
    EXPECT_EQ(dialogs[DialogCount - 2], dialogs[2]);
}
} // namespace
