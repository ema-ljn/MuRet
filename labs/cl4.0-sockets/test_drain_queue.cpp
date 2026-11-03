#include "DrainQueue.h"
#include "LabSocket.h"
#include "TestSupport.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <optional>
#include <vector>

// Labs::DrainQueue, la actividad 8 de la guía: lo que el send no tomó queda detrás de un cursor y
// sale después, una sola vez y en orden. Con un send falso que sigue un guion (ScriptedSend).
namespace
{
using Labs::DrainQueue;
using Labs::DrainQueueCapacity;
using Labs::IoStatus;
using Labs::Tests::ScriptedSend;
using Labs::Tests::Sequence;

TEST(DrainQueueTest, SendsEverythingWhenTheSendTakesIt)
{
    DrainQueue queue;
    ScriptedSend send;
    ASSERT_TRUE(queue.Push(Sequence(10)));

    EXPECT_EQ(queue.Flush(send.Function()), IoStatus::Done);

    EXPECT_EQ(queue.Pending(), 0u);
    EXPECT_EQ(send.wire, Sequence(10));
}

// El caso del jefe (Q-C11): 00..09 con un send que toma 2, 2 y después "probá más tarde". El
// primer Flush deja 6 pendientes y al cable salieron 00 01 02 03; el segundo, sin guion, manda
// 04..09 y nada más. Cada llamada pide lo que falta desde el cursor: 10, 8 y 6, y después 6.
TEST(DrainQueueTest, APartialSendKeepsTheRestInOrder)
{
    DrainQueue queue;
    ScriptedSend send;
    send.script = {2, 2, std::nullopt};
    ASSERT_TRUE(queue.Push(Sequence(10)));

    EXPECT_EQ(queue.Flush(send.Function()), IoStatus::TryLater);
    EXPECT_EQ(queue.Pending(), 6u);
    EXPECT_EQ(send.wire, Sequence(4));

    EXPECT_EQ(queue.Flush(send.Function()), IoStatus::Done);
    EXPECT_EQ(queue.Pending(), 0u);
    EXPECT_EQ(send.wire, Sequence(10));
    EXPECT_EQ(send.asked, (std::vector<std::size_t>{10, 8, 6, 6}));
}

// Un send que no toma nada y no dice por qué es como "probá más tarde": Flush vuelve, no gira.
TEST(DrainQueueTest, ASendThatTakesNothingStopsTheFlush)
{
    DrainQueue queue;
    ScriptedSend send;
    send.script = {0};
    ASSERT_TRUE(queue.Push(Sequence(10)));

    EXPECT_EQ(queue.Flush(send.Function()), IoStatus::TryLater);
    EXPECT_EQ(queue.Pending(), 10u);
    EXPECT_EQ(send.asked.size(), 1u);
}

// Entran DrainQueueCapacity bytes, los del buffer de envío del original; uno más no entra y no se
// agrega nada.
TEST(DrainQueueTest, FillsUpToTheOriginalBuffer)
{
    DrainQueue queue;

    EXPECT_TRUE(queue.Push(Sequence(DrainQueueCapacity)));
    EXPECT_FALSE(queue.Push(Sequence(1)));
    EXPECT_EQ(queue.Pending(), DrainQueueCapacity);
}

// Lo que ya salió no ocupa lugar: con el buffer lleno y la mitad mandada, entra otra mitad, y sale
// todo en orden.
TEST(DrainQueueTest, ReusesTheRoomOfWhatWasSent)
{
    constexpr std::size_t Half = DrainQueueCapacity / 2;
    DrainQueue queue;
    ScriptedSend send;
    send.script = {Half, std::nullopt};
    ASSERT_TRUE(queue.Push(Sequence(DrainQueueCapacity)));
    ASSERT_EQ(queue.Flush(send.Function()), IoStatus::TryLater);

    EXPECT_TRUE(queue.Push(Sequence(Half, DrainQueueCapacity)));
    EXPECT_EQ(queue.Pending(), DrainQueueCapacity);
    EXPECT_EQ(queue.Flush(send.Function()), IoStatus::Done);
    EXPECT_EQ(send.wire, Sequence(DrainQueueCapacity + Half));
}

// Lo nuevo va detrás de lo pendiente: con 06..09 esperando, 0A 0B salen después.
TEST(DrainQueueTest, NewBytesGoBehindThePending)
{
    DrainQueue queue;
    ScriptedSend send;
    send.script = {6, std::nullopt};
    ASSERT_TRUE(queue.Push(Sequence(10)));
    ASSERT_EQ(queue.Flush(send.Function()), IoStatus::TryLater);

    ASSERT_TRUE(queue.Push(Sequence(2, 10)));
    EXPECT_EQ(queue.Flush(send.Function()), IoStatus::Done);

    EXPECT_EQ(send.wire, Sequence(12));
}

// Un error del socket vuelve tal cual, y lo pendiente queda: quien llama decide si cierra.
TEST(DrainQueueTest, AnErrorStopsTheFlushAndKeepsThePending)
{
    DrainQueue queue;
    ScriptedSend send;
    send.failWith = IoStatus::PeerClosed;
    ASSERT_TRUE(queue.Push(Sequence(10)));

    EXPECT_EQ(queue.Flush(send.Function()), IoStatus::PeerClosed);
    EXPECT_EQ(queue.Pending(), 10u);
    EXPECT_TRUE(send.wire.empty());
}

// Con nada pendiente, Flush no llama al send.
TEST(DrainQueueTest, AnEmptyQueueDoesNotCallTheSend)
{
    DrainQueue queue;
    ScriptedSend send;

    EXPECT_EQ(queue.Flush(send.Function()), IoStatus::Done);
    EXPECT_TRUE(send.asked.empty());
}
} // namespace
