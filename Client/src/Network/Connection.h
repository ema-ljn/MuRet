#pragma once

#include "Core/Expected.h"
#include "Core/IClock.h"
#include "Core/Log.h"
#include "Network/CloseReason.h"
#include "Network/Crypto/CryptoError.h"
#include "Network/Crypto/SimpleModulus.h"
#include "Network/Delivery.h"
#include "Network/IObfuscation.h"
#include "Network/ISocket.h"
#include "Network/LinkState.h"
#include "Network/Packet.h"
#include "Network/PacketReader.h"
#include "Network/PacketSerials.h"
#include "Network/SendQueue.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <random>
#include <string_view>
#include <variant>
#include <vector>

// El camino de un paquete por una conexión con un servidor, en el orden del original. Al recibir:
// el socket, el corte con la máscara de la ofuscación (PacketReader), AfterCut, el descifrado de un
// C3/C4 y BeforeDispatch. Al mandar: el cifrado de lo que va Encrypted (con el BeforeEncrypt de la
// ofuscación), BeforeSend y la cola (SendQueue).
// Contrato: el plan de la CL4 (CL4.3 y CL4.4), "Contrato" e "Interfaces"; 03-red-y-cifrado.md,
// "Cadena de recepción" y "C3/C4", y el orden que dice IObfuscation.h.
//
// La consulta el bucle una vez por frame (Poll), en su hilo (D-C5), y nunca espera, salvo al
// resolver el nombre en Open (pregunta 5 del plan). Lo que pasa sale como eventos, en orden, y no
// se despacha acá: Network no ve a Protocol. Lo despacha quien la usa (la ServerSession).
//
// Las claves son de cada conexión: el GameServer las tiene, el ConnectServer no. Sin claves, un
// C3/C4 que llega es Undecryptable, y lo que va Encrypted no sale. Los seriales de los C3/C4 son
// de la conexión y vuelven a 0 en cada Open, como 0x4c1340 al conectar al GameServer.
//
// Un paquete malo no siempre corta. Un error del corte cierra con BadStream, porque se perdió dónde
// empieza el siguiente; un InvalidPacket de la ofuscación se descarta con un aviso y la conexión
// sigue, porque el largo lo dio la cabecera.
namespace Client::Network
{
// Pregunta 5: lo que se espera a que termine un connect. El original no tiene uno propio: su
// connect es bloqueante (0x4e8570).
inline constexpr Core::Duration ConnectTimeout = std::chrono::seconds{10};

// Lo que se lee por Poll: lecturas de 0x2000, el buffer de recv del original (0x4e88af), hasta que
// no haya más o hasta 8, para acotar el trabajo de un frame. El 8 es de MuRet.
inline constexpr std::size_t ReceiveChunkSize = 0x2000;
inline constexpr int MaxReadsPerPoll = 8;

// Pregunta 5: si resolver el host y empezar a conectar tarda más que esto, va al log. El resolver
// del sistema espera a getaddrinfo, y mientras tanto el juego no dibuja.
inline constexpr Core::Duration SlowConnectStart = std::chrono::milliseconds{100};

// Las claves de los C3/C4 con el GameServer: Enc1.dat para lo que sale y Dec2.dat para lo que
// llega (03 § C3/C4).
struct ConnectionKeys
{
    Crypto::EncryptionKey encryption;
    Crypto::DecryptionKey decryption;
};

// Lo que pasa en un Poll.
struct Opened // el connect terminó: va antes de lo que llegó en el mismo Poll
{
};
// Un C3/C4 que llegó y no se despacha: quien usa la conexión decide si manda el aviso F1 03 (el
// original lo manda con un checksum malo). El aviso al log ya lo dejó la conexión.
struct Undecryptable
{
    // Por qué no se descifró; std::nullopt si la conexión no tiene claves (la del ConnectServer).
    std::optional<Crypto::CryptoError> error;
};
struct Closed // siempre lo último del lote; el motivo también queda en LastCloseReason()
{
    CloseReason reason;
};
using Event = std::variant<Opened, Packet, Undecryptable, Closed>;

class Connection
{
public:
    // El socket, la ofuscación, el reloj y el log son de quien la arma y tienen que vivir más que
    // ella. Una ofuscación temporal no compila, porque moriría antes. `paddingSeed` siembra el
    // std::mt19937 del byte de relleno de lo que se cifra, que en el original sale de rand(): los
    // tests le dan uno fijo, y main.cpp le va a dar uno de std::random_device cuando arme la
    // conexión (la CL4.5 pone la sesión en el bucle).
    Connection(ISocket& socket, const IObfuscation& obfuscation, Core::IClock& clock, Core::Log& log,
               std::uint32_t paddingSeed);
    Connection(ISocket& socket, const IObfuscation&& obfuscation, Core::IClock& clock, Core::Log& log,
               std::uint32_t paddingSeed) = delete;

    // Guarda referencias: no se copia ni se mueve.
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;
    ~Connection() = default;

    // Cierra lo que haya, sin evento (lo pidió quien la usa); vacía el corte y la cola, vuelve los
    // seriales a 0, se queda con las claves (std::nullopt: sin cifrado, como con el ConnectServer)
    // y empieza a conectar. Si resolver y empezar a conectar tarda más que SlowConnectStart, lo
    // anota. Un connect que el socket ya terminó sale en el próximo Poll: el Opened, porque
    // PollConnect repite el resultado (ISocket.h), o el Closed, si falló. Network no ve a App: el
    // host y el puerto llegan sueltos.
    void Open(std::string_view host, std::uint16_t port, std::optional<ConnectionKeys> keys);

    // Una vez por frame: primero PollConnect y después el ConnectTimeout (un frame largo no corta
    // un connect que ya terminó), manda lo encolado, lee, corta, traduce y descifra. Lo que pasó,
    // en orden.
    // Quien recorre el lote tira lo que queda si a mitad llamó a Open o a Close, o si State() pasó
    // a Closed (un Send desde un handler la puede cerrar): es de una conexión que ya no está. Solo
    // con el socket conectado lee o manda: un Closed ahí es el otro lado que cerró.
    [[nodiscard]] std::vector<Event> Poll();

    // El XOR32 ya viene aplicado si lo lleva (Protocol). Lo que va Encrypted se cifra como C3/C4
    // con el serial que sigue y un byte de relleno del std::mt19937; si EncryptPacket falla, no
    // sale, con un aviso que dice el tipo, el opcode y el error, y la conexión sigue. Después pasa
    // por BeforeSend, se encola y, con la conexión abierta, sale lo que el socket tome: con algo
    // pendiente, lo nuevo va detrás, nunca directo. Mientras conecta espera en la cola hasta el
    // Opened. Si la cola se llena o el socket falla, la cierra, y el Closed sale en el próximo
    // Poll. Cerrada, se descarta con un aviso; lo que va Encrypted sin claves, también.
    void Send(Packet packet, Delivery delivery);

    // Cierra sin evento: lo pidió quien la usa, y LastCloseReason() da ByClient. Si ya estaba
    // cerrada, no hace nada.
    void Close();

    [[nodiscard]] LinkState State() const;

    // Por qué se cerró desde el último Open; std::nullopt si no se cerró.
    [[nodiscard]] std::optional<CloseReason> LastCloseReason() const;

private:
    void PollConnect(std::vector<Event>& events);

    // Manda lo encolado. false si cerró.
    bool FlushQueue();

    // Lee hasta MaxReadsPerPoll veces y agrega lo que se cortó.
    void ReadAvailable(std::vector<Event>& events);

    // Saca del corte los paquetes enteros, traducidos. false si el corte falló y cerró.
    bool TakePackets(std::vector<Event>& events);

    // AfterCut, el descifrado si es un C3/C4 y BeforeDispatch: el Packet que se despacha, o el
    // Undecryptable de un C3/C4 que no. std::nullopt si se descarta, con su aviso.
    std::optional<Event> Translate(Packet packet);

    // Descifra un C3/C4 que llegó: el C1/C2 rearmado, o por qué no, con su aviso.
    Core::Expected<Packet, Undecryptable> Decrypt(const Packet& encrypted);

    // El byte de relleno del próximo paquete cifrado.
    std::byte NextPadding();

    // Cierra el socket, vacía el corte y la cola, y lo anota en el log. El Closed sale en el Poll
    // que sigue, o al final de este, salvo con ByClient.
    void CloseWith(CloseReason reason);

    ISocket& m_socket;
    const IObfuscation& m_obfuscation;
    Core::IClock& m_clock;
    Core::Log& m_log;

    LinkState m_state = LinkState::Closed;
    Core::Duration m_connectStartedAt{};
    std::optional<CloseReason> m_lastCloseReason;
    std::optional<CloseReason> m_closeToReport; // el Closed que todavía no salió en un Poll
    PacketReader m_reader;
    SendQueue m_queue;
    std::optional<ConnectionKeys> m_keys;
    PacketSerials m_serials;
    std::mt19937 m_padding;
    std::array<std::byte, ReceiveChunkSize> m_receiveBuffer{};
};
} // namespace Client::Network
