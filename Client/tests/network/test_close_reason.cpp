#include "Network/CloseReason.h"

#include "support/NetworkPrinters.h"

#include <gtest/gtest.h>

#include <set>
#include <string_view>

namespace
{
using Client::Network::CloseReason;
using Client::Network::CloseReasonText;

// Two reasons never say the same: the log has to tell them apart.
TEST(CloseReasonTest, EveryReasonHasItsOwnText)
{
    const CloseReason reasons[] = {
        CloseReason::ByClient,   CloseReason::ResolveFailed, CloseReason::Refused,   CloseReason::ConnectTimedOut,
        CloseReason::PeerClosed, CloseReason::SocketFailed,  CloseReason::BadStream, CloseReason::SendQueueFull,
    };
    std::set<std::string_view> texts;
    for (const CloseReason reason : reasons)
    {
        SCOPED_TRACE(::testing::PrintToString(reason));
        const std::string_view text = CloseReasonText(reason);
        EXPECT_FALSE(text.empty());
        EXPECT_TRUE(texts.insert(text).second) << "repetido: " << text;
    }
}
} // namespace
