#include "Data/Text/NpcNames.h"

#include "Data/DataError.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Client::Data::Text::LoadNpcNames: NpcName(%s).txt con el tokenizador 0x54e8d0 y el lazo de 0x54e7f0. Los nombres
// son inventados.
namespace
{
using Client::Data::DataError;
using Client::Data::Text::LoadNpcNames;
using Client::Data::Text::NpcNameBytes;
using Client::Data::Text::NpcNameCount;
using Client::Data::Text::NpcNameNumberBytes;
using Client::Data::Text::NpcNameRecord;
using Client::Data::Text::NpcNameTokenBytes;

std::vector<std::byte> Bytes(std::string_view text)
{
    std::vector<std::byte> bytes;
    for (const char c : text)
        bytes.push_back(static_cast<std::byte>(c));
    return bytes;
}

std::string_view NameOf(const NpcNameRecord& record)
{
    const std::string_view name(record.name.data(), record.name.size());
    return name.substr(0, name.find('\0'));
}

struct ExpectedRecord
{
    std::uint16_t type = 0;
    std::string_view name;
};

void ExpectRecords(std::string_view file, const std::vector<ExpectedRecord>& expected)
{
    const std::vector<std::byte> bytes = Bytes(file);
    const auto loaded = LoadNpcNames(bytes);
    ASSERT_TRUE(loaded.HasValue()) << file;
    ASSERT_EQ(loaded.Value().size(), expected.size()) << file;
    for (std::size_t i = 0; i < expected.size(); ++i)
    {
        EXPECT_EQ(loaded.Value()[i].type, expected[i].type) << file << " #" << i;
        EXPECT_EQ(NameOf(loaded.Value()[i]), expected[i].name) << file << " #" << i;
    }
}

void ExpectInvalid(std::string_view file)
{
    const std::vector<std::byte> bytes = Bytes(file);
    const auto loaded = LoadNpcNames(bytes);
    ASSERT_FALSE(loaded.HasValue()) << file;
    EXPECT_EQ(loaded.Error(), DataError::InvalidData) << file;
}

// El formato del archivo: comentarios "//" hasta el '\n', el tipo, un número que nadie mira y el nombre entre
// comillas; el nombre llega con su '\0' y el resto del registro en 0.
TEST(NpcNamesTest, ReadsTheTypeAndTheQuotedName)
{
    ExpectRecords("//Primera parte\r\n 84 1 \"Rey inventado\"\r\n\t85 1 \"Arquero inventado\"\r\n//fin\r\n",
                  {{84, "Rey inventado"}, {85, "Arquero inventado"}});

    const std::vector<std::byte> bytes = Bytes(" 7 1 \"Ab\"");
    const auto loaded = LoadNpcNames(bytes);
    ASSERT_TRUE(loaded.HasValue());
    NpcNameRecord expected;
    expected.type = 7;
    expected.name[0] = 'A';
    expected.name[1] = 'b';
    ASSERT_EQ(loaded.Value().size(), 1u);
    EXPECT_EQ(loaded.Value()[0], expected);
}

// 0x54e814: el 2 (el final o un 0xff) o el nombre "end", también entre comillas. Un archivo vacío no tiene registros.
TEST(NpcNamesTest, StopsAtTheEndOrAtEnd)
{
    ExpectRecords("", {});
    ExpectRecords(" \r\n\t// nada\n", {});
    ExpectRecords("3 1 \"Uno\" end 4 1 \"Dos\"", {{3, "Uno"}});
    ExpectRecords("3 1 \"Uno\" \"end\" 4 1 \"Dos\"", {{3, "Uno"}});
    ExpectRecords("3 1 \"Uno\"\xff 4 1 \"Dos\"", {{3, "Uno"}});
    // "endx" y "End" no son "end": un registro con el último número.
    ExpectRecords("3 1 \"Uno\" endx 9 \"Dos\"", {{3, "Uno"}, {1, "Dos"}});
    ExpectRecords("3 1 \"Uno\" End 9 \"Dos\"", {{3, "Uno"}, {1, "Dos"}});
}

// 0x54e861: el tipo sale del último número leído, sea cual sea el primer token del registro (0 antes del primero), y
// el nombre es la cadena que dejó el tercero ("" si fue otra cosa). Los dos tokens del final no miran el 2.
TEST(NpcNamesTest, TheTypeIsTheLastNumberWhateverTheToken)
{
    ExpectRecords("\"Uno\" 5 \"Dos\"", {{0, "Dos"}});
    ExpectRecords("# 1 \"Uno\" ; 2 \"Dos\" , 3 \"Tres\" { 4 \"Cuatro\" } 6 \"Cinco\" @ 7 \"Seis\"",
                  {{0, "Uno"}, {1, "Dos"}, {2, "Tres"}, {3, "Cuatro"}, {4, "Cinco"}, {6, "Seis"}});
    ExpectRecords("8 1 2 9 1 Nombre", {{8, ""}, {9, "Nombre"}});
    ExpectRecords("5 1", {{5, ""}});
    ExpectRecords("5", {{5, ""}});
}

// 0x54e9d9: '.', dígitos y '-' con atof a un float y el ftol en un word; el byte que termina el número se pierde.
TEST(NpcNamesTest, NumbersGoThroughAtofAndFtol)
{
    ExpectRecords("12.9 0 \"A\" -1 0 \"B\" 1-2 0 \"C\" --3 0 \"D\" . 0 \"E\" 70000 0 \"F\" -.5 0 \"G\"",
                  {{12, "A"}, {0xffff, "B"}, {1, "C"}, {0, "D"}, {0, "E"}, {70000 & 0xffff, "F"}, {0, "G"}});
    // "0x5": el 0 se lleva la x, y el 5 es el segundo token del registro.
    ExpectRecords("0x5 \"H\"", {{0, "H"}});
    // El '"' que termina el 7 se pierde: Ab es un nombre y la cadena va de su comilla a la del 1.
    ExpectRecords("7\"Ab\" 1 \"Cd\"", {{7, " 1 "}, {7, ""}});
}

// 0x54e8f4: un '/' solo se pierde con el byte que sigue en la mano; "//" va hasta el '\n'.
TEST(NpcNamesTest, CommentsRunToTheNewline)
{
    ExpectRecords("/3 1 \"A\"", {{3, "A"}});
    ExpectRecords("3 1 \"A\" //\n4 1 \"B\"// \"C\"\n", {{3, "A"}, {4, "B"}});
    ExpectRecords("3 1 \"A//B\"", {{3, "A//B"}});
}

// 0x54e925 mira solo el '\n': un comentario sin él al final del archivo no termina nunca.
TEST(NpcNamesTest, ACommentWithoutNewlineAtTheEndHangs)
{
    ExpectInvalid("3 1 \"A\" // sin fin");
    ExpectInvalid("3 1 \"A\" //");
    ExpectInvalid("//");
}

// Las cadenas no pasan por la CRT: un byte alto entra en el nombre. Fuera de ellas, isspace, isdigit e isalnum
// recibirían un índice negativo.
TEST(NpcNamesTest, HighBytesOnlyInsideStrings)
{
    ExpectRecords("3 1 \"\xb0\xa1\xc7\xd8\"", {{3, "\xb0\xa1\xc7\xd8"}});
    ExpectRecords("3 1 \"A\" // \xb0\xa1\n", {{3, "A"}});
    ExpectInvalid("\xb0 3 1 \"A\"");
    ExpectInvalid("3\xb0 1 \"A\"");
    ExpectInvalid("3 1 Ab\xb0");
    ExpectInvalid("3 1 \"A\" /\xb0");
}

// Una cadena con un '\0' del archivo: strcmp y strcpy se paran en él.
TEST(NpcNamesTest, AZeroByteEndsTheNameForStrcpy)
{
    using namespace std::string_view_literals;
    ExpectRecords("3 1 \"A\0B\""sv, {{3, "A"}});
    ExpectRecords("3 1 \"A\" \"end\0x\" 4 1 \"B\""sv, {{3, "A"}});
}

// Lo que pisaría otra cosa: el nombre con su '\0' en 0x34 bytes, 512 registros, la cadena con el suyo en 0x108 y el
// número en 0x64.
TEST(NpcNamesTest, WhatWouldOverflowIsInvalid)
{
    const std::string longest(NpcNameBytes - 1, 'n');
    ExpectRecords("3 1 \"" + longest + "\"", {{3, longest}});
    ExpectInvalid("3 1 \"" + longest + "n\"");

    std::string full;
    for (std::size_t i = 0; i < NpcNameCount; ++i)
        full += "1 1 \"A\"\n";
    const std::vector<std::byte> bytes = Bytes(full);
    const auto loaded = LoadNpcNames(bytes);
    ASSERT_TRUE(loaded.HasValue());
    EXPECT_EQ(loaded.Value().size(), NpcNameCount);
    ExpectInvalid(full + "1 1 \"A\"\n");
    ExpectRecords(full + "end 1 1 \"A\"\n", std::vector<ExpectedRecord>(NpcNameCount, ExpectedRecord{1, "A"}));

    const std::string token(NpcNameTokenBytes - 1, 't');
    ExpectRecords("\"" + token + "\" 1 \"A\"", {{0, "A"}});
    ExpectInvalid("\"" + token + "t\" 1 \"A\"");
    ExpectRecords(token + " 1 \"A\"", {{0, "A"}});
    ExpectInvalid(token + "t 1 \"A\"");

    const std::string number = std::string(NpcNameNumberBytes - 2, '0') + "7";
    ExpectRecords(number + " 1 \"A\"", {{7, "A"}});
    ExpectInvalid("0" + number + " 1 \"A\"");
    // 99 nueves: el float es infinito y el ftol no tiene un resultado portable.
    ExpectInvalid(std::string(NpcNameNumberBytes - 1, '9') + " 1 \"A\"");
}
} // namespace
