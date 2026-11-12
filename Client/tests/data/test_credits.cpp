#include "Data/Text/Credits.h"

#include "Core/Xor3.h"
#include "Data/DataError.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

// Client::Data::Text::LoadCredits: credit.bmd (0x63ff30), 400 registros de 33 bytes con el XOR3 del login
// (0x4c1690) de una vez sobre los 13.200. Las tablas son inventadas.
namespace
{
using Client::Data::DataError;
using Client::Data::Text::CreditEntry;
using Client::Data::Text::CreditFileSize;
using Client::Data::Text::CreditKind;
using Client::Data::Text::CreditKindAt;
using Client::Data::Text::CreditRecordSize;
using Client::Data::Text::LoadCredits;

void PutRecord(std::vector<std::byte>& file, std::size_t record, CreditKind kind, std::string_view text)
{
    const std::size_t first = record * CreditRecordSize;
    file[first] = static_cast<std::byte>(static_cast<std::uint8_t>(kind));
    for (std::size_t i = 0; i < text.size(); ++i)
    {
        file[first + 1 + i] = static_cast<std::byte>(text[i]);
    }
}

// Un archivo de `size` bytes con tres registros y el final en el cuarto, cifrado.
std::vector<std::byte> CipheredFile(std::size_t size)
{
    std::vector<std::byte> file(size);
    PutRecord(file, 0, CreditKind::Title, "Una parte");
    PutRecord(file, 1, CreditKind::Role, "Un rol");
    PutRecord(file, 2, CreditKind::Name, "Alguien");
    PutRecord(file, 3, CreditKind::End, "");
    Client::Core::Xor3(file);
    return file;
}

TEST(CreditsTest, TheRecordsComeOutDeciphered)
{
    const std::vector<std::byte> file = CipheredFile(CreditFileSize);

    const auto credits = LoadCredits(file);

    ASSERT_TRUE(credits.HasValue());
    ASSERT_EQ(credits.Value().size(), 400U);
    EXPECT_EQ(credits.Value()[0], (CreditEntry{.kind = 1, .text = "Una parte"}));
    EXPECT_EQ(credits.Value()[1], (CreditEntry{.kind = 2, .text = "Un rol"}));
    EXPECT_EQ(credits.Value()[2], (CreditEntry{.kind = 3, .text = "Alguien"}));
    EXPECT_EQ(credits.Value()[3], (CreditEntry{.kind = 0, .text = ""}));
}

// El texto va hasta el primer cero de sus 32 bytes; uno lleno no tiene cero y llega al final del registro.
TEST(CreditsTest, AFullTextTakesTheThirtyTwoBytes)
{
    std::vector<std::byte> file(CreditFileSize);
    PutRecord(file, 0, CreditKind::Name, "abcdefghijklmnopqrstuvwxyz012345");
    file[CreditRecordSize] = std::byte{7};
    Client::Core::Xor3(file);

    const auto credits = LoadCredits(file);

    ASSERT_TRUE(credits.HasValue());
    EXPECT_EQ(credits.Value()[0].text, "abcdefghijklmnopqrstuvwxyz012345");
    // Una clase que no es ninguna de las cuatro queda tal cual.
    EXPECT_EQ(credits.Value()[1].kind, 7);
}

// fread lee lo que haya: con menos, MuRet no tiene lo que quedaba en la ventana. Lo que sobra se ignora.
TEST(CreditsTest, TheFileHasToBeWhole)
{
    const std::vector<std::byte> shorter = CipheredFile(CreditFileSize - 1);
    const auto refused = LoadCredits(shorter);
    ASSERT_FALSE(refused.HasValue());
    EXPECT_EQ(refused.Error(), DataError::FileTooShort);

    const std::vector<std::byte> longer = CipheredFile(CreditFileSize + 5);
    const auto loaded = LoadCredits(longer);
    ASSERT_TRUE(loaded.HasValue());
    EXPECT_EQ(loaded.Value().size(), 400U);
}

// 0x63ffe0 lee la clase de cualquier índice: después del último, el final.
TEST(CreditsTest, PastTheTableIsTheEnd)
{
    const std::vector<CreditEntry> credits{{.kind = 3, .text = "Alguien"}};

    EXPECT_EQ(CreditKindAt(credits, 0), std::uint8_t{3});
    EXPECT_EQ(CreditKindAt(credits, 1), std::uint8_t{0});
    EXPECT_EQ(CreditKindAt(std::span<const CreditEntry>{}, 0), std::uint8_t{0});
}
} // namespace
