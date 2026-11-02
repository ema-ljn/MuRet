#include "App/ServerSession.h"

#include "Network/LinkState.h"
#include "Protocol/Connection/Opcodes.h"
#include "Protocol/Connection/ServerList.h"
#include "Protocol/Outgoing.h"
#include "Protocol/Pet/PetInfo.h"

#include <cassert>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace Client::App
{
namespace
{
// Lo que dice el aviso de un paquete que el estado no espera, o que no se puede leer.
constexpr std::string_view OutOfTurn = "llegó fuera de turno";
constexpr std::string_view Unreadable = "no se puede leer";
} // namespace

ServerSession::ServerSession(Network::Connection& connection, Core::Log& log, SessionSettings settings)
    : m_connection(connection), m_log(log), m_settings(std::move(settings)), m_connectServer(log), m_gameServer(log)
{
    RegisterConnectServerHandlers();
}

void ServerSession::RegisterConnectServerHandlers()
{
    using Protocol::Connection::ConnectHelloOpcode;
    using Protocol::Connection::ServerListOpcode;
    using Protocol::Connection::ServerListSubcode;
    using Protocol::Connection::ServerSelectSubcode;
    // Un Dispatcher recién hecho no tiene nada: registrar no puede fallar.
    [[maybe_unused]] const bool registered =
        m_connectServer.On(ConnectHelloOpcode, [this](const Network::Packet& packet) { OnConnectHello(packet); }) &&
        m_connectServer.On(ServerListOpcode, ServerListSubcode,
                           [this](const Network::Packet& packet) { OnServerList(packet); }) &&
        m_connectServer.On(ServerListOpcode, ServerSelectSubcode,
                           [this](const Network::Packet& packet) { OnServerAddress(packet); });
    assert(registered && "los handlers del ConnectServer se registran una vez");
}

void ServerSession::Start()
{
    m_failure.reset();
    m_gameServerAddress.reset();
    Connect(m_settings.connectServer);
    m_state = SessionState::ConnectingConnectServer;
}

// Lo que queda del lote después de un Connect o un Fail es de la conexión de antes, y lo que queda
// después de que un Send de un handler cerró la conexión, de una que ya no está: ese cierre sale en
// el Poll siguiente (Connection.h). Si la cerró el Poll mismo (el otro lado, un corte perdido), el
// Closed es lo último del lote y lo que llegó antes se despacha.
void ServerSession::Poll()
{
    const std::uint32_t generation = m_generation;
    const std::vector<Network::Event> events = m_connection.Poll();
    const bool closedByThePoll = m_connection.State() == Network::LinkState::Closed;
    for (const Network::Event& event : events)
    {
        if (std::holds_alternative<Network::Opened>(event))
        {
            OnOpened();
        }
        else if (const auto* packet = std::get_if<Network::Packet>(&event))
        {
            OnPacket(*packet);
        }
        else
        {
            OnClosed(std::get<Network::Closed>(event).reason);
        }
        const bool closedByAHandler = !closedByThePoll && m_connection.State() == Network::LinkState::Closed;
        if (m_generation != generation || closedByAHandler)
        {
            return;
        }
    }
}

SessionState ServerSession::State() const
{
    return m_state;
}

std::optional<SessionFailure> ServerSession::Failure() const
{
    return m_failure;
}

std::optional<ServerAddress> ServerSession::GameServer() const
{
    return m_gameServerAddress;
}

// Al CS, lo primero es el A9, como lo manda 0x415be0 apenas la escena de login conecta (Q-C16). El
// GS saluda primero (CL4.4): al abrirlo no sale nada.
void ServerSession::OnOpened()
{
    if (m_state == SessionState::ConnectingConnectServer)
    {
        m_log.Info("conectó con el ConnectServer");
        Protocol::Outgoing request = Protocol::Pet::WritePetInfoRequest(Protocol::Pet::LoginScenePetInfoRequest);
        m_connection.Send(std::move(request.packet), request.delivery);
        m_state = SessionState::WaitingConnectHello;
        return;
    }
    m_log.Info("conectó con el GameServer");
}

void ServerSession::OnPacket(const Network::Packet& packet)
{
    const bool withConnectServer = WithConnectServer();
    Protocol::Dispatcher& dispatcher = withConnectServer ? m_connectServer : m_gameServer;
    if (dispatcher.Dispatch(packet) == Protocol::DispatchResult::Malformed)
    {
        m_log.Warning("se descarta un paquete mal formado del {}", withConnectServer ? "ConnectServer" : "GameServer");
    }
}

void ServerSession::OnClosed(Network::CloseReason reason)
{
    const bool withConnectServer = WithConnectServer();
    m_log.Error("se perdió la conexión con el {}: {}", withConnectServer ? "ConnectServer" : "GameServer",
                Network::CloseReasonText(reason));
    Fail(withConnectServer ? SessionFailure::ConnectServerLost : SessionFailure::GameServerLost);
}

// 0x4e33e4 pide la lista con cada saludo: en 8 de 64 conexiones sale dos veces (04 § El flujo de
// conexión). Acá, una vez.
void ServerSession::OnConnectHello([[maybe_unused]] const Network::Packet& packet)
{
    if (m_state != SessionState::WaitingConnectHello)
    {
        m_log.Warning("se ignora el saludo del ConnectServer (00): {}", OutOfTurn);
        return;
    }
    m_log.Info("el ConnectServer saludó: se pide la lista de servidores");
    Protocol::Outgoing request = Protocol::Connection::WriteServerListRequest();
    m_connection.Send(std::move(request.packet), request.delivery);
    m_state = SessionState::WaitingServerList;
}

// Pregunta 9 del plan de la CL4: el primero de la lista, en el orden en que llega, hasta que la
// escena de login deje elegir (CL5.5).
void ServerSession::OnServerList(const Network::Packet& packet)
{
    if (m_state != SessionState::WaitingServerList)
    {
        m_log.Warning("se ignora la lista de servidores (F4 06): {}", OutOfTurn);
        return;
    }
    const auto list = Protocol::Connection::ReadServerList(packet);
    if (!list.HasValue())
    {
        m_log.Warning("se ignora la lista de servidores (F4 06): {}", Unreadable);
        return;
    }
    const std::vector<Protocol::Connection::ServerEntry>& entries = list.Value();
    if (entries.empty())
    {
        m_log.Error("la lista de servidores está vacía: no hay a qué GameServer conectarse");
        Fail(SessionFailure::EmptyServerList);
        return;
    }
    m_log.Info("la lista trae {} servidores: se elige el primero", entries.size());
    Protocol::Outgoing choice = Protocol::Connection::WriteServerSelect(entries.front().code);
    m_connection.Send(std::move(choice.packet), choice.delivery);
    m_state = SessionState::WaitingServerAddress;
}

// El original loguea la dirección, cierra el CS (0x4e8380) y conecta al GS (0x4c1340). Acá el log
// no la dice: es del servidor, como el host de la config.
void ServerSession::OnServerAddress(const Network::Packet& packet)
{
    if (m_state != SessionState::WaitingServerAddress)
    {
        m_log.Warning("se ignora la dirección del GameServer (F4 03): {}", OutOfTurn);
        return;
    }
    const auto address = Protocol::Connection::ReadServerAddress(packet);
    if (!address.HasValue())
    {
        m_log.Warning("se ignora la dirección del GameServer (F4 03): {}", Unreadable);
        return;
    }
    m_log.Info("el ConnectServer dio la dirección del GameServer: se cierra y se conecta a ese");
    m_gameServerAddress = ServerAddress{address.Value().host, address.Value().port};
    Connect(*m_gameServerAddress);
    m_state = SessionState::ConnectingGameServer;
}

void ServerSession::Connect(const ServerAddress& address)
{
    m_connection.Open(address.host, address.port);
    ++m_generation;
}

void ServerSession::Fail(SessionFailure failure)
{
    m_connection.Close();
    ++m_generation;
    m_failure = failure;
    m_state = SessionState::Failed;
}

bool ServerSession::WithConnectServer() const
{
    switch (m_state)
    {
    case SessionState::ConnectingConnectServer:
    case SessionState::WaitingConnectHello:
    case SessionState::WaitingServerList:
    case SessionState::WaitingServerAddress:
        return true;
    case SessionState::Idle:
    case SessionState::ConnectingGameServer:
    case SessionState::Failed:
        break;
    }
    return false;
}
} // namespace Client::App
