#include "UI/Notices.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

// Client::UI: los avisos del login por número (CL5.5; 06 § Los avisos). El texto de cada uno sale
// de la tabla de saltos de 0x6431b0, el aviso del resultado del F1 01 de 0x4e5ebc y 0x4e5f04, y lo
// que hace su OK, de 0x643560. Todos son del tipo 2, con un OK.
namespace
{
using Client::UI::FollowUpOf;
using Client::UI::NoticeFollowUp;
using Client::UI::NoticeId;
using Client::UI::NoticeOfLoginResult;
using Client::UI::NoticeTexts;
using Client::UI::TextsOf;

// Un aviso como optional, sin pasar un int por la plantilla de std::optional (C4242 en MSVC).
std::optional<NoticeId> Some(NoticeId notice)
{
    return notice;
}

// El primer texto de un aviso, o 0 si no tiene.
std::size_t FirstText(NoticeId notice)
{
    const std::optional<NoticeTexts> texts = TextsOf(notice);
    return texts ? texts->first : 0;
}

TEST(NoticesTest, EachNoticeHasItsText)
{
    EXPECT_EQ(FirstText(0x15), 407u);
    EXPECT_EQ(FirstText(0x16), 414u);
    EXPECT_EQ(FirstText(0x17), 415u);
    EXPECT_EQ(FirstText(0x18), 416u);
    EXPECT_EQ(FirstText(0x63), 416u);
    EXPECT_EQ(FirstText(0x19), 417u);
    EXPECT_EQ(FirstText(0x61), 417u);
    EXPECT_EQ(FirstText(0x1b), 408u);
    EXPECT_EQ(FirstText(0x1c), 409u);
    EXPECT_EQ(FirstText(0x1d), 410u);
    EXPECT_EQ(FirstText(0x20), 413u);
    EXPECT_EQ(FirstText(0x22), 597u);
    EXPECT_EQ(FirstText(0x24), 599u);
    EXPECT_EQ(FirstText(0x25), 433u);
    EXPECT_EQ(FirstText(0x26), 435u);
    EXPECT_EQ(FirstText(0x64), 403u);
    EXPECT_EQ(FirstText(0x65), 404u);
    EXPECT_EQ(FirstText(0x70), 402u);
}

// El 0x1a y el 0x6f tienen dos líneas: el 405 y el 406. Los demás, una.
TEST(NoticesTest, TheVersionNoticeHasTwoLines)
{
    constexpr std::array<NoticeId, 2> TwoLines{0x1a, 0x6f};
    for (const NoticeId notice : TwoLines)
    {
        const std::optional<NoticeTexts> texts = TextsOf(notice);
        ASSERT_TRUE(texts.has_value());
        EXPECT_EQ(texts->first, 405u);
        EXPECT_EQ(texts->second, std::optional<std::size_t>{std::size_t{406}});
    }
    EXPECT_EQ(TextsOf(0x70)->second, std::nullopt);
}

// Uno que el login no abre no tiene texto acá.
TEST(NoticesTest, AnUnknownNoticeHasNoText)
{
    EXPECT_EQ(TextsOf(0x00), std::nullopt);
    EXPECT_EQ(TextsOf(0x21), std::nullopt);
    EXPECT_EQ(TextsOf(0xff), std::nullopt);
}

// 0x4e5ebc y 0x4e5f04: el resultado del F1 01. El 1 es el aceptado, sin aviso; el resto, por la
// tabla, y lo que no está en ella (el 7, el 14, el 18 a 191, el 211 y más) da el 0x1b.
TEST(NoticesTest, EachLoginResultHasItsNotice)
{
    EXPECT_EQ(NoticeOfLoginResult(1), std::nullopt);
    EXPECT_EQ(NoticeOfLoginResult(0), Some(0x15));
    EXPECT_EQ(NoticeOfLoginResult(2), Some(0x16));
    EXPECT_EQ(NoticeOfLoginResult(3), Some(0x17));
    EXPECT_EQ(NoticeOfLoginResult(4), Some(0x18));
    EXPECT_EQ(NoticeOfLoginResult(5), Some(0x19));
    EXPECT_EQ(NoticeOfLoginResult(6), Some(0x1a));
    EXPECT_EQ(NoticeOfLoginResult(7), Some(0x1b));
    EXPECT_EQ(NoticeOfLoginResult(8), Some(0x1c));
    EXPECT_EQ(NoticeOfLoginResult(9), Some(0x25));
    EXPECT_EQ(NoticeOfLoginResult(10), Some(0x1d));
    EXPECT_EQ(NoticeOfLoginResult(13), Some(0x20));
    EXPECT_EQ(NoticeOfLoginResult(14), Some(0x1b));
    EXPECT_EQ(NoticeOfLoginResult(15), Some(0x1b));
    EXPECT_EQ(NoticeOfLoginResult(17), Some(0x26));
    EXPECT_EQ(NoticeOfLoginResult(18), Some(0x1b));
    EXPECT_EQ(NoticeOfLoginResult(191), Some(0x1b));
    EXPECT_EQ(NoticeOfLoginResult(192), Some(0x22));
    EXPECT_EQ(NoticeOfLoginResult(193), Some(0x23));
    EXPECT_EQ(NoticeOfLoginResult(194), Some(0x24));
    EXPECT_EQ(NoticeOfLoginResult(195), Some(0x1b));
    EXPECT_EQ(NoticeOfLoginResult(208), Some(0x22));
    EXPECT_EQ(NoticeOfLoginResult(209), Some(0x23));
    EXPECT_EQ(NoticeOfLoginResult(210), Some(0x24));
    EXPECT_EQ(NoticeOfLoginResult(211), Some(0x1b));
    EXPECT_EQ(NoticeOfLoginResult(255), Some(0x1b));
}

// 0x643560: después de una clave mala (0x15) o vacía (0x65), la cuenta se abre con el foco en la
// clave; con los demás del login, en la cuenta; con la versión, el bloqueo y el no conectó, el
// juego se cierra (Q-C69); los otros solo cierran el aviso.
TEST(NoticesTest, TheOkOfEachNotice)
{
    EXPECT_EQ(FollowUpOf(0x15), NoticeFollowUp::ReopenAtPassword);
    EXPECT_EQ(FollowUpOf(0x65), NoticeFollowUp::ReopenAtPassword);
    constexpr std::array<NoticeId, 15> AtTheAccount{0x16, 0x17, 0x18, 0x19, 0x1b, 0x1d, 0x1e, 0x1f,
                                                    0x20, 0x22, 0x23, 0x24, 0x25, 0x26, 0x64};
    for (const NoticeId notice : AtTheAccount)
    {
        EXPECT_EQ(FollowUpOf(notice), NoticeFollowUp::ReopenAtAccount) << static_cast<int>(notice);
    }
    constexpr std::array<NoticeId, 4> Quitting{0x1a, 0x1c, 0x6f, 0x70};
    for (const NoticeId notice : Quitting)
    {
        EXPECT_EQ(FollowUpOf(notice), NoticeFollowUp::Quit) << static_cast<int>(notice);
    }
    EXPECT_EQ(FollowUpOf(0x63), NoticeFollowUp::Close);
    EXPECT_EQ(FollowUpOf(0x61), NoticeFollowUp::Close);
}
} // namespace
