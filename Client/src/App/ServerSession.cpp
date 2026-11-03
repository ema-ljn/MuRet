#include "App/ServerSession.h"

#include "App/FaithfulPacer.h"
#include "Network/Crypto/CryptoError.h"
#include "Network/LinkState.h"
#include "Protocol/BuildConstants.h"
#include "Protocol/Connection/Hello.h"
#include "Protocol/Connection/Login.h"
#include "Protocol/Connection/Opcodes.h"
#include "Protocol/Connection/ServerList.h"
#include "Protocol/Outgoing.h"
#include "Protocol/Pet/PetInfo.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <string>
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

// Un visitor de std::visit con un operator() por tipo: si Network::Event suma un tipo, Poll no
// compila hasta que la sesión diga qué hace con él.
template <typename... Handlers> struct Overloaded : Handlers...
{
    using Handlers::operator()...;
};
template <typename... Handlers> Overloaded(Handlers...) -> Overloaded<Handlers...>;

// La versión del saludo como texto para el log: viene del servidor, y un byte que no se ve (un \0,
// un salto de línea) sale como '?', así la línea sigue siendo una.
std::string VersionText(const std::array<char, Protocol::VersionSize>& version)
{
    std::string text(version.begin(), version.end());
    std::ranges::replace_if(text, [](char value) { return value < ' ' || value > '~'; }, '?');
    return text;
}

// Los errores que se contestan con el F1 03: el checksum malo, como el original, y lo que no mide
// bloques enteros, como contesta probablemente el original (03, "C3/C4").
bool DeservesTheReport(const Network::Undecryptable& undecryptable)
{
    return undecryptable.error == Network::Crypto::CryptoError::ChecksumMismatch ||
           undecryptable.error == Network::Crypto::CryptoError::NotWholeBlocks;
}
} // namespace

ServerSession::ServerSession(Network::Connection& connection, Core::IClock& clock, Core::Log& log,
                             Network::ConnectionKeys keys, SessionSettings settings)
    : m_connection(connection), m_clock(clock), m_log(log), m_keys(keys), m_settings(std::move(settings)),
      m_connectServer(log), m_gameServer(log)
{
    RegisterConnectServerHandlers();
    RegisterGameServerHandlers();
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

void ServerSession::RegisterGameServerHandlers()
{
    using Protocol::Connection::HelloSubcode;
    using Protocol::Connection::JoinOpcode;
    using Protocol::Connection::LoginSubcode;
    [[maybe_unused]] const bool registered =
        m_gameServer.On(JoinOpcode, HelloSubcode, [this](const Network::Packet& packet) { OnGameHello(packet); }) &&
        m_gameServer.On(JoinOpcode, LoginSubcode, [this](const Network::Packet& packet) { OnLoginResult(packet); });
    assert(registered && "los handlers del GameServer se registran una vez");
}

void ServerSession::Start()
{
    m_failure.reset();
    m_gameServerAddress.reset();
    m_loginResult.reset();
    Connect(m_settings.connectServer, std::nullopt);
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
        std::visit(
            Overloaded{
                [this](const Network::Opened&) { OnOpened(); },
                [this](const Network::Packet& packet) { OnPacket(packet); },
                [this](const Network::Undecryptable& undecryptable) { OnUndecryptable(undecryptable); },
                [this](const Network::Closed& closed) { OnClosed(closed.reason); },
            },
            event);
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

std::optional<std::uint8_t> ServerSession::LoginResult() const
{
    return m_loginResult;
}

// Al CS, lo primero es el A9, como lo manda 0x415be0 apenas la escena de login conecta (Q-C16). El
// GS saluda primero: al abrirlo no sale nada.
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
    m_log.Info("conectó con el GameServer: se espera su saludo");
    m_state = SessionState::WaitingGameHello;
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

// El aviso ya lo dejó la conexión. Sin claves (el CS) no hay error, y no se contesta.
void ServerSession::OnUndecryptable(const Network::Undecryptable& undecryptable)
{
    if (!DeservesTheReport(undecryptable))
    {
        return;
    }
    m_log.Info("un C3/C4 del GameServer no se descifró: se le avisa al GameServer (F1 03)");
    Protocol::Outgoing report = Protocol::Connection::WriteDecryptErrorReport();
    m_connection.Send(std::move(report.packet), report.delivery);
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
    Connect(*m_gameServerAddress, m_keys);
    m_state = SessionState::ConnectingGameServer;
}

// 0x4c1e20 muestra "Connectting error" con otro resultado y "Version dismatch" con otra versión,
// aunque el resultado tampoco sea 1. Acá el resultado se mira primero: un saludo rechazado no
// llega a comparar la versión.
void ServerSession::OnGameHello(const Network::Packet& packet)
{
    if (m_state != SessionState::WaitingGameHello)
    {
        m_log.Warning("se ignora el saludo del GameServer (F1 00): {}", OutOfTurn);
        return;
    }
    const auto hello = Protocol::Connection::ReadServerHello(packet);
    if (!hello.HasValue())
    {
        m_log.Warning("se ignora el saludo del GameServer (F1 00): {}", Unreadable);
        return;
    }
    if (hello.Value().result != Protocol::Connection::HelloAccepted)
    {
        m_log.Error("el GameServer rechazó la conexión (resultado {})", hello.Value().result);
        Fail(SessionFailure::HelloRejected);
        return;
    }
    const std::string_view version(hello.Value().version.data(), hello.Value().version.size());
    if (version != Protocol::ClientVersion)
    {
        m_log.Error("el GameServer tiene la versión {} y este cliente la {}: no se manda el login",
                    VersionText(hello.Value().version), Protocol::ClientVersion);
        Fail(SessionFailure::VersionMismatch);
        return;
    }
    SendLogin();
}

// El tick es el de GetTickCount en el original (0x641bf0): el del reloj del bucle en este momento.
// Ni la cuenta ni la clave van al log, tampoco cuando no entran.
void ServerSession::SendLogin()
{
    const Protocol::Connection::LoginRequest request{m_settings.credentials.account, m_settings.credentials.password,
                                                     TickCountMs(m_clock.Now())};
    auto login = Protocol::Connection::WriteLoginRequest(request);
    if (!login.HasValue())
    {
        m_log.Error("no se puede escribir el login: la cuenta o la clave pasan de {} bytes",
                    Protocol::Connection::CredentialSize);
        Fail(SessionFailure::CredentialsTooLong);
        return;
    }
    m_log.Info("el GameServer saludó: se manda el login");
    Protocol::Outgoing outgoing = std::move(login).Value();
    m_connection.Send(std::move(outgoing.packet), outgoing.delivery);
    m_state = SessionState::WaitingLoginResult;
}

void ServerSession::OnLoginResult(const Network::Packet& packet)
{
    if (m_state != SessionState::WaitingLoginResult)
    {
        m_log.Warning("se ignora el resultado del login (F1 01): {}", OutOfTurn);
        return;
    }
    const auto result = Protocol::Connection::ReadLoginResult(packet);
    if (!result.HasValue())
    {
        m_log.Warning("se ignora el resultado del login (F1 01): {}", Unreadable);
        return;
    }
    m_loginResult = result.Value();
    if (result.Value() != Protocol::Connection::LoginAccepted)
    {
        m_log.Error("el GameServer rechazó el login (resultado {})", result.Value());
        Fail(SessionFailure::LoginRejected);
        return;
    }
    m_log.Info("el GameServer aceptó el login");
    m_state = SessionState::LoggedIn;
}

// El CS va sin claves y el GS con las suyas: cada Open vuelve los seriales a 0.
void ServerSession::Connect(const ServerAddress& address, const std::optional<Network::ConnectionKeys>& keys)
{
    m_connection.Open(address.host, address.port, keys);
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
    case SessionState::WaitingGameHello:
    case SessionState::WaitingLoginResult:
    case SessionState::LoggedIn:
    case SessionState::Failed:
        break;
    }
    return false;
}
} // namespace Client::App
