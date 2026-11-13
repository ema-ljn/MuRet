#include "Protocol/Movement.h"

#include "Network/Crypto/Xor32.h"
#include "Network/Delivery.h"
#include "Network/FoxAntiObfuscation.h"
#include "Protocol/ProtocolError.h"

#include "support/Bytes.h"
#include "support/ExpectedError.h"
#include "support/Packets.h"
#include "support/SameBytes.h"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <utility>
#include <vector>

namespace
{
using Client::Network::Crypto::Xor32Decode;
using Client::Protocol::ProtocolError;
using Client::Protocol::Movement::Cell;
using Client::Protocol::Movement::FacingAction;
using Client::Protocol::Movement::ReadAction;
using Client::Protocol::Movement::ReadDamage;
using Client::Protocol::Movement::ReadWalk;
using Client::Protocol::Movement::SittingAction;
using Client::Protocol::Movement::WalkUpdateTimeMax;
using Client::Protocol::Movement::WalkWriteError;
using Client::Protocol::Movement::WriteAction;
using Client::Protocol::Movement::WriteWalk;
using Client::Tests::Bytes;
using Client::Tests::ErrorOf;
using Client::Tests::PacketOf;
using Client::Tests::SameBytes;

// 04 § Caminar y pegar: bytes nativos del cable. El blanco lejano es una entrada
// explícita del test: no se atribuye al cable, que no lleva el blanco del clic.
TEST(MovementTest, TheDocumentedWireBytesMatchForAnExplicitFarTarget)
{
    constexpr std::array<Cell, 4> Path{{{0xb6, 0x73}, {0xb5, 0x73}, {0xb4, 0x74}, {0xb3, 0x74}}};
    auto sent = WriteWalk(Path, {0, 0}, 0.0f);
    ASSERT_TRUE(sent.HasValue());
    EXPECT_EQ(sent.Value().delivery, Client::Network::Delivery::Clear);
    EXPECT_TRUE(
        SameBytes(Xor32Decode(sent.Value().packet).Bytes(), Bytes({0xc1, 0x08, 0xd3, 0xb6, 0x73, 0x73, 0x76, 0x70})));
    EXPECT_TRUE(SameBytes(sent.Value().packet.Bytes(), Bytes({0xc1, 0x08, 0xd3, 0xec, 0x23, 0xe2, 0x0b, 0x08})));
    Client::Network::FoxAntiObfuscation foxAnti;
    const auto wire = foxAnti.BeforeSend(std::move(sent.Value().packet));
    EXPECT_TRUE(SameBytes(wire.Bytes(), Bytes({0xc1, 0x08, 0xa3, 0x9c, 0x53, 0x92, 0x7b, 0x78})));
}

// Expectativas estáticas: tabla 0x6c82a4 y empaquetado 0x56b006..0x56b0a4.
TEST(MovementTest, AllEightStepsUseTheHighNibbleFirst)
{
    constexpr std::array<Cell, 9> Path{
        {{10, 10}, {9, 9}, {9, 8}, {10, 7}, {11, 7}, {12, 8}, {12, 9}, {11, 10}, {10, 10}}};
    const auto sent = WriteWalk(Path, {200, 200}, 0.0f);
    ASSERT_TRUE(sent.HasValue());
    EXPECT_TRUE(SameBytes(Xor32Decode(sent.Value().packet).Bytes(),
                          Bytes({0xc1, 0x0a, 0xd3, 10, 10, 0x78, 0x01, 0x23, 0x45, 0x67})));
}

// Expectativas estáticas: resta de pasos y búsqueda en 0x56b017..0x56b04b.
TEST(MovementTest, AnUnrecognizedDeltaBecomesZeroWithoutByteWrapping)
{
    for (const auto& path : {std::array<Cell, 2>{{{1, 1}, {3, 1}}}, std::array<Cell, 2>{{{255, 0}, {0, 0}}}})
    {
        const auto sent = WriteWalk(path, {100, 100}, 0.0f);
        ASSERT_TRUE(sent.HasValue());
        const auto plain = Xor32Decode(sent.Value().packet);
        EXPECT_EQ(plain.Bytes()[5], std::byte{0x01});
        EXPECT_EQ(plain.Bytes()[6], std::byte{0x00});
    }
}

// Expectativa estática: blanco explícito y fallback, 0x56b0ed..0x56b148.
TEST(MovementTest, AnExplicitNeighborTargetOverridesTheLastStep)
{
    constexpr std::array<Cell, 2> Path{{{10, 10}, {11, 10}}};
    const auto sent = WriteWalk(Path, {11, 9}, 0.0f);
    ASSERT_TRUE(sent.HasValue());
    EXPECT_TRUE(SameBytes(Xor32Decode(sent.Value().packet).Bytes(), Bytes({0xc1, 7, 0xd3, 10, 10, 0x11, 0x30})));
}

// Expectativa estática: no hay delta (0,0) en la tabla, 0x56b0ed..0x56b148.
TEST(MovementTest, ATargetAtTheEndKeepsTheLastStepDirection)
{
    constexpr std::array<Cell, 2> Path{{{10, 10}, {11, 10}}};
    const auto sent = WriteWalk(Path, {11, 10}, 0.0f);
    ASSERT_TRUE(sent.HasValue());
    EXPECT_EQ(Xor32Decode(sent.Value().packet).Bytes()[5], std::byte{0x31});
}

// Expectativas estáticas: límite 0x56ad8d..0x56ad96, P0 0x56b145..0x56b154;
// TimeMax conserva la cantidad local (0x56ad1d..0x56ad4f).
TEST(MovementTest, FifteenLocalCellsAreSentAsFourteen)
{
    std::array<Cell, 15> path{};
    for (std::size_t index = 0; index < path.size(); ++index)
    {
        path[index] = {static_cast<std::uint8_t>(100 + index), 100};
    }
    const auto sent = WriteWalk(path, {200, 200}, 0.0f);
    ASSERT_TRUE(sent.HasValue());
    EXPECT_TRUE(SameBytes(Xor32Decode(sent.Value().packet).Bytes(),
                          Bytes({0xc1, 0x0d, 0xd3, 100, 100, 0x3d, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x30})));
    EXPECT_EQ(path.back().x, 114);
    const auto wait = WalkUpdateTimeMax(path.size());
    ASSERT_TRUE(wait.HasValue());
    EXPECT_EQ(wait.Value(), 49u);
}

std::array<Cell, 15> PathWithAnUnsentTurn()
{
    std::array<Cell, 15> path{};
    for (std::size_t index = 0; index < path.size(); ++index)
    {
        path[index] = {static_cast<std::uint8_t>(100 + index), 100};
    }
    path.back() = {113, 101};
    return path;
}

// Expectativa estática: el límite de 0x56ad8d..0x56ad96 ya se aplicó cuando
// 0x56b0f6/0x56b114 leen la última celda para la resta del blanco.
TEST(MovementTest, TheTargetIsRelativeToTheLastTransmittedCell)
{
    const auto path = PathWithAnUnsentTurn();
    const auto sent = WriteWalk(path, {112, 101}, 0.0f);
    ASSERT_TRUE(sent.HasValue());
    EXPECT_EQ(Xor32Decode(sent.Value().packet).Bytes()[5], std::byte{0x6d});
}

// Expectativa estática: BL conserva la dirección del último paso empaquetado,
// 0x56b006..0x56b0a4 y fallback de 0x56b13d..0x56b148.
TEST(MovementTest, AFarTargetUsesTheLastTransmittedStepAndNotTheUnsentTurn)
{
    const auto path = PathWithAnUnsentTurn();
    const auto sent = WriteWalk(path, {200, 200}, 0.0f);
    ASSERT_TRUE(sent.HasValue());
    EXPECT_EQ(Xor32Decode(sent.Value().packet).Bytes()[5], std::byte{0x3d});
}

// Expectativa estática: SUB de registros de 32 bits tras extender bytes con cero,
// 0x56b0f6..0x56b103 y 0x56b112..0x56b121; no se envuelve la resta del blanco.
TEST(MovementTest, TheTargetDeltaDoesNotWrapAtTheByteBoundary)
{
    constexpr std::array<Cell, 2> Path{{{254, 1}, {255, 1}}};
    const auto sent = WriteWalk(Path, {0, 0}, 0.0f);
    ASSERT_TRUE(sent.HasValue());
    EXPECT_TRUE(SameBytes(Xor32Decode(sent.Value().packet).Bytes(), Bytes({0xc1, 7, 0xd3, 254, 1, 0x31, 0x30})));
}

// Expectativas estáticas: cantidad de bytes en 0x56b15a..0x56b166.
// El largo NO distingue 14 de 15: ambos dan 13. El límite lo prueba P0 arriba.
TEST(MovementTest, ThePacketLengthMatchesTheNibblePayloadSize)
{
    constexpr std::array<std::size_t, 15> Expected{6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
    std::array<Cell, 15> path{};
    for (std::size_t count = 1; count <= path.size(); ++count)
    {
        const auto sent = WriteWalk(std::span(path).first(count), {200, 200}, 0.0f);
        ASSERT_TRUE(sent.HasValue());
        EXPECT_EQ(sent.Value().packet.Bytes().size(), Expected[count - 1]) << count;
    }
}

// Cuenta estática x87: 0x56b0b7..0x56b0cc, sin guardados float entre operaciones;
// el truncamiento lo fuerza 0x69fd4b antes del FISTP m64int de 0x69fd55.
TEST(MovementTest, OneCellTakesTheDirectionFromTheAngleAndAllowsMoreThanOneTurn)
{
    constexpr std::array<Cell, 1> Path{{{10, 20}}};
    struct Case
    {
        float angle;
        std::uint8_t direction;
    };
    constexpr std::array<Case, 9> Cases{
        {{0, 1}, {90, 3}, {180, 5}, {270, 7}, {-45, 0}, {-90, 0}, {-180, 6}, {360, 1}, {720, 1}}};
    for (const auto& test : Cases)
    {
        const auto sent = WriteWalk(Path, {99, 99}, test.angle);
        ASSERT_TRUE(sent.HasValue());
        EXPECT_TRUE(
            SameBytes(Xor32Decode(sent.Value().packet).Bytes(), Bytes({0xc1, 6, 0xd3, 10, 20, test.direction << 4})))
            << test.angle;
    }
}

// Expectativas estáticas del contrato numérico: constantes y orden de
// 0x56b0b7..0x56b0cc. No son una medición nativa de la precisión x87.
TEST(MovementTest, AngleBoundariesKeepTheFloatConstantAndTheUnstoredDoubleSum)
{
    constexpr std::array<Cell, 1> Path{{{10, 20}}};
    for (const auto& test :
         {std::pair{std::nextafter(22.5f, 0.0f), std::byte{0x10}}, std::pair{22.5f, std::byte{0x20}},
          std::pair{std::nextafter(-22.5f, -100.0f), std::byte{0x00}}, std::pair{-22.5f, std::byte{0x10}}})
    {
        const auto sent = WriteWalk(Path, {}, test.first);
        ASSERT_TRUE(sent.HasValue());
        EXPECT_EQ(Xor32Decode(sent.Value().packet).Bytes()[5], test.second);
    }
}

// Expectativas estáticas del contrato double de MuRet; constantes/orden: 0x56b0b7..0x56b0cc.
// El ancho int64 y truncamiento original sí los fijan 0x69fd4b/0x69fd55 (FISTP m64int).
// No se presenta la precisión double como una medición de la CW nativa.
// La tasa float es 11930465 / 2^29. Con ±2^40, la cuenta supera int32 pero
// entra en int64; sus bits bajos tras truncar son 1 y 2. No se normaliza el ángulo.
TEST(MovementTest, TheAngleConversionKeepsTheFullInt64Domain)
{
    constexpr std::array<Cell, 1> Path{{{10, 20}}};
    for (const auto& test : {std::pair{0x1p40f, std::byte{0x10}}, std::pair{-0x1p40f, std::byte{0x20}},
                             std::pair{0x1p68f, std::byte{0x00}}, std::pair{-0x1p68f, std::byte{0x00}}})
    {
        const auto sent = WriteWalk(Path, {}, test.first);
        ASSERT_TRUE(sent.HasValue());
        EXPECT_EQ(Xor32Decode(sent.Value().packet).Bytes()[5], test.second);
    }
    EXPECT_EQ(ErrorOf(WriteWalk(Path, {}, 0x1p69f)), WalkWriteError::InvalidAngle);
    EXPECT_EQ(ErrorOf(WriteWalk(Path, {}, -0x1p69f)), WalkWriteError::InvalidAngle);
}

TEST(MovementTest, OnlyTheOneCellBranchValidatesTheAngle)
{
    constexpr std::array<Cell, 1> Single{{{10, 10}}};
    constexpr std::array<Cell, 2> Multiple{{{10, 10}, {11, 10}}};
    for (const float angle : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
                              -std::numeric_limits<float>::infinity(), std::numeric_limits<float>::max(),
                              -std::numeric_limits<float>::max()})
    {
        EXPECT_EQ(ErrorOf(WriteWalk(Single, {}, angle)), WalkWriteError::InvalidAngle);
        EXPECT_TRUE(WriteWalk(Multiple, {}, angle).HasValue());
    }
}

TEST(MovementTest, InvalidLocalCellCountsAreExplicitErrors)
{
    constexpr std::array<Cell, 16> Path{};
    EXPECT_EQ(ErrorOf(WriteWalk(std::span(Path).first(0), {}, 0.0f)), WalkWriteError::InvalidPathSize);
    EXPECT_EQ(ErrorOf(WriteWalk(Path, {}, 0.0f)), WalkWriteError::InvalidPathSize);
    for (const std::size_t count : {std::size_t{0}, std::size_t{16}, std::numeric_limits<std::size_t>::max()})
    {
        EXPECT_EQ(ErrorOf(WalkUpdateTimeMax(count)), WalkWriteError::InvalidPathSize);
    }
}

// Expectativas estáticas: las tres ramas de 0x56ad1d..0x56ad4f.
TEST(MovementTest, TheUpdateTimeUsesTheOriginalLocalCount)
{
    constexpr std::array<std::uint32_t, 15> Expected{0, 0, 5, 16, 19, 22, 25, 28, 31, 34, 37, 40, 43, 46, 49};
    for (std::size_t index = 0; index < Expected.size(); ++index)
    {
        const auto wait = WalkUpdateTimeMax(index + 1);
        ASSERT_TRUE(wait.HasValue());
        EXPECT_EQ(wait.Value(), Expected[index]);
    }
}

// 04 § Caminar y pegar; 0x4c6798..0x4c67a9 no enmascara el ID BE y 0x4c67dc hace SHR4.
TEST(MovementTest, ReadsWalkWithABigEndianIdAndTheWholeDirectionNibble)
{
    const auto walk = ReadWalk(PacketOf(Bytes({0xc1, 8, 0xd3, 0x92, 0x34, 0xfe, 0xab, 0x6b})));
    ASSERT_TRUE(walk.HasValue());
    EXPECT_EQ(walk.Value().characterId, 0x9234);
    EXPECT_EQ(walk.Value().cell.x, 0xfe);
    EXPECT_EQ(walk.Value().cell.y, 0xab);
    EXPECT_EQ(walk.Value().direction, 6);
    const auto high = ReadWalk(PacketOf(Bytes({0xc1, 8, 0xd3, 0, 1, 2, 3, 0xf2})));
    ASSERT_TRUE(high.HasValue());
    EXPECT_EQ(high.Value().direction, 15);
}

TEST(MovementTest, ReadsDamageWithTheIdMaskAndBothFlags)
{
    const auto damage = ReadDamage(PacketOf(Bytes({0xc1, 8, 0xd7, 0xff, 0xff, 0xff, 0xfe, 0xff})));
    ASSERT_TRUE(damage.HasValue());
    EXPECT_EQ(damage.Value().characterId, 0x7fff);
    EXPECT_TRUE(damage.Value().shock);
    EXPECT_EQ(damage.Value().amount, 0xfffe);
    EXPECT_EQ(damage.Value().type, 0x3f);
    EXPECT_TRUE(damage.Value().flag);
    const auto zero = ReadDamage(PacketOf(Bytes({0xc1, 8, 0xd7, 0, 0, 0, 0, 0x40})));
    ASSERT_TRUE(zero.HasValue());
    EXPECT_EQ(zero.Value().characterId, 0);
    EXPECT_EQ(zero.Value().amount, 0);
    EXPECT_EQ(zero.Value().type, 0);
    EXPECT_FALSE(zero.Value().shock);
    EXPECT_FALSE(zero.Value().flag);
}

// Formato verificado en cable, 04 § Caminar y pegar: IDs sintéticos asimétricos.
TEST(MovementTest, DamageIdsAreBigEndianWithShockOnlyInBitFifteen)
{
    const auto high = ReadDamage(PacketOf(Bytes({0xc1, 8, 0xd7, 0x92, 0x34, 0x24, 0x68, 0})));
    ASSERT_TRUE(high.HasValue());
    EXPECT_EQ(high.Value().characterId, 0x1234);
    EXPECT_TRUE(high.Value().shock);
    EXPECT_EQ(high.Value().amount, 0x2468);
    const auto low = ReadDamage(PacketOf(Bytes({0xc1, 8, 0xd7, 0x12, 0xb4, 0x13, 0x57, 0})));
    ASSERT_TRUE(low.HasValue());
    EXPECT_EQ(low.Value().characterId, 0x12b4);
    EXPECT_FALSE(low.Value().shock);
    EXPECT_EQ(low.Value().amount, 0x1357);
}

// Formato de 04 § Caminar y pegar: bits 0..5 del tipo y bit7 de la bandera son independientes.
TEST(MovementTest, TheDamageTypeBitsDoNotChooseTheIndependentHighFlag)
{
    struct Case
    {
        int byte;
        std::uint8_t type;
        bool flag;
    };
    constexpr std::array<Case, 5> Cases{
        {{0x80, 0, true}, {0x01, 1, false}, {0x40, 0, false}, {0x3f, 0x3f, false}, {0xbf, 0x3f, true}}};
    for (const auto& test : Cases)
    {
        const auto damage = ReadDamage(PacketOf(Bytes({0xc1, 8, 0xd7, 0, 1, 0, 2, test.byte})));
        ASSERT_TRUE(damage.HasValue());
        EXPECT_EQ(damage.Value().type, test.type);
        EXPECT_EQ(damage.Value().flag, test.flag);
    }
}

// Expectativa estática: la rama PathNum=1 usa solo angle en 0x56b0b2..0x56b0eb.
TEST(MovementTest, AOneCellPathIgnoresAnExplicitNeighborTarget)
{
    constexpr std::array<Cell, 1> Path{{{10, 20}}};
    const auto sent = WriteWalk(Path, {10, 19}, 90.0f);
    ASSERT_TRUE(sent.HasValue());
    EXPECT_EQ(Xor32Decode(sent.Value().packet).Bytes()[5], std::byte{0x30});
}

TEST(MovementTest, EveryTruncatedPayloadIsTooShort)
{
    for (const int opcode : {0xd3, 0xd7})
    {
        const auto full = Bytes({0xc1, 8, opcode, 0x12, 0x34, 0x56, 0x78, 0x90});
        for (std::size_t size = 3; size < full.size(); ++size)
        {
            std::vector<std::byte> cut(full.begin(), full.begin() + static_cast<std::ptrdiff_t>(size));
            cut[1] = static_cast<std::byte>(size);
            const auto packet = PacketOf(std::move(cut));
            if (opcode == 0xd3)
                EXPECT_EQ(ErrorOf(ReadWalk(packet)), ProtocolError::TooShort);
            else
                EXPECT_EQ(ErrorOf(ReadDamage(packet)), ProtocolError::TooShort);
        }
    }
}

TEST(MovementTest, TheReadersRejectOtherPacketKindsAndIgnoreExtraFields)
{
    EXPECT_EQ(ErrorOf(ReadWalk(PacketOf(Bytes({0xc1, 3, 0xd7})))), ProtocolError::WrongPacket);
    EXPECT_EQ(ErrorOf(ReadDamage(PacketOf(Bytes({0xc1, 3, 0xd3})))), ProtocolError::WrongPacket);
    EXPECT_EQ(ErrorOf(ReadWalk(PacketOf(Bytes({0xc2, 0, 8, 0xd3, 0, 0, 0, 0})))), ProtocolError::WrongPacket);
    EXPECT_EQ(ErrorOf(ReadDamage(PacketOf(Bytes({0xc3, 3, 0xd7})))), ProtocolError::WrongPacket);
    EXPECT_TRUE(ReadWalk(PacketOf(Bytes({0xc1, 9, 0xd3, 0, 1, 2, 3, 0x40, 0xaa}))).HasValue());
    EXPECT_TRUE(ReadDamage(PacketOf(Bytes({0xc1, 9, 0xd7, 0, 1, 2, 3, 0x40, 0xaa}))).HasValue());
}
// 0x4cab40 y 0x4cb61c: el bit 6 del wire conserva un segundo número de daño.
TEST(MovementTest, DamagePreservesTheSecondNumberBitIndependentlyOfTheHighFlag)
{
    for (const int value : {0, 0x40, 0x80, 0xc0, 0xff})
    {
        const auto damage = ReadDamage(PacketOf(Bytes({0xc1, 8, 0xd7, 0, 1, 0, 2, value})));
        ASSERT_TRUE(damage.HasValue());
        EXPECT_EQ(damage.Value().doubleHit, (value & 0x40) != 0);
        EXPECT_EQ(damage.Value().flag, (value & 0x80) != 0);
        EXPECT_EQ(damage.Value().type, value & 0x3f);
    }
}

// 0x59b456, 0x576a60, los casos de 0x576eb0 y 0x4f2416: el C1 05 18 con la dirección y la acción tal cual, XOR32.
TEST(MovementTest, TheActionKeepsTheRawDirectionAndAction)
{
    for (const std::uint8_t direction : {std::uint8_t{0}, std::uint8_t{7}, std::uint8_t{0xff}})
    {
        for (const std::uint8_t action : {FacingAction, SittingAction, std::uint8_t{0x83}, std::uint8_t{0}})
        {
            const auto sent = WriteAction(direction, action);
            EXPECT_EQ(sent.delivery, Client::Network::Delivery::Clear);
            EXPECT_TRUE(SameBytes(Xor32Decode(sent.packet).Bytes(), Bytes({0xc1, 5, 0x18, direction, action})));
        }
    }
}

// 0x4cb75d..0x4cb7d0: la clave en big endian sin máscara (pkt[3] · 256 + pkt[4]), la dirección y la acción.
TEST(MovementTest, ReadsTheActionOfAnotherWithABigEndianKey)
{
    const auto action = ReadAction(PacketOf(Bytes({0xc1, 7, 0x18, 0x81, 0x23, 5, 0x7a})));
    ASSERT_TRUE(action.HasValue());
    EXPECT_EQ(action.Value().key, 0x8123);
    EXPECT_EQ(action.Value().direction, 5);
    EXPECT_EQ(action.Value().action, 0x7a);
    EXPECT_TRUE(ReadAction(PacketOf(Bytes({0xc1, 8, 0x18, 0, 1, 2, 3, 0xaa}))).HasValue());
}

TEST(MovementTest, TheActionReaderRejectsShortAndOtherPackets)
{
    const auto full = Bytes({0xc1, 7, 0x18, 0, 1, 2, 3});
    for (std::size_t size = 3; size < full.size(); ++size)
    {
        std::vector<std::byte> cut(full.begin(), full.begin() + static_cast<std::ptrdiff_t>(size));
        cut[1] = static_cast<std::byte>(size);
        EXPECT_EQ(ErrorOf(ReadAction(PacketOf(std::move(cut)))), ProtocolError::TooShort);
    }
    EXPECT_EQ(ErrorOf(ReadAction(PacketOf(Bytes({0xc1, 7, 0xd3, 0, 1, 2, 3})))), ProtocolError::WrongPacket);
    EXPECT_EQ(ErrorOf(ReadAction(PacketOf(Bytes({0xc2, 0, 8, 0x18, 0, 1, 2, 3})))), ProtocolError::WrongPacket);
}
} // namespace
