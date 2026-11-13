#include "UI/Emotes.h"

#include "Data/Text/TextTable.h"
#include "UI/NameChecks.h"

#include <array>
#include <cstddef>
#include <optional>
#include <string_view>

namespace Client::UI
{
namespace
{
// Los 22 casos, por la dirección de su bloque: la animación y la acción de 0x576a60(héroe, héroe, animación, acción).
constexpr EmoteCase Case57ca85{0x99, 0x83};
constexpr EmoteCase Case57c686{0x9b, 0x84};
constexpr EmoteCase Case57c2a2{0x9d, 0x85};
constexpr EmoteCase Case57bea3{0xa3, 0x86};
constexpr EmoteCase Case57babf{0xa1, 0x87};
constexpr EmoteCase Case57b6c0{0xa5, 0x88};
constexpr EmoteCase Case57b2dc{0xa9, 0x8b};
constexpr EmoteCase Case57aef9{0xa7, 0x89};
constexpr EmoteCase Case57ab15{0xab, 0x8c};
constexpr EmoteCase Case57a731{0xaf, 0x8e};
constexpr EmoteCase Case57a333{0x9f, 0x8a};
constexpr EmoteCase Case579f35{0xad, 0x8d};
constexpr EmoteCase Case579b36{0xb1, 0x8f};
constexpr EmoteCase Case579737{0xb3, 0x90};
constexpr EmoteCase Case579353{0xb5, 0x91};
constexpr EmoteCase Case578f54{0xb7, 0x92};
constexpr EmoteCase Case578b70{0xb8, 0x93};
constexpr EmoteCase Case578771{0xc0, 0x94};
constexpr EmoteCase Case57838d{0xbc, 0x98};
constexpr EmoteCase Case577796{0xbd, 0x99};
constexpr EmoteCase Case577b92{0xbf, 0x9b};
constexpr EmoteCase Case577f8e{0xbe, 0x9a};

// Lo que se busca, en tramos: los textos `first`..`last` de Text.bmd uno por uno, o un literal del binario.
struct EmoteRun
{
    std::size_t first = 0;
    std::size_t last = 0;
    std::string_view literal{};
    EmoteCase emote{};
};

constexpr EmoteRun Texts(std::size_t first, std::size_t last, EmoteCase emote)
{
    return EmoteRun{.first = first, .last = last, .literal = {}, .emote = emote};
}

constexpr EmoteRun Literal(std::string_view literal, EmoteCase emote)
{
    return EmoteRun{.first = 0, .last = 0, .literal = literal, .emote = emote};
}

// 0x576ede..0x577f88, en orden. Los literales en CP949: A4 D0 es ㅠ, A4 CC ㅜ y A4 D1 ㅡ.
constexpr std::array<EmoteRun, 37> Runs{{
    Texts(270, 277, Case57ca85),
    Texts(278, 280, Case57c686),
    Texts(281, 286, Case57c2a2),
    Texts(287, 290, Case57bea3),
    Texts(292, 295, Case57babf),
    Texts(296, 302, Case57b6c0),
    Literal(";", Case57b2dc), // 0x6c90e0
    Texts(303, 305, Case57b2dc),
    Literal("\xa4\xd0.\xa4\xd0", Case57aef9), // 0x6c90e4
    Literal("\xa4\xcc.\xa4\xcc", Case57aef9), // 0x6c90ec
    Literal("T_T", Case57aef9),               // 0x6c90f4
    Texts(306, 309, Case57aef9),
    Literal("\xa4\xd1.\xa4\xd1", Case57ab15),  // 0x6c90f8
    Literal("\xa4\xd1.,\xa4\xd1", Case57ab15), // 0x6c9100
    Literal("\xa4\xd1,.\xa4\xd1", Case57ab15), // 0x6c9108
    Literal("-.-", Case57ab15),                // 0x6c9110
    Literal("-_-", Case57ab15),                // 0x6c9114
    Texts(310, 311, Case57ab15),
    Literal("^^", Case57a731),  // 0x6c9118
    Literal("^.^", Case57a731), // 0x6c911c
    Literal("^_^", Case57a731), // 0x6c9120
    Texts(312, 316, Case57a731),
    Texts(318, 321, Case57a333),
    Texts(322, 325, Case579f35),
    Texts(326, 329, Case579b36),
    Texts(330, 334, Case579737),
    Texts(335, 338, Case579353),
    Texts(339, 341, Case578f54),
    Texts(342, 343, Case578b70),
    Literal("/\xa4\xd1", Case578b70), // 0x6c9124
    Literal("\xa4\xd1^", Case578b70), // 0x6c9128
    Texts(344, 347, Case578771),
    Texts(LastEmoteExtraText, LastEmoteExtraText, Case57838d),
    Literal("hustle", Case57838d), // 0x6c912c
    // 0x577780, 0x577b7c y 0x577f78: solos, después de todos.
    Texts(291, 291, Case577796),
    Texts(317, 317, Case577b92),
    Texts(LastEmoteText, LastEmoteText, Case577f8e),
}};

constexpr std::size_t CountTheTriggers()
{
    std::size_t count = 0;
    for (const EmoteRun& run : Runs)
        count += run.literal.empty() ? run.last - run.first + 1 : 1;
    return count;
}
static_assert(CountTheTriggers() == EmoteTriggerCount);

// 0x76d0e44 + (texto − 270) · 0x12c: el texto de Text.bmd, vacío si no lo tiene.
std::string_view TextAt(const Data::Text::TextTable& texts, std::size_t index)
{
    return texts.Text(index).value_or(std::string_view{});
}
} // namespace

std::optional<EmoteCase> FindTheEmote(const Data::Text::TextTable& texts, std::string_view said)
{
    for (const EmoteRun& run : Runs)
    {
        if (!run.literal.empty())
        {
            if (ContainsLikeTheOriginal(said, run.literal))
                return run.emote;
            continue;
        }
        for (std::size_t index = run.first; index <= run.last; ++index)
        {
            if (ContainsLikeTheOriginal(said, TextAt(texts, index)))
                return run.emote;
        }
    }
    return std::nullopt;
}
} // namespace Client::UI
