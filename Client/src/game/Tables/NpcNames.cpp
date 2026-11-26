#include "Tables/NpcNames.h"

#include "Core/Expected.h"
#include "Data/DataError.h"
#include "Math/Ftol.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace Client::Tables
{
namespace
{
// What 0x54e8d0 returns in EAX (and in [0x76baac0], except 2).
constexpr int TextToken = 0;
constexpr int NumberToken = 1;
constexpr int EndToken = 2;
constexpr int OtherToken = 0x3c;
// The byte getc returns at EOF (-1), which the tokenizer compares as a char: the file's 0xff is the same.
constexpr unsigned char EndByte = 0xff;

// The CRT is asked with the signed char (MOVSX): 0xff is EOF (-1), which all four accept and return 0 for; a byte from
// 0x80 to 0xfe gives a negative index outside the CRT's table, which MuRet does not invent.
bool IsOutsideTheCrtTable(unsigned char c)
{
    return c >= 0x80 && c != EndByte;
}

// 0x6a0459 (isspace, mask 8) in the "C" locale: the five control characters and the space.
bool IsSpace(unsigned char c)
{
    return c == ' ' || (c >= '\t' && c <= '\r');
}

// 0x6a0404 (isdigit, mask 4).
bool IsDigit(unsigned char c)
{
    return c >= '0' && c <= '9';
}

// 0x6a0386 (isalpha, mask 0x103).
bool IsAlpha(unsigned char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

// 0x6a04a9 (isalnum, mask 0x107).
bool IsAlnum(unsigned char c)
{
    return IsAlpha(c) || IsDigit(c);
}

// 0x6a05ea (atof) over what the number gathered (only digits, '.' and '-'): a '-', the digits and a '.', up to the
// first thing that breaks the format; without digits, 0. It stores a float with FSTP (to nearest). Empty if it fits
// neither the float nor ftol's int64: with 0x63 bytes that can only be infinity or close to it.
std::optional<float> Atof(std::string_view text)
{
    double value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value, std::chars_format::fixed);
    if (error == std::errc::invalid_argument)
        return 0.0f;
    if (error != std::errc{})
        return std::nullopt;
    // Far from FLT_MAX, so the static_cast never leaves the float; from 2^63 up there is no ftol.
    constexpr double Limit = 0x1p64;
    if (std::abs(value) >= Limit)
        return std::nullopt;
    return static_cast<float>(value);
}

// 0x54e8d0 with [0x7758554]'s FILE over the file's bytes.
class Tokenizer
{
public:
    explicit Tokenizer(std::span<const std::byte> file) : m_file(file) {}

    // The token's type, or InvalidData where the original would hang or overwrite something.
    Core::Expected<int, Data::DataError> Next()
    {
        // 0x54e8de: the empty string, which stays so except for strings and names.
        m_text.clear();
        unsigned char c = Get();
        if (c == EndByte)
            return EndToken;
        for (;;)
        {
            // 0x54e8f4..0x54e938: "//" up to the '\n'. A lone '/' is lost and it goes on with the next byte.
            if (c == '/')
            {
                c = Get();
                if (c == '/')
                {
                    c = Get();
                    // 0x54e925: the loop only checks '\n'; at EOF getc returns -1 forever.
                    while (c != '\n')
                    {
                        if (m_position >= m_file.size())
                            return Core::Unexpected(Data::DataError::InvalidData);
                        c = Get();
                    }
                }
            }
            // 0x54e93a: isspace.
            if (IsOutsideTheCrtTable(c))
                return Core::Unexpected(Data::DataError::InvalidData);
            if (!IsSpace(c))
                break;
            c = Get();
            if (c == EndByte)
                return EndToken;
        }
        // 0x54e96b: 0x54eb54's table for '"'..'}'; the rest, isalpha.
        switch (c)
        {
        case '"':
            return ReadString();
        case '#':
        case ',':
        case ';':
        case '{':
        case '}':
            return static_cast<int>(c);
        case '-':
        case '.':
        case '0':
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7':
        case '8':
        case '9':
            return ReadNumber(c);
        default:
            if (!IsAlpha(c))
                return OtherToken;
            return ReadName(c);
        }
    }

    // 0x7681db0 up to its first '\0' (a string can bring one from the file), as strcmp and strcpy read it.
    [[nodiscard]] std::string_view Text() const
    {
        const std::string_view text(m_text);
        return text.substr(0, text.find('\0'));
    }

    // [0x76baac4]: the last number, even if the token is another (0 in the .bss before the first).
    [[nodiscard]] float Number() const
    {
        return m_number;
    }

private:
    // 0x6a0641 and 0x6a0672 (getc), as a char: EOF's 0xff or the byte.
    unsigned char Get()
    {
        if (m_position >= m_file.size())
            return EndByte;
        return std::to_integer<unsigned char>(m_file[m_position++]);
    }

    // 0x6a067d (ungetc) of the byte just read. With 0xff, MOVSX gives EOF and ungetc does nothing: the byte stays read.
    void Unget(unsigned char c)
    {
        if (c != EndByte)
            --m_position;
    }

    // 0x54ea56..0x54eaa2: up to the other quote (which is lost) or 0xff, without escapes.
    Core::Expected<int, Data::DataError> ReadString()
    {
        for (unsigned char c = Get(); c != '"' && c != EndByte; c = Get())
            m_text.push_back(static_cast<char>(c));
        if (m_text.size() >= NpcNameTokenBytes)
            return Core::Unexpected(Data::DataError::InvalidData);
        return TextToken;
    }

    // 0x54e9d9..0x54ea4b: puts the first back with ungetc and gathers '.', digits and '-'; the terminator does not come
    // back.
    Core::Expected<int, Data::DataError> ReadNumber(unsigned char first)
    {
        Unget(first);
        std::string digits;
        for (unsigned char c = Get(); c != EndByte; c = Get())
        {
            if (c != '.')
            {
                // 0x54ea07: isdigit.
                if (IsOutsideTheCrtTable(c))
                    return Core::Unexpected(Data::DataError::InvalidData);
                if (!IsDigit(c) && c != '-')
                    break;
            }
            digits.push_back(static_cast<char>(c));
        }
        // [ebp − 0x64]: 0x64 bytes with its '\0'; more would overwrite the saved EBP.
        if (digits.size() >= NpcNameNumberBytes)
            return Core::Unexpected(Data::DataError::InvalidData);
        const std::optional<float> number = Atof(digits);
        if (!number)
            return Core::Unexpected(Data::DataError::InvalidData);
        m_number = *number;
        return NumberToken;
    }

    // 0x54eac1..0x54eb1e: the letter and the letters, digits, '.' and '_' that follow; the terminator comes back with
    // ungetc.
    Core::Expected<int, Data::DataError> ReadName(unsigned char first)
    {
        m_text.push_back(static_cast<char>(first));
        unsigned char c = Get();
        for (; c != EndByte; c = Get())
        {
            if (c != '.' && c != '_')
            {
                // 0x54eaef: isalnum.
                if (IsOutsideTheCrtTable(c))
                    return Core::Unexpected(Data::DataError::InvalidData);
                if (!IsAlnum(c))
                    break;
            }
            m_text.push_back(static_cast<char>(c));
        }
        Unget(c);
        if (m_text.size() >= NpcNameTokenBytes)
            return Core::Unexpected(Data::DataError::InvalidData);
        return TextToken;
    }

    std::span<const std::byte> m_file;
    std::size_t m_position = 0;
    std::string m_text;
    float m_number = 0.0f;
};
} // namespace

Core::Expected<std::vector<NpcNameRecord>, Data::DataError> LoadNpcNames(std::span<const std::byte> file)
{
    // The bytes of 0x7681eb8..0x7688ab8 as 0x54e7f0 writes them (in the .bss, at 0): a long name goes on into the next
    // records, and the next record overwrites its part.
    constexpr std::size_t RecordBytes = 2 + NpcNameBytes;
    constexpr std::size_t TableBytes = NpcNameCount * RecordBytes;
    std::vector<char> table(TableBytes, '\0');
    std::size_t count = 0;
    std::size_t written = 0;
    Tokenizer tokens(file);
    for (;;)
    {
        // 0x54e814..0x54e85f: up to 2 or the name "end" (0x6c7a60) in a token 0.
        const auto token = tokens.Next();
        if (!token.HasValue())
            return Core::Unexpected(token.Error());
        if (token.Value() == EndToken || (token.Value() == TextToken && tokens.Text() == "end"))
            break;
        // 0x54e861..0x54e884: the record [0x7758560]++ and the word of the last number's ftol.
        if (count >= NpcNameCount)
            return Core::Unexpected(Data::DataError::InvalidData);
        const std::optional<std::int32_t> type = Math::OriginalFtolLow(static_cast<double>(tokens.Number()));
        if (!type)
            return Core::Unexpected(Data::DataError::InvalidData);
        const std::size_t at = count * RecordBytes;
        const auto word = static_cast<std::uint32_t>(*type);
        table[at] = static_cast<char>(word & 0xffu);
        table[at + 1] = static_cast<char>((word >> 8) & 0xffu);
        ++count;
        // 0x54e887..0x54e8b4: two more tokens and the strcpy of the second's string, which goes on into the next
        // records if it does not fit its 0x34 bytes. Only one that would pass 0x7688ab8 writes outside the table.
        for (int i = 0; i < 2; ++i)
        {
            if (const auto next = tokens.Next(); !next.HasValue())
                return Core::Unexpected(next.Error());
        }
        const std::string_view name = tokens.Text();
        const std::size_t end = at + 2 + name.size() + 1;
        if (end > TableBytes)
            return Core::Unexpected(Data::DataError::InvalidData);
        name.copy(table.data() + at + 2, name.size());
        table[end - 1] = '\0';
        written = std::max(written, end);
    }
    // The records read and those a long name touched after the last.
    const std::size_t records = std::max(count, (written + RecordBytes - 1) / RecordBytes);
    std::vector<NpcNameRecord> loaded(records);
    for (std::size_t r = 0; r < records; ++r)
    {
        const std::size_t at = r * RecordBytes;
        loaded[r].type =
            static_cast<std::uint16_t>(static_cast<unsigned char>(table[at]) |
                                       (static_cast<unsigned>(static_cast<unsigned char>(table[at + 1])) << 8));
        std::copy_n(table.begin() + static_cast<std::ptrdiff_t>(at + 2), NpcNameBytes, loaded[r].name.begin());
    }
    return loaded;
}
} // namespace Client::Tables
