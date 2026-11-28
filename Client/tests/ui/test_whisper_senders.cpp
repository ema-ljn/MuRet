#include "Chat/WhisperSenders.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <string>

// 0x554d20 and 0x555030 (CL8 milestone 3; Q-C453): the list of those who whispered. The names are invented.
namespace
{
using Client::Chat::ClearTheWhisperSenders;
using Client::Chat::RememberTheWhisperSender;
using Client::Chat::WhisperSenderAt;
using Client::Chat::WhisperSenders;

TEST(WhisperSendersTest, RemembersEachSenderOnceBelowLevelTen)
{
    WhisperSenders senders;
    RememberTheWhisperSender(senders, 9, "Tordo");
    RememberTheWhisperSender(senders, 9, "Tordo");
    RememberTheWhisperSender(senders, 10, "Zorzal");

    EXPECT_EQ(WhisperSenderAt(senders, 0), "Tordo");
    EXPECT_EQ(WhisperSenderAt(senders, 1), "");
    EXPECT_EQ(senders.next, 1);
}

TEST(WhisperSendersTest, TheNextSlotWrapsAfterTen)
{
    WhisperSenders senders;
    for (char letter = 'a'; letter < 'a' + 11; ++letter)
        RememberTheWhisperSender(senders, 1, std::string(3, letter));

    EXPECT_EQ(WhisperSenderAt(senders, 0), "kkk");
    EXPECT_EQ(WhisperSenderAt(senders, 9), "jjj");
    EXPECT_EQ(senders.next, 1);
}

// strcpy of a 10-byte name leaves its '\0' in the next slot: the two read as one until the neighbor is written.
TEST(WhisperSendersTest, ATenByteNameRunsIntoTheNextSlot)
{
    WhisperSenders senders;
    senders.bytes[10] = 'x';
    senders.bytes[11] = 'y';
    RememberTheWhisperSender(senders, 1, "Calandrias");

    EXPECT_EQ(WhisperSenderAt(senders, 0), "Calandrias");
    EXPECT_EQ(WhisperSenderAt(senders, 1), "");

    WhisperSenders crossed;
    crossed.bytes.fill('\0');
    for (std::size_t i = 0; i < 10; ++i)
        crossed.bytes[i] = 'z';
    crossed.bytes[10] = 'w';
    EXPECT_EQ(WhisperSenderAt(crossed, 0), "zzzzzzzzzzw");
}

// 0x555030: the 110 bytes at 0, the next one stays.
TEST(WhisperSendersTest, ClearingKeepsTheNextSlot)
{
    WhisperSenders senders;
    RememberTheWhisperSender(senders, 1, "Tordo");
    ClearTheWhisperSenders(senders);

    EXPECT_EQ(WhisperSenderAt(senders, 0), "");
    EXPECT_EQ(senders.next, 1);
}
} // namespace
