#include "Data/Text/TextTable.h"

#include "Core/Xor3.h"
#include "Data/DataError.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Client::Data::Text: la tabla de Text.bmd, con archivos sintéticos.
namespace
{
using Client::Data::DataError;
using Client::Data::Text::LoadTextTable;
using Client::Data::Text::TextCount;
using Client::Data::Text::TextRecordSize;
using Client::Data::Text::TextTable;
using Client::Data::Text::TextTableSize;

// Text() se puede pedir a una tabla que sigue viva, y no a una temporal.
template <typename Table>
concept ReadsText = requires(Table&& table) { std::forward<Table>(table).Text(0); };
static_assert(ReadsText<TextTable&>);
static_assert(ReadsText<const TextTable&>);
static_assert(!ReadsText<TextTable>);
static_assert(!ReadsText<const TextTable>);

// Una tabla en claro, con todos los registros vacíos.
std::vector<std::byte> PlainTable(std::size_t size = TextTableSize)
{
    return std::vector<std::byte>(size);
}

void PutText(std::vector<std::byte>& table, std::size_t index, std::string_view text)
{
    for (std::size_t i = 0; i < text.size(); ++i)
    {
        table[index * TextRecordSize + i] = static_cast<std::byte>(text[i]);
    }
}

// Lo que habría en el archivo: la tabla cifrada con XOR3.
std::vector<std::byte> Encrypt(std::vector<std::byte> table)
{
    Client::Core::Xor3(table);
    return table;
}

TEST(TextTableTest, ReadsTheFirstAndTheLastText)
{
    std::vector<std::byte> table = PlainTable();
    PutText(table, 0, "el primero");
    PutText(table, TextCount - 1, "el último");

    const auto loaded = LoadTextTable(Encrypt(table));

    ASSERT_TRUE(loaded.HasValue());
    EXPECT_EQ(loaded.Value().Text(0), "el primero");
    EXPECT_EQ(loaded.Value().Text(TextCount - 1), "el último");
    EXPECT_EQ(loaded.Value().Text(1), "");
}

// El texto termina en el primer '\0'; lo que queda después en el registro no es parte del texto.
TEST(TextTableTest, TextEndsAtTheFirstNul)
{
    std::vector<std::byte> table = PlainTable();
    PutText(table, 3, std::string_view("corto\0resto viejo", 17));

    const auto loaded = LoadTextTable(Encrypt(table));

    ASSERT_TRUE(loaded.HasValue());
    EXPECT_EQ(loaded.Value().Text(3), "corto");
}

// Q-C07: un registro sin '\0' es un texto de 300 bytes, y no sigue en el registro de al lado. El
// original lo leería como cadena de C y seguiría en el 8. El último registro llega hasta el final
// del archivo, así que también prueba que se descifró entero.
TEST(TextTableTest, RecordWithoutNulIsAFullRecord)
{
    std::vector<std::byte> table = PlainTable();
    const std::string fullRecord(TextRecordSize, 'A');
    PutText(table, 7, fullRecord);
    PutText(table, 8, "el de al lado");
    PutText(table, TextCount - 1, fullRecord);

    const auto loaded = LoadTextTable(Encrypt(table));

    ASSERT_TRUE(loaded.HasValue());
    EXPECT_EQ(loaded.Value().Text(7), fullRecord);
    EXPECT_EQ(loaded.Value().Text(TextCount - 1), fullRecord);
}

// Un índice que viene del servidor puede pasarse de 2047: FUN_004da070 le suma 2020 a un byte del
// paquete, así que llega hasta 2275, y el original leería después de la tabla. Tampoco hay texto
// para un índice que, multiplicado por 300, da la vuelta y caería adentro, ni para el más grande.
TEST(TextTableTest, IndexPastTheTableHasNoText)
{
    constexpr std::size_t Largest = std::numeric_limits<std::size_t>::max();
    const auto loaded = LoadTextTable(Encrypt(PlainTable()));

    ASSERT_TRUE(loaded.HasValue());
    EXPECT_EQ(loaded.Value().Text(TextCount), std::nullopt);
    EXPECT_EQ(loaded.Value().Text(2020 + 255), std::nullopt);
    EXPECT_EQ(loaded.Value().Text(Largest / TextRecordSize + 1), std::nullopt);
    EXPECT_EQ(loaded.Value().Text(Largest), std::nullopt);
}

// Sacarle la tabla a un Expected la mueve. El estándar no dice cómo queda un std::vector movido
// (válido, pero sin especificar; en la práctica, vacío), así que Text() mira los registros que le
// quedaron a la tabla y no lee fuera de ellos.
TEST(TextTableTest, AMovedTableReadsOnlyWhatItKept)
{
    std::vector<std::byte> table = PlainTable();
    PutText(table, 0, "el primero");
    auto loaded = LoadTextTable(Encrypt(table));
    ASSERT_TRUE(loaded.HasValue());

    const TextTable kept = std::move(loaded).Value();
    const std::optional<std::string_view> rest = loaded.Value().Text(0);

    EXPECT_EQ(kept.Text(0), "el primero");
    EXPECT_TRUE(!rest.has_value() || rest->size() <= TextRecordSize);
}

// Pregunta 2 del plan: un archivo corto es un error y no hay tabla. El original leería igual y
// dejaría en la tabla lo que hubiera en la memoria.
TEST(TextTableTest, ShortFileIsAnError)
{
    const auto loaded = LoadTextTable(Encrypt(PlainTable(TextTableSize - 1)));

    ASSERT_FALSE(loaded.HasValue());
    EXPECT_EQ(loaded.Error(), DataError::FileTooShort);
}

// Los bytes de más se ignoran, como con el fread de tamaño fijo del original: aunque alcancen para
// un registro más, no hay texto 2048.
TEST(TextTableTest, ExtraBytesAreIgnored)
{
    std::vector<std::byte> table = PlainTable(TextTableSize + TextRecordSize);
    PutText(table, TextCount - 1, "el último");
    PutText(table, TextCount, "de más");

    const auto loaded = LoadTextTable(Encrypt(table));

    ASSERT_TRUE(loaded.HasValue());
    EXPECT_EQ(loaded.Value().Text(TextCount - 1), "el último");
    EXPECT_EQ(loaded.Value().Text(TextCount), std::nullopt);
}
} // namespace
