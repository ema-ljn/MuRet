#include "DrainQueue.h"
#include "LabSocket.h"
#include "TestSupport.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <vector>

// Jefe de la CL4.0 (docs/learning/ola-cl4.0-sockets-no-bloqueantes.md, actividad 9; Q-C11): el
// drenaje de FD_WRITE de 0x4e87b0 tal como está, contra la cola con cursor, los dos sobre el mismo
// send falso que sigue un guion.
//
// OriginalDrain es un modelo chico de 0x4e87b0, escrito desde el decompilado y el desensamblado
// (client-research/mainexe-vmxp-c32bbd5f/decompiled/functions/004e87b0_FUN_004e87b0.asm). No es
// código para el cliente: deja afuera el log y el cierre (0x4e8380), que solo se ven en lo que
// devuelve, y la llamada a 0x4e8880, que es un RET vacío.
namespace
{
using Labs::DrainQueue;
using Labs::IoResult;
using Labs::IoStatus;
using Labs::Tests::ScriptedSend;
using Labs::Tests::Sequence;

// WSAEWOULDBLOCK: CMP EAX,0x2733 en 0x4e8823, después de Ordinal_111 (WSAGetLastError).
constexpr int WsaEWouldBlock = 0x2733;
// Otro error cualquiera, que no es WSAEWOULDBLOCK: con él, 0x4e87b0 loguea "FD_WRITE Send Error" y
// cierra. Es el WSAECONNRESET de Winsock, pero el modelo solo mira que no sea 0x2733.
constexpr int AnotherError = 0x2746;
// El SOCKET_ERROR que devuelve send (Ordinal_19) cuando falla: CMP EDI,-0x1 en 0x4e87dd.
constexpr int SocketError = -1;
// El buffer de envío del objeto de red: 0x2000 bytes en +0x10, con el largo en +0x2010.
constexpr std::size_t OriginalSendBufferSize = 0x2000;

// El buffer de envío del original: los bytes desde +0x10 y lo pendiente en +0x2010, un int.
struct OriginalSendBuffer
{
    std::array<std::byte, OriginalSendBufferSize> bytes{};
    std::int32_t pending = 0;
};

// Lo que dan send y, si falló, WSAGetLastError.
struct WinsockSend
{
    int result = 0;
    int lastError = 0;
};

// El send que llama el modelo: desde `offset` del buffer, `length` bytes, como el
// send(socket, buf + offset, length, 0) de 0x4e87d5.
using OriginalSendFunction = std::function<WinsockSend(std::int32_t offset, std::int32_t length)>;

// 0x4e87b0 tal como está. true si termina bien o con WSAEWOULDBLOCK (MOV EAX,1); false si cierra
// (0x4e883f: CALL 0x4e8380 y XOR EAX,EAX).
bool OriginalDrain(OriginalSendBuffer& buffer, const OriginalSendFunction& send)
{
    // XOR EBX,EBX en 0x4e87b4: lo mandado en esta vuelta arranca en 0 cada vez, y lo que salió en
    // la vuelta anterior no se corre del principio del buffer.
    std::int32_t sent = 0;
    // TEST EAX,EAX y JLE en 0x4e87bd, y JG 0x4e87c1 en 0x4e8810.
    while (buffer.pending > 0)
    {
        // SUB EAX,EBX en 0x4e87ca: pide lo pendiente menos lo mandado, con lo pendiente ya
        // descontado abajo. Lo mandado se resta dos veces.
        const WinsockSend outcome = send(sent, buffer.pending - sent);
        if (outcome.result == SocketError)
        {
            // 0x4e8823: con WSAEWOULDBLOCK vuelve sin tocar nada; con otro error, el log y el
            // cierre.
            return outcome.lastError == WsaEWouldBlock;
        }
        if (outcome.result < 1)
        {
            // JLE 0x4e883f en 0x4e87e4: un send que da 0 también cierra.
            return false;
        }
        sent += outcome.result;           // ADD EBX,EDI en 0x4e8802
        buffer.pending -= outcome.result; // SUB ECX,EDI en 0x4e8804
    }
    return true;
}

// El send del modelo sobre el guion: cuánto pidió cada llamada, que puede ser negativo, y el
// IoResult del guion pasado a lo que daría Winsock. Con un largo negativo no llama al guion: el
// span no lo puede decir, y lo que haría Winsock con él no se probó.
struct OriginalOverTheScript
{
    OriginalSendBuffer& buffer;
    ScriptedSend& scripted;
    std::vector<std::int32_t> asked;

    WinsockSend operator()(std::int32_t offset, std::int32_t length)
    {
        asked.push_back(length);
        if (length < 0)
        {
            return {SocketError, AnotherError};
        }
        const std::span<const std::byte> bytes =
            std::span<const std::byte>(buffer.bytes)
                .subspan(static_cast<std::size_t>(offset), static_cast<std::size_t>(length));
        const IoResult result = scripted(bytes);
        if (result.status == IoStatus::TryLater)
        {
            return {SocketError, WsaEWouldBlock};
        }
        if (result.status != IoStatus::Done)
        {
            return {SocketError, AnotherError};
        }
        return {static_cast<int>(result.bytes), 0};
    }

    OriginalSendFunction Function()
    {
        return [this](std::int32_t offset, std::int32_t length) { return (*this)(offset, length); };
    }
};

// `bytes` en el buffer del original, como lo deja el send que no pudo mandar todo.
OriginalSendBuffer BufferWith(const std::vector<std::byte>& bytes)
{
    OriginalSendBuffer buffer;
    std::copy(bytes.begin(), bytes.end(), buffer.bytes.begin());
    buffer.pending = static_cast<std::int32_t>(bytes.size());
    return buffer;
}

std::vector<std::byte> Concat(const std::vector<std::byte>& first, const std::vector<std::byte>& second)
{
    std::vector<std::byte> joined = first;
    joined.insert(joined.end(), second.begin(), second.end());
    return joined;
}

// Q-C11, con los bytes del paso 5 del plan: 00..09 pendientes y un send que toma 2, 2 y después
// da WSAEWOULDBLOCK. La primera vuelta pide 10, 8 - 2 = 6 y 6 - 4 = 2: al cable salen 00 01 y
// 02 03, y quedan 6 pendientes sin correr. La segunda, con el próximo FD_WRITE y un send que toma
// todo, pide 6 desde el principio del buffer: salen 00..05. Al cable van
// 00 01 02 03 00 01 02 03 04 05: 00..03 repetidos y 06..09 perdidos.
TEST(OriginalDrainTest, RepeatsTheFirstBytesAndLosesTheLast)
{
    OriginalSendBuffer buffer = BufferWith(Sequence(10));
    ScriptedSend scripted;
    scripted.script = {2, 2, std::nullopt};
    OriginalOverTheScript send{buffer, scripted, {}};

    EXPECT_TRUE(OriginalDrain(buffer, send.Function()));
    EXPECT_EQ(scripted.wire, Sequence(4));
    EXPECT_EQ(buffer.pending, 6);
    EXPECT_EQ(send.asked, (std::vector<std::int32_t>{10, 6, 2}));

    EXPECT_TRUE(OriginalDrain(buffer, send.Function()));
    EXPECT_EQ(scripted.wire, Concat(Sequence(4), Sequence(6)));
    EXPECT_EQ(buffer.pending, 0);
    EXPECT_EQ(send.asked, (std::vector<std::int32_t>{10, 6, 2, 6}));
}

// La cola con cursor, con el mismo guion: pide 10, 8 y 6 en la primera vuelta y 6 en la segunda,
// desde el cursor, y al cable van 00..09 una sola vez.
TEST(OriginalDrainTest, TheCursorQueueSendsEachByteOnce)
{
    DrainQueue queue;
    ScriptedSend scripted;
    scripted.script = {2, 2, std::nullopt};
    ASSERT_TRUE(queue.Push(Sequence(10)));

    EXPECT_EQ(queue.Flush(scripted.Function()), IoStatus::TryLater);
    EXPECT_EQ(scripted.wire, Sequence(4));
    EXPECT_EQ(queue.Pending(), 6u);

    EXPECT_EQ(queue.Flush(scripted.Function()), IoStatus::Done);
    EXPECT_EQ(scripted.wire, Sequence(10));
    EXPECT_EQ(scripted.asked, (std::vector<std::size_t>{10, 8, 6, 6}));
}

// Basta un parcial seguido de WSAEWOULDBLOCK: con {2, WSAEWOULDBLOCK} la segunda llamada pide 6 y
// quedan 8 pendientes, y la vuelta siguiente manda 00..07 desde el principio. Al cable,
// 00 01 00 01 02 03 04 05 06 07.
TEST(OriginalDrainTest, OnePartialSendIsEnough)
{
    OriginalSendBuffer buffer = BufferWith(Sequence(10));
    ScriptedSend scripted;
    scripted.script = {2, std::nullopt};
    OriginalOverTheScript send{buffer, scripted, {}};

    EXPECT_TRUE(OriginalDrain(buffer, send.Function()));
    EXPECT_TRUE(OriginalDrain(buffer, send.Function()));

    EXPECT_EQ(scripted.wire, Concat(Sequence(2), Sequence(8)));
    EXPECT_EQ(send.asked, (std::vector<std::int32_t>{10, 6, 8}));
}

// Dos parciales seguidos sin WSAEWOULDBLOCK: 2 de 10 y después los 6 que pidió. Quedan 2
// pendientes con 8 mandados, y la tercera llamada pide 2 - 8 = -6. Qué hace Winsock con un largo
// negativo no se probó: con cualquier error que no sea WSAEWOULDBLOCK, o con 0, 0x4e87b0 cierra.
// Hasta ahí, al cable fueron 00..07, sin repetir nada, y 08 09 no salen.
TEST(OriginalDrainTest, TwoPartialSendsInARowAskForANegativeLength)
{
    OriginalSendBuffer buffer = BufferWith(Sequence(10));
    ScriptedSend scripted;
    scripted.script = {2, 6};
    OriginalOverTheScript send{buffer, scripted, {}};

    EXPECT_FALSE(OriginalDrain(buffer, send.Function()));

    EXPECT_EQ(send.asked, (std::vector<std::int32_t>{10, 6, -6}));
    EXPECT_EQ(scripted.wire, Sequence(8));
}
} // namespace
