#include "UI/SprintfFormat.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace Client::UI
{
namespace
{
// Las clases de carácter y los estados de output.c, como los guarda la tabla de 0x6bdb14 (la clase en el nibble bajo
// de la entrada del carácter − 0x20, el estado que sigue en el alto de la entrada clase · 8 + estado). Leídas del
// binario.
enum class CharClass : std::uint8_t
{
    Other,
    Percent,
    Dot,
    Star,
    Zero,
    Digit,
    Flag,
    Size,
    Type,
};

enum class State : std::uint8_t
{
    Normal,
    Percent,
    Flag,
    Width,
    Dot,
    Precision,
    Size,
    Type,
};

constexpr std::size_t StateCount = 8;
constexpr std::size_t ClassCount = 9;

// Los nibbles altos de 0x6bdb14 + clase · 8 + estado.
constexpr std::array<std::array<State, StateCount>, ClassCount> Transitions = {{
    {State::Normal, State::Normal, State::Normal, State::Normal, State::Normal, State::Normal, State::Normal,
     State::Normal},
    {State::Percent, State::Normal, State::Normal, State::Normal, State::Normal, State::Normal, State::Normal,
     State::Percent},
    {State::Normal, State::Dot, State::Dot, State::Dot, State::Normal, State::Normal, State::Normal, State::Normal},
    {State::Normal, State::Width, State::Width, State::Normal, State::Precision, State::Normal, State::Normal,
     State::Normal},
    {State::Normal, State::Flag, State::Flag, State::Width, State::Precision, State::Precision, State::Normal,
     State::Normal},
    {State::Normal, State::Width, State::Width, State::Width, State::Precision, State::Precision, State::Normal,
     State::Normal},
    {State::Normal, State::Flag, State::Flag, State::Normal, State::Normal, State::Normal, State::Normal,
     State::Normal},
    {State::Normal, State::Size, State::Size, State::Size, State::Size, State::Size, State::Size, State::Normal},
    {State::Normal, State::Type, State::Type, State::Type, State::Type, State::Type, State::Type, State::Normal},
}};

// Los nibbles bajos de 0x6bdb14 + (carácter − 0x20); fuera de 0x20..0x78, Other (0x6a594f..0x6a5967).
CharClass ClassOf(char letter)
{
    switch (letter)
    {
    case ' ':
    case '#':
    case '+':
    case '-':
        return CharClass::Flag;
    case '%':
        return CharClass::Percent;
    case '*':
        return CharClass::Star;
    case '.':
        return CharClass::Dot;
    case '0':
        return CharClass::Zero;
    case 'F':
    case 'I':
    case 'L':
    case 'N':
    case 'h':
    case 'l':
    case 'w':
        return CharClass::Size;
    case 'B':
    case 'C':
    case 'E':
    case 'G':
    case 'S':
    case 'X':
    case 'Z':
    case 'c':
    case 'd':
    case 'e':
    case 'f':
    case 'g':
    case 'i':
    case 'n':
    case 'o':
    case 'p':
    case 's':
    case 'u':
    case 'x':
        return CharClass::Type;
    default:
        return letter >= '1' && letter <= '9' ? CharClass::Digit : CharClass::Other;
    }
}

constexpr int Decimal = 10;
constexpr std::uint32_t Octal = 8;
constexpr std::uint32_t Hexadecimal = 16;

// Lo que juntó una marca desde su '%'.
struct Spec
{
    std::size_t start = 0;
    bool left = false;
    bool sign = false;
    bool space = false;
    bool alternate = false;
    bool zero = false;
    bool shortSize = false;
    int width = 0;
    int precision = -1;
};

std::string Digits(std::uint32_t number, std::uint32_t radix, int precision, bool upper)
{
    std::string reversed;
    const char* const digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    while (precision-- > 0 || number != 0)
    {
        reversed.push_back(digits[number % radix]);
        number /= radix;
    }
    return std::string(reversed.rbegin(), reversed.rend());
}

// El final de output.c: el ancho con espacios o ceros, el prefijo (el signo o el 0x) y el texto.
void Pad(std::string& out, const Spec& spec, std::string_view prefix, std::string_view text)
{
    const int used = static_cast<int>(prefix.size() + text.size());
    const int padding = spec.width > used ? spec.width - used : 0;
    const auto count = static_cast<std::size_t>(padding);
    if (!spec.left && !spec.zero)
        out.append(count, ' ');
    out.append(prefix);
    if (spec.zero && !spec.left)
        out.append(count, '0');
    out.append(text);
    if (spec.left)
        out.append(count, ' ');
}

// Un tipo con su argumento; false si MuRet deja la marca escrita (sin argumento, o con uno de la otra clase).
bool Convert(std::string& out, Spec spec, char type, const SprintfArgument* argument)
{
    if (argument == nullptr)
        return false;
    if (type == 's')
    {
        if (!argument->IsText())
            return false;
        std::string_view text = argument->Text();
        if (spec.precision >= 0 && text.size() > static_cast<std::size_t>(spec.precision))
            text = text.substr(0, static_cast<std::size_t>(spec.precision));
        Pad(out, spec, {}, text);
        return true;
    }
    if (argument->IsText())
        return false;
    std::uint32_t bits = argument->Bits();
    if (type == 'c')
    {
        const std::array<char, 1> letter{static_cast<char>(bits & 0xffu)};
        Pad(out, spec, {}, std::string_view(letter.data(), letter.size()));
        return true;
    }
    const bool isSigned = type == 'd' || type == 'i';
    if (spec.shortSize)
        bits = isSigned
                   ? static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int16_t>(bits & 0xffffu)))
                   : (bits & 0xffffu);
    std::uint32_t radix = static_cast<std::uint32_t>(Decimal);
    std::string prefix;
    if (type == 'o')
        radix = Octal;
    else if (type == 'x' || type == 'X')
    {
        radix = Hexadecimal;
        if (spec.alternate && bits != 0)
            prefix = type == 'x' ? "0x" : "0X";
    }
    bool negative = false;
    if (isSigned && static_cast<std::int32_t>(bits) < 0)
    {
        negative = true;
        bits = 0u - bits;
    }
    if (spec.precision < 0)
        spec.precision = 1;
    else
        spec.zero = false;
    std::string text = Digits(bits, radix, spec.precision, type == 'X');
    if (type == 'o' && spec.alternate && (text.empty() || text.front() != '0'))
        text.insert(text.begin(), '0');
    if (isSigned)
    {
        if (negative)
            prefix = "-";
        else if (spec.sign)
            prefix = "+";
        else if (spec.space)
            prefix = " ";
    }
    Pad(out, spec, prefix, text);
    return true;
}

bool IsSupportedType(char type)
{
    switch (type)
    {
    case 'c':
    case 'd':
    case 'i':
    case 'o':
    case 's':
    case 'u':
    case 'x':
    case 'X':
        return true;
    default:
        return false;
    }
}
} // namespace

std::string FormatLikeSprintf(std::string_view format, std::span<const SprintfArgument> arguments)
{
    std::string out;
    std::size_t next = 0;
    const auto take = [&]() -> const SprintfArgument*
    { return next < arguments.size() ? &arguments[next++] : nullptr; };
    State state = State::Normal;
    Spec spec;
    for (std::size_t i = 0; i < format.size() && format[i] != '\0'; ++i)
    {
        const char letter = format[i];
        state = Transitions[static_cast<std::size_t>(ClassOf(letter))][static_cast<std::size_t>(state)];
        switch (state)
        {
        case State::Normal:
            out.push_back(letter);
            break;
        case State::Percent:
            spec = Spec{.start = i};
            break;
        case State::Flag:
            spec.left = spec.left || letter == '-';
            spec.sign = spec.sign || letter == '+';
            spec.space = spec.space || letter == ' ';
            spec.alternate = spec.alternate || letter == '#';
            spec.zero = spec.zero || letter == '0';
            break;
        case State::Width:
            if (letter == '*')
            {
                const SprintfArgument* argument = take();
                const auto width =
                    argument != nullptr && !argument->IsText() ? static_cast<std::int32_t>(argument->Bits()) : 0;
                spec.left = spec.left || width < 0;
                spec.width = width < 0 ? -width : width;
            }
            else
                spec.width = spec.width * Decimal + (letter - '0');
            break;
        case State::Dot:
            spec.precision = 0;
            break;
        case State::Precision:
            if (letter == '*')
            {
                const SprintfArgument* argument = take();
                const auto precision =
                    argument != nullptr && !argument->IsText() ? static_cast<std::int32_t>(argument->Bits()) : 0;
                spec.precision = precision < 0 ? -1 : precision;
            }
            else
                spec.precision = spec.precision * Decimal + (letter - '0');
            break;
        case State::Size:
            spec.shortSize = spec.shortSize || letter == 'h';
            break;
        case State::Type:
        {
            const std::string_view written = format.substr(spec.start, i + 1 - spec.start);
            if (!IsSupportedType(letter))
            {
                out.append(written);
                break;
            }
            const std::size_t before = next;
            if (!Convert(out, spec, letter, take()))
            {
                next = before;
                out.append(written);
            }
            break;
        }
        }
    }
    return out;
}
} // namespace Client::UI
