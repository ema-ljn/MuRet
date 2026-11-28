#include "Ui/Notices.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <ios>
#include <optional>
#include <vector>

// Client::Ui: the login notices by number (CL5.5; 06 § Los avisos). Each one's text comes from 0x6431b0's jump table,
// the F1 01 result's notice from 0x4e5ebc and 0x4e5f04, and what its OK does, from 0x643560. All are type 2, with an
// OK.
namespace
{
using Client::Ui::FollowUpOf;
using Client::Ui::KindOf;
using Client::Ui::NoticeFollowUp;
using Client::Ui::NoticeId;
using Client::Ui::NoticeKind;
using Client::Ui::NoticeOfCreateResult;
using Client::Ui::NoticeOfDeleteResult;
using Client::Ui::NoticeOfLoginResult;
using Client::Ui::NoticeTexts;
using Client::Ui::TextsOf;

// Without passing an int through std::optional's template (C4242 in MSVC).
std::optional<NoticeId> Some(NoticeId notice)
{
    return notice;
}

// 0 if it has none.
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

// The 0x1a and the 0x6f have two lines: 405 and 406. The rest, one.
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

// One the login does not open has no text here.
TEST(NoticesTest, AnUnknownNoticeHasNoText)
{
    EXPECT_EQ(TextsOf(0x00), std::nullopt);
    EXPECT_EQ(TextsOf(0x21), std::nullopt);
    EXPECT_EQ(TextsOf(0xff), std::nullopt);
}

// 0x4e5ebc and 0x4e5f04: the F1 01 result. 1 is accepted, without a notice; the rest, by the table, and what is not
// in it (7, 14, 18 to 191, 211 and up) gives the 0x1b.
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

// 0x643560: after a wrong (0x15) or empty (0x65) password, the account window opens with the focus on the password;
// with the other login ones, on the account; with the version, the lock and the not connected, the game closes
// (Q-C69); the others only close the notice.
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

// The selection ones (CL6.5), with the text of 0x6431b0's jump table.
TEST(NoticesTest, EachSelectionNoticeHasItsText)
{
    struct NoticeText
    {
        NoticeId notice;
        std::size_t text;
    };
    const std::vector<NoticeText> expected{{0x36, 1716}, {0x37, 396},  {0x5d, 471}, {0x5e, 1712}, {0x5f, 1713},
                                           {0x60, 1714}, {0x61, 417},  {0x62, 439}, {0x72, 1715}, {0x79, 390},
                                           {0x7a, 391},  {0x83, 1711}, {0x89, 401}, {0x92, 1654}};
    for (const auto& [notice, text] : expected)
    {
        const std::optional<NoticeTexts> texts = TextsOf(notice);
        ASSERT_TRUE(texts.has_value()) << std::hex << static_cast<int>(notice);
        EXPECT_EQ(texts->first, text) << std::hex << static_cast<int>(notice);
        EXPECT_FALSE(texts->second.has_value()) << std::hex << static_cast<int>(notice);
    }
}

// 0x6431b0's kinds (0x642bf0): the 0x5d without buttons, the 0x5e with OK and cancel, the 0x5f with the code field;
// the rest, and all the login ones, with an OK.
TEST(NoticesTest, EachNoticeHasItsKind)
{
    EXPECT_EQ(KindOf(0x5d), NoticeKind::NoButton);
    EXPECT_EQ(KindOf(0x5e), NoticeKind::OkCancel);
    EXPECT_EQ(KindOf(0x5f), NoticeKind::Code);
    for (const NoticeId notice :
         {NoticeId{0x36}, NoticeId{0x60}, NoticeId{0x84}, NoticeId{0x92}, NoticeId{0x70}, NoticeId{0x15}})
    {
        EXPECT_EQ(KindOf(notice), NoticeKind::Ok) << std::hex << static_cast<int>(notice);
    }
}

// The 0x5e's OK opens the code one, and the 0x5f's sends the deletion (0x643560).
TEST(NoticesTest, TheDeletionGoesFromTheConfirmationToTheCode)
{
    EXPECT_EQ(FollowUpOf(0x5e), NoticeFollowUp::AskForTheCode);
    EXPECT_EQ(FollowUpOf(0x5f), NoticeFollowUp::SendTheDeletion);
    EXPECT_EQ(FollowUpOf(0x60), NoticeFollowUp::Close);
    EXPECT_EQ(FollowUpOf(0x92), NoticeFollowUp::Close);
}

// The answers: create (0x4c2290) with 0 the 0x36 and with 2 the 0x37; delete (0x4e2b10) with 0 the 0x92, with 1 the
// 0x60, with 3 the 0x62 and with another the 0x89.
TEST(NoticesTest, EachCreateAndDeleteResultHasItsNotice)
{
    EXPECT_EQ(NoticeOfCreateResult(0), std::optional{NoticeId{0x36}});
    EXPECT_EQ(NoticeOfCreateResult(2), std::optional{NoticeId{0x37}});
    EXPECT_EQ(NoticeOfCreateResult(1), std::nullopt);
    EXPECT_EQ(NoticeOfCreateResult(5), std::nullopt);
    EXPECT_EQ(NoticeOfDeleteResult(0), NoticeId{0x92});
    EXPECT_EQ(NoticeOfDeleteResult(1), NoticeId{0x60});
    EXPECT_EQ(NoticeOfDeleteResult(3), NoticeId{0x62});
    EXPECT_EQ(NoticeOfDeleteResult(2), NoticeId{0x89});
    EXPECT_EQ(NoticeOfDeleteResult(9), NoticeId{0x89});
}
} // namespace
