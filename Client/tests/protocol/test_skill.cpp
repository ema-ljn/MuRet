#include "Protocol/Skill.h"

#include "Network/Crypto/Xor32.h"
#include "Network/Delivery.h"
#include "Protocol/ProtocolError.h"

#include "support/Bytes.h"
#include "support/ExpectedError.h"
#include "support/Packets.h"
#include "support/SameBytes.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <utility>
#include <vector>

// Client::Protocol::Skill: lo que llega de las skills (04 § Las skills, CL9 hito 4), con claves inventadas.
namespace
{
using Client::Network::Delivery;
using Client::Network::Crypto::Xor32Decode;
using Client::Protocol::ProtocolError;
using Client::Tests::Bytes;
using Client::Tests::ErrorOf;
using Client::Tests::PacketOf;
using Client::Tests::SameBytes;
namespace Skill = Client::Protocol::Skill;

// 0x4cce2f..0x4cce55: las claves en big endian; el bit 15 del blanco es el éxito y se va del blanco.
TEST(SkillProtocolTest, TheMagicAttackSplitsTheSuccessBitFromTheTarget)
{
    const auto attack = Skill::ReadMagicAttack(PacketOf(Bytes({0xc1, 8, 0x19, 0x0d, 0x92, 0x34, 0x81, 0x23})));
    ASSERT_TRUE(attack.HasValue());
    EXPECT_EQ(attack.Value().skill, 0x0d);
    EXPECT_EQ(attack.Value().source, 0x9234);
    EXPECT_EQ(attack.Value().target, 0x0123);
    EXPECT_TRUE(attack.Value().succeeded);
    const auto failed = Skill::ReadMagicAttack(PacketOf(Bytes({0xc1, 8, 0x19, 1, 0, 2, 0x7f, 0xff})));
    ASSERT_TRUE(failed.HasValue());
    EXPECT_EQ(failed.Value().target, 0x7fff);
    EXPECT_FALSE(failed.Value().succeeded);
}

TEST(SkillProtocolTest, TheDurationMagicReadsTheCellAndTheDirection)
{
    const auto magic = Skill::ReadDurationMagic(PacketOf(Bytes({0xc1, 9, 0x1e, 0x0e, 0x12, 0x34, 0x80, 0x81, 0xc0})));
    ASSERT_TRUE(magic.HasValue());
    EXPECT_EQ(magic.Value().skill, 0x0e);
    EXPECT_EQ(magic.Value().key, 0x1234);
    EXPECT_EQ(magic.Value().x, 0x80);
    EXPECT_EQ(magic.Value().y, 0x81);
    EXPECT_EQ(magic.Value().direction, 0xc0);
}

TEST(SkillProtocolTest, TheMagicCancelReadsTheSkillAndTheKey)
{
    const auto cancel = Skill::ReadMagicCancel(PacketOf(Bytes({0xc1, 6, 0x1b, 0xc9, 0x81, 0x02})));
    ASSERT_TRUE(cancel.HasValue());
    EXPECT_EQ(cancel.Value().skill, 0xc9);
    EXPECT_EQ(cancel.Value().key, 0x8102);
}

// 0x4cbe22: la máscara es el DWORD de pkt[4] en little endian; la clave, big endian desde pkt[8].
TEST(SkillProtocolTest, TheActorStateReadsALittleEndianMask)
{
    const auto state = Skill::ReadActorState(PacketOf(Bytes({0xc1, 10, 0x07, 1, 0x00, 0x01, 0x00, 0x10, 0x12, 0x34})));
    ASSERT_TRUE(state.HasValue());
    EXPECT_EQ(state.Value().type, 1);
    EXPECT_EQ(state.Value().mask, 0x10000100u);
    EXPECT_EQ(state.Value().key, 0x1234);
}

// 0x4dbf59..0x4dbf61: la clave en pkt[3..4], AH y AL.
TEST(SkillProtocolTest, TheActorEffectReadsTheKeyFirst)
{
    const auto effect = Skill::ReadActorEffect(PacketOf(Bytes({0xc1, 6, 0x48, 0x12, 0x34, 0x10})));
    ASSERT_TRUE(effect.HasValue());
    EXPECT_EQ(effect.Value().key, 0x1234);
    EXPECT_EQ(effect.Value().kind, 0x10);
}

// 0x4cc29d y 0x4cc2a3: los WORD en little endian, sin la máscara.
TEST(SkillProtocolTest, TheSkillTargetKeepsLittleEndianKeysWithTheirHighBit)
{
    const auto target = Skill::ReadSkillTarget(PacketOf(Bytes({0xc1, 8, 0x69, 3, 0x34, 0x12, 0x02, 0x81})));
    ASSERT_TRUE(target.HasValue());
    EXPECT_EQ(target.Value().skill, 3);
    EXPECT_EQ(target.Value().source, 0x1234);
    EXPECT_EQ(target.Value().target, 0x8102);
}

// 0x4c5070: las cuatro formas del F3 11.
TEST(SkillProtocolTest, TheSkillListReadsItsFourShapes)
{
    const auto remove = Skill::ReadSkillList(PacketOf(Bytes({0xc1, 7, 0xf3, 0x11, 0xff, 0, 5})));
    ASSERT_TRUE(remove.HasValue());
    EXPECT_EQ(remove.Value().count, Skill::SkillListRemoveOne);
    EXPECT_EQ(remove.Value().slot, 5);
    EXPECT_TRUE(remove.Value().entries.empty());

    const auto add = Skill::ReadSkillList(PacketOf(Bytes({0xc1, 8, 0xf3, 0x11, 0xfe, 0, 6, 0x11})));
    ASSERT_TRUE(add.HasValue());
    EXPECT_EQ(add.Value().slot, 6);
    EXPECT_EQ(add.Value().skill, 0x11);

    const auto clear = Skill::ReadSkillList(PacketOf(Bytes({0xc1, 7, 0xf3, 0x11, 3, 2, 9})));
    ASSERT_TRUE(clear.HasValue());
    EXPECT_EQ(clear.Value().list, Skill::SkillListClearSlot);
    EXPECT_EQ(clear.Value().slot, 9);
    EXPECT_TRUE(clear.Value().entries.empty());

    // De a tres bytes desde pkt[6]: el lugar, la skill y uno que el cliente no lee.
    const auto whole =
        Skill::ReadSkillList(PacketOf(Bytes({0xc1, 12, 0xf3, 0x11, 2, 0, 0, 0x11, 0xaa, 1, 0x29, 0xbb})));
    ASSERT_TRUE(whole.HasValue());
    ASSERT_EQ(whole.Value().entries.size(), 2u);
    EXPECT_EQ(whole.Value().entries[0].slot, 0);
    EXPECT_EQ(whole.Value().entries[0].skill, 0x11);
    EXPECT_EQ(whole.Value().entries[1].slot, 1);
    EXPECT_EQ(whole.Value().entries[1].skill, 0x29);

    const auto none = Skill::ReadSkillList(PacketOf(Bytes({0xc1, 6, 0xf3, 0x11, 0, 0})));
    ASSERT_TRUE(none.HasValue());
    EXPECT_TRUE(none.Value().entries.empty());
}

TEST(SkillProtocolTest, TheSkillListWithMissingEntriesIsTooShort)
{
    EXPECT_EQ(ErrorOf(Skill::ReadSkillList(PacketOf(Bytes({0xc1, 10, 0xf3, 0x11, 2, 0, 0, 0x11, 0xaa, 1})))),
              ProtocolError::TooShort);
    EXPECT_EQ(ErrorOf(Skill::ReadSkillList(PacketOf(Bytes({0xc1, 7, 0xf3, 0x11, 0xfe, 0, 6})))),
              ProtocolError::TooShort);
    EXPECT_EQ(ErrorOf(Skill::ReadSkillList(PacketOf(Bytes({0xc1, 6, 0xf3, 0x11, 0xff, 0})))), ProtocolError::TooShort);
    EXPECT_EQ(ErrorOf(Skill::ReadSkillList(PacketOf(Bytes({0xc1, 5, 0xf3, 0x11, 1})))), ProtocolError::TooShort);
}

TEST(SkillProtocolTest, TheSummonLifeIsOneByte)
{
    const auto life = Skill::ReadSummonLife(PacketOf(Bytes({0xc1, 5, 0xf3, 0x20, 0x64})));
    ASSERT_TRUE(life.HasValue());
    EXPECT_EQ(life.Value().life, 0x64);
    EXPECT_EQ(ErrorOf(Skill::ReadSummonLife(PacketOf(Bytes({0xc1, 4, 0xf3, 0x20})))), ProtocolError::TooShort);
    EXPECT_EQ(ErrorOf(Skill::ReadSummonLife(PacketOf(Bytes({0xc1, 5, 0xf3, 0x11, 0x64})))), ProtocolError::WrongPacket);
}

TEST(SkillProtocolTest, EveryTruncatedPayloadIsTooShort)
{
    const std::vector<std::vector<std::byte>> fulls{
        Bytes({0xc1, 8, 0x19, 1, 2, 3, 4, 5}), Bytes({0xc1, 9, 0x1e, 1, 2, 3, 4, 5, 6}),
        Bytes({0xc1, 6, 0x1b, 1, 2, 3}),       Bytes({0xc1, 10, 0x07, 1, 2, 3, 4, 5, 6, 7}),
        Bytes({0xc1, 6, 0x48, 1, 2, 3}),       Bytes({0xc1, 8, 0x69, 1, 2, 3, 4, 5}),
    };
    for (const auto& full : fulls)
    {
        for (std::size_t size = 3; size < full.size(); ++size)
        {
            std::vector<std::byte> cut(full.begin(), full.begin() + static_cast<std::ptrdiff_t>(size));
            cut[1] = static_cast<std::byte>(size);
            const auto packet = PacketOf(std::move(cut));
            const auto opcode = std::to_integer<int>(full[2]);
            const auto error = [&]
            {
                switch (opcode)
                {
                case 0x19:
                    return ErrorOf(Skill::ReadMagicAttack(packet));
                case 0x1e:
                    return ErrorOf(Skill::ReadDurationMagic(packet));
                case 0x1b:
                    return ErrorOf(Skill::ReadMagicCancel(packet));
                case 0x07:
                    return ErrorOf(Skill::ReadActorState(packet));
                case 0x48:
                    return ErrorOf(Skill::ReadActorEffect(packet));
                default:
                    return ErrorOf(Skill::ReadSkillTarget(packet));
                }
            }();
            EXPECT_EQ(error, ProtocolError::TooShort) << opcode << " " << size;
        }
    }
}

TEST(SkillProtocolTest, TheReadersRejectOtherPacketKinds)
{
    EXPECT_EQ(ErrorOf(Skill::ReadMagicAttack(PacketOf(Bytes({0xc1, 8, 0x1e, 1, 2, 3, 4, 5})))),
              ProtocolError::WrongPacket);
    EXPECT_EQ(ErrorOf(Skill::ReadDurationMagic(PacketOf(Bytes({0xc2, 0, 9, 0x1e, 1, 2, 3, 4, 5})))),
              ProtocolError::WrongPacket);
    EXPECT_EQ(ErrorOf(Skill::ReadSkillTarget(PacketOf(Bytes({0xc1, 8, 0x19, 1, 2, 3, 4, 5})))),
              ProtocolError::WrongPacket);
    EXPECT_TRUE(Skill::ReadMagicCancel(PacketOf(Bytes({0xc1, 7, 0x1b, 1, 2, 3, 0xaa}))).HasValue());
}

// 0x4cc562..0x4cce21 y 0x4d5d8c: el aviso de los que no llegaron cifrados, C1 06 F1 03 00 [cabeza], para cifrar.
TEST(SkillProtocolTest, TheClearReportIsF103WithTheHead)
{
    for (const auto head : {Skill::MagicAttackOpcode, Skill::DurationMagicOpcode, Skill::Report73Head})
    {
        const auto report = Skill::WriteClearSkillReport(head);
        EXPECT_EQ(report.delivery, Delivery::Encrypted);
        EXPECT_TRUE(SameBytes(Xor32Decode(report.packet).Bytes(), Bytes({0xc1, 0x06, 0xf1, 0x03, 0x00, head})));
    }
}

// 0x55bf36..0x55c622 (0x55b6f0): el 19 del cliente, C1 06 19 [skill] [clave H] [clave L], para cifrar; la clave entera,
// con su bit 15.
TEST(SkillProtocolTest, TheMagicAttackGoesOutWithTheBigEndianKey)
{
    const auto attack = Skill::WriteMagicAttack(0x3c, 0x8a1b);
    EXPECT_EQ(attack.delivery, Delivery::Encrypted);
    EXPECT_TRUE(SameBytes(Xor32Decode(attack.packet).Bytes(), Bytes({0xc1, 0x06, 0x19, 0x3c, 0x8a, 0x1b})));
}

// 0x562af5..0x563b78 (0x561f60): el 1E del elfo, C1 0C 1E [skill] [X] [Y] [rumbo] [hacia] [espalda] [clave H] [clave L]
// 00, para cifrar.
TEST(SkillProtocolTest, TheDurationMagicGoesOutWithTwelveBytes)
{
    const auto shot = Skill::WriteDurationMagic(
        {.skill = 0x36, .x = 0xb6, .y = 0x73, .direction = 0x5f, .towards = 0xaa, .back = 0xdf, .key = 0x8a1b});
    EXPECT_EQ(shot.delivery, Delivery::Encrypted);
    EXPECT_TRUE(SameBytes(Xor32Decode(shot.packet).Bytes(),
                          Bytes({0xc1, 0x0c, 0x1e, 0x36, 0xb6, 0x73, 0x5f, 0xaa, 0xdf, 0x8a, 0x1b, 0x00})));
}
} // namespace
