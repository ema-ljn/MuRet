#include "Network/Connection.h"

#include "Core/Expected.h"
#include "Network/ObfuscationError.h"
#include "Network/PacketError.h"
#include "Network/SocketError.h"

#include <algorithm>
#include <chrono>
#include <format>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace Client::Network
{
namespace
{
// Lo que un error del socket dice del cierre. Closed es el otro lado: la conexión solo lee y manda
// con el socket conectado (ISocket.h).
CloseReason ReasonFor(SocketError error)
{
    switch (error)
    {
    case SocketError::ResolveFailed:
        return CloseReason::ResolveFailed;
    case SocketError::Refused:
        return CloseReason::Refused;
    case SocketError::Closed:
        return CloseReason::PeerClosed;
    case SocketError::Failed:
        break;
    }
    return CloseReason::SocketFailed;
}

// Por qué se descarta un paquete que llegó.
constexpr std::string_view NotTranslated = "la ofuscación no lo traduce";
constexpr std::string_view NotDecrypted = "esta conexión no descifra";

bool IsEncrypted(PacketType type)
{
    return type == PacketType::C3 || type == PacketType::C4;
}

// Un paquete para el log, sin sus datos: el tipo y, si va en claro, el opcode ("C1 F4", "C3").
std::string KindOf(PacketType type, std::optional<std::uint8_t> opcode)
{
    const auto typeByte = static_cast<std::uint8_t>(type);
    if (!opcode)
    {
        return std::format("{:02X}", typeByte);
    }
    return std::format("{:02X} {:02X}", typeByte, *opcode);
}

std::string KindOf(const Packet& packet)
{
    return KindOf(packet.Type(), packet.Opcode());
}
} // namespace

Connection::Connection(ISocket& socket, const IObfuscation& obfuscation, Core::IClock& clock, Core::Log& log)
    : m_socket(socket), m_obfuscation(obfuscation), m_clock(clock), m_log(log), m_reader(obfuscation)
{
}

void Connection::Open(std::string_view host, std::uint16_t port)
{
    // Cerrada, el corte y la cola ya están vacíos: los vacía CloseWith, y Send no encola.
    if (m_state != LinkState::Closed)
    {
        CloseWith(CloseReason::ByClient);
    }
    m_lastCloseReason.reset();
    m_closeToReport.reset();

    const Core::Duration before = m_clock.Now();
    const Core::Expected<ConnectProgress, SocketError> started = m_socket.StartConnect(host, port);
    m_connectStartedAt = m_clock.Now();
    const Core::Duration took = m_connectStartedAt - before;
    if (took > SlowConnectStart)
    {
        m_log.Warning("resolver el host y empezar a conectar tardó {} ms, y el juego esperó",
                      std::chrono::duration_cast<std::chrono::milliseconds>(took).count());
    }
    if (!started.HasValue())
    {
        CloseWith(ReasonFor(started.Error()));
        return;
    }
    m_state = LinkState::Connecting;
}

std::vector<Event> Connection::Poll()
{
    std::vector<Event> events;
    if (m_state == LinkState::Connecting)
    {
        PollConnect(events);
    }
    if (m_state == LinkState::Open && FlushQueue())
    {
        ReadAvailable(events);
    }
    if (m_closeToReport)
    {
        events.emplace_back(Closed{*m_closeToReport});
        m_closeToReport.reset();
    }
    return events;
}

void Connection::Send(Packet packet, Delivery delivery)
{
    if (m_state == LinkState::Closed)
    {
        m_log.Warning("se descarta un {} que iba a salir: la conexión está cerrada", KindOf(packet));
        return;
    }
    if (delivery == Delivery::Encrypted)
    {
        m_log.Warning("se descarta un {} que va cifrado: esta conexión no cifra", KindOf(packet));
        return;
    }
    const Packet wire = m_obfuscation.BeforeSend(std::move(packet));
    if (m_queue.Push(wire.Bytes()).has_value())
    {
        CloseWith(CloseReason::SendQueueFull);
        return;
    }
    if (m_state == LinkState::Open)
    {
        FlushQueue();
    }
}

void Connection::Close()
{
    if (m_state == LinkState::Closed)
    {
        return;
    }
    CloseWith(CloseReason::ByClient);
}

LinkState Connection::State() const
{
    return m_state;
}

std::optional<CloseReason> Connection::LastCloseReason() const
{
    return m_lastCloseReason;
}

// Primero si terminó y después el tope: un frame largo no corta un connect que ya terminó.
void Connection::PollConnect(std::vector<Event>& events)
{
    const Core::Expected<ConnectProgress, SocketError> progress = m_socket.PollConnect();
    if (!progress.HasValue())
    {
        CloseWith(ReasonFor(progress.Error()));
        return;
    }
    if (progress.Value() == ConnectProgress::Connected)
    {
        m_state = LinkState::Open;
        events.emplace_back(Opened{});
        return;
    }
    if (m_clock.Now() - m_connectStartedAt >= ConnectTimeout)
    {
        CloseWith(CloseReason::ConnectTimedOut);
    }
}

// Flush solo falla con Socket (Full es de Push), y el error ya quedó en LastSocketError(). Si no
// estuviera, sería un bug de la cola: se toma como una falla del socket, sin decir que cerró el
// otro lado.
bool Connection::FlushQueue()
{
    if (!m_queue.Flush(m_socket).has_value())
    {
        return true;
    }
    CloseWith(ReasonFor(m_queue.LastSocketError().value_or(SocketError::Failed)));
    return false;
}

// Hasta que el socket no tenga nada más, hasta MaxReadsPerPoll lecturas, o hasta que cierre. Lo que
// se cortó antes de un cierre ya está en el lote: el Closed va después.
void Connection::ReadAvailable(std::vector<Event>& events)
{
    for (int read = 0; read < MaxReadsPerPoll; ++read)
    {
        const Core::Expected<std::size_t, SocketError> received = m_socket.Receive(m_receiveBuffer);
        if (!received.HasValue())
        {
            CloseWith(ReasonFor(received.Error()));
            return;
        }
        if (received.Value() == 0)
        {
            return;
        }
        // Un socket no da más de lo que entra en el buffer; el min es para que uno roto no lea
        // afuera.
        const std::size_t size = std::min(received.Value(), m_receiveBuffer.size());
        m_reader.Feed(std::span<const std::byte>(m_receiveBuffer).first(size));
        if (!TakePackets(events))
        {
            return;
        }
    }
}

bool Connection::TakePackets(std::vector<Event>& events)
{
    while (true)
    {
        Core::Expected<std::optional<Packet>, PacketError> next = m_reader.Next();
        if (!next.HasValue())
        {
            CloseWith(CloseReason::BadStream);
            return false;
        }
        std::optional<Packet> packet = std::move(next).Value();
        if (!packet)
        {
            return true;
        }
        if (std::optional<Packet> translated = Translate(std::move(*packet)))
        {
            events.emplace_back(std::move(*translated));
        }
    }
}

// El aviso dice lo que llegó del cable, el tipo y el opcode: se guardan antes de pasarle el paquete
// a la ofuscación, que se queda con él.
std::optional<Packet> Connection::Translate(Packet packet)
{
    const PacketType type = packet.Type();
    const std::optional<std::uint8_t> opcode = packet.Opcode();
    Core::Expected<Packet, ObfuscationError> cut = m_obfuscation.AfterCut(std::move(packet));
    if (!cut.HasValue())
    {
        m_log.Warning("se descarta un {} que llegó: {}", KindOf(type, opcode), NotTranslated);
        return std::nullopt;
    }
    if (IsEncrypted(cut.Value().Type()))
    {
        m_log.Warning("se descarta un {} que llegó: {}", KindOf(type, opcode), NotDecrypted);
        return std::nullopt;
    }
    Core::Expected<Packet, ObfuscationError> ready = m_obfuscation.BeforeDispatch(std::move(cut).Value());
    if (!ready.HasValue())
    {
        m_log.Warning("se descarta un {} que llegó: {}", KindOf(type, opcode), NotTranslated);
        return std::nullopt;
    }
    return std::move(ready).Value();
}

// Solo la conexión ve el socket, así que el error del sistema de un SocketFailed lo anota ella,
// antes de cerrarlo.
void Connection::CloseWith(CloseReason reason)
{
    if (reason == CloseReason::SocketFailed)
    {
        m_log.Warning("la conexión se cerró: {} (error {} del sistema)", CloseReasonText(reason),
                      m_socket.LastSystemError());
    }
    else if (reason == CloseReason::ByClient)
    {
        m_log.Info("la conexión se cerró: {}", CloseReasonText(reason));
    }
    else
    {
        m_log.Warning("la conexión se cerró: {}", CloseReasonText(reason));
    }
    m_socket.Close();
    m_reader = PacketReader(m_obfuscation);
    m_queue.Clear();
    m_state = LinkState::Closed;
    m_lastCloseReason = reason;
    if (reason != CloseReason::ByClient)
    {
        m_closeToReport = reason;
    }
}
} // namespace Client::Network
