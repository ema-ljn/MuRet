#include "Tables/NpcNames.h"

#include "Data/DataError.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// NpcName(%s).txt with the tokenizer 0x54e8d0 and 0x54e7f0's loop. The names are made up.
namespace
{
using Client::Data::DataError;
using Client::Tables::LoadNpcNames;
using Client::Tables::NpcNameBytes;
using Client::Tables::NpcNameCount;
using Client::Tables::NpcNameNumberBytes;
using Client::Tables::NpcNameRecord;
using Client::Tables::NpcNameTokenBytes;

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

// The file format: "//" comments up to the '\n', the type, a number nobody looks at and the quoted name; the name
// arrives with its '\0' and the rest of the record at 0.
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

// 0x54e814: the 2 (the end or a 0xff) or the name "end", also quoted. An empty file has no records.
TEST(NpcNamesTest, StopsAtTheEndOrAtEnd)
{
    ExpectRecords("", {});
    ExpectRecords(" \r\n\t// nada\n", {});
    ExpectRecords("3 1 \"Uno\" end 4 1 \"Dos\"", {{3, "Uno"}});
    ExpectRecords("3 1 \"Uno\" \"end\" 4 1 \"Dos\"", {{3, "Uno"}});
    ExpectRecords("3 1 \"Uno\"\xff 4 1 \"Dos\"", {{3, "Uno"}});
    // "endx" and "End" are not "end": a record with the last number.
    ExpectRecords("3 1 \"Uno\" endx 9 \"Dos\"", {{3, "Uno"}, {1, "Dos"}});
    ExpectRecords("3 1 \"Uno\" End 9 \"Dos\"", {{3, "Uno"}, {1, "Dos"}});
}

// 0x54e861: the type comes from the last number read, whatever the record's first token (0 before the first), and the
// name is the string the third one left ("" if it was something else). The last two tokens do not check for the 2.
TEST(NpcNamesTest, TheTypeIsTheLastNumberWhateverTheToken)
{
    ExpectRecords("\"Uno\" 5 \"Dos\"", {{0, "Dos"}});
    ExpectRecords("# 1 \"Uno\" ; 2 \"Dos\" , 3 \"Tres\" { 4 \"Cuatro\" } 6 \"Cinco\" @ 7 \"Seis\"",
                  {{0, "Uno"}, {1, "Dos"}, {2, "Tres"}, {3, "Cuatro"}, {4, "Cinco"}, {6, "Seis"}});
    ExpectRecords("8 1 2 9 1 Nombre", {{8, ""}, {9, "Nombre"}});
    ExpectRecords("5 1", {{5, ""}});
    ExpectRecords("5", {{5, ""}});
}

// 0x54e9d9: '.', digits and '-' with atof to a float and ftol into a word; the byte that ends the number is lost.
TEST(NpcNamesTest, NumbersGoThroughAtofAndFtol)
{
    ExpectRecords("12.9 0 \"A\" -1 0 \"B\" 1-2 0 \"C\" --3 0 \"D\" . 0 \"E\" 70000 0 \"F\" -.5 0 \"G\"",
                  {{12, "A"}, {0xffff, "B"}, {1, "C"}, {0, "D"}, {0, "E"}, {70000 & 0xffff, "F"}, {0, "G"}});
    // "0x5": the 0 takes the x, and the 5 is the record's second token.
    ExpectRecords("0x5 \"H\"", {{0, "H"}});
    // The '"' that ends the 7 is lost: Ab is a name and the string goes from its quote to the 1's.
    ExpectRecords("7\"Ab\" 1 \"Cd\"", {{7, " 1 "}, {7, ""}});
}

// 0x54e8f4: a lone '/' is lost along with the byte that follows it in hand; "//" runs to the '\n'.
TEST(NpcNamesTest, CommentsRunToTheNewline)
{
    ExpectRecords("/3 1 \"A\"", {{3, "A"}});
    ExpectRecords("3 1 \"A\" //\n4 1 \"B\"// \"C\"\n", {{3, "A"}, {4, "B"}});
    ExpectRecords("3 1 \"A//B\"", {{3, "A//B"}});
}

// 0x54e925 checks only the '\n': a comment without it at the end of the file never ends.
TEST(NpcNamesTest, ACommentWithoutNewlineAtTheEndHangs)
{
    ExpectInvalid("3 1 \"A\" // sin fin");
    ExpectInvalid("3 1 \"A\" //");
    ExpectInvalid("//");
}

// Strings do not go through the CRT: a high byte enters the name. Outside them, isspace, isdigit and isalnum would get
// a negative index.
TEST(NpcNamesTest, HighBytesOnlyInsideStrings)
{
    ExpectRecords("3 1 \"\xb0\xa1\xc7\xd8\"", {{3, "\xb0\xa1\xc7\xd8"}});
    ExpectRecords("3 1 \"A\" // \xb0\xa1\n", {{3, "A"}});
    ExpectInvalid("\xb0 3 1 \"A\"");
    ExpectInvalid("3\xb0 1 \"A\"");
    ExpectInvalid("3 1 Ab\xb0");
    ExpectInvalid("3 1 \"A\" /\xb0");
}

// A string with a '\0' from the file: strcmp and strcpy stop at it.
TEST(NpcNamesTest, AZeroByteEndsTheNameForStrcpy)
{
    using namespace std::string_view_literals;
    ExpectRecords("3 1 \"A\0B\""sv, {{3, "A"}});
    ExpectRecords("3 1 \"A\" \"end\0x\" 4 1 \"B\""sv, {{3, "A"}});
}

// 0x54e8b4: the strcpy has no cap. A name that does not fit with its '\0' in its 0x34 bytes goes on into the next
// record's type and name, which is returned even if the file has no other; if it has one, its type and name overwrite
// those bytes and the rest stays.
TEST(NpcNamesTest, ALongNameSpillsIntoTheNextRecord)
{
    const std::string full(NpcNameBytes, 'n');
    ExpectRecords("3 1 \"" + full + "\"", {{3, full}, {0, ""}});

    // 0x34 + 2 + 3 letters: the '\0' in the fourth byte of record 1's name.
    const std::string longer(NpcNameBytes + 5, 'n');
    const std::vector<std::byte> alone = Bytes("3 1 \"" + longer + "\"");
    auto loaded = LoadNpcNames(alone);
    ASSERT_TRUE(loaded.HasValue());
    ASSERT_EQ(loaded.Value().size(), 2u);
    EXPECT_EQ(loaded.Value()[1].type, 0x6e6e);
    EXPECT_EQ(NameOf(loaded.Value()[1]), "nnn");

    const std::vector<std::byte> followed = Bytes("3 1 \"" + longer + "\" 4 1 \"B\"");
    loaded = LoadNpcNames(followed);
    ASSERT_TRUE(loaded.HasValue());
    ASSERT_EQ(loaded.Value().size(), 2u);
    EXPECT_EQ(NameOf(loaded.Value()[0]), full);
    EXPECT_EQ(loaded.Value()[1].type, 4);
    EXPECT_EQ(NameOf(loaded.Value()[1]), "B");
    EXPECT_EQ(loaded.Value()[1].name[2], 'n');
    EXPECT_EQ(loaded.Value()[1].name[3], '\0');
}

// What would write outside 0x7681eb8..0x7688ab8 or elsewhere: a name past the end of the table, 512 records, the
// string with its '\0' at 0x108 and the number at 0x64.
TEST(NpcNamesTest, WhatWouldOverflowIsInvalid)
{
    std::string full;
    for (std::size_t i = 0; i < NpcNameCount; ++i)
        full += "1 1 \"A\"\n";
    const std::vector<std::byte> bytes = Bytes(full);
    const auto loaded = LoadNpcNames(bytes);
    ASSERT_TRUE(loaded.HasValue());
    EXPECT_EQ(loaded.Value().size(), NpcNameCount);
    ExpectInvalid(full + "1 1 \"A\"\n");
    ExpectRecords(full + "end 1 1 \"A\"\n", std::vector<ExpectedRecord>(NpcNameCount, ExpectedRecord{1, "A"}));
    // In the last record, 0x33 letters and the '\0' reach exactly 0x7688ab8; one more goes past.
    std::string last;
    for (std::size_t i = 0; i + 1 < NpcNameCount; ++i)
        last += "1 1 \"A\"\n";
    std::vector<ExpectedRecord> lastRecords(NpcNameCount - 1, ExpectedRecord{1, "A"});
    const std::string longest(NpcNameBytes - 1, 'n');
    lastRecords.push_back({2, longest});
    ExpectRecords(last + "2 1 \"" + longest + "\"", lastRecords);
    ExpectInvalid(last + "2 1 \"" + longest + "n\"");

    const std::string token(NpcNameTokenBytes - 1, 't');
    ExpectRecords("\"" + token + "\" 1 \"A\"", {{0, "A"}});
    ExpectInvalid("\"" + token + "t\" 1 \"A\"");
    ExpectRecords(token + " 1 \"A\"", {{0, "A"}});
    ExpectInvalid(token + "t 1 \"A\"");

    const std::string number = std::string(NpcNameNumberBytes - 2, '0') + "7";
    ExpectRecords(number + " 1 \"A\"", {{7, "A"}});
    ExpectInvalid("0" + number + " 1 \"A\"");
    // 99 nines: the float is infinite and ftol has no portable result.
    ExpectInvalid(std::string(NpcNameNumberBytes - 1, '9') + " 1 \"A\"");
}
} // namespace
