#pragma once

#include "Protocol/Outgoing.h"

// What a module sends to the GameServer. Session implements it over its socket.
namespace Client::Kernel
{
class IPacketSender
{
public:
    virtual ~IPacketSender() = default;
    // Without a GameServer open, a warning and nothing, like every world packet.
    virtual void Send(Protocol::Outgoing packet) const = 0;
};
} // namespace Client::Kernel
