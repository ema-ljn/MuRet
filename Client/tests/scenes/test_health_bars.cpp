#include "Scenes/HealthBars.h"

#include "App/WindowState.h"
#include "Core/Expected.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Scenes/ActorState.h"
#include "Scenes/GameProjection.h"
#include "Scenes/WorldRuntime.h"
#include "UI/HudPrimitives.h"

#include <glm/ext/vector_float3.hpp>
#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <span>
#include <string>
#include <vector>

// Las barras de vida del 0x1a8 (CL9 hito 2): la cola de 0x42a2b0 y el dibujo de 0x42a4d0. Las posiciones son
// inventadas; la proyección deja el ojo en (x, y, 1), así que un punto va a (320 − x, 240 + y).
namespace
{
using Client::Render::SpriteBatch;
using Client::Render::SpriteVertex;
using Client::Scenes::ActorState;
using Client::Scenes::GameProjection;
using Client::Scenes::QueuedHealthBar;

constexpr Client::App::PixelSize Screen640{640, 480};
constexpr std::uint16_t LifeStoneType = 0x1a8;

GameProjection FlatProjection()
{
    GameProjection flat;
    flat.view = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    flat.horizontalScale = 1.0f;
    flat.verticalScale = 1.0f;
    flat.centerX = 320;
    flat.centerY = 240;
    flat.screenWidth = 640;
    flat.screenHeight = 480;
    return flat;
}

ActorState Stone(std::uint8_t stage)
{
    ActorState stone;
    stone.type = LifeStoneType;
    stone.byte93 = stage;
    stone.position = {12.5f, -30.0f, 0.1f};
    stone.boundsTop = 70.3f;
    return stone;
}

// Lo que dejaría 0x5a6af0 con los argumentos que 0x42a4d0 le pasa por cada barra.
std::vector<SpriteVertex> ExpectedGauges(const std::vector<QueuedHealthBar>& bars, const GameProjection& projection)
{
    SpriteBatch sprites;
    sprites.Begin(Screen640);
    for (const QueuedHealthBar& bar : bars)
    {
        const auto point = Client::Scenes::ProjectGamePoint(projection, bar.position);
        EXPECT_TRUE(point.HasValue());
        if (!point.HasValue())
            continue;
        Client::UI::DrawGauge(sprites, static_cast<float>(point.Value().x - 25),
                              static_cast<float>(point.Value().y + 12), 50.0f, 2.0f, static_cast<float>(bar.fill * 10),
                              false, true);
    }
    const std::span<const SpriteVertex> vertices = sprites.Vertices();
    return {vertices.begin(), vertices.end()};
}

// 0x42a2b0: solo el 0x1a8 con +0x93 debajo de 5, al final de la cola, con la z sobre el techo de su caja (la suma en
// double y el FSTP en float).
TEST(HealthBarsTest, OnlyTheLifeStoneUnderFiveIsQueued)
{
    std::deque<QueuedHealthBar> queue;
    Client::Scenes::QueueTheHealthBar(Stone(4), queue);
    Client::Scenes::QueueTheHealthBar(Stone(0), queue);
    Client::Scenes::QueueTheHealthBar(Stone(5), queue);
    ActorState other = Stone(1);
    other.type = 0x1a7;
    Client::Scenes::QueueTheHealthBar(other, queue);

    ASSERT_EQ(queue.size(), 2u);
    const float top = static_cast<float>(static_cast<double>(0.1f) + static_cast<double>(70.3f) + 100.0);
    EXPECT_EQ(queue[0].position, glm::vec3(12.5f, -30.0f, top));
    EXPECT_EQ(queue[0].fill, 4);
    EXPECT_EQ(queue[1].fill, 0);
    EXPECT_NE(top, 0.1f + 70.3f + 100.0f) << "la suma en float da otro valor: el caso no prueba la x87";
}

// 0x42a4d0: la cuenta baja con cada pop_front y el índice sube, así que de 1, 2, 3 y 4 dibuja 1, 1, 2 y 2, los
// primeros, y deja el resto en la cola. Cada barra: 0x5a6af0(x − 25, y + 12, 50, 2, +0x93 · 10, verde, acotada).
TEST(HealthBarsTest, EachFrameDrawsTheFirstHalfRoundedUp)
{
    const GameProjection flat = FlatProjection();
    for (const std::size_t queued : {1u, 2u, 3u, 4u})
    {
        SCOPED_TRACE(queued);
        std::deque<QueuedHealthBar> queue;
        std::vector<QueuedHealthBar> bars;
        for (std::size_t i = 0; i < queued; ++i)
        {
            bars.push_back(QueuedHealthBar{.position = {10.0f * static_cast<float>(i), -20.0f, 0.0f},
                                           .fill = static_cast<std::uint8_t>(i + 1)});
            queue.push_back(bars.back());
        }
        SpriteBatch sprites;
        sprites.Begin(Screen640);

        const auto drawn = Client::Scenes::DrawTheHealthBars(queue, &flat, sprites);

        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
        const std::size_t shown = (queued + 1) / 2;
        const std::vector<QueuedHealthBar> first(bars.begin(), bars.begin() + static_cast<std::ptrdiff_t>(shown));
        const std::vector<SpriteVertex> expected = ExpectedGauges(first, flat);
        const std::span<const SpriteVertex> vertices = sprites.Vertices();
        ASSERT_EQ(vertices.size(), expected.size());
        for (std::size_t v = 0; v < expected.size(); ++v)
        {
            EXPECT_EQ(vertices[v].x, expected[v].x) << v;
            EXPECT_EQ(vertices[v].y, expected[v].y) << v;
            EXPECT_EQ(vertices[v].color, expected[v].color) << v;
        }
        ASSERT_EQ(queue.size(), queued - shown);
        if (!queue.empty())
        {
            EXPECT_EQ(queue.front().fill, bars[shown].fill);
        }
    }
}

// La barra del punto en (−20, 30) va a (340 − 25, 270 + 12): el marco de 0x5a6af0 en (315, 282), 54 por 6.
TEST(HealthBarsTest, TheBarHangsUnderThePoint)
{
    const GameProjection flat = FlatProjection();
    std::deque<QueuedHealthBar> queue{QueuedHealthBar{.position = {-20.0f, 30.0f, 0.0f}, .fill = 2}};
    SpriteBatch sprites;
    sprites.Begin(Screen640);

    ASSERT_TRUE(Client::Scenes::DrawTheHealthBars(queue, &flat, sprites).HasValue());

    SpriteBatch expected;
    expected.Begin(Screen640);
    Client::UI::DrawGauge(expected, 315.0f, 282.0f, 50.0f, 2.0f, 20.0f, false, true);
    ASSERT_EQ(sprites.Vertices().size(), expected.Vertices().size());
    for (std::size_t v = 0; v < expected.Vertices().size(); ++v)
    {
        EXPECT_EQ(sprites.Vertices()[v].x, expected.Vertices()[v].x) << v;
        EXPECT_EQ(sprites.Vertices()[v].y, expected.Vertices()[v].y) << v;
    }
    EXPECT_TRUE(queue.empty());
}

// Sin la proyección del pase del mundo, una cola con algo corta antes de sacar nada; la vacía no la necesita.
TEST(HealthBarsTest, WithoutTheProjectionAQueuedBarStops)
{
    SpriteBatch sprites;
    sprites.Begin(Screen640);
    std::deque<QueuedHealthBar> empty;
    EXPECT_TRUE(Client::Scenes::DrawTheHealthBars(empty, nullptr, sprites).HasValue());

    std::deque<QueuedHealthBar> queue{QueuedHealthBar{}, QueuedHealthBar{}};
    const auto drawn = Client::Scenes::DrawTheHealthBars(queue, nullptr, sprites);
    ASSERT_FALSE(drawn.HasValue());
    EXPECT_NE(drawn.Error().find("0x42a529"), std::string::npos) << drawn.Error();
    EXPECT_EQ(queue.size(), 2u);
    EXPECT_TRUE(sprites.Vertices().empty());
}
} // namespace
