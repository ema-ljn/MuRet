#include "Data/Text/NpcNames.h"

#include "Core/Expected.h"
#include "Data/DataError.h"
#include "World/OriginalFtol.h"

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

namespace Client::Data::Text
{
namespace
{
// Lo que devuelve 0x54e8d0 en EAX (y en [0x76baac0], salvo el 2).
constexpr int TextToken = 0;
constexpr int NumberToken = 1;
constexpr int EndToken = 2;
constexpr int OtherToken = 0x3c;
// El byte que getc devuelve en EOF (-1) y que el tokenizador compara en un char: el 0xff del archivo es igual.
constexpr unsigned char EndByte = 0xff;

// Las preguntas a la CRT con el char con signo (MOVSX): el 0xff es EOF (-1), que las cuatro aceptan y para el que
// devuelven 0; un byte de 0x80 a 0xfe da un índice negativo fuera de la tabla de la CRT, que MuRet no inventa.
bool IsOutsideTheCrtTable(unsigned char c)
{
    return c >= 0x80 && c != EndByte;
}

// 0x6a0459 (isspace, máscara 8) en la "C": los cinco de control y el espacio.
bool IsSpace(unsigned char c)
{
    return c == ' ' || (c >= '\t' && c <= '\r');
}

// 0x6a0404 (isdigit, máscara 4).
bool IsDigit(unsigned char c)
{
    return c >= '0' && c <= '9';
}

// 0x6a0386 (isalpha, máscara 0x103).
bool IsAlpha(unsigned char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

// 0x6a04a9 (isalnum, máscara 0x107).
bool IsAlnum(unsigned char c)
{
    return IsAlpha(c) || IsDigit(c);
}

// 0x6a05ea (atof) sobre lo que juntó el número (solo dígitos, '.' y '-'): un '-', los dígitos y un '.', hasta lo
// primero que no siga el formato; sin dígitos, 0. Lo deja en un float con FSTP (al más cercano). Vacío si no entra
// ni en el float ni en el int64 del ftol: con 0x63 bytes no hay otra cosa que el infinito o algo cerca.
std::optional<float> Atof(std::string_view text)
{
    double value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value, std::chars_format::fixed);
    if (error == std::errc::invalid_argument)
        return 0.0f;
    if (error != std::errc{})
        return std::nullopt;
    // Lejos de FLT_MAX, así el static_cast nunca se sale del float; lo de 2^63 para arriba no tiene ftol.
    constexpr double Limit = 0x1p64;
    if (std::abs(value) >= Limit)
        return std::nullopt;
    return static_cast<float>(value);
}

// 0x54e8d0 con el FILE de [0x7758554] sobre los bytes del archivo.
class Tokenizer
{
public:
    explicit Tokenizer(std::span<const std::byte> file) : m_file(file) {}

    // El tipo del token, o InvalidData donde el original se colgaría o pisaría otra cosa.
    Core::Expected<int, DataError> Next()
    {
        // 0x54e8de: la cadena vacía, que queda así salvo en las cadenas y los nombres.
        m_text.clear();
        unsigned char c = Get();
        if (c == EndByte)
            return EndToken;
        for (;;)
        {
            // 0x54e8f4..0x54e938: "//" hasta el '\n'. Con un '/' solo se pierde y sigue con el byte de después.
            if (c == '/')
            {
                c = Get();
                if (c == '/')
                {
                    c = Get();
                    // 0x54e925: el lazo solo mira el '\n'; en EOF, getc devuelve -1 para siempre.
                    while (c != '\n')
                    {
                        if (m_position >= m_file.size())
                            return Core::Unexpected(DataError::InvalidData);
                        c = Get();
                    }
                }
            }
            // 0x54e93a: isspace.
            if (IsOutsideTheCrtTable(c))
                return Core::Unexpected(DataError::InvalidData);
            if (!IsSpace(c))
                break;
            c = Get();
            if (c == EndByte)
                return EndToken;
        }
        // 0x54e96b: la tabla de 0x54eb54 para '"'..'}'; lo demás, isalpha.
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

    // 0x7681db0 hasta su primer '\0' (una cadena puede traer uno del archivo), como la leen strcmp y strcpy.
    [[nodiscard]] std::string_view Text() const
    {
        const std::string_view text(m_text);
        return text.substr(0, text.find('\0'));
    }

    // [0x76baac4]: el último número, aunque el token sea otro (0 en la .bss antes del primero).
    [[nodiscard]] float Number() const
    {
        return m_number;
    }

private:
    // 0x6a0641 y 0x6a0672 (getc), en el char: el 0xff de EOF o el byte.
    unsigned char Get()
    {
        if (m_position >= m_file.size())
            return EndByte;
        return std::to_integer<unsigned char>(m_file[m_position++]);
    }

    // 0x6a067d (ungetc) del byte recién leído. Con el 0xff, MOVSX da EOF y ungetc no hace nada: el byte queda leído.
    void Unget(unsigned char c)
    {
        if (c != EndByte)
            --m_position;
    }

    // 0x54ea56..0x54eaa2: hasta la otra comilla (que se pierde) o el 0xff, sin escapes.
    Core::Expected<int, DataError> ReadString()
    {
        for (unsigned char c = Get(); c != '"' && c != EndByte; c = Get())
            m_text.push_back(static_cast<char>(c));
        if (m_text.size() >= NpcNameTokenBytes)
            return Core::Unexpected(DataError::InvalidData);
        return TextToken;
    }

    // 0x54e9d9..0x54ea4b: devuelve el primero con ungetc y junta '.', dígitos y '-'; el que termina no vuelve.
    Core::Expected<int, DataError> ReadNumber(unsigned char first)
    {
        Unget(first);
        std::string digits;
        for (unsigned char c = Get(); c != EndByte; c = Get())
        {
            if (c != '.')
            {
                // 0x54ea07: isdigit.
                if (IsOutsideTheCrtTable(c))
                    return Core::Unexpected(DataError::InvalidData);
                if (!IsDigit(c) && c != '-')
                    break;
            }
            digits.push_back(static_cast<char>(c));
        }
        // [ebp − 0x64]: con su '\0', 0x64 bytes; más pisaría el EBP guardado.
        if (digits.size() >= NpcNameNumberBytes)
            return Core::Unexpected(DataError::InvalidData);
        const std::optional<float> number = Atof(digits);
        if (!number)
            return Core::Unexpected(DataError::InvalidData);
        m_number = *number;
        return NumberToken;
    }

    // 0x54eac1..0x54eb1e: la letra y las letras, dígitos, '.' y '_' que siguen; el que termina vuelve con ungetc.
    Core::Expected<int, DataError> ReadName(unsigned char first)
    {
        m_text.push_back(static_cast<char>(first));
        unsigned char c = Get();
        for (; c != EndByte; c = Get())
        {
            if (c != '.' && c != '_')
            {
                // 0x54eaef: isalnum.
                if (IsOutsideTheCrtTable(c))
                    return Core::Unexpected(DataError::InvalidData);
                if (!IsAlnum(c))
                    break;
            }
            m_text.push_back(static_cast<char>(c));
        }
        Unget(c);
        if (m_text.size() >= NpcNameTokenBytes)
            return Core::Unexpected(DataError::InvalidData);
        return TextToken;
    }

    std::span<const std::byte> m_file;
    std::size_t m_position = 0;
    std::string m_text;
    float m_number = 0.0f;
};
} // namespace

Core::Expected<std::vector<NpcNameRecord>, DataError> LoadNpcNames(std::span<const std::byte> file)
{
    // Los bytes de 0x7681eb8..0x7688ab8 como los escribe 0x54e7f0 (en la .bss, en 0): un nombre largo sigue en los
    // registros que siguen, y el registro siguiente pisa lo que le toca.
    constexpr std::size_t RecordBytes = 2 + NpcNameBytes;
    constexpr std::size_t TableBytes = NpcNameCount * RecordBytes;
    std::vector<char> table(TableBytes, '\0');
    std::size_t count = 0;
    std::size_t written = 0;
    Tokenizer tokens(file);
    for (;;)
    {
        // 0x54e814..0x54e85f: hasta el 2 o el nombre "end" (0x6c7a60) en un token 0.
        const auto token = tokens.Next();
        if (!token.HasValue())
            return Core::Unexpected(token.Error());
        if (token.Value() == EndToken || (token.Value() == TextToken && tokens.Text() == "end"))
            break;
        // 0x54e861..0x54e884: el registro [0x7758560]++ y el word del ftol del último número.
        if (count >= NpcNameCount)
            return Core::Unexpected(DataError::InvalidData);
        const std::optional<std::int32_t> type = World::OriginalFtolLow(static_cast<double>(tokens.Number()));
        if (!type)
            return Core::Unexpected(DataError::InvalidData);
        const std::size_t at = count * RecordBytes;
        const auto word = static_cast<std::uint32_t>(*type);
        table[at] = static_cast<char>(word & 0xffu);
        table[at + 1] = static_cast<char>((word >> 8) & 0xffu);
        ++count;
        // 0x54e887..0x54e8b4: dos tokens más y el strcpy de la cadena del segundo, que sigue en los registros que siguen
        // si no entra en sus 0x34 bytes. Solo el que pasaría de 0x7688ab8 escribe fuera de la tabla.
        for (int i = 0; i < 2; ++i)
        {
            if (const auto next = tokens.Next(); !next.HasValue())
                return Core::Unexpected(next.Error());
        }
        const std::string_view name = tokens.Text();
        const std::size_t end = at + 2 + name.size() + 1;
        if (end > TableBytes)
            return Core::Unexpected(DataError::InvalidData);
        name.copy(table.data() + at + 2, name.size());
        table[end - 1] = '\0';
        written = std::max(written, end);
    }
    // Los registros leídos y los que tocó un nombre largo después del último.
    const std::size_t records = std::max(count, (written + RecordBytes - 1) / RecordBytes);
    std::vector<NpcNameRecord> loaded(records);
    for (std::size_t r = 0; r < records; ++r)
    {
        const std::size_t at = r * RecordBytes;
        loaded[r].type = static_cast<std::uint16_t>(static_cast<unsigned char>(table[at]) |
                                                    (static_cast<unsigned>(static_cast<unsigned char>(table[at + 1]))
                                                     << 8));
        std::copy_n(table.begin() + static_cast<std::ptrdiff_t>(at + 2), NpcNameBytes, loaded[r].name.begin());
    }
    return loaded;
}
} // namespace Client::Data::Text
