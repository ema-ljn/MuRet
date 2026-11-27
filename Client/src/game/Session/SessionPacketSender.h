#pragma once

#include "Kernel/IPacketSender.h"
#include "Protocol/Outgoing.h"

namespace Client::App
{
class ServerSession;
} // namespace Client::App

// Kernel::IPacketSender over the session's GameServer (App::ServerSession::SendGamePacket).
namespace Client::Session
{
class SessionPacketSender final : public Kernel::IPacketSender
{
public:
    explicit SessionPacketSender(App::ServerSession& session);
    void Send(Protocol::Outgoing packet) const override;

private:
    App::ServerSession& m_session;
};
} // namespace Client::Session
