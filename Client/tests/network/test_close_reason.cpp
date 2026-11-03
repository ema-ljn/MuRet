#include "Network/CloseReason.h"

#include "support/NetworkPrinters.h"

#include <gtest/gtest.h>

#include <set>
#include <string_view>

// Client::Network::CloseReasonText: el motivo de un cierre en palabras, para el log (el plan de la
// CL4, CL4.3).
namespace
{
using Client::Network::CloseReason;
using Client::Network::CloseReasonText;

// Cada motivo tiene su texto, y dos motivos no dicen lo mismo: en el log tienen que distinguirse.
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
