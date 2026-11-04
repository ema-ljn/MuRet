#include "Render/Models/RayPicking.h"

#include <gtest/gtest.h>

#include <glm/ext/vector_float3.hpp>

#include <array>
#include <cstddef>
#include <optional>

// Client::Render::Models::RayPicking: el segmento del mouse contra la caja de cada personaje, como
// 0x622900 (una separación en 6 ejes) y 0x59e700 (gana el de menor distancia² de la cámara a su
// origen), con la caja del jugador corrida de Q-C100 (06 § El picking).
namespace
{
using Client::Render::Models::Box;
using Client::Render::Models::NearestHit;
using Client::Render::Models::PlayerBoxAt;
using Client::Render::Models::SegmentHitsBox;

// Un cubo de 10 de lado centrado en el origen.
constexpr Box UnitBox{{-5.0f, -5.0f, -5.0f}, {5.0f, 5.0f, 5.0f}};

TEST(RayPickingTest, HitsTheBoxInFront)
{
    EXPECT_TRUE(SegmentHitsBox({0, 0, 100}, {0, 0, -100}, UnitBox));
    EXPECT_TRUE(SegmentHitsBox({-100, -100, -100}, {100, 100, 100}, UnitBox));
    EXPECT_TRUE(SegmentHitsBox({4, -100, 0}, {4, 100, 0}, UnitBox));
}

TEST(RayPickingTest, MissesBeside)
{
    EXPECT_FALSE(SegmentHitsBox({6, 0, 100}, {6, 0, -100}, UnitBox));
    EXPECT_FALSE(SegmentHitsBox({-100, 6, -100}, {100, 6, 100}, UnitBox));
    // En diagonal, al lado de una arista: solo lo separa un eje producto.
    EXPECT_FALSE(SegmentHitsBox({-100, 112, 0}, {100, -88, 0}, UnitBox));
}

// Es un segmento: lo que queda detrás de su fin, o antes de su origen, no se toca.
TEST(RayPickingTest, TheSegmentEndsWhereItEnds)
{
    EXPECT_FALSE(SegmentHitsBox({0, 0, 100}, {0, 0, 10}, UnitBox));
    EXPECT_FALSE(SegmentHitsBox({0, 0, -10}, {0, 0, -100}, UnitBox));
    EXPECT_TRUE(SegmentHitsBox({0, 0, 100}, {0, 0, 4}, UnitBox));
}

// Q-C100: la caja del 0x20a va de (−60, −60, 0) a (40, 40, 120) desde la posición, sin girar con el
// personaje.
TEST(RayPickingTest, ThePlayerBoxIsOffCentre)
{
    const glm::vec3 position{1000.0f, 2000.0f, 163.0f};
    const Box box = PlayerBoxAt(position);

    EXPECT_EQ(box.min, glm::vec3(940.0f, 1940.0f, 163.0f));
    EXPECT_EQ(box.max, glm::vec3(1040.0f, 2040.0f, 283.0f));
    // Un segmento vertical en x + 45 pasa al lado, y uno en x − 55, adentro.
    EXPECT_FALSE(SegmentHitsBox(position + glm::vec3(45, 0, 500), position + glm::vec3(45, 0, -500), box));
    EXPECT_TRUE(SegmentHitsBox(position + glm::vec3(-55, 0, 500), position + glm::vec3(-55, 0, -500), box));
}

// 0x59e700: de los que toca el segmento, el de menor distancia² de la cámara (el origen) a su
// posición, no al punto donde lo toca. Sin ninguno, std::nullopt.
TEST(RayPickingTest, TheNearestIsByOriginDistance)
{
    const glm::vec3 camera{0.0f, 0.0f, 1000.0f};
    const glm::vec3 end{0.0f, 0.0f, -1000.0f};
    // El 0 y el 2 están debajo de la cámara; el 2 más cerca. El 1, al lado.
    const std::array<glm::vec3, 3> positions = {glm::vec3{10, 10, 0}, glm::vec3{500, 0, 0}, glm::vec3{10, 10, 300}};

    EXPECT_EQ(NearestHit(camera, end, positions), std::optional<std::size_t>{2});
    EXPECT_EQ(NearestHit(camera, end, std::array<glm::vec3, 1>{glm::vec3{500, 0, 0}}), std::nullopt);
}

// Gana el origen más cerca aunque el segmento entre antes en otra caja. La cámara mira a lo largo de
// x a la altura 60: entra en la caja de B (de x 0 a 100, z de −59 a 61) enseguida, y en la de A (x
// de 40 a 140) más adelante, pero el origen de A está a 100² + 60² de la cámara y el de B a 60² +
// 119².
TEST(RayPickingTest, TheNearestIsNotWhereTheSegmentEnters)
{
    const glm::vec3 camera{0.0f, 0.0f, 60.0f};
    const glm::vec3 end{2000.0f, 0.0f, 60.0f};
    const std::array<glm::vec3, 2> positions = {glm::vec3{60, 0, -59}, glm::vec3{100, 0, 0}};

    EXPECT_EQ(NearestHit(camera, end, positions), std::optional<std::size_t>{1});
}
} // namespace
