#include "LabSocket.h"
#include "TestSupport.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <utility>

// Labs::LabSocket, la actividad 3 de la guía: RAII sobre el descriptor, que se mueve y no se
// copia, y un bind a 127.0.0.1:0 que da un puerto.
namespace
{
using Labs::LabSocket;

class LabSocketTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_TRUE(m_network.Ok());
    }

    Labs::NetworkInit m_network;
};

TEST_F(LabSocketTest, OpensATcpSocket)
{
    const LabSocket socket = LabSocket::OpenTcp();

    EXPECT_TRUE(socket.IsOpen());
}

// Uno vacío no tiene descriptor: todo da false o 0, y cerrarlo no hace nada.
TEST_F(LabSocketTest, AnEmptySocketHasNothingToDo)
{
    LabSocket socket;

    EXPECT_FALSE(socket.IsOpen());
    EXPECT_FALSE(socket.SetNonBlocking());
    EXPECT_FALSE(socket.BindLoopback(0));
    EXPECT_EQ(socket.LocalPort(), 0);
    socket.Close();
    EXPECT_FALSE(socket.IsOpen());
}

// Con el puerto 0, el sistema elige uno libre, y getsockname lo dice.
TEST_F(LabSocketTest, BindingToPortZeroGetsAPort)
{
    LabSocket socket = LabSocket::OpenTcp();

    ASSERT_TRUE(socket.BindLoopback(0));
    EXPECT_NE(socket.LocalPort(), 0);
}

// El movido queda vacío, y al morir no cierra el descriptor que se llevó el otro: el otro sigue
// teniendo el mismo puerto. Si el destructor del movido lo cerrara, getsockname fallaría y
// LocalPort daría 0.
TEST_F(LabSocketTest, AMovedSocketLeavesTheDescriptorToTheOther)
{
    LabSocket other;
    std::uint16_t port = 0;
    {
        LabSocket moved = LabSocket::OpenTcp();
        ASSERT_TRUE(moved.BindLoopback(0));
        port = moved.LocalPort();
        other = std::move(moved);
        EXPECT_FALSE(moved.IsOpen()); // NOLINT(bugprone-use-after-move): es lo que se prueba
    }

    EXPECT_TRUE(other.IsOpen());
    EXPECT_EQ(other.LocalPort(), port);
}

// Lo mismo con el constructor de movimiento: el nuevo se queda con el descriptor, y el movido,
// vacío, no lo cierra al morir.
TEST_F(LabSocketTest, AMoveConstructedSocketTakesTheDescriptor)
{
    std::optional<LabSocket> other;
    std::uint16_t port = 0;
    {
        LabSocket moved = LabSocket::OpenTcp();
        ASSERT_TRUE(moved.BindLoopback(0));
        port = moved.LocalPort();
        other.emplace(std::move(moved));
        EXPECT_FALSE(moved.IsOpen()); // NOLINT(bugprone-use-after-move): es lo que se prueba
    }

    ASSERT_TRUE(other.has_value());
    EXPECT_TRUE(other->IsOpen());
    EXPECT_EQ(other->LocalPort(), port);
}

// El destructor cierra: el puerto queda libre, y otro socket lo puede tomar. Un listener que no
// aceptó a nadie no deja nada en TIME_WAIT, así que no hace falta SO_REUSEADDR.
TEST_F(LabSocketTest, TheDestructorFreesThePort)
{
    std::uint16_t port = 0;
    {
        LabSocket listener = LabSocket::OpenTcp();
        ASSERT_TRUE(listener.BindLoopback(0));
        ASSERT_TRUE(listener.Listen());
        port = listener.LocalPort();
    }

    LabSocket again = LabSocket::OpenTcp();
    EXPECT_TRUE(again.BindLoopback(port));
}

// Asignar encima cierra el que había: su puerto también queda libre.
TEST_F(LabSocketTest, MovingOntoASocketClosesTheOldOne)
{
    LabSocket listener = LabSocket::OpenTcp();
    ASSERT_TRUE(listener.BindLoopback(0));
    ASSERT_TRUE(listener.Listen());
    const std::uint16_t port = listener.LocalPort();

    listener = LabSocket::OpenTcp();

    EXPECT_TRUE(listener.IsOpen());
    EXPECT_EQ(listener.LocalPort(), 0);
    LabSocket again = LabSocket::OpenTcp();
    EXPECT_TRUE(again.BindLoopback(port));
}
} // namespace
