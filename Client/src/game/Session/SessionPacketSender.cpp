#include "Session/SessionPacketSender.h"

#include "Protocol/Outgoing.h"
#include "Session/ServerSession.h"

#include <utility>

namespace Client::Session
{
SessionPacketSender::SessionPacketSender(App::ServerSession& session) : m_session(session) {}

void SessionPacketSender::Send(Protocol::Outgoing packet) const
{
    m_session.SendGamePacket(std::move(packet));
}
} // namespace Client::Session
