// Jefe de la CL2.0 (docs/learning/ola-cl2.0-flujos-y-bloques.md, actividad 13): Q-C03, el corte del
// original que lee bytes viejos (Client/docs/research/07-quirks-y-bugs.md).
//
// OldCut es un modelo chico de la lectura y el corte de main.exe (0x4e88a0 y 0x4e893d), escrito a
// partir de RecvBuffer en red_reference.py (repo de research,
// client-research/analysis/mainexe/scripts/). No es código para el cliente: muestra el bug, y los
// tests dicen qué hace el FrameCutter con el mismo flujo.
//
// El modelo deja afuera el hook de FoxAnti: en el original, 0x4e8938 salta a 0x6b7cb0, que llama a
// [0x6b7d00] y pisa +0x4014 con lo que devuelve, antes del corte. Es lo que hace selftest de
// red_reference.py con hook=None, y lo de FoxAnti va en la CL2.4.

#include "FrameCutter.h"

#include "FrameBytes.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <span>
#include <utility>
#include <vector>

namespace
{

using Labs::Frame;
using Labs::FrameCutter;
using Labs::Tests::Bytes;
using Labs::Tests::FeedAll;
using Labs::Tests::FirstC1;
using Labs::Tests::Join;
using Labs::Tests::SecondC2;

// Lo que mide el buffer de recepción del original: 0x4e88af, cmp eax,0x2000.
constexpr std::size_t ReceiveCapacity = 0x2000;
// Con menos bytes que estos, 0x4e88a0 vuelve sin cortar (0x4e8927, cmp eax,0x3).
constexpr std::size_t MinBytesToCut = 3;
// Cuánto se corre el byte alto del tamaño de un C2/C4, que es big-endian (0x4e8967, shl esi,0x8).
constexpr unsigned BitsPerByte = 8;

class OldCut
{
public:
    // Lo que devuelve 0x4e88a0: 0 si cortó, 3 si esperó más bytes y 1 si no pudo leer (el buffer
    // lleno, o recv en 0 o en error). red_reference.py llama CLOSED al 1, pero con el buffer lleno
    // nadie cierra nada: el WndProc ignora lo que devuelve (0x4af4e0, en 0x4b3207).
    enum class ReadResult : std::uint8_t
    {
        Cut = 0,
        NotRead = 1,
        NeedMore = 3,
    };

    // Una lectura de recv. Como en el original, recv nunca trae más que el lugar libre.
    ReadResult OnRead(std::span<const std::byte> chunk)
    {
        // 0x4e88af: con el buffer lleno lo anota en MuError.log (0x41fa40) y vuelve sin llamar a
        // recv. El modelo cuenta esas anotaciones para que un test vea que pasó por acá.
        if (count >= ReceiveCapacity)
        {
            ++overflowLogs;
            return ReadResult::NotRead;
        }
        // recv devuelve 0 cuando el otro lado cerró. Más que el lugar libre no pasa en el
        // original (recv pide 0x2000 - n); acá se rechaza para no escribir fuera del buffer.
        if (chunk.empty() || chunk.size() > ReceiveCapacity - count)
        {
            return ReadResult::NotRead;
        }
        std::ranges::copy(chunk, buffer.begin() + static_cast<std::ptrdiff_t>(count));
        count += chunk.size();
        if (count < MinBytesToCut)
        {
            return ReadResult::NeedMore;
        }
        Split();
        return ReadResult::Cut;
    }

    // Los paquetes que el corte puso en la cola (0x4e8f70), en orden.
    [[nodiscard]] const std::vector<Frame>& Queue() const
    {
        return queue;
    }

    // Los bytes que el original cree que tiene sin cortar (+0x4014).
    [[nodiscard]] std::size_t Count() const
    {
        return count;
    }

    // Cuántas veces vio el buffer lleno y lo anotó en el log.
    [[nodiscard]] std::size_t OverflowLogs() const
    {
        return overflowLogs;
    }

    // Los primeros n bytes del buffer, para ver qué se corrió al principio y qué no.
    [[nodiscard]] Frame Prefix(std::size_t n) const
    {
        return {buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(n)};
    }

private:
    // 0x4e893d. Lee buffer[offset + 1] y buffer[offset + 2] sin mirar cuántos bytes quedan.
    void Split()
    {
        std::size_t offset = 0;
        while (true)
        {
            const std::byte type = buffer[offset];
            std::size_t size = 0;
            if (type == Labs::TypeC1 || type == Labs::TypeC3)
            {
                size = ByteAt(offset + 1); // 0x4e8970
            }
            else if (type == Labs::TypeC2 || type == Labs::TypeC4)
            {
                size = (ByteAt(offset + 1) << BitsPerByte) | ByteAt(offset + 2); // 0x4e895f y 0x4e8962
            }
            else
            {
                count = 0; // 0x4e89ca: cabecera inválida, descarta todo
                return;
            }
            if (size == 0)
            {
                return; // 0x4e897b, jle: sale sin correr lo que quedaba al principio
            }
            if (size > count)
            {
                // 0x4e89dd: el resto parcial se corre al principio, si no estaba ahí ya (el original
                // también mira que quede al menos un byte, que acá siempre queda).
                if (offset > 0)
                {
                    std::memmove(buffer.data(), buffer.data() + offset, count);
                }
                return;
            }
            const auto begin = buffer.begin() + static_cast<std::ptrdiff_t>(offset);
            queue.emplace_back(begin, begin + static_cast<std::ptrdiff_t>(size));
            count -= size;
            offset += size;
            if (count == 0)
            {
                return;
            }
        }
    }

    [[nodiscard]] std::size_t ByteAt(std::size_t index) const
    {
        return std::to_integer<std::size_t>(buffer[index]);
    }

    // El buffer de 0x2000 y 2 bytes más. Un paquete empieza como mucho en 0x1FFF, y el corte lee
    // hasta offset + 2. En el original esos dos bytes son el contador de +0x4014, que está justo
    // después del buffer; acá quedan en 0.
    std::array<std::byte, ReceiveCapacity + 2> buffer{};
    std::size_t count = 0;
    std::size_t overflowLogs = 0;
    std::vector<Frame> queue;
};

// El caso 1b de red_reference.py: los dos paquetes del caso 1, partidos después de C2 00.
constexpr std::size_t SplitAfter = 2; // los dos primeros bytes del C2 llegan con el C1

// La primera lectura trae el C1 y C2 00. El tamaño del C2 se arma con el 00 y con el byte que
// había en el buffer después, que es viejo: con un buffer nuevo, 00. Da 0, y el original sale sin
// correr los dos bytes al principio. La segunda lectura se pega en la posición 2, detrás de C1 05
// (que ya se cortó), y el corte sigue desde ahí: sale C1 05 06 20 03, que nadie mandó, y el 04 que
// sobra es una cabecera inválida que descarta el buffer. El C2 de verdad no sale nunca.
TEST(OldCutTest, Case1bShiftsTheStreamWhenTheOldByteIsZero)
{
    OldCut cut;
    const Frame firstRead = Join(FirstC1, Frame(SecondC2.begin(), SecondC2.begin() + SplitAfter));
    const Frame secondRead(SecondC2.begin() + SplitAfter, SecondC2.end());

    ASSERT_EQ(cut.OnRead(firstRead), OldCut::ReadResult::Cut);
    ASSERT_EQ(cut.Queue().size(), 1u);
    EXPECT_EQ(cut.Queue()[0], FirstC1);
    EXPECT_EQ(cut.Count(), 2u);
    EXPECT_EQ(cut.Prefix(2), Bytes({0xC1, 0x05})); // no se corrió nada

    ASSERT_EQ(cut.OnRead(secondRead), OldCut::ReadResult::Cut);
    ASSERT_EQ(cut.Queue().size(), 2u);
    EXPECT_EQ(cut.Queue()[1], Bytes({0xC1, 0x05, 0x06, 0x20, 0x03}));
    EXPECT_EQ(cut.Count(), 0u);
}

// El mismo flujo en el FrameCutter: con C2 00 espera el tercer byte de la cabecera, y el C2 sale
// entero con la segunda lectura.
TEST(OldCutTest, FrameCutterWaitsForTheMissingBytes)
{
    FrameCutter cutter;
    const Frame stream = Join(FirstC1, SecondC2);
    const std::span<const std::byte> bytes(stream);
    const std::size_t firstRead = FirstC1.size() + SplitAfter;

    const std::vector<Frame> first = FeedAll(cutter, bytes.first(firstRead));
    ASSERT_EQ(first.size(), 1u);
    EXPECT_EQ(first[0], FirstC1);
    EXPECT_EQ(cutter.Pending(), SplitAfter);

    const std::vector<Frame> second = FeedAll(cutter, bytes.subspan(firstRead));
    ASSERT_EQ(second.size(), 1u);
    EXPECT_EQ(second[0], SecondC2);
    EXPECT_FALSE(cutter.Error().has_value());
}

// Si el byte viejo no es 0, puede ser peor. Una lectura anterior deja un 02 en la posición 7; con
// el caso 1b encima, el tamaño del C2 da 2, que entra en los 2 bytes que quedan: sale un "C2 00"
// de 2 bytes, más corto que su propia cabecera. Lo que llega después empieza con 06, una cabecera
// inválida, y se descarta.
TEST(OldCutTest, AnOldByteOfTwoCutsAPacketShorterThanItsHeader)
{
    OldCut cut;
    const Frame earlier = Bytes({0xC1, 0x08, 0x10, 0x00, 0x00, 0x00, 0x00, 0x02});
    const Frame firstRead = Join(FirstC1, Frame(SecondC2.begin(), SecondC2.begin() + SplitAfter));
    const Frame secondRead(SecondC2.begin() + SplitAfter, SecondC2.end());

    ASSERT_EQ(cut.OnRead(earlier), OldCut::ReadResult::Cut);
    ASSERT_EQ(cut.Count(), 0u);

    ASSERT_EQ(cut.OnRead(firstRead), OldCut::ReadResult::Cut);
    ASSERT_EQ(cut.Queue().size(), 3u);
    EXPECT_EQ(cut.Queue()[1], FirstC1);
    EXPECT_EQ(cut.Queue()[2], Bytes({0xC2, 0x00}));

    ASSERT_EQ(cut.OnRead(secondRead), OldCut::ReadResult::Cut);
    EXPECT_EQ(cut.Queue().size(), 3u);
    EXPECT_EQ(cut.Count(), 0u);
}

// Con tamaño 0 al principio de la lectura el original no avanza ni descarta: los bytes se acumulan
// hasta llenar los 0x2000, y desde ahí cada lectura vuelve sin leer. La conexión no se cierra, pero
// no se lee nada más. El FrameCutter, en cambio, se para en cuanto llega el tamaño
// (FrameCutterTest.StopsOnSizeZero), y quien lo usa cierra.
TEST(OldCutTest, SizeZeroFillsTheBufferUntilNothingMoreIsRead)
{
    OldCut cut;

    ASSERT_EQ(cut.OnRead(Bytes({0xC1, 0x00, 0x00})), OldCut::ReadResult::Cut);
    EXPECT_EQ(cut.Count(), 3u);
    EXPECT_TRUE(cut.Queue().empty());

    const Frame rest(ReceiveCapacity - 3, std::byte{0x00});
    ASSERT_EQ(cut.OnRead(rest), OldCut::ReadResult::Cut);
    EXPECT_EQ(cut.Count(), ReceiveCapacity);
    EXPECT_TRUE(cut.Queue().empty());

    EXPECT_EQ(cut.OverflowLogs(), 0u);
    EXPECT_EQ(cut.OnRead(Bytes({0xC1})), OldCut::ReadResult::NotRead);
    EXPECT_EQ(cut.OverflowLogs(), 1u);
    EXPECT_EQ(cut.Count(), ReceiveCapacity);
}

// Si el byte viejo da 3 o más, el tamaño no entra en los 2 bytes que quedan: el original los corre
// al principio (0x4e89dd) y la lectura siguiente los completa bien. Una lectura anterior deja un 03
// en la posición 7.
TEST(OldCutTest, AnOldByteOfThreeOrMoreWaitsAndCutsRight)
{
    OldCut cut;
    const Frame earlier = Bytes({0xC1, 0x08, 0x10, 0x00, 0x00, 0x00, 0x00, 0x03});
    const Frame firstRead = Join(FirstC1, Frame(SecondC2.begin(), SecondC2.begin() + SplitAfter));
    const Frame secondRead(SecondC2.begin() + SplitAfter, SecondC2.end());

    ASSERT_EQ(cut.OnRead(earlier), OldCut::ReadResult::Cut);
    ASSERT_EQ(cut.OnRead(firstRead), OldCut::ReadResult::Cut);
    ASSERT_EQ(cut.Queue().size(), 2u);
    EXPECT_EQ(cut.Count(), SplitAfter);
    EXPECT_EQ(cut.Prefix(SplitAfter), Bytes({0xC2, 0x00})); // se corrieron al principio

    ASSERT_EQ(cut.OnRead(secondRead), OldCut::ReadResult::Cut);
    ASSERT_EQ(cut.Queue().size(), 3u);
    EXPECT_EQ(cut.Queue()[2], SecondC2);
    EXPECT_EQ(cut.Count(), 0u);
}

// El caso 2 de red_reference.py: con menos de 3 bytes en el buffer, 0x4e88a0 ni corta (0x4e8927) y
// devuelve 3; con el que falta, corta. Una lectura vacía (recv da 0: el otro lado cerró) no lee.
TEST(OldCutTest, WaitsWithLessThanThreeBytesAndDoesNotReadAnEmptyRead)
{
    OldCut cut;

    EXPECT_EQ(cut.OnRead(Bytes({0xC1, 0x03})), OldCut::ReadResult::NeedMore);
    EXPECT_TRUE(cut.Queue().empty());
    EXPECT_EQ(cut.OnRead(Bytes({0x00})), OldCut::ReadResult::Cut);
    ASSERT_EQ(cut.Queue().size(), 1u);
    EXPECT_EQ(cut.Queue()[0], Bytes({0xC1, 0x03, 0x00}));

    EXPECT_EQ(cut.OnRead(Frame{}), OldCut::ReadResult::NotRead);
    EXPECT_EQ(cut.OverflowLogs(), 0u);
}

} // namespace
