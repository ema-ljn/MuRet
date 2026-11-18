#include "Protocol/DialogAnswers.h"

#include "Network/PacketWriter.h"
#include "Protocol/Outgoing.h"

#include <cstdint>

namespace Client::Protocol::DialogAnswers
{
Outgoing WriteChaosCastleEntry(std::uint8_t level, std::uint8_t slot)
{
    return Xor32C1(
        Network::PacketWriter::C1(ChaosCastleOpcode).PutU8(ChaosCastleEnterSubcode).PutU8(level).PutU8(slot));
}

Outgoing WriteNoticeYes()
{
    return Xor32C1(Network::PacketWriter::C1(NoticeAnswerOpcode).PutU8(NoticeYesSubcode));
}
} // namespace Client::Protocol::DialogAnswers
