#include "LoopbackPair.h"

#include <utility>

namespace Labs
{
std::optional<LoopbackPair> LoopbackPair::Open()
{
    LoopbackPair pair;
    pair.m_listener = LabSocket::OpenTcp();
    // El puerto 0 le pide uno libre al sistema: así dos tests en paralelo no chocan.
    if (!pair.m_listener.BindLoopback(0) || !pair.m_listener.Listen() || !pair.m_listener.SetNonBlocking())
    {
        return std::nullopt;
    }
    pair.m_port = pair.m_listener.LocalPort();

    pair.m_client = LabSocket::OpenTcp();
    if (!pair.m_client.SetNonBlocking())
    {
        return std::nullopt;
    }
    pair.m_connectStarted = pair.m_client.ConnectLoopback(pair.m_port);
    return pair;
}

const IoResult& LoopbackPair::ConnectStarted() const
{
    return m_connectStarted;
}

std::uint16_t LoopbackPair::Port() const
{
    return m_port;
}

bool LoopbackPair::Accept()
{
    if (m_server.IsOpen())
    {
        return true;
    }
    LabSocket accepted = m_listener.Accept();
    if (!accepted.IsOpen() || !accepted.SetNonBlocking())
    {
        return false;
    }
    m_server = std::move(accepted);
    return true;
}

LabSocket& LoopbackPair::Client()
{
    return m_client;
}

LabSocket& LoopbackPair::Server()
{
    return m_server;
}
} // namespace Labs
