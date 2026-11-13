#include "UI/WhisperSenders.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <string>

// 0x554d20 y 0x555030 (CL8 hito 3; Q-C453): la lista de los que susurraron. Los nombres son inventados.
namespace
{
using Client::UI::ClearTheWhisperSenders;
using Client::UI::RememberTheWhisperSender;
using Client::UI::WhisperSenderAt;
using Client::UI::WhisperSenders;

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

// Diez lugares: el undécimo vuelve al primero.
TEST(WhisperSendersTest, TheNextSlotWrapsAfterTen)
{
    WhisperSenders senders;
    for (char letter = 'a'; letter < 'a' + 11; ++letter)
        RememberTheWhisperSender(senders, 1, std::string(3, letter));

    EXPECT_EQ(WhisperSenderAt(senders, 0), "kkk");
    EXPECT_EQ(WhisperSenderAt(senders, 9), "jjj");
    EXPECT_EQ(senders.next, 1);
}

// strcpy de un nombre de 10 bytes deja su '\0' en el lugar que sigue: los dos se leen seguidos hasta que el de al
// lado se escribe.
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

// 0x555030: los 110 bytes en 0, el próximo queda.
TEST(WhisperSendersTest, ClearingKeepsTheNextSlot)
{
    WhisperSenders senders;
    RememberTheWhisperSender(senders, 1, "Tordo");
    ClearTheWhisperSenders(senders);

    EXPECT_EQ(WhisperSenderAt(senders, 0), "");
    EXPECT_EQ(senders.next, 1);
}
} // namespace
