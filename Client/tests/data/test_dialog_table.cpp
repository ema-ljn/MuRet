#include "Tables/DialogTable.h"

#include "Crypto/Xor3.h"
#include "Data/DataError.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

// Dialog.bmd (0x54b2b0), 200 records of 0x400 with 0x5496c0's XOR3 from each one's first byte. The texts are made up.
namespace
{
using Client::Data::DataError;
using Client::Tables::DialogAnswerOf;
using Client::Tables::DialogCount;
using Client::Tables::DialogRecordSize;
using Client::Tables::DialogTableSize;
using Client::Tables::DialogTextOf;
using Client::Tables::LoadDialogTable;

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
        Client::Crypto::Xor3(std::span(plain).subspan(i * DialogRecordSize, DialogRecordSize));
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

// 0x419710 and 0x632f70 would write the lines of the eleventh answer onward outside 0x7d3691c's 10, over the text at
// 0x7d36a98 and what follows: it fails on load (Q-C1127).
TEST(DialogTable, MoreThanTenAnswersWouldWriteOutsideTheRows)
{
    std::vector<std::byte> plain(DialogTableSize);
    PutInt(std::span(plain).subspan(3 * DialogRecordSize), 0x12c, 11);
    const auto loaded = LoadDialogTable(EncryptedFile(plain));
    ASSERT_FALSE(loaded.HasValue());
    EXPECT_EQ(loaded.Error(), DataError::InvalidData);
}

// 0x54b343..0x54b37e with 0x69fecb's fread: a record and a half. The second is its half that was read with the other
// half of the first, already decrypted, which the XOR3 encrypts again; from the third on nothing is read and the
// block is decrypted once more each time. With less than a record, the rest of the uninitialized block is an error
// (Q-C1127).
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
    Client::Crypto::Xor3(std::span(file).first(DialogRecordSize));
    Client::Crypto::Xor3(std::span(file).subspan(DialogRecordSize, DialogRecordSize));
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
    // The third, the second's block decrypted again: the file's half encrypted and the first's half in clear.
    EXPECT_NE(DialogTextOf(dialogs[2]), "Segundo inventado");
    EXPECT_EQ(DialogAnswerOf(dialogs[2], 4), "Cinco");
    EXPECT_EQ(dialogs[3], dialogs[1]);
    EXPECT_EQ(dialogs[DialogCount - 1], dialogs[3]);
    EXPECT_EQ(dialogs[DialogCount - 2], dialogs[2]);
}
} // namespace
