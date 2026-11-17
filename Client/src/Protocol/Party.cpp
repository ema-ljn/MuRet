#include "Protocol/Party.h"

#include "Network/PacketWriter.h"
#include "Protocol/Outgoing.h"

namespace Client::Protocol::Party
{
Outgoing WritePartyListRequest()
{
    return ClearC1(Network::PacketWriter::C1(PartyListOpcode));
}
} // namespace Client::Protocol::Party
