#include "Scenes/ActionSend.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// Scenes::ActionDirection y SendTheAction: la dirección del C1 18 (0x576b44, 0x4f248a, 0x59b456) y quién lo manda.
// Los valores salen de la cuenta del ASM (0x6b87dc, 0x6b87d8, 0x6b87d4 y el _ftol 0x69fd3c), no de una corrida.
namespace
{
using Client::Scenes::ActionDirection;
using Client::Scenes::ActionSender;
using Client::Scenes::SendTheAction;

struct Edge
{
    float angle;
    std::uint8_t direction;
};

// Cada octante empieza en 22,5 + 45 · k: el borde ya es del siguiente.
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

// El _ftol trunca hacia el 0 (Q-C777): de −112,5° a −22,5° (sin los bordes) da 0, y debajo de −67,5° cada ángulo da el
// octante de al lado; en −112,5° el −1 & 0xff & 7 da 7.
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
    std::vector<std::pair<std::uint8_t, std::uint8_t>> sent;
    const ActionSender send = [&sent](std::uint8_t direction, std::uint8_t action)
    { sent.emplace_back(direction, action); };
    ASSERT_TRUE(SendTheAction(100.0f, 0x82, send, "0x4f2416").HasValue());
    ASSERT_TRUE(SendTheAction(-130.0f, 0x8e, send, "0x576b3e").HasValue());
    EXPECT_EQ(sent, (std::vector<std::pair<std::uint8_t, std::uint8_t>>{{3, 0x82}, {7, 0x8e}}));
}

// Sin quien mande o con un ángulo fuera del _ftol portable, el error con la dirección de quien llama y nada sale.
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
