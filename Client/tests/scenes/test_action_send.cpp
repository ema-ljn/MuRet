#include "Actors/ActionSend.h"

#include "support/SentActions.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <vector>

// Actors::ActionDirection and SendTheAction: the direction of the C1 18 (0x576b44, 0x4f248a, 0x59b456) and who sends
// it. The values come from the ASM's arithmetic (0x6b87dc, 0x6b87d8, 0x6b87d4 and the _ftol 0x69fd3c), not from a run.
namespace
{
using Client::Actors::ActionDirection;
using Client::Actors::ActionSender;
using Client::Actors::SendTheAction;
using Client::Tests::SentAction;

struct Edge
{
    float angle;
    std::uint8_t direction;
};

// Each octant starts at 22.5 + 45 · k: the edge already belongs to the next one.
TEST(ActionSendTest, EachOctantStartsAtItsEdge)
{
    const std::vector<Edge> edges = {
        {0.0f, 1},   {22.49f, 1},  {22.5f, 2},  {67.49f, 2},  {67.5f, 3},  {112.5f, 4}, {157.5f, 5}, {202.5f, 6},
        {247.5f, 7}, {292.49f, 7}, {292.5f, 0}, {337.49f, 0}, {337.5f, 1}, {359.9f, 1}, {382.5f, 2}, {720.0f, 1},
    };
    for (const Edge& edge : edges)
    {
        SCOPED_TRACE(edge.angle);
        EXPECT_EQ(ActionDirection(edge.angle), std::optional<std::uint8_t>{edge.direction});
    }
}

// _ftol truncates toward 0 (Q-C777): from −112.5° to −22.5° (without the edges) it gives 0, and below −67.5° each angle
// gives the next octant; at −112.5° the −1 & 0xff & 7 gives 7.
TEST(ActionSendTest, NegativeAnglesTruncateTowardZero)
{
    const std::vector<Edge> edges = {
        {-22.5f, 1},  {-22.51f, 0},  {-45.0f, 0},  {-100.0f, 0}, {-112.49f, 0},
        {-112.5f, 7}, {-157.49f, 7}, {-157.5f, 6}, {-200.0f, 6}, {-337.5f, 2},
    };
    for (const Edge& edge : edges)
    {
        SCOPED_TRACE(edge.angle);
        EXPECT_EQ(ActionDirection(edge.angle), std::optional<std::uint8_t>{edge.direction});
    }
}

TEST(ActionSendTest, SendTheActionGivesTheDirectionAndTheAction)
{
    std::vector<SentAction> sent;
    const ActionSender send = Client::Tests::RecordingActions(sent);
    ASSERT_TRUE(SendTheAction(100.0f, 0x82, send, "0x4f2416").HasValue());
    ASSERT_TRUE(SendTheAction(-130.0f, 0x8e, send, "0x576b3e").HasValue());
    EXPECT_EQ(sent, (std::vector<SentAction>{{3, 0x82}, {7, 0x8e}}));
}

// Without a sender or with an angle outside the portable _ftol, the error with the caller's address and nothing goes
// out.
TEST(ActionSendTest, NoSenderOrAnUnportableAngleIsAnError)
{
    const auto unsent = SendTheAction(0.0f, 0x82, ActionSender{}, "0x4f2416");
    ASSERT_FALSE(unsent.HasValue());
    EXPECT_NE(unsent.Error().find("0x4f2416"), std::string::npos);

    int calls = 0;
    const ActionSender send = [&calls](std::uint8_t, std::uint8_t) { ++calls; };
    const auto infinite = SendTheAction(std::numeric_limits<float>::infinity(), 0x83, send, "0x576b3e");
    ASSERT_FALSE(infinite.HasValue());
    EXPECT_NE(infinite.Error().find("0x69fd3c"), std::string::npos);
    EXPECT_EQ(calls, 0);
    EXPECT_EQ(ActionDirection(std::numeric_limits<float>::infinity()), std::nullopt);
}
} // namespace
